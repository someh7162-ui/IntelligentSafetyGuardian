"""Build enterprise firmware. Never connects to hardware or embeds default live credentials."""
from pathlib import Path
import argparse,hashlib,json,subprocess,urllib.parse,re,shutil,os
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--config',type=Path)
parser.add_argument('--toolchain',type=Path,help='Directory containing arm-none-eabi-gcc')
parser.add_argument('--out',type=Path,default=Path(__file__).resolve().parent/'build')
args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
tool=args.toolchain
if tool is None:
    found=shutil.which('arm-none-eabi-gcc')
    if not found:raise SystemExit('Set --toolchain to the Arm GNU bin directory, or add it to PATH')
    tool=Path(found).parent
exe='.exe' if os.name=='nt' else ''
flags=['-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard','-DUSE_HAL_DRIVER','-DSTM32F407xx','-DRG_ENTERPRISE_BUILD','-Og','-g3','-ffunction-sections','-fdata-sections']
if args.config:
    cfg=json.loads(args.config.read_text(encoding='utf-8-sig'));base=cfg['apiBase'].rstrip('/');url=urllib.parse.urlsplit(base)
    if url.scheme!='https' or not url.hostname or url.username or url.password or url.query or url.fragment or any(c in base for c in '\r\n"'):raise ValueError('Expected a public HTTPS device API base without credentials')
    if not re.fullmatch('[0-9a-f]{64}',cfg['token']) or not re.fullmatch('[A-Za-z0-9_-]{1,64}',cfg['deviceId']):raise ValueError('Invalid device identity or token')
    apn=cfg.get('apn','')
    if not re.fullmatch(r'[A-Za-z0-9._-]{0,80}',apn):raise ValueError('Invalid APN')
    cert=Path(cfg['caCertificate']).read_text(encoding='ascii')
    if not cert.startswith('-----BEGIN CERTIFICATE-----') or len(cert)>4096:raise ValueError('Expected a PEM root certificate')
    storage=cfg.get('storage',{});provision=False;backup_hash='';uid=''
    if storage.get('enableWrites'):
        backup=Path(storage['backup']);data=backup.read_bytes();uid=storage['mcuUid'].lower()
        if len(data)!=1048576 or not re.fullmatch('[0-9a-f]{24}',uid):raise ValueError('Expected a complete 1MiB backup and matching MCU UID')
        backup_hash=hashlib.sha256(data).hexdigest()
        if backup_hash!=storage['backupSha256'].lower():raise ValueError('Backup digest does not match; writes remain disabled')
        # First provision only accepts confirmed erased storage. Existing owned journals are resumed by the generated build.
        if data!=b'\xff'*len(data):raise ValueError('Reserved region contains data; refusing write provisioning or automatic erasure')
        provision=True
    q=lambda s:json.dumps(s,ensure_ascii=True)
    header='\n'.join([f'#define RG_DEVICE_ID {q(cfg["deviceId"])}',f'#define RG_API_BASE {q(base)}',f'#define RG_API_HOST {q(url.hostname)}',f'#define RG_DEVICE_TOKEN {q(cfg["token"])}','#define RG_CELLULAR_ENABLED 1',f'#define RG_APN {q(apn)}',f'#define RG_STORAGE_PROVISIONED {int(provision)}',f'#define RG_STORAGE_UID {q(uid)}',f'#define RG_STORAGE_BACKUP_SHA256 {q(backup_hash)}',f'static const char RG_CA_CERT[]={q(cert)};'])
    (out/'enterprise_private.h').write_text(header,encoding='ascii');flags+=['-DRG_PRIVATE_CONFIG','-I'+str(out)]
incs=['Core/Inc','Drivers/STM32F4xx_HAL_Driver/Inc','Drivers/STM32F4xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32F4xx/Include','Drivers/CMSIS/Include','USB_DEVICE/App','USB_DEVICE/Target','Middlewares/ST/STM32_USB_Device_Library/Core/Inc','Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc']
flags+=['-I'+str(ROOT/p) for p in incs]
sources=[ROOT/name for name in json.loads((Path(__file__).parent/'sources.json').read_text())]
sources+=[ROOT/'Core/Src'/f'{n}.c' for n in ['rg_gps','rg_supervision','rg_journal','rg_json','rg_enterprise_codec','rg_cellular','rg_w25q']]
sources=list(dict.fromkeys(sources));objects=[]
for i,source in enumerate(sources):
    obj=out/f'{i:02d}_{source.stem}.o';objects.append(str(obj))
    extra=['-x','assembler-with-cpp'] if source.suffix=='.s' else ['-std=gnu11','-Wall','-Wextra','-Werror','--specs=nano.specs']
    if source.is_relative_to(ROOT/'Drivers') or source.is_relative_to(ROOT/'Middlewares'):extra=[v for v in extra if v!='-Werror']
    subprocess.run([str(tool/('arm-none-eabi-gcc'+exe)),*flags,*extra,'-c',str(source),'-o',str(obj)],check=True)
elf=out/'RiderGuard_Enterprise.elf';binary=elf.with_suffix('.bin')
subprocess.run([str(tool/('arm-none-eabi-gcc'+exe)),*objects,*flags[:4],'-T'+str(ROOT/'STM32F407VETX_FLASH.ld'),'--specs=nosys.specs','--specs=nano.specs','-Wl,--gc-sections','-Wl,-u,_printf_float','-Wl,-Map='+str(elf.with_suffix('.map')),'-static','-Wl,--start-group','-lc','-lm','-Wl,--end-group','-o',str(elf)],check=True)
subprocess.run([str(tool/('arm-none-eabi-objcopy'+exe)),'-O','binary',str(elf),str(binary)],check=True)
subprocess.run([str(tool/('arm-none-eabi-size'+exe)),str(elf)],check=True)
manifest={'name':'RiderGuard_Enterprise','binary':str(binary),'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'provisioned':bool(args.config),'storageWrites':bool(args.config and provision),'hardwareFlashed':False,'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8');print('Built enterprise firmware; no device was flashed.')

# STM32F407企业监管固件

最新实机基线为2026年11月11日的`2.0.2-local-vga`，OV2640输出640×480 JPEG；原`2.0.1-local`版本保留。这是不携带设备凭证的本机联调构建，4G业务上传与外置Flash写入默认关闭；不是完整道路验收版本。

## 已有实现

`Core/Src/enterprise_app.inc`调度约100Hz IMU采样、非阻塞相机轮询、语音、事件检测、蜂窝任务和日志任务。急刹采用初始3m/s²持续200ms规则；超速连续3秒触发，限速内持续5秒结束，10分钟累计3次才释放上报。无有效限速或GNSS失效时不判断超速。试验路口只生成待人工复核记录。

静止标定估计重力和陀螺仪偏置；安装方向默认+X，需在固定安装后核对。I2C通信失败可恢复专用MPU总线。未开放持久存储时，事件照片仅暂留RAM，不保证断电保存。

## 构建

安装Python 3.9以上与适配STM32的Arm GNU工具链。将工具链bin目录加入PATH后，在本目录执行：

```powershell
python IntegrationWork/enterprise/build_firmware.py
```

也可显式指定目录：

```powershell
python IntegrationWork/enterprise/build_firmware.py --toolchain "C:/toolchains/arm-gnu/bin"
```

构建器使用随仓库提供的`sources.json`，不依赖本机CubeIDE生成的`Debug/objects.list`。输出位于`IntegrationWork/enterprise/build/`，不会自动连接或烧录硬件。CubeIDE普通构建未定义`RG_ENTERPRISE_BUILD`时仍走诊断入口；企业版以此构建器为准。

私有配置通过`--config`加载，禁止提交真实配置或生成的`enterprise_private.h`。只有核对外置存储型号、备份摘要、MCU身份和确认空白保留区后才允许开放写入。不得为通过检查擦除未知数据。

## 主机测试

在本目录执行，`gcc`为本机C编译器，而不是Arm交叉编译器：

```powershell
gcc -std=c11 -Wall -Wextra -Werror -ICore/Inc IntegrationWork/enterprise/test_engine.c Core/Src/rg_supervision.c Core/Src/rg_gps.c -lm -o test_engine.exe
./test_engine.exe
gcc -std=c11 -Wall -Wextra -Werror -ICore/Inc IntegrationWork/enterprise/test_journal.c Core/Src/rg_journal.c -o test_journal.exe
./test_journal.exe
```

覆盖超速计次、窗口、失效定位、急刹、颠簸、转弯、静止偏置、运动中拒绝标定、日志掉电及未知数据保护。主机通过不代表道路效果已经验证。

## 实机记录与边界

2026年11月11日：VGA版本写入校验与独立回读比对通过，连续3张照片实际解码尺寸均为640×480，大小为16558、16557、16200字节，摄像头错误为0；采集期间IMU约100.02Hz，无新增IMU错误或采样迟到。详见[分辨率验证记录](../../../docs/validation/20261111_OV2640分辨率验证.md)和[新版本摘要](releases/2.0.2-local-vga/verification.json)。此结果不代表更高分辨率、道路识别或持久存储已验证。

本机拍照入口为`enterprise_diag.camera_command`：调试器写入1请求一张照片，2表示正在采集，0表示空闲；`camera_result`为1表示成功、2表示失败。只有核对当前固件与ELF一致后才能按符号访问，不能沿用旧版本内存地址。该入口既不上传照片，也不制造急刹/违规事件；照片暂留RAM，可由调试器按`camera_frame`回读。`read_command`仍只用于原有外置存储只读备份。

已发布的实际烧录文件及无凭证验证摘要在[releases/2.0.1-local](releases/2.0.1-local/)。写入校验和独立回读均通过；约6.1秒窗口实测99.85Hz，标定完成、摄像头PID为0x2642。启动有1次IMU错误，后续观察窗口未增加。

GPS正在接收但当时未定位；外置存储ID读为0xFFFFFF；语音尚未验收；4G业务关闭。当前USB诊断状态不能代替完整的企业遥测/事件/图片协议验收。详细步骤见[后续执行方案](../../../docs/plans/企业监管后续执行方案_20261110.md)。

既有接线：GNSS UART4 PA0/PA1；4G USART3 PD8/PD9；语音UART5 PC12/PD2；MPU6050 I2C1 PB8/PB9。连接以实际板型原理图为准，旧PC10/PC11记录不能直接用于本版本。

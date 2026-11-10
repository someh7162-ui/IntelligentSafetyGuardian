param([switch]$NoBrowser, [switch]$Rebuild, [switch]$Stop)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtime = Join-Path $projectRoot 'deploy\local-runtime'
$backend = Join-Path $projectRoot 'admin\ruoyi\backend'
$frontend = Join-Path $projectRoot 'admin\ruoyi\frontend'
$gateway = Join-Path $projectRoot 'device\gateway'
$stateFile = Join-Path $runtime 'local-services.json'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$state = if (Test-Path -LiteralPath $stateFile) { Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json } else { $null }

function Test-Port([int]$port) {
    $client = [Net.Sockets.TcpClient]::new()
    try { return $client.ConnectAsync('127.0.0.1', $port).Wait(400) -and $client.Connected }
    catch { return $false } finally { $client.Dispose() }
}

function Test-Url([string]$url) {
    try { return (Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 3).StatusCode -eq 200 }
    catch { return $false }
}

function Stop-Owned($record) {
    if (-not $record) { return }
    $process = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$record.pid) -ErrorAction SilentlyContinue
    $sameInstance = $process -and $record.startTicks -and $process.CreationDate.ToUniversalTime().Ticks -eq [long]$record.startTicks
    if ($process -and $process.CommandLine -and ($sameInstance -or $process.CommandLine.Contains($projectRoot)) -and $process.CommandLine.Contains([string]$record.marker)) {
        Stop-Process -Id ([int]$record.pid) -ErrorAction Stop
        for ($i=0; $i -lt 20; $i++) { if (-not (Get-Process -Id ([int]$record.pid) -ErrorAction SilentlyContinue)) { break }; Start-Sleep -Milliseconds 100 }
    }
}

if ($Stop) {
    if ($state) { Stop-Owned $state.frontend; Stop-Owned $state.backend; Stop-Owned $state.ai }
    Write-Host 'RiderGuard application services stopped. Local MySQL/Redis were preserved.'
    exit 0
}

function Start-Hidden([string]$name, [string]$executable, [string[]]$arguments, [string]$directory, [string]$marker) {
    $quoted = ($arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
    $process = Start-Process -FilePath $executable -ArgumentList $quoted -WorkingDirectory $directory -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $runtime ($name + '-stdout.log')) -RedirectStandardError (Join-Path $runtime ($name + '-stderr.log'))
    $started = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $process.Id)
    return [ordered]@{ pid=$process.Id; marker=$marker; startTicks=$started.CreationDate.ToUniversalTime().Ticks }
}

function Wait-Url([string]$name, [string]$url, [int]$seconds) {
    $until = [DateTime]::UtcNow.AddSeconds($seconds)
    while ([DateTime]::UtcNow -lt $until) {
        if (Test-Url $url) { return }
        Start-Sleep -Milliseconds 800
    }
    throw "$name did not become ready. Check deploy\local-runtime\*-stderr.log and *-stdout.log."
}

$java = (Get-Command java.exe -ErrorAction Stop).Source
$node = (Get-Command node.exe -ErrorAction Stop).Source
$python = (Get-Command python.exe -ErrorAction Stop).Source
$env:JAVA_HOME = Split-Path (Split-Path $java)
if (-not (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin\javac.exe'))) { throw 'JDK 21 is required.' }

Write-Host '[1/5] Preparing the isolated USB gateway...'
$gatewayPython = Join-Path $gateway '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $gatewayPython)) {
    & $python -m venv (Join-Path $gateway '.venv')
    if ($LASTEXITCODE) { throw 'Could not create the USB gateway virtual environment.' }
}
& $gatewayPython -c "import importlib.util,sys;sys.exit(0 if importlib.util.find_spec('serial') else 1)"
if ($LASTEXITCODE) {
    & $gatewayPython -m pip install --disable-pip-version-check -r (Join-Path $gateway 'requirements.txt')
    if ($LASTEXITCODE) { throw 'Could not install USB gateway dependencies.' }
}
$env:RIDERGUARD_LOCAL_TEST_ENABLED = 'true'
$env:RIDERGUARD_GATEWAY_PYTHON = $gatewayPython
$env:RIDERGUARD_USB_GATEWAY_SCRIPT = Join-Path $gateway 'local_gateway.py'
$env:RIDERGUARD_IMAGE_DIR = Join-Path $runtime 'uploads'
$env:RIDERGUARD_AMAP_SECURITY_FILE = Join-Path $runtime 'amap-security-code.txt'
if (-not $env:RIDERGUARD_INFERENCE_MODE) { $env:RIDERGUARD_INFERENCE_MODE = 'mock' }
$env:RIDERGUARD_DEMO_MODE = if ($env:RIDERGUARD_INFERENCE_MODE -eq 'mock') { 'true' } else { 'false' }

Write-Host '[2/5] Preparing local database services...'
& (Join-Path $PSScriptRoot 'start_local_services.ps1')

Write-Host '[3/5] Building/reusing the current backend...'
$fingerprints = Get-ChildItem -LiteralPath $backend -Recurse -File | Where-Object { $_.FullName -notmatch '\\target\\' -and $_.Extension -in '.java','.xml','.yml','.yaml','.properties' } | Sort-Object FullName | ForEach-Object { $_.FullName + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
$digest = [Security.Cryptography.SHA256]::Create()
try { $sourceHash = ([BitConverter]::ToString($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes(($fingerprints -join "`n"))))).Replace('-','') } finally { $digest.Dispose() }
$port = 8080
$reuseBackend = $state -and -not $Rebuild -and $state.sourceHash -eq $sourceHash -and (Test-Url ('http://127.0.0.1:' + $state.backendPort + '/auth/code'))
if ($reuseBackend) { $port = [int]$state.backendPort; $backendRecord = $state.backend }
else {
    if ($state) { Stop-Owned $state.backend }
    if (Test-Port $port) { $port = 18080 }
    if (Test-Port $port) { throw 'Both backend ports are occupied. Stop the old project backend and retry.' }
    $buildName = 'ruoyi-admin-local-' + (Get-Date -Format 'yyyyMMddHHmmss')
    $jar = Join-Path $backend ('ruoyi-admin\target\' + $buildName + '.jar')
    Push-Location $backend
    try {
        & (Join-Path $backend 'mvnw.cmd') ('-Dmaven.repo.local=' + (Join-Path $runtime 'maven-repository')) ('-Driderguard.build.name=' + $buildName) -DskipTests -Pdev -pl ruoyi-admin -am package
        if ($LASTEXITCODE) { throw 'Backend build failed.' }
    } finally { Pop-Location }
    $backendRecord = Start-Hidden 'backend' $java @('-jar',$jar,'--spring.profiles.active=dev',('--server.port='+$port),'--server.address=127.0.0.1') $projectRoot $jar
}

Write-Host '[4/5] Preparing local inference service...'
$aiRecord = if ($state) { $state.ai } else { $null }
if (-not (Test-Url 'http://127.0.0.1:8091/health')) {
    if (Test-Port 8091) { throw 'Port 8091 is occupied by an unresponsive service.' }
    $aiRecord = Start-Hidden 'ai' $python @('-m','uvicorn','ai.server:app','--app-dir',$projectRoot,'--host','127.0.0.1','--port','8091') $projectRoot 'ai.server:app'
} elseif ($aiRecord -and -not $aiRecord.startTicks) {
    # Upgrade this launcher's earlier process record without trusting a reused PID.
    $previous = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$aiRecord.pid) -ErrorAction SilentlyContinue
    if ($previous -and $previous.CommandLine.Contains('ai.server:app') -and $previous.ExecutablePath -eq $python -and $previous.CreationDate.ToUniversalTime() -le (Get-Item -LiteralPath $stateFile).LastWriteTimeUtc) {
        $aiRecord | Add-Member -NotePropertyName startTicks -NotePropertyValue $previous.CreationDate.ToUniversalTime().Ticks -Force
    }
}

Write-Host '[5/5] Preparing the management website...'
$vite = Join-Path $frontend 'node_modules\vite\bin\vite.js'
if (-not (Test-Path -LiteralPath $vite)) {
    Push-Location $frontend
    try { & corepack.cmd pnpm install --frozen-lockfile; if ($LASTEXITCODE) { throw 'Frontend installation failed.' } } finally { Pop-Location }
}
$env:VITE_BACKEND_PROXY_TARGET = 'http://127.0.0.1:' + $port
$frontendRecord = if ($state) { $state.frontend } else { $null }
if ($state -and [int]$state.backendPort -ne $port) { Stop-Owned $state.frontend }
if (-not (Test-Port 80)) {
    $frontendRecord = Start-Hidden 'frontend' $node @($vite,'--mode','development','--host','127.0.0.1','--port','80','--strictPort') $frontend $vite
} elseif (-not $frontendRecord) { throw 'Port 80 is already occupied. Stop the old frontend before using this launcher.' }

[ordered]@{ sourceHash=$sourceHash; backendPort=$port; backend=$backendRecord; frontend=$frontendRecord; ai=$aiRecord } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $stateFile -Encoding utf8
Wait-Url 'Backend' ('http://127.0.0.1:'+$port+'/auth/code') 90
Wait-Url 'Inference service' 'http://127.0.0.1:8091/health' 30
Wait-Url 'Frontend' 'http://127.0.0.1/' 30
Write-Host 'Ready: http://127.0.0.1/  -> 骑手安全 -> 规则与运维 -> 本机设备接入'
Write-Host 'No serial connection or image upload starts until requested from the management page.'
if (-not $NoBrowser) { Start-Process 'http://127.0.0.1/' }

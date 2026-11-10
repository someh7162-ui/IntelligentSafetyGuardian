param(
    [string]$Port = 'COM3',
    [string]$DeviceId = 'STM32-CAM-001',
    [string]$TokenFile = (Join-Path $PSScriptRoot 'riderguard-device-token.txt'),
    [string]$Endpoint = 'http://127.0.0.1:8080/device/riderguard/image',
    [string]$GpsEndpoint = 'http://127.0.0.1:8080/device/riderguard/telemetry',
    [int]$ImageIntervalMs = 30000,
    [int]$GpsIntervalMs = 3000
)
$ErrorActionPreference = 'Stop'
$python = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Start the local management system once to prepare the USB gateway.' }
& $python (Join-Path $PSScriptRoot 'local_gateway.py') --port $Port --device-id $DeviceId --token-file $TokenFile --image-url $Endpoint --telemetry-url $GpsEndpoint --image-ms $ImageIntervalMs --telemetry-ms $GpsIntervalMs
exit $LASTEXITCODE

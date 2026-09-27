$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtime = Join-Path $projectRoot 'deploy\local-runtime'
$mysqlHome = Join-Path $runtime 'mysql\mysql-8.4.11-winx64'
$redisHome = Join-Path $runtime 'redis\Redis-8.4.7-Windows-x64-msys2'
$mysqlExe = Join-Path $mysqlHome 'bin\mysqld.exe'
$redisExe = Join-Path $redisHome 'redis-server.exe'
$mysqlData = Join-Path $runtime 'mysql-data'
$redisData = Join-Path $runtime 'redis-data'

if (-not (Test-Path -LiteralPath $mysqlExe) -or -not (Test-Path -LiteralPath $redisExe)) {
  throw 'MySQL or Redis is missing from deploy\local-runtime. Download and extract the local runtime first.'
}
if (-not (Test-Path -LiteralPath (Join-Path $mysqlData 'mysql'))) {
  throw 'MySQL data directory is not initialized. Refusing to create a new database over an unknown installation.'
}

function Test-LocalPort([int]$port) {
  $client = [Net.Sockets.TcpClient]::new()
  try {
    return $client.ConnectAsync('127.0.0.1', $port).Wait(500) -and $client.Connected
  } catch {
    return $false
  } finally {
    $client.Dispose()
  }
}

function Wait-LocalPort([int]$port) {
  for ($attempt = 0; $attempt -lt 40; $attempt++) {
    if (Test-LocalPort $port) { return }
    Start-Sleep -Milliseconds 500
  }
  throw "Port $port did not become ready. Check deploy\local-runtime\*.log."
}

New-Item -ItemType Directory -Force -Path $redisData | Out-Null
$mysqlConfig = Join-Path $runtime 'my.ini'
$redisConfig = Join-Path $runtime 'redis.conf'
$mysqlPath = $mysqlHome.Replace('\', '/')
$mysqlDataPath = $mysqlData.Replace('\', '/')
$mysqlLog = (Join-Path $runtime 'mysql.log').Replace('\', '/')
@(
  '[mysqld]'
  "basedir=`"$mysqlPath`""
  "datadir=`"$mysqlDataPath`""
  'port=3306'
  'bind-address=127.0.0.1'
  'mysqlx=0'
  'character-set-server=utf8mb4'
  'default-time-zone=+08:00'
  "log-error=`"$mysqlLog`""
) | Set-Content -LiteralPath $mysqlConfig -Encoding ascii
@(
  'bind 127.0.0.1'
  'protected-mode yes'
  'port 6379'
  'requirepass ruoyi123'
  'maxclients 1000'
  'dir "../../redis-data"'
  'appendonly yes'
  'logfile "../../redis.log"'
) | Set-Content -LiteralPath $redisConfig -Encoding ascii

if (-not (Test-LocalPort 3306)) {
  Start-Process -FilePath $mysqlExe -ArgumentList ('--defaults-file="' + $mysqlConfig + '"') -WorkingDirectory (Split-Path $mysqlExe) -WindowStyle Hidden
  Wait-LocalPort 3306
}
if (-not (Test-LocalPort 6379)) {
  Start-Process -FilePath $redisExe -ArgumentList '../../redis.conf' -WorkingDirectory $redisHome -WindowStyle Hidden
  Wait-LocalPort 6379
}

Write-Host 'MySQL 3306 and Redis 6379 are ready for RiderGuard.'

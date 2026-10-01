@echo off
setlocal EnableExtensions

title RiderGuard - Start Management System
set "RUOYI_ROOT=%~dp0"
set "BACKEND_DIR=%RUOYI_ROOT%backend"
set "FRONTEND_DIR=%RUOYI_ROOT%frontend"
set "PROFILE=dev"

rem Prefer the configured user JDK. Fall back to the installed Microsoft JDK path
rem so an already-open terminal can start without a new login session.
if defined JAVA_HOME if exist "%JAVA_HOME%\bin\java.exe" goto :java_ready
if exist "C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot\bin\java.exe" set "JAVA_HOME=C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot"
:java_ready

echo.
echo ================================================
echo        RiderGuard Management System Startup
echo ================================================
echo.

if not exist "%BACKEND_DIR%\mvnw.cmd" (
  echo [ERROR] Backend Maven Wrapper not found: %BACKEND_DIR%\mvnw.cmd
  pause
  exit /b 1
)

if not exist "%FRONTEND_DIR%\package.json" (
  echo [ERROR] Frontend project not found: %FRONTEND_DIR%\package.json
  pause
  exit /b 1
)

if not defined JAVA_HOME (
  echo [ERROR] JAVA_HOME is not configured. Install JDK 21 and restart the terminal.
  pause
  exit /b 1
)

if not exist "%JAVA_HOME%\bin\java.exe" (
  echo [ERROR] Java executable not found under JAVA_HOME: %JAVA_HOME%
  pause
  exit /b 1
)

if not exist "%JAVA_HOME%\bin\javac.exe" (
  echo [ERROR] A full JDK is required. javac.exe was not found under: %JAVA_HOME%
  pause
  exit /b 1
)

set "PATH=%JAVA_HOME%\bin;%PATH%"
where java >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Java is not available. RuoYi-Vue-Plus 6.x requires JDK 21.
  pause
  exit /b 1
)

where node >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Node.js not found. Install Node.js 20.19 or later.
  pause
  exit /b 1
)

call corepack pnpm --version >nul 2>nul
if errorlevel 1 (
  echo [ERROR] pnpm/Corepack not found. Run "corepack enable" after installing Node.js.
  pause
  exit /b 1
)

echo [1/3] Checking frontend dependencies...
if not exist "%FRONTEND_DIR%\node_modules\.bin\vite.cmd" (
  echo       First run detected. Installing frontend dependencies...
  pushd "%FRONTEND_DIR%"
  call corepack pnpm install --frozen-lockfile
  if errorlevel 1 (
    popd
    echo [ERROR] Frontend dependency installation failed.
    pause
    exit /b 1
  )
  popd
)

echo Starting local MySQL and Redis...
powershell -NoProfile -ExecutionPolicy Bypass -File "%RUOYI_ROOT%..\..\scripts\start_local_services.ps1"
if errorlevel 1 (
  echo [ERROR] Local database services did not start. Check deploy\local-runtime logs.
  pause
  exit /b 1
)

echo [2/3] Starting RuoYi backend (port 8080, dev profile)...
set "BACKEND_PORT=8080"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $r=Invoke-RestMethod 'http://127.0.0.1:8080/auth/code' -TimeoutSec 3; if ($r.code -eq 200) { exit 0 } } catch {}; exit 1" >nul 2>nul
if not errorlevel 1 (
  echo       Backend is already running on port 8080. Reusing it.
  echo       Restart the backend later if you need newly edited Java code.
  goto :backend_started
)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$c=New-Object Net.Sockets.TcpClient; try {$c.Connect('127.0.0.1',8080); exit 0} catch {exit 1} finally {$c.Dispose()}" >nul 2>nul
if not errorlevel 1 (
  set "BACKEND_PORT=18080"
  echo       Port 8080 is occupied by an unresponsive process; using port 18080.
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$c=New-Object Net.Sockets.TcpClient; try {$c.Connect('127.0.0.1',18080); exit 0} catch {exit 1} finally {$c.Dispose()}" >nul 2>nul
  if not errorlevel 1 (
    echo [ERROR] Port 18080 is also occupied. Close the conflicting process and retry.
    pause
    exit /b 1
  )
)
set "BUILD_NAME=ruoyi-admin-run-%RANDOM%%RANDOM%"
set "BACKEND_JAR=%BACKEND_DIR%\ruoyi-admin\target\%BUILD_NAME%.jar"
set "MAVEN_REPO=%RUOYI_ROOT%..\..\deploy\local-runtime\maven-repository"
set "RIDERGUARD_IMAGE_DIR=%RUOYI_ROOT%..\..\deploy\local-runtime\uploads"
set "RIDERGUARD_DEMO_MODE=true"
set "RIDERGUARD_INFERENCE_MODE=mock"
set "RIDERGUARD_AMAP_SECURITY_FILE=%RUOYI_ROOT%..\..\deploy\local-runtime\amap-security-code.txt"
echo       Building the latest backend sources...
  pushd "%BACKEND_DIR%"
  call mvnw.cmd "-Dmaven.repo.local=%MAVEN_REPO%" "-Driderguard.build.name=%BUILD_NAME%" -DskipTests -P%PROFILE% -pl ruoyi-admin -am package
  if errorlevel 1 (
    popd
    echo [ERROR] Backend build failed.
    pause
    exit /b 1
  )
  popd
echo       Starting local image inference service (mock mode)...
start "RiderGuard Backend" "%ComSpec%" /k ""%JAVA_HOME%\bin\java.exe" -jar "%BACKEND_JAR%" --spring.profiles.active=dev --server.port=%BACKEND_PORT%"

:backend_started
powershell -NoProfile -ExecutionPolicy Bypass -Command "$c=New-Object Net.Sockets.TcpClient; try {$c.Connect('127.0.0.1',8091); exit 0} catch {exit 1} finally {$c.Dispose()}" >nul 2>nul
if errorlevel 1 start "RiderGuard AI" "%ComSpec%" /k ""%RUOYI_ROOT%..\..\scripts\start_ai_service.bat""

echo [3/3] Starting RiderGuard frontend (port 80)...
set "VITE_BACKEND_PROXY_TARGET=http://127.0.0.1:%BACKEND_PORT%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $r=Invoke-WebRequest 'http://127.0.0.1' -UseBasicParsing -TimeoutSec 3; if ($r.StatusCode -eq 200 -and $r.Content -match 'RiderGuard') { exit 0 } } catch {}; exit 1" >nul 2>nul
if errorlevel 1 (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$c=New-Object Net.Sockets.TcpClient; try {$c.Connect('127.0.0.1',80); exit 0} catch {exit 1} finally {$c.Dispose()}" >nul 2>nul
  if not errorlevel 1 (
    echo [ERROR] Port 80 is occupied by another service. Close it before starting RiderGuard.
    pause
    exit /b 1
  )
  start "RiderGuard Frontend" /D "%FRONTEND_DIR%" "%ComSpec%" /k "echo [RiderGuard Frontend] Starting... && call corepack pnpm dev"
) else (
  echo       Frontend is already running on port 80. Reusing it.
)

echo.
echo Service windows are open:
echo   Frontend: http://localhost
echo   Backend:  http://localhost:%BACKEND_PORT%
echo   AI:       http://127.0.0.1:8091/health
echo.
echo Note: The backend dev profile requires MySQL 3306 (database: ry-vue) and Redis 6379.
echo Keep both service windows open. Close a window to stop that service.
echo Waiting for the frontend server...
set "FRONTEND_READY=0"
for /l %%N in (1,1,30) do (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $r=Invoke-WebRequest 'http://127.0.0.1' -UseBasicParsing -TimeoutSec 3; if ($r.StatusCode -eq 200 -and $r.Content -match 'RiderGuard') { exit 0 } } catch {}; exit 1" >nul 2>nul
  if not errorlevel 1 (
    set "FRONTEND_READY=1"
    goto :frontend_ready
  )
  powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul
)

:frontend_ready
if not "%FRONTEND_READY%"=="1" (
  echo [WARNING] Frontend did not respond within 30 seconds.
  echo Open http://127.0.0.1 manually after the frontend window is ready.
  exit /b 1
)
echo Frontend is ready. Waiting for the backend captcha service...
for /l %%N in (1,1,60) do (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $r=Invoke-RestMethod 'http://127.0.0.1:%BACKEND_PORT%/auth/code' -TimeoutSec 3; if ($r.code -eq 200) { exit 0 } } catch {}; exit 1" >nul 2>nul
  if not errorlevel 1 goto :backend_ready
  powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul
)
echo [WARNING] Backend did not start on port %BACKEND_PORT%. Captcha and login will not work.
echo Check the backend window, MySQL 3306, and Redis 6379.
exit /b 1

:backend_ready
echo Backend is ready. Opening browser...
start "" "http://127.0.0.1"
exit /b 0

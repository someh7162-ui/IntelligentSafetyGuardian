@echo off
setlocal
cd /d "%~dp0.."
if not defined RIDERGUARD_INFERENCE_MODE set "RIDERGUARD_INFERENCE_MODE=mock"
python -c "import fastapi, uvicorn" >nul 2>nul
if errorlevel 1 (
  echo [ERROR] FastAPI and uvicorn are required. Run: python -m pip install -r backend\requirements.txt
  exit /b 1
)
echo [RiderGuard AI] mode=%RIDERGUARD_INFERENCE_MODE% port=8091
python -m uvicorn ai.server:app --host 127.0.0.1 --port 8091

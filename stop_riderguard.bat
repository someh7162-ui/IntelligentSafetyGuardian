@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start_riderguard_local.ps1" -Stop
if errorlevel 1 pause

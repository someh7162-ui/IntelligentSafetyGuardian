@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\start_riderguard_local.ps1"
if errorlevel 1 pause

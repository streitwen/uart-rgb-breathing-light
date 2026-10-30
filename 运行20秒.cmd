@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_20s.ps1"
echo.
echo 程序已结束，灯光应已熄灭。
pause

@echo off
rem Double-click: builds the documentation website and opens it in the browser.
rem   tools\docs.cmd          build site\ and open site\index.html
rem   tools\docs.cmd serve    live preview at http://localhost:8000 (Ctrl+C to stop)
rem Needs Docker Desktop running. Wraps docs.ps1 so Windows' script policy does not block it.
setlocal
set "MODE=%~1"
if "%MODE%"=="" set "MODE=build"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0docs.ps1" %MODE%
if errorlevel 1 (
    echo.
    echo Failed. Is Docker Desktop running?
    pause
    exit /b 1
)
if /i "%MODE%"=="build" start "" "%~dp0..\site\index.html"

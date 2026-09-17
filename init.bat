@echo off
setlocal

set REPO_DIR=%~dp0

where python >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo Python 3 is required before Ludus Sandbox can initialize this host.
    exit /b 1
)

python "%REPO_DIR%scripts\python\sandbox.py" init %*
exit /b %ERRORLEVEL%
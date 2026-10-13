@echo off
setlocal EnableExtensions

rem Keep extra compiler options; the last definition disables logging.
set "EXTRA_CL=%EXTRA_CL% /DENABLE_LOG=0"
call "%~dp0build.bat" %*
exit /b %errorlevel%

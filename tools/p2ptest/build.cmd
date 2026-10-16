@echo off
rem Build p2p_probe (VS18 bundled vcvars64; output goes to repo build\p2ptest\)
setlocal
set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community
set VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
  echo [x] 找不到 vcvars64.bat: %VCVARS%
  exit /b 1
)
call "%VCVARS%" >nul 2>&1
set OUT=%~dp0..\..\build\p2ptest
if not exist "%OUT%" mkdir "%OUT%"
cd /d "%OUT%"
cl /nologo /utf-8 /O2 /EHsc "%~dp0p2p_probe.cpp" /Fe:"%OUT%\p2p_probe.exe"
if errorlevel 1 exit /b 1
echo [ok] %OUT%\p2p_probe.exe

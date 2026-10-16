@echo off
rem 编译 p2p_probe（VS18 自带 vcvars64；产物放到仓库的 build\p2ptest\）
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
cl /nologo /O2 /EHsc "%~dp0p2p_probe.cpp" /Fe:"%OUT%\p2p_probe.exe" /Fo:"%OUT%\"
if errorlevel 1 exit /b 1
echo [ok] %OUT%\p2p_probe.exe

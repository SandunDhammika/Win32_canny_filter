@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found.
  exit /b 1
)

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"

if "%VSINSTALL%"=="" (
  echo ERROR: Visual Studio C++ Build Tools not found.
  exit /b 1
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo ERROR: vcvars64.bat not found at "%VCVARS%".
  exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
  echo ERROR: Failed to initialize MSVC environment.
  exit /b 1
)

echo Building with MSVC from: %VSINSTALL%
nmake /f Makefile.vc clean
nmake /f Makefile.vc
exit /b %errorlevel%

@echo off
rem Inject main-repo plugins (librime-plugins/*) into the librime submodule
rem before building. Called by CI and by local builds.
rem Idempotent: robocopy only copies files that differ at the destination.

setlocal
set "WEASEL_ROOT=%~dp0"
if "%WEASEL_ROOT:~-1%"=="\" set "WEASEL_ROOT=%WEASEL_ROOT:~0,-1%"
cd /d "%WEASEL_ROOT%"
echo [inject-plugins] WEASEL_ROOT=%WEASEL_ROOT%

if not exist "librime\CMakeLists.txt" (
  echo [inject-plugins] ERROR: librime\CMakeLists.txt not found under WEASEL_ROOT
  echo [inject-plugins] Did you check out with submodules:recursive?
  exit /b 1
)

if not exist "librime\plugins" mkdir "librime\plugins"

for /d %%D in ("%WEASEL_ROOT%\librime-plugins\*") do (
  echo [inject-plugins] copying %%~nxD into librime\plugins\
  robocopy "%%D" "librime\plugins\%%~nxD" /E /NFL /NDL /NJH /NJS /NP >nul
  if errorlevel 8 (
    echo [inject-plugins] copy FAILED for %%~nxD
    exit /b 1
  )
)

if not exist "librime\plugins\rime-weasel-ai\src\weasel_ai_module.cc" (
  echo [inject-plugins] ERROR: weasel_ai_module.cc missing after copy
  exit /b 1
)

echo [inject-plugins] done. librime\plugins now contains:
dir /b librime\plugins
exit /b 0

@echo off
rem Inject main-repo plugins (librime-plugins/*) into the librime submodule
rem before building. Called by CI and by local builds.
rem Idempotent: robocopy only copies files that differ at the destination.

setlocal
set WEASEL_ROOT=%~dp0..
cd /d "%WEASEL_ROOT%"

rem actions/checkout does NOT create librime\.git, so check for real
rem submodule content instead of the .git handle.
if not exist librime\CMakeLists.txt (
  echo [inject-plugins] librime submodule content missing - run checkout with submodules:recursive
  exit /b 1
)

if not exist librime\plugins mkdir librime\plugins

for /d %%D in ("%WEASEL_ROOT%\librime-plugins\*") do (
  echo [inject-plugins] copying %%~nxD into librime\plugins\
  robocopy "%%D" "librime\plugins\%%~nxD" /E /NFL /NDL /NJH /NJS /NP /XD .injected
  if errorlevel 8 (
    echo [inject-plugins] copy FAILED for %%~nxD ^(robocopy code %errorlevel%^)
    exit /b 1
  )
)

echo [inject-plugins] done.
dir /b librime\plugins
exit /b 0

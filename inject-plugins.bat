@echo off
rem Inject main-repo plugins (librime-plugins/*) into the librime submodule
rem before building. Called by CI and by local builds. Idempotent.

setlocal
set WEASEL_ROOT=%~dp0..
cd /d "%WEASEL_ROOT%"

if not exist librime\.git (
  echo [inject-plugins] librime submodule not initialized; skipping.
  exit /b 0
)

if not exist librime\plugins mkdir librime\plugins

for /d %%D in ("%WEASEL_ROOT%\librime-plugins\*") do (
  if not exist "%%D\.injected" (
    echo [inject-plugins] copying %%~nxD into librime\plugins\
    robocopy "%%D" "librime\plugins\%%~nxD" /E /NFL /NDL /NJH /NJS >nul
    if errorlevel 8 (
      echo [inject-plugins] copy failed for %%~nxD
      exit /b 1
    )
    type nul > "%%D\.injected"
  )
)

exit /b 0

@echo off
rem Plays Riptide on Windows: brings the game's code up to date (quick when nothing has changed), then starts the
rem game in its own window. Set UE_ROOT if Unreal Engine 5.7 isn't in the default place.
setlocal
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.7"
set "PROJECT=%~dp0..\..\Riptide.uproject"
for %%I in ("%PROJECT%") do set "PROJECT=%%~fI"

echo Getting Riptide ready...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" RiptideEditor Win64 Development "-Project=%PROJECT%" -WaitMutex -NoHotReload > "%TEMP%\riptide_build.log" 2>&1
if errorlevel 1 (
    echo.
    echo Riptide's code didn't build. The end of the build log:
    powershell -NoProfile -Command "Get-Content '%TEMP%\riptide_build.log' -Tail 25"
    echo.
    pause
    exit /b 1
)
start "" "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJECT%" -game

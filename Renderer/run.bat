@echo off
setlocal

set "BUILD_DIR=%~dp0build-short"
if not exist "%BUILD_DIR%" set "BUILD_DIR=%~dp0build"
if exist "C:\msys64\ucrt64\bin" set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"
pushd "%~dp0"
set "RUN_EXE="

if exist "%BUILD_DIR%\test-app\Debug\TestApp.exe"   set "RUN_EXE=%BUILD_DIR%\test-app\Debug\TestApp.exe"
if not defined RUN_EXE if exist "%BUILD_DIR%\test-app\Release\TestApp.exe" set "RUN_EXE=%BUILD_DIR%\test-app\Release\TestApp.exe"
if not defined RUN_EXE if exist "%BUILD_DIR%\test-app\TestApp.exe"         set "RUN_EXE=%BUILD_DIR%\test-app\TestApp.exe"

if not defined RUN_EXE (
    for /R "%BUILD_DIR%" %%F in (TestApp.exe) do (
        set "RUN_EXE=%%~fF"
        goto :run_app
    )
)

:run_app

if not defined RUN_EXE (
    echo Error: no built TestApp.exe found under %BUILD_DIR% -- run build.bat first
    popd
    exit /b 1
)

"%RUN_EXE%"
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%

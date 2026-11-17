@echo off
setlocal
pushd "%~dp0"

REM MSYS2 is often installed but not added to the shell's PATH.
if exist "C:\msys64\ucrt64\bin\cmake.exe" set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"
if exist "C:\msys64\mingw64\bin\cmake.exe" set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"

REM ==============================
REM CONFIGURATION
REM ==============================
for %%I in ("%CD%") do set "SHORT_DIR=%%~sI"
set "BUILD_DIR=%SHORT_DIR%\build-short"
set "CONFIG=%1"
if "%CONFIG%"=="" set "CONFIG=Release"

REM ==============================
REM GIT HOOKS (OPTIONAL SAFE STEP)
REM ==============================
if exist .git (
    if exist ".githooks" (
        echo Installing Git hooks...
        for %%F in (.githooks\*) do (
            copy /Y "%%F" ".git\hooks\%%~nxF" >nul
        )
    )
)

REM ==============================
REM CONFIGURE (LET CMAKE DECIDE GENERATOR)
REM ==============================
echo Configuring CMake (%CONFIG%)...

cmake -S "%SHORT_DIR%" -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if errorlevel 1 goto :failed

REM ==============================
REM BUILD
REM ==============================
echo Building (%CONFIG%)...

cmake --build "%BUILD_DIR%" --config "%CONFIG%" --parallel

if errorlevel 1 goto :failed

REM ==============================
REM FIND EXECUTABLE (FLEXIBLE)
REM ==============================
set "RUN_EXE="

for /R "%BUILD_DIR%" %%F in (*.exe) do (
    if /I "%%~nF"=="TestApp" (
        set "RUN_EXE=%%F"
        goto :run
    )
)

echo Error: TestApp.exe not found.
goto :failed

:run
echo Running: %RUN_EXE%
"%RUN_EXE%"

endlocal
popd
exit /b 0

:failed
set "EXIT_CODE=%ERRORLEVEL%"
if "%EXIT_CODE%"=="0" set "EXIT_CODE=1"
popd
endlocal & exit /b %EXIT_CODE%

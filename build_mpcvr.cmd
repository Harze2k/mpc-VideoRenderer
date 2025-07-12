@ECHO OFF
REM (C) 2018-2024 see Authors.txt
REM (Enhanced for verbose logging, error analysis, and resilience)
REM
REM This file is part of MPC-BE.
REM (License header remains the same)

SETLOCAL ENABLEDELAYEDEXPANSION
CD /D %~dp0

SET "TITLE=MPC Video Renderer"
SET "PROJECT=MpcVideoRenderer"

REM --- Build Configuration ---
SET "MSBUILD_SWITCHES=/nologo /maxcpucount /nodeReuse:true"
SET "BUILDTYPE=Build"
SET "BUILDCFG=Release"
SET "SUFFIX="
SET "SIGN=False"
SET "Wait=True"

REM --- Argument Parsing ---
FOR %%A IN (%*) DO (
  IF /I "%%A" == "Debug" (
    SET "BUILDCFG=Debug"
    SET "SUFFIX=_Debug"
  )
  IF /I "%%A" == "Sign" (
    SET "SIGN=True"
  )
  IF /I "%%A" == "NoWait" (
    SET "Wait=False"
  )
  IF /I "%%A" == "VS2019" (
    SET "COMPILER=VS2019"
  )
  IF /I "%%A" == "VS2022" (
    SET "COMPILER=VS2022"
  )
)

CALL :SubMsg "INFO" "Starting build for %TITLE%..."
ECHO Configuration: %BUILDCFG%
ECHO Sign enabled:  %SIGN%
ECHO Wait on exit:  %Wait%
ECHO.

REM --- Prerequisite Checks ---
CALL :SubMsg "INFO" "Checking prerequisites..."
IF /I "%SIGN%" == "True" (
  IF NOT EXIST "%~dp0signinfo.txt" (
    CALL :SubMsg "WARNING" "signinfo.txt not found. Signing will be skipped."
    SET "SIGN=False"
  )
)

CALL :SubVSPath
IF NOT DEFINED VS_PATH (
    CALL :SubMsg "ERROR" "Could not detect Visual Studio installation path. Please check your VS installation."
    GOTO :EndScript
)
SET "TOOLSET=%VS_PATH%\Common7\Tools\vsdevcmd"
IF NOT EXIST "%TOOLSET%" (
    CALL :SubMsg "ERROR" "Visual Studio Dev CMD not found at: %TOOLSET%"
    GOTO :EndScript
)
ECHO Prerequisites check passed.
ECHO.

REM --- Directory Creation ---
CALL :SubMsg "INFO" "Setting up build directories..."
SET "LOG_DIR=_bin\logs"
IF NOT EXIST "%LOG_DIR%" MD "%LOG_DIR%"
IF NOT EXIST "_bin\shaders" MD "_bin\shaders"
IF NOT EXIST "_bin\lib\Release_x86" MD "_bin\lib\Release_x86"
IF NOT EXIST "_bin\lib\Release_x64" MD "_bin\lib\Release_x64"
IF NOT EXIST "_bin\lib\Debug_x86" MD "_bin\lib\Debug_x86"
IF NOT EXIST "_bin\lib\Debug_x64" MD "_bin\lib\Debug_x64"
IF NOT EXIST "_bin\Filter_x86%SUFFIX%" MD "_bin\Filter_x86%SUFFIX%"
IF NOT EXIST "_bin\Filter_x64%SUFFIX%" MD "_bin\Filter_x64%SUFFIX%"
ECHO Directories are ready.
ECHO.

REM ============================================================================
REM == Build x86
REM ============================================================================
CALL "%TOOLSET%" -arch=x86 -no_logo
CD /D %~dp0
CALL :SubCompiling x86
IF !ERRORLEVEL! NEQ 0 (
    CALL :AnalyzeLog "x86"
    CALL :SubMsg "ERROR" "Fatal: Build failed for x86 platform."
    GOTO :EndScript
)

REM ============================================================================
REM == Build x64
REM ============================================================================
CALL "%TOOLSET%" -arch=amd64 -no_logo
CD /D %~dp0
CALL :SubCompiling x64
IF !ERRORLEVEL! NEQ 0 (
    CALL :AnalyzeLog "x64"
    CALL :SubMsg "ERROR" "Fatal: Build failed for x64 platform."
    GOTO :EndScript
)

REM ============================================================================
REM == Post-Build Steps
REM ============================================================================
IF /I "%SIGN%" == "True" (
  CALL :SubMsg "INFO" "Attempting to sign build outputs..."
  SET "FILES_TO_SIGN="%~dp0_bin\Filter_x86%SUFFIX%\%PROJECT%.ax" "%~dp0_bin\Filter_x64%SUFFIX%\%PROJECT%64.ax""
  CALL "%~dp0\sign.cmd" %FILES_TO_SIGN%
  IF %ERRORLEVEL% NEQ 0 (
    REM NON-FATAL: Warn but do not exit
    CALL :SubMsg "WARNING" "Problem signing files. The build will continue without signed binaries."
  ) ELSE (
    CALL :SubMsg "INFO" "Files signed successfully."
  )
)

REM --- Package Creation ---
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define VER_MAJOR" "Include\Version.h"') DO (SET "VERMAJOR=%%A")
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define VER_MINOR" "Include\Version.h"') DO (SET "VERMINOR=%%A")
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define VER_BUILD" "Include\Version.h"') DO (SET "VERBUILD=%%A")
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define VER_RELEASE" "Include\Version.h"') DO (SET "VERRELEASE=%%A")
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define REV_DATE" "revision.h"') DO (SET "REVDATE=%%A")
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define REV_HASH" "revision.h"') DO (SET "REVHASH=%%A")
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define REV_NUM" "revision.h"') DO (SET "REVNUM=%%A")
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define REV_BRANCH" "revision.h"') DO (SET "REVBRANCH=%%A")

IF /I "%VERRELEASE%" == "1" (
  SET "PCKG_NAME=%PROJECT%-%VERMAJOR%.%VERMINOR%.%VERBUILD%.%REVNUM%%SUFFIX%"
) ELSE (
  IF /I "%REVBRANCH%" == "master" (
    SET "PCKG_NAME=%PROJECT%-%VERMAJOR%.%VERMINOR%.%VERBUILD%.%REVNUM%_git%REVDATE%-%REVHASH%%SUFFIX%"
  ) ELSE (
    SET "PCKG_NAME=%PROJECT%-%VERMAJOR%.%VERMINOR%.%VERBUILD%.%REVNUM%.%REVBRANCH%_git%REVDATE%-%REVHASH%%SUFFIX%"
  )
)

CALL :SubDetectSevenzipPath
IF DEFINED SEVENZIP (
    IF EXIST "_bin\%PCKG_NAME%.zip" DEL "_bin\%PCKG_NAME%.zip"
    CALL :SubMsg "INFO" "Attempting to create archive %PCKG_NAME%.zip..."
    "%SEVENZIP%" a -tzip -mx9 "_bin\%PCKG_NAME%.zip" ^
        ".\_bin\Filter_x86%SUFFIX%\%PROJECT%.ax" ^
        ".\_bin\Filter_x64%SUFFIX%\%PROJECT%64.ax" ^
        ".\distrib\Install_MPCVR_32.cmd" ^
        ".\distrib\Install_MPCVR_64.cmd" ^
        ".\distrib\Uninstall_MPCVR_32.cmd" ^
        ".\distrib\Uninstall_MPCVR_64.cmd" ^
        ".\distrib\Reset_Settings.cmd" ^
        ".\Readme.md" ^
        ".\history.txt" ^
        ".\LICENSE.txt" > NUL
    IF %ERRORLEVEL% NEQ 0 (
        REM NON-FATAL: Warn but do not exit
        CALL :SubMsg "WARNING" "Unable to create %PCKG_NAME%.zip! The build artifacts are still available."
    ) ELSE (
        CALL :SubMsg "INFO" "%PCKG_NAME%.zip successfully created."
    )
) ELSE (
    CALL :SubMsg "WARNING" "7-Zip not found. Skipping archive creation."
)

:EndScript
CALL :SubMsg "INFO" "Build script finished."
IF /I "%Wait%" == "True" (
  TIMEOUT /T 3
)
ENDLOCAL
EXIT /B 0

REM ============================================================================
REM == Subroutines
REM ============================================================================

:SubVSPath
SET "PARAMS=-property installationPath -requires Microsoft.Component.MSBuild"
IF /I "%COMPILER%" == "VS2019" ( SET "PARAMS=%PARAMS% -version [16.0,17.0)" )
IF /I "%COMPILER%" == "VS2022" ( SET "PARAMS=%PARAMS% -version [17.0,18.0)" )
IF NOT DEFINED COMPILER ( SET "PARAMS=%PARAMS% -latest" )
SET "VSWHERE_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
IF NOT EXIST "%VSWHERE_PATH%" ( EXIT /B )
SET "VSWHERE="%VSWHERE_PATH%" %PARAMS%"
FOR /f "usebackq delims=" %%A IN (`!VSWHERE!`) DO SET VS_PATH=%%A
EXIT /B

:SubDetectSevenzipPath
FOR %%G IN (7z.exe) DO (SET "SEVENZIP_PATH=%%~$PATH:G")
IF EXIST "%SEVENZIP_PATH%" (SET "SEVENZIP=%SEVENZIP_PATH%" & EXIT /B)
FOR %%G IN (7za.exe) DO (SET "SEVENZIP_PATH=%%~$PATH:G")
IF EXIST "%SEVENZIP_PATH%" (SET "SEVENZIP=%SEVENZIP_PATH%" & EXIT /B)
FOR /F "tokens=2*" %%A IN ('REG QUERY "HKLM\SOFTWARE\7-Zip" /v "Path" 2^>NUL ^| FIND "REG_SZ" ^|^| REG QUERY "HKLM\SOFTWARE\Wow6432Node\7-Zip" /v "Path" 2^>NUL ^| FIND "REG_SZ"') DO SET "SEVENZIP=%%B\7z.exe"
EXIT /B

:SubCompiling
TITLE Compiling %TITLE% - %BUILDCFG%^|%1...
CALL :SubMsg "INFO" "Compiling for platform: %1"
SET "LOG_FILE=%LOG_DIR%\build_%BUILDCFG%_%1.log"
IF EXIST "%LOG_FILE%" DEL "%LOG_FILE%"

ECHO Building %PROJECT%.sln %BUILDCFG% %1... > "%LOG_FILE%"
ECHO Start time: %TIME% >> "%LOG_FILE%"
ECHO. >> "%LOG_FILE%"

MSBuild.exe %PROJECT%.sln %MSBUILD_SWITCHES%^
 /target:%BUILDTYPE% /p:Configuration="%BUILDCFG%" /p:Platform=%1^
 /flp:LogFile=%LOG_DIR%\msbuild_summary_%1.log;Verbosity=minimal^
 /clp:Verbosity=detailed;ShowTimestamp >> "%LOG_FILE%" 2>>&1

SET "BUILD_RESULT=%ERRORLEVEL%"
ECHO. >> "%LOG_FILE%"
ECHO End time: %TIME% >> "%LOG_FILE%"
ECHO MSBuild exited with code: %BUILD_RESULT% >> "%LOG_FILE%"
EXIT /B %BUILD_RESULT%

:AnalyzeLog
SET "LOG_FILE=%LOG_DIR%\build_%BUILDCFG%_%1.log"
ECHO.
CALL :SubMsg "WARNING" "Analyzing build log for errors and warnings: %LOG_FILE%"
ECHO.
ECHO   --- Begin Error & Warning Summary for %1 ---
IF NOT EXIST "%LOG_FILE%" (
    ECHO Log file not found. Cannot analyze.
) ELSE (
    REM Use FINDSTR to find all lines containing "error" or "warning", case-insensitive
    FINDSTR /I /C:" error " /C:" warning " "%LOG_FILE%"
)
ECHO   --- End of Summary ---
ECHO.
CALL :SubMsg "INFO" "Full log is available at: %LOG_FILE%"
EXIT /B

:SubMsg
ECHO. & ECHO ------------------------------
IF /I "%~1" == "ERROR" (
  CALL :SubColorText "0C" "[ERROR]  " & ECHO %~2
) ELSE IF /I "%~1" == "INFO" (
  CALL :SubColorText "0A" "[INFO]   " & ECHO %~2
) ELSE IF /I "%~1" == "WARNING" (
  CALL :SubColorText "0E" "[WARNING]" & ECHO %~2
)
ECHO ------------------------------ & ECHO.
IF /I "%~1" == "ERROR" (
  IF /I "%Wait%" == "True" (
    ECHO Press any key to close this window...
    PAUSE >NUL
  )
)
EXIT /B

:SubColorText
FOR /F "tokens=1,2 delims=#" %%A IN ('"PROMPT #$H#$E# & ECHO ON & FOR %%B IN (1) DO REM"') DO (SET "DEL=%%A")
<NUL SET /p ".=%DEL%" > "%~2"
FINDSTR /v /a:%1 /R ".18" "%~2" NUL
DEL "%~2" > NUL 2>&1
EXIT /B

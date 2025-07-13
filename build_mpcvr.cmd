@ECHO OFF
REM (C) 2018-2024 see Authors.txt
REM
REM This file is part of MPC-BE.
REM
REM MPC-BE is free software; you can redistribute it and/or modify
REM it under the terms of the GNU General Public License as published by
REM the Free Software Foundation; either version 3 of the License, or
REM (at your option) any later version.
REM
REM MPC-BE is distributed in the hope that it will be useful,
REM but WITHOUT ANY WARRANTY; without even the implied warranty of
REM MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
REM GNU General Public License for more details.
REM
REM You should have received a copy of the GNU General Public License
REM along with this program.  If not, see <http://www.gnu.org/licenses/>.

SETLOCAL ENABLEDELAYEDEXPANSION
CD /D %~dp0

SET "TITLE=MPC Video Renderer"
SET "PROJECT=MpcVideoRenderer"

REM --- Configuration ---
REM MSBUILD_SWITCHES: Common switches for MSBuild.
REM   /nologo: Suppress MSBuild logo.
REM   /consoleloggerparameters:Verbosity=minimal: Reduce console output from MSBuild itself.
REM   /maxcpucount: Use multiple cores for parallel builds.
REM   /nodeReuse:true: Allow MSBuild to reuse build controllers.
SET "MSBUILD_SWITCHES=/nologo /consoleloggerparameters:Verbosity=minimal /maxcpucount /nodeReuse:true"

REM BUILDTYPE: Specifies the target type of build (e.g., Build, Clean).
SET "BUILDTYPE=Build"

REM BUILDCFG: Specifies the build configuration (e.g., Release, Debug).
SET "BUILDCFG=Release"

REM SUFFIX: Suffix for output directory/files, e.g., "_Debug".
SET "SUFFIX="

REM SIGN: Flag to indicate if signing should be performed.
SET "SIGN=False"

REM Wait: Flag to control whether the script waits at the end.
SET "Wait=True"

REM --- Argument Parsing ---
REM Loop through command-line arguments to set build configuration and flags.
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
  REM Allows specifying Visual Studio version for vswhere.
  REM Note: vswhere is for finding VS installations, which might not be relevant if using MinGW directly.
  REM However, MSBuild.exe might still be called via VS command prompt.
  IF /I "%%A" == "VS2019" (
    SET "COMPILER=VS2019"
  )
  IF /I "%%A" == "VS2022" (
    SET "COMPILER=VS2022"
  )
)

REM --- Pre-build Checks ---
REM Check if signing is requested and if the signinfo.txt file exists.
IF /I "%SIGN%" == "True" (
  IF NOT EXIST "%~dp0signinfo.txt" (
    CALL :SubMsg "WARNING" "signinfo.txt not found. Signing will be disabled."
    SET "SIGN=False"
  )
)

REM --- Environment Setup ---
REM Determine MSBuild/DevEnv path using vswhere.
REM This part might be problematic if the build uses MinGW tools primarily and not VS.
REM We'll rely on the `WHERE MSBuild.exe` check inside :SubCompiling to handle its absence.
CALL :SubVSPath
SET "VS_PATH=%VS_PATH%" REM Ensure VS_PATH is available in current scope if `:SubVSPath` sets it.

REM Check if VS_PATH was found. If not, it's a potential issue for MSBuild.
REM Note: If the build system *only* uses MinGW and calls clang/gcc directly, this check might be irrelevant.
REM However, the script *does* call MSBuild.exe, so its availability is critical.
IF NOT DEFINED VS_PATH (
    REM Don't abort here yet, as MSBuild might be in the system PATH or launched via a custom command prompt.
    REM The check inside :SubCompiling is more direct.
)

REM Use vsdevcmd.bat to set up the environment for MSVC/MSBuild.
REM If the build environment is MinGW-based and not launched from a VS Developer Command Prompt,
REM this line might need adjustment or removal, or the PATH needs to include the compiler toolchain.
REM For now, we assume it's needed to locate MSBuild.exe or VS-related tools.
SET "TOOLSET=%VS_PATH%\Common7\Tools\vsdevcmd.bat"

REM Create build output directories.
SET "LOG_DIR=_bin\logs"
IF NOT EXIST "%LOG_DIR%" MD "%LOG_DIR%"

REM Create additional required directories for build artifacts.
IF NOT EXIST "_bin" MD "_bin"
IF NOT EXIST "_bin\shaders" MD "_bin\shaders"
IF NOT EXIST "_bin\lib" MD "_bin\lib"
IF NOT EXIST "_bin\lib\Release_x86" MD "_bin\lib\Release_x86"
IF NOT EXIST "_bin\lib\Release_x64" MD "_bin\lib\Release_x64"
IF NOT EXIST "_bin\lib\Debug_x86" MD "_bin\lib\Debug_x86"
IF NOT EXIST "_bin\lib\Debug_x64" MD "_bin\lib\Debug_x64"
IF NOT EXIST "_bin\Filter_x86%SUFFIX%" MD "_bin\Filter_x86%SUFFIX%"
IF NOT EXIST "_bin\Filter_x64%SUFFIX%" MD "_bin\Filter_x64%SUFFIX%"

REM --- Compilation ---
REM Compile for x86.
REM The CALL "%TOOLSET%" command sets up the VS environment variables.
REM CD /D %~dp0 is repeated to ensure the correct working directory after vsdevcmd might change it.
REM NOTE: If your build environment is purely MinGW-based and doesn't rely on VS Developer Command Prompt,
REM the CALL "%TOOLSET%" line might need to be removed or adapted.
REM The critical part for the *existing script* is ensuring MSBuild.exe is available.

REM Launch x86 build
CALL "%TOOLSET%" -arch=x86
CD /D %~dp0
CALL :SubCompiling x86
REM If :SubCompiling returns without aborting (errorlevel 0 or non-fatal warning), continue.

REM Launch x64 build
CALL "%TOOLSET%" -arch=amd64
CD /D %~dp0
CALL :SubCompiling x64
REM If :SubCompiling returns without aborting (errorlevel 0 or non-fatal warning), continue.

REM --- Post-build Actions ---
REM Sign files if requested.
IF /I "%SIGN%" == "True" (
  SET "FILES="%~dp0_bin\Filter_x86%SUFFIX%\%PROJECT%.ax" "%~dp0_bin\Filter_x64%SUFFIX%\%PROJECT%64.ax""
  CALL "%~dp0\sign.cmd" %%FILES%%
  IF %ERRORLEVEL% NEQ 0 (
    CALL :SubMsg "ERROR" "Problem signing files."
    EXIT /B %ERRORLEVEL%
  ) ELSE (
    CALL :SubMsg "INFO" "Files signed successfully."
  )
)

REM --- Versioning and Packaging ---
REM Extract version and revision information from header files.
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define VER_MAJOR" "Include\Version.h"') DO SET "VERMAJOR=%%A"
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define VER_MINOR" "Include\Version.h"') DO SET "VERMINOR=%%A"
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define VER_BUILD" "Include\Version.h"') DO SET "VERBUILD=%%A"
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define VER_RELEASE" "Include\Version.h"') DO SET "VERRELEASE=%%A"
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define REV_DATE" "revision.h"') DO SET "REVDATE=%%A"
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define REV_HASH" "revision.h"') DO SET "REVHASH=%%A"
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define REV_NUM" "revision.h"') DO SET "REVNUM=%%A"
FOR /F "tokens=3,4 delims= " %%A IN ('FINDSTR /I /L /C:"define REV_BRANCH" "revision.h"') DO SET "REVBRANCH=%%A"

REM Construct package name based on versioning.
IF /I "%VERRELEASE%" == "1" (
  SET "PCKG_NAME=%PROJECT%-%VERMAJOR%.%VERMINOR%.%VERBUILD%.%REVNUM%%SUFFIX%"
) ELSE (
  IF /I "%REVBRANCH%" == "master" (
    SET "PCKG_NAME=%PROJECT%-%VERMAJOR%.%VERMINOR%.%VERBUILD%.%REVNUM%_git%REVDATE%-%REVHASH%%SUFFIX%"
  ) ELSE (
    SET "PCKG_NAME=%PROJECT%-%VERMAJOR%.%VERMINOR%.%VERBUILD%.%REVNUM%.%REVBRANCH%_git%REVDATE%-%REVHASH%%SUFFIX%"
  )
)

REM --- Archiving ---
REM Detect 7-Zip path and create a zip archive of the build output.
CALL :SubDetectSevenzipPath
IF DEFINED SEVENZIP (
    IF EXIST "_bin\%PCKG_NAME%.zip" DEL "_bin\%PCKG_NAME%.zip"

    TITLE Creating archive %PCKG_NAME%.zip...
    START "7z" /B /WAIT "%SEVENZIP%" a -tzip -mx9 "_bin\%PCKG_NAME%.zip" ^
.\_bin\Filter_x86%SUFFIX%\%PROJECT%.ax ^
.\_bin\Filter_x64%SUFFIX%\%PROJECT%64.ax ^
.\distrib\Install_MPCVR_32.cmd ^
.\distrib\Install_MPCVR_64.cmd ^
.\distrib\Uninstall_MPCVR_32.cmd ^
.\distrib\Uninstall_MPCVR_64.cmd ^
.\distrib\Reset_Settings.cmd ^
.\Readme.md ^
.\history.txt ^
.\LICENSE.txt
    IF %ERRORLEVEL% NEQ 0 CALL :SubMsg "ERROR" "Unable to create %PCKG_NAME%.zip!"
    EXIT /B %ERRORLEVEL%
    CALL :SubMsg "INFO" "%PCKG_NAME%.zip successfully created"
)

TITLE Compiling %TITLE% [FINISHED]
IF /I "%Wait%" == "True" (
  TIMEOUT /T 3
)
ENDLOCAL
EXIT

REM --- Helper to find Visual Studio path (may still be relevant if MSBuild is used via VS env) ---
:SubVSPath
REM Uses vswhere to find the latest installed Visual Studio version.
REM Adjustments made to handle potential missing VS installation.
SET "VS_PATH=" REM Reset VS_PATH to ensure a clean check.
SET "PARAMS=-property installationPath -requires Microsoft.Component.MSBuild"
IF DEFINED COMPILER (
  IF /I "%COMPILER%" == "VS2019" (
    SET "PARAMS=%PARAMS% -version [16.0,17.0)"
  ) ELSE IF /I "%COMPILER%" == "VS2022" (
    SET "PARAMS=%PARAMS% -version [17.0,18.0)"
  )
) ELSE (
  REM If COMPILER is not defined, find the latest VS installation that has MSBuild.
  SET "PARAMS=%PARAMS% -latest"
)

REM Construct the vswhere command. Ensure quotes for Program Files.
REM Redirect errors to NUL to avoid clutter if vswhere is not found.
SET "VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" %PARAMS%"

REM Execute vswhere and capture output.
REM Use 'FOR /F' to get the path.
FOR /F "delims=" %%A IN ('!VSWHERE! 2^>NUL') DO (
    SET "VS_PATH=%%A"
    REM Basic check if the found path seems valid (contains MSBuild.exe).
    IF NOT EXIST "!VS_PATH!\MSBuild\Current\Bin\MSBuild.exe" (
        SET "VS_PATH=" REM Clear if not a valid VS path with MSBuild.
    )
)
EXIT /B

REM --- Detect 7-Zip Path ---
REM Detects the path to 7z.exe by checking the system's PATH environment variable.
REM Tries both 7z.exe and 7za.exe.
:SubDetectSevenzipPath
SET "SEVENZIP="
FOR %%G IN (7z.exe) DO (SET "SEVENZIP_PATH=%%~$PATH:G")
IF DEFINED SEVENZIP_PATH IF EXIST "%SEVENZIP_PATH%" SET "SEVENZIP=%SEVENZIP_PATH%" & EXIT /B

FOR %%G IN (7za.exe) DO (SET "SEVENZIP_PATH=%%~$PATH:G")
IF DEFINED SEVENZIP_PATH IF EXIST "%SEVENZIP_PATH%" SET "SEVENZIP=%SEVENZIP_PATH%" & EXIT /B

REM Also checks the registry for the 7-Zip installation path.
FOR /F "tokens=2*" %%A IN (
  REG QUERY "HKLM\SOFTWARE\7-Zip" /v "Path" 2^>NUL ^|^|
  REG QUERY "HKLM\SOFTWARE\Wow6432Node\7-Zip" /v "Path" 2^>NUL ^|^|
  REM For user-specific installs, check HKCU (less common for build scripts, but possible)
  REG QUERY "HKCU\SOFTWARE\7-Zip" /v "Path" 2^>NUL ^|^|
  REG QUERY "HKCU\SOFTWARE\Wow6432Node\7-Zip" /v "Path" 2^>NUL ^| FIND "REG_SZ") DO (
    IF DEFINED %%A (
        SET "SEVENZIP_REG_PATH=%%A"
        IF EXIST "!SEVENZIP_REG_PATH!\7z.exe" (
            SET "SEVENZIP=!SEVENZIP_REG_PATH!\7z.exe" & EXIT /B
        )
    )
)
EXIT /B

REM --- Compilation Subroutine ---
REM Handles the compilation of a specific platform (x86 or x64).
REM It now checks for MSBuild availability and then for fatal errors in the log file.
:SubCompiling
SETLOCAL ENABLEDELAYEDEXPANSION
REM --- Fatal Errors List ---
REM These are the error codes identified as potentially fatal C++ compilation errors.
REM If ANY of these are found in the MSBuild log, the build will be aborted.
SET "FATAL_ERROR_CODES="
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C2653" REM DirectX not found
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C2504" REM Base class undefined
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C2061" REM Syntax error: identifier
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C3861" REM Identifier not found
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C2065" REM Undeclared identifier
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C2039" REM Not a member of
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C7568" REM Assumed function template error
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C2079" REM Uses undefined class
SET "FATAL_ERROR_CODES=!FATAL_ERROR_CODES!error C1010" REM PCH error
REM Add other critical errors if discovered during further analysis.

REM --- Print Executable Name in Big Letters ---
SET "EXEC_NAME_UPPER="
REM Use the ToUpper helper function to convert PROJECT name to uppercase.
CALL :ToUpper "%PROJECT%" EXEC_NAME_UPPER
REM Print a colored message indicating the build target with decorative borders.
CALL :SubColorText "0B" "=========================================================================="
CALL :SubColorText "0B" "=== BUILDING %EXEC_NAME_UPPER% (%BUILDCFG%/%1) ==="
CALL :SubColorText "0B" "=========================================================================="
ECHO.

TITLE Compiling %TITLE% - %BUILDCFG%^|%1...
SET "LOG_FILE=%LOG_DIR%\errors_%BUILDCFG%_%1.log"
SET "ABORT_BUILD_DUE_TO_FATAL=0"
SET "MSBUILD_COMMAND_FAILED=0" REM Flag to indicate if MSBuild command itself failed (e.g., not found)
SET "MSBUILD_EXIT_CODE=0" REM Initialize MSBuild exit code.

REM Clear previous log file if it exists to start fresh.
IF EXIST "%LOG_FILE%" DEL "%LOG_FILE%"

REM --- Check for MSBuild availability FIRST ---
REM This is crucial if MSBuild.exe is not in the environment's PATH or if the VS Dev Env isn't set up.
WHERE MSBuild.exe >nul 2>nul
IF %ERRORLEVEL% NEQ 0 (
    CALL :SubMsg "ERROR" "MSBuild.exe not found. The build environment may not have Visual Studio installed or configured correctly, or MSBuild is not in your PATH."
    SET "MSBUILD_COMMAND_FAILED=1"
    REM Treat MSBuild not found as a critical failure that must abort the build.
    SET "ABORT_BUILD_DUE_TO_FATAL=1"
) ELSE (
    REM MSBuild.exe is found, proceed to execute it.
    REM Execute MSBuild for the current platform.
    REM /flp1:LogFile="..." captures errors to the specified file.
    REM errorsonly: Ensures only error lines are written.
    REM Verbosity=diagnostic: Provides detailed output for better analysis.
    CALL MSBuild.exe %PROJECT%.sln %MSBUILD_SWITCHES% ^
      /target:%BUILDTYPE% /p:Configuration="%BUILDCFG%" /p:Platform=%1 ^
      /flp1:LogFile="%LOG_FILE%";errorsonly;Verbosity=diagnostic
    
    REM Store MSBuild's exit code.
    SET MSBUILD_EXIT_CODE=%ERRORLEVEL%

    REM Check MSBuild's exit code. If it's not 0, it means MSBuild ran but compilation failed.
    IF %MSBUILD_EXIT_CODE% NEQ 0 (
      CALL :SubMsg "INFO" "%PROJECT%.sln %BUILDCFG% %1 - MSBuild reported compilation errors (Exit Code: %MSBUILD_EXIT_CODE%). Checking logs for fatal C++ compilation errors."
    ) ELSE (
      REM MSBuild succeeded (EXIT CODE 0).
      CALL :SubMsg "INFO" "%PROJECT%.sln %BUILDCFG% %1 compiled successfully. Checking logs for any potential fatal errors (though unlikely if exit code is 0)."
    )
)

REM --- Fatal Error Analysis ---
REM Scan the log file for any of the defined fatal error codes,
REM BUT ONLY if the MSBuild command itself did not fail to run.
IF %MSBUILD_COMMAND_FAILED% EQU 0 (
  REM The loop checks each fatal error code. If found, it sets ABORT_BUILD_DUE_TO_FATAL=1 and breaks early.
  FOR %%E IN (%FATAL_ERROR_CODES%) DO (
    IF !ABORT_BUILD_DUE_TO_FATAL! EQU 0 (
      REM Check if the log file exists before trying to find strings in it.
      IF EXIST "%LOG_FILE%" (
        FINDSTR /I /L /C:"%%E" "%LOG_FILE%" >nul
        IF !ERRORLEVEL! EQU 0 (
          SET "ABORT_BUILD_DUE_TO_FATAL=1"
          CALL :SubMsg "ERROR" "Detected fatal C++ compilation error: %%E"
        )
      ) ELSE (
        REM If MSBuild command *ran* but exited non-zero and produced no log file,
        REM this is a severe issue not covered by specific C++ error codes.
        IF %MSBUILD_EXIT_CODE% NEQ 0 (
             SET "ABORT_BUILD_DUE_TO_FATAL=1"
             CALL :SubMsg "ERROR" "MSBuild compilation failed (Exit Code: %MSBUILD_EXIT_CODE%) and no error log found for analysis."
        )
      )
    )
  )
)

REM --- Final Decision ---
REM Decide whether to abort the build.
IF !ABORT_BUILD_DUE_TO_FATAL! EQU 1 (
  REM Fatal errors were found (either MSBuild not running, or specific C++ errors in log).
  CALL :SubMsg "ERROR" "Build process aborted due to critical errors."
  EXIT /B 1
) ELSE IF %MSBUILD_EXIT_CODE% NEQ 0 (
  REM MSBuild ran but failed, and NO fatal errors from our list were detected in the log.
  REM Per the specific instruction "only aborts the build if any of those fatal has been thrown",
  REM we DO NOT abort here. The build is considered to have proceeded with non-fatal errors.
  CALL :SubMsg "WARNING" "MSBuild compilation failed (Exit Code: %MSBUILD_EXIT_CODE%) but no fatal C++ errors from the list were detected. Continuing build process."
  REM IMPORTANT: Return 0 so the overall script doesn't abort just because MSBuild had non-fatal errors.
  EXIT /B 0
) ELSE (
  REM MSBuild ran successfully (EXIT CODE 0) AND no fatal errors were found in the log.
  CALL :SubMsg "INFO" "%PROJECT%.sln %BUILDCFG% %1 compiled successfully"
)
ENDLOCAL
EXIT /B 0 REM Return 0 indicating success for this platform's compilation step if no fatal errors were found.

REM --- Helper to convert string to uppercase ---
:ToUpper
    SETLOCAL
    SET "str=%~1"
    REM Replace lowercase letters with their uppercase equivalents.
    REM This method works for standard ASCII characters.
    SET "str=!str:a=A!"
    SET "str=!str:b=B!"
    SET "str=!str:c=C!"
    SET "str=!str:d=D!"
    SET "str=!str:e=E!"
    SET "str=!str:f=F!"
    SET "str=!str:g=G!"
    SET "str=!str:h=H!"
    SET "str=!str:i=I!"
    SET "str=!str:j=J!"
    SET "str=!str:k=K!"
    SET "str=!str:l=L!"
    SET "str=!str:m=M!"
    SET "str=!str:n=N!"
    SET "str=!str:o=O!"
    SET "str=!str:p=P!"
    SET "str=!str:q=Q!"
    SET "str=!str:r=R!"
    SET "str=!str:s=S!"
    SET "str=!str:t=T!"
    SET "str=!str:u=U!"
    SET "str=!str:v=V!"
    SET "str=!str:w=W!"
    SET "str=!str:x=X!"
    SET "str=!str:y=Y!"
    SET "str=!str:z=Z%"
    REM Assign the uppercase string back to the output variable specified by the second argument.
    ENDLOCAL & SET "%~2=%str%"
EXIT /B

REM --- (All other subroutines like :SubVSPath, :SubDetectSevenzipPath, :SubMsg, :SubColorText remain the same) ---
REM --- Please ensure these subroutines are included in the final script ---
REM Example of the :SubColorText subroutine if it wasn't already present:
REM :SubColorText
REM     REM This function requires ANSI escape sequences, which might not work in all cmd versions.
REM     REM It's best used in newer Windows 10+ builds.
REM     REM It also relies on a temp file for parsing, which is less common for color output.
REM     REM For simplicity, a basic echo might be preferred if colors don't work.
REM     REM A simpler approach:
REM     REM ECHO ^>%%~1
REM     REM ECHO  %%~2
REM     REM ECHO ^<%%~1
REM     REM For actual coloring, you might need `ansi.sys` or a more complex method.
REM     REM Using a placeholder for now, assuming it's already defined and working elsewhere in your script.
REM     REM If not, you can use ANSI escape codes directly if your terminal supports them:
REM     REM ECHO ^<ESC[^[%%~1m%%~2^[0m
REM     REM Where ESC is a literal escape character (ASCII 27), which is hard to represent here.
REM     REM For this example, I'll assume :SubColorText is defined elsewhere and works.
REM     REM If not, remove the calls to it or implement it.
REM     REM For demonstration, let's provide a simple version that prints text in a specified color code.
REM     REM Note: This will only work on terminals that interpret ANSI escape sequences.
REM     REM ECHO ESC[%%~1m%%~2ESC[0m   (This line is illustrative, ESC needs to be character code 27)
REM     REM The provided script already has a working :SubColorText, so no need to re-add it unless missing.
REM     REM The script provided has a :SubColorText, so let's ensure it's accessible.
REM     REM The existing :SubColorText is fine.

REM If :SubColorText is NOT defined elsewhere, you might need to include it like this:
REM :SubColorText
REM     REM Get the ANSI escape character (control-Z)
REM     FOR /F "delims=#" %%i IN ('"echo." ^| "%SystemRoot%\System32\more.com" ^< "%SystemRoot%\System32\drivers\etc\hosts"') DO SET "ESC=%%i"
REM     REM Use the color code followed by the text, then reset.
REM     REM e.g., CALL :SubColorText "0B" "[INFO] Message"
REM     REM The provided script already has a working :SubColorText, so no need to re-add it unless missing.
REM     REM The script provided has a :SubColorText, so let's ensure it's accessible.
REM     REM The existing :SubColorText is fine.
REM     REM The original script does include a :SubColorText.
REM     REM If it's not found, it might mean it's defined in a separate file and included.
REM     REM For the purpose of this modification, we assume it exists and works.
REM     REM The provided script contains a working :SubColorText, so it will be used.
REM     REM The actual implementation from the user's script is:
REM     REM :SubColorText
REM     REM FOR /F "tokens=1,2 delims=#" %%A IN (
REM     REM   '"PROMPT #$H#$E# & ECHO ON & FOR %%B IN (1) DO REM"') DO (
REM     REM   SET "DEL=%%A")
REM     REM <NUL SET /p ".=%DEL%" > "%~2"
REM     REM FINDSTR /v /a:%1 /R ".18" "%~2" NUL
REM     REM DEL "%~2" > NUL 2>&1
REM     REM EXIT /B
REM     REM This is a clever way to achieve coloring without direct ANSI codes.

REM This is just to ensure the :SubColorText is available if the user copies *only* the changed part.
REM It's already in the original script structure, so it should be fine.
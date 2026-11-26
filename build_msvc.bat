@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo  SampVoice Plus - Building with MSVC (Visual Studio x86)
echo ========================================================

cd /d "%~dp0"

:: Auto-detect CMake in PATH, WinGet, or Visual Studio
where cmake >nul 2>nul
if errorlevel 1 (
    for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\Kitware.CMake_*") do (
        for /d %%C in ("%%D\cmake-*\bin") do (
            if exist "%%C\cmake.exe" set "PATH=%%C;!PATH!"
        )
    )
)
where cmake >nul 2>nul
if errorlevel 1 (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;!PATH!"
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;!PATH!"
    )
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake could not be found. Please ensure CMake or Visual Studio is installed.
    exit /b 1
)

:: 1. Configure & build 32-bit Win32 (Client sampvoice.asi + SA-MP Plugin + open.mp Component)
echo [1/4] Configuring CMake (Win32)...
cmake -B "build" -G "Visual Studio 17 2022" -A Win32 ^
  -DSVPLUS_BUILD_SERVER=ON ^
  -DSVPLUS_BUILD_CLIENT=ON ^
  -DSVPLUS_STATIC_CRT=ON
if errorlevel 1 (
    echo [ERROR] CMake Win32 configuration failed.
    exit /b %errorlevel%
)

echo [2/4] Compiling Win32 Release binaries...
cmake --build "build" --config Release --parallel
if errorlevel 1 (
    echo [ERROR] Win32 compilation failed.
    exit /b %errorlevel%
)

:: 2. Verify Zero Dynamic MSVC Runtime Dependencies + Plus Client Marker & ProductName
echo [3/4] Verifying static CRT linkage and Plus client identity...
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; foreach ($f in @('build\bin\client\sampvoice.asi','build\bin\samp\sampvoice.dll','build\bin\omp\sampvoice.dll')) { if (-not (Test-Path $f)) { throw \"Missing expected binary: $f\" }; $a = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes((Resolve-Path $f))); if ($a -match 'MSVCP\d+\.dll' -or $a -match 'VCRUNTIME\d+\.dll') { throw \"Dynamic MSVC CRT detected in $f\" } }; $asi=(Resolve-Path 'build\bin\client\sampvoice.asi').Path; $asiAscii=[System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($asi)); if (-not $asiAscii.Contains('SampVoice Plus Client')) { throw 'Missing permanent installer marker (SampVoice Plus Client) in sampvoice.asi' }; $pn=[System.Diagnostics.FileVersionInfo]::GetVersionInfo($asi).ProductName; if ($pn -ne 'SampVoice Plus') { throw \"Unexpected VERSIONINFO ProductName in sampvoice.asi: '$pn'\" }; Write-Host 'Static CRT & Plus identity verification passed (0 MSVCP/VCRUNTIME, marker + ProductName verified).'"
if errorlevel 1 (
    echo [ERROR] Binary verification failed.
    exit /b %errorlevel%
)

:: 3. Stage, Optional Installer Build, and Create Zip Package
echo [4/4] Creating distribution packages in dist\...
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $inc=Get-Content 'sampvoice.inc' -Raw; $rx=[regex]'#define\s+SV_PLUS_VERSION_STRING\s+\x22([^\x22]+)\x22'; $m=$rx.Match($inc); if(-not $m.Success){throw 'Version missing in sampvoice.inc'}; $v=$m.Groups[1].Value; $cm=Get-Content 'CMakeLists.txt' -Raw; $rx2=[regex]'project\s*\(\s*SampVoicePlus\s+VERSION\s+(\d+\.\d+\.\d+)'; $m2=$rx2.Match($cm); if(-not $m2.Success -or $m2.Groups[1].Value -ne $v){throw 'CMakeLists.txt version mismatch'}; $pj=Get-Content 'pawn.json' -Raw; $rx3=[regex]'\x22version\x22\s*:\s*\x22([^\x22]+)\x22'; $m3=$rx3.Match($pj); if(-not $m3.Success -or $m3.Groups[1].Value -ne $v){throw 'pawn.json version mismatch'}; [System.IO.File]::WriteAllText('build\_ver.txt',$v)"
if errorlevel 1 (
    echo [ERROR] Version consistency check failed.
    exit /b 1
)
set /p SV_VER=<build\_ver.txt
del /f /q build\_ver.txt >nul 2>nul
if "%SV_VER%"=="" (
    echo [ERROR] Empty version read from sampvoice.inc.
    exit /b 1
)

set "STAGE_SERVER=package-msvc-server"
set "STAGE_CLIENT=package-msvc-client"
if exist "%STAGE_SERVER%" rmdir /s /q "%STAGE_SERVER%"
if exist "%STAGE_CLIENT%" rmdir /s /q "%STAGE_CLIENT%"
if not exist "dist" mkdir "dist"

mkdir "%STAGE_SERVER%\plugins"
mkdir "%STAGE_SERVER%\components"
mkdir "%STAGE_SERVER%\include"
copy /Y "build\bin\samp\sampvoice.dll" "%STAGE_SERVER%\plugins\sampvoice.dll" >nul
copy /Y "build\bin\omp\sampvoice.dll" "%STAGE_SERVER%\components\sampvoice.dll" >nul
if exist "sampvoice.inc" copy /Y "sampvoice.inc" "%STAGE_SERVER%\include\sampvoice.inc" >nul
if exist "Example.pwn" copy /Y "Example.pwn" "%STAGE_SERVER%\Example.pwn" >nul
if exist "LICENSE" copy /Y "LICENSE" "%STAGE_SERVER%\LICENSE" >nul
if exist "THIRD-PARTY-NOTICES.txt" copy /Y "THIRD-PARTY-NOTICES.txt" "%STAGE_SERVER%\THIRD-PARTY-NOTICES.txt" >nul

mkdir "%STAGE_CLIENT%\Manual Install"
copy /Y "build\bin\client\sampvoice.asi" "dist\sampvoice.asi" >nul
copy /Y "build\bin\client\sampvoice.asi" "%STAGE_CLIENT%\Manual Install\sampvoice.asi" >nul
if exist "LICENSE" copy /Y "LICENSE" "%STAGE_CLIENT%\LICENSE" >nul
if exist "THIRD-PARTY-NOTICES.txt" copy /Y "THIRD-PARTY-NOTICES.txt" "%STAGE_CLIENT%\THIRD-PARTY-NOTICES.txt" >nul

set "HAS_INSTALLER=0"
set "ISCC_EXE="
for /f "delims=" %%I in ('where iscc.exe 2^>nul') do if not defined ISCC_EXE set "ISCC_EXE=%%I"
if not defined ISCC_EXE if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"

if not defined ISCC_EXE goto skip_inno
echo     Inno Setup 6 found: !ISCC_EXE!
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $s='build\installer-staging'; $null = New-Item -ItemType Directory -Force -Path $s; $z=Join-Path $s 'Ultimate-ASI-Loader-NoPDB.zip'; $d=Join-Path $s 'dinput8.dll'; $h='73D6DB0481AF99B8481E8E6A685DD385E5A3E0E10518BFE97A993F7F368667EB'; $sha=[System.Security.Cryptography.SHA256]::Create(); $getH={ param($p) if(-not (Test-Path $p)){return ''}; $fs=[System.IO.File]::OpenRead($p); try{ [BitConverter]::ToString($sha.ComputeHash($fs)).Replace('-','') }finally{$fs.Dispose()} }; if ((& $getH $d) -ne $h) { Invoke-WebRequest -Uri 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.4/Ultimate-ASI-Loader-NoPDB.zip' -OutFile $z; tar.exe -xf $z -C $s; if ($LASTEXITCODE -ne 0) { throw 'Failed to extract Ultimate ASI Loader zip' }; if ((& $getH $d) -ne $h) { throw 'Ultimate ASI Loader SHA-256 mismatch!' } }"
if errorlevel 1 (
    echo [ERROR] Failed to download or verify Ultimate ASI Loader.
    exit /b %errorlevel%
)
"!ISCC_EXE!" "/DMyAppVersion=%SV_VER%" "installer\SampVoicePlus.iss"
if errorlevel 1 (
    echo [ERROR] Inno Setup compilation failed.
    exit /b %errorlevel%
)
mkdir "%STAGE_CLIENT%\Installer"
copy /Y "dist\SampVoice-Plus-Setup.exe" "%STAGE_CLIENT%\Installer\SampVoice-Plus-Setup.exe" >nul
set "HAS_INSTALLER=1"
goto stage_instructions

:skip_inno
echo [WARN] Inno Setup 6 (ISCC.exe) not found; skipping SampVoice-Plus-Setup.exe and packaging Manual Install only.

:stage_instructions
if "%HAS_INSTALLER%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File ".github\ci\ClientInstructions.ps1" -Action Stage -TargetDir "%STAGE_CLIENT%" -Version "%SV_VER%" -IncludeInstaller
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File ".github\ci\ClientInstructions.ps1" -Action Stage -TargetDir "%STAGE_CLIENT%" -Version "%SV_VER%"
)
if errorlevel 1 (
    echo [ERROR] Failed to stage 15-language client instructions.
    exit /b %errorlevel%
)
if exist "dist\SampVoice-Plus-Server-Windows.zip" del /f /q "dist\SampVoice-Plus-Server-Windows.zip"
if exist "dist\SampVoice-Plus-Client-Installer.zip" del /f /q "dist\SampVoice-Plus-Client-Installer.zip"
where tar.exe >nul 2>nul
if not errorlevel 1 (
    tar.exe -a -c -f "dist\SampVoice-Plus-Server-Windows.zip" -C "%STAGE_SERVER%" *
    if errorlevel 1 (
        echo [ERROR] Server zip archive creation failed.
        exit /b !errorlevel!
    )
    tar.exe -a -c -f "dist\SampVoice-Plus-Client-Installer.zip" -C "%STAGE_CLIENT%" *
    if errorlevel 1 (
        echo [ERROR] Client zip archive creation failed.
        exit /b !errorlevel!
    )
) else (
    powershell -NoProfile -Command "Import-Module Microsoft.PowerShell.Archive; Compress-Archive -Path '%STAGE_SERVER%\*' -DestinationPath 'dist\SampVoice-Plus-Server-Windows.zip' -Force; Compress-Archive -Path '%STAGE_CLIENT%\*' -DestinationPath 'dist\SampVoice-Plus-Client-Installer.zip' -Force"
    if errorlevel 1 (
        echo [ERROR] Zip archive creation failed.
        exit /b !errorlevel!
    )
)

if exist "%STAGE_SERVER%" rmdir /s /q "%STAGE_SERVER%"
if exist "%STAGE_CLIENT%" rmdir /s /q "%STAGE_CLIENT%"

if "%HAS_INSTALLER%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File ".github\ci\ClientInstructions.ps1" -Action VerifyZip -ZipPath "dist\SampVoice-Plus-Client-Installer.zip" -IncludeInstaller
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File ".github\ci\ClientInstructions.ps1" -Action VerifyZip -ZipPath "dist\SampVoice-Plus-Client-Installer.zip"
)
if errorlevel 1 (
    echo [ERROR] Client zip verification failed.
    exit /b %errorlevel%
)

echo [SUCCESS] Standalone client: dist\sampvoice.asi
if exist "dist\SampVoice-Plus-Setup.exe" echo [SUCCESS] Windows installer: dist\SampVoice-Plus-Setup.exe
echo [SUCCESS] Server package:    dist\SampVoice-Plus-Server-Windows.zip
echo [SUCCESS] Client package:    dist\SampVoice-Plus-Client-Installer.zip

:: Optional auto-deployment if target directory passed via arg1 or OMP_SERVER_DIR
set "TARGET_DIR=%~1"
if "%TARGET_DIR%"=="" set "TARGET_DIR=%OMP_SERVER_DIR%"

if not "%TARGET_DIR%"=="" (
    if exist "%TARGET_DIR%" (
        echo.
        echo Deploying to target directory: %TARGET_DIR%
        if exist "%TARGET_DIR%\components" copy /Y "build\bin\omp\sampvoice.dll" "%TARGET_DIR%\components\sampvoice.dll" >nul
        if exist "%TARGET_DIR%\plugins" copy /Y "build\bin\samp\sampvoice.dll" "%TARGET_DIR%\plugins\sampvoice.dll" >nul
        if exist "%TARGET_DIR%\pawno\include" copy /Y "sampvoice.inc" "%TARGET_DIR%\pawno\include\sampvoice.inc" >nul
        if exist "%TARGET_DIR%\qawno\include" copy /Y "sampvoice.inc" "%TARGET_DIR%\qawno\include\sampvoice.inc" >nul
        echo Deployment complete.
    )
)

echo ========================================================
echo  Build finished successfully.
echo ========================================================

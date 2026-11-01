@echo off
rem Builds build\InfiniteRasaLauncherXP.exe, a 32-bit exe that runs on Windows XP and later.
rem Needs Visual Studio (or Build Tools) with "Desktop development with C++".
rem   build.cmd          release build
rem   build.cmd clean    remove the build folder first
setlocal enableextensions
cd /d "%~dp0"

if /i "%~1"=="clean" if exist build rmdir /s /q build

if not defined VSCMD_VER (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        echo Visual Studio was not found. Install the Build Tools with "Desktop development with C++".
        exit /b 1
    )
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if not defined VSCMD_VER (
    if not defined VSDIR (
        echo The Visual C++ tools were not found. Add "Desktop development with C++" in the Visual Studio Installer.
        exit /b 1
    )
    set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
    call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
    echo This must be built for x86; run it from a plain prompt or an x86 developer prompt.
    exit /b 1
)

if not exist build\obj\bearssl mkdir build\obj\bearssl
if not exist build\obj\app mkdir build\obj\app

rem XP: target NT 5.1 headers, no thread-safe statics (they need Vista's TLS support), and x87 floating
rem point so our code runs on CPUs without SSE2 (Pentium III, Athlon XP).
set "CFLAGS=/nologo /O2 /MT /W3 /GS /Gy /utf-8 /D_WIN32_WINNT=0x0501 /DWINVER=0x0501 /D_CRT_SECURE_NO_WARNINGS /Zc:threadSafeInit- /arch:IA32"

if not exist build\bearssl.lib (
    echo Building BearSSL...
    dir /s /b third_party\bearssl\src\*.c > build\bearssl.rsp
    cl %CFLAGS% /MP /c /Ithird_party\bearssl\inc /Ithird_party\bearssl\src /Fobuild\obj\bearssl\ @build\bearssl.rsp >build\bearssl.log || (type build\bearssl.log & exit /b 1)
    lib /nologo /out:build\bearssl.lib build\obj\bearssl\*.obj || exit /b 1
)

echo Building launcher...
cl %CFLAGS% /MP /c /std:c++17 /EHsc /DUNICODE /D_UNICODE /Ithird_party\bearssl\inc /Fobuild\obj\app\ src\*.cpp || exit /b 1
ml /nologo /c /coff /Fobuild\obj\app\xp_imports.obj src\xp_imports.asm >nul || exit /b 1
rc /nologo /fo build\obj\app\launcher.res res\launcher.rc || exit /b 1

link /nologo /OUT:build\InfiniteRasaLauncherXP.exe /SUBSYSTEM:WINDOWS,5.01 /MANIFEST:NO /OPT:REF /OPT:ICF ^
    /DYNAMICBASE /NXCOMPAT /LARGEADDRESSAWARE ^
    build\obj\app\*.obj build\obj\app\launcher.res build\bearssl.lib ^
    kernel32.lib user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib crypt32.lib ^
    ws2_32.lib rpcrt4.lib gdiplus.lib msimg32.lib || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File tools\check-xp-imports.ps1 build\InfiniteRasaLauncherXP.exe || exit /b 1
echo Built build\InfiniteRasaLauncherXP.exe

if /i not "%~1"=="test" exit /b 0
echo Building tests...
if not exist build\obj\tests mkdir build\obj\tests
cl %CFLAGS% /c /std:c++17 /EHsc /DUNICODE /D_UNICODE /Fobuild\obj\tests\ tests\tests.cpp || exit /b 1
rem The app objects minus main.obj, plus the embedded file manifest the verify test reads.
link /nologo /OUT:build\tests.exe /SUBSYSTEM:CONSOLE,5.01 /MANIFEST:NO build\obj\tests\tests.obj ^
    build\obj\app\dialogs.obj build\obj\app\game.obj build\obj\app\https.obj build\obj\app\json.obj ^
    build\obj\app\news.obj build\obj\app\settings.obj build\obj\app\sha256.obj build\obj\app\util.obj ^
    build\obj\app\xp_shims.obj build\obj\app\xp_imports.obj build\obj\app\launcher.res build\bearssl.lib ^
    kernel32.lib user32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib crypt32.lib ws2_32.lib rpcrt4.lib || exit /b 1
build\tests.exe %2

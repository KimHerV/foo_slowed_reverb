@echo off
rem Usage: build.cmd [x86] [x64] [arm64ec] [all]   (default: x64)
rem Builds each architecture into build\<arch>\ and packages all built DLLs into build\foo_slowed_reverb.fb2k-component.
rem Shared SDK lives next to the repo (not committed): ..\_SDK\SDK-<date>, libraries built once per platform with
rem ..\_SDK\build_sdk.cmd. Set SDK_DIR to use a different location (CI does).
setlocal
cd /d "%~dp0"
if "%SDK_DIR%"=="" (set "SDK=%~dp0..\_SDK\SDK-2026-10-01") else (set "SDK=%SDK_DIR%")
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VS=%%i"
if not exist "%VS%\VC\Auxiliary\Build\vcvarsall.bat" (echo Visual Studio not found & exit /b 1)

set ARCHES=%*
if "%ARCHES%"=="" set ARCHES=x64
if /i "%ARCHES%"=="all" set ARCHES=x86 x64 arm64ec
if not exist build mkdir build
for %%A in (%ARCHES%) do (
  call :build %%A || exit /b 1
)

rem Package: .fb2k-component is a zip. x86 DLL at the root, others in per-architecture subfolders.
if exist build\pkg rmdir /s /q build\pkg
mkdir build\pkg
if exist build\x86\foo_slowed_reverb.dll copy /y build\x86\foo_slowed_reverb.dll build\pkg\ >nul
for %%A in (x64 arm64ec) do if exist build\%%A\foo_slowed_reverb.dll (
  mkdir build\pkg\%%A
  copy /y build\%%A\foo_slowed_reverb.dll build\pkg\%%A\ >nul
)
if exist build\foo_slowed_reverb.fb2k-component del build\foo_slowed_reverb.fb2k-component
rem Entry order matters: foobar2000 applies x64\ / arm64ec\ over the root files in archive order, so the root (x86)
rem entries must come first or they overwrite the 64-bit DLL. Compress-Archive doesn't guarantee order, so write the zip directly.
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $z = [IO.Compression.ZipFile]::Open('build\foo_slowed_reverb.fb2k-component', 'Create'); try { foreach ($f in Get-ChildItem build\pkg -File) { [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, $f.FullName, $f.Name) }; foreach ($d in 'x64', 'arm64ec') { foreach ($f in Get-ChildItem build\pkg\$d -File -ErrorAction SilentlyContinue) { [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, $f.FullName, $d + '/' + $f.Name) } } } finally { $z.Dispose() }" || exit /b 1
echo PACKAGE OK
exit /b 0

:build
setlocal
set ARCH=%1
set "PLAT=" & set "CLX="
if /i "%ARCH%"=="x86" (
  set "VCARGS=x86" & set "PLAT=Win32" & set "LIBDIR=Release" & set "LINKX=/MACHINE:X86"
)
if /i "%ARCH%"=="x64" (
  set "VCARGS=x64" & set "PLAT=x64" & set "LIBDIR=x64\Release" & set "LINKX=/MACHINE:X64"
)
if /i "%ARCH%"=="arm64ec" (
  set "VCARGS=x64_arm64" & set "PLAT=ARM64EC" & set "LIBDIR=ARM64EC\Release" & set "CLX=/arm64EC" & set "LINKX=/MACHINE:ARM64EC"
)
if "%PLAT%"=="" (echo Unknown architecture: %ARCH% & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" %VCARGS% >nul || (echo vcvarsall %VCARGS% failed & exit /b 1)
set OUT=build\%ARCH%
if not exist %OUT% mkdir %OUT%
set INC=/I "%SDK%" /I "%SDK%\foobar2000" /I src
echo === %ARCH% ===
rc /nologo /fo %OUT%\foo_slowed_reverb.res src\foo_slowed_reverb.rc || exit /b 1
rem Columns UI SDK (in the SDK folder as columns_ui-sdk), built into a static library so only what is used gets linked.
if not exist %OUT%\cui mkdir %OUT%\cui
cl /nologo /c %CLX% /O2 /EHsc /std:c++20 /MD /utf-8 /W0 /DNDEBUG /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS %INC% /Fo"%OUT%\cui\\" "%SDK%\columns_ui-sdk\*.cpp" >nul || exit /b 1
lib /nologo %LINKX% /OUT:%OUT%\columns_ui-sdk.lib %OUT%\cui\*.obj || exit /b 1
cl /nologo /c %CLX% /O2 /EHsc /std:c++20 /MD /utf-8 /W3 /DNDEBUG /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS %INC% /Fo"%OUT%\\" src\main.cpp src\dsp_slowed_reverb.cpp src\export.cpp src\live.cpp src\panel.cpp src\theme.cpp src\sliders.cpp src\presets.cpp || exit /b 1
link /nologo /DLL /LTCG %LINKX% /OUT:%OUT%\foo_slowed_reverb.dll %OUT%\main.obj %OUT%\dsp_slowed_reverb.obj %OUT%\export.obj %OUT%\live.obj %OUT%\panel.obj %OUT%\theme.obj %OUT%\sliders.obj %OUT%\presets.obj %OUT%\foo_slowed_reverb.res ^
  "%SDK%\foobar2000\foobar2000_component_client\%LIBDIR%\foobar2000_component_client.lib" ^
  "%SDK%\foobar2000\SDK\%LIBDIR%\foobar2000_SDK.lib" ^
  "%SDK%\pfc\%LIBDIR%\pfc.lib" ^
  "%SDK%\foobar2000\shared\shared-%PLAT%.lib" ^
  %OUT%\columns_ui-sdk.lib ^
  user32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib advapi32.lib gdi32.lib gdiplus.lib || exit /b 1
echo BUILD OK (%ARCH%)
exit /b 0

@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
rem pushd/popd: callers run test\test_core.exe from where they were.
pushd "%~dp0"
cl /nologo /O2 /EHsc /std:c++20 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS test_core.cpp /Fe:test_core.exe
set "ERR=%ERRORLEVEL%"
popd
exit /b %ERR%

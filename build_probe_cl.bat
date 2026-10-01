@echo off
setlocal
set "MSVC=E:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.29.30133"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "PROJ=D:\mod3\MHW plugins\dll\WeaponSoundEnhance"
set "OUT=%PROJ%\out_probe\x64\Release"
set "OBJ=%PROJ%\out_probe\obj\x64\Release"

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /LD /O2 /MT /std:c++17 /EHsc /utf-8 /DNDEBUG ^
 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WINDOWS /D_USRDLL ^
 /I"%MSVC%\include" /I"%SDK%\Include\%SDKVER%\ucrt" /I"%SDK%\Include\%SDKVER%\shared" /I"%SDK%\Include\%SDKVER%\um" ^
 "%PROJ%\SonarLayerProbe.cpp" ^
 /Fo"%OBJ%\\" /Fe"%OUT%\SonarLayerProbe.dll" ^
 /link /LIBPATH:"%MSVC%\lib\x64" /LIBPATH:"%SDK%\Lib\%SDKVER%\ucrt\x64" /LIBPATH:"%SDK%\Lib\%SDKVER%\um\x64" ^
 kernel32.lib user32.lib

if errorlevel 1 ( echo [FAILED] & exit /b 1 )
echo [OK] %OUT%\SonarLayerProbe.dll
endlocal

@echo off
setlocal
set "MSVC=E:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.29.30133"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "PROJ=D:\mod3\MHW plugins\dll\WeaponSoundEnhance"
set "OUT=%PROJ%\out\x64\Release"
set "OBJ=%PROJ%\out\obj\x64\Release"

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /c /O2 /MT /std:c++17 /EHsc /utf-8 /DNDEBUG ^
 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WINDOWS /D_USRDLL ^
 /I"%PROJ%\src\wwise" /I"%PROJ%\third_party\minhook" ^
 /I"%MSVC%\include" /I"%SDK%\Include\%SDKVER%\ucrt" /I"%SDK%\Include\%SDKVER%\shared" /I"%SDK%\Include\%SDKVER%\um" ^
 "%PROJ%\WeaponSoundEnhance.cpp" "%PROJ%\src\wwise\SonarAudio.cpp" "%PROJ%\src\wwise\SonarWwise.cpp" "%PROJ%\src\wwise\SonarBanks.cpp" "%PROJ%\src\wwise\SonarIpc.cpp" ^
 /Fo"%OBJ%\\"

if errorlevel 1 ( echo [COMPILE FAILED] & exit /b 1 )

"%SDK%\bin\%SDKVER%\x64\rc.exe" /nologo ^
 /i "%SDK%\Include\%SDKVER%\um" /i "%SDK%\Include\%SDKVER%\shared" /i "%SDK%\Include\%SDKVER%\ucrt" ^
 /fo "%OBJ%\resource.res" "%PROJ%\resource.rc"
if errorlevel 1 ( echo [RC FAILED] & exit /b 1 )

"%MSVC%\bin\Hostx64\x64\link.exe" /nologo /DLL /OUT:"%OUT%\WeaponSoundEnhance.dll" ^
 /LIBPATH:"%MSVC%\lib\x64" /LIBPATH:"%SDK%\Lib\%SDKVER%\ucrt\x64" /LIBPATH:"%SDK%\Lib\%SDKVER%\um\x64" ^
 /LIBPATH:"%PROJ%\third_party\minhook" ^
 "%OBJ%\WeaponSoundEnhance.obj" "%OBJ%\SonarAudio.obj" "%OBJ%\SonarWwise.obj" "%OBJ%\SonarBanks.obj" "%OBJ%\SonarIpc.obj" "%OBJ%\resource.res" ^
 libMinHook.x64.lib kernel32.lib user32.lib

if errorlevel 1 ( echo [LINK FAILED] & exit /b 1 )
echo [OK] %OUT%\WeaponSoundEnhance.dll
endlocal

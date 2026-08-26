@echo off
setlocal enabledelayedexpansion
rem ============================================================================
rem  WeaponSoundEnhanceGUI —— 一键构建脚本 (需要 VS2019/2022 C++ 工具链)
rem  双击运行即可，产物输出到 .\out\x64\Release\WeaponSoundEnhanceGUI.exe
rem ============================================================================
cd /d "%~dp0"

set "OUT=out\x64\Release"
if not exist "%OUT%" mkdir "%OUT%"

rem 用 vswhere 定位 MSBuild
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\MSBuild\Current\Bin\MSBuild.exe" (
            set "MSBUILD=%%i\MSBuild\Current\Bin\MSBuild.exe"
        )
    )
)

if not defined MSBUILD (
    echo [ERROR] 未找到 MSBuild / Visual Studio C++ 工具链。
    echo        请安装 "使用 C++ 的桌面开发" 工作负载后重试。
    pause
    exit /b 1
)

echo [build] Using: %MSBUILD%
"%MSBUILD%" "%~dp0WeaponSoundEnhanceGUI.vcxproj" /p:Configuration=Release /p:Platform=x64 /m
if errorlevel 1 (
    echo [ERROR] 构建失败。
    pause
    exit /b 1
)

if exist "%OUT%\WeaponSoundEnhanceGUI.exe" (
    echo.
    echo [success] 生成的配置工具: %CD%\%OUT%\WeaponSoundEnhanceGUI.exe
    echo          请把它放到 nativePC\plugins\ 与 WeaponSoundEnhance.dll 同目录使用。
) else (
    echo [warn] 未找到输出 exe，请检查构建日志。
)
pause
exit /b 0

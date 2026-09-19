#!/bin/sh
# 用 w64devkit (MinGW-w64) 构建 WeaponSoundEnhance 的 DLL 和 GUI。
# 官方走的是 MSVC (build.bat / vcxproj)，这份是免安装工具链的等价物。
#
#   用法：  sh build-mingw.sh [dll|gui|all]      默认 all
#
# 唯一需要改源码的地方：gui/src/main.cpp 里那处 SEH (__try/__except) 是 MSVC 专有语法，
# 已加 #ifdef _MSC_VER 包住；非 MSVC 下退化为直接调用（崩溃保护失效，其余一致）。

set -e
export PATH="/d/w64devkit/bin:$PATH"
cd "$(dirname "$0")"
IM=gui/third_party/imgui
WHAT="${1:-all}"

COMMON="-O2 -std=c++17 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -static-libgcc -static-libstdc++"

if [ "$WHAT" = "dll" ] || [ "$WHAT" = "all" ]; then
    echo "==> 构建 WeaponSoundEnhance.dll"
    mkdir -p out
    g++ -shared $COMMON -D_WINDOWS -D_USRDLL \
        -o out/WeaponSoundEnhance.dll WeaponSoundEnhance.cpp \
        -lole32 -luuid -lshell32 -lwinmm
    ls -l out/WeaponSoundEnhance.dll
fi

if [ "$WHAT" = "gui" ] || [ "$WHAT" = "all" ]; then
    echo "==> 构建 WeaponSoundEnhanceGUI.exe"
    mkdir -p gui/out
    windres gui/resource.rc -O coff -o gui/out/resource.o
    # 注意：入口是 WinMain（ANSI），不要加 -municode，否则链接器找 wWinMain 会失败
    g++ $COMMON -mwindows -DUNICODE -D_UNICODE \
        -I gui/src -I "$IM" -I "$IM/backends" \
        gui/src/main.cpp gui/src/app.cpp gui/src/config.cpp gui/src/fsmdb.cpp gui/src/game.cpp \
        "$IM/imgui.cpp" "$IM/imgui_draw.cpp" "$IM/imgui_tables.cpp" "$IM/imgui_widgets.cpp" \
        "$IM/backends/imgui_impl_win32.cpp" "$IM/backends/imgui_impl_dx11.cpp" \
        gui/out/resource.o \
        -o gui/out/WeaponSoundEnhanceGUI.exe \
        -ld3d11 -ld3dcompiler -ldwmapi -lcomdlg32 -lole32 -lshell32 -lwinmm -lgdi32 -lurlmon
    ls -l gui/out/WeaponSoundEnhanceGUI.exe
fi

echo "==> 完成"

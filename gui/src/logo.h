#pragma once
// ---------------------------------------------------------------------------
//  Sonar GUI 的界面小图标（标题栏旁边那只绿色猫）
//
//  用 Windows 自带的 WIC 解 PNG（不引入 stb_image 之类的新依赖），
//  再建一张 D3D11 纹理交给 ImGui 画。三个 ImGui 上下文各有一台 D3D 设备，
//  所以纹理按设备分别缓存（见 gui/src/main.cpp 的调用点）。
//
//  加载失败一律返回 0，界面只是不显示图标，绝不影响功能。
// ---------------------------------------------------------------------------
// 【坑】third_party/stb_vorbis.c 里有 `#define R (PLAYBACK_RIGHT | PLAYBACK_MONO)`
// 且从不 undef。只要它先于 d3d11.h 被包含（app.cpp 走 wse_audio.h 就会），
// d3d11.h 里的 `struct D3D11_VIDEO_COLOR_RGBA { float R; ... }` 就会被展开成
// `float (4|1);`，报一个莫名其妙的语法错误。这里在拉 d3d11.h 之前把它清掉。
#ifdef R
#undef R
#endif

#include <windows.h>   // 必须先有 windows.h，d3d11.h 依赖它的基础类型
#include <d3d11.h>
#include <string>

// 把 PNG 文件读成 D3D11 纹理；失败返回 nullptr。
ID3D11ShaderResourceView* WseLoadPngTexture(ID3D11Device* device, const wchar_t* path);

// 带缓存：同一台设备 + 同一路径只加载一次。
ID3D11ShaderResourceView* WseGetLogoTexture(ID3D11Device* device, const wchar_t* path);

// 释放缓存（退出时调用；设备本身由调用方释放）。
void WseFreeLogoTextures();

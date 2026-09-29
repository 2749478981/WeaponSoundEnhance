// Sonar GUI 的界面图标加载（WIC 解 PNG -> D3D11 纹理），见 logo.h 的说明。
#include "logo.h"

#include <wincodec.h>
#include <vector>
#include <map>

#pragma comment(lib, "windowscodecs.lib")

namespace {

struct Cached {
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11Device*             device = nullptr;
};
// key = 设备指针 + 路径；退出时统一释放
std::map<std::wstring, Cached>& Cache() {
    static std::map<std::wstring, Cached> c;
    return c;
}

std::wstring MakeKey(ID3D11Device* device, const wchar_t* path) {
    wchar_t buf[64];
    swprintf_s(buf, L"%p|", (void*)device);
    return std::wstring(buf) + (path ? path : L"");
}

}  // namespace

ID3D11ShaderResourceView* WseLoadPngTexture(ID3D11Device* device, const wchar_t* path) {
    if (!device || !path || !*path) return nullptr;

    // WIC 初始化（进程内首次调用时建工厂；COM 已由 main.cpp 初始化）
    static IWICImagingFactory* factory = nullptr;
    if (!factory) {
        if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory))) || !factory) {
            factory = nullptr;
            return nullptr;
        }
    }

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)) ||
        !decoder) {
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter*   conv  = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;

    do {
        if (FAILED(decoder->GetFrame(0, &frame)) || !frame) break;
        if (FAILED(factory->CreateFormatConverter(&conv)) || !conv) break;
        // 统一转成 32bpp BGRA（含 alpha，右下角透明区才能正确透出背景）
        if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeCustom))) break;

        UINT w = 0, h = 0;
        if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0) break;
        // 图标不需要太大：超过 512 就等比缩小，省显存也避免噪点
        if (w > 512 || h > 512) {
            const float k = (w > h) ? (512.0f / (float)w) : (512.0f / (float)h);
            const UINT nw = (UINT)((float)w * k);
            const UINT nh = (UINT)((float)h * k);
            IWICBitmapScaler* scaler = nullptr;
            if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && scaler) {
                if (SUCCEEDED(scaler->Initialize(conv, nw, nh,
                                                 WICBitmapInterpolationModeFant))) {
                    IWICFormatConverter* conv2 = nullptr;
                    if (SUCCEEDED(factory->CreateFormatConverter(&conv2)) && conv2 &&
                        SUCCEEDED(conv2->Initialize(scaler, GUID_WICPixelFormat32bppBGRA,
                                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                                    WICBitmapPaletteTypeCustom))) {
                        conv->Release();
                        conv = conv2;   // conv2 的所有权转给 conv
                        conv2 = nullptr;
                        w = nw;
                        h = nh;
                    }
                    if (conv2) conv2->Release();
                }
                scaler->Release();
            }
        }

        const UINT stride = w * 4;
        std::vector<BYTE> pixels((size_t)stride * h);
        if (FAILED(conv->CopyPixels(nullptr, stride, (UINT)pixels.size(), pixels.data()))) break;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = pixels.data();
        sd.SysMemPitch = stride;

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&td, &sd, &tex)) || !tex) break;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = td.Format;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(tex, &srvd, &srv))) srv = nullptr;
        tex->Release();
    } while (false);

    if (conv) conv->Release();
    if (frame) frame->Release();
    decoder->Release();
    return srv;
}

ID3D11ShaderResourceView* WseGetLogoTexture(ID3D11Device* device, const wchar_t* path) {
    if (!device || !path || !*path) return nullptr;
    const std::wstring key = MakeKey(device, path);
    auto it = Cache().find(key);
    if (it != Cache().end()) return it->second.srv;

    Cached c;
    c.device = device;
    c.srv = WseLoadPngTexture(device, path);   // 失败也缓存（nullptr），避免每帧重试读盘
    Cache()[key] = c;
    return c.srv;
}

void WseFreeLogoTextures() {
    for (auto& kv : Cache()) {
        if (kv.second.srv) kv.second.srv->Release();
    }
    Cache().clear();
}

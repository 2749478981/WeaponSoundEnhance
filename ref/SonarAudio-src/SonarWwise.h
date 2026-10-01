// ===========================================================================
//  SonarWwise.h —— 游戏内 Wwise 符号解析 + 播放入口封装
//
//  MHW 把 Wwise 静态链接进 MonsterHunterWorld.exe，但**导出了 mangled 名字**
//  （见 ref/wwise_symbols.txt，205 个符号，地址在 0x143E71CBF..0x143E765C6，
//   落在 PE 的导出目录里）。
//
//  所以拿函数的正确姿势是：
//    1) GetModuleHandleW(nullptr) 拿 exe 基址（ASLR 安全）
//    2) 手工解析 PE 导出表，按 **导出名精确匹配** 找
//         ?PostEvent@SoundEngine@AK@@YA?AW4AKRESULT@@PEBD...
//       而不是把 0x143E73DA7 这种绝对地址写死（换版本必失效，
//       且 wwise_symbols.txt 里的绝对地址是基于 0x140000000 镜像基址的）。
//    3) 用 RVA（exportAddr - 0x140000000 的语义）换算：我们直接信任导出表的
//       运行时地址，天然 ASLR 安全。
//
//  若导出表里找不到（例如某些打包器剥掉导出），退化为按 AOB/绝对地址，
//  但那属于兜底，会打日志警告。
// ===========================================================================
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

namespace sonaraudio {

// ---- Wwise 类型（只声明用得到的部分，别引整个 SDK）--------------------------
// AKRESULT: AK_Success=1, AK_InvalidParameter=2, ...
enum { AK_Success = 1 };
// AkActionOnEventType
enum { AkActionOnEventType_Stop = 0, AkActionOnEventType_Pause = 1,
       AkActionOnEventType_Resume = 2, AkActionOnEventType_Break = 3,
       AkActionOnEventType_ReleaseEnvelope = 4 };
// AkCurveInterpolation
enum { AkCurveInterpolation_Linear = 4 };

// ---- 函数指针类型（签名照 wwise_symbols.txt 的 mangled 名解出来）-----------
// AKRESULT PostEvent(AkUniqueID in_eventID, AkGameObjectID in_gameObjectID,
//                    AkUInt32 in_uFlags, AkCallbackFunc in_pfnCallback,
//                    void* in_pCookie, AkUInt32 in_cExternals,
//                    AkExternalSourceInfo* in_pExternalSources,
//                    AkPlayingID in_PlayingID)
using fn_PostEvent_byID_t = uint32_t (*)(uint32_t /*eventID*/, uint64_t /*gameObjID*/,
                                         uint32_t /*flags*/, void* /*cb*/, void* /*cookie*/,
                                         uint32_t /*nExt*/, void* /*ext*/, uint32_t /*playingID*/);
// AKRESULT PostEvent(const char* in_pszEventName, ...)  —— 同上，首参换成名字
using fn_PostEvent_byName_t = uint32_t (*)(const char* /*pszEventName*/, uint64_t /*gameObjID*/,
                                           uint32_t /*flags*/, void* /*cb*/, void* /*cookie*/,
                                           uint32_t /*nExt*/, void* /*ext*/, uint32_t /*playingID*/);
// const wchar_t* 版本
using fn_PostEvent_byNameW_t = uint32_t (*)(const wchar_t* /*pszEventName*/, uint64_t /*gameObjID*/,
                                            uint32_t /*flags*/, void* /*cb*/, void* /*cookie*/,
                                            uint32_t /*nExt*/, void* /*ext*/, uint32_t /*playingID*/);

// AkUniqueID GetEventIDFromPlayingID(AkPlayingID)  —— Query::SoundEngine::AK
using fn_GetEventIDFromPlayingID_t = uint32_t (*)(uint32_t /*playingID*/);

// AKRESULT GetPlayingIDsFromGameObject(AkGameObjectID, AkUInt32& io_numIDs, AkPlayingID* out)
using fn_GetPlayingIDsFromGameObject_t = uint32_t (*)(uint64_t /*gameObjID*/, uint32_t* /*inout num*/,
                                                      uint32_t* /*out ids*/);

// void StopPlayingID(AkPlayingID, AkTimeMs, AkCurveInterpolation)
using fn_StopPlayingID_t = void (*)(uint32_t /*playingID*/, int32_t /*ms*/, uint32_t /*curve*/);

// AKRESULT StopAll(AkGameObjectID)
using fn_StopAll_t = void (*)(uint64_t /*gameObjID*/);

// ---- 解析结果 ---------------------------------------------------------------
struct WwiseApi {
    fn_PostEvent_byID_t                 postEvent_byID     = nullptr;
    fn_PostEvent_byName_t               postEvent_byName   = nullptr;
    fn_PostEvent_byNameW_t              postEvent_byNameW  = nullptr;
    fn_GetEventIDFromPlayingID_t        getEventIDFromPlayingID = nullptr;
    fn_GetPlayingIDsFromGameObject_t    getPlayingIDsFromGameObject = nullptr;
    fn_StopPlayingID_t                  stopPlayingID      = nullptr;
    fn_StopAll_t                        stopAll            = nullptr;

    uintptr_t exe_base = 0;   // 运行时 exe 基址（ASLR 解算后）

    bool ok() const { return postEvent_byID != nullptr || postEvent_byName != nullptr; }
};

// 解析入口。返回是否至少拿到 byID 或 byName 的 PostEvent。
bool ResolveWwiseApi(WwiseApi& out, std::string& log);

// ---- 符号名常量（mangled，直接抄自 ref/wwise_symbols.txt）-------------------
// 只列我们要用的；其余按需再加。
namespace sym {
inline constexpr const char* kPostEvent_byID =
    "?PostEvent@SoundEngine@AK@@YAKK_KKP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXKPEAUAkExternalSourceInfo@@K@Z";
inline constexpr const char* kPostEvent_byName =
    "?PostEvent@SoundEngine@AK@@YAKPEBD_KKP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXKPEAUAkExternalSourceInfo@@K@Z";
inline constexpr const char* kPostEvent_byNameW =
    "?PostEvent@SoundEngine@AK@@YAKPEB_W_KKP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXKPEAUAkExternalSourceInfo@@K@Z";
inline constexpr const char* kGetEventIDFromPlayingID =
    "?GetEventIDFromPlayingID@Query@SoundEngine@AK@@YAKK@Z";
inline constexpr const char* kGetPlayingIDsFromGameObject =
    "?GetPlayingIDsFromGameObject@Query@SoundEngine@AK@@YA?AW4AKRESULT@@_KAEAKPEAK@Z";
inline constexpr const char* kStopPlayingID =
    "?StopPlayingID@SoundEngine@AK@@YAXKJW4AkCurveInterpolation@@@Z";
inline constexpr const char* kStopAll =
    "?StopAll@SoundEngine@AK@@YAX_K@Z";
}  // namespace sym

}  // namespace sonaraudio

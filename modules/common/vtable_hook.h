// filepath: modules/common/vtable_hook.h
// Lightweight x86/x64 vtable trampoline for COM interface hooks.
//
// Usage:
//   struct OriginalD3D9Device {
//       using Present_t = HRESULT (STDMETHODCALLTYPE *)(IDirect3DDevice9*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*);
//       using EndScene_t = HRESULT (STDMETHODCALLTYPE *)(IDirect3DDevice9*);
//       EndScene_t  EndScene  = nullptr;
//       Present_t   Present   = nullptr;
//   };
//   OriginalD3D9Device g_original;
//   InstallVTableHook(static_cast<void***>(pDevice),
//                     { { kIDirect3DDevice9_Present,  &HookedPresent },
//                       { kIDirect3DDevice9_EndScene, &HookedEndScene } },
//                     &g_original);

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstddef>
#include <cstdint>

#include "logging.h"

namespace omnirender {

// Index of a vtable entry. For a COM interface, the vtable is a flat
// array of function pointers; entry 0 is QueryInterface, 1 is AddRef,
// 2 is Release. Higher indices are interface-specific.
using VTableIndex = std::size_t;

struct VTableHook {
    VTableIndex      index;       // entry to replace
    void*            replacement; // function pointer to install
};

// Persist the original vtable entry by index. Defined here in the header
// so it can be instantiated at the call site; only the specialisations
// listed below are used by the project. Each `requires` clause is
// checked at compile time and silently skipped when the matching
// member is absent on the caller's Originals struct.
template <typename Originals>
inline void StoreOriginal(Originals& originals, VTableIndex index, void* fn) noexcept {
    if (!fn) return;
    switch (index) {
    case 0:  if constexpr (requires { originals.QueryInterface; }) originals.QueryInterface = reinterpret_cast<decltype(originals.QueryInterface)>(fn); break;
    case 1:  if constexpr (requires { originals.AddRef;        }) originals.AddRef        = reinterpret_cast<decltype(originals.AddRef)>(fn);        break;
    case 2:  if constexpr (requires { originals.Release;       }) originals.Release       = reinterpret_cast<decltype(originals.Release)>(fn);       break;
    case 3:  if constexpr (requires { originals.EndScene;      }) originals.EndScene      = reinterpret_cast<decltype(originals.EndScene)>(fn);      break;
    case 4:  if constexpr (requires { originals.Present;       }) originals.Present       = reinterpret_cast<decltype(originals.Present)>(fn);       break;
    case 5:  if constexpr (requires { originals.SetDepthStencilSurface; }) originals.SetDepthStencilSurface = reinterpret_cast<decltype(originals.SetDepthStencilSurface)>(fn); break;
    case 6:  if constexpr (requires { originals.Reset;         }) originals.Reset         = reinterpret_cast<decltype(originals.Reset)>(fn);         break;
    case 7:  if constexpr (requires { originals.CreateTexture; }) originals.CreateTexture = reinterpret_cast<decltype(originals.CreateTexture)>(fn); break;
    case 8:  if constexpr (requires { originals.CreateRenderTarget; }) originals.CreateRenderTarget = reinterpret_cast<decltype(originals.CreateRenderTarget)>(fn); break;
    case 9:  if constexpr (requires { originals.GetBackBuffer; }) originals.GetBackBuffer = reinterpret_cast<decltype(originals.GetBackBuffer)>(fn); break;
    case 10: if constexpr (requires { originals.BeginScene;     }) originals.BeginScene    = reinterpret_cast<decltype(originals.BeginScene)>(fn);    break;
    case 11: if constexpr (requires { originals.GetDisplayMode; }) originals.GetDisplayMode = reinterpret_cast<decltype(originals.GetDisplayMode)>(fn); break;
    case 12: if constexpr (requires { originals.Clear;         }) originals.Clear         = reinterpret_cast<decltype(originals.Clear)>(fn);         break;
    case 13: if constexpr (requires { originals.DrawPrimitive; }) originals.DrawPrimitive = reinterpret_cast<decltype(originals.DrawPrimitive)>(fn); break;
    case 14: if constexpr (requires { originals.SetTransform;  }) originals.SetTransform  = reinterpret_cast<decltype(originals.SetTransform)>(fn);  break;
    case 15: if constexpr (requires { originals.SetRenderState;}) originals.SetRenderState= reinterpret_cast<decltype(originals.SetRenderState)>(fn);break;
    case 16: if constexpr (requires { originals.SetTexture;    }) originals.SetTexture    = reinterpret_cast<decltype(originals.SetTexture)>(fn);    break;
    case 17: if constexpr (requires { originals.SetStreamSource;}) originals.SetStreamSource= reinterpret_cast<decltype(originals.SetStreamSource)>(fn);break;
    case 18: if constexpr (requires { originals.SetIndices;    }) originals.SetIndices    = reinterpret_cast<decltype(originals.SetIndices)>(fn);    break;
    case 19: if constexpr (requires { originals.DrawIndexedPrimitive; }) originals.DrawIndexedPrimitive = reinterpret_cast<decltype(originals.DrawIndexedPrimitive)>(fn); break;
    default: break;
    }
}

template <typename Originals = void>
bool InstallVTableHook(void** vtable,
                       std::initializer_list<VTableHook> hooks,
                       Originals* out_originals = nullptr) noexcept {
    if (!vtable) return false;

    size_t max_index = 0;
    for (const auto& hook : hooks) {
        if (hook.index > max_index) max_index = hook.index;
    }
    size_t bytes_to_protect = (max_index + 1) * sizeof(void*);

    DWORD old_protect = 0;
    if (!::VirtualProtect(vtable, bytes_to_protect, PAGE_READWRITE, &old_protect)) {
        OMNI_LOG_ERROR("VirtualProtect failed: %lu", ::GetLastError());
        return false;
    }

    for (const auto& hook : hooks) {
        if (vtable[hook.index] == hook.replacement) continue;
        void* original = vtable[hook.index];
        if constexpr (!std::is_void_v<Originals>) {
            if (out_originals) {
                StoreOriginal<Originals>(*out_originals, hook.index, original);
            }
        }
        vtable[hook.index] = hook.replacement;
    }

    ::VirtualProtect(vtable, bytes_to_protect, old_protect, &old_protect);
    return true;
}

}  // namespace omnirender

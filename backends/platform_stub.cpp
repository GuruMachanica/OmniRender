// filepath: backends/platform_stub.cpp
// Non-Windows builds have no vendor SDK sources (NGX/XeSS are Windows-only),
// which would leave the OmniRenderBackends static library with zero object
// files. Some archivers (e.g. macOS `ar`) reject empty archives, so this TU
// guarantees the library always has at least one member on macOS/Linux.
namespace omnirender::backends {

int PlatformStubSymbol() noexcept {
    return 0;
}

}  // namespace omnirender::backends

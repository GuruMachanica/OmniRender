# filepath: Dockerfile
# Multi-stage build for OmniRender.
#
# NOTE: Container image publishing is currently disabled for
# v0.7.0-alpha (see .github/workflows/publish-image.yml for the
# full rationale). The Dockerfile is preserved as a forward-looking
# reference for the day GitHub Actions exposes a buildable
# mcr.microsoft.com/windows base image that ships MSVC and a
# matching Windows runtime manifest.
#
# Stage 1: build the daemon and hook DLL using the official
#           mcr.microsoft.com/windows SDK image. The build tools
#           image already has MSVC, CMake, and Ninja installed.
#
# Stage 2: runtime image with only the built artifacts and a
#           minimal entrypoint.
#
# WINDOWS_VERSION_TAG controls the base image manifest tag. The
# mcr.microsoft.com/windows repo exposes "ltsc2022" and "ltsc2019"
# aliases (plus rolling tags) but NOT numeric builds like
# 10.0.20348. Override with --build-arg WINDOWS_VERSION_TAG=ltsc2022
# (the default) or use a rolling tag such as
# WINDOWS_VERSION_TAG=10.0.26100 when running on a matching
# windows-latest runner.

# ---------- Stage 1: build ----------
ARG WINDOWS_VERSION_TAG=ltsc2022
FROM mcr.microsoft.com/windows:${WINDOWS_VERSION_TAG} AS builder

SHELL ["cmd", "/S", "/C"]

WORKDIR C:/src/omnirender

COPY CMakeLists.txt C:/src/omnirender/CMakeLists.txt
COPY modules C:/src/omnirender/modules
COPY tests C:/src/omnirender/tests
COPY profiles C:/src/omnirender/profiles

RUN cmake -B build -G "Ninja" -A x64 -DCMAKE_BUILD_TYPE=Release -DOMNIRENDER_BUILD_HOOK=ON -DOMNIRENDER_BUILD_DAEMON=ON ^
 && cmake --build build --config Release

# ---------- Stage 2: runtime ----------
ARG WINDOWS_VERSION_TAG=ltsc2022
FROM mcr.microsoft.com/windows:${WINDOWS_VERSION_TAG}

SHELL ["cmd", "/S", "/C"]

WORKDIR C:/omnirender

COPY --from=builder C:/src/omnirender/build/modules/daemon/Release/OmniRenderDaemon.exe C:/omnirender/OmniRenderDaemon.exe
COPY --from=builder C:/src/omnirender/build/modules/hook/Release/omnirender-hook.dll C:/omnirender/omnirender-hook.dll
COPY --from=builder C:/src/omnirender/modules/shaders/ C:/omnirender/shaders/

ENTRYPOINT ["C:/omnirender/OmniRenderDaemon.exe"]

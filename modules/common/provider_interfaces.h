// filepath: modules/common/provider_interfaces.h
#pragma once

#include "frame_context.h"

namespace omnirender {

// Interface for acquiring or synthesizing scene linearized depth.
class IDepthProvider {
public:
    virtual ~IDepthProvider() = default;
    virtual DataSource GetSource() const noexcept = 0;
    virtual bool ProvideDepth(FrameContext& ctx) = 0;
};

// Interface for acquiring or synthesizing screen-space motion vectors.
class IMotionProvider {
public:
    virtual ~IMotionProvider() = default;
    virtual DataSource GetSource() const noexcept = 0;
    virtual bool ProvideMotion(FrameContext& ctx) = 0;
};

// Interface for extracting or estimating camera view-projection matrices.
class ICameraProvider {
public:
    virtual ~ICameraProvider() = default;
    virtual DataSource GetSource() const noexcept = 0;
    virtual bool ProvideCamera(FrameContext& ctx) = 0;
};

// Interface for measuring or synthesizing scene exposure and HDR parameters.
class IExposureProvider {
public:
    virtual ~IExposureProvider() = default;
    virtual DataSource GetSource() const noexcept = 0;
    virtual bool ProvideExposure(FrameContext& ctx) = 0;
};

// Interface for generating reactive masks for particles, alpha, and UI overlays.
class IReactiveMaskProvider {
public:
    virtual ~IReactiveMaskProvider() = default;
    virtual DataSource GetSource() const noexcept = 0;
    virtual bool ProvideReactiveMask(FrameContext& ctx) = 0;
};

// Interface for detecting geometric or motion disocclusion boundaries.
class IDisocclusionProvider {
public:
    virtual ~IDisocclusionProvider() = default;
    virtual DataSource GetSource() const noexcept = 0;
    virtual bool ProvideDisocclusion(FrameContext& ctx) = 0;
};

}  // namespace omnirender

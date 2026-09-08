// filepath: tests/gpu/validation/ImageMetrics.h
#pragma once

#include <cstddef>
#include <cstdint>

namespace omnirender::test::gpu {

struct PixelStats {
    uint8_t min_val = 255;
    uint8_t max_val = 0;
    double  mean = 0.0;
    double  variance = 0.0;
    double  std_dev = 0.0;
    double  non_zero_percentage = 0.0;
    size_t  total_samples = 0;
    size_t  non_zero_samples = 0;
};

struct ImageQualityReport {
    PixelStats test_stats;
    PixelStats ref_stats;
    double     mse = 0.0;
    double     psnr = 0.0;
    double     ssim = 0.0;
    bool       is_valid = false;
};

class ImageMetrics {
public:
    static PixelStats ComputePixelStats(const uint8_t* data, size_t byte_count);

    static double ComputeMse(const uint8_t* a, const uint8_t* b, size_t count);

    static double ComputePsnr(double mse, double max_val = 255.0);

    static double ComputeSsim(const uint8_t* a, const uint8_t* b,
                              uint32_t width, uint32_t height,
                              uint32_t channels = 4);

    static ImageQualityReport Evaluate(const uint8_t* test_img,
                                       const uint8_t* ref_img,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t channels = 4);
};

}  // namespace omnirender::test::gpu

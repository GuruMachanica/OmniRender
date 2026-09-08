// filepath: tests/gpu/validation/ImageMetrics.cpp
#include "ImageMetrics.h"
#include <cmath>
#include <algorithm>
#include "DebugLogger.h"

namespace omnirender::test::gpu {

PixelStats ImageMetrics::ComputePixelStats(const uint8_t* data, size_t byte_count) {
    PixelStats stats{};
    if (!data || byte_count == 0) {
        return stats;
    }

    stats.total_samples = byte_count;
    double sum = 0.0;
    double sum_sq = 0.0;

    for (size_t i = 0; i < byte_count; ++i) {
        const uint8_t val = data[i];
        if (val < stats.min_val) stats.min_val = val;
        if (val > stats.max_val) stats.max_val = val;
        if (val > 0) {
            stats.non_zero_samples++;
        }
        sum += val;
        sum_sq += static_cast<double>(val) * static_cast<double>(val);
    }

    const double n = static_cast<double>(byte_count);
    stats.mean = sum / n;
    stats.variance = std::max(0.0, (sum_sq / n) - (stats.mean * stats.mean));
    stats.std_dev = std::sqrt(stats.variance);
    stats.non_zero_percentage = (static_cast<double>(stats.non_zero_samples) / n) * 100.0;

    return stats;
}

double ImageMetrics::ComputeMse(const uint8_t* a, const uint8_t* b, size_t count) {
    if (!a || !b || count == 0) return 0.0;

    double sum_sq_err = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum_sq_err += diff * diff;
    }
    return sum_sq_err / static_cast<double>(count);
}

double ImageMetrics::ComputePsnr(double mse, double max_val) {
    if (mse <= 1e-10) {
        return 99.99; // Essentially identical
    }
    return 20.0 * std::log10(max_val / std::sqrt(mse));
}

double ImageMetrics::ComputeSsim(const uint8_t* a, const uint8_t* b,
                                uint32_t width, uint32_t height,
                                uint32_t channels) {
    if (!a || !b || width == 0 || height == 0 || channels == 0) {
        return 0.0;
    }

    const double k1 = 0.01;
    const double k2 = 0.03;
    const double L  = 255.0;
    const double c1 = (k1 * L) * (k1 * L);
    const double c2 = (k2 * L) * (k2 * L);

    const size_t pixel_count = static_cast<size_t>(width) * height;
    const uint32_t eval_channels = std::min<uint32_t>(channels, 3); // Evaluate R, G, B
    double channel_ssim_sum = 0.0;

    for (uint32_t c = 0; c < eval_channels; ++c) {
        double sum_x = 0.0;
        double sum_y = 0.0;
        double sum_sq_x = 0.0;
        double sum_sq_y = 0.0;
        double sum_xy = 0.0;

        for (size_t i = 0; i < pixel_count; ++i) {
            const double x = a[i * channels + c];
            const double y = b[i * channels + c];
            sum_x += x;
            sum_y += y;
            sum_sq_x += x * x;
            sum_sq_y += y * y;
            sum_xy += x * y;
        }

        const double n = static_cast<double>(pixel_count);
        const double mu_x = sum_x / n;
        const double mu_y = sum_y / n;
        const double var_x = std::max(0.0, (sum_sq_x / n) - (mu_x * mu_x));
        const double var_y = std::max(0.0, (sum_sq_y / n) - (mu_y * mu_y));
        const double cov_xy = (sum_xy / n) - (mu_x * mu_y);

        const double numerator = (2.0 * mu_x * mu_y + c1) * (2.0 * cov_xy + c2);
        const double denominator = (mu_x * mu_x + mu_y * mu_y + c1) * (var_x + var_y + c2);

        if (denominator > 1e-10) {
            channel_ssim_sum += numerator / denominator;
        } else {
            channel_ssim_sum += 1.0;
        }
    }

    return channel_ssim_sum / static_cast<double>(eval_channels);
}

ImageQualityReport ImageMetrics::Evaluate(const uint8_t* test_img,
                                         const uint8_t* ref_img,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t channels) {
    ImageQualityReport report{};
    if (!test_img || width == 0 || height == 0) {
        return report;
    }

    const size_t byte_count = static_cast<size_t>(width) * height * channels;
    report.test_stats = ComputePixelStats(test_img, byte_count);

    if (ref_img) {
        report.ref_stats = ComputePixelStats(ref_img, byte_count);
        report.mse       = ComputeMse(test_img, ref_img, byte_count);
        report.psnr      = ComputePsnr(report.mse);
        report.ssim      = ComputeSsim(test_img, ref_img, width, height, channels);
    }

    report.is_valid = (report.test_stats.non_zero_percentage > 5.0);

    DebugLogger::Instance().Debug("Image metrics evaluated: MSE=%.4f, PSNR=%.2f dB, SSIM=%.4f, non-zero=%.1f%%",
                                  report.mse, report.psnr, report.ssim, report.test_stats.non_zero_percentage);
    return report;
}

}  // namespace omnirender::test::gpu

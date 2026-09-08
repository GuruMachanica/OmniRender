// filepath: tests/gpu/validation/DiagnosticsReporter.cpp
#include "DiagnosticsReporter.h"
#include <cstdio>
#include <iomanip>
#include <iostream>
#include "DebugLogger.h"

namespace omnirender::test::gpu {

static const char* VendorIdToName(uint32_t vendor_id) {
    switch (vendor_id) {
        case 0x10DE: return "NVIDIA Corporation";
        case 0x1002: return "Advanced Micro Devices (AMD)";
        case 0x8086: return "Intel Corporation";
        case 0x1414: return "Microsoft Corporation (Software/WARP)";
        default:     return "Unknown / Emulated Vendor";
    }
}

DiagnosticsReporter::DiagnosticsReporter() = default;

void DiagnosticsReporter::PrintBanner() {
    std::cout << "\n===================================================================\n";
    std::cout << "         OmniRender Standalone GPU Integration Test Suite          \n";
    std::cout << "===================================================================\n" << std::endl;
    DebugLogger::Instance().Info("Started OmniRender Standalone GPU Integration Test Suite");
}

void DiagnosticsReporter::PrintAdapterInfo(const GpuAdapterInfo& info) {
    const double vram_mb = static_cast<double>(info.dedicated_vram_bytes) / (1024.0 * 1024.0);
    const double sys_mb  = static_cast<double>(info.dedicated_sys_bytes) / (1024.0 * 1024.0);
    const double shd_mb  = static_cast<double>(info.shared_sys_bytes) / (1024.0 * 1024.0);

    std::cout << "--- GPU Hardware & Adapter Discovery ---" << std::endl;
    std::cout << "  Adapter:         " << info.description << std::endl;
    std::cout << "  Vendor:          " << VendorIdToName(info.vendor_id) << " (0x" << std::hex << info.vendor_id << std::dec << ")" << std::endl;
    std::cout << "  Device ID:       0x" << std::hex << info.device_id << std::dec << std::endl;
    std::cout << "  Driver Mode:     " << (info.is_warp ? "WARP (Software Rasterizer)" : "Native Hardware Driver") << std::endl;
    std::cout << "  Feature Level:   Direct3D " << info.feature_level_str << std::endl;
    std::cout << "  Dedicated VRAM:  " << std::fixed << std::setprecision(2) << vram_mb << " MB" << std::endl;
    std::cout << "  Dedicated Sys:   " << std::fixed << std::setprecision(2) << sys_mb << " MB" << std::endl;
    std::cout << "  Shared Memory:   " << std::fixed << std::setprecision(2) << shd_mb << " MB" << std::endl;
    std::cout << "----------------------------------------\n" << std::endl;

    DebugLogger::Instance().Info("GPU: %s, Mode: %s, VRAM: %.2f MB, Feature: %s",
                                 info.description.c_str(),
                                 info.is_warp ? "WARP" : "Hardware",
                                 vram_mb, info.feature_level_str.c_str());
}

void DiagnosticsReporter::RecordStageTiming(const std::string& stage_name, double duration_ms) {
    timings_.push_back({ stage_name, duration_ms });
    DebugLogger::Instance().Debug("Stage [%s] completed in %.3f ms", stage_name.c_str(), duration_ms);
}

void DiagnosticsReporter::RecordTestResult(const std::string& name, bool passed, const std::string& message) {
    results_.push_back({ name, passed ? TestStatus::Pass : TestStatus::Fail, message });
    if (passed) {
        std::cout << "  [PASS] " << name;
        if (!message.empty()) std::cout << " (" << message << ")";
        std::cout << std::endl;
        DebugLogger::Instance().Info("[PASS] %s %s", name.c_str(), message.c_str());
    } else {
        std::cout << "  [FAIL] " << name;
        if (!message.empty()) std::cout << " (" << message << ")";
        std::cout << std::endl;
        DebugLogger::Instance().Error("[FAIL] %s %s", name.c_str(), message.c_str());
    }
}

void DiagnosticsReporter::RecordTestSkipped(const std::string& name, const std::string& reason) {
    results_.push_back({ name, TestStatus::Skip, reason });
    std::cout << "  [SKIP] " << name;
    if (!reason.empty()) std::cout << " (" << reason << ")";
    std::cout << std::endl;
    DebugLogger::Instance().Info("[SKIP] %s %s", name.c_str(), reason.c_str());
}

void DiagnosticsReporter::PrintImageQualityReport(const std::string& label, const ImageQualityReport& report) {
    std::cout << "\n--- Image Quality & Statistical Verification [" << label << "] ---" << std::endl;
    std::cout << "  Pixel Range:     [" << static_cast<int>(report.test_stats.min_val)
              << " .. " << static_cast<int>(report.test_stats.max_val) << "]" << std::endl;
    std::cout << "  Mean Luminance:  " << std::fixed << std::setprecision(2) << report.test_stats.mean << std::endl;
    std::cout << "  Variance / Std:  " << std::fixed << std::setprecision(2) << report.test_stats.variance
              << " / " << report.test_stats.std_dev << std::endl;
    std::cout << "  Non-Zero Pixels: " << std::fixed << std::setprecision(2) << report.test_stats.non_zero_percentage << " %" << std::endl;
    if (report.mse > 0.0 || report.psnr > 0.0) {
        std::cout << "  MSE to Ref:      " << std::fixed << std::setprecision(4) << report.mse << std::endl;
        std::cout << "  PSNR:            " << std::fixed << std::setprecision(2) << report.psnr << " dB" << std::endl;
        std::cout << "  SSIM:            " << std::fixed << std::setprecision(4) << report.ssim << std::endl;
    }
    std::cout << "--------------------------------------------------------\n" << std::endl;

    DebugLogger::Instance().Info("Quality [%s]: Mean=%.2f, Var=%.2f, NonZero=%.1f%%, PSNR=%.2f dB, SSIM=%.4f",
                                 label.c_str(), report.test_stats.mean, report.test_stats.variance,
                                 report.test_stats.non_zero_percentage, report.psnr, report.ssim);
}

size_t DiagnosticsReporter::GetPassedTests() const noexcept {
    size_t passed = 0;
    for (const auto& res : results_) {
        if (res.status == TestStatus::Pass) passed++;
    }
    return passed;
}

size_t DiagnosticsReporter::GetSkippedTests() const noexcept {
    size_t skipped = 0;
    for (const auto& res : results_) {
        if (res.status == TestStatus::Skip) skipped++;
    }
    return skipped;
}

bool DiagnosticsReporter::HasFailures() const noexcept {
    for (const auto& res : results_) {
        if (res.status == TestStatus::Fail) return true;
    }
    return false;
}

bool DiagnosticsReporter::PrintFinalSummary() {
    std::cout << "\n===================================================================" << std::endl;
    std::cout << "                        Execution Timings                          " << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;
    for (const auto& t : timings_) {
        std::cout << "  " << std::left << std::setw(35) << t.stage_name
                  << std::right << std::setw(10) << std::fixed << std::setprecision(3)
                  << t.duration_ms << " ms" << std::endl;
    }

    std::cout << "\n===================================================================" << std::endl;
    std::cout << "                       Final Test Summary                          " << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;
    const size_t total = results_.size();
    const size_t passed = GetPassedTests();
    const size_t skipped = GetSkippedTests();
    const size_t failed = total - passed - skipped;

    std::cout << "  Total Tests Executed: " << total << std::endl;
    std::cout << "  Passed:               " << passed << std::endl;
    if (skipped > 0) {
        std::cout << "  Skipped:              " << skipped << std::endl;
    }
    std::cout << "  Failed:               " << failed << std::endl;
    std::cout << "  Overall Result:       " << (failed == 0 ? "SUCCESS [ALL PASSED]" : "FAILURE [TESTS FAILED]") << std::endl;
    std::cout << "===================================================================\n" << std::endl;

    DebugLogger::Instance().Info("Final Summary: Total=%zu, Passed=%zu, Skipped=%zu, Failed=%zu, Result=%s",
                                 total, passed, skipped, failed, failed == 0 ? "SUCCESS" : "FAILURE");
    DebugLogger::Instance().Flush();
    return (failed == 0);
}

}  // namespace omnirender::test::gpu

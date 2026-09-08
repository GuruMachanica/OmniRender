// filepath: tests/gpu/validation/DiagnosticsReporter.h
#pragma once

#include <string>
#include <vector>
#include "../d3d11/D3D11DeviceFixture.h"
#include "ImageMetrics.h"

namespace omnirender::test::gpu {

struct StageTiming {
    std::string stage_name;
    double      duration_ms = 0.0;
};

enum class TestStatus {
    Pass,
    Fail,
    Skip
};

struct TestCaseResult {
    std::string name;
    TestStatus  status = TestStatus::Fail;
    std::string message;
};

class DiagnosticsReporter {
public:
    DiagnosticsReporter();
    ~DiagnosticsReporter() = default;

    void PrintBanner();
    void PrintAdapterInfo(const GpuAdapterInfo& info);
    void RecordStageTiming(const std::string& stage_name, double duration_ms);
    void RecordTestResult(const std::string& name, bool passed, const std::string& message = "");
    void RecordTestSkipped(const std::string& name, const std::string& reason = "");
    void PrintImageQualityReport(const std::string& label, const ImageQualityReport& report);
    bool PrintFinalSummary();

    [[nodiscard]] size_t GetTotalTests() const noexcept { return results_.size(); }
    [[nodiscard]] size_t GetPassedTests() const noexcept;
    [[nodiscard]] size_t GetSkippedTests() const noexcept;
    [[nodiscard]] bool HasFailures() const noexcept;

private:
    std::vector<StageTiming>    timings_;
    std::vector<TestCaseResult> results_;
};

}  // namespace omnirender::test::gpu

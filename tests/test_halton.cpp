// filepath: tests/test_halton.cpp
// Verifies the Halton 2,3 low-discrepancy sequence.
//
// Properties checked:
//   1. Both x and y are always in [-0.5, +0.5].
//   2. The sequence is deterministic and period-16 (frame_index % 16).
//   3. Adjacent samples are not identical.

#include <cassert>
#include <cmath>
#include <cstdio>

#include "../modules/common/halton.h"

int main() {
    using namespace omnirender;

    for (uint64_t i = 0; i < 64; ++i) {
        Halton23 h = Halton23At(i);
        if (h.x < -0.5f || h.x > 0.5f) {
            std::printf("test_halton: x out of range at frame %llu: %f\n",
                        static_cast<unsigned long long>(i), h.x);
            return 1;
        }
        if (h.y < -0.5f || h.y > 0.5f) {
            std::printf("test_halton: y out of range at frame %llu: %f\n",
                        static_cast<unsigned long long>(i), h.y);
            return 1;
        }
    }

    // Periodicity: frame 0 and frame 16 produce the same result.
    Halton23 a = Halton23At(0);
    Halton23 b = Halton23At(16);
    if (std::fabs(a.x - b.x) > 1e-6f || std::fabs(a.y - b.y) > 1e-6f) {
        std::printf("test_halton: period not 16 (frame 0 vs 16 differ: %f,%f vs %f,%f)\n",
                    a.x, a.y, b.x, b.y);
        return 1;
    }

    // Adjacent samples differ.
    for (uint64_t i = 0; i < 15; ++i) {
        Halton23 a = Halton23At(i);
        Halton23 b = Halton23At(i + 1);
        if (std::fabs(a.x - b.x) < 1e-6f && std::fabs(a.y - b.y) < 1e-6f) {
            std::printf("test_halton: frame %llu and %llu produced identical samples\n",
                        static_cast<unsigned long long>(i),
                        static_cast<unsigned long long>(i + 1));
            return 1;
        }
    }

    std::printf("test_halton: OK (64 frames verified)\n");
    return 0;
}

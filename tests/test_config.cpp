// filepath: tests/test_config.cpp
// Verifies the TOML-style config loader.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "../modules/common/config.h"

namespace fs = std::filesystem;

static void WriteFile(const fs::path& p, const std::string& body) {
    std::ofstream out(p, std::ios::trunc);
    out << body;
}

int main() {
    using namespace omnirender::config;

    // ---- 1. LoadFromFile parses sections and types ----
    auto tmp = fs::temp_directory_path() / "omnirender_test_config";
    fs::create_directories(tmp);
    auto path = tmp / "sample.toml";
    WriteFile(path, R"(
# OmniRender test profile
[renderer]
upscaler = "auto"
quality  = "quality"
sharpen  = 0.25

[temporal]
enabled  = true
history  = 8
motion   = "auto"

[hud]
exclude = true
)");

    ConfigStore c;
    bool loaded = c.LoadFromFile(path);
    (void)loaded;
    assert(loaded);
    assert(c.GetString("renderer.upscaler", "") == "auto");
    assert(c.GetString("renderer.quality",  "") == "quality");
    assert(c.GetFloat ("renderer.sharpen",  0.0) == 0.25);
    assert(c.GetBool  ("temporal.enabled",  false) == true);
    assert(c.GetInt   ("temporal.history",  0) == 8);

    // ---- 2. MergeFrom: per-game overrides global ----
    auto profile = tmp / "profile.toml";
    WriteFile(profile, R"(
[renderer]
upscaler = "fsr"
sharpen  = 0.5
)");
    ConfigStore game;
    game.LoadFromFile(profile);
    c.MergeFrom(game);
    assert(c.GetString("renderer.upscaler", "") == "fsr");
    assert(c.GetFloat ("renderer.sharpen",  0.0) == 0.5);
    // [temporal] should still be intact because profile didn't define it
    assert(c.GetInt("temporal.history", 0) == 8);

    // ---- 3. Env overrides: set a var, call ApplyEnvOverrides, check it ----
    _putenv_s("OMNIRENDER_RENDERER_UPSCALER", "dlss");
    c.ApplyEnvOverrides("OMNIRENDER_");
    assert(c.GetString("renderer.upscaler", "") == "dlss");
    _putenv_s("OMNIRENDER_RENDERER_UPSCALER", "");

    // ---- 4. HashFile is stable ----
    auto h1 = HashFile(path);
    auto h2 = HashFile(path);
    assert(!h1.empty());
    assert(h1 == h2);
    assert(h1.size() == 16);  // 16 hex chars = 64-bit FNV-1a

    // ---- 5. SaveToFile round-trip ----
    auto roundtrip = tmp / "roundtrip.toml";
    c.SaveToFile(roundtrip);
    ConfigStore d;
    d.LoadFromFile(roundtrip);
    assert(d.GetString("renderer.upscaler", "") == "dlss");
    assert(d.GetFloat ("renderer.sharpen",   0.0) == 0.5);

    // ---- 6. Default profile is non-empty ----
    ConfigStore def = DefaultProfile();
    assert(def.GetString("renderer.upscaler", "") == "auto");
    assert(def.GetBool  ("temporal.enabled",  false) == true);
    assert(def.GetBool  ("hud.exclude",       false) == true);

    // ---- 7. LoadFullConfig works without an exe_path ----
    auto cfg = LoadFullConfig({});
    assert(cfg.GetString("renderer.upscaler", "") == "auto");
    assert(cfg.GetFloat ("renderer.sharpen",  0.0) == 0.25);

    // ---- cleanup ----
    std::error_code ec;
    fs::remove_all(tmp, ec);

    std::printf("test_config: OK (7 cases verified)\n");
    return 0;
}

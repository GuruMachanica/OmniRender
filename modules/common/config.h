// filepath: modules/common/config.h
// TOML-style configuration loader.
//
// Pure C++17, no third-party dependencies. Supports a subset of
// TOML sufficient for OmniRender's per-game profiles:
//
//   # comments
//   [section]
//   key = "string"
//   key = 1          # integer
//   key = 1.0        # float
//   key = true       # boolean
//   key = "a", "b"   # string array (limited)
//
// Files are searched at:
//   %USERPROFILE%/.omnirender/config.toml      (global)
//   %USERPROFILE%/.omnirender/profiles/<hash>.toml  (per-game)
//
// Environment variables override file values. See ConfigStore docs.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace omnirender::config {

// Loaded configuration value. Holds the original string form plus
// parsed typed accessors.
struct Value {
    std::string raw;

    Value() = default;
    explicit Value(std::string s) : raw(std::move(s)) {}

    bool        as_bool(bool def = false) const noexcept;
    int64_t     as_int(int64_t def = 0) const noexcept;
    double      as_float(double def = 0.0) const noexcept;
    std::string as_string(const std::string& def = "") const noexcept;

    bool empty() const noexcept { return raw.empty(); }
};

// Flat key/value store keyed by "section.key" (e.g. "renderer.upscaler").
class ConfigStore {
public:
    // Load from a single TOML file. Returns true on success (including
    // "file not found" — that's not an error, just an empty config).
    bool LoadFromFile(const std::filesystem::path& path);

    // Save to a single TOML file. Returns true on success.
    bool SaveToFile(const std::filesystem::path& path) const;

    // Set / get a value.
    void Set(const std::string& key, const std::string& value);
    std::optional<Value> Get(const std::string& key) const;
    Value GetOr(const std::string& key, const std::string& def) const;

    // Convenience typed accessors.
    bool        GetBool  (const std::string& key, bool        def) const;
    int64_t     GetInt   (const std::string& key, int64_t     def) const;
    double      GetFloat (const std::string& key, double      def) const;
    std::string GetString(const std::string& key, const std::string& def) const;

    // All keys in [section].
    std::vector<std::string> KeysInSection(const std::string& section) const;

    // Merge another store on top of this one. Overwrites for keys that
    // are present in `other`. Used to layer per-game profiles over the
    // global config.
    void MergeFrom(const ConfigStore& other);

    // Override individual keys from environment variables. The env-var
    // name is the key with dots replaced by underscores and the result
    // upper-cased, prefixed with "OMNIRENDER_". So "renderer.upscaler"
    // -> "OMNIRENDER_RENDERER_UPSCALER".
    void ApplyEnvOverrides(const char* env_prefix = "OMNIRENDER_");

    // Iterate (for SaveToFile).
    const std::map<std::string, std::string>& All() const noexcept { return kv_; }

private:
    std::map<std::string, std::string> kv_;
};

// Compute the SHA-256 of a file as a hex string. Used to identify the
// game's executable in the profiles directory.
std::string HashFile(const std::filesystem::path& path);

// Default profile schema (audit #23). Used when the user has not yet
// created a profile for the running game.
ConfigStore DefaultProfile();

// File-system helpers.
std::filesystem::path GlobalConfigPath();
std::filesystem::path ProfilePathFor(const std::filesystem::path& exe);

// Load the full configuration stack: env vars > per-game profile >
// global config > defaults. The `exe_path` may be empty (e.g. when
// running the test host).
ConfigStore LoadFullConfig(const std::filesystem::path& exe_path = {});

// ---------------------------------------------------------------------------
// Runtime knobs. These are populated from the ConfigStore in
// modules/daemon/main.cpp at startup and consumed by the pipeline +
// processing modules. They live in this header so the runtime paths
// can read them without dragging the full ConfigStore into every TU.
// ---------------------------------------------------------------------------

// Pipeline feature toggles. Defaults match the bounded, VRAM-friendly
// pipeline described in the audit; individual per-game profiles may
// disable any of them.
inline bool g_enable_reconstruction = true;
inline bool g_enable_upscale        = true;
inline bool g_enable_tonemap        = true;
inline bool g_enable_optical_flow   = false;
inline bool g_enable_dlss           = false;
inline bool g_enable_trt_tonemap    = false;
inline bool g_enable_xess           = false;
inline bool g_enable_fsr            = false;
inline bool g_enable_rt_effects     = false;

// Working-resolution ceiling (pixels). The pipeline clamps the input
// frame down to this size before any history-keeping pass so the
// total VRAM footprint stays bounded. Profiles may override per
// game; defaults are picked for a mid-range 4 GB GPU.
inline UINT g_max_work_width  = 1920;
inline UINT g_max_work_height = 1080;

// Number of frames kept in the bounded history ping-pong. More
// history = better reconstruction quality but linearly more VRAM.
inline UINT g_max_history = 4;

}  // namespace omnirender::config

// Top-level default constants used by the daemon when the profile
// omits a key. These live in the omnirender namespace (not config)
// because they are the *contract* the pipeline expects; runtime
// overrides live in omnirender::config::* above.
namespace omnirender {
inline constexpr UINT kDefaultMaxProcessingWidth  = 1920;
inline constexpr UINT kDefaultMaxProcessingHeight = 1080;
inline constexpr UINT kDefaultMaxHistoryFrames     = 4;
}  // namespace omnirender

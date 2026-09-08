// filepath: modules/common/config.cpp
// Implementation of the TOML-style configuration loader.

#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "logging.h"

namespace omnirender::config {

// ---- Value -----------------------------------------------------------

bool Value::as_bool(bool def) const noexcept {
    if (raw.empty()) return def;
    if (raw == "1" || raw == "true"  || raw == "TRUE"  || raw == "yes" || raw == "YES" || raw == "y" || raw == "Y") return true;
    if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "no"  || raw == "NO"  || raw == "n" || raw == "N") return false;
    return def;
}

int64_t Value::as_int(int64_t def) const noexcept {
    if (raw.empty()) return def;
    try { return std::stoll(raw); } catch (...) { return def; }
}

double Value::as_float(double def) const noexcept {
    if (raw.empty()) return def;
    try { return std::stod(raw); } catch (...) { return def; }
}

std::string Value::as_string(const std::string& def) const noexcept {
    if (raw.empty()) return def;
    // Strip surrounding quotes if present.
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        return raw.substr(1, raw.size() - 2);
    }
    return raw;
}

// ---- Helpers ---------------------------------------------------------

static std::string Trim(std::string_view s) {
    auto begin = s.begin();
    auto end   = s.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    return std::string(begin, end);
}

static std::string StripInlineComment(std::string_view s) {
    // Inside a quoted value, '#' is literal. Outside, '#' starts a comment.
    bool in_quote = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') { in_quote = !in_quote; continue; }
        if (!in_quote && c == '#') { return std::string(s.substr(0, i)); }
    }
    return std::string(s);
}

static std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

static std::string EnvKey(const std::string& dotted_key, const char* prefix) {
    std::string out = prefix ? prefix : "";
    for (char c : dotted_key) {
        if (c == '.' || c == '-') out += '_';
        else out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// ---- File I/O --------------------------------------------------------

bool ConfigStore::LoadFromFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty()) continue;
        if (trimmed.front() == '#') continue;

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = Trim(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }

        // key = value (with possible inline comment after a #)
        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key   = Trim(trimmed.substr(0, eq));
        std::string value = Trim(StripInlineComment(trimmed.substr(eq + 1)));
        if (!section.empty()) key = section + "." + key;
        Set(key, value);
    }
    OMNI_LOG_INFO("config: loaded %s", path.string().c_str());
    return true;
}

bool ConfigStore::SaveToFile(const std::filesystem::path& path) const {
    // Ensure parent directories exist before attempting to write.
    // On first run .omnirender/ doesn't exist, and ofstream open silently fails.
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            OMNI_LOG_WARN("config: could not create directory %s: %s",
                          path.parent_path().string().c_str(), ec.message().c_str());
        }
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    std::string current_section;
    for (const auto& [key, value] : kv_) {
        auto dot = key.find('.');
        std::string section = (dot == std::string::npos) ? "" : key.substr(0, dot);
        std::string bare   = (dot == std::string::npos) ? key : key.substr(dot + 1);

        if (section != current_section) {
            if (!current_section.empty()) out << "\n";
            out << "[" << section << "]\n";
            current_section = section;
        }

        // Quote string-looking values; emit numbers and bools raw.
        bool is_number = !value.empty() && (std::isdigit(static_cast<unsigned char>(value.front())) ||
                                            value.front() == '-' || value.front() == '+');
        bool is_bool = (Lower(value) == "true" || Lower(value) == "false");
        if (is_number || is_bool) {
            out << "  " << bare << " = " << value << "\n";
        } else {
            out << "  " << bare << " = \"" << value << "\"\n";
        }
    }
    return true;
}

// ---- Set / Get -------------------------------------------------------

void ConfigStore::Set(const std::string& key, const std::string& value) {
    kv_[key] = value;
}

std::optional<Value> ConfigStore::Get(const std::string& key) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) return std::nullopt;
    return Value(it->second);
}

Value ConfigStore::GetOr(const std::string& key, const std::string& def) const {
    auto it = kv_.find(key);
    if (it == kv_.end() || it->second.empty()) return Value(def);
    return Value(it->second);
}

bool        ConfigStore::GetBool  (const std::string& key, bool        def) const { return GetOr(key, "").as_bool(def); }
int64_t     ConfigStore::GetInt   (const std::string& key, int64_t     def) const { return GetOr(key, "").as_int(def); }
double      ConfigStore::GetFloat (const std::string& key, double      def) const { return GetOr(key, "").as_float(def); }
std::string ConfigStore::GetString(const std::string& key, const std::string& def) const { return GetOr(key, def).as_string(def); }

std::vector<std::string> ConfigStore::KeysInSection(const std::string& section) const {
    std::vector<std::string> out;
    std::string prefix = section + ".";
    for (const auto& [key, value] : kv_) {
        if (key.size() > prefix.size() &&
            key.compare(0, prefix.size(), prefix) == 0) {
            out.push_back(key.substr(prefix.size()));
        }
    }
    return out;
}

void ConfigStore::MergeFrom(const ConfigStore& other) {
    for (const auto& [key, value] : other.kv_) {
        kv_[key] = value;  // overwrite
    }
}

void ConfigStore::ApplyEnvOverrides(const char* env_prefix) {
    // Walk every key in our store and see if the corresponding env-var
    // is set. We only override values for keys we know about; otherwise
    // the env-var would inject a key that's never read.
    for (auto& [key, value] : kv_) {
        std::string env_name = EnvKey(key, env_prefix);
        if (const char* v = std::getenv(env_name.c_str())) {
            value = v;
        }
    }
}

// ---- Profile helpers ------------------------------------------------

std::filesystem::path GlobalConfigPath() {
    // %USERPROFILE%\.omnirender\config.toml
#ifdef _WIN32
    if (const char* home = std::getenv("USERPROFILE")) {
        return std::filesystem::path(home) / ".omnirender" / "config.toml";
    }
#endif
    return std::filesystem::path();
}

std::filesystem::path ProfilePathFor(const std::filesystem::path& exe) {
    if (exe.empty()) return {};
    std::string hash = HashFile(exe);
    if (hash.empty()) return {};
#ifdef _WIN32
    if (const char* home = std::getenv("USERPROFILE")) {
        return std::filesystem::path(home) / ".omnirender" / "profiles" / (hash + ".toml");
    }
#endif
    return {};
}

std::string HashFile(const std::filesystem::path& path) {
    // Simple, stable, dependency-free FNV-1a 64-bit hash. We don't need
    // cryptographic strength here — just a stable identifier.
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    constexpr uint64_t kOffset = 14695981039346656037ULL;
    constexpr uint64_t kPrime  = 1099511628211ULL;
    uint64_t h = kOffset;
    char buf[4096];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<uint8_t>(buf[i]);
            h *= kPrime;
        }
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    return std::string(hex);
}

ConfigStore DefaultProfile() {
    ConfigStore c;
    // [renderer]
    c.Set("renderer.upscaler", "auto");
    c.Set("renderer.quality",  "quality");
    c.Set("renderer.sharpen",  "0.25");
    // [temporal]
    c.Set("temporal.enabled",  "true");
    c.Set("temporal.history",  "8");
    c.Set("temporal.motion",   "auto");
    // [hud]
    c.Set("hud.exclude",       "true");
    // [pipeline]
    c.Set("pipeline.enable_reconstruction", "true");
    c.Set("pipeline.enable_upscale",        "true");
    c.Set("pipeline.enable_tonemap",        "true");
    return c;
}

ConfigStore LoadFullConfig(const std::filesystem::path& exe_path) {
    ConfigStore cfg = DefaultProfile();

    // Layer 1: global config overrides defaults.
    ConfigStore global;
    global.LoadFromFile(GlobalConfigPath());
    cfg.MergeFrom(global);

    // Layer 2: per-game profile overrides global.
    if (!exe_path.empty()) {
        ConfigStore profile;
        profile.LoadFromFile(ProfilePathFor(exe_path));
        cfg.MergeFrom(profile);
    }

    // Layer 3: environment overrides everything.
    cfg.ApplyEnvOverrides("OMNIRENDER_");

    return cfg;
}

}  // namespace omnirender::config

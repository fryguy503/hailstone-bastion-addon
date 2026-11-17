#pragma once
#include "hailstone_plugin.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace bastion {
// Resolve the host's writable path only when its live character/server scope changes.
// The ordinary readers are in-memory; character_config_dir can create directories.
class CharacterDirectory {
public:
    std::filesystem::path get(const PluginHostApi* host);
    void reset();
private:
    struct Scope {
        std::string character, server, host, world, world_host;
        std::uint32_t port = 0, player = 0, zone = 0, instance = 0;
        std::uint64_t context = 0;
        bool operator==(const Scope&) const = default;
    };
    static std::optional<Scope> scope(const PluginHostApi* host);
    const PluginHostApi* host_ = nullptr;
    std::optional<Scope> scope_;
    std::filesystem::path directory_;
    std::uint64_t generation_ = 0;
    bool resolving_ = false;
};
struct Preference { const char* key; std::int64_t low, high, initial; };
// Numeric data in the host-provided character directory; no executable text.
class Preferences {
public:
    Preferences(std::string filename, std::span<const Preference> definitions);
    void refresh(const PluginHostApi* host);
    void invalidate_context() { directory_cache_.reset(); }
    bool set(std::string_view key, std::int64_t value);
    std::int64_t get(std::string_view key) const;
    bool reload();
    bool reset();
    const std::string& error() const { return error_; }
private:
    bool save(const std::map<std::string, std::int64_t>& values);
    std::map<std::string, std::int64_t> defaults() const;
    std::string filename_, error_;
    std::span<const Preference> definitions_;
    std::filesystem::path directory_;
    CharacterDirectory directory_cache_;
    std::map<std::string, std::int64_t> values_;
};
}

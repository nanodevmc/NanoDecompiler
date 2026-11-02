#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nd {

struct CacheStats {
    size_t hit_count = 0;
    size_t miss_count = 0;
    size_t total_cached_files = 0;
    uint64_t total_cached_bytes = 0;
};

class DecompCache {
public:
    static DecompCache& instance();

    void set_enabled(bool enabled) { m_enabled = enabled; }
    bool is_enabled() const { return m_enabled; }

    std::string compute_hash(const std::vector<uint8_t>& class_bytes);

    std::optional<std::string> get(const std::string& hash);
    void put(const std::string& hash, const std::string& java_source);

    void clear();
    CacheStats get_stats() const;

    static uint32_t get_optimal_worker_count();

private:
    DecompCache();
    std::string get_cache_root() const;
    std::string get_cache_file_path(const std::string& hash) const;

    bool m_enabled = true;
    mutable CacheStats m_stats;
};

}  // namespace nd

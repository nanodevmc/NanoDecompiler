#include "decomp_cache.hpp"
#include "sha256.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace nd {

DecompCache& DecompCache::instance() {
    static DecompCache s_instance;
    return s_instance;
}

DecompCache::DecompCache() {
    std::string root = get_cache_root();
    std::error_code ec;
    fs::create_directories(fs::u8path(root), ec);
}

std::string DecompCache::compute_hash(const std::vector<uint8_t>& class_bytes) {
    if (class_bytes.empty()) return "";
    auto raw_sha = sha256(class_bytes);
    std::ostringstream oss;
    for (uint8_t b : raw_sha) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

std::string DecompCache::get_cache_root() const {
#if defined(_WIN32)
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && *local_app_data) {
        return (fs::u8path(local_app_data) / "NanoDecompiler" / "cache" / "classes").string();
    }
    const char* user_prof = std::getenv("USERPROFILE");
    if (user_prof && *user_prof) {
        return (fs::u8path(user_prof) / ".nd_cache" / "classes").string();
    }
#else
    const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache && *xdg_cache) {
        return (fs::u8path(xdg_cache) / "nanodecompiler" / "classes").string();
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return (fs::u8path(home) / ".cache" / "nanodecompiler" / "classes").string();
    }
#endif
    return "./.nd_cache/classes";
}

std::string DecompCache::get_cache_file_path(const std::string& hash) const {
    if (hash.size() < 4) return "";
    std::string prefix1 = hash.substr(0, 2);
    std::string prefix2 = hash.substr(2, 2);
    fs::path p = fs::u8path(get_cache_root()) / prefix1 / prefix2 / (hash + ".java");
    return p.string();
}

std::optional<std::string> DecompCache::get(const std::string& hash) {
    if (!m_enabled || hash.empty()) return std::nullopt;

    std::string file_path = get_cache_file_path(hash);
    if (file_path.empty()) return std::nullopt;

    std::error_code ec;
    if (!fs::exists(fs::u8path(file_path), ec)) {
        m_stats.miss_count++;
        return std::nullopt;
    }

    std::ifstream ifs(fs::u8path(file_path), std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        m_stats.miss_count++;
        return std::nullopt;
    }

    std::ostringstream sstr;
    sstr << ifs.rdbuf();
    m_stats.hit_count++;
    return sstr.str();
}

void DecompCache::put(const std::string& hash, const std::string& java_source) {
    if (!m_enabled || hash.empty() || java_source.empty()) return;

    std::string file_path = get_cache_file_path(hash);
    if (file_path.empty()) return;

    fs::path target_path = fs::u8path(file_path);
    std::error_code ec;
    fs::create_directories(target_path.parent_path(), ec);

    std::ofstream ofs(target_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (ofs.is_open()) {
        ofs.write(java_source.data(), java_source.size());
    }
}

void DecompCache::clear() {
    std::string root = get_cache_root();
    std::error_code ec;
    fs::remove_all(fs::u8path(root), ec);
    fs::create_directories(fs::u8path(root), ec);
    m_stats = CacheStats{};
}

CacheStats DecompCache::get_stats() const {
    std::string root = get_cache_root();
    std::error_code ec;
    size_t count = 0;
    uint64_t bytes = 0;

    if (fs::exists(fs::u8path(root), ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(fs::u8path(root), ec)) {
            if (entry.is_regular_file(ec)) {
                count++;
                bytes += entry.file_size(ec);
            }
        }
    }

    m_stats.total_cached_files = count;
    m_stats.total_cached_bytes = bytes;
    return m_stats;
}

uint32_t DecompCache::get_optimal_worker_count() {
    unsigned int hc = std::thread::hardware_concurrency();
    if (hc == 0) return 1;
    if (hc <= 2) return hc; // On dual core, use up to 2
    return hc - 1;          // On multi-core, keep 1 core for OS/UI responsiveness
}

}  // namespace nd

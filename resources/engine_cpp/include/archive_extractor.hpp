#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "platform_detect.hpp"

namespace nd {

struct ArchivePluginEntry {
    std::string filename;
    std::string full_path;
    std::string rel_path;
    uint64_t size_bytes = 0;
    PlatformInfo platform;
    bool is_plugin = false;
    bool is_server_core = false;
    std::string core_reason;
};

struct ArchiveExtractionSummary {
    bool ok = false;
    std::string error;
    std::string temp_dir;
    std::vector<ArchivePluginEntry> plugins;
    std::vector<ArchivePluginEntry> server_cores;
    size_t skipped_libraries = 0;
    size_t total_jars = 0;
};

/** Проверяет, поддерживается ли архив движком (.zip, .tar.gz, .tgz, .tar, .7z, .rar) */
bool is_supported_archive_file(const std::string& path);

/** Распаковывает архив в temp-папку и находит Bukkit/Spigot/Paper/Bungee/Velocity плагины.
 *  Если распаковка не удалась, созданная движком временная папка удаляется сразу. */
ArchiveExtractionSummary extract_and_scan_archive(const std::string& archive_path, const std::string& custom_temp_dir = "");

/** Удаляет временную папку распаковки. Удаляются ТОЛЬКО каталоги строго внутри
 *  <tmp>/NanoDecompiler/archives (и не символические ссылки) - произвольный путь
 *  этой функцией удалить нельзя. Вызывается после обработки найденных плагинов. */
void remove_archive_temp_dir(const std::string& dir);

/** Безопасная распаковка ZIP в out_dir (без path traversal, с лимитами размера/числа файлов,
 *  без записи через символические ссылки). single_root - если все записи лежат в ОДНОМ
 *  корневом каталоге, его имя. Используется установщиком JDK/Maven. */
bool extract_zip_safe(const std::string& zip_path, const std::string& out_dir, std::string& err, std::string* single_root = nullptr);

/** То же для tar.gz/tgz/tar. allow_inner_symlinks: создавать символические ссылки, цель которых
 *  относительная и остаётся внутри out_dir (нужно JDK); иначе ссылки пропускаются. */
bool extract_tar_gz_safe(const std::string& tar_path, const std::string& out_dir, std::string& err, bool allow_inner_symlinks = false,
                         std::string* single_root = nullptr);

}  // namespace nd

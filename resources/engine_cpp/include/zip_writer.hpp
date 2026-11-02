#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace nd {

struct ZipWriteResult {
    bool success = false;
    std::string message;
    size_t replaced_count = 0;
};

class ZipWriter {
public:
    // Создает новый zip/jar архив out_zip_path на основе in_zip_path,
    // заменяя содержимое файлов из map replacements (имя -> новые байты).
    // Все остальные файлы копируются потоково БЕЗ пересжатия (Zero-Copy).
    static ZipWriteResult patch_zip(const std::string& in_zip_path,
                                    const std::string& out_zip_path,
                                    const std::map<std::string, std::vector<uint8_t>>& replacements);
};

}  // namespace nd

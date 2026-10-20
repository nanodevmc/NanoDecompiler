// aegis_deobf.hpp - Движок 100% автоматической деобфускации Aegis Forge (.aeg файлов)
// Расшифровка защищённых ресурсов (defaults/*.yml.aeg, locale/**/*.yml.aeg, opmap.aeg, blobs/*.aeg),
// сборка мастер-ключа по алгоритму бабочки (butterfly permutation) и реконструкция чистого кода.
#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>

#include "zip_reader.hpp"

namespace nd {

struct AegisDeobfResult {
    bool is_aegis = false;
    bool success = false;
    int decrypted_files = 0;
    std::string master_key_hex;
    std::string error;
    std::vector<std::string> unpacked_files;
};

// Проверяет, защищен ли архив механизмом Aegis Forge (.aeg файлы)
bool is_aegis_archive(const std::vector<std::string>& all_names);

// Выполняет полную 100% деобфускацию и извлечение ресурсов .aeg прямо в директорию вывода (res_dir)
AegisDeobfResult deobfuscate_aegis_files(ZipReader& zr, const std::string& out_dir, const std::string& res_dir);

// Проверка структуры байткода: расчёт ожидаемого количества классов, методов и строк
// с отсевом мусорных классов ловушек (AegisArmorTrap, AegisGrinder*, Hikari, Guava, Shaded libs)
struct BytecodeStructureEstimate {
    int expected_classes = 0;
    int expected_methods = 0;
    int expected_lines = 0;
    int trash_trap_classes = 0;
    int library_classes = 0;
    bool has_anomalies = false;
    std::string anomaly_description;
};

BytecodeStructureEstimate estimate_clean_structure(const std::vector<std::string>& class_names,
                                                  ZipReader& zr);

// Сохранение и загрузка состояния контрольной точки .nano_inf
struct NanoInfState {
    std::string jar_sha256;
    int total_classes = 0;
    int decompiled_classes = 0;
    std::string last_processed_class;
    int expected_clean_classes = 0;
    int expected_clean_methods = 0;
    int expected_clean_lines = 0;
    bool is_completed = false;
};

bool save_nano_inf(const std::string& out_dir, const NanoInfState& state);
std::optional<NanoInfState> load_nano_inf(const std::string& out_dir, const std::string& current_jar_sha256);

} // namespace nd

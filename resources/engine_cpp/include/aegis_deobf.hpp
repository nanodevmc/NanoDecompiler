// aegis_deobf.hpp - Движок 100% автоматической деобфускации Aegis Forge (.aeg файлов)
// Расшифровка защищённых ресурсов (defaults/*.yml.aeg, locale/**/*.yml.aeg, opmap.aeg, blobs/*.aeg),
// сборка мастер-ключа по алгоритму бабочки (butterfly permutation) и реконструкция чистого кода.
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <cstdint>

#include "zip_reader.hpp"
#include "ast_nodes.hpp"

namespace nd {

struct MethodCtx;

struct AegisDecryptedClass {
    std::string internal_name;
    std::vector<uint8_t> class_data;
};

struct AegisIRConstant {
    int tag = 0; // 1=str, 2=int, 3=long, 4=double, 5=memberRef
    std::string str_val;
    int32_t int_val = 0;
    int64_t long_val = 0;
    double double_val = 0.0;
    std::string ref_owner, ref_name, ref_desc;
};

struct AegisExceptionEntry {
    uint32_t start_pc = 0;
    uint32_t end_pc = 0;
    uint32_t handler_pc = 0;
    uint16_t catch_type_idx = 0;
};

struct AegisIRMethod {
    std::string token_hex;
    uint16_t version = 0;
    uint16_t max_stack = 0;
    uint16_t max_locals = 0;
    std::vector<AegisIRConstant> constant_pool;
    std::vector<AegisExceptionEntry> exceptions;
    std::vector<uint8_t> bytecode;
};

struct AegisDeobfResult {
    bool is_aegis = false;
    bool success = false;
    int decrypted_files = 0;
    int recovered_classes_count = 0;
    int recovered_vm_methods_count = 0;
    std::string master_key_hex;
    std::string error;
    std::vector<std::string> unpacked_files;
    std::vector<AegisDecryptedClass> recovered_classes;
    std::map<uint64_t, std::shared_ptr<AegisIRMethod>> vm_methods;
};

// Проверяет, защищен ли архив механизмом Aegis Forge (.aeg файлы)
bool is_aegis_archive(const std::vector<std::string>& all_names);

// Проверяет, является ли класс мусорной ловушкой Aegis (AegisArmorTrap, AegisGrinder*, AegisSecurityGuard*, dev/aegisforge)
bool is_aegis_trap_class(const std::string& name);

// Выполняет полную 100% деобфускацию и извлечение ресурсов .aeg прямо в директорию вывода (res_dir)
AegisDeobfResult deobfuscate_aegis_files(ZipReader& zr, const std::string& out_dir, const std::string& res_dir);

// Управление активными VM-методами для сессии декомпиляции
void set_active_aegis_vm_methods(const std::map<uint64_t, std::shared_ptr<AegisIRMethod>>& vm_methods);
std::shared_ptr<AegisIRMethod> get_aegis_vm_method(uint64_t token);
void clear_active_aegis_vm();

// Оффлайн трансляция опкодов Aegis VM в AST выражения без запуска JVM
ExprPtr translate_aegis_ir_method(const AegisIRMethod& ir,
                                  const std::vector<ExprPtr>& call_args,
                                  MethodCtx& ctx,
                                  std::vector<StmtPtr>* out_stmts = nullptr);

// Расшифровка строк AegisVM.decryptString("...", key) в чистый текст
std::optional<std::string> aegis_decrypt_string_fast(const std::string& encrypted_utf8, int32_t key);

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

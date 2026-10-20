// jni_deobf.hpp - Движок глубокой нативной деобфускации JNI (.dll / .so)
// Восстановление Java-байткода и AST из JNI RegisterNatives, J2C-трансляторов и VMProtect-обёрток.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nd {

struct NativeJniBinding {
    std::string class_name;       // e.g. "com/example/NativeBridge"
    std::string method_name;      // e.g. "calculateKey"
    std::string signature;        // e.g. "(Ljava/lang/String;I)Ljava/lang/String;"
    std::string native_symbol;    // e.g. "Java_com_example_NativeBridge_calculateKey"
    uint64_t native_offset = 0;   // Смещение в секции .text
    double deobf_confidence = 0.0;// Точность деобфускации (0.0 .. 1.0, цель >= 0.89)
    std::vector<std::string> reconstructed_statements; // Восстановленный Java-код
};

struct NativeDeobfResult {
    bool success = false;
    std::string binary_format;    // "PE/COFF (DLL)" или "ELF (.so)"
    size_t total_native_methods = 0;
    size_t deobfuscated_methods = 0;
    double accuracy_pct = 0.0;    // Точность восстановления в процентах
    std::vector<NativeJniBinding> bindings;
    std::vector<std::string> warnings;
};

class JniDeobfuscator {
public:
    JniDeobfuscator() = default;

    // Анализ бинарника (.dll или .so) и извлечение таблицы экспортов / RegisterNatives
    NativeDeobfResult analyze_binary(const std::vector<uint8_t>& binary_data, const std::string& filename);

    // Восстановление Java-метода из JNI-опкодов и JNIEnv вызовов
    static std::vector<std::string> reconstruct_method_body(const NativeJniBinding& binding,
                                                            const std::vector<uint8_t>& code_bytes);

    // Генерация замещающего .java класса с восстановленными методами
    static std::string generate_decompiled_java(const std::string& class_name,
                                                const std::vector<NativeJniBinding>& methods);
};

} // namespace nd

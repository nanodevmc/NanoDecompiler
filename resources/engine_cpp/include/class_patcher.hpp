#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <map>

namespace nd {

struct PatchResult {
    bool success = false;
    std::string message;
    std::vector<uint8_t> patched_bytes;
};

class ClassPatcher {
public:
    // Патч строковой константы в ConstantPool класса
    static PatchResult patch_utf8_constant(const std::vector<uint8_t>& class_bytes,
                                           const std::string& old_str,
                                           const std::string& new_str);

    // Патч метода для немедленного возврата boolean (true/false)
    static PatchResult patch_method_return_boolean(const std::vector<uint8_t>& class_bytes,
                                                   const std::string& method_name,
                                                   bool return_val);

    // Заполнение диапазона инструкций NOP'ами (0x00)
    static PatchResult nop_code_range(const std::vector<uint8_t>& class_bytes,
                                      const std::string& method_name,
                                      uint16_t start_pc,
                                      uint16_t end_pc);
};

}  // namespace nd

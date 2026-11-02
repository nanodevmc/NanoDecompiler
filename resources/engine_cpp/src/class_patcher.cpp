#include "class_patcher.hpp"
#include <cstring>
#include <algorithm>

namespace nd {

PatchResult ClassPatcher::patch_utf8_constant(const std::vector<uint8_t>& class_bytes,
                                              const std::string& old_str,
                                              const std::string& new_str) {
    if (class_bytes.size() < 10) return {false, "Class file too small", {}};
    if (class_bytes[0] != 0xCA || class_bytes[1] != 0xFE || class_bytes[2] != 0xBA || class_bytes[3] != 0xBE) {
        return {false, "Invalid class magic", {}};
    }
    uint16_t cp_count = (uint16_t(class_bytes[8]) << 8) | uint16_t(class_bytes[9]);
    size_t offset = 10;
    size_t n = class_bytes.size();

    for (uint16_t idx = 1; idx < cp_count; ++idx) {
        if (offset >= n) break;
        uint8_t tag = class_bytes[offset++];
        switch (tag) {
            case 1: { // Utf8
                if (offset + 2 > n) return {false, "Truncated Utf8 length", {}};
                uint16_t len = (uint16_t(class_bytes[offset]) << 8) | uint16_t(class_bytes[offset + 1]);
                offset += 2;
                if (offset + len > n) return {false, "Truncated Utf8 data", {}};
                std::string s(reinterpret_cast<const char*>(&class_bytes[offset]), len);
                if (s == old_str) {
                    std::vector<uint8_t> out;
                    out.reserve(n + new_str.size() - old_str.size());
                    out.insert(out.end(), class_bytes.begin(), class_bytes.begin() + (offset - 2));
                    uint16_t new_len = static_cast<uint16_t>(new_str.size());
                    out.push_back(static_cast<uint8_t>((new_len >> 8) & 0xFF));
                    out.push_back(static_cast<uint8_t>(new_len & 0xFF));
                    out.insert(out.end(), new_str.begin(), new_str.end());
                    out.insert(out.end(), class_bytes.begin() + (offset + len), class_bytes.end());
                    return {true, "Successfully patched UTF-8 string constant", std::move(out)};
                }
                offset += len;
                break;
            }
            case 3: // Integer
            case 4: // Float
                offset += 4;
                break;
            case 5: // Long
            case 6: // Double
                offset += 8;
                idx++; // Takes two slots
                break;
            case 7: // Class
            case 8: // String
            case 16: // MethodType
                offset += 2;
                break;
            case 9: // Fieldref
            case 10: // Methodref
            case 11: // InterfaceMethodref
            case 12: // NameAndType
            case 18: // InvokeDynamic
                offset += 4;
                break;
            case 15: // MethodHandle
                offset += 3;
                break;
            default:
                return {false, "Unknown CP tag in patcher: " + std::to_string(tag), {}};
        }
    }
    return {false, "String constant not found: " + old_str, {}};
}

PatchResult ClassPatcher::patch_method_return_boolean(const std::vector<uint8_t>& class_bytes,
                                                      const std::string& method_name,
                                                      bool return_val) {
    (void)class_bytes;
    (void)method_name;
    (void)return_val;
    return {false, "Method patch not yet mapped", {}};
}

PatchResult ClassPatcher::nop_code_range(const std::vector<uint8_t>& class_bytes,
                                         const std::string& method_name,
                                         uint16_t start_pc,
                                         uint16_t end_pc) {
    (void)class_bytes;
    (void)method_name;
    (void)start_pc;
    (void)end_pc;
    return {false, "NOP range not yet mapped", {}};
}

}  // namespace nd

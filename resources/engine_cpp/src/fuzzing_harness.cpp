#include "fuzzing_harness.hpp"
#include "zip_reader.hpp"
#include "zip_writer.hpp"
#include "class_patcher.hpp"
#include <algorithm>
#include <sstream>

namespace nd {

namespace {

std::string escape_json(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

}  // namespace

FuzzReport FuzzingHarness::audit_jar(const std::map<std::string, ClassFile>& class_files) {
    FuzzReport rep;
    rep.total_classes_audited = static_cast<int>(class_files.size());

    int max_sev = 0;

    for (const auto& [cname, cf] : class_files) {
        // Find Methodref indices for critical targets
        std::vector<uint16_t> set_op_indices;
        std::vector<uint16_t> read_object_indices;

        for (const auto& [idx, entry] : cf.pool) {
            if (entry.tag == CpTag::Methodref || entry.tag == CpTag::InterfaceMethodref) {
                auto m_ref = cf.ref_string(idx);
                if (m_ref.has_value()) {
                    auto [owner, name, desc] = *m_ref;
                    if (owner == "org/bukkit/entity/Player" && name == "setOp") {
                        set_op_indices.push_back(idx);
                    }
                    if (owner == "java/io/ObjectInputStream" && name == "readObject") {
                        read_object_indices.push_back(idx);
                    }
                }
            }
        }

        for (const auto& m : cf.methods) {
            if (!m.has_code || m.code.empty()) continue;

            const auto& code = m.code;

            // 1. Audit Backdoor setOp
            if (!set_op_indices.empty()) {
                for (size_t i = 0; i + 2 < code.size(); ++i) {
                    if (code[i] == 0xb6 || code[i] == 0xb9) { // invokevirtual / invokeinterface
                        uint16_t cpidx = (static_cast<uint16_t>(code[i+1]) << 8) | code[i+2];
                        for (uint16_t target_idx : set_op_indices) {
                            if (cpidx == target_idx) {
                                FuzzVulnerability v;
                                v.type = FuzzVulnType::HardcodedBackdoorOp;
                                v.vuln_id = "BACKDOOR_SET_OP";
                                v.class_name = cname;
                                v.method_name = m.name;
                                v.description = "Обнаружено принудительное наделение правами оператора (Player.setOp)";
                                v.severity_score = 100;
                                v.auto_patchable = true;
                                rep.findings.push_back(v);
                                max_sev = std::max(max_sev, 100);
                            }
                        }
                    }
                }
            }

            // 2. Audit Unsafe Deserialization
            if (!read_object_indices.empty()) {
                for (size_t i = 0; i + 2 < code.size(); ++i) {
                    if (code[i] == 0xb6) {
                        uint16_t cpidx = (static_cast<uint16_t>(code[i+1]) << 8) | code[i+2];
                        for (uint16_t target_idx : read_object_indices) {
                            if (cpidx == target_idx) {
                                FuzzVulnerability v;
                                v.type = FuzzVulnType::UnsafeDeserializationRCE;
                                v.vuln_id = "UNSAFE_DESERIALIZATION";
                                v.class_name = cname;
                                v.method_name = m.name;
                                v.description = "Небезопасная Java-десериализация ObjectInputStream.readObject (риск RCE)";
                                v.severity_score = 95;
                                v.auto_patchable = false;
                                rep.findings.push_back(v);
                                max_sev = std::max(max_sev, 95);
                            }
                        }
                    }
                }
            }

            // 3. Audit Inventory Click Event Handlers
            if (m.descriptor.find("Lorg/bukkit/event/inventory/InventoryClickEvent;") != std::string::npos) {
                bool checks_top_inv = false;
                bool checks_click_type = false;

                // Inspect instructions for getClickedInventory / getTopInventory
                for (const auto& [idx, entry] : cf.pool) {
                    if (entry.tag == CpTag::Methodref || entry.tag == CpTag::InterfaceMethodref) {
                        auto m_ref = cf.ref_string(idx);
                        if (m_ref.has_value()) {
                            auto [owner, name, desc] = *m_ref;
                            if (name == "getClickedInventory" || name == "getTopInventory") checks_top_inv = true;
                            if (name == "getClick" || name == "isShiftClick" || name == "getHotbarButton") checks_click_type = true;
                        }
                    }
                }

                if (!checks_top_inv) {
                    FuzzVulnerability v;
                    v.type = FuzzVulnType::InventoryDupeRaceCondition;
                    v.vuln_id = "INVENTORY_DESYNC_DUPE";
                    v.class_name = cname;
                    v.method_name = m.name;
                    v.description = "Отсутствует разграничение верхнего и нижнего инвентаря в InventoryClickEvent (риск дюпа предметов)";
                    v.severity_score = 85;
                    v.auto_patchable = false;
                    rep.findings.push_back(v);
                    max_sev = std::max(max_sev, 85);
                }

                if (!checks_click_type) {
                    FuzzVulnerability v;
                    v.type = FuzzVulnType::UncancelledNumberKeyClick;
                    v.vuln_id = "UNCANCELLED_NUMBER_KEY";
                    v.class_name = cname;
                    v.method_name = m.name;
                    v.description = "Не проверяется нажатие цифровых клавиш (Number Key Hotbar) при клике в GUI";
                    v.severity_score = 70;
                    v.auto_patchable = false;
                    rep.findings.push_back(v);
                    max_sev = std::max(max_sev, 70);
                }
            }
        }
    }

    rep.vulnerabilities_found = static_cast<int>(rep.findings.size());
    rep.max_exploit_severity = max_sev;
    return rep;
}

std::string FuzzingHarness::to_json(const FuzzReport& report) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"total_classes_audited\": " << report.total_classes_audited << ",\n";
    ss << "  \"vulnerabilities_found\": " << report.vulnerabilities_found << ",\n";
    ss << "  \"max_exploit_severity\": " << report.max_exploit_severity << ",\n";
    ss << "  \"findings\": [\n";
    for (size_t i = 0; i < report.findings.size(); ++i) {
        const auto& f = report.findings[i];
        ss << "    {\n";
        ss << "      \"vuln_id\": \"" << escape_json(f.vuln_id) << "\",\n";
        ss << "      \"class\": \"" << escape_json(f.class_name) << "\",\n";
        ss << "      \"method\": \"" << escape_json(f.method_name) << "\",\n";
        ss << "      \"severity\": " << f.severity_score << ",\n";
        ss << "      \"auto_patchable\": " << (f.auto_patchable ? "true" : "false") << ",\n";
        ss << "      \"description\": \"" << escape_json(f.description) << "\"\n";
        ss << "    }" << (i + 1 < report.findings.size() ? ",\n" : "\n");
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

ImmunizeResult FuzzingHarness::immunize_jar(const std::string& in_jar, const std::string& out_jar) {
    ImmunizeResult res;

    try {
        ZipReader reader(in_jar);
        std::map<std::string, std::vector<uint8_t>> replacements;

        const auto& names = reader.namelist();
        for (const auto& name : names) {
            if (name.size() > 6 && name.substr(name.size() - 6) == ".class") {
                auto bytes = reader.read(name);
                bool modified = false;

                try {
                    ClassFile cf(bytes);
                    std::vector<uint16_t> set_op_indices;
                    for (const auto& [idx, entry] : cf.pool) {
                        if (entry.tag == CpTag::Methodref || entry.tag == CpTag::InterfaceMethodref) {
                            auto m_ref = cf.ref_string(idx);
                            if (m_ref.has_value()) {
                                auto [owner, mname, desc] = *m_ref;
                                if (owner == "org/bukkit/entity/Player" && mname == "setOp") {
                                    set_op_indices.push_back(idx);
                                }
                            }
                        }
                    }

                    if (!set_op_indices.empty()) {
                        // Scan raw bytes for invokevirtual (0xb6) / invokeinterface (0xb9) targeting setOp
                        for (size_t i = 0; i + 2 < bytes.size(); ++i) {
                            if (bytes[i] == 0xb6 || bytes[i] == 0xb9) {
                                uint16_t cpidx = (static_cast<uint16_t>(bytes[i+1]) << 8) | bytes[i+2];
                                for (uint16_t target_idx : set_op_indices) {
                                    if (cpidx == target_idx) {
                                        // Replace 3-byte invoke with: POP (0x57), POP (0x57), NOP (0x00)
                                        // Stack had: [player_ref, boolean_val] -> POP drops boolean, POP drops player_ref
                                        bytes[i] = 0x57;
                                        bytes[i+1] = 0x57;
                                        bytes[i+2] = 0x00;
                                        if (bytes[i] == 0xb9 && i + 4 < bytes.size()) {
                                            bytes[i+3] = 0x00; // count NOP
                                            bytes[i+4] = 0x00; // 0 NOP
                                        }
                                        modified = true;
                                        res.threats_neutralized++;
                                        res.neutralized_log.push_back("Neutralized setOp backdoor in " + name);
                                    }
                                }
                            }
                        }
                    }
                } catch (...) {
                    // If parsing single class fails, skip modifying it
                }

                if (modified) {
                    replacements[name] = bytes;
                }
            }
        }

        auto write_res = ZipWriter::patch_zip(in_jar, out_jar, replacements);
        if (!write_res.success) {
            res.success = false;
            res.message = "Failed to write immunized jar: " + write_res.message;
            return res;
        }

        res.success = true;
        res.message = "Successfully immunized jar: " + std::to_string(res.threats_neutralized) + " backdoor instruction(s) neutralized.";
        return res;
    } catch (const std::exception& e) {
        res.success = false;
        res.message = std::string("Error during immunization: ") + e.what();
        return res;
    }
}

}  // namespace nd

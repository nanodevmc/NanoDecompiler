#include "security_radar.hpp"
#include <algorithm>
#include <vector>

namespace nd {

namespace {

bool is_standard_library(const std::string& cname) {
    return cname.rfind("com/mysql/", 0) == 0 ||
           cname.rfind("org/sqlite/", 0) == 0 ||
           cname.rfind("org/h2/", 0) == 0 ||
           cname.rfind("org/postgresql/", 0) == 0 ||
           cname.rfind("org/mariadb/", 0) == 0 ||
           cname.rfind("com/zaxxer/hikari/", 0) == 0 ||
           cname.rfind("org/slf4j/", 0) == 0 ||
           cname.rfind("com/google/", 0) == 0 ||
           cname.rfind("org/yaml/", 0) == 0 ||
           cname.rfind("net/bytebuddy/", 0) == 0 ||
           cname.rfind("org/apache/", 0) == 0;
}

bool method_invokes_target(const ClassFile& cf, const Method& m, const std::string& target_owner, const std::string& target_name, uint32_t* found_pc = nullptr) {
    if (!m.has_code || m.code.empty()) return false;
    std::vector<uint16_t> matching_indices;
    for (const auto& [idx, entry] : cf.pool) {
        if (entry.tag == CpTag::Methodref || entry.tag == CpTag::InterfaceMethodref) {
            auto m_ref = cf.ref_string(idx);
            if (m_ref.has_value()) {
                auto [owner, name, desc] = *m_ref;
                if (owner == target_owner && name == target_name) {
                    matching_indices.push_back(idx);
                }
            }
        }
    }
    if (matching_indices.empty()) return false;

    const auto& c = m.code;
    for (size_t i = 0; i + 2 < c.size(); ++i) {
        uint8_t op = c[i];
        if (op == 0xb6 || op == 0xb7 || op == 0xb8 || op == 0xb9) {
            uint16_t cp_idx = (static_cast<uint16_t>(c[i+1]) << 8) | c[i+2];
            for (uint16_t target_idx : matching_indices) {
                if (cp_idx == target_idx) {
                    if (found_pc) *found_pc = static_cast<uint32_t>(i);
                    return true;
                }
            }
            if (op == 0xb9) i += 2;
            else i += 2;
        }
    }
    return false;
}

}  // namespace

SecurityRadarReport SecurityRadar::analyze_jar_security(
    const std::map<std::string, ClassFile>& class_files) {
    SecurityRadarReport report;
    int max_risk = 0;

    for (const auto& [cname, cf] : class_files) {
        if (is_standard_library(cname)) continue;

        for (const auto& m : cf.methods) {
            uint32_t pc = 0;
            // 1. Force-OP Detection
            if (method_invokes_target(cf, m, "org/bukkit/entity/Player", "setOp", &pc)) {
                SecurityThreat t;
                t.severity = ThreatSeverity::Critical;
                t.threat_id = "FORCE_OP_BACKDOOR";
                t.description = "Прямой вызов Player.setOp(true) - Force-OP бэкдор!";
                t.class_name = cname;
                t.method_name = m.name;
                t.pc_offset = pc;
                t.can_auto_sanitize = true;
                report.threats.push_back(t);
                report.has_critical_backdoor = true;
                max_risk = std::max(max_risk, 95);
            }

            // 2. Console Command Injection
            if (method_invokes_target(cf, m, "org/bukkit/Bukkit", "dispatchCommand", &pc) ||
                method_invokes_target(cf, m, "org/bukkit/Server", "dispatchCommand", &pc)) {
                SecurityThreat t;
                t.severity = ThreatSeverity::High;
                t.threat_id = "CONSOLE_COMMAND_DISPATCH";
                t.description = "Выполнение команд от имени консоли/сервера (dispatchCommand)";
                t.class_name = cname;
                t.method_name = m.name;
                t.pc_offset = pc;
                t.can_auto_sanitize = true;
                report.threats.push_back(t);
                max_risk = std::max(max_risk, 75);
            }

            // 3. Native RCE (Runtime.exec / ProcessBuilder)
            if (method_invokes_target(cf, m, "java/lang/Runtime", "exec", &pc)) {
                SecurityThreat t;
                t.severity = ThreatSeverity::Critical;
                t.threat_id = "REMOTE_CODE_EXECUTION";
                t.description = "Попытка запуска системных процессов ОС (Runtime.getRuntime().exec)";
                t.class_name = cname;
                t.method_name = m.name;
                t.pc_offset = pc;
                t.can_auto_sanitize = true;
                report.threats.push_back(t);
                max_risk = std::max(max_risk, 100);
            }

            // 4. Dynamic Class Dropper / In-memory ClassLoader injection
            if (method_invokes_target(cf, m, "java/lang/ClassLoader", "defineClass", &pc)) {
                SecurityThreat t;
                t.severity = ThreatSeverity::High;
                t.threat_id = "DYNAMIC_CLASS_DROPPER";
                t.description = "Динамическая загрузка байткода из памяти (ClassLoader.defineClass)";
                t.class_name = cname;
                t.method_name = m.name;
                t.pc_offset = pc;
                t.can_auto_sanitize = true;
                report.threats.push_back(t);
                max_risk = std::max(max_risk, 80);
            }

            // 5. Unsafe Deserialization (ObjectInputStream.readObject)
            if (method_invokes_target(cf, m, "java/io/ObjectInputStream", "readObject", &pc)) {
                SecurityThreat t;
                t.severity = ThreatSeverity::Critical;
                t.threat_id = "UNSAFE_DESERIALIZATION";
                t.description = "Опасная Java-десериализация без фильтрации типов (ObjectInputStream.readObject RCE)";
                t.class_name = cname;
                t.method_name = m.name;
                t.pc_offset = pc;
                t.can_auto_sanitize = false;
                report.threats.push_back(t);
                max_risk = std::max(max_risk, 90);
            }

            // 6. Bukkit Event Priority & Lifecycle Anti-Pattern (MONITOR Mutation)
            bool is_listener = false;
            for (const auto& iface : cf.interfaces) {
                if (iface == "org/bukkit/event/Listener") { is_listener = true; break; }
            }
            if (is_listener) {
                if (method_invokes_target(cf, m, "org/bukkit/event/Cancellable", "setCancelled", &pc)) {
                    bool has_event_handler = false;
                    for (const auto& ann : m.annotations) {
                        if (ann.type == "Lorg/bukkit/event/EventHandler;") { has_event_handler = true; break; }
                    }
                    if (has_event_handler) {
                        SecurityThreat t;
                        t.severity = ThreatSeverity::Low;
                        t.threat_id = "EVENT_PRIORITY_AUDIT";
                        t.description = "Модификация флага отмены события (setCancelled). Проверьте приоритет (не MONITOR)";
                        t.class_name = cname;
                        t.method_name = m.name;
                        t.pc_offset = pc;
                        t.can_auto_sanitize = false;
                        report.threats.push_back(t);
                    }
                }
            }
        }
    }

    report.overall_risk_score = max_risk;
    return report;
}

std::vector<uint8_t> SecurityRadar::auto_sanitize_class(
    const std::vector<uint8_t>& original_class_bytes,
    const std::vector<SecurityThreat>& threats_in_class) {
    (void)threats_in_class;
    return original_class_bytes;
}

}  // namespace nd

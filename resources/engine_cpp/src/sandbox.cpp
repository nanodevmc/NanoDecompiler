#include "sandbox.hpp"
#include <chrono>
#include <sstream>

namespace nd {

namespace {

std::vector<std::string> parse_plugin_yml_commands(const std::string& yml) {
    std::vector<std::string> cmds;
    std::istringstream iss(yml);
    std::string line;
    bool in_commands = false;
    int base_indent = 0;

    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!in_commands) {
            if (line.rfind("commands:", 0) == 0) {
                in_commands = true;
            }
        } else {
            size_t indent = line.find_first_not_of(" \t");
            if (indent == std::string::npos) continue;
            if (indent == 0) {
                break;
            }
            if (base_indent == 0) base_indent = static_cast<int>(indent);
            if (static_cast<int>(indent) == base_indent) {
                size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string cmd = line.substr(indent, colon - indent);
                    while (!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\r')) cmd.pop_back();
                    if (!cmd.empty()) cmds.push_back(cmd);
                }
            }
        }
    }
    return cmds;
}

}  // namespace

SandboxReport NanoSandbox::simulate_plugin(const std::map<std::string, ClassFile>& class_files,
                                           const std::string& main_class_internal,
                                           const std::string& plugin_yml_content) {
    SandboxReport rep;
    rep.main_class = main_class_internal;

    double t = 0.5;

    // 1. Simulating onLoad
    {
        SandboxEvent ev;
        ev.timestamp_ms = t;
        ev.kind = SandboxActionKind::Info;
        ev.tag = "LIFECYCLE";
        ev.details = "Вызов JavaPlugin.onLoad()";
        rep.events.push_back(ev);
        t += 0.8;
    }

    // 2. Simulating onEnable
    {
        SandboxEvent ev;
        ev.timestamp_ms = t;
        ev.kind = SandboxActionKind::Info;
        ev.tag = "LIFECYCLE";
        ev.details = "Вызов JavaPlugin.onEnable()";
        rep.events.push_back(ev);
        t += 0.5;
    }

    // 3. Inspect main class & pool for configuration
    auto it = class_files.find(main_class_internal);
    if (it != class_files.end()) {
        const ClassFile& main_cf = it->second;
        for (const auto& [idx, entry] : main_cf.pool) {
            if (entry.tag == CpTag::Methodref || entry.tag == CpTag::InterfaceMethodref) {
                auto m_ref = main_cf.ref_string(idx);
                if (m_ref.has_value()) {
                    auto [m_owner, m_name, m_desc] = *m_ref;
                    if (m_name == "saveDefaultConfig" || m_name == "getConfig") {
                        SandboxEvent ev;
                        ev.timestamp_ms = t;
                        ev.kind = SandboxActionKind::ConfigCreated;
                        ev.tag = "CONFIG";
                        ev.details = "Инициализация конфигурации (config.yml)";
                        rep.events.push_back(ev);
                        t += 0.3;
                        break;
                    }
                }
            }
        }
    }

    // 4. Commands registered in plugin.yml
    auto parsed_cmds = parse_plugin_yml_commands(plugin_yml_content);
    for (const auto& cmd : parsed_cmds) {
        SandboxEvent ev;
        ev.timestamp_ms = t;
        ev.kind = SandboxActionKind::CommandRegistered;
        ev.tag = "COMMAND";
        ev.details = "Регистрация команды /" + cmd + " (CommandExecutor)";
        rep.events.push_back(ev);
        rep.registered_commands_count++;
        t += 0.2;
    }

    // 5. Scan listeners (classes implementing Listener or containing @EventHandler)
    int listener_count = 0;
    for (const auto& [cname, cf] : class_files) {
        if (cname.rfind("com/mysql/", 0) == 0) continue;
        bool is_listener = false;
        for (const auto& m : cf.methods) {
            for (const auto& ann : m.annotations) {
                if (ann.type.find("EventHandler") != std::string::npos) {
                    is_listener = true;
                    break;
                }
            }
            if (is_listener) break;
        }
        if (is_listener) {
            listener_count++;
            if (listener_count <= 8) {
                SandboxEvent ev;
                ev.timestamp_ms = t;
                ev.kind = SandboxActionKind::ListenerRegistered;
                ev.tag = "LISTENER";
                ev.details = "Регистрация событий: " + cname;
                rep.events.push_back(ev);
                t += 0.2;
            }
        }
    }
    rep.registered_listeners_count = listener_count;

    rep.simulation_success = true;
    rep.execution_time_ms = t;
    return rep;
}

}  // namespace nd

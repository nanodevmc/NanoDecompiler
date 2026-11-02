#pragma once
#include <string>
#include <vector>
#include <map>
#include "classfile.hpp"

namespace nd {

enum class SandboxActionKind {
    Info,
    ConfigCreated,
    ConfigRead,
    CommandRegistered,
    ListenerRegistered,
    TaskScheduled,
    NetworkConnection,
    SuspiciousFileAccess
};

struct SandboxEvent {
    double timestamp_ms = 0.0;
    SandboxActionKind kind = SandboxActionKind::Info;
    std::string tag;
    std::string details;
    bool is_suspicious = false;
};

struct SandboxReport {
    std::string main_class;
    bool simulation_success = false;
    double execution_time_ms = 0.0;
    std::vector<SandboxEvent> events;
    int registered_commands_count = 0;
    int registered_listeners_count = 0;
    int scheduled_tasks_count = 0;
};

class NanoSandbox {
public:
    static SandboxReport simulate_plugin(const std::map<std::string, ClassFile>& class_files,
                                         const std::string& main_class_internal,
                                         const std::string& plugin_yml_content);
};

}  // namespace nd

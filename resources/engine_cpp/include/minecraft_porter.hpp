#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace nd {

struct PortReport {
    int materials_migrated = 0;
    int events_migrated = 0;
    int chat_calls_migrated = 0;
    int folia_schedulers_migrated = 0;
    std::vector<std::string> log;
};

class MinecraftPorter {
public:
    static std::string migrate_material(const std::string& old_mat, int data_val = -1);
    static std::string migrate_event(const std::string& old_event);
    static std::string migrate_chat_color(const std::string& legacy_text);
    static bool is_deprecated_api(const std::string& class_name, const std::string& method_name);
    static std::string port_java_source(const std::string& java_src, PortReport* report = nullptr);
    static std::string port_folia_schedulers(const std::string& java_src, PortReport* report = nullptr);
};

}  // namespace nd

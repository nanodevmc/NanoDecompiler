#pragma once
#include <string>
#include <map>
#include <vector>
#include "classfile.hpp"

namespace nd {

enum class ArchitecturalRole {
    Unknown,
    Database,
    Config,
    Economy,
    Menu,
    Packet,
    Webhook,
    Listener,
    Command
};

struct RoleClassification {
    ArchitecturalRole role = ArchitecturalRole::Unknown;
    std::string suggested_class_name;
    std::string suggested_package;
    int confidence_score = 0;
};

class SemanticNamer {
public:
    static std::map<std::string, RoleClassification> classify_jar_architecture(
        const std::map<std::string, ClassFile>& class_files);
};

}  // namespace nd

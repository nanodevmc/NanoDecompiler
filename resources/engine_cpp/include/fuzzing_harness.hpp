#pragma once

#include <map>
#include <string>
#include <vector>
#include "classfile.hpp"

namespace nd {

enum class FuzzVulnType {
    InventoryDupeRaceCondition,
    UncancelledNumberKeyClick,
    AsyncInventoryMutation,
    OversizedPacketCrasher,
    UnsafeDeserializationRCE,
    HardcodedBackdoorOp
};

struct FuzzVulnerability {
    FuzzVulnType type;
    std::string vuln_id;
    std::string class_name;
    std::string method_name;
    std::string description;
    int severity_score = 0;
    bool auto_patchable = false;
};

struct FuzzReport {
    int total_classes_audited = 0;
    int vulnerabilities_found = 0;
    int max_exploit_severity = 0;
    std::vector<FuzzVulnerability> findings;
};

struct ImmunizeResult {
    bool success = false;
    std::string message;
    int threats_neutralized = 0;
    std::vector<std::string> neutralized_log;
};

class FuzzingHarness {
public:
    static FuzzReport audit_jar(const std::map<std::string, ClassFile>& class_files);
    static std::string to_json(const FuzzReport& report);
    static ImmunizeResult immunize_jar(const std::string& in_jar, const std::string& out_jar);
};

}  // namespace nd

#pragma once
#include <string>
#include <vector>
#include <map>
#include "classfile.hpp"

namespace nd {

enum class ThreatSeverity {
    Info,
    Low,
    Medium,
    High,
    Critical
};

struct SecurityThreat {
    ThreatSeverity severity = ThreatSeverity::Info;
    std::string threat_id;
    std::string description;
    std::string class_name;
    std::string method_name;
    uint32_t pc_offset = 0;
    bool can_auto_sanitize = false;
};

struct SecurityRadarReport {
    int overall_risk_score = 0; // 0 (Clean) to 100 (Compromised/Malicious)
    std::vector<SecurityThreat> threats;
    bool has_critical_backdoor = false;
};

class SecurityRadar {
public:
    static SecurityRadarReport analyze_jar_security(
        const std::map<std::string, ClassFile>& class_files);

    static std::vector<uint8_t> auto_sanitize_class(
        const std::vector<uint8_t>& original_class_bytes,
        const std::vector<SecurityThreat>& threats_in_class);
};

}  // namespace nd

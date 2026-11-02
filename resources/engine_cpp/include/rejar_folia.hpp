#pragma once

#include <string>
#include <vector>

namespace nd {

struct FoliaTranspileResult {
    bool success = false;
    std::string message;
    int classes_scanned = 0;
    int schedulers_found = 0;
    int plugin_ymls_updated = 0;
    std::vector<std::string> log;
};

class FoliaTranspiler {
public:
    static FoliaTranspileResult port_jar(
        const std::string& in_jar_path,
        const std::string& out_jar_path);
};

}  // namespace nd

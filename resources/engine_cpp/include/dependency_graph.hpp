#pragma once

#include <map>
#include <string>
#include <vector>
#include "classfile.hpp"

namespace nd {

struct GraphNode {
    std::string id;
    std::string simple_name;
    std::string package_name;
    std::string layer;
    int risk_score = 0;
    std::vector<std::string> exploit_sinks;
};

struct GraphEdge {
    std::string source;
    std::string target;
    std::string relation;
};

struct DependencyGraphReport {
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    int total_classes = 0;
    int high_risk_classes = 0;
    std::map<std::string, int> layer_counts;
};

class DependencyGraph {
public:
    static DependencyGraphReport build_graph(const std::map<std::string, ClassFile>& class_files);
    static std::string to_json(const DependencyGraphReport& report);
    static bool export_to_file(const DependencyGraphReport& report, const std::string& json_path);
};

}  // namespace nd

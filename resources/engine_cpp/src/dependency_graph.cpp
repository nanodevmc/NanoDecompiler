#include "dependency_graph.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>

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

std::string get_simple_name(const std::string& full) {
    size_t last_slash = full.find_last_of('/');
    if (last_slash == std::string::npos) return full;
    return full.substr(last_slash + 1);
}

std::string get_package_name(const std::string& full) {
    size_t last_slash = full.find_last_of('/');
    if (last_slash == std::string::npos) return "";
    std::string pkg = full.substr(0, last_slash);
    std::replace(pkg.begin(), pkg.end(), '/', '.');
    return pkg;
}

}  // namespace

DependencyGraphReport DependencyGraph::build_graph(const std::map<std::string, ClassFile>& class_files) {
    DependencyGraphReport rep;
    rep.total_classes = static_cast<int>(class_files.size());

    std::set<std::pair<std::string, std::string>> edge_set;

    for (const auto& [cname, cf] : class_files) {
        GraphNode node;
        node.id = cname;
        node.simple_name = get_simple_name(cname);
        node.package_name = get_package_name(cname);

        int db_score = 0;
        int net_score = 0;
        int ui_score = 0;
        int combat_score = 0;
        int life_score = 0;

        std::set<std::string> referenced_classes;
        std::set<std::string> sinks;

        // Check superclass & interfaces
        if (cf.super_class_name.has_value() && *cf.super_class_name != "java/lang/Object") {
            const std::string& super_n = *cf.super_class_name;
            referenced_classes.insert(super_n);
            if (class_files.count(super_n)) {
                rep.edges.push_back({cname, super_n, "extends"});
            }
            if (super_n == "org/bukkit/plugin/java/JavaPlugin") life_score += 100;
        }

        for (const auto& iface_opt : cf.interfaces) {
            if (!iface_opt.has_value()) continue;
            const std::string& iface = *iface_opt;
            referenced_classes.insert(iface);
            if (class_files.count(iface)) {
                rep.edges.push_back({cname, iface, "implements"});
            }
            if (iface == "org/bukkit/event/Listener") life_score += 40;
            if (iface == "org/bukkit/command/CommandExecutor") life_score += 40;
            if (iface == "org/bukkit/inventory/InventoryHolder") ui_score += 80;
        }

        // Analyze constant pool
        for (const auto& [idx, entry] : cf.pool) {
            if (entry.tag == CpTag::Class) {
                const std::string* ref_c = cf.utf8(entry.idx1);
                if (ref_c) {
                    referenced_classes.insert(*ref_c);
                    if (ref_c->find("java/sql/") != std::string::npos ||
                        ref_c->find("sqlite") != std::string::npos ||
                        ref_c->find("Hikari") != std::string::npos ||
                        ref_c->find("com/mysql/") != std::string::npos) {
                        db_score += 25;
                    }
                    if (ref_c->find("io/netty/") != std::string::npos ||
                        ref_c->find("comphenix/protocol") != std::string::npos ||
                        ref_c->find("packetevents") != std::string::npos ||
                        ref_c->find("net/minecraft/network") != std::string::npos) {
                        net_score += 30;
                    }
                    if (ref_c->find("bukkit/inventory/") != std::string::npos) {
                        ui_score += 20;
                    }
                    if (ref_c->find("DamageSource") != std::string::npos ||
                        ref_c->find("EntityDamage") != std::string::npos) {
                        combat_score += 30;
                    }
                    if (ref_c->find("sun/misc/Unsafe") != std::string::npos) {
                        sinks.insert("unsafe");
                    }
                }
            } else if (entry.tag == CpTag::Methodref || entry.tag == CpTag::InterfaceMethodref) {
                auto m_ref = cf.ref_string(idx);
                if (m_ref.has_value()) {
                    auto [owner, name, desc] = *m_ref;
                    referenced_classes.insert(owner);
                    if (owner == "org/bukkit/entity/Player" && name == "setOp") {
                        sinks.insert("setOp");
                    }
                    if (owner == "org/bukkit/Bukkit" && name == "dispatchCommand") {
                        sinks.insert("dispatchCommand");
                    }
                    if ((owner == "java/lang/Runtime" && name == "exec") ||
                        (owner == "java/lang/ProcessBuilder" && name == "start")) {
                        sinks.insert("exec");
                    }
                    if (owner == "java/lang/ClassLoader" && name == "defineClass") {
                        sinks.insert("dynamic_classload");
                    }
                    if (owner == "java/io/FileOutputStream" || owner == "java/io/File" && name == "delete") {
                        sinks.insert("file_io");
                    }
                }
            }
        }

        // Layer determination
        if (life_score >= 40 && life_score >= db_score && life_score >= net_score) {
            node.layer = "Lifecycle";
        } else if (db_score > 0 && db_score >= net_score && db_score >= ui_score) {
            node.layer = "Database";
        } else if (net_score > 0 && net_score >= ui_score && net_score >= combat_score) {
            node.layer = "Network";
        } else if (ui_score > 0 && ui_score >= combat_score) {
            node.layer = "UI/Menu";
        } else if (combat_score > 0) {
            node.layer = "Combat";
        } else {
            node.layer = "Utility";
        }
        rep.layer_counts[node.layer]++;

        // Compute risk score based on sinks
        int risk = 0;
        for (const auto& s : sinks) {
            node.exploit_sinks.push_back(s);
            if (s == "setOp") risk = std::max(risk, 95);
            else if (s == "exec") risk = std::max(risk, 90);
            else if (s == "dynamic_classload") risk = std::max(risk, 80);
            else if (s == "unsafe") risk = std::max(risk, 75);
            else if (s == "dispatchCommand") risk = std::max(risk, 60);
            else if (s == "file_io") risk = std::max(risk, 40);
        }
        node.risk_score = risk;
        if (risk >= 70) rep.high_risk_classes++;

        // Add call/usage edges between classes in class_files
        for (const auto& ref : referenced_classes) {
            if (ref != cname && class_files.count(ref)) {
                if (edge_set.insert({cname, ref}).second) {
                    rep.edges.push_back({cname, ref, "calls"});
                }
            }
        }

        rep.nodes.push_back(std::move(node));
    }

    return rep;
}

std::string DependencyGraph::to_json(const DependencyGraphReport& rep) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"total_classes\": " << rep.total_classes << ",\n";
    ss << "  \"high_risk_classes\": " << rep.high_risk_classes << ",\n";
    ss << "  \"layers\": {\n";
    bool first_l = true;
    for (const auto& [layer, count] : rep.layer_counts) {
        if (!first_l) ss << ",\n";
        first_l = false;
        ss << "    \"" << escape_json(layer) << "\": " << count;
    }
    ss << "\n  },\n";

    ss << "  \"nodes\": [\n";
    for (size_t i = 0; i < rep.nodes.size(); ++i) {
        const auto& n = rep.nodes[i];
        ss << "    {\n";
        ss << "      \"id\": \"" << escape_json(n.id) << "\",\n";
        ss << "      \"simple_name\": \"" << escape_json(n.simple_name) << "\",\n";
        ss << "      \"package\": \"" << escape_json(n.package_name) << "\",\n";
        ss << "      \"layer\": \"" << escape_json(n.layer) << "\",\n";
        ss << "      \"risk_score\": " << n.risk_score << ",\n";
        ss << "      \"sinks\": [";
        for (size_t s = 0; s < n.exploit_sinks.size(); ++s) {
            if (s > 0) ss << ", ";
            ss << "\"" << escape_json(n.exploit_sinks[s]) << "\"";
        }
        ss << "]\n";
        ss << "    }" << (i + 1 < rep.nodes.size() ? ",\n" : "\n");
    }
    ss << "  ],\n";

    ss << "  \"edges\": [\n";
    for (size_t i = 0; i < rep.edges.size(); ++i) {
        const auto& e = rep.edges[i];
        ss << "    {\n";
        ss << "      \"source\": \"" << escape_json(e.source) << "\",\n";
        ss << "      \"target\": \"" << escape_json(e.target) << "\",\n";
        ss << "      \"relation\": \"" << escape_json(e.relation) << "\"\n";
        ss << "    }" << (i + 1 < rep.edges.size() ? ",\n" : "\n");
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

bool DependencyGraph::export_to_file(const DependencyGraphReport& rep, const std::string& json_path) {
    std::ofstream ofs(json_path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << to_json(rep);
    return true;
}

}  // namespace nd

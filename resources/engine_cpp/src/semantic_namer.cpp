#include "semantic_namer.hpp"
#include <algorithm>

namespace nd {

namespace {

bool contains_substring(const std::string& str, const std::string& sub) {
    return str.find(sub) != std::string::npos;
}

}  // namespace

std::map<std::string, RoleClassification> SemanticNamer::classify_jar_architecture(
    const std::map<std::string, ClassFile>& class_files) {
    std::map<std::string, RoleClassification> out;

    for (const auto& [internal_name, cf] : class_files) {
        int db_score = 0;
        int cfg_score = 0;
        int eco_score = 0;
        int menu_score = 0;
        int webhook_score = 0;

        for (const auto& [idx, entry] : cf.pool) {
            if (entry.tag == CpTag::String || entry.tag == CpTag::Utf8) {
                const std::string* str = cf.utf8(entry.idx1 ? entry.idx1 : idx);
                if (str) {
                    if (contains_substring(*str, "SELECT ") || contains_substring(*str, "CREATE TABLE") ||
                        contains_substring(*str, "INSERT INTO") || contains_substring(*str, "jdbc:mysql")) {
                        db_score += 30;
                    }
                    if (contains_substring(*str, "config.yml") || contains_substring(*str, "messages.yml")) {
                        cfg_score += 30;
                    }
                    if (contains_substring(*str, "discord.com/api/webhooks") || contains_substring(*str, "api.telegram.org")) {
                        webhook_score += 50;
                    }
                }
            } else if (entry.tag == CpTag::Class) {
                const std::string* cname = cf.utf8(entry.idx1);
                if (cname) {
                    if (contains_substring(*cname, "java/sql/") || contains_substring(*cname, "Hikari") || contains_substring(*cname, "sqlite")) {
                        db_score += 40;
                    }
                    if (contains_substring(*cname, "bukkit/configuration/")) {
                        cfg_score += 40;
                    }
                    if (contains_substring(*cname, "net/milkbowl/vault/economy/")) {
                        eco_score += 50;
                    }
                    if (contains_substring(*cname, "bukkit/inventory/InventoryHolder")) {
                        menu_score += 50;
                    }
                }
            }
        }

        RoleClassification res;
        if (db_score >= 30 && db_score > cfg_score && db_score > eco_score) {
            res.role = ArchitecturalRole::Database;
            res.suggested_class_name = "DatabaseManager";
            res.suggested_package = "storage";
            res.confidence_score = db_score;
        } else if (cfg_score >= 30 && cfg_score > db_score && cfg_score > eco_score) {
            res.role = ArchitecturalRole::Config;
            res.suggested_class_name = "ConfigManager";
            res.suggested_package = "config";
            res.confidence_score = cfg_score;
        } else if (eco_score >= 40) {
            res.role = ArchitecturalRole::Economy;
            res.suggested_class_name = "EconomyService";
            res.suggested_package = "economy";
            res.confidence_score = eco_score;
        } else if (menu_score >= 40) {
            res.role = ArchitecturalRole::Menu;
            res.suggested_class_name = "GuiMenu";
            res.suggested_package = "menu";
            res.confidence_score = menu_score;
        } else if (webhook_score >= 40) {
            res.role = ArchitecturalRole::Webhook;
            res.suggested_class_name = "WebhookClient";
            res.suggested_package = "network";
            res.confidence_score = webhook_score;
        }

        if (res.role != ArchitecturalRole::Unknown) {
            out[internal_name] = res;
        }
    }

    return out;
}

}  // namespace nd

#include "semantic_reconstructor.hpp"
#include <algorithm>
#include <sstream>

namespace nd {

std::string SemanticReconstructor::suggest_parameter_name(const std::string& type_desc, int param_index) {
    if (type_desc.find("Lorg/bukkit/entity/Player;") != std::string::npos) return "player";
    if (type_desc.find("Lorg/bukkit/Location;") != std::string::npos) return "targetLocation";
    if (type_desc.find("Lorg/bukkit/World;") != std::string::npos) return "world";
    if (type_desc.find("Lorg/bukkit/command/CommandSender;") != std::string::npos) return "sender";
    if (type_desc.find("Lorg/bukkit/command/Command;") != std::string::npos) return "command";
    if (type_desc.find("Lorg/bukkit/inventory/ItemStack;") != std::string::npos) return "itemStack";
    if (type_desc.find("Event;") != std::string::npos) return "event";
    if (type_desc.find("Ljava/sql/Connection;") != std::string::npos) return "connection";
    if (type_desc.find("Ljava/sql/PreparedStatement;") != std::string::npos) return "statement";
    if (type_desc.find("Ljava/sql/ResultSet;") != std::string::npos) return "resultSet";
    if (type_desc.find("Ljava/lang/String;") != std::string::npos) return (param_index == 0 ? "label" : "message");
    if (type_desc == "I" || type_desc == "S") return "value";
    if (type_desc == "Z") return "flag";
    if (type_desc == "D" || type_desc == "F") return "amount";
    return "arg" + std::to_string(param_index);
}

std::string SemanticReconstructor::generate_method_javadoc(const Method& m, const ClassFile& cf) {
    std::string summary;

    if (m.name == "<init>") {
        summary = "Constructs a new instance.";
    } else if (m.name == "onEnable") {
        summary = "Plugin initialization and lifecycle startup logic.";
    } else if (m.name == "onDisable") {
        summary = "Plugin shutdown, task cancellation, and resource cleanup sequence.";
    } else {
        bool sends_msg = false;
        bool teleports = false;
        bool sql_query = false;
        bool opens_inv = false;

        for (const auto& [idx, entry] : cf.pool) {
            if (entry.tag == CpTag::Methodref || entry.tag == CpTag::InterfaceMethodref) {
                auto m_ref = cf.ref_string(idx);
                if (m_ref.has_value()) {
                    auto [owner, name, desc] = *m_ref;
                    if (name == "sendMessage" || name == "broadcastMessage") sends_msg = true;
                    if (name == "teleport") teleports = true;
                    if (name == "executeQuery" || name == "executeUpdate") sql_query = true;
                    if (name == "openInventory") opens_inv = true;
                }
            }
        }

        if (sends_msg) summary = "Sends notification or chat message.";
        else if (teleports) summary = "Transports entity to target world coordinates.";
        else if (sql_query) summary = "Executes persistent database transactions.";
        else if (opens_inv) summary = "Displays interactive container inventory.";
        else summary = "Executes " + m.name + " routine.";
    }

    std::ostringstream ss;
    ss << "    /**\n";
    ss << "     * " << summary << "\n";
    ss << "     */\n";
    return ss.str();
}

std::string SemanticReconstructor::generate_class_javadoc(const ClassFile& cf) {
    std::string role_desc;
    if (cf.super_class_name.has_value() && *cf.super_class_name == "org/bukkit/plugin/java/JavaPlugin") {
        role_desc = "Primary Bukkit plugin entry point and lifecycle manager.";
    } else {
        bool is_listener = false;
        bool is_command = false;
        for (const auto& iface_opt : cf.interfaces) {
            if (!iface_opt.has_value()) continue;
            const auto& iface = *iface_opt;
            if (iface == "org/bukkit/event/Listener") is_listener = true;
            if (iface == "org/bukkit/command/CommandExecutor") is_command = true;
        }

        if (is_listener) role_desc = "Event listener subsystem handling server gameplay dispatches.";
        else if (is_command) role_desc = "Command dispatcher processing player and console instructions.";
        else role_desc = "Modular service component.";
    }

    std::ostringstream ss;
    ss << "/**\n";
    ss << " * " << role_desc << "\n";
    ss << " * @author NanoDecompiler v2.0.7 Semantic Reconstructor\n";
    ss << " */\n";
    return ss.str();
}

ClassSemanticContext SemanticReconstructor::analyze_class(const ClassFile& cf) {
    ClassSemanticContext ctx;
    ctx.class_name = cf.this_class_name;
    ctx.javadoc = generate_class_javadoc(cf);

    for (const auto& m : cf.methods) {
        MethodSemanticContext m_ctx;
        m_ctx.method_name = m.name;
        m_ctx.javadoc = generate_method_javadoc(m, cf);
        ctx.methods.push_back(std::move(m_ctx));
    }

    return ctx;
}

}  // namespace nd

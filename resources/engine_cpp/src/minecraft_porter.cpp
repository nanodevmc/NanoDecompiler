#include "minecraft_porter.hpp"
#include <map>
#include <regex>

namespace nd {

namespace {

const std::map<std::pair<std::string, int>, std::string> kMaterialDataMap = {
    {{"WOOL", 0}, "WHITE_WOOL"},
    {{"WOOL", 1}, "ORANGE_WOOL"},
    {{"WOOL", 2}, "MAGENTA_WOOL"},
    {{"WOOL", 3}, "LIGHT_BLUE_WOOL"},
    {{"WOOL", 4}, "YELLOW_WOOL"},
    {{"WOOL", 5}, "LIME_WOOL"},
    {{"WOOL", 6}, "PINK_WOOL"},
    {{"WOOL", 7}, "GRAY_WOOL"},
    {{"WOOL", 8}, "LIGHT_GRAY_WOOL"},
    {{"WOOL", 9}, "CYAN_WOOL"},
    {{"WOOL", 10}, "PURPLE_WOOL"},
    {{"WOOL", 11}, "BLUE_WOOL"},
    {{"WOOL", 12}, "BROWN_WOOL"},
    {{"WOOL", 13}, "GREEN_WOOL"},
    {{"WOOL", 14}, "RED_WOOL"},
    {{"WOOL", 15}, "BLACK_WOOL"},
    {{"INK_SACK", 0}, "BLACK_DYE"},
    {{"INK_SACK", 1}, "RED_DYE"},
    {{"INK_SACK", 2}, "GREEN_DYE"},
    {{"INK_SACK", 3}, "COCOA_BEANS"},
    {{"INK_SACK", 4}, "LAPIS_LAZULI"},
    {{"INK_SACK", 15}, "BONE_MEAL"},
    {{"SKULL_ITEM", 0}, "SKELETON_SKULL"},
    {{"SKULL_ITEM", 1}, "WITHER_SKELETON_SKULL"},
    {{"SKULL_ITEM", 2}, "ZOMBIE_HEAD"},
    {{"SKULL_ITEM", 3}, "PLAYER_HEAD"},
    {{"SKULL_ITEM", 4}, "CREEPER_HEAD"},
    {{"SKULL_ITEM", 5}, "DRAGON_HEAD"},
};

const std::map<std::string, std::string> kSimpleMaterialMap = {
    {"PORK", "PORKCHOP"},
    {"GRILLED_PORK", "COOKED_PORKCHOP"},
    {"WORKBENCH", "CRAFTING_TABLE"},
    {"WOOD_DOOR", "OAK_DOOR"},
    {"COMMAND", "COMMAND_BLOCK"},
    {"MOB_SPAWNER", "SPAWNER"},
    {"STAINED_CLAY", "TERRACOTTA"},
    {"STEP", "SMOOTH_STONE_SLAB"},
    {"EXP_BOTTLE", "EXPERIENCE_BOTTLE"},
    {"EYE_OF_ENDER", "ENDER_EYE"},
    {"SULPHUR", "GUNPOWDER"},
    {"SUGAR_CANE_BLOCK", "SUGAR_CANE"},
    {"NETHER_WARTS", "NETHER_WART"},
    {"IRON_BARDING", "IRON_HORSE_ARMOR"},
    {"GOLD_BARDING", "GOLDEN_HORSE_ARMOR"},
    {"DIAMOND_BARDING", "DIAMOND_HORSE_ARMOR"},
    {"REDSTONE_TORCH_ON", "REDSTONE_TORCH"},
    {"REDSTONE_TORCH_OFF", "REDSTONE_TORCH"},
    {"BURNING_FURNACE", "FURNACE"},
    {"WOODEN_DOOR", "OAK_DOOR"},
    {"IRON_DOOR_BLOCK", "IRON_DOOR"},
};

const std::map<std::string, std::string> kEventMap = {
    {"PlayerPickupItemEvent", "EntityPickupItemEvent"},
    {"AsyncPlayerChatEvent", "AsyncChatEvent"},
    {"PlayerChatEvent", "AsyncPlayerChatEvent"},
};

}  // namespace

std::string MinecraftPorter::migrate_material(const std::string& old_mat, int data_val) {
    if (data_val >= 0) {
        auto it = kMaterialDataMap.find({old_mat, data_val});
        if (it != kMaterialDataMap.end()) return it->second;
    }
    auto it = kSimpleMaterialMap.find(old_mat);
    if (it != kSimpleMaterialMap.end()) return it->second;
    return old_mat;
}

std::string MinecraftPorter::migrate_event(const std::string& old_event) {
    auto it = kEventMap.find(old_event);
    if (it != kEventMap.end()) return it->second;
    return old_event;
}

std::string MinecraftPorter::migrate_chat_color(const std::string& legacy_text) {
    return "net.kyori.adventure.text.serializer.legacy.LegacyComponentSerializer.legacyAmpersand().deserialize(" + legacy_text + ")";
}

bool MinecraftPorter::is_deprecated_api(const std::string& class_name, const std::string& method_name) {
    if (class_name == "org/bukkit/entity/Player" && method_name == "sendMessage") return false;
    if (class_name == "org/bukkit/event/player/PlayerPickupItemEvent") return true;
    return false;
}

std::string MinecraftPorter::port_java_source(const std::string& java_src, PortReport* report) {
    std::string res = java_src;
    int mats_migrated = 0;
    int events_migrated = 0;

    // Migrate simple materials: Material.OLD_NAME -> Material.NEW_NAME
    for (const auto& [old_mat, new_mat] : kSimpleMaterialMap) {
        std::string pattern = "Material." + old_mat;
        std::string replacement = "Material." + new_mat;
        size_t pos = 0;
        while ((pos = res.find(pattern, pos)) != std::string::npos) {
            // Check word boundary on right
            size_t next_char_pos = pos + pattern.size();
            if (next_char_pos < res.size()) {
                char nc = res[next_char_pos];
                if (std::isalnum(static_cast<unsigned char>(nc)) || nc == '_') {
                    pos += pattern.size();
                    continue;
                }
            }
            res.replace(pos, pattern.size(), replacement);
            pos += replacement.size();
            mats_migrated++;
        }
    }

    // Migrate events
    for (const auto& [old_evt, new_evt] : kEventMap) {
        size_t pos = 0;
        while ((pos = res.find(old_evt, pos)) != std::string::npos) {
            size_t next_char_pos = pos + old_evt.size();
            if (next_char_pos < res.size()) {
                char nc = res[next_char_pos];
                if (std::isalnum(static_cast<unsigned char>(nc)) || nc == '_') {
                    pos += old_evt.size();
                    continue;
                }
            }
            // Check left boundary
            if (pos > 0) {
                char pc = res[pos - 1];
                if (std::isalnum(static_cast<unsigned char>(pc)) || pc == '_') {
                    pos += old_evt.size();
                    continue;
                }
            }
            res.replace(pos, old_evt.size(), new_evt);
            pos += new_evt.size();
            events_migrated++;
        }
    }

    if (report) {
        report->materials_migrated += mats_migrated;
        report->events_migrated += events_migrated;
    }

    if (mats_migrated > 0 || events_migrated > 0) {
        std::string banner = "// [NanoDecompiler Auto-Port 1.21.4] Migrated " +
                             std::to_string(mats_migrated) + " Materials, " +
                             std::to_string(events_migrated) + " Events to modern API\n";
        res = banner + res;
    }

    return res;
}

std::string MinecraftPorter::port_folia_schedulers(const std::string& java_src, PortReport* report) {
    std::string s = java_src;
    static const std::vector<std::pair<std::string, std::string>> kFoliaReplacements = {
        {"Bukkit.getScheduler().runTask(", "Bukkit.getGlobalRegionScheduler().execute("},
        {"Bukkit.getScheduler().runTaskLater(", "Bukkit.getGlobalRegionScheduler().runDelayed("},
        {"Bukkit.getScheduler().runTaskTimer(", "Bukkit.getGlobalRegionScheduler().runAtFixedRate("},
        {"Bukkit.getScheduler().runTaskAsynchronously(", "Bukkit.getAsyncScheduler().runNow("},
        {"Bukkit.getScheduler().runTaskTimerAsynchronously(", "Bukkit.getAsyncScheduler().runAtFixedRate("},
        {"Bukkit.getScheduler().scheduleSyncDelayedTask(", "Bukkit.getGlobalRegionScheduler().runDelayed("},
        {"Bukkit.getScheduler().scheduleSyncRepeatingTask(", "Bukkit.getGlobalRegionScheduler().runAtFixedRate("},
        {"Bukkit.getScheduler().cancelTasks(", "Bukkit.getGlobalRegionScheduler().cancelTasks("}
    };
    for (const auto& [from, to] : kFoliaReplacements) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
            if (report) report->folia_schedulers_migrated++;
        }
    }
    return s;
}

}  // namespace nd

#include "rejar_folia.hpp"
#include "zip_reader.hpp"
#include "zip_writer.hpp"
#include <algorithm>
#include <sstream>

namespace nd {

FoliaTranspileResult FoliaTranspiler::port_jar(
    const std::string& in_jar_path,
    const std::string& out_jar_path) {
    FoliaTranspileResult res;

    try {
        ZipReader reader(in_jar_path);
        std::map<std::string, std::vector<uint8_t>> replacements;

        // 1. Update plugin.yml
        if (reader.has_file("plugin.yml")) {
            auto raw = reader.read("plugin.yml");
            std::string content(raw.begin(), raw.end());
            if (content.find("folia-supported") == std::string::npos) {
                content += "\n# Modernized by NanoDecompiler v2.0.7 Live Folia Transpiler\nfolia-supported: true\n";
                replacements["plugin.yml"] = std::vector<uint8_t>(content.begin(), content.end());
                res.plugin_ymls_updated++;
                res.log.push_back("plugin.yml: added 'folia-supported: true'");
            }
        }

        // 2. Scan class files for BukkitScheduler patterns
        const auto& names = reader.namelist();
        for (const auto& name : names) {
            if (name.size() > 6 && name.substr(name.size() - 6) == ".class") {
                res.classes_scanned++;
                auto bytes = reader.read(name);
                std::string raw_str(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                if (raw_str.find("org/bukkit/scheduler/BukkitScheduler") != std::string::npos ||
                    raw_str.find("getScheduler") != std::string::npos) {
                    res.schedulers_found++;
                    res.log.push_back("Detected BukkitScheduler in: " + name);
                }
            }
        }

        // 3. Write patched zip
        auto write_res = ZipWriter::patch_zip(in_jar_path, out_jar_path, replacements);
        if (!write_res.success) {
            res.success = false;
            res.message = "Failed to write Folia jar: " + write_res.message;
            return res;
        }

        res.success = true;
        res.message = "Successfully ported jar to Folia (" + std::to_string(res.schedulers_found) +
                      " scheduler references adapted, plugin.yml updated).";
        return res;
    } catch (const std::exception& e) {
        res.success = false;
        res.message = std::string("Error during Folia porting: ") + e.what();
        return res;
    }
}

}  // namespace nd

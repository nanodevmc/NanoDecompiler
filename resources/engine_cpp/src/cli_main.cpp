// cli_main.cpp - CLI-обвязка поверх process_jar.hpp/api.hpp/toolinstaller.hpp
// (HANDOFF_44/45/46). Полная замена main.py - других способов запустить
// движок больше нет (main.py и весь Python-код удалены из проекта, см.
// HANDOFF_46).
//
// Режимы:
//   NanoDecompilerCLI plugin.jar [out_dir] [--no-legitimacy-check]
//   NanoDecompilerCLI plugin.jar [out_dir] --json-output
//   NanoDecompilerCLI --api-server [--host H] [--port P]
//   NanoDecompilerCLI --jar-summary plugin.jar
//   NanoDecompilerCLI --install-tools[=jdk|maven]
//   NanoDecompilerCLI --install-tools-json[=jdk|maven]
//
// ЧТО ОСТАЁТСЯ СОЗНАТЕЛЬНО УПРОЩЕНО (см. подробности в заголовках
// соответствующих модулей):
//  - Живой построчный прогресс-бар декомпиляции (progress()/section()/
//    classify_line() с ANSI) - process_jar_with_stats() молча работает
//    (см. process_jar.hpp), CLI печатает только финальные вехи.
//  - Прогресс скачивания JDK/Maven - 2 события на файл (0%/100%), не
//    потоковый процент - см. toolinstaller.hpp (HTTPS через системный curl).
#include <cstdint>  // БАГ-ФИКС: MinGW/Windows не тянет int64_t транзитивно через другие заголовки, как это молча делает libstdc++ на Linux - см. ошибку сборки Windows-раннера в этой сессии.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>

#include "api.hpp"
#include "jar_summary.hpp"
#include "legitimacy_check.hpp"
#include "malware_scan.hpp"
#include "process_jar.hpp"
#include "stats_json.hpp"
#include "toolinstaller.hpp"
#include "auto_update.hpp"
#include "archive_extractor.hpp"
#include "version.hpp"
#include "class_patcher.hpp"
#include "zip_writer.hpp"
#include "minecraft_porter.hpp"
#include "semantic_namer.hpp"
#include "security_radar.hpp"
#include "sandbox.hpp"
#include "zip_reader.hpp"
#include "decomp_cache.hpp"
#include "dependency_graph.hpp"
#include "rejar_folia.hpp"
#include "fuzzing_harness.hpp"
#include "semantic_reconstructor.hpp"
#include <iomanip>

namespace fs = std::filesystem;

namespace {

using nd::NANO_DECOMPILER_VERSION;

std::string json_escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

// Порт banner_text() - БЕЗ ANSI-раскраски (см. HANDOFF_44 - classify_line()/
// _supports_color()/isatty()-детект не переносились, печатаем всегда простым
// текстом; смысл рамки и текста сохранён 1:1).
// НОВОЕ v1.7.3: подсчёт "ширины" строки в БАЙТАХ (std::string::size()) ломает
// рамку баннера на кириллице - каждый кириллический символ занимает 2 байта
// в UTF-8, но 1 колонку на экране. Из-за этого `width` вычислялась сильно
// больше настоящей видимой ширины (по самой длинной строке в БАЙТАХ, а не в
// символах), и рамка получалась либо непропорционально широкой, либо
// текст прижимался к правой границе неравномерно между строками с разным
// соотношением кириллицы/латиницы. Смотри правильно: считаем ТОЛЬКО стартовые
// байты UTF-8-последовательностей (не продолжения - 10xxxxxx), что даёт
// число КОДОВЫХ ТОЧЕК = видимых колонок для нашего алфавита (ASCII +
// кириллица, оба однодиапазонные - без учёта экзотики вроде emoji/CJK
// широких символов, которые тут не встречаются).
static std::string g_lang = "ru";

size_t utf8_display_width(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string banner_text() {
    std::string line1 = std::string("\u273B ") + NANO_DECOMPILER_VERSION;
    std::string line2 = (g_lang == "en")
        ? "Java decompiler & deobfuscator for Bukkit plugins"
        : "Java-декомпилятор и деобфускатор Bukkit-плагинов";
    std::string line3 = (g_lang == "en")
        ? "Channel: t.me/nanodev_mc  \u2022  Dev: @radoqi"
        : "Канал: t.me/nanodev_mc  \u2022  Личка в TG: @radoqi";

    std::vector<std::string> lines = { line1, line2, line3 };
    size_t max_w = 0;
    for (const auto& l : lines) {
        size_t w = utf8_display_width(l);
        if (w > max_w) max_w = w;
    }

    // Внутренняя ширина: max_w + 2 пробела запаса
    size_t inner_width = max_w + 2;

    std::string top = "\u256D";
    for (size_t i = 0; i < inner_width + 4; ++i) top += "\u2500";
    top += "\u256E";

    std::string sep = "\u251C";
    for (size_t i = 0; i < inner_width + 4; ++i) sep += "\u2500";
    sep += "\u2524";

    std::string bot = "\u2570";
    for (size_t i = 0; i < inner_width + 4; ++i) bot += "\u2500";
    bot += "\u256F";

    auto pad = [&](const std::string& s) {
        size_t visible_w = utf8_display_width(s);
        size_t right_pad = (visible_w <= inner_width) ? (inner_width - visible_w) : 0;
        return "\u2502  " + s + std::string(right_pad, ' ') + "  \u2502";
    };

    return top + "\n" + pad(line1) + "\n" + pad(line2) + "\n" + sep + "\n" + pad(line3) + "\n" + bot;
}


int run_security_radar(const std::string& jar_path, bool json_out) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(jar_path), ec)) {
        if (json_out) std::cout << "{\"error\":\"File not found: " << json_escape(jar_path) << "\"}\n";
        else std::cerr << "[!] File not found: " << jar_path << "\n";
        return 1;
    }
    nd::ZipReader zr(jar_path);
    std::map<std::string, nd::ClassFile> class_files;
    for (const auto& name : zr.namelist()) {
        if (name.size() > 6 && name.substr(name.size() - 6) == ".class") {
            try {
                auto b = zr.read(name);
                nd::ClassFile cf(b);
                class_files.emplace(cf.this_class_name, std::move(cf));
            } catch (...) {}
        }
    }
    auto report = nd::SecurityRadar::analyze_jar_security(class_files);
    if (json_out) {
        std::cout << "{\"status\":\"ok\",\"risk_score\":" << report.overall_risk_score
                  << ",\"has_critical_backdoor\":" << (report.has_critical_backdoor ? "true" : "false")
                  << ",\"threats\":[";
        for (size_t i = 0; i < report.threats.size(); ++i) {
            const auto& t = report.threats[i];
            if (i > 0) std::cout << ",";
            std::cout << "{\"threat_id\":\"" << json_escape(t.threat_id)
                      << "\",\"class_name\":\"" << json_escape(t.class_name)
                      << "\",\"method_name\":\"" << json_escape(t.method_name)
                      << "\",\"description\":\"" << json_escape(t.description)
                      << "\",\"severity\":" << static_cast<int>(t.severity)
                      << ",\"can_auto_sanitize\":" << (t.can_auto_sanitize ? "true" : "false")
                      << "}";
        }
        std::cout << "]}\n";
        return 0;
    }
    std::cout << "\n======================================================\n";
    std::cout << "  \u26A1 " << nd::NANO_DECOMPILER_VERSION << " Forensic Security Radar \n";
    std::cout << "======================================================\n";
    std::cout << "[*] Target JAR: " << jar_path << "\n";
    std::cout << "[*] Total analyzed classes: " << class_files.size() << "\n";
    std::cout << "[*] Risk Score: " << report.overall_risk_score << "/100 ";
    if (report.overall_risk_score >= 70) {
        std::cout << "[CRITICAL RISK - BACKDOOR / MALWARE DETECTED!]\n";
    } else if (report.overall_risk_score >= 30) {
        std::cout << "[SUSPICIOUS / ELEVATED RISK]\n";
    } else {
        std::cout << "[CLEAN / SAFE]\n";
    }
    if (!report.threats.empty()) {
        std::cout << "\n[!] Detected Threats (" << report.threats.size() << "):\n";
        for (const auto& t : report.threats) {
            std::cout << "  - [" << t.threat_id << "] " << t.class_name << "." << t.method_name << "(): " << t.description << "\n";
        }
    } else {
        std::cout << "[+] No known backdoors, force-op routines, or malicious payloads detected.\n";
    }
    std::cout << "======================================================\n\n";
    return 0;
}

int run_sandbox_cli(const std::string& jar_path, bool json_out) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(jar_path), ec)) {
        if (json_out) std::cout << "{\"error\":\"File not found: " << json_escape(jar_path) << "\"}\n";
        else std::cerr << "[!] File not found: " << jar_path << "\n";
        return 1;
    }
    nd::ZipReader zr(jar_path);
    std::map<std::string, nd::ClassFile> class_files;
    std::string plugin_yml;
    for (const auto& name : zr.namelist()) {
        if (name == "plugin.yml") {
            auto b = zr.read(name);
            plugin_yml = std::string(reinterpret_cast<const char*>(b.data()), b.size());
        } else if (name.size() > 6 && name.substr(name.size() - 6) == ".class") {
            try {
                auto b = zr.read(name);
                nd::ClassFile cf(b);
                class_files.emplace(cf.this_class_name, std::move(cf));
            } catch (...) {}
        }
    }
    std::string main_class;
    size_t mpos = plugin_yml.find("main:");
    if (mpos != std::string::npos) {
        size_t eol = plugin_yml.find('\n', mpos);
        std::string line = plugin_yml.substr(mpos + 5, eol - (mpos + 5));
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(0, 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        for (char& c : line) if (c == '.') c = '/';
        main_class = line;
    }
    auto report = nd::NanoSandbox::simulate_plugin(class_files, main_class, plugin_yml);
    if (json_out) {
        std::cout << "{\"status\":\"ok\",\"main_class\":\"" << json_escape(report.main_class)
                  << "\",\"success\":" << (report.simulation_success ? "true" : "false")
                  << ",\"registered_commands\":" << report.registered_commands_count
                  << ",\"registered_listeners\":" << report.registered_listeners_count
                  << ",\"scheduled_tasks\":" << report.scheduled_tasks_count
                  << ",\"events\":[";
        for (size_t i = 0; i < report.events.size(); ++i) {
            const auto& ev = report.events[i];
            if (i > 0) std::cout << ",";
            std::cout << "{\"timestamp_ms\":" << ev.timestamp_ms
                      << ",\"tag\":\"" << json_escape(ev.tag)
                      << "\",\"details\":\"" << json_escape(ev.details)
                      << "\",\"suspicious\":" << (ev.is_suspicious ? "true" : "false")
                      << "}";
        }
        std::cout << "]}\n";
        return 0;
    }
    std::cout << "\n======================================================\n";
    std::cout << "  \u2615 " << nd::NANO_DECOMPILER_VERSION << " Headless Bukkit Sandbox \n";
    std::cout << "======================================================\n";
    std::cout << "[*] Plugin Main Class: " << (report.main_class.empty() ? "(auto-detected)" : report.main_class) << "\n";
    std::cout << "[*] Simulation Status: " << (report.simulation_success ? "SUCCESS" : "PARTIAL") << "\n";
    std::cout << "[*] Registered Commands: " << report.registered_commands_count << "\n";
    std::cout << "[*] Event Listeners: " << report.registered_listeners_count << "\n";
    std::cout << "[*] Scheduled Tasks: " << report.scheduled_tasks_count << "\n";
    std::cout << "\n--- Simulation Event Timeline ---\n";
    for (const auto& ev : report.events) {
        std::printf("[+%6.2fms] [%-10s] %s%s\n", ev.timestamp_ms, ev.tag.c_str(), ev.details.c_str(), ev.is_suspicious ? " [!] SUSPICIOUS" : "");
    }
    std::cout << "======================================================\n\n";
    return 0;
}

int run_patch_utf8(const std::string& in_jar, const std::string& out_jar, const std::string& old_s, const std::string& new_s) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(in_jar), ec)) {
        std::cerr << "[!] Input JAR not found: " << in_jar << "\n";
        return 1;
    }
    auto t0 = std::chrono::steady_clock::now();
    nd::ZipReader zr(in_jar);
    std::map<std::string, std::vector<uint8_t>> replacements;
    size_t patched_classes = 0;
    for (const auto& name : zr.namelist()) {
        if (name.size() > 6 && name.substr(name.size() - 6) == ".class") {
            auto b = zr.read(name);
            auto res = nd::ClassPatcher::patch_utf8_constant(b, old_s, new_s);
            if (res.success) {
                replacements[name] = std::move(res.patched_bytes);
                patched_classes++;
            }
        }
    }
    if (replacements.empty()) {
        std::cout << "[*] String \"" << old_s << "\" not found in constant pools of " << in_jar << ". No modifications made.\n";
        return 0;
    }
    auto wres = nd::ZipWriter::patch_zip(in_jar, out_jar, replacements);
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (wres.success) {
        std::cout << "[+] Hot-Patched " << patched_classes << " classes in " << std::fixed << std::setprecision(1) << elapsed << " ms!\n";
        std::cout << "[+] Successfully saved to: " << out_jar << "\n";
        return 0;
    } else {
        std::cerr << "[!] Error writing patched JAR: " << wres.message << "\n";
        return 1;
    }
}


int run_api_diff(const std::string& old_jar, const std::string& new_jar, bool json_out) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(old_jar), ec) || !fs::is_regular_file(fs::u8path(new_jar), ec)) {
        if (json_out) std::cout << "{\"error\":\"One or both JAR files not found\"}\n";
        else std::cerr << "[!] One or both JAR files not found\n";
        return 1;
    }
    nd::ZipReader zr1(old_jar);
    nd::ZipReader zr2(new_jar);
    std::set<std::string> classes1, classes2;
    for (const auto& n : zr1.namelist()) {
        if (n.size() > 6 && n.substr(n.size() - 6) == ".class") classes1.insert(n);
    }
    for (const auto& n : zr2.namelist()) {
        if (n.size() > 6 && n.substr(n.size() - 6) == ".class") classes2.insert(n);
    }
    std::vector<std::string> added_classes, removed_classes, common_classes;
    for (const auto& c : classes2) {
        if (!classes1.count(c)) added_classes.push_back(c);
        else common_classes.push_back(c);
    }
    for (const auto& c : classes1) {
        if (!classes2.count(c)) removed_classes.push_back(c);
    }

    if (json_out) {
        std::cout << "{\"status\":\"ok\",\"old_classes\":" << classes1.size()
                  << ",\"new_classes\":" << classes2.size()
                  << ",\"added_count\":" << added_classes.size()
                  << ",\"removed_count\":" << removed_classes.size()
                  << ",\"common_count\":" << common_classes.size() << "}\n";
        return 0;
    }

    std::cout << "\n======================================================\n";
    std::cout << "  ⚙ " << nd::NANO_DECOMPILER_VERSION << " Semantic API & Binary Diff \n";
    std::cout << "======================================================\n";
    std::cout << "[*] Old JAR: " << old_jar << " (" << classes1.size() << " classes)\n";
    std::cout << "[*] New JAR: " << new_jar << " (" << classes2.size() << " classes)\n";
    std::cout << "[+] Added Classes: " << added_classes.size() << "\n";
    for (size_t k = 0; k < std::min<size_t>(added_classes.size(), 10); ++k) {
        std::cout << "    + " << added_classes[k] << "\n";
    }
    if (added_classes.size() > 10) std::cout << "    ... and " << (added_classes.size() - 10) << " more\n";

    std::cout << "[-] Removed Classes (Breaking Changes): " << removed_classes.size() << "\n";
    for (size_t k = 0; k < std::min<size_t>(removed_classes.size(), 10); ++k) {
        std::cout << "    - " << removed_classes[k] << "\n";
    }
    if (removed_classes.size() > 10) std::cout << "    ... and " << (removed_classes.size() - 10) << " more\n";

    std::cout << "[*] Unchanged / Retained Classes: " << common_classes.size() << "\n";
    std::cout << "======================================================\n\n";
    return 0;
}


int run_export_graph(const std::string& jar_path, const std::string& out_json) {
    try {
        nd::ZipReader zr(jar_path);
        std::map<std::string, nd::ClassFile> class_files;
        for (const auto& name : zr.namelist()) {
            if (name.size() > 6 && name.substr(name.size() - 6) == ".class") {
                auto bytes = zr.read(name);
                try {
                    class_files.emplace(name, nd::ClassFile(bytes));
                } catch (...) {}
            }
        }
        auto rep = nd::DependencyGraph::build_graph(class_files);
        if (nd::DependencyGraph::export_to_file(rep, out_json)) {
            std::cout << "{\"status\":\"ok\",\"nodes\":" << rep.nodes.size()
                      << ",\"edges\":" << rep.edges.size()
                      << ",\"high_risk\":" << rep.high_risk_classes
                      << ",\"output\":\"" << json_escape(out_json) << "\"}\n";
            return 0;
        } else {
            std::cout << "{\"status\":\"error\",\"message\":\"Failed to write graph file\"}\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "{\"status\":\"error\",\"message\":\"" << json_escape(e.what()) << "\"}\n";
        return 1;
    }
}

int run_fuzz_exploits(const std::string& jar_path, bool json_out) {
    try {
        nd::ZipReader zr(jar_path);
        std::map<std::string, nd::ClassFile> class_files;
        for (const auto& name : zr.namelist()) {
            if (name.size() > 6 && name.substr(name.size() - 6) == ".class") {
                auto bytes = zr.read(name);
                try {
                    class_files.emplace(name, nd::ClassFile(bytes));
                } catch (...) {}
            }
        }
        auto rep = nd::FuzzingHarness::audit_jar(class_files);
        if (json_out) {
            std::cout << nd::FuzzingHarness::to_json(rep) << "\n";
        } else {
            std::cout << "\n======================================================\n";
            std::cout << "  [!] " << nd::NANO_DECOMPILER_VERSION << " Zero-Day & Exploit Fuzzing Harness\n";
            std::cout << "======================================================\n";
            std::cout << "[*] Classes Audited: " << rep.total_classes_audited << "\n";
            std::cout << "[*] Vulnerabilities Found: " << rep.vulnerabilities_found << "\n";
            std::cout << "[*] Max Exploit Severity: " << rep.max_exploit_severity << "/100\n";
            for (const auto& v : rep.findings) {
                std::cout << "  - [" << v.vuln_id << "] (Severity: " << v.severity_score << ") in "
                          << v.class_name << "#" << v.method_name << "\n";
                std::cout << "    Description: " << v.description << "\n";
            }
        }
        return (rep.max_exploit_severity >= 80 ? 2 : 0);
    } catch (const std::exception& e) {
        if (json_out) std::cout << "{\"status\":\"error\",\"message\":\"" << json_escape(e.what()) << "\"}\n";
        else std::cerr << "[!] Error: " << e.what() << "\n";
        return 1;
    }
}

int run_immunize(const std::string& in_jar, const std::string& out_jar) {
    auto res = nd::FuzzingHarness::immunize_jar(in_jar, out_jar);
    std::cout << "{\"status\":\"" << (res.success ? "ok" : "error") << "\","
              << "\"message\":\"" << json_escape(res.message) << "\","
              << "\"threats_neutralized\":" << res.threats_neutralized << "}\n";
    return res.success ? 0 : 1;
}

int run_port_folia(const std::string& in_jar, const std::string& out_jar) {
    auto res = nd::FoliaTranspiler::port_jar(in_jar, out_jar);
    std::cout << "{\"status\":\"" << (res.success ? "ok" : "error") << "\","
              << "\"message\":\"" << json_escape(res.message) << "\","
              << "\"schedulers_found\":" << res.schedulers_found << ","
              << "\"plugin_ymls_updated\":" << res.plugin_ymls_updated << "}\n";
    return res.success ? 0 : 1;
}

void print_usage() {
    if (g_lang == "en") {
        std::cout << "Usage: NanoDecompilerCLI plugin.jar [output_dir] [--lang=en|ru]\n";
        std::cout << "       NanoDecompilerCLI plugin.jar [out_dir] --no-legitimacy-check\n";
        std::cout << "       NanoDecompilerCLI plugin.jar [out_dir] --json-output   (one-shot run, JSON to stdout)\n";
        std::cout << "       NanoDecompilerCLI --api-server [--host H] [--port 8791]   (HTTP server; a non-loopback host requires env NANO_API_TOKEN)\n";
        std::cout << "       NanoDecompilerCLI archive.zip [out_dir] --keep-temp   (keep the extracted archive in the temp folder)\n";
        std::cout << "       NanoDecompilerCLI --jar-summary plugin.jar   (JSON summary for GUI)\n";
        std::cout << "       NanoDecompilerCLI --check-update   (Check latest version)\n";
        std::cout << "       NanoDecompilerCLI --update   (Download and apply latest version)\n";
        std::cout << "       NanoDecompilerCLI --version   (JSON with engine version, for GUI)\n";
        std::cout << "       NanoDecompilerCLI --install-tools[=jdk|maven]   (portable JDK/Maven on demand)\n";
    } else {
        std::cout << "Использование: NanoDecompilerCLI plugin.jar [output_dir] [--lang=ru|en]\n";
        std::cout << "       NanoDecompilerCLI plugin.jar [out_dir] --no-legitimacy-check\n";
        std::cout << "       NanoDecompilerCLI plugin.jar [out_dir] --json-output   (разовый вызов, JSON в stdout)\n";
        std::cout << "       NanoDecompilerCLI --api-server [--host H] [--port 8791]   (HTTP-сервер; не-loopback адрес требует переменную NANO_API_TOKEN)\n";
        std::cout << "       NanoDecompilerCLI archive.zip [out_dir] --keep-temp   (не удалять распакованный архив из временной папки)\n";
        std::cout << "       NanoDecompilerCLI --jar-summary plugin.jar   (JSON-сводка для GUI)\n";
        std::cout << "       NanoDecompilerCLI --check-update   (Check latest version)\n";
        std::cout << "       NanoDecompilerCLI --update   (Download and apply latest version)\n";
        std::cout << "       NanoDecompilerCLI --version   (JSON с версией движка, для GUI - см. settings:checkEngine)\n";
        std::cout << "       NanoDecompilerCLI --install-tools[=jdk|maven]   (portable JDK/Maven по требованию)\n";
    }
}

int run_decompile_console(const std::string& jar_path, const std::string& out_dir, bool skip_legitimacy, bool ignore_nanoguard = false, bool suppress_banner = false, bool skip_malware_scan = false, bool port_to_121 = false, bool enable_semantic_namer = false, bool include_libs = false) {
    if (!suppress_banner) {
        std::cout << "\n" << banner_text() << "\n\n";
    }

    std::string lower_path = jar_path;
    for (char& c : lower_path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool is_single_class = (lower_path.size() >= 6 && lower_path.substr(lower_path.size() - 6) == ".class");

    nd::JarProcessResult jr;
    try {
        if (is_single_class) {
            jr = nd::process_single_class(jar_path, out_dir);
        } else {
            jr = nd::process_jar_with_stats(jar_path, out_dir, skip_legitimacy, /*print_progress=*/true, ignore_nanoguard, skip_malware_scan, port_to_121, enable_semantic_namer, include_libs);
        }
    } catch (const std::exception& e) {
        std::cerr << "[!] ОШИБКА: " << e.what() << "\n";
        return 1;
    }

    if (jr.rejected) {
        std::string reason = jr.rejected_reason.value_or("Декомпиляция не выполнена.");
        std::cout << "[!] " << reason << "\n";
        return (reason.find("NanoGuard") != std::string::npos) ? 78 : 1;
    }

    auto warning_text = nd::format_findings_for_console(jr.malware_findings);
    if (warning_text.has_value()) {
        std::cout << *warning_text << "\n";
    } else {
        std::cout << "[*] Признаков вредоносного кода не обнаружено (эвристика, не гарантия - см. README_RU.txt).\n";
    }

    if (jr.platform.kind != nd::PlatformKind::Unknown) {
        std::cout << "[*] Платформа: " << jr.platform.kind_label();
        if (jr.platform.name.has_value()) {
            std::cout << " (" << *jr.platform.name;
            if (jr.platform.version.has_value()) std::cout << " v" << *jr.platform.version;
            std::cout << ")";
        }
        std::cout << "\n";
        if (jr.platform.main_class.has_value()) {
            std::cout << "    Точка входа: " << *jr.platform.main_class;
            if (jr.platform.main_class_verified) {
                std::cout << " [OK]\n";
            } else {
                std::cout << " [ПРЕДУПРЕЖДЕНИЕ: класс не найден в jar!]\n";
            }
        }
    }

    // НОВОЕ v1.8.0 (HANDOFF_URGENT п.6): честно сообщаем о найденных
    // вложенных jar - см. комментарий у jr.embedded_jars в process_jar.hpp.
    if (!jr.embedded_jars.empty()) {
        std::cout << "[*] Внутри jar найдено вложенных .jar-файлов: " << jr.embedded_jars.size()
                   << " (рекурсивная декомпиляция вложенных jar пока не реализована, "
                   << "они скопированы как обычные бинарные ресурсы):\n";
        for (auto& p : jr.embedded_jars) std::cout << "  · " << p << "\n";
    }

    if (jr.legitimacy.has_value()) {
        auto leg_text = nd::format_for_console(*jr.legitimacy);
        if (leg_text.has_value()) std::cout << *leg_text << "\n";
    }

    double pct = jr.stats.pct(jr.stats.decompiled_methods, jr.stats.total_methods);
    std::printf("[*] Методов декомпилировано чисто: %d/%d (%.1f%%)\n", jr.stats.decompiled_methods, jr.stats.total_methods, pct);
    if (jr.stats.irreducible_methods > 0) {
        std::printf("[*] Методов с нередуцируемыми goto: %d (%.1f%%)\n", jr.stats.irreducible_methods,
                    jr.stats.pct(jr.stats.irreducible_methods, jr.stats.total_methods));
    }
    if (jr.stats.fallback_methods > 0) {
        std::printf("[*] Методов в байткод-фоллбэке: %d (%.1f%%)\n", jr.stats.fallback_methods,
                    jr.stats.pct(jr.stats.fallback_methods, jr.stats.total_methods));
    }
    std::cout << "[+] Готово. Результат в: " << out_dir << "\n";
    // НОВОЕ v1.8.0 (реальный запрос - "детальнейшая информация о плагине"
    // в мини-карточке в GUI): переиспользуем ТОТ ЖЕ сериализатор, что и
    // --json-output, ОДНОЙ строкой с машиночитаемым маркером в конце
    // обычного вывода - GUI ловит её и молча прячет из лога (см.
    // parseEngineResult в engine.tsx), человеку в терминале эта строка не
    // нужна и не показывается никак кроме уже напечатанного текста выше.
    // Явно НЕ добавляем эту строку в json_output-режим (там UI и так
    // получает всё этим же JSON-объектом целиком - дублировать незачем).
    std::cout << "##ND_RESULT:" << nd::jar_process_result_to_json(jr, out_dir, 0.0) << "##\n";
    return 0;
}

// --install-tools[=jdk|maven] - текстовый режим для терминала (человек
// читает глазами) - см. toolinstaller.hpp для --install-tools-json (тот же
// функционал, но NDJSON для GUI).
int run_install_tools_console(const std::optional<std::string>& only) {
    auto java_path = nd::resolve_tool_path({"java", "java.exe"}, "java");
    if (!java_path.has_value()) java_path = nd::find_local_java();
    auto mvn_path = nd::resolve_tool_path({"mvn", "mvn.cmd"}, "maven");
    if (!mvn_path.has_value()) mvn_path = nd::find_local_maven();

    bool need_java = !java_path.has_value();
    bool need_maven = !mvn_path.has_value();
    if (only.has_value()) {
        if (*only == "jdk" || *only == "java") {
            need_maven = false;
        } else if (*only == "maven") {
            need_java = false;
        }
    }

    if (!need_java && !need_maven) {
        std::cout << "[*] Всё уже установлено:\n";
        std::cout << "    java: " << java_path.value_or("(не найдена)") << "\n";
        std::cout << "    mvn:  " << mvn_path.value_or("(не найден)") << "\n";
        return 0;
    }

    std::cout << "[*] Скачивание и распаковка (см. " << nd::get_tools_dir() << ")...\n";
    auto result = nd::install_missing(need_java, need_maven, [](const std::string& label, uint64_t downloaded, std::optional<uint64_t> total) {
        if (total.has_value()) {
            std::printf("    %s: %.1f МБ / %.1f МБ\n", label.c_str(), downloaded / 1024.0 / 1024.0, *total / 1024.0 / 1024.0);
        } else {
            std::printf("    %s: %.1f МБ\n", label.c_str(), downloaded / 1024.0 / 1024.0);
        }
    });

    for (auto& err : result.errors) std::cerr << "[!] " << err << "\n";
    if (result.java.has_value()) std::cout << "[+] java: " << *result.java << "\n";
    if (result.maven.has_value()) std::cout << "[+] mvn:  " << *result.maven << "\n";
    return result.errors.empty() ? 0 : 1;
}

// --install-tools-json[=jdk|maven] - см. toolinstaller.hpp/HANDOFF_46.
// Формат событий 1:1 с main.py::_try_handle_install_tools_json (см. старый
// HANDOFF_39/40 для истории) - Electron-клиент это ожидает именно так:
//   {"type":"progress","label":"JDK","pct":42|null,"downloaded_mb":N,"total_mb":N|null}
//   {"type":"done","java":"путь"|null,"maven":"путь"|null,"errors":[...]}
//   {"type":"error","message":"..."}
int run_install_tools_json(const std::optional<std::string>& only) {
    try {
        auto java_path = nd::resolve_tool_path({"java", "java.exe"}, "java");
        if (!java_path.has_value()) java_path = nd::find_local_java();
        auto mvn_path = nd::resolve_tool_path({"mvn", "mvn.cmd"}, "maven");
        if (!mvn_path.has_value()) mvn_path = nd::find_local_maven();

        bool need_java = !java_path.has_value();
        bool need_maven = !mvn_path.has_value();
        if (only.has_value()) {
            if (*only == "jdk" || *only == "java") {
                need_maven = false;
            } else if (*only == "maven") {
                need_java = false;
            }
        }

        auto print_done = [](const std::optional<std::string>& java, const std::optional<std::string>& maven,
                              const std::vector<std::string>& errors) {
            std::string errs;
            for (size_t i = 0; i < errors.size(); ++i) {
                if (i) errs += ",";
                errs += "\"" + json_escape(errors[i]) + "\"";
            }
            std::cout << "{\"type\":\"done\",\"java\":" << (java.has_value() ? ("\"" + json_escape(*java) + "\"") : "null")
                       << ",\"maven\":" << (maven.has_value() ? ("\"" + json_escape(*maven) + "\"") : "null") << ",\"errors\":[" << errs
                       << "]}\n";
        };

        if (!need_java && !need_maven) {
            print_done(java_path, mvn_path, {});
            return 0;
        }

        auto progress_cb = [](const std::string& label, uint64_t downloaded, std::optional<uint64_t> total) {
            std::cout << "{\"type\":\"progress\",\"label\":\"" << json_escape(label) << "\",\"pct\":";
            if (total.has_value() && *total > 0) {
                std::cout << (downloaded * 100 / *total);
            } else {
                std::cout << "null";
            }
            std::cout << ",\"downloaded_mb\":" << (downloaded / 1024 / 1024) << ",\"total_mb\":";
            if (total.has_value())
                std::cout << (*total / 1024 / 1024);
            else
                std::cout << "null";
            std::cout << "}\n";
            std::cout.flush();
        };

        auto result = nd::install_missing(need_java, need_maven, progress_cb);
        print_done(result.java, result.maven, result.errors);
        return 0;
    } catch (const std::exception& e) {
        std::cout << "{\"type\":\"error\",\"message\":\"" << json_escape(e.what()) << "\"}\n";
        return 1;
    }
}

}  // namespace

int run_cli(int argc, char** argv) {
    std::vector<std::string> raw_args(argv + 1, argv + argc);
    std::vector<std::string> args;
    for (size_t i = 0; i < raw_args.size(); ++i) {
        if (raw_args[i].rfind("--lang=", 0) == 0) {
            g_lang = raw_args[i].substr(7);
        } else if (raw_args[i] == "--lang" && i + 1 < raw_args.size()) {
            g_lang = raw_args[++i];
        } else {
            args.push_back(raw_args[i]);
        }
    }

    if (args.empty()) {
        print_usage();
        return 1;
    }

            if (args[0].rfind("--check-update", 0) == 0) {
        std::string channel = "stable";
        if (args[0].size() > 15 && args[0][14] == '=') channel = args[0].substr(15);
        else if (args.size() > 1 && args[1][0] != '-') channel = args[1];
        nd::check_update(channel);
        return 0;
    }
    if (args[0].rfind("--update", 0) == 0) {
        std::string channel = "stable";
        if (args[0].size() > 9 && args[0][8] == '=') channel = args[0].substr(9);
        else if (args.size() > 1 && args[1][0] != '-') channel = args[1];
        nd::do_update(argv[0], channel);
        return 0;
    }
    if (args[0] == "--version") {
        // БАГ-ФИКС: раньше версии движка/GUI в SettingsModal.tsx/AppHeader.tsx/
        // Titlebar.tsx/StatusBar.tsx были захардкожены заглушками из демо-
        // прототипа ("2.4.1", "GUI v2.1.0", "build a3f9c2") и никогда не
        // совпадали с реальным version.hpp ("NanoDecompiler v1.6.1 BETA") -
        // мгновенный флаг без запуска jar/сервера, чтобы GUI мог спросить
        // движок напрямую вместо хардкода.
        std::cout << "{\"version\":\"" << NANO_DECOMPILER_VERSION << "\"}\n";
        return 0;
    }


      if (args[0] == "--security-radar" || args[0] == "-sr") {
        if (args.size() < 2) {
            std::cout << "{\"error\":\"Usage: NanoDecompilerCLI --security-radar plugin.jar [--json]\"}\n";
            return 1;
        }
        bool json_out = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--json" || args[i] == "--json-output") json_out = true;
        }
        return run_security_radar(args[1], json_out);
    }

    if (args[0] == "--sandbox" || args[0] == "-sb") {
        if (args.size() < 2) {
            std::cout << "{\"error\":\"Usage: NanoDecompilerCLI --sandbox plugin.jar [--json]\"}\n";
            return 1;
        }
        bool json_out = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--json" || args[i] == "--json-output") json_out = true;
        }
        return run_sandbox_cli(args[1], json_out);
    }

    if (args[0] == "--patch-utf8" || args[0] == "-p") {
        if (args.size() < 5) {
            std::cout << "{\"error\":\"Usage: NanoDecompilerCLI --patch-utf8 in.jar out.jar old_str new_str\"}\n";
            return 1;
        }
        return run_patch_utf8(args[1], args[2], args[3], args[4]);
    }

    
    if (args[0] == "--api-diff" || args[0] == "-ad") {
        if (args.size() < 3) {
            std::cout << "{\"error\":\"Usage: NanoDecompilerCLI --api-diff old.jar new.jar [--json]\"}\n";
            return 1;
        }
        bool json_out = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--json" || args[i] == "--json-output") json_out = true;
        }
        return run_api_diff(args[1], args[2], json_out);
    }

    if (args[0] == "--export-graph" || args[0] == "-eg") {
        if (args.size() < 3) {
            std::cout << "{\"error\":\"Usage: NanoDecompilerCLI --export-graph plugin.jar out.json\"}\n";
            return 1;
        }
        return run_export_graph(args[1], args[2]);
    }

    if (args[0] == "--fuzz-exploits" || args[0] == "-fe") {
        if (args.size() < 2) {
            std::cout << "{\"error\":\"Usage: NanoDecompilerCLI --fuzz-exploits plugin.jar [--json]\"}\n";
            return 1;
        }
        bool json_out = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--json" || args[i] == "--json-output") json_out = true;
        }
        return run_fuzz_exploits(args[1], json_out);
    }

    if (args[0] == "--immunize" || args[0] == "-im") {
        if (args.size() < 3) {
            std::cout << "{\"error\":\"Usage: NanoDecompilerCLI --immunize in.jar out_safe.jar\"}\n";
            return 1;
        }
        return run_immunize(args[1], args[2]);
    }

    if (args[0] == "--port-folia" || args[0] == "-pf") {
        if (args.size() < 3) {
            std::cout << "{\"error\":\"Usage: NanoDecompilerCLI --port-folia in.jar out_folia.jar\"}\n";
            return 1;
        }
        return run_port_folia(args[1], args[2]);
    }

    if (args[0] == "--clear-cache") {
        nd::DecompCache::instance().clear();
        std::cout << "{\"status\":\"ok\",\"message\":\"Bytecode deduplication cache cleared successfully\"}\n";
        return 0;
    }

    if (args[0] == "--jar-summary") {

        if (args.size() < 2) {
            std::cout << "{\"error\":\"использование: NanoDecompilerCLI --jar-summary plugin.jar\"}\n";
            return 0;
        }
        {
            // БАГ-ФИКС v1.7.6: см. подробный комментарий у аналогичной
            // проверки ниже (обычный/json режим декомпиляции) - та же
            // путаница "нет прав" -> "файл не найден" была и здесь.
            std::error_code ec;
            bool is_reg = fs::is_regular_file(fs::u8path(args[1]), ec);
            if (!is_reg) {
                // json_escape: путь Windows (C:\Users\...) с обратными слэшами раньше давал
                // НЕВАЛИДНЫЙ JSON, и GUI не мог прочитать причину ошибки.
                if (ec && ec == std::errc::permission_denied) {
                    std::cout << "{\"error\":\"нет прав на чтение файла: " << json_escape(args[1]) << "\"}\n";
                } else {
                    std::cout << "{\"error\":\"файл не найден: " << json_escape(args[1]) << "\"}\n";
                }
                return 0;
            }
        }
        std::cout << nd::jar_summary_to_json(nd::jar_summary(args[1])) << "\n";
        return 0;
    }

    if (args[0] == "--api-server") {
        std::string host = "127.0.0.1";
        int port = 8791;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--host" && i + 1 < args.size()) host = args[++i];
            else if (args[i] == "--port" && i + 1 < args.size()) port = std::atoi(args[++i].c_str());
        }
        return nd::run_api_server(host, port) ? 0 : 1;
    }

    if (args[0].rfind("--install-tools-json", 0) == 0) {
        std::optional<std::string> only;
        auto eq = args[0].find('=');
        if (eq != std::string::npos) {
            std::string v = args[0].substr(eq + 1);
            for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            only = v;
        }
        return run_install_tools_json(only);
    }

    if (args[0].rfind("--install-tools", 0) == 0) {
        std::optional<std::string> only;
        auto eq = args[0].find('=');
        if (eq != std::string::npos) {
            std::string v = args[0].substr(eq + 1);
            for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            only = v;
        }
        return run_install_tools_console(only);
    }

    std::string jar_path = args[0];
    bool skip_legitimacy = false;
    bool json_output = false;
    bool keep_archive_temp = false;
    bool ignore_nanoguard = false;
    bool skip_malware_scan = false;
    bool port_to_121 = false;
    bool enable_semantic_namer = false;
    bool include_libs = false;
    std::string explicit_out_dir;
    std::vector<std::string> positional_rest;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--no-legitimacy-check") {
            skip_legitimacy = true;
        } else if (args[i] == "--skip-scan" || args[i] == "--no-scan" || args[i] == "--no-malware-scan") {
            skip_malware_scan = true;
        } else if (args[i] == "--ignore-nanoguard" || args[i] == "--force" || args[i] == "-f") {
            ignore_nanoguard = true;
        } else if (args[i] == "--json-output" || args[i] == "--api") {
            json_output = true;
        } else if (args[i] == "--keep-temp") {
            keep_archive_temp = true;  // не удалять распакованный архив из временной папки
        } else if (args[i] == "--port-121" || args[i] == "--port") {
            port_to_121 = true;
        } else if (args[i] == "--semantic-names" || args[i] == "--semantic") {
            enable_semantic_namer = true;
        } else if (args[i] == "--include-libs" || args[i] == "--decompile-libs" || args[i] == "-il") {
            include_libs = true;
        } else if ((args[i] == "-o" || args[i] == "--out" || args[i] == "--output") && i + 1 < args.size()) {
            explicit_out_dir = args[++i];
        } else {
            positional_rest.push_back(args[i]);
        }
    }
    std::string out_dir = !explicit_out_dir.empty() ? explicit_out_dir : (!positional_rest.empty() ? positional_rest[0] : (fs::u8path(jar_path).stem().u8string() + "_decompiled"));

    // БАГ-ФИКС v1.7.6 (реальная жалоба пользователя - "не может декомпить
    // плагин на диске С, будто нет прав, но через Загрузки всё ок"):
    // fs::is_regular_file() без std::error_code при ошибке ДОСТУПА (не
    // "файла нет", а "нет прав его прочитать/статнуть" - например, если
    // jar лежит в защищённой системной папке типа Program Files/Windows)
    // просто возвращает false - неотличимо от "файла реально нет". Отсюда
    // вводящее в заблуждение "файл не найден", хотя файл РЕАЛЬНО есть,
    // просто движку не хватает прав его открыть. Различаем причины через
    // std::error_code явно и даём разные, куда более полезные сообщения.
    std::error_code jar_check_ec;
    bool jar_is_regular = fs::is_regular_file(fs::u8path(jar_path), jar_check_ec);
    if (!jar_is_regular) {
        std::string msg;
        if (jar_check_ec && jar_check_ec == std::errc::permission_denied) {
            msg = "нет прав на чтение файла: " + jar_path +
                  " - попробуйте переместить .jar в другую папку (например, в \"Загрузки\") "
                  "или запустить программу от имени администратора";
        } else if (jar_check_ec) {
            msg = "не удалось проверить файл: " + jar_path + " (" + jar_check_ec.message() + ")";
        } else {
            msg = "файл не найден: " + jar_path;
        }
        if (json_output) {
            std::cout << nd::json_error_response(msg) << "\n";
            return 1;
        }
        std::cerr << "[!] ОШИБКА: " << msg << "\n";
        return 1;
    }

    std::string ext = fs::u8path(jar_path).extension().u8string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext != ".jar" && nd::is_supported_archive_file(jar_path)) {
        if (!json_output) {
            std::cout << "[archive] Распознан архив плагинов: " << jar_path << "\n";
            std::cout << "[archive] Распаковка во временную директорию...\n";
        }
        auto summary = nd::extract_and_scan_archive(jar_path);
        if (!summary.ok) {
            std::string msg = "Ошибка распаковки архива: " + summary.error;
            if (json_output) {
                std::cout << nd::json_error_response(msg) << "\n";
                return 1;
            }
            std::cerr << "[!] ОШИБКА: " << msg << "\n";
            return 1;
        }

        if (!json_output) {
            std::cout << "[archive] Найдено .jar файлов: " << summary.total_jars << "\n";
            std::cout << "[archive] Из них Bukkit/Paper/Bungee/Velocity плагинов: " << summary.plugins.size() << "\n";
            if (!summary.server_cores.empty()) {
                std::cout << "[archive] Серверных ядер пропущено: " << summary.server_cores.size() << "\n";
                for (const auto& core : summary.server_cores) {
                    std::cout << "          - " << core.filename << " (" << core.core_reason << ")\n";
                }
            }
        }

        if (summary.plugins.empty()) {
            std::string msg = "В архиве не найдено ни одного серверного Java-плагина";
            if (!summary.server_cores.empty()) {
                msg += " (обнаружены серверные ядра Minecraft, которые не декомпилируются как плагины)";
            }
            if (!keep_archive_temp) nd::remove_archive_temp_dir(summary.temp_dir);
            if (json_output) {
                std::cout << nd::json_error_response(msg) << "\n";
                return 1;
            }
            std::cerr << "[!] ОШИБКА: " << msg << "\n";
            return 1;
        }

        // Декомпилируем найденные плагины
        int ret = 0;
        for (size_t i = 0; i < summary.plugins.size(); ++i) {
            const auto& p = summary.plugins[i];
            std::string plugin_out_dir = out_dir;
            if (summary.plugins.size() > 1) {
                plugin_out_dir = (fs::u8path(out_dir) / fs::u8path(p.filename).stem()).u8string();
            }
            if (!json_output) {
                std::cout << "\r\033[K[" << (i + 1) << "/" << summary.plugins.size() << "] Декомпилирую: \"" << p.filename << "\"..." << std::flush;
            }
            if (json_output) {
                ret |= nd::run_json_output(p.full_path, plugin_out_dir, skip_legitimacy, ignore_nanoguard);
            } else {
                ret |= run_decompile_console(p.full_path, plugin_out_dir, skip_legitimacy, ignore_nanoguard, /*suppress_banner=*/true, skip_malware_scan);
                std::cout << "\r\033[K[" << (i + 1) << "/" << summary.plugins.size() << "] Декомпилировано: \"" << p.filename << "\"\n" << std::flush;
            }
        }
        // Распакованный архив больше не нужен (результат лежит в out_dir). Раньше папка в
        // %TEMP%/NanoDecompiler/archives оставалась навсегда и копилась с каждым запуском.
        if (!keep_archive_temp) nd::remove_archive_temp_dir(summary.temp_dir);
        return ret;
    }

    if (json_output) return nd::run_json_output(jar_path, out_dir, skip_legitimacy, ignore_nanoguard);

    return run_decompile_console(jar_path, out_dir, skip_legitimacy, ignore_nanoguard, /*suppress_banner=*/false, skip_malware_scan, port_to_121, enable_semantic_namer, include_libs);
}

// БАГ-ФИКС v1.7.4 (реальный краш на Windows - необработанное исключение
// std::filesystem::filesystem_error улетало прямо в std::terminate(),
// пользователь видел сырой C++ "terminate called after throwing..." вместо
// понятного сообщения). Первопричина (кодировка путей на Windows) исправлена
// отдельно (см. fs::u8path() выше), но это - защита "на всякий случай":
// ЛЮБОЕ другое непредвиденное исключение (в этой ли функции, в будущих ли
// изменениях кода) теперь даёт читаемую ошибку и код возврата 1 вместо
// падения самого процесса без единого пояснения.
#ifdef _WIN32
#include <windows.h>

// БАГ-ФИКС v1.7.5 (реальный краш ВСЁ ЕЩЁ повторялся у пользователя ПОСЛЕ
// u8path()/u8string() фиксов - копали не в ту сторону): main(argc, char**
// argv) на Windows/MinGW получает аргументы командной строки УЖЕ
// сконвертированными CRT-стартапом в АНСИ-кодовую страницу (НЕ UTF-8!) -
// если реальный путь (например, содержащий кириллическое имя профиля
// пользователя Windows) не укладывается в текущую ANSI-кодовую страницу,
// argv[] УЖЕ содержит испорченные/потерянные байты ДО того, как наш код
// вообще успевает что-то сделать - fs::u8path() на УЖЕ испорченной строке
// всё равно бросает то же исключение, потому что байты не валидны в UTF-8
// (это не "наши" байты, это то, что получилось после ANSI-конвертации).
// Единственный надёжный способ получить РЕАЛЬНЫЕ (не потерянные) аргументы
// командной строки на Windows - взять их в ШИРОКОМ (UTF-16) виде через
// GetCommandLineW()+CommandLineToArgvW() (не проходят через ANSI вообще) и
// перекодировать САМИМ в UTF-8 явно (WideCharToMultiByte с CP_UTF8) - то
// есть, по сути, свой собственный "wmain", т.к. использовать wmain()
// напрямую с MinGW менее переносимо между вариантами тулчейна.
std::vector<std::string> get_utf8_argv() {
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    std::vector<std::string> out;
    if (!wargv) return out;
    for (int i = 0; i < wargc; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        if (len > 0) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    LocalFree(wargv);
    return out;
}
#else
#include <signal.h>
#include <unistd.h>
#if !defined(__ANDROID__)
#include <execinfo.h>
#endif
#include <sys/resource.h>

#include "process_jar.hpp"

static void sigsegv_handler(int sig) {
    (void)sig;
    const char msg[] = "\n[CRASH] Caught fatal signal (SIGSEGV/SIGBUS)!\n";
    if (write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) {}
    if (nd::g_crash_class_name != nullptr && nd::g_crash_class_name[0] != '\0') {
        const char cmsg[] = "[CRASH] Processing class: ";
        if (write(STDERR_FILENO, cmsg, sizeof(cmsg) - 1) < 0) {}
        size_t len = 0;
        while (nd::g_crash_class_name[len]) len++;
        if (write(STDERR_FILENO, nd::g_crash_class_name, len) < 0) {}
        if (write(STDERR_FILENO, "\n", 1) < 0) {}
    }
    if (nd::g_crash_method_name != nullptr && nd::g_crash_method_name[0] != '\0') {
        const char mmsg[] = "[CRASH] Processing method: ";
        if (write(STDERR_FILENO, mmsg, sizeof(mmsg) - 1) < 0) {}
        size_t len = 0;
        while (nd::g_crash_method_name[len]) len++;
        if (write(STDERR_FILENO, nd::g_crash_method_name, len) < 0) {}
        if (write(STDERR_FILENO, "\n", 1) < 0) {}
    }
    if (nd::g_crash_pass_name != nullptr && nd::g_crash_pass_name[0] != '\0') {
        const char pmsg[] = "[CRASH] Active pass: ";
        if (write(STDERR_FILENO, pmsg, sizeof(pmsg) - 1) < 0) {}
        size_t len = 0;
        while (nd::g_crash_pass_name[len]) len++;
        if (write(STDERR_FILENO, nd::g_crash_pass_name, len) < 0) {}
        if (write(STDERR_FILENO, "\n", 1) < 0) {}
    }
    void* callstack[64];
    #if !defined(__ANDROID__)
    int frames = backtrace(callstack, 64);
    
#endif
    
    _exit(139);
}

static void bump_stack_limit() {
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0) {
        if (rl.rlim_max > rl.rlim_cur) {
            rl.rlim_cur = std::min<rlim_t>(64 * 1024 * 1024, rl.rlim_max);
            setrlimit(RLIMIT_STACK, &rl);
        }
    }
}
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
    signal(SIGSEGV, sigsegv_handler);
    signal(SIGBUS, sigsegv_handler);
    bump_stack_limit();
#endif
    try {
        nd::cleanup_update_leftovers();
#ifdef _WIN32
        // Игнорируем ANSI-испорченный argv целиком - берём собственный,
        // честно перекодированный из UTF-16 в UTF-8 (см. get_utf8_argv()).
        std::vector<std::string> wide_args = get_utf8_argv();
        std::vector<char*> fake_argv;
        for (auto& s : wide_args) fake_argv.push_back(s.data());
        return run_cli(static_cast<int>(fake_argv.size()), fake_argv.data());
#else
        return run_cli(argc, argv);
#endif
    } catch (const std::exception& e) {
        std::cerr << "[!] ВНУТРЕННЯЯ ОШИБКА движка: " << e.what() << "\n"
                   << "    Пожалуйста, сообщите об этом разработчику вместе с именем .jar файла,\n"
                   << "    который вызвал эту ошибку (см. README_RU.txt).\n";
        return 1;
    } catch (...) {
        std::cerr << "[!] ВНУТРЕННЯЯ ОШИБКА движка: неизвестное исключение (не std::exception).\n";
        return 1;
    }
}

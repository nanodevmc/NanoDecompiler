// auto_update.cpp - самообновление CLI (--check-update / --update).
//
// БЕЗОПАСНОСТЬ (переписано в 2.0.0-alpha). Раньше:
//  - скачанный бинарник подменял текущий БЕЗ проверки SHA256 (в Electron-
//    апдейтере проверка была, в CLI - нет);
//  - Windows: HTTP-статус не проверялся - страница с ошибкой 404 сохранялась
//    как .exe и ставилась поверх рабочего; обрыв чтения не детектировался;
//  - Windows: если первый MoveFileA проходил, а второй нет, рабочий exe уже
//    был переименован в .old, и CLI оставался сломанным (отката не было);
//  - POSIX: curl запускался через system() со строкой "curl -sL '<url>' ..."
//    - URL берётся из JSON ответа GitHub, символ ' давал инъекцию команд;
//    mv результата не проверялся, "Update successful!" печаталось всегда;
//  - временные файлы создавались в ТЕКУЩЕЙ директории (nd_update_tmp.json,
//    NanoDecompilerCLI_new) - гонка/подмена в общих папках;
//  - путь к себе брался из argv[0] (может быть относительным или вообще не
//    путём).
// Теперь: curl вызывается без shell, адреса ограничены GitHub'ом (https), файл
// качается рядом с exe (атомарная замена на одной ФС), обязательно сверяется с
// SHA256 из checksums.json релиза, при сбое замены выполняется откат.
#include <cstdint>  // MinGW не тянет int64_t транзитивно, как libstdc++ на Linux
#include "auto_update.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "json_value.hpp"
#include "proc_util.hpp"
#include "sha256.hpp"
#include "version.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#endif

namespace fs = std::filesystem;

namespace nd {

namespace {

constexpr const char* kLatestReleaseUrl = "https://api.github.com/repos/NanoDev1488/NanoDecompiler/releases/latest";
constexpr const char* kReleaseDownloadPrefix = "/nanodev1488/nanodecompiler/releases/download/";
constexpr uint64_t kMaxJsonBytes = 5ull * 1024 * 1024;
constexpr uint64_t kMaxBinaryBytes = 256ull * 1024 * 1024;
constexpr uint64_t kMinBinaryBytes = 64ull * 1024;  // заведомо не страница с ошибкой

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Разбирает https://host[:port]/path; false для чего угодно другого (http,
// userinfo, нестандартный порт).
bool parse_https_url(const std::string& url, std::string& host, std::string& path) {
    const std::string scheme = "https://";
    if (url.size() <= scheme.size() || to_lower(url.substr(0, scheme.size())) != scheme) return false;
    size_t host_start = scheme.size();
    size_t slash = url.find('/', host_start);
    std::string authority = url.substr(host_start, slash == std::string::npos ? std::string::npos : slash - host_start);
    path = slash == std::string::npos ? "/" : url.substr(slash);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;
    size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        if (authority.substr(colon + 1) != "443") return false;
        authority = authority.substr(0, colon);
    }
    host = to_lower(authority);
    for (unsigned char c : url) {
        if (c <= 0x20 || c == 0x7f) return false;  // управляющие символы/пробелы в URL
    }
    return !host.empty();
}

// Допустимые адреса: только GitHub. first_hop - адрес, который выбрали МЫ (API
// проекта или browser_download_url его релиза); на редиректах разрешён ещё
// и CDN релизных файлов *.githubusercontent.com.
bool is_allowed_url(const std::string& url, bool first_hop) {
    std::string host, path;
    if (!parse_https_url(url, host, path)) return false;
    std::string lpath = to_lower(path);
    if (host == "api.github.com") return !first_hop || lpath.rfind("/repos/nanodev1488/nanodecompiler/", 0) == 0;
    if (host == "github.com") return !first_hop || lpath.rfind(kReleaseDownloadPrefix, 0) == 0;
    const std::string suffix = ".githubusercontent.com";
    return !first_hop && host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Скачивает url в память (to_file пуст) или в файл. Возвращает пустую строку
// при успехе, иначе - текст ошибки.
std::string https_fetch(const std::string& url, uint64_t max_bytes, std::string* out_mem, const std::string& to_file) {
    if (!is_allowed_url(url, true)) return "недопустимый адрес загрузки: " + url;

#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("NanoDecompilerCLI", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return "InternetOpen failed";
    DWORD timeout_ms = 30000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout_ms, sizeof(timeout_ms));

    HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
                                          INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return "InternetOpenUrl failed";
    }

    std::string err;
    // HTTP-статус: раньше не проверялся - тело ответа 404 принималось за файл.
    DWORD status = 0;
    DWORD status_len = sizeof(status);
    if (!HttpQueryInfoA(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &status_len, NULL) || status != 200) {
        err = "сервер ответил HTTP " + std::to_string(status);
    }
    // Адрес ПОСЛЕ редиректов тоже должен оставаться GitHub'овским.
    if (err.empty()) {
        char final_url[2048] = {0};
        DWORD final_len = sizeof(final_url);
        if (InternetQueryOptionA(hConnect, INTERNET_OPTION_URL, final_url, &final_len) && !is_allowed_url(final_url, false)) {
            err = std::string("редирект на недопустимый адрес: ") + final_url;
        }
    }
    uint64_t declared = 0;
    bool has_declared = false;
    if (err.empty()) {
        DWORD cl = 0;
        DWORD cl_len = sizeof(cl);
        if (HttpQueryInfoA(hConnect, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &cl, &cl_len, NULL)) {
            declared = cl;
            has_declared = true;
            if (declared > max_bytes) err = "размер файла превышает допустимый лимит";
        }
    }

    std::ofstream file;
    if (err.empty() && !to_file.empty()) {
        file.open(fs::u8path(to_file), std::ios::binary | std::ios::trunc);
        if (!file) err = "не удалось создать файл " + to_file;
    }

    uint64_t total = 0;
    char buffer[16384];
    while (err.empty()) {
        DWORD bytes_read = 0;
        if (!InternetReadFile(hConnect, buffer, sizeof(buffer), &bytes_read)) {
            err = "ошибка чтения из сети";
            break;
        }
        if (bytes_read == 0) break;
        total += bytes_read;
        if (total > max_bytes) {
            err = "размер файла превышает допустимый лимит";
            break;
        }
        if (to_file.empty()) out_mem->append(buffer, bytes_read);
        else file.write(buffer, bytes_read);
    }
    if (err.empty() && has_declared && total != declared) err = "файл скачан не полностью";
    if (file.is_open()) {
        file.close();
        if (!file && err.empty()) err = "не удалось записать файл";
    }
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    if (!err.empty() && !to_file.empty()) {
        std::error_code ec;
        fs::remove(fs::u8path(to_file), ec);
    }
    return err;
#else
    // curl без shell: адрес и пути - отдельные аргументы. -f: HTTP>=400 = ошибка;
    // --proto*: только https и на редиректах; -w: адрес после редиректов.
    std::vector<std::string> argv = {"curl",           "-fsSL",     "--proto", "=https", "--proto-redir", "=https",
                                     "--max-redirs",   "5",         "--max-filesize", std::to_string(max_bytes),
                                     "-m",             "300",       "-A",      "NanoDecompilerCLI"};
    if (!to_file.empty()) {
        argv.push_back("-o");
        argv.push_back(to_file);
        argv.push_back("-w");
        argv.push_back("%{url_effective}");
    }
    argv.push_back(url);

    ProcResult pr = run_process(argv, ProcOutput::Capture, 330, to_file.empty() ? static_cast<size_t>(max_bytes) : 8192);
    std::string err;
    if (!pr.started) err = "не удалось запустить curl (установите curl)";
    else if (pr.timed_out) err = "таймаут загрузки";
    else if (pr.truncated) err = "размер ответа превышает допустимый лимит";
    else if (pr.exit_code != 0) err = "curl завершился с кодом " + std::to_string(pr.exit_code);
    else if (!to_file.empty() && !is_allowed_url(pr.out, false)) err = "редирект на недопустимый адрес: " + pr.out;
    if (err.empty()) {
        if (to_file.empty()) *out_mem = pr.out;
    } else if (!to_file.empty()) {
        std::error_code ec;
        fs::remove(fs::u8path(to_file), ec);
    }
    return err;
#endif
}

std::string normalize_version(std::string v) {
    if (v.rfind("NanoDecompiler ", 0) == 0) {
        v = v.substr(15);
    }
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) {
        v = v.substr(1);
    }
    return v;
}

int compare_semver(const std::string& a, const std::string& b) {
    auto parse_parts = [](const std::string& s) {
        std::vector<int> parts;
        size_t start = 0;
        while (start < s.size()) {
            size_t dot = s.find('.', start);
            if (dot == std::string::npos) dot = s.size();
            try {
                parts.push_back(std::stoi(s.substr(start, dot - start)));
            } catch (...) {
                parts.push_back(0);
            }
            start = dot + 1;
        }
        return parts;
    };
    auto pa = parse_parts(a);
    auto pb = parse_parts(b);
    size_t n = pa.size() > pb.size() ? pa.size() : pb.size();
    for (size_t i = 0; i < n; ++i) {
        int va = (i < pa.size()) ? pa[i] : 0;
        int vb = (i < pb.size()) ? pb[i] : 0;
        if (va != vb) return (va > vb) ? 1 : -1;
    }
    return 0;
}

// Имя файла релизного ассета для этой платформы.
const char* platform_asset_name() {
#ifdef _WIN32
    return "NanoDecompilerClApi-windows.exe";
#elif defined(__APPLE__)
    return "NanoDecompilerClApi-macos";
#else
    return "NanoDecompilerClApi-linux";
#endif
}

std::string find_asset_url(const JsonValue& release, const std::string& asset_name) {
    const JsonValue* assets = release.get("assets");
    if (!assets || !assets->is_array() || !assets->arr_v) return "";
    for (const auto& a : *assets->arr_v) {
        const JsonValue* name = a.get("name");
        const JsonValue* url = a.get("browser_download_url");
        if (a.is_object() && name && name->is_string() && url && url->is_string() && name->str_v == asset_name) return url->str_v;
    }
    return "";
}

std::string hex_of(const std::array<uint8_t, 32>& d) {
    static const char* hexd = "0123456789abcdef";
    std::string out;
    for (uint8_t b : d) {
        out += hexd[b >> 4];
        out += hexd[b & 0xF];
    }
    return out;
}

bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

void check_update() {
    std::cout << "Checking for updates..." << std::endl;
    std::string json_str;
    std::string fetch_err = https_fetch(kLatestReleaseUrl, kMaxJsonBytes, &json_str, "");
    if (!fetch_err.empty() || json_str.empty()) {
        std::cerr << "Failed to check updates." << (fetch_err.empty() ? "" : " (" + fetch_err + ")") << std::endl;
        return;
    }
    auto opt_val = json_parse(json_str);
    if (!opt_val) {
        std::cerr << "Failed to parse GitHub response." << std::endl;
        return;
    }
    JsonValue val = *opt_val;
    if (val.is_object() && val.get("tag_name")) {
        std::string tag = val.get("tag_name")->as_string().value_or("");
        std::cout << "Current version: " << NANO_DECOMPILER_VERSION << std::endl;
        std::cout << "Latest version:  " << tag << std::endl;
        std::string current_norm = normalize_version(NANO_DECOMPILER_VERSION);
        std::string latest_norm = normalize_version(tag);
        if (compare_semver(latest_norm, current_norm) > 0) {
            std::cout << "\nUpdate available! Run with --update to upgrade." << std::endl;
        } else {
            std::cout << "\nYou are on the latest version." << std::endl;
        }
    } else {
        std::cerr << "Invalid response from GitHub." << std::endl;
    }
}

void cleanup_update_leftovers() {
    std::string self = current_executable_path();
    if (self.empty()) return;
    std::error_code ec;
    // .old - прежний exe после обновления на Windows (запущенный файл нельзя было
    // удалить сразу); раньше он оставался на диске навсегда. .new - недокачанный файл.
    fs::remove(fs::u8path(self + ".old"), ec);
    fs::remove(fs::u8path(self + ".new"), ec);
}

// Параметр оставлен ради совместимости сигнатуры: путь к себе берётся у ОС
// (argv[0] может быть относительным, результатом поиска в PATH или вообще не путём).
void do_update(const std::string& /*self_path_argv*/) {
    std::cout << "Fetching latest release info..." << std::endl;
    std::string json_str;
    std::string err = https_fetch(kLatestReleaseUrl, kMaxJsonBytes, &json_str, "");
    if (!err.empty() || json_str.empty()) {
        std::cerr << "Failed to get release info." << (err.empty() ? "" : " (" + err + ")") << std::endl;
        return;
    }
    auto opt_val = json_parse(json_str);
    if (!opt_val) {
        std::cerr << "Failed to parse JSON." << std::endl;
        return;
    }
    const JsonValue& release = *opt_val;
    if (!release.is_object() || !release.get("tag_name") || !release.get("assets")) {
        std::cerr << "Invalid response from GitHub." << std::endl;
        return;
    }

    std::string tag = release.get("tag_name")->as_string().value_or("");
    if (compare_semver(normalize_version(tag), normalize_version(NANO_DECOMPILER_VERSION)) <= 0) {
        std::cout << "Already at the latest version (" << tag << ")." << std::endl;
        return;
    }

    const std::string asset_name = platform_asset_name();
    std::string asset_url = find_asset_url(release, asset_name);
    if (asset_url.empty()) {
        std::cerr << "Could not find compatible asset for this OS in the latest release." << std::endl;
        return;
    }

    // Контрольная сумма ОБЯЗАТЕЛЬНА: нет checksums.json / записи для файла - не обновляемся.
    std::string checksums_url = find_asset_url(release, "checksums.json");
    std::string checksums_text;
    if (checksums_url.empty() || !https_fetch(checksums_url, 1024 * 1024, &checksums_text, "").empty()) {
        std::cerr << "Update aborted: could not get checksums.json for the release (refusing to install an unverified binary)." << std::endl;
        return;
    }
    std::string expected_hash;
    if (auto checksums = json_parse(checksums_text)) {
        if (const JsonValue* h = checksums->get(asset_name)) expected_hash = to_lower(h->as_string().value_or(""));
    }
    if (expected_hash.size() != 64 || expected_hash.find_first_not_of("0123456789abcdef") != std::string::npos) {
        std::cerr << "Update aborted: no valid SHA256 for " << asset_name << " in checksums.json." << std::endl;
        return;
    }

    std::string self_path = current_executable_path();
    if (self_path.empty()) {
        std::cerr << "Update aborted: cannot determine the path of the running executable." << std::endl;
        return;
    }
    // Рядом с exe (одна файловая система -> атомарный rename), а не в текущей директории.
    std::string new_path = self_path + ".new";

    std::cout << "Downloading update from " << asset_url << " ..." << std::endl;
    err = https_fetch(asset_url, kMaxBinaryBytes, nullptr, new_path);
    if (!err.empty()) {
        std::cerr << "Download failed: " << err << std::endl;
        return;
    }

    std::vector<uint8_t> bytes;
    bool ok_read = read_file_bytes(new_path, bytes);
    std::error_code ec;
    if (!ok_read || bytes.size() < kMinBinaryBytes || hex_of(sha256(bytes)) != expected_hash) {
        fs::remove(fs::u8path(new_path), ec);
        std::cerr << "Update aborted: downloaded file failed the SHA256 / size check." << std::endl;
        return;
    }
    bytes.clear();
    bytes.shrink_to_fit();

    std::cout << "Download verified. Replacing executable..." << std::endl;
    fs::path self_p = fs::u8path(self_path);
    fs::path new_p = fs::u8path(new_path);
#ifdef _WIN32
    // Запущенный exe на Windows можно переименовать, но не перезаписать: уводим
    // его в .old, ставим новый и при ЛЮБОМ сбое возвращаем старый на место.
    fs::path old_p = fs::u8path(self_path + ".old");
    fs::remove(old_p, ec);
    ec.clear();
    fs::rename(self_p, old_p, ec);
    if (ec) {
        fs::remove(new_p, ec);
        std::cerr << "Update failed: cannot rename the current executable (" << ec.message() << ")." << std::endl;
        return;
    }
    std::error_code ec2;
    fs::rename(new_p, self_p, ec2);
    if (ec2) {
        std::error_code ec3;
        fs::rename(old_p, self_p, ec3);  // откат
        fs::remove(new_p, ec3);
        std::cerr << "Update failed: cannot put the new executable in place (" << ec2.message() << "); previous version restored." << std::endl;
        return;
    }
#else
    fs::permissions(new_p, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
    if (ec) {
        fs::remove(new_p, ec);
        std::cerr << "Update failed: cannot make the new executable runnable." << std::endl;
        return;
    }
    std::error_code ec2;
    fs::rename(new_p, self_p, ec2);  // атомарная замена; старый inode живёт, пока процесс запущен
    if (ec2) {
        fs::remove(new_p, ec);
        std::cerr << "Update failed: cannot replace the executable (" << ec2.message() << ")." << std::endl;
        return;
    }
#endif
    std::cout << "Update successful! Restart the application." << std::endl;
}

}  // namespace nd

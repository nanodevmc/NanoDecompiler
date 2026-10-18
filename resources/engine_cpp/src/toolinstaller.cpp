// toolinstaller.cpp - см. toolinstaller.hpp.
#include <cstdint>  // БАГ-ФИКС: MinGW/Windows не тянет int64_t транзитивно через другие заголовки, как это молча делает libstdc++ на Linux - см. ошибку сборки Windows-раннера в этой сессии.
#include "toolinstaller.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "archive_extractor.hpp"
#include "json_value.hpp"
#include "proc_util.hpp"
#include "sha256.hpp"
#include "sha512.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace nd {

namespace {

#ifdef _WIN32
constexpr bool kIsWindows = true;
#else
constexpr bool kIsWindows = false;
#endif

std::vector<std::string> split_path_env() {
    const char* p = std::getenv("PATH");
    if (p == nullptr) return {};
    std::string s(p);
    char sep = kIsWindows ? ';' : ':';
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            if (i > start) out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

bool is_executable_file(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(path), ec)) return false;
#ifdef _WIN32
    return true;  // на Windows расширение (.exe/.cmd) уже говорит о типе
#else
    return ::access(path.c_str(), X_OK) == 0;
#endif
}

std::optional<std::string> which(const std::vector<std::string>& exe_names, const std::vector<std::string>& extra_dirs = {}) {
    std::vector<std::string> dirs = split_path_env();
    dirs.insert(dirs.end(), extra_dirs.begin(), extra_dirs.end());
    for (auto& dir : dirs) {
        for (auto& name : exe_names) {
            std::string full = (fs::u8path(dir) / name).u8string();
            if (is_executable_file(full)) return full;
        }
    }
    return std::nullopt;
}

#ifdef _WIN32
std::vector<std::string> registry_path_dirs() {
    std::vector<std::string> out;
    struct HiveKey {
        HKEY hive;
        const char* subkey;
    };
    HiveKey keys[] = {{HKEY_CURRENT_USER, "Environment"}, {HKEY_LOCAL_MACHINE, R"(SYSTEM\CurrentControlSet\Control\Session Manager\Environment)"}};
    for (auto& hk : keys) {
        HKEY key;
        if (RegOpenKeyExA(hk.hive, hk.subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
        DWORD type = 0, size = 0;
        if (RegQueryValueExA(key, "Path", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0) {
            std::string buf(size, '\0');
            if (RegQueryValueExA(key, "Path", nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &size) == ERROR_SUCCESS) {
                if (!buf.empty() && buf.back() == '\0') buf.pop_back();
                size_t start = 0;
                for (size_t i = 0; i <= buf.size(); ++i) {
                    if (i == buf.size() || buf[i] == ';') {
                        if (i > start) out.push_back(buf.substr(start, i - start));
                        start = i + 1;
                    }
                }
            }
        }
        RegCloseKey(key);
    }
    return out;
}

std::vector<std::string> well_known_dirs(const std::string& kind) {
    std::vector<std::string> out;
    auto getenv_s = [](const char* n) -> std::string {
        const char* v = std::getenv(n);
        return v ? v : "";
    };
    std::vector<std::string> program_files = {getenv_s("ProgramFiles"), getenv_s("ProgramFiles(x86)"), getenv_s("ProgramW6432")};
    std::string user_profile = getenv_s("USERPROFILE");

    if (kind == "java") {
        std::string java_home = getenv_s("JAVA_HOME");
        if (!java_home.empty()) out.push_back((fs::u8path(java_home) / "bin").u8string());
        for (auto& pf : program_files) {
            if (pf.empty()) continue;
            for (auto vendor : {"Java", "Eclipse Adoptium", "Eclipse Foundation", "Microsoft", "Zulu"}) {
                fs::path base = fs::u8path(pf) / vendor;
                std::error_code ec;
                if (!fs::is_directory(base, ec)) continue;
                for (auto& entry : fs::directory_iterator(base, ec)) out.push_back((entry.path() / "bin").u8string());
            }
        }
    } else if (kind == "maven") {
        for (auto var : {"MAVEN_HOME", "M2_HOME"}) {
            std::string home = getenv_s(var);
            if (!home.empty()) out.push_back((fs::u8path(home) / "bin").u8string());
        }
        for (auto& pf : program_files) {
            if (pf.empty()) continue;
            fs::path base = fs::u8path(pf) / "Apache" / "maven";
            std::error_code ec;
            if (fs::is_directory(base, ec)) out.push_back((base / "bin").u8string());
            fs::path apache_base = fs::u8path(pf) / "Apache";
            if (fs::is_directory(apache_base, ec)) {
                for (auto& entry : fs::directory_iterator(apache_base, ec)) {
                    std::string name = entry.path().filename().u8string();
                    std::string lower = name;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if (lower.rfind("maven", 0) == 0) out.push_back((entry.path() / "bin").u8string());
                }
            }
        }
        if (!user_profile.empty()) out.push_back((fs::u8path(user_profile) / "scoop" / "apps" / "maven" / "current" / "bin").u8string());
    }
    std::vector<std::string> filtered;
    for (auto& d : out) {
        std::error_code ec;
        if (fs::is_directory(d, ec)) filtered.push_back(d);
    }
    return filtered;
}
#endif

}  // namespace

std::optional<std::string> resolve_tool_path(const std::vector<std::string>& exe_names, const std::string& kind) {
    auto found = which(exe_names);
    if (found.has_value()) return found;
#ifdef _WIN32
    std::vector<std::string> extra = registry_path_dirs();
    auto wk = well_known_dirs(kind);
    extra.insert(extra.end(), wk.begin(), wk.end());
    if (extra.empty()) return std::nullopt;
    return which(exe_names, extra);
#else
    (void)kind;
    return std::nullopt;
#endif
}

std::string get_tools_dir() {
    fs::path base;
#ifdef _WIN32
    const char* local = std::getenv("LOCALAPPDATA");
    base = local != nullptr ? fs::u8path(local) : fs::u8path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".");
    base /= "NanoDecompiler";
#else
    const char* home = std::getenv("HOME");
    base = fs::u8path(home != nullptr ? home : ".") / ".nanodecompiler";
#endif
    fs::path tools = base / "tools";
    std::error_code ec;
    fs::create_directories(tools, ec);
    return tools.u8string();
}

namespace {

std::optional<std::string> find_one(const std::string& tools_dir, const std::string& dir_prefix_lower, const std::vector<std::string>& rel_bin) {
    std::error_code ec;
    if (!fs::is_directory(tools_dir, ec)) return std::nullopt;
    std::vector<std::string> candidates;
    for (auto& entry : fs::directory_iterator(tools_dir, ec)) {
        std::string name = entry.path().filename().u8string();
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind(dir_prefix_lower, 0) != 0) continue;
        fs::path full = entry.path();
        for (auto& part : rel_bin) full /= part;
        if (fs::is_regular_file(full, ec)) candidates.push_back(full.u8string());
    }
    if (candidates.empty()) return std::nullopt;
    std::sort(candidates.begin(), candidates.end());
    return candidates.back();
}

}  // namespace

std::optional<std::string> find_local_java() {
    std::string exe = kIsWindows ? "java.exe" : "java";
    return find_one(get_tools_dir(), "jdk", {"bin", exe});
}

std::optional<std::string> find_local_maven() {
    std::string exe = kIsWindows ? "mvn.cmd" : "mvn";
    return find_one(get_tools_dir(), "apache-maven", {"bin", exe});
}

namespace {

// БЕЗОПАСНОСТЬ (переписано в 2.0.0-alpha). Раньше установщик:
//  - вызывал curl/apt-get через std::system() со строкой команды (самодельное
//    квотирование shell_quote);
//  - не проверял целостность скачанных JDK/Maven вообще (никаких хешей);
//  - складывал архив в общий %TEMP%/ /tmp под "случайным" именем из mt19937,
//    засеянного одним 32-битным значением (предсказуемо) - подмена симлинком;
//  - распаковывал zip/tar БЕЗ защиты от path traversal ("../", абсолютные пути,
//    запись через ранее созданные симлинки), без лимитов размера;
//  - ставил .exe-бит только на сам java/mvn: остальные файлы JDK (например
//    lib/jspawnhelper, нужный для запуска процессов) оставались без прав на запуск.
// Теперь: curl без shell и только по https на известные хосты, SHA-256 JDK берётся
// из API Adoptium (хост отличается от хоста файла), SHA-512 Maven - из независимого
// источника (Maven Central <-> archive.apache.org), приватный каталог 0700,
// безопасная распаковка из archive_extractor.

constexpr uint64_t kMaxJdkBytes = 600ull * 1024 * 1024;
constexpr uint64_t kMaxMavenBytes = 64ull * 1024 * 1024;
constexpr int kDownloadTimeoutSec = 1800;  // JDK ~190 МБ: прежние 300 с рвали загрузку на медленном канале

bool curl_available() { return find_executable("curl", true).has_value(); }

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool is_allowed_tool_url(const std::string& url) {
    const std::string scheme = "https://";
    if (url.size() <= scheme.size() || lower_ascii(url.substr(0, scheme.size())) != scheme) return false;
    for (unsigned char c : url) {
        if (c <= 0x20 || c == 0x7f) return false;
    }
    size_t end = url.find_first_of("/?#", scheme.size());
    std::string authority = lower_ascii(url.substr(scheme.size(), end == std::string::npos ? std::string::npos : end - scheme.size()));
    if (authority.empty() || authority.find('@') != std::string::npos) return false;
    size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        if (authority.substr(colon + 1) != "443") return false;
        authority = authority.substr(0, colon);
    }
    static const char* kHosts[] = {"api.adoptium.net", "github.com", "archive.apache.org", "dlcdn.apache.org", "downloads.apache.org",
                                   "repo.maven.apache.org", "repo1.maven.org"};
    for (const char* h : kHosts) {
        if (authority == h) return true;
    }
    const std::string suffix = ".githubusercontent.com";
    return authority.size() > suffix.size() && authority.compare(authority.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_hex_of_length(const std::string& s, size_t n) {
    return s.size() == n && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::string hex_digest(const std::array<uint8_t, 32>& d) {
    static const char* hexd = "0123456789abcdef";
    std::string out;
    for (uint8_t b : d) {
        out += hexd[b >> 4];
        out += hexd[b & 0xF];
    }
    return out;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Короткий текстовый ответ (JSON Adoptium, .sha512 файл Maven) через curl.
std::string curl_fetch_text(const std::string& url, size_t max_bytes) {
    if (!is_allowed_tool_url(url)) throw ToolInstallError("недопустимый адрес: " + url);
    if (!curl_available()) throw ToolInstallError("системная утилита 'curl' не найдена в PATH");
    std::vector<std::string> argv = {"curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "5",
                                     "--max-filesize", std::to_string(max_bytes), "--max-time", "120", "-A", "NanoDecompiler-toolinstaller", url};
    ProcResult r = run_process(argv, ProcOutput::Capture, 130, max_bytes);
    if (!r.started || r.timed_out || r.truncated || r.exit_code != 0) {
        throw ToolInstallError("не удалось получить " + url + " (curl: код " + std::to_string(r.exit_code) + ")");
    }
    return r.out;
}

// Скачивает url в dest_path целиком через системный curl (см. hpp - HTTPS
// без встроенной TLS-библиотеки). progress_cb получает два вызова: (0%,
// перед стартом) и (100%, после успешного завершения) - см. оговорку в hpp
// про отсутствие потокового прогресса при скачивании подпроцессом.
void curl_download(const std::string& url, const std::string& dest_path, const std::string& label, uint64_t max_bytes,
                   const ProgressCallback& progress_cb) {
    if (!is_allowed_tool_url(url)) throw ToolInstallError("недопустимый адрес загрузки: " + url);
    if (!curl_available()) {
        throw ToolInstallError("системная утилита 'curl' не найдена в PATH - без неё автозакачка JDK/Maven "
                                "недоступна (curl нужен для HTTPS-скачивания без встраивания TLS-библиотеки, "
                                "см. toolinstaller.hpp). Установите curl или скачайте " +
                                label + " вручную.");
    }
    if (progress_cb) progress_cb(label, 0, std::nullopt);
    // -w %{url_effective}: адрес ПОСЛЕ редиректов тоже обязан остаться в списке разрешённых.
    std::vector<std::string> argv = {"curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "5",
                                     "--max-filesize", std::to_string(max_bytes), "--max-time", std::to_string(kDownloadTimeoutSec),
                                     "-A", "NanoDecompiler-toolinstaller", "-o", dest_path, "-w", "%{url_effective}", url};
    ProcResult r = run_process(argv, ProcOutput::Capture, kDownloadTimeoutSec + 30, 8192);
    std::error_code ec;
    bool ok = r.started && !r.timed_out && r.exit_code == 0 && is_allowed_tool_url(r.out);
    if (!ok) {
        fs::remove(fs::u8path(dest_path), ec);
        std::string why = !r.started ? "curl не запустился" : r.timed_out ? "таймаут" : r.exit_code != 0 ? "curl завершился с кодом " + std::to_string(r.exit_code) : "редирект на недопустимый адрес";
        throw ToolInstallError("не удалось скачать " + url + " (" + why + ")");
    }
    auto sz = fs::file_size(fs::u8path(dest_path), ec);
    if (progress_cb) progress_cb(label, ec ? 0 : sz, ec ? std::nullopt : std::optional<uint64_t>(sz));
}

// Приватный каталог для временного архива; удаляется в деструкторе при любом выходе.
struct TempDir {
    std::string path;
    TempDir() { path = make_private_temp_dir(fs::temp_directory_path().u8string(), "nd_tools_"); }
    ~TempDir() {
        if (path.empty()) return;
        std::error_code ec;
        fs::remove_all(fs::u8path(path), ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

#if !defined(_WIN32) && !defined(__APPLE__)
// НОВОЕ 1.9.6 (реальный запрос - "на Linux попробовать установку Java/Maven
// через apt/sudo apt"): системный apt-путь пробуется ПЕРВЫМ на Linux, ДО
// curl_download-пути ниже - он не зависит от доступности зеркал
// Adoptium/Apache (repo Ubuntu/Debian почти всегда доступен и быстрее),
// и именно это, по всей видимости, стояло за повторяющейся жалобой
// "Maven не ставится" (mirror-fallback самого portable-скачивания уже
// чинился в v1.8.0 - добавлял ещё зеркала, но не убирал зависимость от
// внешней сети Apache целиком; apt - независимый путь).
//
// ЧЕСТНАЯ ОГОВОРКА: НЕ протестировано вживую с реальной установкой -
// песочница этой сессии тоже без исходящей сети для apt (подтверждено:
// apt-get install -y default-jdk-headless здесь падает с 403 Forbidden
// от прокси), НО пакетные имена (`default-jdk-headless`, `maven`) и
// команда `apt-get install -y <pkg>` реально проверены через
// `apt-get install --dry-run` в этой же песочнице - оба резолвятся в
// корректное дерево зависимостей на настоящем Ubuntu 24.04 (см. HANDOFF).
// graceful degradation, как и весь остальной toolinstaller: любая
// неудача (нет apt-get, нет pkexec, пользователь отменил графический
// запрос прав, нет сети) тихо роняет исполнение на существующий
// portable curl-путь ниже, а не считается фатальной ошибкой.
bool command_available_posix(const std::string& cmd) { return find_executable(cmd, false).has_value(); }

// pkexec - штатный способ спросить права у пользователя GUI-приложению
// на современных Linux desktop (GNOME/KDE - идёт в комплекте с
// PolicyKit), показывает системный диалог, НЕ требует tty. НЕ используем
// голый `sudo` - без tty он либо тихо провалится (`sudo -n`), либо (без
// -n) зависнет, ожидая пароль на несуществующем терминале, что для
// GUI-приложения неотличимо от зависания.
bool try_apt_install(const std::vector<std::string>& packages, const std::string& label, const ProgressCallback& progress_cb) {
    if (!command_available_posix("apt-get")) return false;  // не Debian/Ubuntu-семейство - тихо на portable-путь
    if (!command_available_posix("pkexec")) return false;   // нет штатного способа спросить права без терминала
    if (progress_cb) progress_cb(label + " (apt)", 0, std::nullopt);
    // DEBIAN_FRONTEND=noninteractive - на случай debconf-диалогов у
    // каких-то транзитивных зависимостей (GUI-приложение не может
    // ответить на текстовый prompt внутри невидимого подпроцесса).
    // Имена пакетов - аргументами массива (раньше склеивались в строку shell).
    std::vector<std::string> argv = {"pkexec", "env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y"};
    for (const auto& p : packages) argv.push_back(p);
    ProcResult r = run_process(argv, ProcOutput::Discard, 30 * 60);
    if (!r.started || r.timed_out || r.exit_code != 0) return false;
    if (progress_cb) progress_cb(label + " (apt)", 100, std::optional<uint64_t>(100));
    return true;
}
#endif

void ensure_executable(const std::string& path) {
#ifndef _WIN32
    std::error_code ec;
    fs::permissions(fs::u8path(path), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#else
    (void)path;
#endif
}

std::string adoptium_os() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "mac";
#else
    return "linux";
#endif
}

std::string adoptium_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__arm__)
    return "arm";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86-32";
#else
    throw ToolInstallError("неизвестная архитектура - автозакачка JDK не поддерживается, скачайте вручную: https://adoptium.net/");
#endif
}

constexpr int kAdoptiumFeatureVersion = 17;  // LTS - см. toolinstaller.py
constexpr const char* kMavenFallbackVersion = "3.9.9";

struct AdoptiumAsset {
    std::string link;
    std::string sha256;
};

// Ссылка на актуальный JDK и его SHA-256 из API Adoptium. Сам файл лежит на
// github.com - то есть контрольная сумма приходит с ДРУГОГО хоста, чем архив.
AdoptiumAsset query_adoptium_asset(const std::string& os, const std::string& arch, bool want_zip) {
    std::string url = "https://api.adoptium.net/v3/assets/latest/" + std::to_string(kAdoptiumFeatureVersion) +
                       "/hotspot?architecture=" + arch + "&image_type=jdk&os=" + os + "&vendor=eclipse";
    auto parsed = json_parse(curl_fetch_text(url, 4u * 1024u * 1024u));
    if (!parsed || !parsed->is_array() || !parsed->arr_v) throw ToolInstallError("API Adoptium вернул неожиданный ответ");
    for (const auto& item : *parsed->arr_v) {
        const JsonValue* binary = item.get("binary");
        const JsonValue* pkg = binary ? binary->get("package") : nullptr;
        const JsonValue* link = pkg ? pkg->get("link") : nullptr;
        const JsonValue* checksum = pkg ? pkg->get("checksum") : nullptr;
        const JsonValue* name = pkg ? pkg->get("name") : nullptr;
        if (!link || !link->is_string() || !checksum || !checksum->is_string() || !name || !name->is_string()) continue;
        std::string lname = lower_ascii(name->str_v);
        bool ext_ok = want_zip ? (lname.size() > 4 && lname.compare(lname.size() - 4, 4, ".zip") == 0)
                               : (lname.size() > 7 && lname.compare(lname.size() - 7, 7, ".tar.gz") == 0);
        std::string sum = lower_ascii(checksum->str_v);
        if (ext_ok && is_hex_of_length(sum, 64) && is_allowed_tool_url(link->str_v)) return {link->str_v, sum};
    }
    throw ToolInstallError("в ответе Adoptium нет подходящего архива JDK с контрольной суммой");
}

}  // namespace

std::string install_jdk(ProgressCallback progress_cb) {
#if !defined(_WIN32) && !defined(__APPLE__)
    if (try_apt_install({"default-jdk-headless"}, "JDK", progress_cb)) {
        // apt кладёт java в системный /usr/bin, НЕ в get_tools_dir() -
        // ищем именно через resolve_tool_path (смотрит PATH текущего
        // процесса), а не find_local_java() (тот смотрит ТОЛЬКО папку
        // portable-установок самого приложения).
        auto java_path = resolve_tool_path({"java"}, "java");
        if (java_path.has_value()) return *java_path;
        // apt отчитался об успехе, но java не нашлась в PATH (нетипичный
        // случай, другой профиль путей дистрибутива) - не бросаем ошибку,
        // просто идём на portable-путь ниже, как и было раньше.
    }
#endif
    std::string tools_dir = get_tools_dir();
    bool is_zip = adoptium_os() == "windows";
    AdoptiumAsset asset = query_adoptium_asset(adoptium_os(), adoptium_arch(), is_zip);

    TempDir tmp;
    if (tmp.path.empty()) throw ToolInstallError("не удалось создать временную папку для загрузки JDK");
    std::string tmp_archive = (fs::u8path(tmp.path) / (is_zip ? "jdk.zip" : "jdk.tar.gz")).u8string();
    curl_download(asset.link, tmp_archive, "JDK", kMaxJdkBytes, progress_cb);

    std::vector<uint8_t> bytes;
    if (!read_file(tmp_archive, bytes) || hex_digest(sha256(bytes)) != asset.sha256) {
        throw ToolInstallError("контрольная сумма SHA-256 скачанного JDK не совпала с указанной Adoptium - установка отменена");
    }
    bytes.clear();
    bytes.shrink_to_fit();

    std::string root;
    std::string err;
    bool extracted = is_zip ? extract_zip_safe(tmp_archive, tools_dir, err, &root) : extract_tar_gz_safe(tmp_archive, tools_dir, err, /*allow_inner_symlinks=*/true, &root);
    if (!extracted) throw ToolInstallError("не удалось распаковать архив JDK: " + err);

    auto java_path = find_local_java();
    if (!java_path.has_value()) {
        throw ToolInstallError("JDK скачан и распакован, но java не найдена внутри - возможно, Adoptium изменил структуру архива. "
                                "Папка распаковки: " +
                                (fs::u8path(tools_dir) / root).u8string());
    }
    ensure_executable(*java_path);
    return *java_path;
}

std::string install_maven(ProgressCallback progress_cb) {
#if !defined(_WIN32) && !defined(__APPLE__)
    // См. комментарий в install_jdk() выше - тот же apt-путь, тот же
    // resolve_tool_path (НЕ find_local_maven - apt ставит в /usr/bin).
    // Это конкретно нацелено на повторяющуюся жалобу "Maven не
    // ставится" - apt полностью обходит сеть Apache mirrors, на
    // которую жалоба указывала изначально.
    if (try_apt_install({"maven"}, "Maven", progress_cb)) {
        auto maven_path = resolve_tool_path({"mvn"}, "maven");
        if (maven_path.has_value()) return *maven_path;
    }
#endif
    std::string tools_dir = get_tools_dir();
    std::string version = kMavenFallbackVersion;
    const std::string file = "apache-maven-" + version + "-bin.zip";
    // БАГ-ФИКС v1.8.0 (реальный репорт - "Maven не качается вообще, не
    // видно его в папке"): логика скачивания сама по себе была верна (URL
    // подтверждён вручную - archive.apache.org хранит версии вечно), но
    // единственный источник - точка отказа, если archive.apache.org
    // недоступен/медленный/заблокирован в конкретной сети пользователя.
    // Пробуем несколько независимых зеркал по очереди - repo.maven.apache.org
    // (Maven Central, тот же файл) и dlcdn.apache.org (текущий, НЕ архивный
    // сервер - там есть только последние версии, но 3.9.9 достаточно новая,
    // может ещё оставаться) как запасные варианты.
    const std::string central_base = "https://repo.maven.apache.org/maven2/org/apache/maven/apache-maven/" + version + "/";
    const std::string apache_base = "https://archive.apache.org/dist/maven/maven-3/" + version + "/binaries/";
    struct Mirror {
        std::string archive_url;
        std::string checksum_url;  // SHA-512 берём с ДРУГОГО источника, чем архив
    };
    std::vector<Mirror> mirrors = {
        {apache_base + file, central_base + file + ".sha512"},
        {central_base + file, apache_base + file + ".sha512"},
        {"https://dlcdn.apache.org/maven/maven-3/" + version + "/binaries/" + file, central_base + file + ".sha512"},
    };

    TempDir tmp;
    if (tmp.path.empty()) throw ToolInstallError("не удалось создать временную папку для загрузки Maven");
    std::string tmp_archive = (fs::u8path(tmp.path) / "maven.zip").u8string();

    std::vector<std::string> mirror_errors;
    bool downloaded = false;
    for (auto& m : mirrors) {
        try {
            curl_download(m.archive_url, tmp_archive, "Maven", kMaxMavenBytes, progress_cb);
            // Файл .sha512 - "<128 hex>" или "<128 hex>  <имя файла>".
            std::string checksum_text = curl_fetch_text(m.checksum_url, 4096);
            std::string expected = lower_ascii(checksum_text.substr(0, 128));
            if (!is_hex_of_length(expected, 128)) throw ToolInstallError("некорректный файл контрольной суммы: " + m.checksum_url);
            std::vector<uint8_t> bytes;
            if (!read_file(tmp_archive, bytes) || sha512_hex(bytes) != expected) {
                throw ToolInstallError("контрольная сумма SHA-512 не совпала для " + m.archive_url);
            }
            downloaded = true;
            break;
        } catch (const std::exception& e) {
            mirror_errors.push_back(m.archive_url + ": " + e.what());
            std::error_code ec;
            fs::remove(fs::u8path(tmp_archive), ec);
        }
    }
    if (!downloaded) {
        std::string joined;
        for (auto& e : mirror_errors) joined += "\n  - " + e;
        throw ToolInstallError("не удалось скачать (и проверить) Maven ни с одного зеркала:" + joined);
    }

    std::string root;
    std::string err;
    if (!extract_zip_safe(tmp_archive, tools_dir, err, &root)) throw ToolInstallError("не удалось распаковать архив Maven: " + err);

    auto mvn_path = find_local_maven();
    if (!mvn_path.has_value()) {
        throw ToolInstallError("Maven скачан и распакован, но mvn не найден внутри - возможно, изменилась структура архива. "
                                "Папка распаковки: " +
                                (fs::u8path(tools_dir) / root).u8string());
    }
    ensure_executable(*mvn_path);
    return *mvn_path;
}

InstallMissingResult install_missing(bool need_java, bool need_maven, ProgressCallback progress_cb) {
    InstallMissingResult result;
    if (need_java) {
        try {
            result.java = install_jdk(progress_cb);
        } catch (const std::exception& e) {
            result.errors.push_back(std::string("JDK: ") + e.what());
        }
    }
    if (need_maven) {
        try {
            result.maven = install_maven(progress_cb);
        } catch (const std::exception& e) {
            result.errors.push_back(std::string("Maven: ") + e.what());
        }
    }
    return result;
}

}  // namespace nd

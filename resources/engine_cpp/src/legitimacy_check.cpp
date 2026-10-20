#include <algorithm>
// legitimacy_check.cpp - см. legitimacy_check.hpp (и ОБЯЗАТЕЛЬНО оговорку
// там же про отсутствие живого сетевого тестирования в этой сессии).
#include <cstdint>  // БАГ-ФИКС: MinGW/Windows не тянет int64_t транзитивно через другие заголовки, как это молча делает libstdc++ на Linux - см. ошибку сборки Windows-раннера в этой сессии.
#include "legitimacy_check.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_map>  // НОВОЕ v1.9.13: kProbeUrls в check_site()

#include "json_value.hpp"
#include "proc_util.hpp"
#include "sha256.hpp"
#include "toolinstaller.hpp"

namespace fs = std::filesystem;

namespace nd {

// ---------------- fields_from_plugin_yml (офлайн, полностью тестируемо) ----------------

PluginYmlFields fields_from_plugin_yml(const std::string& plugin_yml_text) {
    PluginYmlFields result;
    if (plugin_yml_text.empty()) return result;

    // re.search(r"^website:\s*['\"]?([^'\"\n]+)['\"]?\s*$", text, re.M)
    {
        static const std::regex re(R"(^website:\s*['"]?([^'"\n]+)['"]?\s*$)", std::regex::multiline);
        std::smatch m;
        if (std::regex_search(plugin_yml_text, m, re)) {
            std::string v = m[1].str();
            // .strip() - убираем ведущие/хвостовые пробелы
            size_t a = v.find_first_not_of(" \t");
            size_t b = v.find_last_not_of(" \t");
            result.website = (a == std::string::npos) ? "" : v.substr(a, b - a + 1);
        }
    }
    // re.search(r"^author:\s*['\"]?([^'\"\n]+)['\"]?\s*$", text, re.M)
    {
        static const std::regex re(R"(^author:\s*['"]?([^'"\n]+)['"]?\s*$)", std::regex::multiline);
        std::smatch m;
        if (std::regex_search(plugin_yml_text, m, re)) {
            std::string v = m[1].str();
            size_t a = v.find_first_not_of(" \t");
            size_t b = v.find_last_not_of(" \t");
            result.authors = {(a == std::string::npos) ? "" : v.substr(a, b - a + 1)};
        } else {
            // re.search(r"^authors:\s*\[([^\]]*)\]", text, re.M)
            static const std::regex re2(R"(^authors:\s*\[([^\]]*)\])", std::regex::multiline);
            std::smatch m2;
            if (std::regex_search(plugin_yml_text, m2, re2)) {
                std::string inner = m2[1].str();
                // a.strip().strip("'\"") for a in inner.split(",") if a.strip()
                std::stringstream ss(inner);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    size_t a = item.find_first_not_of(" \t");
                    size_t b = item.find_last_not_of(" \t");
                    if (a == std::string::npos) continue;  // a.strip() пусто -> пропускаем (if a.strip())
                    std::string stripped = item.substr(a, b - a + 1);
                    // .strip("'\"") - убираем ведущие/хвостовые кавычки (любые из ' ")
                    size_t s0 = stripped.find_first_not_of("'\"");
                    size_t s1 = stripped.find_last_not_of("'\"");
                    if (s0 == std::string::npos) {
                        result.authors.push_back("");
                    } else {
                        result.authors.push_back(stripped.substr(s0, s1 - s0 + 1));
                    }
                }
            }
        }
    }
    return result;
}

// ---------------- HTTP через curl CLI (см. оговорку в .hpp) ----------------

namespace {

// БЕЗОПАСНОСТЬ. Раньше curl запускался через popen()/std::system() со строкой,
// в которую подставлялись URL и путь (с самодельным квотированием, которое на
// Windows cmd.exe не понимал: %VAR%, ^, \"), а адреса приходили из ответов API и из
// УДАЛЁННОГО конфига legitimacy_sites.json - т.е. кто управляет репозиторием
// (или перехватывает трафик) мог направить движок на любой хост, включая
// внутренние (SSRF). Теперь curl запускается без shell (аргументы - массив),
// только по https, и только на фиксированный список хостов.
constexpr const char* kUserAgent = "Mozilla/5.0 (NanoDecompiler-LegitimacyCheck/1.1)";
constexpr size_t kMaxHttpBodyBytes = 8u * 1024u * 1024u;
// Поддомены этих зон разрешены только для СКАЧИВАНИЯ релизных файлов.
constexpr const char* kDownloadZoneSuffix = ".githubusercontent.com";

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Хост из https://host[:port]/...; пусто, если схема не https или есть userinfo.
std::string https_host_of(const std::string& url) {
    const std::string scheme = "https://";
    if (url.size() <= scheme.size() || lower_ascii(url.substr(0, scheme.size())) != scheme) return "";
    size_t end = url.find_first_of("/?#", scheme.size());
    std::string authority = url.substr(scheme.size(), end == std::string::npos ? std::string::npos : end - scheme.size());
    if (authority.empty() || authority.find('@') != std::string::npos) return "";
    size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        if (authority.substr(colon + 1) != "443") return "";
        authority = authority.substr(0, colon);
    }
    for (unsigned char c : url) {
        if (c <= 0x20 || c == 0x7f) return "";
    }
    return lower_ascii(authority);
}

bool is_allowed_legitimacy_host(const std::string& host, bool for_download) {
    static const char* kHosts[] = {"api.github.com", "github.com", "api.modrinth.com", "modrinth.com", "cdn.modrinth.com",
                                   "api.spiget.org", "spigotmc.org", "www.spigotmc.org", "hangar.papermc.io", "spigotmc.ru",
                                   "raw.githubusercontent.com"};
    for (const char* h : kHosts) {
        if (host == h) return true;
    }
    const std::string suffix = kDownloadZoneSuffix;
    return for_download && host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_allowed_legitimacy_url(const std::string& url, bool for_download = false) {
    std::string host = https_host_of(url);
    return !host.empty() && is_allowed_legitimacy_host(host, for_download);
}

// Возвращает std::nullopt на любой ошибке (таймаут/сеть/ненулевой код
// возврата curl) - зеркалит `except Exception: return None` в оригинале.
std::optional<std::string> http_get(const std::string& url, double timeout_sec, const std::string& accept) {
    if (!is_allowed_legitimacy_url(url)) return std::nullopt;
    double effective_timeout = std::min(timeout_sec, 2.5);
    std::vector<std::string> argv = {"curl", "-s", "--proto", "=https", "--connect-timeout", "2",
                                     "--max-filesize", std::to_string(kMaxHttpBodyBytes),
                                     "-m",   std::to_string(effective_timeout), "-A", kUserAgent, "-H", "Accept: " + accept, url};
    ProcResult r = run_process(argv, ProcOutput::Capture, static_cast<int>(effective_timeout) + 2, kMaxHttpBodyBytes);
    if (!r.started || r.timed_out || r.truncated || r.exit_code != 0) return std::nullopt;
    return std::move(r.out);
}

std::optional<JsonValue> http_get_json(const std::string& url, double timeout_sec) {
    auto raw = http_get(url, timeout_sec, "application/json");
    if (!raw.has_value()) return std::nullopt;
    return json_parse(*raw);
}

// Минимальный urlencode (RFC 3986 unreserved characters оставляем как есть,
// остальное - %XX) - аналог urllib.parse.quote с настройками по умолчанию
// (сохраняет "/", как и urllib.parse.quote(safe="/") по умолчанию).
std::string url_quote(const std::string& s, bool safe_slash = false) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        bool unreserved = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved || (safe_slash && c == '/')) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

}  // namespace

// ---------------- HANDOFF_53: hex-кодирование + скачивание+хэш файла ----------------

namespace {

std::string hex_encode(const std::array<uint8_t, 32>& bytes) {
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t b : bytes) {
        out += hexd[b >> 4];
        out += hexd[b & 0xF];
    }
    return out;
}

// u8path: на Windows путь в UTF-8 (например, временная папка пользователя с
// кириллицей) через голый std::ifstream(std::string) не открывался, а пустое
// чтение давало хэш пустого файла - то есть ложное "хэш НЕ совпадает".
// nullopt - файл не прочитан.
std::optional<std::string> sha256_of_file(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return hex_encode(sha256(data));
}

// Скачивает url во временный файл (лимит через curl --max-filesize) и
// хэширует - используется ТОЛЬКО для GitHub-ассетов (см. спеку - у
// Modrinth хэш и так уже в JSON-ответе, скачивать сам файл не нужно).
// nullopt при любой ошибке (сеть/лимит размера/curl не найден) - как и
// весь остальной сетевой код здесь, тихо деградирует, а не падает.
std::optional<std::string> download_and_sha256(const std::string& url, uint64_t max_bytes, double timeout_sec) {
    if (!is_allowed_legitimacy_url(url, /*for_download=*/true)) return std::nullopt;
    // Приватный каталог со случайным именем (0700): файл в общем /tmp с
    // предсказуемым именем можно было подменить симлинком, пока curl его пишет.
    std::string dir = make_private_temp_dir(fs::temp_directory_path().u8string(), "nd_legitimacy_");
    if (dir.empty()) return std::nullopt;
    std::string tmp = (fs::u8path(dir) / "download.bin").u8string();

    std::vector<std::string> argv = {"curl", "-sL", "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "5",
                                     "-m",   std::to_string(timeout_sec), "--max-filesize", std::to_string(max_bytes),
                                     "-A",   kUserAgent, "-o", tmp, "-w", "%{url_effective}", url};
    ProcResult r = run_process(argv, ProcOutput::Capture, static_cast<int>(timeout_sec) + 5, 8192);
    std::optional<std::string> hash;
    // Адрес ПОСЛЕ редиректов тоже обязан остаться в списке разрешённых.
    if (r.started && !r.timed_out && r.exit_code == 0 && is_allowed_legitimacy_url(r.out, /*for_download=*/true)) {
        std::error_code ec;
        if (fs::is_regular_file(fs::u8path(tmp), ec)) hash = sha256_of_file(tmp);
    }
    std::error_code ec;
    fs::remove_all(fs::u8path(dir), ec);
    return hash;
}

// {plugin_name}/{full_name}/{slug} - буквальная подстановка (НЕ
// шаблонизатор общего назначения - ровно эти три плейсхолдера, см. спеку).
std::string substitute_placeholder(const std::string& tmpl, const std::string& placeholder, const std::string& value) {
    std::string out = tmpl;
    size_t pos;
    while ((pos = out.find(placeholder)) != std::string::npos) out.replace(pos, placeholder.size(), value);
    return out;
}

}  // namespace

// ---------------- HANDOFF_53: мини-язык - имя, конфиг, generic-проверка ----------------

std::optional<SiteKind> parse_site_kind(const std::string& s) {
    if (s == "github_api") return SiteKind::GithubApi;
    if (s == "modrinth_api") return SiteKind::ModrinthApi;
    if (s == "spiget_api") return SiteKind::SpigetApi;
    if (s == "html_search") return SiteKind::HtmlSearch;
    if (s == "hangar_api") return SiteKind::HangarApi;
    return std::nullopt;
}

std::string clean_plugin_name_for_search(const std::string& raw_name) {
    std::string name = raw_name;
    // .strip()
    {
        size_t a = name.find_first_not_of(" \t\r\n");
        size_t b = name.find_last_not_of(" \t\r\n");
        name = (a == std::string::npos) ? "" : name.substr(a, b - a + 1);
    }
    // Отрезаем хвостовой версионный суффикс: разделитель (пробел/дефис/
    // подчёркивание) + опциональная "v"/"V" + цифры/точки, В КОНЦЕ строки.
    // "MyPlugin-v1.2.3" -> "MyPlugin", "MyPlugin_2" -> "MyPlugin",
    // "MyPluginV2" (без разделителя) - НЕ трогаем (см. спеку - только
    // отделённый разделителем суффикс, иначе рискуем испортить настоящее
    // имя вроде "Zone2" или "Node1").
    static const std::regex version_suffix_re(R"([ _\-]+[vV]?\d[\d.]*$)");
    name = std::regex_replace(name, version_suffix_re, "");
    if (name.size() < 2) return "";
    return name;
}

std::vector<SiteConfig> default_legitimacy_sites_config() {
    return {
        // БАГ-ФИКС v1.9.11: было "/releases/latest" (один релиз, объект) -
        // теперь список ВСЕХ релизов (массив, до 10 - GitHub отдаёт
        // новейшие первыми) - см. check_site()/SiteKind::GithubApi для
        // причины и деталей перебора.
        {"GitHub", SiteKind::GithubApi, "github.com", "https://api.github.com/search/repositories?q={plugin_name}+in:name&per_page=5",
         std::optional<std::string>("https://api.github.com/repos/{full_name}/releases?per_page=10")},
        {"Modrinth", SiteKind::ModrinthApi, "modrinth.com", "https://api.modrinth.com/v2/search?query={plugin_name}&limit=5",
         std::optional<std::string>("https://api.modrinth.com/v2/project/{slug}/version")},
        {"SpigotMC", SiteKind::SpigetApi, "spigotmc.org", "https://api.spiget.org/v2/search/resources/{plugin_name}?field=name&size=5",
         std::nullopt},
        // БАГ-ФИКС/БЕЗОПАСНОСТЬ 1.9.6: "RuSpigot" (spigotmc.ru) убран из
        // источников по умолчанию - см. подробное обоснование в hpp. Hangar
        // (hangar.papermc.io) - официальный репозиторий плагинов PaperMC,
        // с честным публичным API (https://hangar.papermc.io/api-docs), без
        // авторизации для чтения. Хэш-сравнение НЕ реализовано (как и для
        // SpigotMC/Spiget) - в подтверждённой структуре ответа Hangar не
        // нашлось поля с хэшем файла версии, только сам файл для скачивания.
        {"Hangar", SiteKind::HangarApi, "hangar.papermc.io", "https://hangar.papermc.io/api/v1/projects?q={plugin_name}&limit=25&offset=0",
         std::nullopt},
    };
}

std::vector<SiteConfig> parse_legitimacy_sites_config(const std::string& json_text) {
    std::vector<SiteConfig> out;
    auto parsed = json_parse(json_text);
    if (!parsed.has_value() || !parsed->is_array()) return out;
    for (auto& item : *parsed->arr_v) {
        if (!item.is_object()) continue;
        const JsonValue* label = item.get("label");
        const JsonValue* kind_s = item.get("kind");
        const JsonValue* site = item.get("site");
        const JsonValue* query = item.get("query");
        if (!label || !label->is_string() || !kind_s || !kind_s->is_string() || !site || !site->is_string() || !query ||
            !query->is_string()) {
            continue;  // невалидная запись - молча пропускаем (см. hpp)
        }
        auto kind = parse_site_kind(kind_s->str_v);
        if (!kind.has_value()) continue;
        SiteConfig cfg;
        cfg.label = label->str_v;
        cfg.kind = *kind;
        cfg.site = site->str_v;
        cfg.query = query->str_v;
        const JsonValue* rq = item.get("release_query");
        if (rq && rq->is_string()) cfg.release_query = rq->str_v;
        // Конфиг приходит из ИНТЕРНЕТА (raw.githubusercontent.com): шаблоны запросов
        // обязаны указывать на https и на известные сервисы - иначе движок можно
        // было направить на любой (в т.ч. внутренний) адрес.
        if (!is_allowed_legitimacy_url(cfg.query)) continue;
        if (cfg.release_query.has_value() && !is_allowed_legitimacy_url(*cfg.release_query)) continue;
        out.push_back(std::move(cfg));
    }
    return out;
}

namespace {
// БАГ-ФИКС (HANDOFF_53 -> закрыто): раньше здесь была заглушка
// USER/REPO. Реальный репозиторий проекта - NanoDev1488/NanoDecompiler
// (те же константы GITHUB_OWNER/GITHUB_REPO используются в
// electron/updater.ts для проверки обновлений) - файл legitimacy_sites.json
// лежит в корне репозитория, ветка main. Раньше curl молча получал 404 и
// движок деградировал на default_legitimacy_sites_config() - деградация
// была безопасной, но саму фичу "менять источники без пересборки" никто
// не включал. Теперь URL настоящий.
constexpr const char* LEGITIMACY_SITES_CONFIG_URL = "https://raw.githubusercontent.com/NanoDev1488/NanoDecompiler/main/legitimacy_sites.json";
}  // namespace

std::vector<SiteConfig> load_legitimacy_sites_config() {
    std::string cache_path = (fs::u8path(get_tools_dir()) / ".." / "legitimacy_sites.json").lexically_normal().u8string();

    auto fresh = http_get(LEGITIMACY_SITES_CONFIG_URL, 4.0, "application/json");
    if (fresh.has_value()) {
        auto parsed = parse_legitimacy_sites_config(*fresh);
        if (!parsed.empty()) {
            std::error_code ec;
            fs::create_directories(fs::u8path(cache_path).parent_path(), ec);
            std::ofstream f(cache_path, std::ios::binary);
            f << *fresh;
            return parsed;
        }
    }

    std::error_code ec;
    if (fs::is_regular_file(cache_path, ec)) {
        std::ifstream f(cache_path, std::ios::binary);
        std::string cached((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto parsed = parse_legitimacy_sites_config(cached);
        if (!parsed.empty()) return parsed;
    }

    return default_legitimacy_sites_config();
}

// ---------------- HANDOFF_53: универсальная проверка по SiteConfig ----------------

LegitimacySourceResult check_site(const SiteConfig& cfg, const std::string& plugin_name, const std::string& jar_sha256_hex,
                                   double timeout_sec) {
    LegitimacySourceResult out;
    if (plugin_name.empty()) return out;
    std::string q = url_quote(plugin_name, true);
    std::string url = substitute_placeholder(cfg.query, "{plugin_name}", q);

    // НОВОЕ v1.9.13 (host-reachability probe): лёгкий GET на стабильный
    // корневой эндпоинт ПЕРЕД основным поиском - чтобы отличить
    // "не найдено" от "источник недоступен/гео-блок". Probe-URL выбраны
    // как самые дешёвые и стабильные эндпоинты каждого API:
    // - GitHub: /rate_limit (всегда 200, без auth, минимальный ответ)
    // - Modrinth: /v2/tag/category (фиксированный список, не меняется)
    // - Spigot (SpigetApi): /v2/resources?size=1&page=1 (страница из 1
    //   элемента - минимальный валидный ответ Spiget API)
    // - Hangar: /v1/projects?limit=1&offset=0 (та же идея)
    // HtmlSearch - не зондируем (нет простого JSON-маркера "я работаю").
    // Таймаут probe намеренно вдвое короче основного: если probe не
    // ответил за 2с - источник точно недоступен, дальше не тратим время.
    static const std::unordered_map<SiteKind, std::string> kProbeUrls = {
        {SiteKind::GithubApi,   "https://api.github.com/rate_limit"},
        {SiteKind::ModrinthApi, "https://api.modrinth.com/v2/tag/category"},
        {SiteKind::SpigetApi,   "https://api.spiget.org/v2/resources?size=1&page=1"},
        {SiteKind::HangarApi,   "https://hangar.papermc.io/api/v1/projects?limit=1&offset=0"},
    };
    auto probe_it = kProbeUrls.find(cfg.kind);
    if (probe_it != kProbeUrls.end()) {
        auto probe_resp = http_get_json(probe_it->second, std::max(timeout_sec / 2.0, 2.0));
        out.host_reachable = probe_resp.has_value();
        // Если probe не прошёл - источник недоступен. Помечаем checked=true
        // (попытка была сделана), но дальше не идём - поиск точно провалится
        // с той же ошибкой, только медленнее.
        if (!out.host_reachable.value()) {
            out.checked = true;  // попытка была; found остаётся false
            return out;
        }
    }

    if (cfg.kind == SiteKind::GithubApi) {
        auto data = http_get_json(url, timeout_sec);
        if (!data.has_value() || !data->is_object() || data->get("items") == nullptr) return out;
        out.checked = true;
        const JsonValue* items = data->get("items");
        if (items && items->is_array()) {
            size_t k = 0;
            for (auto& item : *items->arr_v) {
                if (k >= 5) break;
                const JsonValue* full_name = item.get("full_name");
                const JsonValue* html_url = item.get("html_url");
                if (!full_name || !html_url || !full_name->is_string() || !html_url->is_string()) continue;
                LegitimacyCandidate c;
                c.full_name = full_name->str_v;
                c.url = html_url->str_v;
                const JsonValue* stars = item.get("stargazers_count");
                c.stars = (stars && stars->kind == JsonValue::Kind::Number) ? static_cast<int64_t>(stars->num_v) : 0;
                // БАГ-ФИКС v1.9.11 (жалоба, волна 5 - "хэш должен браться из
                // ВСЕХ найденных проектов и ВСЕХ их релизов, а не только
                // последнего"): раньше release_query всегда указывал на
                // GET /releases/latest (см. default_legitimacy_sites_config)
                // и брался ТОЛЬКО первый asset этого ОДНОГО релиза - если
                // пользователь качал не самую свежую версию, или искомый
                // файл был вторым/третьим ассетом релиза (напр. отдельно
                // jar + sources.jar), хэш никогда бы не совпал, даже если
                // файл подлинный. Теперь release_query указывает на список
                // ВСЕХ релизов (см. правку ниже в default_legitimacy_sites_
                // config + LEGITIMACY_SITES_MINI_LANGUAGE_SPEC.md), и здесь
                // перебираются ВСЕ релизы И ВСЕ ассеты каждого - с ранним
                // выходом при первом совпадении хэша (не тратим сеть/время
                // дальше, как только нашли совпадение). Кап на количество
                // релизов/ассетов - защита от неограниченной сетевой
                // нагрузки на очень старых/активных репозиториях, а не
                // произвольное ограничение "только последнее".
                if (!jar_sha256_hex.empty() && cfg.release_query.has_value()) {
                    std::string rel_url = substitute_placeholder(*cfg.release_query, "{full_name}", c.full_name);
                    auto rel = http_get_json(rel_url, timeout_sec);
                    // Обратная совместимость: старый release_query (в
                    // локальном кэше/пользовательском конфиге до 1.9.11)
                    // мог указывать на /releases/latest - тогда ответ ОБЪЕКТ,
                    // не массив. Оборачиваем в одноэлементный "массив", чтобы
                    // код ниже работал одинаково для обоих случаев.
                    std::vector<const JsonValue*> releases;
                    if (rel.has_value() && rel->is_array()) {
                        for (auto& r : *rel->arr_v) releases.push_back(&r);
                    } else if (rel.has_value() && rel->is_object()) {
                        releases.push_back(&*rel);
                    }
                    constexpr size_t kMaxReleasesChecked = 10;
                    constexpr size_t kMaxAssetsPerRelease = 10;
                    bool matched = false;
                    for (size_t ri = 0; ri < releases.size() && ri < kMaxReleasesChecked && !matched; ++ri) {
                        const JsonValue* assets = releases[ri]->get("assets");
                        if (!assets || !assets->is_array()) continue;
                        size_t ai = 0;
                        for (auto& asset : *assets->arr_v) {
                            if (ai >= kMaxAssetsPerRelease) break;
                            const JsonValue* dl = asset.get("browser_download_url");
                            if (dl && dl->is_string()) {
                                auto digest = download_and_sha256(dl->str_v, 50ULL * 1024 * 1024, timeout_sec);
                                if (digest.has_value()) {
                                    c.sha256_hex = digest;
                                    if (*digest == jar_sha256_hex) { matched = true; break; }
                                }
                            }
                            ai += 1;
                        }
                    }
                }
                out.candidates.push_back(std::move(c));
                k += 1;
            }
        }
        out.found = !out.candidates.empty();
        return out;
    }

    if (cfg.kind == SiteKind::ModrinthApi) {
        auto data = http_get_json(url, timeout_sec);
        if (!data.has_value() || !data->is_object() || data->get("hits") == nullptr) return out;
        out.checked = true;
        const JsonValue* hits = data->get("hits");
        if (hits && hits->is_array()) {
            size_t k = 0;
            for (auto& item : *hits->arr_v) {
                if (k >= 5) break;
                const JsonValue* title = item.get("title");
                const JsonValue* slug = item.get("slug");
                if (!title || !slug || !title->is_string() || !slug->is_string()) continue;
                LegitimacyCandidate c;
                c.full_name = title->str_v;
                c.url = "https://modrinth.com/plugin/" + slug->str_v;
                const JsonValue* downloads = item.get("downloads");
                c.stars = (downloads && downloads->kind == JsonValue::Kind::Number) ? static_cast<int64_t>(downloads->num_v) : 0;
                // БАГ-ФИКС v1.9.11 (та же жалоба, что и у GitHub выше - см.
                // комментарий там): раньше брали только files[0] самой
                // свежей (rel[0]) версии - теперь перебираем ВСЕ версии И
                // ВСЕ файлы каждой, ранний выход при первом совпадении.
                // Modrinth отдаёт хэш НАПРЯМУЮ в JSON - скачивать сам файл
                // не нужно (см. спеку), поэтому кап здесь намного щедрее,
                // чем у GitHub (нет реальной сетевой цены за лишние версии).
                if (!jar_sha256_hex.empty() && cfg.release_query.has_value()) {
                    std::string rel_url = substitute_placeholder(*cfg.release_query, "{slug}", slug->str_v);
                    auto rel = http_get_json(rel_url, timeout_sec);
                    if (rel.has_value() && rel->is_array()) {
                        constexpr size_t kMaxVersionsChecked = 50;
                        bool matched = false;
                        size_t vi = 0;
                        for (auto& ver : *rel->arr_v) {
                            if (vi >= kMaxVersionsChecked || matched) break;
                            const JsonValue* files = ver.get("files");
                            if (files && files->is_array()) {
                                for (auto& file : *files->arr_v) {
                                    const JsonValue* hashes = file.get("hashes");
                                    const JsonValue* sha256_v = hashes ? hashes->get("sha256") : nullptr;
                                    if (sha256_v && sha256_v->is_string()) {
                                        c.sha256_hex = sha256_v->str_v;
                                        if (sha256_v->str_v == jar_sha256_hex) { matched = true; break; }
                                    }
                                }
                            }
                            vi += 1;
                        }
                    }
                }
                out.candidates.push_back(std::move(c));
                k += 1;
            }
        }
        out.found = !out.candidates.empty();
        return out;
    }

    if (cfg.kind == SiteKind::SpigetApi) {
        auto data = http_get_json(url, timeout_sec);
        if (!data.has_value() || !data->is_array()) return out;
        out.checked = true;
        size_t k = 0;
        for (auto& item : *data->arr_v) {
            if (k >= 5) break;
            const JsonValue* id = item.get("id");
            if (!id || id->kind != JsonValue::Kind::Number) continue;
            int64_t rid = static_cast<int64_t>(id->num_v);
            LegitimacyCandidate c;
            const JsonValue* name = item.get("name");
            c.full_name = (name && name->is_string()) ? name->str_v : ("resource #" + std::to_string(rid));
            c.url = "https://www.spigotmc.org/resources/" + std::to_string(rid) + "/";
            const JsonValue* downloads = item.get("downloads");
            c.stars = (downloads && downloads->kind == JsonValue::Kind::Number) ? static_cast<int64_t>(downloads->num_v) : 0;
            // HANDOFF_53: хэш НЕ сравнивается для этого источника - см. спеку.
            out.candidates.push_back(std::move(c));
            k += 1;
        }
        out.found = !out.candidates.empty();
        return out;
    }

    if (cfg.kind == SiteKind::HangarApi) {
        // НОВОЕ 1.9.6: ответ Hangar - {"result": [{"name":.., "namespace":
        // {"owner":.., "slug":..}, "stats": {"stars":..}, ...}]} - структура
        // подтверждена веб-поиском (hangar.papermc.io/api-docs, реальные
        // примеры использования), НЕ протестирована вживую (нет сети в
        // песочнице этой сессии - см. общую оговорку в legitimacy_check.hpp).
        auto data = http_get_json(url, timeout_sec);
        if (!data.has_value() || !data->is_object() || data->get("result") == nullptr) return out;
        out.checked = true;
        const JsonValue* result_arr = data->get("result");
        if (result_arr && result_arr->is_array()) {
            size_t k = 0;
            for (auto& item : *result_arr->arr_v) {
                if (k >= 5) break;
                const JsonValue* name = item.get("name");
                const JsonValue* ns = item.get("namespace");
                const JsonValue* owner = ns ? ns->get("owner") : nullptr;
                const JsonValue* slug = ns ? ns->get("slug") : nullptr;
                if (!name || !name->is_string() || !owner || !owner->is_string() || !slug || !slug->is_string()) continue;
                LegitimacyCandidate c;
                c.full_name = name->str_v;
                c.url = "https://hangar.papermc.io/" + owner->str_v + "/" + slug->str_v;
                const JsonValue* stats = item.get("stats");
                const JsonValue* stars = stats ? stats->get("stars") : nullptr;
                c.stars = (stars && stars->kind == JsonValue::Kind::Number) ? static_cast<int64_t>(stars->num_v) : 0;
                // HANDOFF_53/1.9.6: хэш НЕ сравнивается для этого источника
                // (нет подтверждённого поля хэша в ответе Hangar) - та же
                // оговорка, что и у SpigotMC/Spiget выше.
                out.candidates.push_back(std::move(c));
                k += 1;
            }
        }
        out.found = !out.candidates.empty();
        return out;
    }

    // SiteKind::HtmlSearch
    auto html = http_get(url, timeout_sec, "text/html");
    if (!html.has_value()) return out;
    out.checked = true;
    // std::regex в libstdc++ рекурсивен: на больших входах переполняет стек - режем HTML.
    if (html->size() > 262144) html->resize(262144);
    auto links = ruspigot_parse_result_links(*html);
    size_t k = 0;
    for (auto& [href, title_raw] : links) {
        if (k >= 5) break;
        std::string title = title_raw;
        size_t a = title.find_first_not_of(" \t\r\n");
        size_t b = title.find_last_not_of(" \t\r\n");
        title = (a == std::string::npos) ? "" : title.substr(a, b - a + 1);
        if (title.empty()) continue;
        LegitimacyCandidate c;
        c.full_name = title;
        c.url = (href.rfind("https://", 0) == 0 || href.rfind("http://", 0) == 0) ? href : ("https://" + cfg.site + href);
        // HANDOFF_53: хэш НЕ сравнивается для этого источника - см. спеку.
        out.candidates.push_back(std::move(c));
        k += 1;
    }
    out.found = !out.candidates.empty();
    return out;
}

// ---------------- check_github / check_modrinth / check_spigot (сохранены для
// обратной совместимости - см. HANDOFF_32; run_legitimacy_check теперь
// использует check_site() выше с конфигом из load_legitimacy_sites_config()) ----------------

LegitimacySourceResult check_github(const std::string& plugin_name, double timeout_sec) {
    LegitimacySourceResult out;
    if (plugin_name.empty()) return out;
    std::string q = url_quote(plugin_name + " in:name", true);
    auto data = http_get_json("https://api.github.com/search/repositories?q=" + q + "&per_page=5", timeout_sec);
    if (!data.has_value() || !data->is_object() || data->get("items") == nullptr) return out;
    out.checked = true;
    const JsonValue* items = data->get("items");
    if (items && items->is_array()) {
        size_t k = 0;
        for (auto& item : *items->arr_v) {
            if (k >= 5) break;
            const JsonValue* full_name = item.get("full_name");
            const JsonValue* html_url = item.get("html_url");
            if (!full_name || !html_url || !full_name->is_string() || !html_url->is_string()) continue;
            LegitimacyCandidate c;
            c.full_name = full_name->str_v;
            c.url = html_url->str_v;
            const JsonValue* stars = item.get("stargazers_count");
            c.stars = (stars && stars->kind == JsonValue::Kind::Number) ? static_cast<int64_t>(stars->num_v) : 0;
            out.candidates.push_back(std::move(c));
            k += 1;
        }
    }
    out.found = !out.candidates.empty();
    return out;
}

LegitimacySourceResult check_modrinth(const std::string& plugin_name, double timeout_sec) {
    LegitimacySourceResult out;
    if (plugin_name.empty()) return out;
    std::string q = url_quote(plugin_name, true);
    auto data = http_get_json("https://api.modrinth.com/v2/search?query=" + q + "&limit=5", timeout_sec);
    if (!data.has_value() || !data->is_object() || data->get("hits") == nullptr) return out;
    out.checked = true;
    const JsonValue* hits = data->get("hits");
    if (hits && hits->is_array()) {
        size_t k = 0;
        for (auto& item : *hits->arr_v) {
            if (k >= 5) break;
            const JsonValue* title = item.get("title");
            const JsonValue* slug = item.get("slug");
            if (!title || !slug || !title->is_string() || !slug->is_string()) continue;
            LegitimacyCandidate c;
            c.full_name = title->str_v;
            c.url = "https://modrinth.com/plugin/" + slug->str_v;
            const JsonValue* downloads = item.get("downloads");
            c.stars = (downloads && downloads->kind == JsonValue::Kind::Number) ? static_cast<int64_t>(downloads->num_v) : 0;
            out.candidates.push_back(std::move(c));
            k += 1;
        }
    }
    out.found = !out.candidates.empty();
    return out;
}

LegitimacySourceResult check_spigot(const std::string& plugin_name, double timeout_sec) {
    LegitimacySourceResult out;
    if (plugin_name.empty()) return out;
    std::string q = url_quote(plugin_name, true);
    auto data = http_get_json("https://api.spiget.org/v2/search/resources/" + q + "?field=name&size=5", timeout_sec);
    if (!data.has_value() || !data->is_array()) return out;
    out.checked = true;
    size_t k = 0;
    for (auto& item : *data->arr_v) {
        if (k >= 5) break;
        const JsonValue* id = item.get("id");
        if (!id || id->kind != JsonValue::Kind::Number) continue;
        int64_t rid = static_cast<int64_t>(id->num_v);
        LegitimacyCandidate c;
        const JsonValue* name = item.get("name");
        c.full_name = (name && name->is_string()) ? name->str_v : ("resource #" + std::to_string(rid));
        c.url = "https://www.spigotmc.org/resources/" + std::to_string(rid) + "/";
        const JsonValue* downloads = item.get("downloads");
        c.stars = (downloads && downloads->kind == JsonValue::Kind::Number) ? static_cast<int64_t>(downloads->num_v) : 0;
        out.candidates.push_back(std::move(c));
        k += 1;
    }
    out.found = !out.candidates.empty();
    return out;
}

// ---------------- RuSpigot (regex по HTML - офлайн тестируемо отдельно) ----------------

std::vector<std::pair<std::string, std::string>> ruspigot_parse_result_links(const std::string& html) {
    // re.compile(r'<a[^>]+class="[^"]*resource-tile-title[^"]*"[^>]+href="([^"]+)"[^>]*>\s*([^<]+?)\s*</a>', re.I)
    // Порядок групп в Python-регэкспе: (href, title) - возвращаем в том же порядке.
    static const std::regex re(
        R"RE(<a[^>]+class="[^"]*resource-tile-title[^"]*"[^>]+href="([^"]+)"[^>]*>\s*([^<]+?)\s*</a>)RE",
        std::regex::icase);
    std::vector<std::pair<std::string, std::string>> out;
    auto begin = std::sregex_iterator(html.begin(), html.end(), re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        out.emplace_back((*it)[1].str(), (*it)[2].str());
    }
    return out;
}

LegitimacySourceResult check_ruspigot(const std::string& plugin_name, double timeout_sec) {
    LegitimacySourceResult out;
    if (plugin_name.empty()) return out;
    std::string q = url_quote(plugin_name, true);
    auto html = http_get("https://spigotmc.ru/resources/?q=" + q, timeout_sec, "text/html");
    if (!html.has_value()) return out;
    out.checked = true;
    if (html->size() > 262144) html->resize(262144);
    auto links = ruspigot_parse_result_links(*html);
    size_t k = 0;
    for (auto& [href, title_raw] : links) {
        if (k >= 5) break;
        // title.strip()
        std::string title = title_raw;
        size_t a = title.find_first_not_of(" \t\r\n");
        size_t b = title.find_last_not_of(" \t\r\n");
        title = (a == std::string::npos) ? "" : title.substr(a, b - a + 1);
        if (title.empty()) continue;
        LegitimacyCandidate c;
        c.full_name = title;
        c.url = (href.rfind("https://", 0) == 0 || href.rfind("http://", 0) == 0) ? href : ("https://spigotmc.ru" + href);
        c.stars = 0;
        out.candidates.push_back(std::move(c));
        k += 1;
    }
    out.found = !out.candidates.empty();
    return out;
}

// ---------------- run_legitimacy_check / format_for_console ----------------

LegitimacyCheckResult run_legitimacy_check(const std::string& plugin_name, const std::string& plugin_yml_text, const std::string& jar_path) {
    LegitimacyCheckResult result;
    result.plugin_yml_fields = fields_from_plugin_yml(plugin_yml_text);

    // HANDOFF_53: хэш проверяемого jar - для сравнения с хэшами кандидатов
    // (см. спеку). Пусто, если jar_path не передан ИЛИ файл не читается -
    // сравнение хэшей просто не делается (hash_comparison = nullopt),
    // остальная проверка (поиск похожих проектов) продолжается как обычно.
    std::string jar_sha256_hex;
    if (!jar_path.empty()) {
        if (auto h = sha256_of_file(jar_path)) jar_sha256_hex = *h;
    }

    // HANDOFF_53: очистка имени - ОДИН раз на все источники (см. спеку -
    // результат очистки от источника не зависит). Пустая строка после
    // очистки -> ни один источник не запрашивается вообще (все 4 поля
    // результата останутся checked=false, как и раньше при пустом имени).
    std::string cleaned = clean_plugin_name_for_search(plugin_name);

    // HANDOFF_53: конфиг источников теперь дан данными, не хардкодом (см.
    // load_legitimacy_sites_config) - количество/состав источников МОЖЕТ
    // отличаться от ровно 4 стандартных (пользователь может отредактировать
    // конфиг на GitHub) - но result.github/modrinth/spigot/ruspigot
    // остаются ФИКСИРОВАННЫМИ 4 полями (см. hpp) ради обратной
    // совместимости с form_for_console/stats_json.cpp - сопоставление по
    // cfg.kind; при нескольких записях одного kind в конфиге побеждает
    // последняя (простое детерминированное поведение, не ошибка).
    std::vector<SiteConfig> sites = load_legitimacy_sites_config();

    // БАГ-ФИКС: std::async(launch::async) возвращает future, деструктор которого БЛОКИРУЕТ
    // до конца задачи - "общий верхний предел 8с" не работал: после wait_for функция
    // всё равно ждала все зависшие сетевые задачи при выходе из области видимости
    // (а у GitHub-ветки может быть до сотни скачиваний). Теперь задачи - отсоединённые
    // потоки с promise, а ожидание ограничено ОДНИМ общим дедлайном (раньше 8с
    // отсчитывались заново для каждого источника по очереди).
    struct Pending {
        SiteKind kind;
        std::future<LegitimacySourceResult> fut;
    };
    std::vector<Pending> pending;
    for (auto& cfg : sites) {
        auto prom = std::make_shared<std::promise<LegitimacySourceResult>>();
        pending.push_back({cfg.kind, prom->get_future()});
        std::thread([prom, cfg, cleaned, jar_sha256_hex] {
            try {
                prom->set_value(check_site(cfg, cleaned, jar_sha256_hex));
            } catch (...) {
                try {
                    prom->set_exception(std::current_exception());
                } catch (...) {
                }
            }
        }).detach();
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    for (auto& item : pending) {
        SiteKind kind = item.kind;
        LegitimacySourceResult r;
        if (item.fut.wait_until(deadline) == std::future_status::ready) {
            try {
                r = item.fut.get();
            } catch (...) {
                continue;
            }
        } else {
            continue;  // общий верхний предел 8с, как и в оригинале
        }
        switch (kind) {
            case SiteKind::GithubApi:
                result.github = r;
                break;
            case SiteKind::ModrinthApi:
                result.modrinth = r;
                break;
            case SiteKind::SpigetApi:
                result.spigot = r;
                break;
            case SiteKind::HtmlSearch:
                break;  // НЕ используется по умолчанию с 1.9.6 (см. hpp) - если пользователь сам добавит html_search в конфиг, отдельного поля для него нет (та же оговорка про "фиксированные 4 поля", что и раньше)
            case SiteKind::HangarApi:
                result.hangar = r;
                break;
        }
    }

    // HANDOFF_53: сравнение хэшей - по ВСЕМ кандидатам всех 4 источников,
    // у которых sha256_hex реально получен (см. спеку - сейчас это только
    // GitHub/Modrinth).
    if (!jar_sha256_hex.empty()) {
        HashComparisonResult hc;
        struct Src {
            const char* label;
            const LegitimacySourceResult* r;
        };
        Src srcs[] = {{"GitHub", &result.github}, {"Modrinth", &result.modrinth}, {"SpigotMC", &result.spigot}, {"Hangar", &result.hangar}};
        for (auto& s : srcs) {
            for (auto& c : s.r->candidates) {
                if (!c.sha256_hex.has_value()) continue;
                bool match = (*c.sha256_hex == jar_sha256_hex);
                (match ? hc.matching : hc.mismatching).push_back(s.label);
            }
        }
        if (!hc.matching.empty() || !hc.mismatching.empty()) result.hash_comparison = std::move(hc);
    }

    return result;
}

std::optional<std::string> format_for_console(const LegitimacyCheckResult& result) {
    std::vector<std::string> lines;
    auto& fields = result.plugin_yml_fields;
    if (fields.website.has_value() || !fields.authors.empty()) {
        lines.push_back("[*] Поля в plugin.yml:");
        if (fields.website.has_value()) lines.push_back("    website: " + *fields.website);
        if (!fields.authors.empty()) {
            std::string joined;
            for (size_t i = 0; i < fields.authors.size(); ++i) {
                if (i) joined += ", ";
                joined += fields.authors[i];
            }
            lines.push_back("    author(s): " + joined);
        }
    }
    struct Src { const char* key; const char* label; const LegitimacySourceResult* r; };
    Src sources[] = {
        {"github", "GitHub", &result.github},
        {"modrinth", "Modrinth", &result.modrinth},
        {"spigot", "SpigotMC", &result.spigot},
        {"hangar", "Hangar", &result.hangar},
    };
    for (auto& s : sources) {
        if (!s.r->checked) {
            lines.push_back(std::string("[*] ") + s.label + ": проверка не удалась (нет сети или сервис недоступен)");
        } else if (s.r->found) {
            lines.push_back(std::string("[*] ") + s.label + ": найдены похожие проекты -");
            for (auto& c : s.r->candidates) {
                std::string suffix;
                if (c.sha256_hex.has_value()) suffix = "  [sha256: " + c.sha256_hex->substr(0, 12) + "…]";
                lines.push_back("    " + c.full_name + " (" + c.url + ")" + suffix);
            }
        } else {
            lines.push_back(std::string("[*] ") + s.label + ": похожих проектов не найдено");
        }
    }
    // HANDOFF_53: вердикт по сравнению хэшей (см. спеку) - печатается,
    // только если ХОТЯ БЫ один источник реально дал хэш для сравнения.
    if (result.hash_comparison.has_value()) {
        auto& hc = *result.hash_comparison;
        auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) s += ", ";
                s += v[i];
            }
            return s;
        };
        if (!hc.matching.empty() && hc.mismatching.empty()) {
            lines.push_back("[*] Хэш jar совпадает с найденным на: " + join(hc.matching) + " - похоже на официальную сборку.");
        } else if (hc.matching.empty() && !hc.mismatching.empty()) {
            lines.push_back("[!] ВНИМАНИЕ: хэш jar НЕ совпадает ни с одним найденным источником (" + join(hc.mismatching) +
                             ") - возможна модифицированная/пиратская сборка.");
        } else if (!hc.matching.empty() && !hc.mismatching.empty()) {
            lines.push_back("[!] Хэш jar не соответствует: " + join(hc.mismatching) + ", НО соответствует: " + join(hc.matching) +
                             " - возможно, устаревшая версия или сборка с другого зеркала.");
        }
    }
    if (lines.empty()) return std::nullopt;
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

}  // namespace nd

// api.cpp - см. api.hpp.
#include <cstdint>  // БАГ-ФИКС: MinGW/Windows не тянет int64_t транзитивно через другие заголовки, как это молча делает libstdc++ на Linux - см. ошибку сборки Windows-раннера в этой сессии.
#include "api.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

#include "json_value.hpp"
#include "proc_util.hpp"
#include "process_jar.hpp"
#include "stats_json.hpp"
#include "version.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define ND_CLOSESOCK closesocket
#define ND_INVALID_SOCK INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
#define ND_CLOSESOCK close
#define ND_INVALID_SOCK (-1)
#endif

namespace fs = std::filesystem;

namespace nd {

namespace {

// Единая точка вызова декомпиляции для обоих режимов (--json-output и
// HTTP /decompile) - порт api.py::decompile_silent(). В python это ещё и
// перехватывало консольный вывод process_jar_with_stats() через
// redirect_stdout - здесь process_jar_with_stats() и так МОЛЧА работает
// (см. process_jar.hpp, HANDOFF_44), перехватывать нечего.
std::string decompile_silent_json(const std::string& jar_path, const std::string& out_dir, bool skip_legitimacy) {
    auto t0 = std::chrono::steady_clock::now();
    try {
        std::string lower_path = jar_path;
        for (char& c : lower_path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool is_single_class = (lower_path.size() >= 6 && lower_path.substr(lower_path.size() - 6) == ".class");

        JarProcessResult jr = is_single_class 
            ? process_single_class(jar_path, out_dir, /*print_progress=*/false)
            : process_jar_with_stats(jar_path, out_dir, skip_legitimacy, /*print_progress=*/false);
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return jar_process_result_to_json(jr, out_dir, elapsed);
    } catch (const std::exception& e) {
        return json_error_response(std::string(e.what()));
    }
}

}  // namespace

int run_json_output(const std::string& jar_path, const std::string& out_dir, bool skip_legitimacy) {
    std::string json = decompile_silent_json(jar_path, out_dir, skip_legitimacy);
    std::cout << json << "\n";
    // status:"ok" всегда идёт первым полем - дешёвая проверка без полного
    // JSON-парсинга (зеркалит `result.get("status") == "ok"` оригинала).
    return json.rfind("{\"status\":\"ok\"", 0) == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------
// HTTP-сервер (см. предупреждение в api.hpp про WinSock2-ветку).
//
// БЕЗОПАСНОСТЬ (исправлено в 2.0.0-alpha). Раньше:
//  - неверный --host (опечатка) молча превращался в 0.0.0.0 (inet_pton не проверялся);
//  - сервер без какой-либо авторизации мог слушать все интерфейсы: любой в сети
//    заставлял его декомпилировать произвольный файл и писать результат в
//    произвольную папку;
//  - std::stoul(Content-Length) бросал исключение в потоке -> std::terminate
//    (падал весь процесс), а размер тела не ограничивался (4 ГиБ в память);
//  - на чтение не было таймаута (зависшие клиенты копили потоки), число потоков
//    не ограничивалось;
//  - ответ отправлялся одним send() без цикла (большие ответы обрезались) и без
//    защиты от SIGPIPE;
//  - url_decode: последний %XX не декодировался, мусор после % превращался в NUL;
//  - временная папка по умолчанию общая и предсказуемая (<tmp>/<имя>_decompiled).
// ---------------------------------------------------------------------
namespace {

constexpr size_t kMaxBodyBytes = 512u * 1024u * 1024u;  // загружаемый .jar
constexpr size_t kMaxHeaderBytes = 64u * 1024u;
constexpr int kMaxConcurrentConnections = 8;
constexpr int kRecvTimeoutSeconds = 30;
constexpr int64_t kStaleApiDirMaxAgeSec = 24 * 60 * 60;

std::atomic<int> g_active_connections{0};
std::string g_api_token;  // пусто - авторизация выключена (только для loopback)

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string content_type;
    std::string host;
    std::string authorization;
    std::vector<uint8_t> body;
};

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex_val(s[i + 1]) >= 0 && hex_val(s[i + 2]) >= 0) {
            out += static_cast<char>(hex_val(s[i + 1]) * 16 + hex_val(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

std::optional<std::string> query_param(const std::string& query, const std::string& key) {
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && url_decode(pair.substr(0, eq)) == key) return url_decode(pair.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return std::nullopt;
}

#ifdef _WIN32
constexpr int kSendFlags = 0;
#elif defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;  // macOS: SO_NOSIGPIPE выставляется на сокете
#endif

bool send_all(socket_t fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = static_cast<int>(::send(fd, data + sent, static_cast<int>(std::min<size_t>(len - sent, 1u << 20)), kSendFlags));
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

const char* status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 413: return "Payload Too Large";
        case 503: return "Service Unavailable";
        default: return "Internal Server Error";
    }
}

void send_json(socket_t fd, int code, const std::string& body) {
    std::ostringstream head;
    head << "HTTP/1.1 " << code << " " << status_text(code) << "\r\n";
    head << "Content-Type: application/json; charset=utf-8\r\n";
    head << "Content-Length: " << body.size() << "\r\n";
    head << "Cache-Control: no-store\r\n";
    head << "X-Content-Type-Options: nosniff\r\n";
    head << "Connection: close\r\n\r\n";
    std::string h = head.str();
    if (send_all(fd, h.data(), h.size())) send_all(fd, body.data(), body.size());
}

std::string lower_copy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Читает и разбирает ОДИН HTTP-запрос из сокета (запросная строка +
// заголовки до \r\n\r\n, затем ровно Content-Length байт тела) - без
// keep-alive/chunked (оригинал на BaseHTTPRequestHandler тоже не
// поддерживал chunked для входящих запросов).
// too_large выставляется, если тело превышает kMaxBodyBytes.
std::optional<HttpRequest> read_request(socket_t fd, bool& too_large) {
    too_large = false;
    std::string buf;
    char chunk[4096];
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        int n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) return std::nullopt;
        buf.append(chunk, static_cast<size_t>(n));
        header_end = buf.find("\r\n\r\n");
        if (buf.size() > kMaxHeaderBytes && header_end == std::string::npos) return std::nullopt;  // защита от мусора без конца заголовков
    }
    std::string headers_part = buf.substr(0, header_end);
    std::string body_so_far = buf.substr(header_end + 4);

    std::istringstream hs(headers_part);
    std::string request_line;
    std::getline(hs, request_line);
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();
    std::istringstream rl(request_line);
    HttpRequest req;
    std::string http_ver;
    std::string full_path;
    rl >> req.method >> full_path >> http_ver;
    if (req.method.empty() || full_path.empty() || full_path[0] != '/') return std::nullopt;
    auto qpos = full_path.find('?');
    if (qpos == std::string::npos) {
        req.path = full_path;
    } else {
        req.path = full_path.substr(0, qpos);
        req.query = full_path.substr(qpos + 1);
    }

    size_t content_length = 0;
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        size_t b = val.find_first_not_of(' ');
        val = (b == std::string::npos) ? "" : val.substr(b);
        for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (key == "content-length") {
            // Раньше std::stoul(...) мог бросить исключение в потоке сокета и уронить процесс.
            if (val.empty() || val.size() > 12 || val.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
            unsigned long long parsed = std::strtoull(val.c_str(), nullptr, 10);
            if (parsed > kMaxBodyBytes) {
                too_large = true;
                return std::nullopt;
            }
            content_length = static_cast<size_t>(parsed);
        }
        if (key == "content-type") req.content_type = val;
        if (key == "host") req.host = val;
        if (key == "authorization") req.authorization = val;
    }

    if (body_so_far.size() > content_length) body_so_far.resize(content_length);
    req.body.assign(body_so_far.begin(), body_so_far.end());
    while (req.body.size() < content_length) {
        int n = ::recv(fd, chunk, static_cast<int>(std::min(sizeof(chunk), content_length - req.body.size())), 0);
        if (n <= 0) return std::nullopt;  // обрыв/таймаут: неполное тело не обрабатываем
        req.body.insert(req.body.end(), chunk, chunk + n);
    }
    return req;
}

// Уникальная папка результата по умолчанию: <tmp>/NanoDecompiler/api/<имя>_<случайный>_decompiled.
// Раньше - общая предсказуемая <tmp>/<имя>_decompiled: два запроса с одним именем
// затирали друг другу результат, а в общем /tmp её можно было занять заранее.
std::string default_out_dir_for(const std::string& jar_path) {
    std::string base = fs::u8path(jar_path).stem().u8string();
    for (char& c : base) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') c = '_';
    }
    if (base.size() > 60) base.resize(60);
    fs::path root = fs::temp_directory_path() / "NanoDecompiler" / "api";
    sweep_stale_subdirs(root.u8string(), kStaleApiDirMaxAgeSec);
    std::error_code ec;
    fs::create_directories(root, ec);
    return (root / (base + "_" + secure_random_hex(12) + "_decompiled")).u8string();
}

void handle_health(socket_t fd) { send_json(fd, 200, std::string("{\"status\":\"ok\",\"version\":\"") + NANO_DECOMPILER_VERSION + "\"}"); }

void handle_decompile(socket_t fd, const HttpRequest& req) {
    std::string ct = req.content_type;
    size_t semi = ct.find(';');
    if (semi != std::string::npos) ct = ct.substr(0, semi);
    size_t b = ct.find_first_not_of(' ');
    size_t e = ct.find_last_not_of(' ');
    ct = (b == std::string::npos) ? "" : ct.substr(b, e - b + 1);

    std::string jar_path;
    std::string out_dir;
    std::string tmp_jar_path;

    if (ct == "application/json") {
        std::string body_text(req.body.begin(), req.body.end());
        auto parsed = json_parse(body_text.empty() ? "{}" : body_text);
        if (!parsed.has_value()) {
            send_json(fd, 400, json_error_response("невалидный JSON"));
            return;
        }
        auto jp = parsed->get("jar_path");
        auto op = parsed->get("out_dir");
        if (jp == nullptr || !jp->as_string().has_value() || jp->as_string()->empty()) {
            send_json(fd, 400, json_error_response("поле 'jar_path' обязательно"));
            return;
        }
        jar_path = *jp->as_string();
        if (!fs::is_regular_file(fs::u8path(jar_path))) {
            send_json(fd, 400, json_error_response("файл не найден: " + jar_path));
            return;
        }
        if (op != nullptr && op->as_string().has_value()) out_dir = *op->as_string();
    } else if (ct == "application/java-archive" || ct == "application/octet-stream" || ct == "application/zip" ||
               (ct.empty() && req.body.size() >= 2 && req.body[0] == 'P' && req.body[1] == 'K')) {
        if (req.body.empty()) {
            send_json(fd, 400, json_error_response("пустое тело запроса"));
            return;
        }
        // Загруженный .jar - в приватный каталог со случайным именем (0700), а не в
        // общий %TEMP% под именем из слабого генератора.
        std::string upload_dir = make_private_temp_dir(fs::temp_directory_path().u8string(), "nanodecompiler_upload_");
        if (upload_dir.empty()) {
            send_json(fd, 500, json_error_response("не удалось создать временную папку"));
            return;
        }
        tmp_jar_path = (fs::u8path(upload_dir) / "upload.jar").u8string();
        std::ofstream f(fs::u8path(tmp_jar_path), std::ios::binary);
        f.write(reinterpret_cast<const char*>(req.body.data()), static_cast<std::streamsize>(req.body.size()));
        f.close();
        jar_path = tmp_jar_path;
        auto qod = query_param(req.query, "out_dir");
        if (qod.has_value()) out_dir = *qod;
    } else {
        send_json(fd, 400,
                   json_error_response("непонятное тело запроса - используйте Content-Type: application/json с "
                                        "{\"jar_path\": ...} или пришлите сырые байты .jar (application/java-archive)"));
        return;
    }

    if (out_dir.empty()) out_dir = default_out_dir_for(jar_path);

    std::string result_json = decompile_silent_json(jar_path, out_dir, /*skip_legitimacy=*/false);
    int code = result_json.rfind("{\"status\":\"ok\"", 0) == 0 ? 200 : 500;
    send_json(fd, code, result_json);

    if (!tmp_jar_path.empty()) {
        // удаляем и сам приватный каталог загрузки
        std::error_code ec;
        fs::remove_all(fs::u8path(tmp_jar_path).parent_path(), ec);
    }
}

bool constant_time_equals(const std::string& a, const std::string& b) {
    unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
    size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned char>(ca ^ cb);
    }
    return diff == 0;
}

bool is_loopback_host_header(const std::string& host_header) {
    // Защита от DNS-rebinding для сервера на loopback: принимаем только запросы,
    // адресованные самому 127.0.0.1/localhost/[::1] (имя из чужого домена - нет).
    std::string h = lower_copy(host_header);
    size_t colon = h.rfind(':');
    if (!h.empty() && h[0] == '[') {
        size_t close = h.find(']');
        h = close == std::string::npos ? h : h.substr(0, close + 1);
    } else if (colon != std::string::npos) {
        h = h.substr(0, colon);
    }
    return h == "127.0.0.1" || h == "localhost" || h == "[::1]";
}

void handle_connection_impl(socket_t fd) {
    bool too_large = false;
    auto req = read_request(fd, too_large);
    if (too_large) {
        send_json(fd, 413, json_error_response("слишком большое тело запроса"));
        return;
    }
    if (!req.has_value()) return;

    if (g_api_token.empty()) {
        // Токена нет (разрешено только на loopback): проверяем Host против DNS-rebinding.
        if (!req->host.empty() && !is_loopback_host_header(req->host)) {
            send_json(fd, 403, json_error_response("недопустимый заголовок Host"));
            return;
        }
    } else {
        const std::string prefix = "Bearer ";
        bool ok = req->authorization.compare(0, prefix.size(), prefix) == 0 &&
                  constant_time_equals(req->authorization.substr(prefix.size()), g_api_token);
        if (!ok) {
            send_json(fd, 401, json_error_response("нужен заголовок Authorization: Bearer <токен>"));
            return;
        }
    }

    if (req->method == "GET" && req->path == "/health") {
        handle_health(fd);
    } else if (req->method == "POST" && req->path == "/decompile") {
        handle_decompile(fd, *req);
    } else {
        send_json(fd, 404, json_error_response("неизвестный маршрут"));
    }
}

void handle_connection(socket_t fd) {
    // Любое исключение (в т.ч. из декомпилятора) раньше в detach-потоке означало
    // std::terminate и падение всего сервера.
    try {
        handle_connection_impl(fd);
    } catch (const std::exception& e) {
        send_json(fd, 500, json_error_response(std::string("внутренняя ошибка: ") + e.what()));
    } catch (...) {
        send_json(fd, 500, json_error_response("внутренняя ошибка"));
    }
    ND_CLOSESOCK(fd);
    g_active_connections.fetch_sub(1);
}

bool is_loopback_address(const sockaddr_in& addr) { return (ntohl(addr.sin_addr.s_addr) >> 24) == 127; }

}  // namespace

bool run_api_server(const std::string& host, int port) {
    if (port < 1 || port > 65535) {
        std::cerr << "[api] недопустимый порт: " << port << "\n";
        return false;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[api] WSAStartup не удался\n";
        return false;
    }
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (host == "localhost") {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // Раньше результат не проверялся, и опечатка в --host давала 0.0.0.0 (все интерфейсы).
        std::cerr << "[api] некорректный адрес --host: " << host << "\n";
        return false;
    }

    // Токен: переменная окружения NANO_API_TOKEN (так он не попадает в список процессов).
    if (const char* tok = std::getenv("NANO_API_TOKEN")) g_api_token = tok;
    const bool loopback = is_loopback_address(addr);
    if (!loopback && g_api_token.empty()) {
        std::cerr << "[api] отказ: сервер без авторизации не запускается на не-loopback адресе (" << host
                   << ").\n      Задайте переменную окружения NANO_API_TOKEN (клиент шлёт Authorization: Bearer <токен>) или используйте 127.0.0.1.\n";
        return false;
    }

    socket_t server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == ND_INVALID_SOCK) {
        std::cerr << "[api] не удалось создать сокет\n";
        return false;
    }
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[api] не удалось привязаться к " << host << ":" << port << " (порт занят?)\n";
        ND_CLOSESOCK(server_fd);
        return false;
    }
    if (::listen(server_fd, 16) != 0) {
        std::cerr << "[api] listen() не удался\n";
        ND_CLOSESOCK(server_fd);
        return false;
    }

    std::cout << "[api] " << NANO_DECOMPILER_VERSION << " - HTTP API слушает на http://" << host << ":" << port
              << "  (POST /decompile, GET /health)" << (g_api_token.empty() ? "" : "  [Bearer-авторизация включена]") << "\n";

    while (true) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        socket_t client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == ND_INVALID_SOCK) {
            // Устойчивая ошибка accept (например, кончились дескрипторы) раньше вызывала
            // бесконечный цикл на 100% CPU.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (g_active_connections.load() >= kMaxConcurrentConnections) {
            send_json(client_fd, 503, json_error_response("сервер занят, повторите позже"));
            ND_CLOSESOCK(client_fd);
            continue;
        }
        // Таймаут чтения: зависший клиент не должен держать поток вечно.
#ifdef _WIN32
        DWORD tv = kRecvTimeoutSeconds * 1000;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = kRecvTimeoutSeconds;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
#endif
        g_active_connections.fetch_add(1);
        try {
            std::thread(handle_connection, client_fd).detach();
        } catch (...) {
            g_active_connections.fetch_sub(1);
            ND_CLOSESOCK(client_fd);
        }
    }
    // недостижимо (serve_forever-эквивалент) - ND_CLOSESOCK(server_fd) намеренно не вызывается
}

}  // namespace nd

// proc_util.cpp - см. proc_util.hpp.
#include <cstdint>  // MinGW не тянет int64_t транзитивно, как libstdc++ на Linux
#include "proc_util.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#else
extern char** environ;
#endif
#endif

namespace fs = std::filesystem;

namespace nd {

namespace {

#ifdef _WIN32
constexpr char kPathListSep = ';';
bool is_sep(char c) { return c == '/' || c == '\\'; }
#else
constexpr char kPathListSep = ':';
bool is_sep(char c) { return c == '/'; }
#endif

bool has_separator(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char c) { return is_sep(c); });
}

#ifdef _WIN32
std::wstring to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// Обратное к CommandLineToArgvW квотирование (алгоритм из документации MS):
// обратные слэши удваиваются ТОЛЬКО перед кавычкой/концом аргумента.
std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (auto it = a.begin();; ++it) {
        size_t backslashes = 0;
        while (it != a.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == a.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}
#endif

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == kPathListSep) {
            if (i > start) out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

bool is_absolute_dir(const std::string& d) { return fs::u8path(d).is_absolute(); }

bool is_runnable_file(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || ec) return false;
#ifdef _WIN32
    return true;
#else
    return ::access(p.c_str(), X_OK) == 0;
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// текущий исполняемый файл
// ---------------------------------------------------------------------------

std::string current_executable_path() {
#ifdef _WIN32
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return std::string();
        if (n < buf.size()) return to_utf8(std::wstring(buf.data(), n));
        buf.resize(buf.size() * 2);
        if (buf.size() > 32768) return std::string();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string raw(size, '\0');
    if (_NSGetExecutablePath(&raw[0], &size) != 0) return std::string();
    raw.resize(std::strlen(raw.c_str()));
    char* real = ::realpath(raw.c_str(), nullptr);
    if (real == nullptr) return raw;
    std::string out(real);
    std::free(real);
    return out;
#else
    std::vector<char> buf(4096);
    for (;;) {
        ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
        if (n < 0) return std::string();
        if (static_cast<size_t>(n) < buf.size()) return std::string(buf.data(), static_cast<size_t>(n));
        buf.resize(buf.size() * 2);
        if (buf.size() > 1u << 20) return std::string();
    }
#endif
}

std::string current_executable_dir() {
    std::string p = current_executable_path();
    if (p.empty()) return std::string();
    return fs::u8path(p).parent_path().u8string();
}

// ---------------------------------------------------------------------------
// поиск исполняемого файла (НИКОГДА не в текущей директории)
// ---------------------------------------------------------------------------

std::optional<std::string> find_executable(const std::string& name, bool search_exe_dir) {
    if (name.empty() || name.find('\0') != std::string::npos) return std::nullopt;

    std::vector<std::string> names{name};
#ifdef _WIN32
    {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool has_ext = lower.size() > 4 && (lower.compare(lower.size() - 4, 4, ".exe") == 0 || lower.compare(lower.size() - 4, 4, ".com") == 0);
        if (!has_ext) names = {name + ".exe"};
    }
#endif

    if (has_separator(name)) {
        // Явный путь: относительный не принимаем (он зависел бы от текущей директории).
        fs::path p = fs::u8path(names[0]);
        if (!p.is_absolute()) return std::nullopt;
        if (is_runnable_file(p)) return p.u8string();
        return std::nullopt;
    }

    std::vector<std::string> dirs;
    if (search_exe_dir) {
        std::string d = current_executable_dir();
        if (!d.empty()) dirs.push_back(d);
    }
#ifdef _WIN32
    {
        wchar_t sysdir[MAX_PATH];
        UINT n = GetSystemDirectoryW(sysdir, MAX_PATH);
        if (n > 0 && n < MAX_PATH) dirs.push_back(to_utf8(std::wstring(sysdir, n)));
        wchar_t windir[MAX_PATH];
        n = GetWindowsDirectoryW(windir, MAX_PATH);
        if (n > 0 && n < MAX_PATH) dirs.push_back(to_utf8(std::wstring(windir, n)));
    }
#endif
    if (const char* path_env = std::getenv("PATH")) {
        for (std::string d : split_list(path_env)) {
            // В PATH на Windows встречаются кавычки; "." и относительные записи
            // означают текущую директорию - их пропускаем.
            if (d.size() >= 2 && d.front() == '"' && d.back() == '"') d = d.substr(1, d.size() - 2);
            if (d.empty() || !is_absolute_dir(d)) continue;
            dirs.push_back(d);
        }
    }
#ifndef _WIN32
    for (const char* d : {"/usr/bin", "/bin", "/usr/local/bin", "/opt/homebrew/bin"}) dirs.push_back(d);
#endif

    for (const std::string& d : dirs) {
        for (const std::string& n : names) {
            fs::path candidate = fs::u8path(d) / fs::u8path(n);
            if (is_runnable_file(candidate)) return candidate.u8string();
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// случайные имена и приватные временные каталоги
// ---------------------------------------------------------------------------

std::string secure_random_hex(size_t n_chars) {
    static const char* hexd = "0123456789abcdef";
    std::random_device rd;
    std::string out;
    out.reserve(n_chars);
    while (out.size() < n_chars) {
        uint32_t v = rd();
        for (int i = 0; i < 8 && out.size() < n_chars; ++i) {
            out.push_back(hexd[v & 0xF]);
            v >>= 4;
        }
    }
    return out;
}

std::string make_private_temp_dir(const std::string& base_dir, const std::string& prefix) {
    std::error_code ec;
    fs::create_directories(fs::u8path(base_dir), ec);
    for (int attempt = 0; attempt < 32; ++attempt) {
        fs::path p = fs::u8path(base_dir) / fs::u8path(prefix + secure_random_hex(16));
#ifdef _WIN32
        ec.clear();
        if (fs::create_directory(p, ec) && !ec) return p.u8string();
#else
        // mkdir сразу с 0700 (а не create_directory + chmod): нет окна, в котором
        // каталог доступен остальным. EEXIST -> пробуем другое имя.
        if (::mkdir(p.c_str(), 0700) == 0) return p.u8string();
#endif
    }
    return std::string();
}

void sweep_stale_subdirs(const std::string& base_dir, int64_t max_age_seconds) {
    std::error_code ec;
    fs::path base = fs::u8path(base_dir);
    if (!fs::is_directory(base, ec)) return;
    auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        auto st = fs::symlink_status(it->path(), e2);
        if (e2) continue;
        if (fs::is_symlink(st)) {
            fs::remove(it->path(), e2);  // ссылку удаляем как ссылку, внутрь не заходим
            continue;
        }
        if (!fs::is_directory(st)) continue;
        auto mtime = fs::last_write_time(it->path(), e2);
        if (e2) continue;
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - mtime).count();
        if (age > max_age_seconds) fs::remove_all(it->path(), e2);
    }
}

// ---------------------------------------------------------------------------
// запуск процесса
// ---------------------------------------------------------------------------

#ifdef _WIN32

ProcResult run_process(const std::vector<std::string>& argv, ProcOutput out_mode, int timeout_sec, size_t max_capture_bytes) {
    ProcResult res;
    if (argv.empty()) return res;
    for (const auto& a : argv) {
        if (a.find('\0') != std::string::npos) return res;
    }
    auto resolved = find_executable(argv[0], true);
    if (!resolved) return res;

    std::wstring wexe = to_wide(*resolved);
    std::wstring cmd = quote_arg(wexe);
    for (size_t i = 1; i < argv.size(); ++i) {
        cmd += L' ';
        cmd += quote_arg(to_wide(argv[i]));
    }
    std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end());
    cmdbuf.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE nul_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    HANDLE nul_out = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul_in == INVALID_HANDLE_VALUE) nul_in = nullptr;
    if (nul_out == INVALID_HANDLE_VALUE) nul_out = nullptr;

    HANDLE rd = nullptr, wr = nullptr;
    HANDLE dup_out = nullptr, dup_err = nullptr;
    HANDLE h_out = nul_out, h_err = nul_out;
    bool ok_setup = true;

    if (out_mode == ProcOutput::Capture) {
        if (!CreatePipe(&rd, &wr, &sa, 0)) {
            ok_setup = false;
        } else {
            SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
            h_out = wr;
        }
    } else if (out_mode == ProcOutput::Inherit) {
        // Дублируем наши std-дескрипторы как наследуемые: исходные могут быть
        // ненаследуемыми (зависит от того, кто нас запустил).
        HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE se = GetStdHandle(STD_ERROR_HANDLE);
        if (so && so != INVALID_HANDLE_VALUE &&
            DuplicateHandle(GetCurrentProcess(), so, GetCurrentProcess(), &dup_out, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            h_out = dup_out;
        }
        if (se && se != INVALID_HANDLE_VALUE &&
            DuplicateHandle(GetCurrentProcess(), se, GetCurrentProcess(), &dup_err, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            h_err = dup_err;
        }
    }

    PROCESS_INFORMATION pi{};
    bool created = false;
    if (ok_setup) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nul_in;
        si.hStdOutput = h_out;
        si.hStdError = h_err;
        created = CreateProcessW(wexe.c_str(), cmdbuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
    }

    // Свою копию записывающего конца закрываем сразу: иначе EOF на чтении не придёт.
    if (wr) CloseHandle(wr);
    if (nul_in) CloseHandle(nul_in);
    if (nul_out) CloseHandle(nul_out);
    if (dup_out) CloseHandle(dup_out);
    if (dup_err) CloseHandle(dup_err);

    if (!created) {
        if (rd) CloseHandle(rd);
        return res;
    }
    res.started = true;

    auto t0 = std::chrono::steady_clock::now();
    char buf[8192];

    // true, если что-то прочитали
    auto pump = [&]() -> bool {
        if (!rd) return false;
        bool any = false;
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            DWORD got = 0;
            DWORD want = std::min<DWORD>(avail, static_cast<DWORD>(sizeof(buf)));
            if (!ReadFile(rd, buf, want, &got, nullptr) || got == 0) break;
            any = true;
            size_t room = max_capture_bytes > res.out.size() ? max_capture_bytes - res.out.size() : 0;
            if (room == 0) {
                res.truncated = true;
            } else {
                res.out.append(buf, std::min<size_t>(got, room));
                if (got > room) res.truncated = true;
            }
        }
        return any;
    };

    for (;;) {
        bool any = pump();
        DWORD w = WaitForSingleObject(pi.hProcess, any ? 0 : 15);
        if (w == WAIT_OBJECT_0) {
            pump();
            break;
        }
        bool over_time = timeout_sec > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > timeout_sec;
        if (over_time || res.truncated) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            if (over_time) res.timed_out = true;
            pump();
            break;
        }
    }

    DWORD code = 1;
    if (GetExitCodeProcess(pi.hProcess, &code)) res.exit_code = static_cast<int>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (rd) CloseHandle(rd);
    return res;
}

#else  // POSIX

namespace {
#ifdef __APPLE__
char** current_environ() { return *_NSGetEnviron(); }
#else
char** current_environ() { return ::environ; }
#endif
}  // namespace

ProcResult run_process(const std::vector<std::string>& argv, ProcOutput out_mode, int timeout_sec, size_t max_capture_bytes) {
    ProcResult res;
    if (argv.empty()) return res;
    for (const auto& a : argv) {
        if (a.find('\0') != std::string::npos) return res;
    }
    auto resolved = find_executable(argv[0], true);
    if (!resolved) return res;

    std::vector<std::string> args_storage = argv;
    args_storage[0] = *resolved;
    std::vector<char*> c_argv;
    for (auto& s : args_storage) c_argv.push_back(&s[0]);
    c_argv.push_back(nullptr);

    int pipefd[2] = {-1, -1};
    if (out_mode == ProcOutput::Capture) {
        if (::pipe(pipefd) != 0) return res;
        ::fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (out_mode == ProcOutput::Capture) {
        posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    } else if (out_mode == ProcOutput::Discard) {
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    }

    pid_t pid = 0;
    int rc = posix_spawn(&pid, resolved->c_str(), &fa, nullptr, c_argv.data(), current_environ());
    posix_spawn_file_actions_destroy(&fa);
    if (pipefd[1] >= 0) ::close(pipefd[1]);  // свой записывающий конец - закрыть, иначе нет EOF
    if (rc != 0) {
        if (pipefd[0] >= 0) ::close(pipefd[0]);
        return res;
    }
    res.started = true;

    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    auto kill_child = [&]() { ::kill(pid, SIGKILL); };

    int status = 0;
    bool reaped = false;

    if (pipefd[0] >= 0) {
        char buf[8192];
        for (;;) {
            struct pollfd pfd;
            pfd.fd = pipefd[0];
            pfd.events = POLLIN;
            pfd.revents = 0;
            int pr = ::poll(&pfd, 1, 100);
            if (pr < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (pr > 0) {
                ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
                if (n < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                if (n == 0) break;  // EOF
                size_t room = max_capture_bytes > res.out.size() ? max_capture_bytes - res.out.size() : 0;
                if (room == 0 || static_cast<size_t>(n) > room) res.truncated = true;
                if (room > 0) res.out.append(buf, std::min<size_t>(static_cast<size_t>(n), room));
                if (res.truncated) {
                    kill_child();
                    break;
                }
            }
            if (timeout_sec > 0 && elapsed() > timeout_sec) {
                res.timed_out = true;
                kill_child();
                break;
            }
        }
        ::close(pipefd[0]);
    }

    for (;;) {
        pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            reaped = true;
            break;
        }
        if (w < 0 && errno != EINTR) break;
        if (timeout_sec > 0 && elapsed() > timeout_sec) {
            res.timed_out = true;
            kill_child();
            ::waitpid(pid, &status, 0);
            reaped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (reaped && WIFEXITED(status)) res.exit_code = WEXITSTATUS(status);
    return res;
}

#endif

}  // namespace nd

#include <cstdint>
#include <optional>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "archive_extractor.hpp"
#include "zip_reader.hpp"
#include "platform_detect.hpp"
#include "proc_util.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <set>
#include <zlib.h>

namespace fs = std::filesystem;

namespace nd {

namespace {

// Лимиты на распаковку НЕДОВЕРЕННЫХ архивов (zip/tar-bomb): раньше размеры из
// заголовков принимались как есть, и крошечный архив мог развернуться в сотни
// гигабайт и забить диск (или выделить гигабайты памяти под одну запись).
#ifndef ND_MAX_EXTRACT_TOTAL_BYTES
#define ND_MAX_EXTRACT_TOTAL_BYTES (8ull << 30)  // 8 ГиБ суммарно (макрос - чтобы тесты могли уменьшить лимит через -D)
#endif
constexpr uint64_t kMaxExtractTotalBytes = ND_MAX_EXTRACT_TOTAL_BYTES;
constexpr uint64_t kMaxExtractEntryBytes = 2ull << 30;  // 2 ГиБ на файл
constexpr size_t kMaxExtractEntries = 200000;
constexpr size_t kMaxTarMetaBytes = 1u << 20;  // GNU long name / PAX-заголовок
constexpr int kExternalToolTimeoutSec = 15 * 60;
// Старые папки распаковки (после аварии/закрытия без очистки) подчищаем.
constexpr int64_t kStaleTempMaxAgeSec = 24 * 60 * 60;

// Имена устройств Windows (CON, NUL, COM1...): запись в такой "файл" пишет в
// устройство, а не на диск.
bool is_reserved_device_name(const std::string& segment) {
    std::string base = segment.substr(0, segment.find('.'));
    for (char& c : base) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
    if (base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) && base[3] >= '1' && base[3] <= '9') return true;
    return false;
}

std::string sanitize_subpath(const std::string& raw) {
    if (raw.empty()) return "";
    std::string s = raw;
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    std::string out;
    std::istringstream iss(s);
    std::string seg;
    while (std::getline(iss, seg, '/')) {
        while (!seg.empty() && (seg.back() == ' ' || seg.back() == '.' || seg.back() == '\t')) {
            seg.pop_back();
        }
        if (seg.empty() || seg == "." || seg == "..") continue;
        // Снятие блокировки архивов: игнорируем служебные файлы .*_archive_lock
        if (seg.find("_archive_lock") != std::string::npos) {
            return "";
        }
        for (char& c : seg) {
            if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*' || static_cast<unsigned char>(c) < 32) {
                c = '_';
            }
        }
        if (is_reserved_device_name(seg)) seg.insert(seg.begin(), '_');
        if (!out.empty()) out += "/";
        out += seg;
    }
    return out;
}

std::string archives_root_dir() { return (fs::temp_directory_path() / "NanoDecompiler" / "archives").u8string(); }

// Возвращает "" при неудаче. Имя каталога содержит случайный суффикс, каталог
// создаётся с правами 0700 (раньше имя было `<мс>_<stem>` - предсказуемое: в
// общем /tmp его можно было занять заранее).
std::string create_temp_archive_directory(const std::string& archive_path) {
    std::string stem = fs::u8path(archive_path).stem().u8string();
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            c = '_';
        }
    }
    if (stem.size() > 60) stem.resize(60);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    std::string root = archives_root_dir();
    sweep_stale_subdirs(root, kStaleTempMaxAgeSec);
    return make_private_temp_dir(root, std::to_string(ms) + "_" + stem + "_");
}

// true, если суммарный размер файлов в каталоге превысил limit (обход прекращается сразу).
bool directory_size_exceeds(const std::string& dir, uint64_t limit) {
    std::error_code ec;
    uint64_t total = 0;
    for (fs::recursive_directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_symlink(e2)) continue;
        if (!it->is_regular_file(e2)) continue;
        total += fs::file_size(it->path(), e2);
        if (total > limit) return true;
    }
    return false;
}

// true, если какой-либо РОДИТЕЛЬСКИЙ каталог target внутри out_dir - символическая
// ссылка: писать через неё нельзя (она могла бы вести за пределы out_dir).
bool has_symlink_ancestor(const fs::path& out_dir, const fs::path& target) {
    std::error_code ec;
    fs::path cur = target.parent_path();
    fs::path root = out_dir.lexically_normal();
    while (!cur.empty() && cur.lexically_normal() != root) {
        if (fs::is_symlink(fs::symlink_status(cur, ec))) return true;
        fs::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return false;
}

// Если на месте будущего файла уже лежит символическая ссылка (из более ранней
// записи архива), убираем её: иначе запись пошла бы ПО ссылке.
void clear_symlink_at(const fs::path& target) {
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(target, ec))) fs::remove(target, ec);
}

std::string first_segment(const std::string& clean) {
    size_t slash = clean.find('/');
    return slash == std::string::npos ? clean : clean.substr(0, slash);
}

bool extract_zip_archive(const std::string& archive_path, const std::string& out_dir, std::string& err, std::string* single_root = nullptr) {
    try {
        std::set<std::string> roots;
        ZipReader zr(archive_path);
        auto names = zr.namelist();
        if (names.size() > kMaxExtractEntries) {
            err = "В архиве слишком много файлов";
            return false;
        }
        uint64_t total = 0;
        for (const auto& name : names) {
            std::string clean = sanitize_subpath(name);
            if (clean.empty()) continue;
            roots.insert(first_segment(clean));
            fs::path target = fs::u8path(out_dir) / fs::u8path(clean);
            if (has_symlink_ancestor(fs::u8path(out_dir), target)) continue;
            if (name.back() == '/' || name.back() == '\\') {
                fs::create_directories(target);
                continue;
            }
            if (const ZipEntryInfo* info = zr.getinfo(name)) {
                total += info->uncompressed_size;
                if (total > kMaxExtractTotalBytes) {
                    err = "Распакованный размер архива превышает допустимый лимит";
                    return false;
                }
            }
            if (target.has_parent_path()) {
                fs::create_directories(target.parent_path());
            }
            auto data = zr.read(name);
            clear_symlink_at(target);
            std::ofstream f(target, std::ios::binary);
            if (f) {
                f.write(reinterpret_cast<const char*>(data.data()), data.size());
            }
        }
        if (single_root && roots.size() == 1) *single_root = *roots.begin();
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// Читает ровно n байт (gzread может вернуть меньше на границах внутренних буферов).
size_t gz_read_full(gzFile gz, void* buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        unsigned want = static_cast<unsigned>(std::min<size_t>(n - done, 1u << 20));
        int got = gzread(gz, static_cast<char*>(buf) + done, want);
        if (got <= 0) break;
        done += static_cast<size_t>(got);
    }
    return done;
}

bool gz_skip(gzFile gz, uint64_t n) {
    char sink[4096];
    while (n > 0) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(n, sizeof(sink)));
        size_t got = gz_read_full(gz, sink, want);
        if (got == 0) return false;
        n -= got;
    }
    return true;
}

uint64_t tar_padded(uint64_t size) { return ((size + 511) / 512) * 512; }

// Поле размера: восьмеричное ASCII либо base-256 (старший бит первого байта,
// GNU-расширение для файлов > 8 ГиБ). Раньше base-256 разбирался как мусор.
uint64_t parse_tar_size(const char* field12) {
    unsigned char first = static_cast<unsigned char>(field12[0]);
    if (first & 0x80) {
        uint64_t v = first & 0x7F;
        for (int i = 1; i < 12; ++i) {
            if (v > (UINT64_MAX >> 8)) return UINT64_MAX;  // заведомо за лимитом
            v = (v << 8) | static_cast<unsigned char>(field12[i]);
        }
        return v;
    }
    char tmp[13];
    std::memcpy(tmp, field12, 12);
    tmp[12] = '\0';
    return std::strtoull(tmp, nullptr, 8);
}

// PAX extended header: записи "<len> <key>=<value>\n". Нам нужен только path.
std::string parse_pax_path(const std::string& data) {
    size_t pos = 0;
    while (pos < data.size()) {
        size_t space = data.find(' ', pos);
        if (space == std::string::npos) break;
        unsigned long len = std::strtoul(data.substr(pos, space - pos).c_str(), nullptr, 10);
        if (len == 0 || pos + len > data.size() || space + 1 >= pos + len) break;
        std::string record = data.substr(space + 1, pos + len - space - 1);
        if (!record.empty() && record.back() == '\n') record.pop_back();
        if (record.rfind("path=", 0) == 0) return record.substr(5);
        pos += len;
    }
    return "";
}

bool extract_tar_stream(gzFile gz, const std::string& out_dir, std::string& err, bool allow_inner_symlinks = false,
                        std::string* single_root = nullptr) {
    std::set<std::string> roots;
    char header[512];
    std::string long_name;  // GNU 'L' (././@LongLink): имя следующей записи
    std::string pax_path;   // PAX 'x': path= для следующей записи
    uint64_t total = 0;
    size_t entries = 0;
    while (true) {
        size_t read_bytes = gz_read_full(gz, header, 512);
        if (read_bytes == 0) break;
        if (read_bytes < 512) {
            err = "Неполный tar-блок заголовка";
            return false;
        }

        // Проверяем на нулевой блок (признак окончания tar)
        bool all_zero = true;
        for (int i = 0; i < 512; ++i) {
            if (header[i] != 0) {
                all_zero = false;
                break;
            }
        }
        if (all_zero) break;

        char name_buf[101] = {0};
        std::memcpy(name_buf, header, 100);
        std::string raw_name(name_buf);

        // Префикс ustar
        if (std::memcmp(header + 257, "ustar", 5) == 0) {
            char prefix_buf[156] = {0};
            std::memcpy(prefix_buf, header + 345, 155);
            if (prefix_buf[0] != 0) {
                raw_name = std::string(prefix_buf) + "/" + raw_name;
            }
        }

        uint64_t file_size = parse_tar_size(header + 124);
        char typeflag = header[156];

        // Служебные записи: имя/атрибуты СЛЕДУЮЩЕЙ записи. Раньше они извлекались
        // как обычные файлы "PaxHeader/..." и длинные имена терялись.
        if (typeflag == 'L' || typeflag == 'x') {
            if (file_size > kMaxTarMetaBytes) {
                err = "Слишком большой служебный tar-заголовок";
                return false;
            }
            std::string meta(static_cast<size_t>(file_size), '\0');
            if (gz_read_full(gz, &meta[0], meta.size()) != meta.size() || !gz_skip(gz, tar_padded(file_size) - file_size)) {
                err = "Неполные данные tar-архива";
                return false;
            }
            if (typeflag == 'L') {
                long_name = meta.substr(0, meta.find('\0'));
            } else {
                pax_path = parse_pax_path(meta);
            }
            continue;
        }
        if (typeflag == 'g') {  // глобальный PAX-заголовок - игнорируем
            if (!gz_skip(gz, tar_padded(file_size))) {
                err = "Неполные данные tar-архива";
                return false;
            }
            continue;
        }

        if (!long_name.empty()) raw_name = long_name;
        else if (!pax_path.empty()) raw_name = pax_path;
        long_name.clear();
        pax_path.clear();

        std::string clean = sanitize_subpath(raw_name);
        bool is_dir = typeflag == '5' || (!raw_name.empty() && raw_name.back() == '/');
        bool is_regular = typeflag == '0' || typeflag == '\0' || typeflag == '7';
        bool is_symlink = typeflag == '2' && allow_inner_symlinks;
        // Жёсткие ссылки, устройства, FIFO не создаём вообще; символические - только
        // когда вызывающий явно разрешил (JDK содержит их) и только внутрь out_dir.
        if (clean.empty() || !(is_dir || is_regular || is_symlink)) {
            if (!gz_skip(gz, tar_padded(file_size))) {
                err = "Неполные данные tar-архива";
                return false;
            }
            continue;
        }

        if (++entries > kMaxExtractEntries) {
            err = "В архиве слишком много файлов";
            return false;
        }

        fs::path target = fs::u8path(out_dir) / fs::u8path(clean);
        roots.insert(first_segment(clean));

        if (has_symlink_ancestor(fs::u8path(out_dir), target)) {
            if (!gz_skip(gz, tar_padded(file_size))) {
                err = "Неполные данные tar-архива";
                return false;
            }
            continue;
        }

        if (is_symlink) {
            char link_buf[101] = {0};
            std::memcpy(link_buf, header + 157, 100);
            std::string link_target(link_buf);
            // Цель - относительная и, будучи разрешённой от каталога ссылки, остаётся
            // внутри out_dir (абсолютные пути и подъём выше out_dir отвергаются).
            bool ok_link = !link_target.empty() && link_target.front() != '/' && link_target.front() != '\\' &&
                           link_target.find(':') == std::string::npos;
            if (ok_link) {
                fs::path resolved = (target.parent_path() / fs::u8path(link_target)).lexically_normal();
                std::string rel = resolved.lexically_relative(fs::u8path(out_dir).lexically_normal()).generic_string();
                ok_link = !rel.empty() && rel != ".." && rel.rfind("../", 0) != 0;
            }
            if (ok_link) {
                std::error_code lec;
                fs::create_directories(target.parent_path(), lec);
                fs::remove(target, lec);
                fs::create_symlink(fs::u8path(link_target), target, lec);  // ошибка симлинка не фатальна
            }
            if (!gz_skip(gz, tar_padded(file_size))) {
                err = "Неполные данные tar-архива";
                return false;
            }
            continue;
        }

        if (is_dir) {
            fs::create_directories(target);
            if (!gz_skip(gz, tar_padded(file_size))) {
                err = "Неполные данные tar-архива";
                return false;
            }
            continue;
        }

        if (file_size > kMaxExtractEntryBytes || (total += file_size) > kMaxExtractTotalBytes) {
            err = "Распакованный размер архива превышает допустимый лимит";
            return false;
        }
        if (target.has_parent_path()) {
            fs::create_directories(target.parent_path());
        }
        clear_symlink_at(target);
        std::ofstream out(target, std::ios::binary);
        uint64_t remaining = file_size;
        std::vector<char> chunk(64 * 1024);
        while (remaining > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(remaining, chunk.size()));
            size_t n = gz_read_full(gz, chunk.data(), to_read);
            if (n == 0) {
                err = "Неполные данные tar-архива";
                return false;
            }
            if (out) out.write(chunk.data(), static_cast<std::streamsize>(n));
            remaining -= n;
        }
#ifndef _WIN32
        // Права на запуск из заголовка tar (только x-биты; setuid/setgid/sticky никогда не переносим).
        // Раньше файлы создавались без них: у распакованного JDK не запускались lib/jspawnhelper и др.
        out.close();
        {
            char mode_buf[9] = {0};
            std::memcpy(mode_buf, header + 100, 8);
            unsigned long mode = std::strtoul(mode_buf, nullptr, 8);
            fs::perms add = fs::perms::none;
            if (mode & 0100) add |= fs::perms::owner_exec;
            if (mode & 0010) add |= fs::perms::group_exec;
            if (mode & 0001) add |= fs::perms::others_exec;
            if (add != fs::perms::none) {
                std::error_code pec;
                fs::permissions(target, add, fs::perm_options::add, pec);
            }
        }
#endif
        // Пропускаем padding до 512 байт
        if (!gz_skip(gz, tar_padded(file_size) - file_size)) {
            err = "Неполные данные tar-архива";
            return false;
        }
    }
    if (single_root && roots.size() == 1) *single_root = *roots.begin();
    return true;
}

bool extract_tar_archive(const std::string& archive_path, const std::string& out_dir, std::string& err, bool allow_inner_symlinks = false,
                         std::string* single_root = nullptr) {
    // gzopen(const char*) на Windows трактует путь как ANSI: архив с кириллицей в пути не
    // открывался. Открываем файл сами (по UTF-8/широкому пути) и отдаём zlib дескриптор.
    FILE* fp = nullptr;
#ifdef _WIN32
    fp = _wfopen(fs::u8path(archive_path).c_str(), L"rb");
#else
    fp = std::fopen(archive_path.c_str(), "rb");
#endif
    if (!fp) {
        err = "Не удалось открыть tar.gz архив: " + archive_path;
        return false;
    }
#ifdef _WIN32
    int fd = _dup(_fileno(fp));
#else
    int fd = ::dup(fileno(fp));
#endif
    std::fclose(fp);
    if (fd < 0) {
        err = "Не удалось открыть tar.gz архив: " + archive_path;
        return false;
    }
    gzFile gz = gzdopen(fd, "rb");  // gzclose() закрывает и дескриптор
    if (!gz) {
#ifdef _WIN32
        _close(fd);
#else
        ::close(fd);
#endif
        err = "Не удалось открыть tar.gz архив: " + archive_path;
        return false;
    }
    bool ok = false;
    try {
        ok = extract_tar_stream(gz, out_dir, err, allow_inner_symlinks, single_root);
    } catch (const std::exception& e) {
        err = e.what();
    }
    gzclose(gz);
    return ok;
}

// Ошибка распаковки вызвана превышением лимитов размера/числа файлов (а не повреждением).
bool is_limit_error(const std::string& err) {
    return err.find("too large") != std::string::npos || err.find("size limit") != std::string::npos ||
           err.find("zip bomb") != std::string::npos || err.find("лимит") != std::string::npos ||
           err.find("слишком много") != std::string::npos;
}

bool has_suffix_ci(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i]) return false;
    }
    return true;
}

// .7z/.rar/.zip(fallback) через внешний распаковщик. БЕЗ shell: раньше команда
// собиралась строкой `7za x -y -o"<dir>" "<archive>"` и шла в std::system() -
// имя архива с `"` или `$(...)` исполнялось как команда, а сам 7za/unrar
// искался в ТЕКУЩЕЙ директории (подложенный рядом с архивом 7za.exe запускался
// вместо настоящего). Теперь аргументы передаются массивом, а исполняемые
// файлы ищутся только рядом с движком и в PATH. Заодно у unrar правильный
// синтаксис (у него нет ключа -o<dir>; там `-o+` означает "перезаписывать").
bool extract_external_archive(const std::string& archive_path, const std::string& out_dir, std::string& err) {
    const bool is_rar = has_suffix_ci(archive_path, ".rar");
    // Автономный 7za рар не умеет - для .rar нужен unrar или полный 7z.
    std::vector<std::string> candidates = is_rar ? std::vector<std::string>{"unrar", "7z", "7zz"}
                                                  : std::vector<std::string>{"7za", "7z", "7zz", "7zr"};

    std::string tool_name;
    std::optional<std::string> tool_path;
    for (const auto& c : candidates) {
        tool_path = find_executable(c, true);
        if (tool_path) {
            tool_name = c;
            break;
        }
    }
    if (!tool_path) {
        err = std::string("Не найден внешний распаковщик (") + candidates.front() + "/" + candidates[1] +
              ") ни рядом с движком, ни в PATH";
        return false;
    }

    // Оценка ДО распаковки: у 7-Zip `l -slt` печатает "Size = N" по каждому файлу, поэтому
    // архив-бомбу (малый .7z, гигабайты внутри) отвергаем, не распаковывая. Для unrar
    // такого вывода нет - там сработает проверка размера каталога после распаковки.
    if (tool_name != "unrar") {
        ProcResult lr = run_process({*tool_path, "l", "-slt", "--", archive_path}, ProcOutput::Capture, 120, 64u << 20);
        if (lr.started && !lr.timed_out) {
            uint64_t total = 0;
            size_t entries = 0;
            bool in_entries = false;
            std::istringstream ls(lr.out);
            std::string line;
            while (std::getline(ls, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("----------", 0) == 0) {
                    in_entries = true;
                    continue;
                }
                if (!in_entries) continue;
                if (line.rfind("Path = ", 0) == 0) {
                    ++entries;
                } else if (line.rfind("Size = ", 0) == 0) {
                    total += std::strtoull(line.c_str() + 7, nullptr, 10);
                }
                if (total > kMaxExtractTotalBytes || entries > kMaxExtractEntries) break;
            }
            if (lr.truncated || total > kMaxExtractTotalBytes || entries > kMaxExtractEntries) {
                err = "Распакованный размер архива превышает допустимый лимит (" + tool_name + ": число файлов / размер)";
                return false;
            }
        }
    }

    std::vector<std::string> argv;
    argv.push_back(*tool_path);
    if (tool_name == "unrar") {
        std::string dest = out_dir;
        if (!dest.empty() && dest.back() != '/' && dest.back() != '\\') dest += static_cast<char>(fs::path::preferred_separator);
        argv.insert(argv.end(), {"x", "-y", "-o+", "--", archive_path, dest});
    } else {
        argv.insert(argv.end(), {"x", "-y", "-o" + out_dir, "--", archive_path});
    }

    ProcResult pr = run_process(argv, ProcOutput::Discard, kExternalToolTimeoutSec);
    if (!pr.started) {
        err = "Не удалось запустить распаковщик (" + tool_name + ")";
        return false;
    }
    if (pr.timed_out) {
        err = "Распаковщик (" + tool_name + ") не уложился в таймаут";
        return false;
    }
    // 0 - успех, 1 - предупреждения (напр. заблокированные файлы): не ошибка.
    if (pr.exit_code != 0 && pr.exit_code != 1) {
        err = "Команда распаковщика (" + tool_name + ") завершилась с ошибкой " + std::to_string(pr.exit_code);
        return false;
    }
    if (directory_size_exceeds(out_dir, kMaxExtractTotalBytes)) {
        err = "Распакованный размер архива превышает допустимый лимит";
        return false;
    }
    return true;
}

}  // namespace

void remove_archive_temp_dir(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::u8path(archives_root_dir()), ec);
    if (ec) return;
    fs::path target = fs::weakly_canonical(fs::u8path(dir), ec);
    if (ec) return;
    // Только строго ВНУТРИ нашей папки распаковки и не по символической ссылке.
    std::string rel = fs::relative(target, root, ec).generic_string();
    if (ec || rel.empty() || rel == "." || rel == ".." || rel.rfind("../", 0) == 0) return;
    if (fs::is_symlink(fs::symlink_status(fs::u8path(dir), ec))) return;
    fs::remove_all(fs::u8path(dir), ec);
}

bool extract_zip_safe(const std::string& zip_path, const std::string& out_dir, std::string& err, std::string* single_root) {
    return extract_zip_archive(zip_path, out_dir, err, single_root);
}

bool extract_tar_gz_safe(const std::string& tar_path, const std::string& out_dir, std::string& err, bool allow_inner_symlinks,
                         std::string* single_root) {
    return extract_tar_archive(tar_path, out_dir, err, allow_inner_symlinks, single_root);
}

bool is_supported_archive_file(const std::string& path) {
    std::string lower = path;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".zip") return true;
    if (lower.size() >= 7 && lower.substr(lower.size() - 7) == ".tar.gz") return true;
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".tgz") return true;
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".tar") return true;
    if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".7z") return true;
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".7zip") return true;
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".rar") return true;

    // Проверяем сигнатуры байт
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    char sig[8] = {0};
    f.read(sig, sizeof(sig));
    if (f.gcount() >= 4) {
        // ZIP
        if (sig[0] == 'P' && sig[1] == 'K' && sig[2] == 0x03 && sig[3] == 0x04) return true;
        // GZIP
        if (static_cast<unsigned char>(sig[0]) == 0x1f && static_cast<unsigned char>(sig[1]) == 0x8b) return true;
        // 7Z ('7' 'z' \xbc \xaf)
        if (sig[0] == '7' && sig[1] == 'z' && static_cast<unsigned char>(sig[2]) == 0xbc && static_cast<unsigned char>(sig[3]) == 0xaf) return true;
        // RAR ('R' 'a' 'r' '!')
        if (sig[0] == 'R' && sig[1] == 'a' && sig[2] == 'r' && sig[3] == '!') return true;
    }

    return false;
}

ArchiveExtractionSummary extract_and_scan_archive(const std::string& archive_path, const std::string& custom_temp_dir) {
    ArchiveExtractionSummary res;
    const bool created_here = custom_temp_dir.empty();
    res.temp_dir = !created_here ? custom_temp_dir : create_temp_archive_directory(archive_path);
    if (res.temp_dir.empty()) {
        res.ok = false;
        res.error = "Не удалось создать временную папку для распаковки";
        return res;
    }

    std::string lower = archive_path;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    bool extracted = false;
    std::string err;

    if (lower.size() >= 7 && lower.substr(lower.size() - 7) == ".tar.gz") {
        extracted = extract_tar_archive(archive_path, res.temp_dir, err);
    } else if (lower.size() >= 4 && (lower.substr(lower.size() - 4) == ".tgz" || lower.substr(lower.size() - 4) == ".tar")) {
        extracted = extract_tar_archive(archive_path, res.temp_dir, err);
    } else if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".rar") {
        extracted = extract_external_archive(archive_path, res.temp_dir, err);
    } else if (lower.size() >= 3 && (lower.substr(lower.size() - 3) == ".7z" || (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".7zip"))) {
        extracted = extract_external_archive(archive_path, res.temp_dir, err);
    } else {
        // По умолчанию пробуем встроенный ZIP-распаковщик
        extracted = extract_zip_archive(archive_path, res.temp_dir, err);
        if (!extracted && !is_limit_error(err)) {
            // Fallback на внешние утилиты. НЕ при превышении лимитов размера: внешний
            // распаковщик не имеет лимитов и развернул бы zip-бомбу целиком.
            std::string builtin_err = err;
            extracted = extract_external_archive(archive_path, res.temp_dir, err);
            if (!extracted) err = builtin_err + " | " + err;  // не терять первопричину
        }
    }

    if (!extracted) {
        res.ok = false;
        res.error = err.empty() ? "Не удалось распаковать архив" : err;
        // Не оставляем полураспакованные файлы (в т.ч. при срабатывании лимитов размера).
        if (created_here) remove_archive_temp_dir(res.temp_dir);
        return res;
    }

    res.ok = true;

    // Рекурсивный поиск .jar файлов
    std::vector<std::string> jar_paths;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(fs::u8path(res.temp_dir), ec)) {
        if (ec) break;
        // Символические ссылки (их мог создать внешний распаковщик) не считаем
        // файлами: ссылка на jar вне временной папки иначе попала бы в обработку.
        std::error_code sym_ec;
        if (entry.is_symlink(sym_ec)) continue;
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().u8string();
            std::string ext_lower = ext;
            for (char& c : ext_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext_lower == ".jar") {
                jar_paths.push_back(entry.path().u8string());
            }
        }
    }

    res.total_jars = jar_paths.size();

    // Анализируем каждый .jar
    for (const auto& jp : jar_paths) {
        ArchivePluginEntry entry;
        entry.full_path = jp;
        entry.filename = fs::u8path(jp).filename().u8string();
        entry.rel_path = fs::relative(fs::u8path(jp), fs::u8path(res.temp_dir), ec).u8string();
        entry.size_bytes = fs::file_size(fs::u8path(jp), ec);

        try {
            ZipReader zr(jp);
            auto names = zr.namelist();

            // Проверка на серверное ядро
            bool is_paperclip = false;
            bool is_bundler = false;
            bool is_mc_server = false;
            bool is_craftbukkit_core = false;

            for (const auto& n : names) {
                if (n.rfind("io/papermc/paperclip", 0) == 0 || n.rfind("META-INF/versions/", 0) == 0) {
                    is_paperclip = true;
                } else if (n.rfind("net/minecraft/bundler", 0) == 0) {
                    is_bundler = true;
                } else if (n == "net/minecraft/server/MinecraftServer.class" || n == "net/minecraft/server/Main.class") {
                    is_mc_server = true;
                } else if (n == "org/bukkit/craftbukkit/Main.class") {
                    is_craftbukkit_core = true;
                }
            }

            bool has_plugin_yml = std::find(names.begin(), names.end(), "plugin.yml") != names.end();
            std::string fn_lower = entry.filename;
            for (char& c : fn_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            bool is_server_name = (fn_lower.rfind("paper-", 0) == 0 || fn_lower.rfind("spigot-", 0) == 0 ||
                                  fn_lower.rfind("purpur-", 0) == 0 || fn_lower.rfind("folia-", 0) == 0 ||
                                  fn_lower == "server.jar");

            if (is_paperclip || is_bundler || is_mc_server || (is_craftbukkit_core && !has_plugin_yml) || (is_server_name && !has_plugin_yml)) {
                entry.is_server_core = true;
                entry.is_plugin = false;
                if (is_paperclip) entry.core_reason = "Paperclip загрузчик (Paper/Purpur/Folia)";
                else if (is_bundler) entry.core_reason = "Vanilla / Bundler загрузчик ядра";
                else if (is_mc_server) entry.core_reason = "Серверное ядро net.minecraft.server";
                else if (is_craftbukkit_core) entry.core_reason = "Серверное ядро CraftBukkit/Spigot";
                else entry.core_reason = "Серверное ядро Minecraft";
                res.server_cores.push_back(std::move(entry));
                continue;
            }

            // Проверка на плагин через detect_platform
            auto pinfo = detect_platform(names, [&zr](const std::string& path) -> std::optional<std::string> {
                try {
                    auto data = zr.read(path);
                    return std::string(data.begin(), data.end());
                } catch (...) {
                    return std::nullopt;
                }
            });

            entry.platform = pinfo;
            if (pinfo.kind == PlatformKind::Bukkit || pinfo.kind == PlatformKind::Paper ||
                pinfo.kind == PlatformKind::Bungee || pinfo.kind == PlatformKind::Velocity ||
                pinfo.kind == PlatformKind::Sponge) {
                entry.is_plugin = true;
                entry.is_server_core = false;
                res.plugins.push_back(std::move(entry));
            } else {
                entry.is_plugin = false;
                entry.is_server_core = false;
                res.skipped_libraries++;
            }
        } catch (...) {
            res.skipped_libraries++;
        }
    }

    return res;
}

}  // namespace nd

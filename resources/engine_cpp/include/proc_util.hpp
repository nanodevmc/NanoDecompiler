// proc_util.hpp - запуск внешних программ БЕЗ shell и вспомогательные функции
// для безопасной работы с временными файлами.
//
// ЗАЧЕМ: раньше движок собирал командные строки текстом и отдавал их
// std::system()/popen() (curl, 7za, unrar, chmod/mv, apt-get через pkexec).
// Любой пользовательский/сетевой фрагмент в такой строке (имя архива,
// URL из ответа API, путь к exe) мог выполнить произвольную команду:
// внутри "..." в sh работают $(...) и `...`, в '...' - символ ' рвёт
// кавычку, в cmd.exe - %VAR% и ^. Здесь аргументы передаются МАССИВОМ
// напрямую в CreateProcessW / posix_spawn - интерпретатора команд нет вообще.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nd {

enum class ProcOutput {
    Discard,  // stdout/stderr -> NUL (/dev/null)
    Capture,  // stdout собирается в ProcResult::out, stderr отбрасывается
    Inherit,  // stdout/stderr дочернего процесса = наши
};

struct ProcResult {
    bool started = false;     // процесс удалось создать
    bool timed_out = false;   // убит по таймауту
    bool truncated = false;   // вывод превысил max_capture_bytes, процесс убит
    int exit_code = -1;       // код завершения (-1 - не запустился/убит сигналом)
    std::string out;          // stdout (только ProcOutput::Capture)
};

// argv[0] - имя или путь программы. Имя без разделителя пути ищется ТОЛЬКО в
// каталоге самого exe, системных каталогах и PATH - НИКОГДА в текущей
// директории (иначе подложенный рядом с архивом 7za.exe/curl.exe выполнялся
// бы вместо настоящего). timeout_sec == 0 - без таймаута.
ProcResult run_process(const std::vector<std::string>& argv, ProcOutput out = ProcOutput::Discard, int timeout_sec = 0,
                       size_t max_capture_bytes = 64u * 1024u * 1024u);

// Полный путь к исполняемому файлу или nullopt. name с разделителем пути
// проверяется как есть. search_exe_dir - сначала каталог текущего exe.
std::optional<std::string> find_executable(const std::string& name, bool search_exe_dir = true);

// Абсолютный путь и каталог ТЕКУЩЕГО исполняемого файла (ОС, а не argv[0],
// который может быть относительным или вообще не путём).
std::string current_executable_path();
std::string current_executable_dir();

// Случайная hex-строка из n символов (источник - std::random_device на каждый
// символ-блок; раньше использовался mt19937_64, засеянный ОДНИМ 32-битным
// значением, т.е. имена временных файлов были предсказуемы).
std::string secure_random_hex(size_t n_chars);

// Создаёт НОВЫЙ каталог base/prefix<random> (доступ только владельцу, 0700 на
// POSIX). create_directory сам отказывает, если имя уже занято, поэтому
// подменить его заранее симлинком нельзя. Пустая строка - не удалось.
std::string make_private_temp_dir(const std::string& base_dir, const std::string& prefix);

// Удаляет вложенные каталоги base_dir старше max_age_seconds (подчистка
// временных каталогов от прошлых запусков/аварий). Символические ссылки
// удаляются как ссылки, по ним не заходим. Ошибки игнорируются.
void sweep_stale_subdirs(const std::string& base_dir, int64_t max_age_seconds);

}  // namespace nd

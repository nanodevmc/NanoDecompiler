#pragma once
#include <string>

namespace nd {
    void check_update();
    // Самообновление: скачивает релиз проекта с GitHub, ОБЯЗАТЕЛЬНО сверяет SHA-256 с checksums.json
    // и заменяет текущий исполняемый файл (при сбое - откат). Параметр оставлен для совместимости
    // сигнатуры: путь к себе берётся у ОС, а не из argv[0].
    void do_update(const std::string& self_path);
    // Удаляет остатки прошлого обновления (<exe>.old на Windows, недокачанный <exe>.new).
    void cleanup_update_leftovers();
}

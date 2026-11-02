#pragma once
#include <string>

namespace nd {
    // Проверка обновлений по указанному каналу ("stable", "beta", "nightly")
    void check_update(const std::string& channel = "stable");
    
    // Самообновление CLI с поддержкой каналов и мульти-ассетного матчинга
    void do_update(const std::string& self_path, const std::string& channel = "stable");
    
    // Удаляет остатки прошлого обновления (<exe>.old, <exe>.new).
    void cleanup_update_leftovers();
}

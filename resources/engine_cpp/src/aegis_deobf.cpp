// aegis_deobf.cpp - 100% нативная оффлайн-деобфускация Aegis Forge (.aeg файлов и vmcodes)
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>

#include "aegis_deobf.hpp"
#include "sha256.hpp"
#include "classfile.hpp"
#include "ast_nodes.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace nd {

namespace {

static const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static const uint8_t RCON[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

inline uint8_t xtime(uint8_t x) { return (x << 1) ^ (((x >> 7) & 1) * 0x1b); }

std::vector<std::array<uint8_t, 16>> aes256_expand_key(const uint8_t* key32) {
    std::vector<std::array<uint8_t, 4>> w(60);
    for (int i = 0; i < 8; ++i) {
        w[i] = {key32[i * 4 + 0], key32[i * 4 + 1], key32[i * 4 + 2], key32[i * 4 + 3]};
    }
    for (int i = 8; i < 60; ++i) {
        auto temp = w[i - 1];
        if (i % 8 == 0) {
            std::array<uint8_t, 4> rot = {temp[1], temp[2], temp[3], temp[0]};
            for (int k = 0; k < 4; ++k) rot[k] = SBOX[rot[k]];
            rot[0] ^= RCON[i / 8 - 1];
            temp = rot;
        } else if (i % 8 == 4) {
            for (int k = 0; k < 4; ++k) temp[k] = SBOX[temp[k]];
        }
        for (int k = 0; k < 4; ++k) w[i][k] = w[i - 8][k] ^ temp[k];
    }
    std::vector<std::array<uint8_t, 16>> round_keys(15);
    for (int r = 0; r < 15; ++r) {
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                round_keys[r][c * 4 + row] = w[r * 4 + c][row];
            }
        }
    }
    return round_keys;
}

void aes_encrypt_block(const uint8_t* in, uint8_t* out, const std::vector<std::array<uint8_t, 16>>& round_keys) {
    uint8_t state[16];
    for (int i = 0; i < 16; ++i) state[i] = in[i] ^ round_keys[0][i];

    for (int round = 1; round <= 13; ++round) {
        for (int i = 0; i < 16; ++i) state[i] = SBOX[state[i]];

        uint8_t r1 = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = r1;
        uint8_t r2a = state[2], r2b = state[6];
        state[2] = state[10]; state[6] = state[14]; state[10] = r2a; state[14] = r2b;
        uint8_t r3 = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = r3;

        for (int c = 0; c < 4; ++c) {
            uint8_t a0 = state[c * 4 + 0], a1 = state[c * 4 + 1], a2 = state[c * 4 + 2], a3 = state[c * 4 + 3];
            uint8_t t = a0 ^ a1 ^ a2 ^ a3;
            state[c * 4 + 0] ^= t ^ xtime(a0 ^ a1);
            state[c * 4 + 1] ^= t ^ xtime(a1 ^ a2);
            state[c * 4 + 2] ^= t ^ xtime(a2 ^ a3);
            state[c * 4 + 3] ^= t ^ xtime(a3 ^ a0);
        }

        for (int i = 0; i < 16; ++i) state[i] ^= round_keys[round][i];
    }

    for (int i = 0; i < 16; ++i) state[i] = SBOX[state[i]];
    uint8_t r1 = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = r1;
    uint8_t r2a = state[2], r2b = state[6];
    state[2] = state[10]; state[6] = state[14]; state[10] = r2a; state[14] = r2b;
    uint8_t r3 = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = r3;
    for (int i = 0; i < 16; ++i) out[i] = state[i] ^ round_keys[14][i];
}

std::vector<uint8_t> aes256_gcm_decrypt(const uint8_t* key32, const uint8_t* iv12,
                                        const uint8_t* ciphertext, size_t cipher_len) {
    if (cipher_len < 16) return {};
    size_t data_len = cipher_len - 16;
    std::vector<uint8_t> plaintext(data_len);

    auto round_keys = aes256_expand_key(key32);

    uint8_t counter[16];
    for (int i = 0; i < 12; ++i) counter[i] = iv12[i];
    counter[12] = 0; counter[13] = 0; counter[14] = 0; counter[15] = 2; // Initial data counter in GCM

    size_t offset = 0;
    uint8_t stream[16];
    while (offset < data_len) {
        aes_encrypt_block(counter, stream, round_keys);
        for (int i = 15; i >= 12; --i) {
            if (++counter[i] != 0) break;
        }
        size_t chunk = std::min<size_t>(16, data_len - offset);
        for (size_t i = 0; i < chunk; ++i) {
            plaintext[offset + i] = ciphertext[offset + i] ^ stream[i];
        }
        offset += chunk;
    }
    return plaintext;
}

inline uint64_t butterfly64(uint64_t n) {
    n = ((n & 0x5555555555555555ULL) << 1) | ((n & 0xAAAAAAAAAAAAAAAAULL) >> 1);
    n = ((n & 0x3333333333333333ULL) << 2) | ((n & 0xCCCCCCCCCCCCCCCCULL) >> 2);
    n = ((n & 0x0F0F0F0F0F0F0F0FULL) << 4) | ((n & 0xF0F0F0F0F0F0F0F0ULL) >> 4);
    n = ((n & 0x00FF00FF00FF00FFULL) << 8) | ((n & 0xFF00FF00FF00FF00ULL) >> 8);
    return n;
}

uint64_t apply_transform(uint64_t val, const std::string& trans) {
    auto pos = trans.find(':');
    if (pos == std::string::npos) return val;
    std::string op = trans.substr(0, pos);
    std::string arg_str = trans.substr(pos + 1);
    for (char& c : op) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    uint64_t arg = 0;
    try {
        if (arg_str.size() > 2 && (arg_str[0] == '0' && (arg_str[1] == 'x' || arg_str[1] == 'X'))) {
            arg = std::stoull(arg_str.substr(2), nullptr, 16);
        } else {
            arg = std::stoull(arg_str);
        }
    } catch (...) {
        return val;
    }

    if (op == "xor") return val ^ arg;
    if (op == "rol") {
        arg &= 63;
        return (val << arg) | (val >> (64 - arg));
    }
    if (op == "shr") {
        arg &= 63;
        return val >> arg;
    }
    if (op == "shl") {
        arg &= 63;
        return val << arg;
    }
    if (op == "and") return val & arg;
    if (op == "or") return val | arg;
    if (op == "add") return val + arg;
    if (op == "sub") return val - arg;
    if (op == "mul") return val * arg;
    return val;
}

static std::mutex g_vm_mutex;
static std::map<uint64_t, std::shared_ptr<AegisIRMethod>> g_active_vm_methods;

} // namespace

void set_active_aegis_vm_methods(const std::map<uint64_t, std::shared_ptr<AegisIRMethod>>& vm_methods) {
    std::lock_guard<std::mutex> lock(g_vm_mutex);
    g_active_vm_methods = vm_methods;
}

std::shared_ptr<AegisIRMethod> get_aegis_vm_method(uint64_t token) {
    std::lock_guard<std::mutex> lock(g_vm_mutex);
    auto it = g_active_vm_methods.find(token);
    if (it != g_active_vm_methods.end()) return it->second;
    return nullptr;
}

void clear_active_aegis_vm() {
    std::lock_guard<std::mutex> lock(g_vm_mutex);
    g_active_vm_methods.clear();
}

bool is_aegis_archive(const std::vector<std::string>& all_names) {
    for (const auto& name : all_names) {
        if (name == "aegis/meta.aeg" || name.find(".yml.aeg") != std::string::npos ||
            name.find("aegis/blobs/") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool is_aegis_trap_class(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) lower.push_back((char)std::tolower((unsigned char)c));

    if (lower.find("aegisarmortrap") != std::string::npos ||
        lower.find("aegisgrinder") != std::string::npos ||
        lower.find("aegissecurityguard") != std::string::npos ||
        lower.find("dev/aegisforge") != std::string::npos ||
        lower.find("dev.aegisforge") != std::string::npos ||
        lower.find("aegisvm") != std::string::npos ||
        lower.find("aegisindybootstrap") != std::string::npos ||
        lower.find("aegisarmor") != std::string::npos) {
        return true;
    }
    return false;
}

AegisDeobfResult deobfuscate_aegis_files(ZipReader& zr, const std::string& out_dir, const std::string& res_dir) {
    (void)out_dir;
    AegisDeobfResult result;
    auto names = zr.namelist();
    if (!is_aegis_archive(names)) {
        return result;
    }
    result.is_aegis = true;

    if (!zr.has_file("aegis/meta.aeg")) {
        result.error = "Missing aegis/meta.aeg";
        return result;
    }

    auto meta_bytes = zr.read("aegis/meta.aeg");
    std::string meta_str(meta_bytes.begin(), meta_bytes.end());

    // 1. Стандартный SHA-256 для вычисления asset_key
    auto asset_sha = sha256(meta_bytes);
    uint8_t asset_key[32];
    std::memcpy(asset_key, asset_sha.data(), 32);

    // 2. Расшифровка дефолтных конфигов (defaults/*.yml.aeg)
    std::map<std::string, std::map<std::string, uint64_t>> config_maps;
    for (const auto& name : names) {
        if (name.rfind("defaults/", 0) == 0 && name.size() > 4 && name.substr(name.size() - 4) == ".aeg") {
            std::string clean = name.substr(9, name.size() - 9 - 4); // "config.yml"
            auto enc = zr.read(name);
            if (enc.size() > 28) {
                auto dec = aes256_gcm_decrypt(asset_key, enc.data(), enc.data() + 12, enc.size() - 12);
                if (!dec.empty()) {
                    // Сохраняем в resources чистый YAML
                    fs::path target_path = fs::u8path(res_dir) / fs::u8path(clean);
                    fs::create_directories(target_path.parent_path());
                    std::ofstream out(target_path.u8string(), std::ios::binary);
                    out.write(reinterpret_cast<const char*>(dec.data()), dec.size());
                    result.decrypted_files++;

                    // Парсим целочисленные значения фрагментов
                    std::string yml_text(dec.begin(), dec.end());
                    std::istringstream yiss(yml_text);
                    std::string yline;
                    while (std::getline(yiss, yline)) {
                        while (!yline.empty() && (yline.back() == '\r' || yline.back() == '\n' || yline.back() == ' ')) yline.pop_back();
                        if (yline.empty() || yline[0] == '#') continue;
                        auto cpos = yline.find(':');
                        if (cpos != std::string::npos) {
                            std::string k = yline.substr(0, cpos);
                            while (!k.empty() && k.front() == ' ') k.erase(k.begin());
                            while (!k.empty() && k.back() == ' ') k.pop_back();
                            std::string v = yline.substr(cpos + 1);
                            auto hpos = v.find('#');
                            if (hpos != std::string::npos) v = v.substr(0, hpos);
                            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                            while (!v.empty() && v.back() == ' ') v.pop_back();
                            try {
                                uint64_t num = std::stoull(v);
                                config_maps[clean][k] = num;
                            } catch (...) {}
                        }
                    }
                }
            }
        }
    }

    // 3. Чтение фрагментов meta.aeg
    std::istringstream iss(meta_str);
    std::string line;
    std::vector<std::tuple<std::string, std::string, std::string>> fragments;

    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string file, key, trans;
        if (std::getline(ss, file, '|') && std::getline(ss, key, '|') && std::getline(ss, trans)) {
            fragments.emplace_back(file, key, trans);
        }
    }

    // 4. Сборка MasterKey через алгоритм бабочки (KeyAssembler)
    uint64_t s0 = 7640891576956012808ULL;
    uint64_t s1 = static_cast<uint64_t>(-4942790177534073029LL);
    uint64_t s2 = 4354685564939489101ULL;
    uint64_t s3 = static_cast<uint64_t>(-6534734903238641935LL);

    int idx = 0;
    for (const auto& [f, k, trans] : fragments) {
        uint64_t orig = 100ULL;
        auto fit = config_maps.find(f);
        if (fit != config_maps.end()) {
            auto kit = fit->second.find(k);
            if (kit != fit->second.end()) {
                orig = kit->second;
            }
        }
        uint64_t f_val = apply_transform(orig, trans);
        uint64_t b = butterfly64(f_val ^ (2611923443488327891ULL + idx * 4919ULL));
        uint64_t v14 = s0 ^ b;
        uint64_t v16 = s1;
        for (int r = 0; r < 4; ++r) {
            uint64_t v19 = (r * 5871781006564002453ULL + b);
            uint64_t v21 = butterfly64(v16 ^ v19) ^ (v16 * static_cast<uint64_t>(-7046029254386353131LL));
            uint64_t v25 = v14 ^ v21;
            v14 = v16;
            v16 = v25;
        }
        s0 = v14;
        s1 = v16;
        s2 ^= butterfly64(s0 + f_val);
        s3 ^= (s1 * static_cast<uint64_t>(-3335678366873096957LL));
        idx++;
    }

    uint8_t master_key[32];
    std::memcpy(master_key, &s0, 8);
    std::memcpy(master_key + 8, &s1, 8);
    std::memcpy(master_key + 16, &s2, 8);
    std::memcpy(master_key + 24, &s3, 8);

    // 5. Расшифровка opmap.aeg (таблица соответствия физических опкодов логическим)
    uint8_t p2l[256];
    for (int i = 0; i < 256; ++i) p2l[i] = static_cast<uint8_t>(i);
    if (zr.has_file("aegis/opmap.aeg")) {
        auto enc = zr.read("aegis/opmap.aeg");
        if (enc.size() > 28) {
            auto dec = aes256_gcm_decrypt(master_key, enc.data(), enc.data() + 12, enc.size() - 12);
            if (dec.size() >= 2) {
                uint16_t cnt = (static_cast<uint16_t>(dec[0]) << 8) | dec[1];
                size_t o = 2;
                for (uint16_t i = 0; i < cnt && o + 1 < dec.size(); ++i) {
                    uint8_t log_op = dec[o++];
                    uint8_t phy_op = dec[o++];
                    p2l[phy_op] = log_op;
                }
            }
        }
    }

    // 6. Расшифровка blobs/*.aeg и остальных ресурсов
    for (const auto& name : names) {
        if (name.size() > 4 && name.substr(name.size() - 4) == ".aeg") {
            // defaults/*.yml.aeg уже обработаны выше
            if (name.rfind("defaults/", 0) == 0) continue;

            bool is_blob = (name.find("aegis/blobs/") != std::string::npos);
            auto enc = zr.read(name);
            if (enc.size() <= 28) continue;

            auto dec = is_blob ? aes256_gcm_decrypt(master_key, enc.data(), enc.data() + 12, enc.size() - 12)
                               : aes256_gcm_decrypt(asset_key, enc.data(), enc.data() + 12, enc.size() - 12);
            if (dec.empty()) {
                dec = is_blob ? aes256_gcm_decrypt(asset_key, enc.data(), enc.data() + 12, enc.size() - 12)
                              : aes256_gcm_decrypt(master_key, enc.data(), enc.data() + 12, enc.size() - 12);
            }
            if (dec.empty()) continue;

            result.decrypted_files++;

            if (is_blob) {
                // Извлекаем hex token из имени файла (например, aegis/blobs/6e4d08bc.aeg -> 0x6e4d08bc)
                std::string base_fn = fs::u8path(name).stem().u8string();
                uint64_t token = 0;
                try {
                    token = std::stoull(base_fn, nullptr, 16);
                } catch (...) {}

                // Вариант 1: Спрятанный целый .class файл (Magic 0xCAFEBABE)
                if (dec.size() >= 4 && dec[0] == 0xCA && dec[1] == 0xFE && dec[2] == 0xBA && dec[3] == 0xBE) {
                    try {
                        ClassFile cf(dec);
                        result.recovered_classes.push_back({cf.this_class_name, dec});
                        result.recovered_classes_count++;
                    } catch (...) {}
                    continue; // Не пишем в resources
                }

                // Вариант 2: Защищённый AegisIRMethod (Magic 0xAE695301)
                if (dec.size() >= 12 && dec[0] == 0xAE && dec[1] == 0x69 && dec[2] == 0x53 && dec[3] == 0x01) {
                    auto ir = std::make_shared<AegisIRMethod>();
                    ir->token_hex = base_fn;
                    ir->version = (static_cast<uint16_t>(dec[4]) << 8) | dec[5];
                    ir->max_stack = (static_cast<uint16_t>(dec[6]) << 8) | dec[7];
                    ir->max_locals = (static_cast<uint16_t>(dec[8]) << 8) | dec[9];
                    uint16_t cp_cnt = (static_cast<uint16_t>(dec[10]) << 8) | dec[11];
                    size_t o = 12;

                    for (uint16_t c = 0; c < cp_cnt && o < dec.size(); ++c) {
                        AegisIRConstant entry;
                        entry.tag = dec[o++];
                        if (entry.tag == 1) { // String
                            if (o + 2 <= dec.size()) {
                                uint16_t len = (static_cast<uint16_t>(dec[o]) << 8) | dec[o + 1];
                                o += 2;
                                if (o + len <= dec.size()) {
                                    entry.str_val = std::string(reinterpret_cast<const char*>(&dec[o]), len);
                                    o += len;
                                }
                            }
                        } else if (entry.tag == 2) { // Int
                            if (o + 4 <= dec.size()) {
                                entry.int_val = static_cast<int32_t>((static_cast<uint32_t>(dec[o]) << 24) |
                                                                     (static_cast<uint32_t>(dec[o + 1]) << 16) |
                                                                     (static_cast<uint32_t>(dec[o + 2]) << 8) |
                                                                     dec[o + 3]);
                                o += 4;
                            }
                        } else if (entry.tag == 3) { // Long
                            if (o + 8 <= dec.size()) {
                                uint64_t hi = (static_cast<uint32_t>(dec[o]) << 24) |
                                              (static_cast<uint32_t>(dec[o + 1]) << 16) |
                                              (static_cast<uint32_t>(dec[o + 2]) << 8) | dec[o + 3];
                                uint64_t lo = (static_cast<uint32_t>(dec[o + 4]) << 24) |
                                              (static_cast<uint32_t>(dec[o + 5]) << 16) |
                                              (static_cast<uint32_t>(dec[o + 6]) << 8) | dec[o + 7];
                                entry.long_val = static_cast<int64_t>((hi << 32) | lo);
                                o += 8;
                            }
                        } else if (entry.tag == 4) { // Double
                            if (o + 8 <= dec.size()) {
                                double dval = 0.0;
                                std::memcpy(&dval, &dec[o], 8);
                                entry.double_val = dval;
                                o += 8;
                            }
                        } else if (entry.tag == 5) { // MemberRef (owner, name, desc)
                            auto read_str = [&](std::string& s) {
                                if (o + 2 <= dec.size()) {
                                    uint16_t len = (static_cast<uint16_t>(dec[o]) << 8) | dec[o + 1];
                                    o += 2;
                                    if (o + len <= dec.size()) {
                                        s = std::string(reinterpret_cast<const char*>(&dec[o]), len);
                                        o += len;
                                    }
                                }
                            };
                            read_str(entry.ref_owner);
                            read_str(entry.ref_name);
                            read_str(entry.ref_desc);
                        }
                        ir->constant_pool.push_back(std::move(entry));
                    }

                    // Таблица исключений
                    if (o + 2 <= dec.size()) {
                        uint16_t exc_cnt = (static_cast<uint16_t>(dec[o]) << 8) | dec[o + 1];
                        o += 2;
                        for (uint16_t e = 0; e < exc_cnt && o + 14 <= dec.size(); ++e) {
                            AegisExceptionEntry ee;
                            ee.start_pc = (static_cast<uint32_t>(dec[o]) << 24) | (static_cast<uint32_t>(dec[o + 1]) << 16) |
                                          (static_cast<uint32_t>(dec[o + 2]) << 8) | dec[o + 3];
                            ee.end_pc = (static_cast<uint32_t>(dec[o + 4]) << 24) | (static_cast<uint32_t>(dec[o + 5]) << 16) |
                                        (static_cast<uint32_t>(dec[o + 6]) << 8) | dec[o + 7];
                            ee.handler_pc = (static_cast<uint32_t>(dec[o + 8]) << 24) | (static_cast<uint32_t>(dec[o + 9]) << 16) |
                                            (static_cast<uint32_t>(dec[o + 10]) << 8) | dec[o + 11];
                            ee.catch_type_idx = (static_cast<uint16_t>(dec[o + 12]) << 8) | dec[o + 13];
                            o += 14;
                            ir->exceptions.push_back(ee);
                        }
                    }

                    // Байткод VM
                    if (o + 4 <= dec.size()) {
                        uint32_t bc_len = (static_cast<uint32_t>(dec[o]) << 24) | (static_cast<uint32_t>(dec[o + 1]) << 16) |
                                          (static_cast<uint32_t>(dec[o + 2]) << 8) | dec[o + 3];
                        o += 4;
                        if (o + bc_len <= dec.size()) {
                            ir->bytecode.assign(dec.begin() + o, dec.begin() + o + bc_len);
                            // Трансляция физических опкодов в логические прямо на месте!
                            for (size_t b = 0; b < ir->bytecode.size(); ++b) {
                                ir->bytecode[b] = p2l[ir->bytecode[b]];
                            }
                        }
                    }

                    result.vm_methods[token] = ir;
                    result.recovered_vm_methods_count++;
                    continue; // Не пишем в resources
                }
            }

            // Обычные ресурсы (.yml и прочее) сохраняем в resources без суффикса .aeg
            std::string clean = name.substr(0, name.size() - 4);
            fs::path target_path = fs::u8path(res_dir) / fs::u8path(clean);
            fs::create_directories(target_path.parent_path());
            std::ofstream out(target_path.u8string(), std::ios::binary);
            out.write(reinterpret_cast<const char*>(dec.data()), dec.size());
        }
    }

    set_active_aegis_vm_methods(result.vm_methods);
    result.success = true;
    return result;
}

ExprPtr translate_aegis_ir_method(const AegisIRMethod& ir,
                                  const std::vector<ExprPtr>& call_args,
                                  MethodCtx& ctx) {
    (void)ctx;
    if (ir.bytecode.empty()) return nullptr;

    try {
        std::vector<ExprPtr> stack;
        std::vector<ExprPtr> locals(std::max<size_t>(ir.max_locals + 8, call_args.size() + 16));
        for (size_t i = 0; i < call_args.size(); ++i) {
            locals[i] = call_args[i];
        }

        size_t pc = 0;
        size_t step_count = 0;
        const auto& bc = ir.bytecode;
        const auto& cp = ir.constant_pool;

        auto safe_pop = [&]() -> ExprPtr {
            if (stack.empty()) return std::make_shared<Const>("null", "Object");
            auto val = stack.back();
            stack.pop_back();
            return val ? val : std::make_shared<Const>("null", "Object");
        };

        while (pc < bc.size() && ++step_count < 10000) {
            uint8_t op = bc[pc++];

            switch (op) {
                case 0: // NOP
                    break;
                case 1: { // ALOAD_LOCAL
                    if (pc < bc.size()) {
                        uint8_t idx = bc[pc++];
                        if (idx < locals.size() && locals[idx]) {
                            stack.push_back(locals[idx]);
                        } else if (idx == 0) {
                            stack.push_back(std::make_shared<This>());
                        } else {
                            stack.push_back(std::make_shared<Local>("var" + std::to_string(idx), "Object"));
                        }
                    }
                    break;
                }
                case 2: { // ASTORE_LOCAL
                    if (pc < bc.size()) {
                        uint8_t idx = bc[pc++];
                        auto val = safe_pop();
                        if (idx < locals.size()) locals[idx] = val;
                    }
                    break;
                }
                case 3: { // DUP
                    if (!stack.empty()) stack.push_back(stack.back());
                    break;
                }
                case 4: { // POP
                    if (!stack.empty()) stack.pop_back();
                    break;
                }
                case 5: { // SWAP
                    if (stack.size() >= 2) std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
                    break;
                }
                case 6: { // ACONST_NULL
                    stack.push_back(std::make_shared<Const>("null", "Object"));
                    break;
                }
                case 7: { // ICONST
                    if (pc + 4 <= bc.size()) {
                        int32_t val = (static_cast<uint32_t>(bc[pc]) << 24) | (static_cast<uint32_t>(bc[pc+1]) << 16) |
                                      (static_cast<uint32_t>(bc[pc+2]) << 8) | bc[pc+3];
                        pc += 4;
                        stack.push_back(std::make_shared<Const>(std::to_string(val), "int"));
                    }
                    break;
                }
                case 8: { // LCONST
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        int64_t val = (idx < cp.size()) ? cp[idx].long_val : 0;
                        stack.push_back(std::make_shared<Const>(std::to_string(val) + "L", "long"));
                    }
                    break;
                }
                case 9: { // DCONST
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        double val = (idx < cp.size()) ? cp[idx].double_val : 0.0;
                        stack.push_back(std::make_shared<Const>(std::to_string(val), "double"));
                    }
                    break;
                }
                case 10: { // SCONST
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        std::string val = (idx < cp.size()) ? cp[idx].str_val : "";
                        stack.push_back(std::make_shared<Const>("\"" + val + "\"", "String"));
                    }
                    break;
                }
                case 11: { // BCONST
                    if (pc < bc.size()) {
                        uint8_t val = bc[pc++];
                        stack.push_back(std::make_shared<Const>(val ? "true" : "false", "boolean"));
                    }
                    break;
                }
                case 16: // IADD
                case 17: // ISUB
                case 18: // IMUL
                case 19: // IDIV
                case 21: // IAND
                case 22: // IOR
                case 23: { // IXOR
                    auto b = safe_pop();
                    auto a = safe_pop();
                    std::string bop = "+";
                    if (op == 17) bop = "-";
                    else if (op == 18) bop = "*";
                    else if (op == 19) bop = "/";
                    else if (op == 21) bop = "&";
                    else if (op == 22) bop = "|";
                    else if (op == 23) bop = "^";
                    stack.push_back(std::make_shared<BinOp>(bop, a, b, "int"));
                    break;
                }
                case 32: case 33: case 34: case 35: case 36: case 37: { // Comparisons
                    auto b = safe_pop();
                    auto a = safe_pop();
                    std::string cop = "==";
                    if (op == 33) cop = "!=";
                    else if (op == 34) cop = "<";
                    else if (op == 35) cop = ">=";
                    else if (op == 36) cop = ">";
                    else if (op == 37) cop = "<=";
                    stack.push_back(std::make_shared<BinOp>(cop, a, b, "boolean"));
                    break;
                }
                case 38: // IF_TRUE
                case 39: // IF_FALSE
                case 41: // IF_NULL
                case 42: { // IF_NONNULL
                    if (pc + 2 <= bc.size()) {
                        pc += 2; // пропускаем адрес ветвления
                        safe_pop();
                    }
                    break;
                }
                case 40: { // GOTO
                    if (pc + 2 <= bc.size()) {
                        uint16_t target = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        if (target < bc.size()) pc = target;
                        else pc += 2;
                    }
                    break;
                }
                case 52: { // NEW_INSTANCE
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        std::string cls_name = (idx < cp.size()) ? cp[idx].str_val : "Object";
                        stack.push_back(std::make_shared<NewObject>(cls_name, std::vector<ExprPtr>{}));
                    }
                    break;
                }
                case 53: { // FIELD_GET
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        if (idx < cp.size()) {
                            ExprPtr target = safe_pop();
                            stack.push_back(std::make_shared<FieldAccess>(target, cp[idx].ref_name, cp[idx].ref_desc, false));
                        }
                    }
                    break;
                }
                case 54: { // FIELD_SET
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto val = safe_pop();
                        auto target = safe_pop();
                        if (idx < cp.size()) {
                            auto fa = std::make_shared<FieldAccess>(target, cp[idx].ref_name, cp[idx].ref_desc, false);
                            stack.push_back(std::make_shared<Assign>(fa, val));
                        }
                    }
                    break;
                }
                case 55: { // FIELD_GET_STATIC
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        if (idx < cp.size()) {
                            auto owner_var = std::make_shared<Local>(cp[idx].ref_owner, cp[idx].ref_owner);
                            stack.push_back(std::make_shared<FieldAccess>(owner_var, cp[idx].ref_name, cp[idx].ref_desc, true));
                        }
                    }
                    break;
                }
                case 56: { // FIELD_SET_STATIC
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto val = safe_pop();
                        if (idx < cp.size()) {
                            auto owner_var = std::make_shared<Local>(cp[idx].ref_owner, cp[idx].ref_owner);
                            auto fa = std::make_shared<FieldAccess>(owner_var, cp[idx].ref_name, cp[idx].ref_desc, true);
                            stack.push_back(std::make_shared<Assign>(fa, val));
                        }
                    }
                    break;
                }
                case 48: // INVOKE_VIRTUAL
                case 50: // INVOKE_SPECIAL
                case 51: { // INVOKE_INTERFACE
                    if (pc + 3 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        uint8_t arg_cnt = bc[pc+2];
                        pc += 3;
                        if (idx < cp.size()) {
                            std::vector<ExprPtr> args(arg_cnt);
                            for (int a = arg_cnt - 1; a >= 0; --a) {
                                args[a] = safe_pop();
                            }
                            ExprPtr target = safe_pop();

                            if (cp[idx].ref_name == "<init>") {
                                if (target && target->kind == ExprKind::NewObject) {
                                    auto no = std::dynamic_pointer_cast<NewObject>(target);
                                    for (auto& a : args) {
                                        if (a == no) a = std::make_shared<Const>("this", "Object");
                                    }
                                    no->args = args;
                                    if (stack.empty() || stack.back() != no) {
                                        stack.push_back(no);
                                    }
                                } else if (target && (target->kind == ExprKind::This || target->kind == ExprKind::Local)) {
                                    // super.<init>(...) call
                                    // void, do not push
                                } else {
                                    stack.push_back(std::make_shared<NewObject>(cp[idx].ref_owner, args));
                                }
                            } else {
                                stack.push_back(std::make_shared<MethodCall>(target, cp[idx].ref_name, args, cp[idx].ref_desc, false));
                            }
                        }
                    }
                    break;
                }
                case 49: { // INVOKE_STATIC
                    if (pc + 3 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        uint8_t arg_cnt = bc[pc+2];
                        pc += 3;
                        if (idx < cp.size()) {
                            std::vector<ExprPtr> args(arg_cnt);
                            for (int a = arg_cnt - 1; a >= 0; --a) {
                                args[a] = safe_pop();
                            }
                            auto owner_var = std::make_shared<Local>(cp[idx].ref_owner, cp[idx].ref_owner);
                            stack.push_back(std::make_shared<MethodCall>(owner_var, cp[idx].ref_name, args, cp[idx].ref_desc, true));
                        }
                    }
                    break;
                }
                case 64: { // ARRAY_NEW
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto count = safe_pop();
                        std::string elem_t = (idx < cp.size()) ? cp[idx].str_val : "Object";
                        stack.push_back(std::make_shared<NewArray>(elem_t, std::vector<ExprPtr>{count}));
                    }
                    break;
                }
                case 65: { // ARRAY_LOAD
                    auto arr_idx = safe_pop();
                    auto arr = safe_pop();
                    stack.push_back(std::make_shared<ArrayAccess>(arr, arr_idx));
                    break;
                }
                case 66: { // ARRAY_STORE
                    auto val = safe_pop();
                    auto arr_idx = safe_pop();
                    auto arr = safe_pop();
                    stack.push_back(std::make_shared<Assign>(std::make_shared<ArrayAccess>(arr, arr_idx), val));
                    break;
                }
                case 67: { // ARRAY_LENGTH
                    auto arr = safe_pop();
                    stack.push_back(std::make_shared<FieldAccess>(arr, "length", "int", false));
                    break;
                }
                case 68: { // CHECKCAST
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto val = safe_pop();
                        std::string cast_t = (idx < cp.size()) ? cp[idx].str_val : "Object";
                        stack.push_back(std::make_shared<Cast>(cast_t, val));
                    }
                    break;
                }
                case 69: { // INSTANCEOF
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto val = safe_pop();
                        std::string check_t = (idx < cp.size()) ? cp[idx].str_val : "Object";
                        stack.push_back(std::make_shared<InstanceOf>(val, check_t));
                    }
                    break;
                }
                case 80: // RETURN_VAL
                    if (!stack.empty()) return stack.back();
                    return nullptr;
                case 81: // RETURN_VOID
                    if (!stack.empty()) return stack.back();
                    return std::make_shared<Raw>("");
                    if (!stack.empty()) return stack.back();
                    return std::make_shared<Const>("/* void */", "void");
                default:
                    break;
            }
        }

        if (!stack.empty()) return stack.back();
    } catch (...) {}
    return nullptr;
}

BytecodeStructureEstimate estimate_clean_structure(const std::vector<std::string>& class_names,
                                                  ZipReader& zr) {
    (void)zr;
    BytecodeStructureEstimate est;
    est.expected_classes = 0;
    est.expected_methods = 0;
    est.expected_lines = 0;

    for (const auto& n : class_names) {
        if (is_aegis_trap_class(n)) {
            est.trash_trap_classes++;
            continue;
        }

        if (n.find("libs/hikari") != std::string::npos ||
            n.find("com/mysql/") != std::string::npos ||
            n.find("com/google/protobuf") != std::string::npos) {
            est.library_classes++;
            continue;
        }

        est.expected_classes++;
        est.expected_methods += 7;
        est.expected_lines += 120;
    }

    return est;
}

bool save_nano_inf(const std::string& out_dir, const NanoInfState& state) {
    fs::path p = fs::u8path(out_dir) / ".nano_inf";
    std::ofstream out(p.u8string());
    if (!out.is_open()) return false;
    out << "jar_sha256=" << state.jar_sha256 << "\n";
    out << "total_classes=" << state.total_classes << "\n";
    out << "decompiled_classes=" << state.decompiled_classes << "\n";
    out << "last_processed_class=" << state.last_processed_class << "\n";
    out << "expected_clean_classes=" << state.expected_clean_classes << "\n";
    out << "expected_clean_methods=" << state.expected_clean_methods << "\n";
    out << "expected_clean_lines=" << state.expected_clean_lines << "\n";
    out << "is_completed=" << (state.is_completed ? "1" : "0") << "\n";
    out.close();
#ifdef _WIN32
    SetFileAttributesW(p.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
    return true;
}

std::optional<NanoInfState> load_nano_inf(const std::string& out_dir, const std::string& current_jar_sha256) {
    fs::path p = fs::u8path(out_dir) / ".nano_inf";
    if (!fs::exists(p)) return std::nullopt;
    std::ifstream in(p.u8string());
    if (!in.is_open()) return std::nullopt;
    NanoInfState s;
    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos + 1);
        if (k == "jar_sha256") s.jar_sha256 = v;
        else if (k == "total_classes") s.total_classes = std::stoi(v);
        else if (k == "decompiled_classes") s.decompiled_classes = std::stoi(v);
        else if (k == "last_processed_class") s.last_processed_class = v;
        else if (k == "expected_clean_classes") s.expected_clean_classes = std::stoi(v);
        else if (k == "expected_clean_methods") s.expected_clean_methods = std::stoi(v);
        else if (k == "expected_clean_lines") s.expected_clean_lines = std::stoi(v);
        else if (k == "is_completed") s.is_completed = (v == "1");
    }
    if (s.jar_sha256 != current_jar_sha256) return std::nullopt;
    return s;
}

} // namespace nd

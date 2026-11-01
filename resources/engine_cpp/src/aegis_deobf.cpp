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
#include <thread>

#include "aegis_deobf.hpp"
#include "sha256.hpp"
#include "classfile.hpp"
#include "ast_nodes.hpp"
#include "javatypes.hpp"
#include "stackvm.hpp"
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

struct Aes256RoundKeys {
    uint32_t rk[15][4];
};

static uint32_t Te0[256], Te1[256], Te2[256], Te3[256];
static bool s_aes_tables_init = false;

static void init_aes_tables() {
    if (s_aes_tables_init) return;
    for (int i = 0; i < 256; ++i) {
        uint8_t s = SBOX[i];
        uint8_t x = xtime(s);
        uint32_t w = (static_cast<uint32_t>(x) << 24) |
                     (static_cast<uint32_t>(s) << 16) |
                     (static_cast<uint32_t>(s) << 8)  |
                     (static_cast<uint32_t>(x ^ s));
        Te0[i] = w;
        Te1[i] = (w >> 8) | (w << 24);
        Te2[i] = (w >> 16) | (w << 16);
        Te3[i] = (w >> 24) | (w << 8);
    }
    s_aes_tables_init = true;
}

inline uint32_t rd_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline void wr_u32_be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

void aes256_expand_key_fast(const uint8_t* key32, Aes256RoundKeys& out_keys) {
    init_aes_tables();
    uint32_t w[60];
    for (int i = 0; i < 8; ++i) {
        w[i] = rd_u32_be(key32 + i * 4);
    }
    for (int i = 8; i < 60; ++i) {
        uint32_t temp = w[i - 1];
        if (i % 8 == 0) {
            uint8_t r0 = SBOX[static_cast<uint8_t>(temp >> 16)];
            uint8_t r1 = SBOX[static_cast<uint8_t>(temp >> 8)];
            uint8_t r2 = SBOX[static_cast<uint8_t>(temp)];
            uint8_t r3 = SBOX[static_cast<uint8_t>(temp >> 24)];
            temp = ((static_cast<uint32_t>(r0) ^ RCON[i / 8 - 1]) << 24) |
                   (static_cast<uint32_t>(r1) << 16) |
                   (static_cast<uint32_t>(r2) << 8) |
                   static_cast<uint32_t>(r3);
        } else if (i % 8 == 4) {
            uint8_t r0 = SBOX[static_cast<uint8_t>(temp >> 24)];
            uint8_t r1 = SBOX[static_cast<uint8_t>(temp >> 16)];
            uint8_t r2 = SBOX[static_cast<uint8_t>(temp >> 8)];
            uint8_t r3 = SBOX[static_cast<uint8_t>(temp)];
            temp = (static_cast<uint32_t>(r0) << 24) |
                   (static_cast<uint32_t>(r1) << 16) |
                   (static_cast<uint32_t>(r2) << 8) |
                   static_cast<uint32_t>(r3);
        }
        w[i] = w[i - 8] ^ temp;
    }
    for (int r = 0; r < 15; ++r) {
        for (int c = 0; c < 4; ++c) {
            out_keys.rk[r][c] = w[r * 4 + c];
        }
    }
}

inline void aes_encrypt_block_fast(const uint8_t* in, uint8_t* out, const Aes256RoundKeys& keys) {
    uint32_t s0 = rd_u32_be(in + 0)  ^ keys.rk[0][0];
    uint32_t s1 = rd_u32_be(in + 4)  ^ keys.rk[0][1];
    uint32_t s2 = rd_u32_be(in + 8)  ^ keys.rk[0][2];
    uint32_t s3 = rd_u32_be(in + 12) ^ keys.rk[0][3];

    for (int r = 1; r < 14; ++r) {
        uint32_t t0 = Te0[static_cast<uint8_t>(s0 >> 24)] ^
                      Te1[static_cast<uint8_t>(s1 >> 16)] ^
                      Te2[static_cast<uint8_t>(s2 >> 8)]  ^
                      Te3[static_cast<uint8_t>(s3)]       ^ keys.rk[r][0];
        uint32_t t1 = Te0[static_cast<uint8_t>(s1 >> 24)] ^
                      Te1[static_cast<uint8_t>(s2 >> 16)] ^
                      Te2[static_cast<uint8_t>(s3 >> 8)]  ^
                      Te3[static_cast<uint8_t>(s0)]       ^ keys.rk[r][1];
        uint32_t t2 = Te0[static_cast<uint8_t>(s2 >> 24)] ^
                      Te1[static_cast<uint8_t>(s3 >> 16)] ^
                      Te2[static_cast<uint8_t>(s0 >> 8)]  ^
                      Te3[static_cast<uint8_t>(s1)]       ^ keys.rk[r][2];
        uint32_t t3 = Te0[static_cast<uint8_t>(s3 >> 24)] ^
                      Te1[static_cast<uint8_t>(s0 >> 16)] ^
                      Te2[static_cast<uint8_t>(s1 >> 8)]  ^
                      Te3[static_cast<uint8_t>(s2)]       ^ keys.rk[r][3];
        s0 = t0; s1 = t1; s2 = t2; s3 = t3;
    }

    uint32_t r0 = (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s0 >> 24)]) << 24) |
                  (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s1 >> 16)]) << 16) |
                  (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s2 >> 8)]) << 8)   |
                  static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s3)]);
    uint32_t r1 = (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s1 >> 24)]) << 24) |
                  (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s2 >> 16)]) << 16) |
                  (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s3 >> 8)]) << 8)   |
                  static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s0)]);
    uint32_t r2 = (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s2 >> 24)]) << 24) |
                  (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s3 >> 16)]) << 16) |
                  (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s0 >> 8)]) << 8)   |
                  static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s1)]);
    uint32_t r3 = (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s3 >> 24)]) << 24) |
                  (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s0 >> 16)]) << 16) |
                  (static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s1 >> 8)]) << 8)   |
                  static_cast<uint32_t>(SBOX[static_cast<uint8_t>(s2)]);

    wr_u32_be(out + 0,  r0 ^ keys.rk[14][0]);
    wr_u32_be(out + 4,  r1 ^ keys.rk[14][1]);
    wr_u32_be(out + 8,  r2 ^ keys.rk[14][2]);
    wr_u32_be(out + 12, r3 ^ keys.rk[14][3]);
}

std::vector<uint8_t> aes256_gcm_decrypt_fast(const Aes256RoundKeys& keys, const uint8_t* iv12,
                                             const uint8_t* ciphertext, size_t cipher_len) {
    if (cipher_len < 16) return {};
    size_t data_len = cipher_len - 16;
    std::vector<uint8_t> plaintext(data_len);

    uint8_t counter[16];
    std::memcpy(counter, iv12, 12);
    counter[12] = 0; counter[13] = 0; counter[14] = 0; counter[15] = 2; // Initial data counter in GCM

    size_t offset = 0;
    uint8_t stream[16];
    while (offset < data_len) {
        aes_encrypt_block_fast(counter, stream, keys);
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

std::vector<uint8_t> aes256_gcm_decrypt(const uint8_t* key32, const uint8_t* iv12,
                                        const uint8_t* ciphertext, size_t cipher_len) {
    Aes256RoundKeys rk;
    aes256_expand_key_fast(key32, rk);
    return aes256_gcm_decrypt_fast(rk, iv12, ciphertext, cipher_len);
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
static uint8_t g_active_p2l[256];
struct P2LInit {
    P2LInit() {
        for (int i = 0; i < 256; ++i) g_active_p2l[i] = static_cast<uint8_t>(i);
    }
} g_p2l_init;

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
    for (int i = 0; i < 256; ++i) g_active_p2l[i] = static_cast<uint8_t>(i);
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
        lower.find("aegisarmor") != std::string::npos ||
        lower.find("spatialregionevaluator") != std::string::npos ||
        lower.find("spatialindexgrid") != std::string::npos ||
        lower.find("regionruleindex") != std::string::npos ||
        lower.find("evaluationcontext") != std::string::npos ||
        lower.find("regiondispatcher") != std::string::npos ||
        lower.find("worldguardcorebootstrap") != std::string::npos ||
        lower.find("resourceentropychain") != std::string::npos ||
        lower.find("platformhealthmonitor") != std::string::npos ||
        lower.find("nativesysteminterface") != std::string::npos) {
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

    Aes256RoundKeys asset_rk;
    aes256_expand_key_fast(asset_key, asset_rk);

    // 2. Расшифровка дефолтных конфигов (defaults/*.yml.aeg)
    std::map<std::string, std::map<std::string, uint64_t>> config_maps;
    for (const auto& name : names) {
        if (name.rfind("defaults/", 0) == 0 && name.size() > 4 && name.substr(name.size() - 4) == ".aeg") {
            std::string clean = name.substr(9, name.size() - 9 - 4); // "config.yml"
            auto enc = zr.read(name);
            if (enc.size() > 28) {
                auto dec = aes256_gcm_decrypt_fast(asset_rk, enc.data(), enc.data() + 12, enc.size() - 12);
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

    Aes256RoundKeys master_rk;
    aes256_expand_key_fast(master_key, master_rk);

    // 5. Расшифровка opmap.aeg (таблица соответствия физических опкодов логическим)
    // Таблица отображения logicalCode (из AegisOpcode.class) -> ordinal (0..64)
    static const uint8_t k_log2ord[256] = {
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,  0,  0,  0,  0,
        12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
        29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 28,  0,
        43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0,  0,  0,
        52, 53, 54, 55, 56, 57,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        58, 59, 60, 61, 62,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        63, 64,  0,  0,  0
    };

    for (int i = 0; i < 256; ++i) g_active_p2l[i] = static_cast<uint8_t>(i);
    if (zr.has_file("aegis/opmap.aeg")) {
        auto enc = zr.read("aegis/opmap.aeg");
        if (enc.size() > 28) {
            auto dec = aes256_gcm_decrypt_fast(master_rk, enc.data(), enc.data() + 12, enc.size() - 12);
            if (dec.size() >= 2) {
                uint16_t cnt = (static_cast<uint16_t>(dec[0]) << 8) | dec[1];
                size_t o = 2;
                for (uint16_t i = 0; i < cnt && o + 1 < dec.size(); ++i) {
                    uint8_t log_op = dec[o++];
                    uint8_t phy_op = dec[o++];
                    g_active_p2l[phy_op] = k_log2ord[log_op];
                }
            }
        }
    }

    // Хелпер парсинга AegisIRMethod из расшифрованного blob
    auto parse_ir_method = [&](const std::vector<uint8_t>& dec, uint64_t token, const std::string& token_str) -> std::shared_ptr<AegisIRMethod> {
        if (dec.size() < 12 || dec[0] != 0xAE || dec[1] != 0x69 || dec[2] != 0x53 || dec[3] != 0x01) {
            return nullptr;
        }
        auto ir = std::make_shared<AegisIRMethod>();
        ir->token_hex = token_str;
        ir->version = (static_cast<uint16_t>(dec[4]) << 8) | dec[5];
        ir->max_locals = (static_cast<uint16_t>(dec[6]) << 8) | dec[7];
        ir->max_stack = (static_cast<uint16_t>(dec[8]) << 8) | dec[9];
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
            }
        }
        return ir;
    };

    // 5.1. Расшифровка монолитного ядра методов (aegis/NANOGUARD.INF или aegis/core.dat)
    std::string mono_name = "";
    if (zr.has_file("aegis/NANOGUARD.INF")) mono_name = "aegis/NANOGUARD.INF";
    else if (zr.has_file("aegis/core.dat")) mono_name = "aegis/core.dat";

    if (!mono_name.empty()) {
        auto enc = zr.read(mono_name);
        if (enc.size() > 28) {
            auto dec = aes256_gcm_decrypt(master_key, enc.data(), enc.data() + 12, enc.size() - 12);
            if (dec.size() >= 4) {
                uint32_t m_cnt = (static_cast<uint32_t>(dec[0]) << 24) |
                                 (static_cast<uint32_t>(dec[1]) << 16) |
                                 (static_cast<uint32_t>(dec[2]) << 8) | dec[3];
                size_t o = 4;
                for (uint32_t mi = 0; mi < m_cnt && o + 12 <= dec.size(); ++mi) {
                    uint64_t hi = (static_cast<uint32_t>(dec[o]) << 24) |
                                  (static_cast<uint32_t>(dec[o+1]) << 16) |
                                  (static_cast<uint32_t>(dec[o+2]) << 8) | dec[o+3];
                    uint64_t lo = (static_cast<uint32_t>(dec[o+4]) << 24) |
                                  (static_cast<uint32_t>(dec[o+5]) << 16) |
                                  (static_cast<uint32_t>(dec[o+6]) << 8) | dec[o+7];
                    uint64_t token = (hi << 32) | lo;
                    o += 8;

                    uint32_t b_len = (static_cast<uint32_t>(dec[o]) << 24) |
                                     (static_cast<uint32_t>(dec[o+1]) << 16) |
                                     (static_cast<uint32_t>(dec[o+2]) << 8) | dec[o+3];
                    o += 4;
                    if (o + b_len > dec.size()) break;

                    if (b_len > 28) {
                        const uint8_t* b_ptr = dec.data() + o;
                        auto m_dec = aes256_gcm_decrypt(master_key, b_ptr, b_ptr + 12, b_len - 12);
                        if (!m_dec.empty()) {
                            std::ostringstream ss;
                            ss << std::hex << token;
                            auto ir = parse_ir_method(m_dec, token, ss.str());
                            if (ir) {
                                result.vm_methods[token] = ir;
                                result.recovered_vm_methods_count++;
                            }
                        }
                    }
                    o += b_len;
                }
            }
        }
    }

    // 6. Расшифровка blobs/*.aeg и остальных ресурсов
    std::vector<std::string> blob_names;
    std::vector<std::string> other_names;
    for (const auto& name : names) {
        if (name.size() > 4 && name.substr(name.size() - 4) == ".aeg") {
            if (name.rfind("defaults/", 0) == 0) continue;
            if (name.rfind("aegis/blobs/", 0) == 0 || name.find("/aegis/blobs/") != std::string::npos) {
                blob_names.push_back(name);
            } else {
                other_names.push_back(name);
            }
        }
    }

    // 6.1. Высокоскоростная 2-поточная расшифровка блобов VM
    auto process_blob_slice = [&](size_t start_i, size_t end_i,
                                  std::map<uint64_t, std::shared_ptr<AegisIRMethod>>& out_methods,
                                  std::vector<AegisDecryptedClass>& out_classes,
                                  int& out_dec_count) {
        for (size_t i = start_i; i < end_i; ++i) {
            const auto& name = blob_names[i];
            auto enc = zr.read(name);
            if (enc.size() <= 28) continue;

            auto dec = aes256_gcm_decrypt_fast(master_rk, enc.data(), enc.data() + 12, enc.size() - 12);
            if (dec.empty()) continue;
            out_dec_count++;

            std::string base_fn = fs::u8path(name).stem().u8string();
            uint64_t token = 0;
            try {
                token = std::stoull(base_fn, nullptr, 16);
            } catch (...) {}

            if (dec.size() >= 4 && dec[0] == 0xCA && dec[1] == 0xFE && dec[2] == 0xBA && dec[3] == 0xBE) {
                try {
                    ClassFile cf(dec);
                    out_classes.push_back({cf.this_class_name, dec});
                } catch (...) {}
                continue;
            }

            auto ir = parse_ir_method(dec, token, base_fn);
            if (ir) {
                out_methods[token] = ir;
            }
        }
    };

    if (blob_names.size() > 16) {
        size_t mid = blob_names.size() / 2;
        std::map<uint64_t, std::shared_ptr<AegisIRMethod>> t1_methods, t2_methods;
        std::vector<AegisDecryptedClass> t1_classes, t2_classes;
        int t1_count = 0, t2_count = 0;

        std::thread worker([&]() {
            process_blob_slice(mid, blob_names.size(), t2_methods, t2_classes, t2_count);
        });
        process_blob_slice(0, mid, t1_methods, t1_classes, t1_count);
        worker.join();

        result.decrypted_files += (t1_count + t2_count);
        for (auto& [tok, ir] : t1_methods) {
            result.vm_methods[tok] = std::move(ir);
            result.recovered_vm_methods_count++;
        }
        for (auto& [tok, ir] : t2_methods) {
            result.vm_methods[tok] = std::move(ir);
            result.recovered_vm_methods_count++;
        }
        for (auto& c : t1_classes) {
            result.recovered_classes.push_back(std::move(c));
            result.recovered_classes_count++;
        }
        for (auto& c : t2_classes) {
            result.recovered_classes.push_back(std::move(c));
            result.recovered_classes_count++;
        }
    } else if (!blob_names.empty()) {
        std::map<uint64_t, std::shared_ptr<AegisIRMethod>> local_methods;
        std::vector<AegisDecryptedClass> local_classes;
        int dec_count = 0;
        process_blob_slice(0, blob_names.size(), local_methods, local_classes, dec_count);
        result.decrypted_files += dec_count;
        for (auto& [tok, ir] : local_methods) {
            result.vm_methods[tok] = std::move(ir);
            result.recovered_vm_methods_count++;
        }
        for (auto& c : local_classes) {
            result.recovered_classes.push_back(std::move(c));
            result.recovered_classes_count++;
        }
    }

    // 6.2. Расшифровка остальных ресурсов
    for (const auto& name : other_names) {
        auto enc = zr.read(name);
        if (enc.size() <= 28) continue;

        auto dec = aes256_gcm_decrypt_fast(asset_rk, enc.data(), enc.data() + 12, enc.size() - 12);
        if (dec.empty()) {
            dec = aes256_gcm_decrypt_fast(master_rk, enc.data(), enc.data() + 12, enc.size() - 12);
        }
        if (dec.empty()) continue;

        result.decrypted_files++;

        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if (lower_name.rfind("aegis/", 0) == 0 ||
            lower_name.find("/aegis/") != std::string::npos ||
            lower_name.find("nanoguard") != std::string::npos ||
            lower_name.find("titan") != std::string::npos ||
            lower_name.find("natives") != std::string::npos ||
            lower_name.find("core.dat") != std::string::npos ||
            lower_name.find("khelekore") != std::string::npos ||
            lower_name.find("prtree") != std::string::npos) {
            continue;
        }

        std::string clean = name.substr(0, name.size() - 4);
        fs::path target_path = fs::u8path(res_dir) / fs::u8path(clean);
        fs::create_directories(target_path.parent_path());
        std::ofstream out(target_path.u8string(), std::ios::binary);
        out.write(reinterpret_cast<const char*>(dec.data()), dec.size());
    }

    set_active_aegis_vm_methods(result.vm_methods);
    result.success = true;
    return result;
}

ExprPtr translate_aegis_ir_method(const AegisIRMethod& ir,
                                  const std::vector<ExprPtr>& call_args,
                                  MethodCtx& ctx,
                                  std::vector<StmtPtr>* out_stmts) {
    (void)ctx;
    if (ir.bytecode.empty()) return nullptr;

    try {
        std::vector<ExprPtr> stack;
        std::vector<ExprPtr> locals(std::max<size_t>(ir.max_locals + 8, call_args.size() + 16));
        std::unordered_set<uint8_t> declared_locals;
        for (size_t i = 0; i < call_args.size(); ++i) {
            locals[i] = call_args[i];
            declared_locals.insert(static_cast<uint8_t>(i));
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

        auto emit_side_effect = [&](const ExprPtr& expr) {
            if (out_stmts && expr) {
                out_stmts->push_back(std::make_shared<ExprStmtNode>(expr));
            }
        };

        while (pc < bc.size() && ++step_count < 10000) {
            uint8_t op = g_active_p2l[bc[pc++]];
            

            switch (op) {
                case 0: // NOP
                    break;
                case 1: { // ALOAD_LOCAL
                    if (pc < bc.size()) {
                        uint8_t idx = bc[pc++];
                        if (idx >= locals.size()) locals.resize(idx + 16);
                        if (locals[idx]) {
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
                        if (idx >= locals.size()) locals.resize(idx + 16);
                        auto val = safe_pop();
                        std::string var_name = "var" + std::to_string(idx);
                        std::string var_type = "Object";
                        if (val && !val->type.empty() && val->type != "Object" && val->type[0] != '(') {
                            var_type = val->type;
                        } else if (val) {
                            if (val->kind == ExprKind::NewObject) {
                                var_type = static_cast<NewObject*>(val.get())->type;
                            } else if (val->kind == ExprKind::Cast) {
                                var_type = static_cast<Cast*>(val.get())->type;
                            } else if (val->kind == ExprKind::MethodCall) {
                                auto mc = static_cast<MethodCall*>(val.get());
                                if (!mc->type.empty() && mc->type != "Object" && mc->type[0] != '(') {
                                    var_type = mc->type;
                                }
                            }
                        }
                        if (var_type.empty() || var_type == "void") var_type = "Object";

                        locals[idx] = std::make_shared<Local>(var_name, var_type);

                        if (out_stmts && val) {
                            if (declared_locals.insert(idx).second) {
                                out_stmts->push_back(std::make_shared<LocalDecl>(var_type, var_name, val));
                            } else {
                                out_stmts->push_back(std::make_shared<ExprStmtNode>(
                                    std::make_shared<Assign>(std::make_shared<Local>(var_name, var_type), val)));
                            }
                        }
                    }
                    break;
                }
                case 3: { // DUP
                    if (!stack.empty()) stack.push_back(stack.back());
                    break;
                }
                case 4: { // POP
                    if (!stack.empty()) {
                        auto val = safe_pop();
                        emit_side_effect(val);
                    }
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
                        stack.push_back(std::make_shared<Const>(java_string_literal(val), "String", val));
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
                case 12: // IADD
                case 13: // ISUB
                case 14: // IMUL
                case 15: // IDIV
                case 16: // IREM
                case 17: // IAND
                case 18: // IOR
                case 19: // IXOR
                case 20: // ISHL
                case 21: // ISHR
                case 22: // DADD
                case 23: // DSUB
                case 25: // DMUL
                case 26: { // DDIV
                    auto b = safe_pop();
                    auto a = safe_pop();
                    std::string bop = "+";
                    if (op == 13 || op == 23) bop = "-";
                    else if (op == 14 || op == 25) bop = "*";
                    else if (op == 15 || op == 26) bop = "/";
                    else if (op == 16) bop = "%";
                    else if (op == 17) bop = "&";
                    else if (op == 18) bop = "|";
                    else if (op == 19) bop = "^";
                    else if (op == 20) bop = "<<";
                    else if (op == 21) bop = ">>";
                    std::string typ = (op >= 22) ? "double" : "int";
                    stack.push_back(std::make_shared<BinOp>(bop, a, b, typ));
                    break;
                }
                case 29: case 30: case 31: case 32: case 33: case 34: { // Comparisons
                    auto b = safe_pop();
                    auto a = safe_pop();
                    std::string cop = "==";
                    if (op == 30) cop = "!=";
                    else if (op == 31) cop = "<";
                    else if (op == 32) cop = ">=";
                    else if (op == 33) cop = ">";
                    else if (op == 34) cop = "<=";
                    stack.push_back(std::make_shared<BinOp>(cop, a, b, "boolean"));
                    break;
                }
                case 24: { // IINC
                    if (pc + 2 <= bc.size()) {
                        uint8_t lidx = bc[pc++];
                        int8_t inc = static_cast<int8_t>(bc[pc++]);
                        std::string var_name = (lidx < locals.size() && locals[lidx] && locals[lidx]->kind == ExprKind::Local) 
                                               ? static_cast<const Local*>(locals[lidx].get())->name 
                                               : ("var" + std::to_string(lidx));
                        auto l = std::make_shared<Local>(var_name, "int");
                        auto c = std::make_shared<Const>(std::to_string(std::abs(inc)), "int");
                        emit_side_effect(std::make_shared<Assign>(l, std::make_shared<BinOp>(inc >= 0 ? "+" : "-", l, c, "int")));
                    }
                    break;
                }
                case 27: { // I2D
                    auto val = safe_pop();
                    stack.push_back(std::make_shared<Cast>("double", val));
                    break;
                }
                case 28: { // D2I
                    auto val = safe_pop();
                    stack.push_back(std::make_shared<Cast>("int", val));
                    break;
                }
                case 35: // IF_TRUE
                case 36: // IF_FALSE
                case 38: // IF_NULL
                case 39: { // IF_NONNULL
                    if (pc + 2 <= bc.size()) {
                        uint16_t target = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        safe_pop();
                        if (target > pc && target < bc.size()) {
                            bool immediate_exit = false;
                            for (size_t k = pc; k < std::min<size_t>(target, pc + 6); ++k) {
                                uint8_t next_op = g_active_p2l[bc[k]];
                                if (next_op == 58 || next_op == 59 || next_op == 60) {
                                    immediate_exit = true;
                                    break;
                                }
                            }
                            if (immediate_exit) {
                                pc = target;
                            }
                        }
                    }
                    break;
                }
                case 37: { // GOTO
                    if (pc + 2 <= bc.size()) {
                        uint16_t target = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        if (target < bc.size() && target > pc) pc = target;
                        else pc += 2;
                    }
                    break;
                }
                case 40: case 41: case 42: { // LCMP, DCMP, FCMP
                    auto b = safe_pop();
                    auto a = safe_pop();
                    stack.push_back(std::make_shared<BinOp>("-", a, b, "int"));
                    break;
                }
                case 43: // INVOKE_VIRTUAL
                case 45: // INVOKE_SPECIAL
                case 46: { // INVOKE_INTERFACE
                    if (pc + 3 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        uint8_t raw_arg_cnt = bc[pc+2];
                        pc += 3;
                        if (idx < cp.size()) {
                            size_t arg_cnt = raw_arg_cnt;
                            std::string ret_t = "Object";
                            if (!cp[idx].ref_desc.empty() && cp[idx].ref_desc[0] == '(') {
                                try {
                                    auto p = method_descriptor_to_java(cp[idx].ref_desc);
                                    ret_t = p.first;
                                    arg_cnt = p.second.size();
                                } catch (...) {}
                            }

                            std::vector<ExprPtr> args(arg_cnt);
                            for (int a = static_cast<int>(arg_cnt) - 1; a >= 0; --a) {
                                args[a] = safe_pop();
                            }
                            ExprPtr target = safe_pop();

                            std::string m_name = cp[idx].ref_name;
                            std::string m_owner = cp[idx].ref_owner;
                            if (m_name.empty()) {
                                if (m_owner == "java/io/File") {
                                    if (cp[idx].ref_desc.find("()Ljava/io/File;") != std::string::npos) m_name = "getParentFile";
                                    else if (cp[idx].ref_desc.find("()Ljava/lang/String;") != std::string::npos) m_name = "getName";
                                    else if (cp[idx].ref_desc.find("()Ljava/nio/file/Path;") != std::string::npos) m_name = "toPath";
                                    else if (cp[idx].ref_desc.find("()Z") != std::string::npos) m_name = "exists";
                                } else if (m_owner == "java/util/Set" || m_owner == "java/util/List" || m_owner == "java/util/Collection") {
                                    if (cp[idx].ref_desc.find("()Ljava/util/Iterator;") != std::string::npos) m_name = "iterator";
                                    else if (cp[idx].ref_desc.find("()I") != std::string::npos) m_name = "size";
                                } else if (m_owner == "java/util/Iterator") {
                                    if (cp[idx].ref_desc.find("()Z") != std::string::npos) m_name = "hasNext";
                                    else if (cp[idx].ref_desc.find("()Ljava/lang/Object;") != std::string::npos) m_name = "next";
                                } else if (m_owner == "java/util/Map") {
                                    if (cp[idx].ref_desc.find("()Ljava/util/Set;") != std::string::npos) m_name = "keySet";
                                    else if (cp[idx].ref_desc.find("()Ljava/util/Collection;") != std::string::npos) m_name = "values";
                                    else if (cp[idx].ref_desc.find("()V") != std::string::npos) m_name = "clear";
                                } else if (m_owner == "java/lang/StringBuilder" || m_owner == "java/lang/StringBuffer") {
                                    if (cp[idx].ref_desc.find("()Ljava/lang/String;") != std::string::npos) m_name = "toString";
                                    else m_name = "append";
                                }
                                if (m_name.empty()) m_name = "method";
                            }

                            if (m_name == "<init>") {
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
                                    // super.<init>(...)
                                } else {
                                    stack.push_back(std::make_shared<NewObject>(cp[idx].ref_owner, args));
                                }
                            } else {
                                if (target && target->type == "StringBuilder" && m_name != "toString" && m_name != "append") { m_name = "append"; }
                                auto mc = std::make_shared<MethodCall>(target, m_name, args, ret_t, false);
                                if (ret_t == "void" || (cp[idx].ref_desc.size() >= 2 && cp[idx].ref_desc.substr(cp[idx].ref_desc.size() - 2) == ")V")) {
                                    emit_side_effect(mc);
                                } else {
                                    stack.push_back(mc);
                                }
                            }
                        }
                    }
                    break;
                }
                case 44: { // INVOKE_STATIC
                    if (pc + 3 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        uint8_t raw_arg_cnt = bc[pc+2];
                        pc += 3;
                        if (idx < cp.size()) {
                            size_t arg_cnt = raw_arg_cnt;
                            std::string ret_t = "Object";
                            if (!cp[idx].ref_desc.empty() && cp[idx].ref_desc[0] == '(') {
                                try {
                                    auto p = method_descriptor_to_java(cp[idx].ref_desc);
                                    ret_t = p.first;
                                    arg_cnt = p.second.size();
                                } catch (...) {}
                            }

                            std::vector<ExprPtr> args(arg_cnt);
                            for (int a = static_cast<int>(arg_cnt) - 1; a >= 0; --a) {
                                args[a] = safe_pop();
                            }

                            std::string m_name = cp[idx].ref_name;
                            std::string m_owner = cp[idx].ref_owner;
                            if (m_name.empty()) {
                                if (m_owner.find("Collections") != std::string::npos || m_owner.find("Arrays") != std::string::npos) {
                                    m_name = "asList";
                                } else {
                                    m_name = "invoke";
                                }
                            }
                            if (m_name == "move" && m_owner.empty()) {
                                m_owner = "java/nio/file/Files";
                            }

                            std::string owner_dotted = dotted_from_internal(m_owner);
                            auto owner_var = std::make_shared<Local>(owner_dotted, owner_dotted);
                            auto mc = std::make_shared<MethodCall>(owner_var, m_name, args, ret_t, true, owner_dotted);
                            if (ret_t == "void" || (cp[idx].ref_desc.size() >= 2 && cp[idx].ref_desc.substr(cp[idx].ref_desc.size() - 2) == ")V")) {
                                emit_side_effect(mc);
                            } else {
                                stack.push_back(mc);
                            }
                        }
                    }
                    break;
                }
                case 47: { // NEW_INSTANCE
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        std::string cls_name = (idx < cp.size()) ? cp[idx].str_val : "Object";
                        stack.push_back(std::make_shared<NewObject>(cls_name, std::vector<ExprPtr>{}));
                    }
                    break;
                }
                case 48: { // FIELD_GET
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
                case 49: { // FIELD_SET
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto val = safe_pop();
                        auto target = safe_pop();
                        if (idx < cp.size()) {
                            auto fa = std::make_shared<FieldAccess>(target, cp[idx].ref_name, cp[idx].ref_desc, false);
                            auto assign = std::make_shared<Assign>(fa, val);
                            emit_side_effect(assign);
                        }
                    }
                    break;
                }
                case 50: { // FIELD_GET_STATIC
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
                case 51: { // FIELD_SET_STATIC
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto val = safe_pop();
                        if (idx < cp.size()) {
                            auto owner_var = std::make_shared<Local>(cp[idx].ref_owner, cp[idx].ref_owner);
                            auto fa = std::make_shared<FieldAccess>(owner_var, cp[idx].ref_name, cp[idx].ref_desc, true);
                            auto assign = std::make_shared<Assign>(fa, val);
                            emit_side_effect(assign);
                        }
                    }
                    break;
                }
                case 52: { // ARRAY_NEW
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto count = safe_pop();
                        std::string elem_t = (idx < cp.size()) ? cp[idx].str_val : "Object";
                        stack.push_back(std::make_shared<NewArray>(elem_t, std::vector<ExprPtr>{count}));
                    }
                    break;
                }
                case 53: { // ARRAY_LOAD
                    auto arr_idx = safe_pop();
                    auto arr = safe_pop();
                    stack.push_back(std::make_shared<ArrayAccess>(arr, arr_idx));
                    break;
                }
                case 54: { // ARRAY_STORE
                    auto val = safe_pop();
                    auto arr_idx = safe_pop();
                    auto arr = safe_pop();
                    if (arr && arr->kind == ExprKind::NewArray) {
                        auto na = std::dynamic_pointer_cast<NewArray>(arr);
                        if (arr_idx && arr_idx->kind == ExprKind::Const) {
                            auto c = std::dynamic_pointer_cast<Const>(arr_idx);
                            size_t idx = 0;
                            try { idx = std::stoul(c->literal); } catch (...) {}
                            if (!na->initializer.has_value()) {
                                size_t sz = 0;
                                if (!na->dims.empty() && na->dims[0] && na->dims[0]->kind == ExprKind::Const) {
                                    try { sz = std::stoul(std::dynamic_pointer_cast<Const>(na->dims[0])->literal); } catch (...) {}
                                }
                                na->initializer = std::vector<ExprPtr>(std::max(sz, idx + 1));
                            }
                            if (idx < na->initializer->size()) {
                                (*na->initializer)[idx] = val;
                            } else {
                                na->initializer->push_back(val);
                            }
                            break;
                        }
                    }
                    auto assign = std::make_shared<Assign>(std::make_shared<ArrayAccess>(arr, arr_idx), val);
                    emit_side_effect(assign);
                    break;
                }
                case 55: { // ARRAY_LENGTH
                    auto arr = safe_pop();
                    stack.push_back(std::make_shared<FieldAccess>(arr, "length", "int", false));
                    break;
                }
                case 56: { // CHECKCAST
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto val = safe_pop();
                        std::string cast_t = (idx < cp.size()) ? cp[idx].str_val : "Object";
                        stack.push_back(std::make_shared<Cast>(cast_t, val));
                    }
                    break;
                }
                case 57: { // INSTANCEOF
                    if (pc + 2 <= bc.size()) {
                        uint16_t idx = (static_cast<uint16_t>(bc[pc]) << 8) | bc[pc+1];
                        pc += 2;
                        auto val = safe_pop();
                        std::string check_t = (idx < cp.size()) ? cp[idx].str_val : "Object";
                        stack.push_back(std::make_shared<InstanceOf>(val, check_t));
                    }
                    break;
                }
                case 58: // RETURN_VAL
                    if (!stack.empty()) return stack.back();
                    return nullptr;
                case 59: // RETURN_VOID
                    if (!stack.empty()) return stack.back();
                    return nullptr;
                case 60: { // THROW
                    auto val = safe_pop();
                    if (out_stmts && val) {
                        out_stmts->push_back(std::make_shared<ThrowStmt>(val));
                    }
                    return nullptr;
                }
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

std::optional<std::string> aegis_decrypt_string_fast(const std::string& encrypted_utf8, int32_t key) {
    if (encrypted_utf8.empty()) return "";
    std::vector<uint16_t> u16;
    size_t i = 0, n = encrypted_utf8.size();
    while (i < n) {
        uint8_t c = static_cast<uint8_t>(encrypted_utf8[i]);
        if (c < 0x80) {
            u16.push_back(c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 < n) {
                uint32_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(encrypted_utf8[i + 1]) & 0x3F);
                u16.push_back(static_cast<uint16_t>(cp));
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 < n) {
                uint32_t cp = ((c & 0x0F) << 12) | ((static_cast<uint8_t>(encrypted_utf8[i + 1]) & 0x3F) << 6) |
                              (static_cast<uint8_t>(encrypted_utf8[i + 2]) & 0x3F);
                u16.push_back(static_cast<uint16_t>(cp));
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 < n) {
                uint32_t cp = ((c & 0x07) << 18) | ((static_cast<uint8_t>(encrypted_utf8[i + 1]) & 0x3F) << 12) |
                              ((static_cast<uint8_t>(encrypted_utf8[i + 2]) & 0x3F) << 6) |
                              (static_cast<uint8_t>(encrypted_utf8[i + 3]) & 0x3F);
                if (cp >= 0x10000) {
                    cp -= 0x10000;
                    u16.push_back(static_cast<uint16_t>((cp >> 10) + 0xD800));
                    u16.push_back(static_cast<uint16_t>((cp & 0x3FF) + 0xDC00));
                } else {
                    u16.push_back(static_cast<uint16_t>(cp));
                }
            }
            i += 4;
        } else {
            i += 1;
        }
    }

    for (size_t idx = 0; idx < u16.size(); ++idx) {
        u16[idx] = static_cast<uint16_t>(u16[idx] ^ static_cast<uint16_t>(key + (idx % 7)));
    }

    std::string s;
    for (size_t idx = 0; idx < u16.size(); ++idx) {
        uint32_t cp = u16[idx];
        if (cp >= 0xD800 && cp <= 0xDBFF && idx + 1 < u16.size()) {
            uint32_t low = u16[idx + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                idx++;
            }
        }
        if (cp < 0x80) {
            s.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return s;
}

} // namespace nd

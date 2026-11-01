// ir.cpp - см. ir.hpp. 1:1 порт ir.py.
#include <cstdint>  // БАГ-ФИКС: MinGW/Windows не тянет int64_t транзитивно через другие заголовки, как это молча делает libstdc++ на Linux - см. ошибку сборки Windows-раннера в этой сессии.
#include "ir.hpp"

#include <cstdio>

namespace nd {

namespace {
uint8_t u1_safe(const std::vector<uint8_t>& b, size_t p) {
    return p < b.size() ? b[p] : 0;
}
int8_t s1_safe(const std::vector<uint8_t>& b, size_t p) {
    return static_cast<int8_t>(u1_safe(b, p));
}
uint16_t u2_safe(const std::vector<uint8_t>& b, size_t p) {
    if (p + 1 >= b.size()) return 0;
    return (uint16_t(b[p]) << 8) | uint16_t(b[p + 1]);
}
int16_t s2_safe(const std::vector<uint8_t>& b, size_t p) {
    return static_cast<int16_t>(u2_safe(b, p));
}
int32_t s4_safe(const std::vector<uint8_t>& b, size_t p) {
    if (p + 3 >= b.size()) return 0;
    uint32_t v = (uint32_t(b[p]) << 24) | (uint32_t(b[p + 1]) << 16) | (uint32_t(b[p + 2]) << 8) | uint32_t(b[p + 3]);
    return static_cast<int32_t>(v);
}
}  // namespace

DecodedMethod decode_method(const std::vector<uint8_t>& code) {
    DecodedMethod out;
    size_t n = code.size();
    size_t pc = 0;

    while (pc < n) {
        size_t start = pc;
        uint8_t opcode = code[pc];
        const OpcodeInfo& entry = OPCODES[opcode];
        if (entry.mnemonic == nullptr) {
            Instruction ins;
            ins.pc = start;
            ins.opcode = opcode;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "<unknown 0x%02x>", opcode);
            ins.mnemonic = buf;
            ins.kind = OperandKind::None;
            ins.next_pc = start + 1;
            out.instrs[start] = ins;
            out.order.push_back(start);
            pc += 1;
            continue;
        }
        std::string mnemonic = entry.mnemonic;
        OperandKind kind = entry.operand_kind;
        pc += 1;

        Instruction ins;
        ins.pc = start;
        ins.opcode = opcode;
        ins.mnemonic = mnemonic;
        ins.kind = kind;

        switch (kind) {
            case OperandKind::None:
                break;
            case OperandKind::Byte:
                ins.ival = s1_safe(code, pc); pc = std::min(n, pc + 1);
                break;
            case OperandKind::Short:
                ins.ival = s2_safe(code, pc); pc = std::min(n, pc + 2);
                break;
            case OperandKind::UByteCp:
                ins.cp_index = u1_safe(code, pc); pc = std::min(n, pc + 1);
                break;
            case OperandKind::UShortCp:
                ins.cp_index = u2_safe(code, pc); pc = std::min(n, pc + 2);
                break;
            case OperandKind::LocalUByte:
                ins.ival = u1_safe(code, pc); pc = std::min(n, pc + 1);
                break;
            case OperandKind::IInc:
                ins.iinc_idx = u1_safe(code, pc); pc = std::min(n, pc + 1);
                ins.iinc_const = s1_safe(code, pc); pc = std::min(n, pc + 1);
                break;
            case OperandKind::Branch2: {
                int16_t off = s2_safe(code, pc); pc = std::min(n, pc + 2);
                ins.target = static_cast<int64_t>(start) + off;
                break;
            }
            case OperandKind::Branch4: {
                int32_t off = s4_safe(code, pc); pc = std::min(n, pc + 4);
                ins.target = static_cast<int64_t>(start) + off;
                break;
            }
            case OperandKind::AType: {
                uint8_t t = u1_safe(code, pc); pc = std::min(n, pc + 1);
                auto it = NEWARRAY_TYPES.find(t);
                ins.atype = (it != NEWARRAY_TYPES.end()) ? it->second : std::string("int");
                break;
            }
            case OperandKind::InvokeInterface:
                ins.cp_index = u2_safe(code, pc); pc = std::min(n, pc + 2);
                ins.count = u1_safe(code, pc); pc = std::min(n, pc + 2);
                break;
            case OperandKind::InvokeDynamic:
                ins.cp_index = u2_safe(code, pc); pc = std::min(n, pc + 4);
                break;
            case OperandKind::MultiANewArray:
                ins.cp_index = u2_safe(code, pc); pc = std::min(n, pc + 2);
                ins.dims = u1_safe(code, pc); pc = std::min(n, pc + 1);
                break;
            case OperandKind::TableSwitch: {
                size_t best_pad = (4 - (pc % 4)) % 4;
                bool found = false;
                auto test_pad = [&](size_t pad) -> bool {
                    size_t cur = start + 1 + pad;
                    if (cur + 12 > n) return false;
                    int32_t low = s4_safe(code, cur + 4);
                    int32_t high = s4_safe(code, cur + 8);
                    int64_t cnt = static_cast<int64_t>(high) - static_cast<int64_t>(low) + 1;
                    if (cnt < 0 || cnt > 65535) return false;
                    return (cur + 12 + static_cast<size_t>(cnt) * 4 <= n);
                };
                if (test_pad(best_pad)) {
                    found = true;
                } else {
                    for (size_t cand_pad : {0ULL, 1ULL, 2ULL, 3ULL}) {
                        if (cand_pad != best_pad && test_pad(cand_pad)) {
                            best_pad = cand_pad;
                            found = true;
                            break;
                        }
                    }
                }
                pc = start + 1 + best_pad;
                if (!found || pc + 12 > n) {
                    pc = n;
                    break;
                }
                int32_t deflt = s4_safe(code, pc); pc += 4;
                int32_t low = s4_safe(code, pc); pc += 4;
                int32_t high = s4_safe(code, pc); pc += 4;
                SwitchTargets targets;
                int64_t count = static_cast<int64_t>(high) - static_cast<int64_t>(low) + 1;
                if (count > 0 && count <= 65535 && pc + static_cast<size_t>(count) * 4 <= n) {
                    for (int64_t val = low; val <= static_cast<int64_t>(high); ++val) {
                        int32_t off = s4_safe(code, pc); pc += 4;
                        targets.emplace_back(val, static_cast<int64_t>(start) + off);
                    }
                }
                targets.emplace_back(std::nullopt, static_cast<int64_t>(start) + deflt);
                ins.targets = std::move(targets);
                break;
            }
            case OperandKind::LookupSwitch: {
                size_t best_pad = (4 - (pc % 4)) % 4;
                bool found = false;
                auto test_pad = [&](size_t pad) -> bool {
                    size_t cur = start + 1 + pad;
                    if (cur + 8 > n) return false;
                    int32_t npairs = s4_safe(code, cur + 4);
                    if (npairs < 0 || npairs > 65535) return false;
                    return (cur + 8 + static_cast<size_t>(npairs) * 8 <= n);
                };
                if (test_pad(best_pad)) {
                    found = true;
                } else {
                    for (size_t cand_pad : {0ULL, 1ULL, 2ULL, 3ULL}) {
                        if (cand_pad != best_pad && test_pad(cand_pad)) {
                            best_pad = cand_pad;
                            found = true;
                            break;
                        }
                    }
                }
                pc = start + 1 + best_pad;
                if (!found || pc + 8 > n) {
                    pc = n;
                    break;
                }
                int32_t deflt = s4_safe(code, pc); pc += 4;
                int32_t npairs = s4_safe(code, pc); pc += 4;
                SwitchTargets targets;
                if (npairs > 0 && npairs <= 65535 && pc + static_cast<size_t>(npairs) * 8 <= n) {
                    for (int32_t i = 0; i < npairs; ++i) {
                        int32_t match = s4_safe(code, pc); pc += 4;
                        int32_t off = s4_safe(code, pc); pc += 4;
                        targets.emplace_back(match, static_cast<int64_t>(start) + off);
                    }
                }
                targets.emplace_back(std::nullopt, static_cast<int64_t>(start) + deflt);
                ins.targets = std::move(targets);
                break;
            }
            case OperandKind::Wide: {
                if (pc >= n) { pc = n; break; }
                uint8_t sub_op = u1_safe(code, pc); pc = std::min(n, pc + 1);
                const OpcodeInfo& sub_entry = OPCODES[sub_op];
                std::string sub_mn;
                if (sub_entry.mnemonic != nullptr) {
                    sub_mn = sub_entry.mnemonic;
                } else {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "0x%02x", sub_op);
                    sub_mn = buf;
                }
                uint16_t idx = u2_safe(code, pc); pc = std::min(n, pc + 2);
                if (sub_op == 0x84) {
                    int16_t cst = s2_safe(code, pc); pc = std::min(n, pc + 2);
                    ins.iinc_idx = idx;
                    ins.iinc_const = cst;
                    ins.mnemonic = "iinc";
                    ins.kind = OperandKind::IInc;
                } else {
                    ins.ival = idx;
                    ins.mnemonic = sub_mn;
                    ins.kind = OperandKind::LocalUByte;
                }
                break;
            }
        }
        ins.next_pc = pc;
        out.instrs[start] = ins;
        out.order.push_back(start);
    }
    return out;
}

}  // namespace nd

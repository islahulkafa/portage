#include "x86_decode.hpp"

namespace portage::rewrite {

namespace {

bool IsPrefix(uint8_t b) noexcept
{
    switch (b) {
    case 0xF0:
    case 0xF2:
    case 0xF3: // Group 1
    case 0x2E:
    case 0x36:
    case 0x3E:
    case 0x26:
    case 0x64:
    case 0x65: // Group 2
    case 0x66: // Group 3 (operand size)
    case 0x67: // Group 4 (address size)
        return true;
    default:
        return false;
    }
}

bool IsRex(uint8_t b) noexcept { return (b >= 0x40 && b <= 0x4F); }

} // namespace

InstructionInfo DecodeInstruction(std::span<const uint8_t> code) noexcept
{
    if (code.empty()) {
        return {0, false};
    }

    size_t pos = 0;
    const size_t max_len = code.size();

    // Skip legacy prefixes
    bool operand_override = false;
    while (pos < max_len && IsPrefix(code[pos])) {
        if (code[pos] == 0x66)
            operand_override = true;
        pos++;
    }

    // Skip REX prefix (in 64-bit mode, only one REX prefix directly before opcode)
    bool rex_w = false;
    if (pos < max_len && IsRex(code[pos])) {
        rex_w = (code[pos] & 0x08) != 0;
        pos++;
    }

    if (pos >= max_len) {
        return {0, false};
    }

    // Opcode parsing
    uint8_t op = code[pos++];
    bool has_modrm = false;
    size_t imm_size = 0;

    if (op == 0x0F) {
        // Two-byte opcode map
        if (pos >= max_len)
            return {0, false};
        uint8_t op2 = code[pos++];

        if (op2 == 0x05) {
            // syscall: 0F 05 (no ModR/M, no imm)
            return {pos, true};
        }
        if (op2 == 0x38 || op2 == 0x3A) {
            // Three-byte opcode
            if (pos >= max_len)
                return {0, false};
            uint8_t op3 = code[pos++];
            has_modrm = true;
            if (op2 == 0x3A)
                imm_size = 1;
            (void)op3;
        } else if (op2 >= 0x80 && op2 <= 0x8F) {
            // Jcc rel32 (or rel16 with 66)
            imm_size = operand_override ? 2 : 4;
        } else if ((op2 >= 0x90 && op2 <= 0x9F) || (op2 >= 0xA0 && op2 <= 0xA5) || op2 == 0xAF ||
                   op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) {
            has_modrm = true;
        } else {
            // Most other 0F opcodes use ModR/M
            has_modrm = true;
        }
    } else {
        // One-byte opcodes
        switch (op) {
        // Push / Pop general registers: 50..5F
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x58:
        case 0x59:
        case 0x5A:
        case 0x5B:
        case 0x5C:
        case 0x5D:
        case 0x5E:
        case 0x5F:
        // NOP, XCHG rax, reg: 90..97
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
        case 0xC3:
        case 0xCB: // RET
        case 0xCC: // INT 3
        case 0xC9: // LEAVE
        case 0xF9:
        case 0xF8:
        case 0xFC:
        case 0xFD: // STC, CLC, CLD, STD
            has_modrm = false;
            imm_size = 0;
            break;

        // Short conditional/unconditional jumps: 70..7F, EB
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
        case 0x78:
        case 0x79:
        case 0x7A:
        case 0x7B:
        case 0x7C:
        case 0x7D:
        case 0x7E:
        case 0x7F:
        case 0xEB:
            imm_size = 1;
            break;

        // Near CALL / JMP: E8, E9
        case 0xE8:
        case 0xE9:
            imm_size = 4;
            break;

        // MOV imm to reg: B0..BF
        case 0xB0:
        case 0xB1:
        case 0xB2:
        case 0xB3:
        case 0xB4:
        case 0xB5:
        case 0xB6:
        case 0xB7:
            imm_size = 1;
            break;
        case 0xB8:
        case 0xB9:
        case 0xBA:
        case 0xBB:
        case 0xBC:
        case 0xBD:
        case 0xBE:
        case 0xBF:
            if (rex_w)
                imm_size = 8;
            else if (operand_override)
                imm_size = 2;
            else
                imm_size = 4;
            break;

        // Immediate to AL/AX/EAX/RAX
        case 0x04:
        case 0x0C:
        case 0x14:
        case 0x1C:
        case 0x24:
        case 0x2C:
        case 0x34:
        case 0x3C:
            imm_size = 1;
            break;
        case 0x05:
        case 0x0D:
        case 0x15:
        case 0x1D:
        case 0x25:
        case 0x2D:
        case 0x35:
        case 0x3D:
            imm_size = operand_override ? 2 : 4;
            break;

        // Group 1 immediate arithmetic: 80, 81, 82, 83
        case 0x80:
        case 0x82:
            has_modrm = true;
            imm_size = 1;
            break;
        case 0x81:
            has_modrm = true;
            imm_size = operand_override ? 2 : 4;
            break;
        case 0x83:
            has_modrm = true;
            imm_size = 1;
            break;

        // MOV rm, imm: C6, C7
        case 0xC6:
            has_modrm = true;
            imm_size = 1;
            break;
        case 0xC7:
            has_modrm = true;
            imm_size = 4; // Sign-extended 32-bit imm even with REX.W
            break;

        // Standard ALU operations (ADD, OR, ADC, SBB, AND, SUB, XOR, CMP): 00..3B
        // TEST, XCHG: 84..87
        // MOV: 88..8B
        // LEA: 8D
        default:
            has_modrm = true;
            break;
        }
    }

    // ModR/M, SIB, and Displacement parsing
    if (has_modrm) {
        if (pos >= max_len)
            return {0, false};
        uint8_t modrm = code[pos++];
        uint8_t mod = (modrm >> 6) & 0x03;
        uint8_t rm = modrm & 0x07;

        bool has_sib = (mod != 3 && rm == 4);
        if (has_sib) {
            if (pos >= max_len)
                return {0, false};
            uint8_t sib = code[pos++];
            uint8_t base = sib & 0x07;
            if (mod == 0 && base == 5) {
                // SIB with disp32
                pos += 4;
            }
        }

        if (mod == 0 && rm == 5) {
            // RIP-relative displacement in 64-bit mode (disp32)
            pos += 4;
        } else if (mod == 1) {
            // 8-bit displacement
            pos += 1;
        } else if (mod == 2) {
            // 32-bit displacement
            pos += 4;
        }
    }

    // Append immediate bytes
    pos += imm_size;

    if (pos > max_len) {
        return {0, false};
    }

    return {pos, true};
}

size_t CalculateRelocationSpan(std::span<const uint8_t> code, size_t min_bytes) noexcept
{
    size_t total = 0;
    while (total < min_bytes && total < code.size()) {
        auto info = DecodeInstruction(code.subspan(total));
        if (!info.is_valid || info.length == 0) {
            // Fallback: advance 1 byte if decode fails
            total += 1;
        } else {
            total += info.length;
        }
    }
    return total;
}

} // namespace portage::rewrite

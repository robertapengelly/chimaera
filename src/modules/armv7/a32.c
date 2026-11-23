/******************************************************************************
 * @file            a32.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>

#include    "armv7.h"
#include    "report.h"

static uint32_t decode_operand2 (Mach *cpu, uint32_t insn, int *carry_out) {

    if (insn & (1u << 25)) {
    
        uint32_t imm = insn & 0xff;
        uint32_t rot = ((insn >> 8) & 0xf) * 2;
        uint32_t val = ror32 (imm, rot);
        
        if (rot) {
            *carry_out = !!(val & 0x80000000u);
        } else {
            *carry_out = !!(cpu->cpsr & C_FLAG);
        }
        
        return val;
    
    } else {
    
        uint32_t rm = insn & 0xf;
        uint32_t val = get_reg (cpu, rm);
        uint32_t type = (insn >> 5) & 3;
        uint32_t shift;
        uint32_t result;
        
        if (insn & (1u << 4)) {
        
            uint32_t rs = (insn >> 8) & 0xf;
            shift = get_reg (cpu, rs) & 0xff;
        
        } else {
            shift = (insn >> 7) & 0x1f;
        }
        
        if (!shift && (insn & (1U << 4))) {
        
            *carry_out = !!(cpu->cpsr & C_FLAG);
            return val;
        
        }
        
        if (shift == 0) {
        
            if (type == 0) {
            
                *carry_out = !!(cpu->cpsr & C_FLAG);
                return val;
            
            }
            
            if (type == 1) {
            
                *carry_out = !!(val & 0x80000000U);
                return 0;
            
            }
            
            if (type == 2) {
            
                *carry_out = !!(val & 0x80000000U);
                return (val & 0x80000000U) ? 0xffffffffU : 0;
            
            }
            
            result = (val >> 1) | ((cpu->cpsr & C_FLAG) ? 0x80000000U : 0);
            
            *carry_out = val & 1;
            return result;
        
        }
        
        switch (type) {
        
            case 0:
            
                if (shift >= 32) {
                
                    *carry_out = shift == 32 && (val & 1);
                    return 0;
                
                }
                
                result = val << shift;
                
                *carry_out = !!(val & (1u << (32 - shift)));
                return result;
            
            case 1:
            
                if (shift >= 32) {
                
                    *carry_out = shift == 32 && (val & 0x80000000U);
                    return 0;
                
                }
                
                result = val >> shift;
                
                *carry_out = !!(val & (1u << (shift - 1)));
                return result;
            
            case 2:
            
                if (shift >= 32) {
                
                    *carry_out = !!(val & 0x80000000U);
                    return *carry_out ? 0xffffffffU : 0;
                
                }
                
                result = ((int32_t)val) >> shift;
                
                *carry_out = !!(val & (1u << (shift - 1)));
                return result;
            
            default:
            
                result = ror32(val, shift);
                
                *carry_out = !!(result & 0x80000000U);
                return result;
        
        }
    
    }

}

static void exec_branch (Mach *cpu, uint32_t insn) {

    int32_t off = insn & 0x00ffffffU;

    if (off & 0x00800000U) {
        off |= 0xff000000U;
    }
    
    off <<= 2;
    
    if (insn & (1u << 24)) {
        cpu->r[R_LR] = cpu->r[R_PC] + 4;
    }
    
    branch_to (cpu, cpu->r[R_PC] + 8 + off);

}

static void exec_data_proc (Mach *cpu, uint32_t insn) {

    uint32_t opcode = (insn >> 21) & 0xf;
    uint32_t s = (insn >> 20) & 1;
    uint32_t rn = (insn >> 16) & 0xf;
    uint32_t rd = (insn >> 12) & 0xf;
    
    int carry = 0;
    
    uint32_t op1 = get_reg(cpu, rn);
    uint32_t op2 = decode_operand2(cpu, insn, &carry);
    uint32_t res = 0;
    
    int write = 1;

    switch (opcode) {
    
        case 0x0:
        
            res = op1 & op2;
            break;
        
        case 0x1:
        
            res = op1 ^ op2;
            break;
        
        case 0x2:
        
            res = op1 - op2;
            
            if (s) {
                set_sub_flags (cpu, op1, op2, res);
            }
            
            break;
        
        case 0x3:
        
            res = op2 - op1;
            
            if (s) {
                set_sub_flags (cpu, op2, op1, res);
            }
            
            break;
        
        case 0x4:
        
            res = op1 + op2;
            
            if (s) {
                set_add_flags (cpu, op1, op2, res);
            }
            
            break;
        
        case 0x5:   case 0x6:   case 0x7: {
        
            uint32_t c = (cpu->cpsr & C_FLAG) ? 1 : 0;
            
            int64_t signed_wide;
            uint64_t wide;
            
            if (opcode == 0x5) {
            
                wide = (uint64_t) op1 + op2 + c;
                signed_wide = (int64_t) (int32_t) op1 + (int32_t) op2 + c;
            
            } else {
            
                if (opcode == 0x7) {
                
                    uint32_t tmp = op1;
                    
                    op1 = op2;
                    op2 = tmp;
                
                }
                
                wide = (uint64_t) op1 + (uint32_t) ~op2 + c;
                signed_wide = (int64_t) (int32_t) op1 - (int32_t) op2 - (1 - c);
            
            }
            
            res = (uint32_t) wide;
            
            if (s) {
            
                set_nz (cpu, res);
                cpu->cpsr &= ~(C_FLAG | V_FLAG);
                
                if (wide >> 32) {
                    cpu->cpsr |= C_FLAG;
                }
                
                if (signed_wide != (int64_t) (int32_t) res) {
                    cpu->cpsr |= V_FLAG;
                }
            
            }
            
            break;
        
        }
        
        case 0x8:
        
            res = op1 & op2;
            set_nz (cpu, res);
            
            write = 0;
            break;
        
        case 0x9:
        
            res = op1 ^ op2;
            set_nz (cpu, res);
            
            write = 0;
            break;
        
        case 0xa:
        
            res = op1 - op2;
            set_sub_flags (cpu, op1, op2, res);
            
            write = 0;
            break;
        
        case 0xb:
        
            res = op1 + op2;
            set_add_flags (cpu, op1, op2, res);
            
            write = 0;
            break;
        
        case 0xc:
        
            res = op1 | op2;
            break;
        
        case 0xd:
        
            res = op2;
            break;
        
        case 0xe:
        
            res = op1 & ~op2;
            break;
        
        case 0xf:
        
            res = ~op2;
            break;
        
        default:
        
            report_at (program_name, 0, REPORT_ERROR, "unimplemented data opcode %x at %08x", opcode, cpu->r[R_PC]);
            exit (EXIT_FAILURE);
    
    }
    
    if (opcode == 0x8 || opcode == 0x9) {
    
        if (carry) {
            cpu->cpsr |= C_FLAG;
        } else {
            cpu->cpsr &= ~C_FLAG;
        }
    
    }
    
    if (write) {
    
        if (rd == R_PC) {
        
            branch_to (cpu, res);
            return;
        
        }
        
        cpu->r[rd] = res;
        
        if (s) {
        
            if (opcode != 0x2 && opcode != 0x3 && opcode != 0x4) {
                set_nz (cpu, res);
            }
            
            if (opcode == 0x0 || opcode == 0x1 || opcode == 0xc || opcode == 0xd || opcode == 0xe || opcode == 0xf) {
            
                if (carry) {
                    cpu->cpsr |= C_FLAG;
                } else {
                    cpu->cpsr &= ~C_FLAG;
                }
            
            }
        
        }
    
    }

}

static uint32_t decode_ls_offset (Mach *cpu, uint32_t insn) {

    int carry = 0;
    
    if (!(insn & (1u << 25))) {
        return insn & 0xfff;
    }
    
    return decode_operand2 (cpu, insn & ~(1U << 25), &carry);

}

static void write_reg_arm (Mach *cpu, unsigned rd, uint32_t val) {

    if (rd == R_PC) {
    
        branch_to (cpu, val);
        return;
    
    }
    
    cpu->r[rd] = val;

}

static void exec_load_store (Mach *cpu, uint32_t insn) {

    uint32_t p = (insn >> 24) & 1;
    uint32_t u = (insn >> 23) & 1;
    uint32_t b = (insn >> 22) & 1;
    uint32_t w = (insn >> 21) & 1;
    uint32_t l = (insn >> 20) & 1;
    
    uint32_t rn = (insn >> 16) & 0xf;
    uint32_t rd = (insn >> 12) & 0xf;
    
    uint32_t base = get_reg (cpu, rn);
    uint32_t off = decode_ls_offset (cpu, insn);
    
    uint32_t addr;
    
    if (u) {
        addr = base + off;
    } else {
        addr = base - off;
    }
    
    if (!p) {
        addr = base;
    }
    
    if (l) {
    
        uint32_t val;
        
        if (b) {
            val = read8 (cpu, addr);
        } else {
            val = read32 (cpu, addr);
        }
        
        if (rd == R_PC && memory_region_kind (cpu, addr, b ? 1 : 4) == MEM_KIND_IMPORT_PTR) {
            stop_unsupported (cpu, "hit Mach-O lazy symbol pointer; dynamic linker/iOS syscalls are not implemented");
        }
        
        write_reg_arm (cpu, rd, val);
    
    } else {
    
        uint32_t val = get_reg (cpu, rd);
        
        if (b) {
            write8 (cpu, addr, val & 0xff);
        } else {
            write32 (cpu, addr, val);
        }
    
    }
    
    if (!p) {
    
        if (u) {
            base += off;
        } else {
            base -= off;
        }
        
        cpu->r[rn] = base;
    
    } else if (w) {
        cpu->r[rn] = addr;
    }

}

static void exec_block_transfer (Mach *cpu, uint32_t insn) {

    uint32_t p = (insn >> 24) & 1;
    uint32_t u = (insn >> 23) & 1;
    uint32_t w = (insn >> 21) & 1;
    uint32_t l = (insn >> 20) & 1;
    uint32_t rn = (insn >> 16) & 0xf;
    
    uint32_t list = insn & 0xffff;
    uint32_t addr = cpu->r[rn];
    
    uint32_t count = 0;
    int i;

    for (i = 0; i < 16; i++) {
    
        if (list & (1u << i)) {
            count++;
        }
    
    }
    
    if (!u) {
        addr -= count * 4;
    }
    
    if (p == u) {
        addr += 4;
    }
    
    for (i = 0; i < 16; i++) {
    
        if (!(list & (1u << i))) {
            continue;
        }
        
        if (l) {
            cpu->r[i] = read32 (cpu, addr);
        } else {
            write32 (cpu, addr, get_reg (cpu, i));
        }
        
        addr += 4;
    
    }

    if (w) {
    
        if (u) {
            cpu->r[rn] += count * 4;
        } else {
            cpu->r[rn] -= count * 4;
        }
    
    }

}

void exec_arm32 (Mach *cpu) {

    uint32_t pc;
    uint32_t insn;
    uint32_t cond;
    
    pc = cpu->r[R_PC];
    
    insn = read32 (cpu, pc);
    cond = insn >> 28;
    
    cpu->last_pc = pc;
    cpu->last_insn = insn;
    
    if (insn == 0xf57ff01fU) {
    
        cpu->exclusive_valid = 0;
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if (!cond_passed (cpu, cond)) {
    
        cpu->r[R_PC] += 4;
        return;
    
    }
    
    if ((insn & 0x0ffffff0U) == 0x012fff10U || (insn & 0x0ffffff0U) == 0x012fff30U) {
    
        uint32_t target = get_reg (cpu, insn & 0xf);
        
        if (insn & 0x20) {
            cpu->r[R_LR] = pc + 4;
        }
        
        branch_to (cpu, target);
        return;
    
    }
    
    if ((insn & 0x0ff00fffU) == 0x01900f9fU) {
    
        uint32_t addr = cpu->r[(insn >> 16) & 15];
        
        cpu->r[(insn >> 12) & 15] = read32 (cpu, addr);
        cpu->exclusive_addr = addr;
        cpu->exclusive_valid = 1;
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if ((insn & 0x0ff00ff0U) == 0x01800f90U) {
    
        uint32_t addr = cpu->r[(insn >> 16) & 15];
        unsigned rd = (insn >> 12) & 15;
        
        if (cpu->exclusive_valid && cpu->exclusive_addr == addr) {
        
            write32 (cpu, addr, cpu->r[insn & 15]);
            cpu->r[rd] = 0;
        
        } else {
            cpu->r[rd] = 1;
        }
        
        cpu->exclusive_valid = 0;
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if ((insn & 0x0fb00000U) == 0x03000000U) {
    
        unsigned rd = (insn >> 12) & 15;
        uint32_t imm = ((insn >> 4) & 0xf000U) | (insn & 0xfffU);
        
        if (insn & (1U << 22)) {
            cpu->r[rd] = (cpu->r[rd] & 0xffffU) | (imm << 16);
        } else {
            cpu->r[rd] = imm;
        }
        
        cpu->r[R_PC] += 4;
        return;
    
    }
    
    if ((insn & 0x0e000090U) == 0x00000090U && (insn & 0x60U)) {
    
        unsigned rn = (insn >> 16) & 15;
        unsigned rd = (insn >> 12) & 15;
        unsigned type = (insn >> 5) & 3;
        uint32_t base = get_reg (cpu, rn);
        uint32_t off = (insn & (1U << 22)) ? ((insn >> 4) & 0xf0U) | (insn & 15U) : get_reg (cpu, insn & 15U);
        uint32_t updated = (insn & (1U << 23)) ? base + off : base - off;
        uint32_t addr = (insn & (1U << 24)) ? updated : base;
        
        if (insn & (1U << 20)) {
        
            if (type == 1) {
                cpu->r[rd] = read16 (cpu, addr);
            } else if (type == 2) {
                cpu->r[rd] = (uint32_t) (int32_t) (int8_t) read8 (cpu, addr);
            } else {
                cpu->r[rd] = (uint32_t) (int32_t) (int16_t) read16 (cpu, addr);
            }
        
        } else if (type == 1) {
            write16 (cpu, addr, (uint16_t) cpu->r[rd]);
        } else {
        
            if (rd >= 14 || (rd & 1)) {
                stop_unsupported (cpu, "invalid ARM doubleword transfer");
            }
            
            if (type == 2) {
            
                cpu->r[rd] = read32 (cpu, addr);
                cpu->r[rd + 1] = read32 (cpu, addr + 4);
            
            } else {
            
                write32 (cpu, addr, cpu->r[rd]);
                write32 (cpu, addr + 4, cpu->r[rd + 1]);
            
            }
        
        }
        
        if (!(insn & (1U << 24)) || (insn & (1U << 21))) {
            cpu->r[rn] = updated;
        }
        
        cpu->r[R_PC] += 4;
        return;
    
    }
    
    if ((insn & 0x0ff000f0U) == 0x00600090U) {
    
        unsigned rd = (insn >> 16) & 15;
        
        cpu->r[rd] = cpu->r[(insn >> 12) & 15] - cpu->r[insn & 15] * cpu->r[(insn >> 8) & 15];
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if ((insn & 0x0fc000f0U) == 0x00000090U) {
    
        unsigned rd = (insn >> 16) & 15;
        uint32_t result = cpu->r[insn & 15] * cpu->r[(insn >> 8) & 15];
        
        if (insn & (1U << 21)) {
            result += cpu->r[(insn >> 12) & 15];
        }
        
        cpu->r[rd] = result;
        
        if (insn & (1U << 20)) {
            set_nz (cpu, result);
        }
        
        cpu->r[R_PC] += 4;
        return;
    
    }
    
    if ((insn & 0x0f8000f0U) == 0x00800090U) {
    
        unsigned hi = (insn >> 16) & 15, lo = (insn >> 12) & 15;
        uint32_t a = cpu->r[insn & 15], b = cpu->r[(insn >> 8) & 15];
        uint64_t result;
        
        if (insn & (1U << 22)) {
            result = (uint64_t) ((int64_t) (int32_t) a * (int32_t) b);
        } else {
            result = (uint64_t) a * b;
        }
        
        if (insn & (1U << 21)) {
            result += ((uint64_t) cpu->r[hi] << 32) | cpu->r[lo];
        }
        
        cpu->r[lo] = (uint32_t) result;
        cpu->r[hi] = (uint32_t) (result >> 32);
        
        if (insn & (1U << 20)) {
        
            cpu->cpsr &= ~(N_FLAG | Z_FLAG);
            cpu->cpsr |= cpu->r[hi] & N_FLAG;
            
            if (!result) {
                cpu->cpsr |= Z_FLAG;
            }
        
        }
        
        cpu->r[R_PC] += 4;
        return;
    
    }
    
    if ((insn & 0x0fff0ff0U) == 0x016f0f10U) {
    
        uint32_t val = cpu->r[insn & 15];
        unsigned count = 0;
        
        while (count < 32 && !(val & 0x80000000U)) {
        
            val <<= 1;
            count++;
        
        }
        
        cpu->r[(insn >> 12) & 15] = count;
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if ((insn & 0x0fff0ff0U) == 0x06ff0f30U) {
    
        uint32_t val = cpu->r[insn & 15], result = 0;
        unsigned i;
        
        for (i = 0; i < 32; i++) {
        
            result = (result << 1) | (val & 1U);
            val >>= 1;
        
        }
        
        cpu->r[(insn >> 12) & 15] = result;
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if ((insn & 0x0fa003f0U) == 0x06a00070U) {
    
        unsigned rn = (insn >> 16) & 15, rd = (insn >> 12) & 15;
        uint32_t val = ror32 (get_reg (cpu, insn & 15), ((insn >> 10) & 3) * 8);
        
        if (insn & (1U << 20)) {
            val = (insn & (1U << 22)) ? val & 0xffffU : (uint32_t) (int32_t) (int16_t) val;
        } else {
            val = (insn & (1U << 22)) ? val & 0xffU : (uint32_t) (int32_t) (int8_t) val;
        }
        
        if (rn != R_PC) {
            val += cpu->r[rn];
        }
        
        cpu->r[rd] = val;
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if ((insn & 0x0fa00070U) == 0x07a00050U) {
    
        unsigned width = ((insn >> 16) & 31) + 1;
        unsigned lsb = (insn >> 7) & 31;
        
        uint32_t val;
        
        if (width + lsb > 32) {
            stop_unsupported (cpu, "invalid ARM bit field extract");
        }
        
        val = cpu->r[insn & 15] >> lsb;
        val &= 0xffffffffU >> (32 - width);
        
        if (!(insn & (1U << 22)) && (val & (1U << (width - 1)))) {
            val |= ~(0xffffffffU >> (32 - width));
        }
        
        cpu->r[(insn >> 12) & 15] = val;
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if ((insn & 0x0fe00070U) == 0x07c00010U) {
    
        unsigned msb = (insn >> 16) & 31;
        unsigned lsb = (insn >> 7) & 31;
        unsigned rd = (insn >> 12) & 15;
        unsigned rn = insn & 15;
        
        uint32_t mask, val;
        
        if (msb < lsb || rd == R_PC) {
            stop_unsupported (cpu, "invalid ARM bit field instruction");
        }
        
        mask = (0xffffffffU >> (31 - msb)) & (0xffffffffU << lsb);
        val = rn == R_PC ? 0 : cpu->r[rn];
        
        cpu->r[rd] = (cpu->r[rd] & ~mask) | ((val << lsb) & mask);
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if (cpu->linux_abi && (insn & 0x0fff0fffU) == 0x0e1d0f70U) {
    
        cpu->r[(insn >> 12) & 0xf] = cpu->tls;
        cpu->r[R_PC] += 4;
        
        return;
    
    }
    
    if ((insn & 0x0f000000U) == 0x0f000000U) {
    
        uint32_t imm = insn & 0x00ffffffU;
        
        if (imm == 0 && cpu->linux_abi) {
        
            linux_syscall (cpu);
            cpu->r[R_PC] += 4;
            
            return;
        
        }
        
        if (imm == 0) {
        
            cpu->running = 0;
            return;
        
        }
        
        report_at (program_name, 0, REPORT_ERROR, "SVC %u at %08x", imm, pc);
        
        cpu->running = 0;
        return;
    
    }
    
    if ((insn & 0x0e000000U) == 0x0a000000U) {
    
        exec_branch (cpu, insn);
        return;
    
    }
    
    if ((insn & 0x0c000000U) == 0x04000000U) {
    
        exec_load_store (cpu, insn);
        
        if (cpu->r[R_PC] == pc) {
            cpu->r[R_PC] += 4;
        }
        
        return;
    
    }
    
    if ((insn & 0x0e000000U) == 0x08000000U) {
    
        exec_block_transfer (cpu, insn);
        
        if ((insn & (1U << 20)) && (insn & (1U << R_PC))) {
            branch_to (cpu, cpu->r[R_PC]);
        } else {
            cpu->r[R_PC] += 4;
        }
        
        return;
    
    }
    
    if ((insn & 0x0c000000U) == 0x00000000U) {
    
        exec_data_proc (cpu, insn);
        
        if (cpu->r[R_PC] == pc) {
            cpu->r[R_PC] += 4;
        }
        
        return;
    
    }
    
    report_at (program_name, 0, REPORT_ERROR, "unimplemented instruction %08x at %08x", insn, pc);
    exit(1);

}

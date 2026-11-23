/******************************************************************************
 * @file            thumb.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>

#include    "armv7.h"
#include    "report.h"

static uint32_t get_reg_t (Mach *cpu, unsigned r) {

    if (r == R_PC) {
        return (cpu->r[R_PC] + 4) & ~3U;
    }
    
    return cpu->r[r];

}

static void set_logic_flags_t (Mach *cpu, uint32_t v) {
    set_nz (cpu, v);
}

static int thumb_cond_passed (Mach *cpu, unsigned cond) {
    return cond_passed (cpu, cond);
}

void exec_thumb16 (Mach *cpu) {

    uint32_t pc = cpu->r[R_PC] & ~1U;
    uint16_t op = read16 (cpu, pc);
    uint32_t next_pc = pc + 2;
    
    check_exec_fetch (cpu, pc, 2);
    
    cpu->last_pc = pc | 1U;
    cpu->last_insn = op;
    
    /* Thumb-2 BL / BLX immediate. */
    if ((op & 0xf800U) == 0xf000U || (op & 0xf800U) == 0xf800U) {
    
        uint16_t op2 = read16 (cpu, pc + 2);
        
        if ((op2 & 0xc000u) == 0xc000u) {
        
            uint32_t s = (op >> 10) & 1;
            
            uint32_t j1 = (op2 >> 13) & 1;
            uint32_t j2 = (op2 >> 11) & 1;
            
            uint32_t i1 = (~(j1 ^ s)) & 1;
            uint32_t i2 = (~(j2 ^ s)) & 1;
            
            uint32_t imm10 = op & 0x03ffu;
            uint32_t imm11 = op2 & 0x07ffu;
            
            uint32_t imm32;
            uint32_t target;
            
            cpu->last_insn = ((uint32_t)op << 16) | op2;
            imm32 = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
            
            if (s) {
                imm32 |= 0xfe000000U;
            }
            
            cpu->r[R_LR] = (pc + 4) | 1U;
            
            if (op2 & 0x1000U) {
            
                /* BL keeps Thumb state. */
                target = pc + 4 + imm32;
                branch_to (cpu, target | 1U);
            
            } else {
            
                /* BLX immediate switches to ARM state and aligns the base PC. */
                target = ((pc + 4) & ~3U) + (imm32 & ~3U);
                cpu->thumb = 0;
                
                cpu->r[R_PC] = target & ~3U;
            
            }
            
            return;
        
        }
        
        report_at (program_name, 0, REPORT_ERROR, "unimplemented Thumb-2 instruction %04x %04x at %08x", op, op2, pc | 1U);
        exit (EXIT_FAILURE);
    
    }
    
    /* Other Thumb-2 32-bit prefixes. */
    if ((op & 0xf800U) == 0xe800U) {
    
        uint16_t op2 = read16 (cpu, pc + 2);
        cpu->last_insn = ((uint32_t) op << 16) | op2;
        
        report_at (program_name, 0, REPORT_ERROR, "unimplemented Thumb-2 instruction %04x %04x at %08x", op, op2, pc | 1U);
        exit (EXIT_FAILURE);
    
    }
    
    /* LSL/LSR/ASR immediate */
    if ((op & 0xe000U) == 0x0000U && (op & 0x1800U) != 0x1800U) {
    
        unsigned type = (op >> 11) & 3;
        unsigned imm = (op >> 6) & 0x1f;
        unsigned rm = (op >> 3) & 7;
        unsigned rd = op & 7;
        
        uint32_t val = cpu->r[rm];
        uint32_t res;
        
        if (type == 0) {
        
            res = val << imm;
            
            if (imm) {
            
                if (val & (1U << (32 - imm))) {
                    cpu->cpsr |= C_FLAG;
                } else {
                    cpu->cpsr &= ~C_FLAG;
                }
            
            }
        
        } else if (type == 1) {
        
            if (imm == 0) {
                imm = 32;
            }
            
            res = (imm == 32) ? 0 : (val >> imm);
            
            if (val & (1U << (imm - 1))) {
                cpu->cpsr |= C_FLAG;
            } else {
                cpu->cpsr &= ~C_FLAG;
            }
        
        } else {
        
            if (imm == 0) {
                imm = 32;
            }
            
            res = (uint32_t) (((int32_t) val) >> imm);
            
            if (val & (1U << (imm - 1))) {
                cpu->cpsr |= C_FLAG;
            } else {
                cpu->cpsr &= ~C_FLAG;
            }
        
        }
        
        cpu->r[rd] = res;
        set_nz (cpu, res);
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* ADD/SUB register/immediate */
    if ((op & 0xf800U) == 0x1800U) {
    
        unsigned immflag = (op >> 10) & 1;
        unsigned sub = (op >> 9) & 1;
        unsigned rn_imm = (op >> 6) & 7;
        unsigned rm = (op >> 3) & 7;
        unsigned rd = op & 7;
        
        uint32_t a = cpu->r[rm];
        uint32_t b = immflag ? rn_imm : cpu->r[rn_imm];
        
        uint32_t res = sub ? (a - b) : (a + b);
        cpu->r[rd] = res;
        
        if (sub) {
            set_sub_flags (cpu, a, b, res);
        } else {
            set_add_flags (cpu, a, b, res);
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* MOV/CMP/ADD/SUB immediate */
    if ((op & 0xe000U) == 0x2000U) {
    
        unsigned subop = (op >> 11) & 3;
        unsigned rd = (op >> 8) & 7;
        
        uint32_t imm = op & 0xff;
        uint32_t a, res;
        
        if (subop == 0) {
        
            cpu->r[rd] = imm;
            set_logic_flags_t (cpu, imm);
        
        } else if (subop == 1) {
        
            a = cpu->r[rd];
            
            res = a - imm;
            set_sub_flags (cpu, a, imm, res);
        
        } else if (subop == 2) {
        
            a = cpu->r[rd];
            
            res = a + imm;
            cpu->r[rd] = res;
            
            set_add_flags (cpu, a, imm, res);
        
        } else {
        
            a = cpu->r[rd];
            
            res = a - imm;
            cpu->r[rd] = res;
            
            set_sub_flags (cpu, a, imm, res);
        
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* ALU register ops */
    if ((op & 0xfc00U) == 0x4000U) {
    
        unsigned alu = (op >> 6) & 0xf;
        unsigned rm = (op >> 3) & 7;
        unsigned rd = op & 7;
        
        uint32_t a = cpu->r[rd];
        uint32_t b = cpu->r[rm];
        
        uint32_t res;
        
        switch (alu) {
        
            case 0x0:
            
                res = a & b;
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            case 0x1:
            
                res = a ^ b;
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            case 0x2:
            
                res = a << (b & 0xff);
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            case 0x3:
            
                res = a >> (b & 0xff);
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            case 0x4:
            
                res = (uint32_t) ((int32_t) a >> (b & 0xff));
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            case 0x8:
            
                res = a & b;
                set_nz (cpu, res);
                
                break;
            
            case 0x9:
            
                res = 0 - b;
                
                cpu->r[rd] = res;
                set_sub_flags (cpu, 0, b, res);
                
                break;
            
            case 0xa:
            
                res = a - b;
                set_sub_flags (cpu, a, b, res);
                
                break;
            
            case 0xb:
            
                res = a + b;
                set_add_flags (cpu, a, b, res);
                
                break;
            
            case 0xc:
            
                res = a | b;
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            case 0xd:
            
                res = a * b;
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            case 0xe:
            
                res = a & ~b;
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            case 0xf:
            
                res = ~b;
                
                cpu->r[rd] = res;
                set_nz (cpu, res);
                
                break;
            
            default:
            
                report_at (program_name, 0, REPORT_ERROR, "unimplemented Thumb ALU op %x at %08x", alu, pc | 1U);
                exit (EXIT_FAILURE);
        
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* High-register ops / BX / BLX */
    if ((op & 0xfc00u) == 0x4400u) {
    
        unsigned op2 = (op >> 8) & 3;
        
        unsigned h1 = (op >> 7) & 1;
        unsigned h2 = (op >> 6) & 1;
        
        unsigned rs = ((op >> 3) & 7) | (h2 << 3);
        unsigned rd = (op & 7) | (h1 << 3);
        
        uint32_t target;
        
        if (op2 == 0) {
        
            cpu->r[rd] = get_reg_t (cpu, rd) + get_reg_t (cpu, rs);
            
            if (rd == R_PC) {
                branch_to (cpu, cpu->r[R_PC]);    
            } else {
                cpu->r[R_PC] = next_pc | 1U;
            }
            
            return;
        
        }
        
        if (op2 == 1) {
        
            uint32_t res = get_reg_t (cpu, rd) - get_reg_t (cpu, rs);
            
            set_sub_flags (cpu, get_reg_t (cpu, rd), get_reg_t (cpu, rs), res);
            cpu->r[R_PC] = next_pc | 1U;
            
            return;
        
        }
        
        if (op2 == 2) {
        
            cpu->r[rd] = get_reg_t (cpu, rs);
            
            if (rd == R_PC) {
                branch_to (cpu, cpu->r[R_PC]);
            } else {
                cpu->r[R_PC] = next_pc | 1U;
            }
            
            return;
        
        }
        
        target = cpu->r[rs];
        branch_to (cpu, target);
        
        return;
    
    }
    
    /* PC-relative LDR */
    if ((op & 0xf800U) == 0x4800U) {
    
        unsigned rd = (op >> 8) & 7;
        
        uint32_t imm = (op & 0xff) << 2;
        uint32_t addr = ((pc + 4) & ~3U) + imm;
        
        cpu->r[rd] = read32 (cpu, addr);
        cpu->r[R_PC] = next_pc | 1U;
        
        return;
    
    }
    
    /* STR/LDR register */
    if ((op & 0xf200U) == 0x5000U) {
    
        unsigned l = (op >> 11) & 1;
        unsigned b = (op >> 10) & 1;
        
        unsigned ro = (op >> 6) & 7;
        unsigned rb = (op >> 3) & 7;
        unsigned rd = op & 7;
        
        uint32_t addr = cpu->r[rb] + cpu->r[ro];
        
        if (l) {
            cpu->r[rd] = b ? read8 (cpu, addr) : read32 (cpu, addr);
        } else {
        
            if (b) {
                write8 (cpu, addr, cpu->r[rd] & 0xff);
            } else {
                write32 (cpu, addr, cpu->r[rd]);
            }
        
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* STR/LDR immediate word/byte */
    if ((op & 0xe000U) == 0x6000U) {
    
        unsigned b = (op >> 12) & 1;
        unsigned l = (op >> 11) & 1;
        
        unsigned imm5 = (op >> 6) & 0x1f;
        
        unsigned rb = (op >> 3) & 7;
        unsigned rd = op & 7;
        
        uint32_t addr = cpu->r[rb] + (b ? imm5 : (imm5 << 2));
        
        if (l) {
            cpu->r[rd] = b ? read8 (cpu, addr) : read32 (cpu, addr);
        } else {
        
            if (b) {
                write8 (cpu, addr, cpu->r[rd] & 0xff);
            } else {
                write32 (cpu, addr, cpu->r[rd]);
            }
        
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* STRH/LDRH immediate */
    if ((op & 0xf000U) == 0x8000U) {
    
        unsigned imm5 = (op >> 6) & 0x1f;
        unsigned l = (op >> 11) & 1;
        
        unsigned rb = (op >> 3) & 7;
        unsigned rd = op & 7;
        
        uint32_t addr = cpu->r[rb] + (imm5 << 1);
        
        if (l) {
            cpu->r[rd] = read16 (cpu, addr);
        } else {
            write16 (cpu, addr, cpu->r[rd] & 0xffff);
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* SP-relative STR/LDR */
    if ((op & 0xf000U) == 0x9000U) {
    
        unsigned l = (op >> 11) & 1;
        unsigned rd = (op >> 8) & 7;
        
        uint32_t addr = cpu->r[R_SP] + ((op & 0xff) << 2);
        
        if (l) {
            cpu->r[rd] = read32 (cpu, addr);
        } else {
            write32 (cpu, addr, cpu->r[rd]);
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* ADD to PC/SP */
    if ((op & 0xf000U) == 0xa000U) {
    
        unsigned sp = (op >> 11) & 1;
        unsigned rd = (op >> 8) & 7;
        
        uint32_t imm = (op & 0xff) << 2;
        
        cpu->r[rd] = (sp ? cpu->r[R_SP] : ((pc + 4) & ~3U)) + imm;
        cpu->r[R_PC] = next_pc | 1U;
        
        return;
    
    }
    
    /* ADD/SUB SP */
    if ((op & 0xff00U) == 0xb000U) {
    
        uint32_t imm = (op & 0x7f) << 2;
        
        if (op & 0x80) {
            cpu->r[R_SP] -= imm;
        } else {
            cpu->r[R_SP] += imm;
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* PUSH */
    if ((op & 0xfe00U) == 0xb400U) {
    
        uint32_t list = op & 0xff;
        int i;
        
        if (op & 0x0100u) {
            list |= 1U << R_LR;
        }
        
        for (i = 15; i >= 0; i--) {
        
            if (list & (1U << i)) {
            
                cpu->r[R_SP] -= 4;
                write32 (cpu, cpu->r[R_SP], cpu->r[i]);
            
            }
        
        }
        
        cpu->r[R_PC] = next_pc | 1U;
        return;
    
    }
    
    /* POP */
    if ((op & 0xfe00U) == 0xbc00U) {
    
        uint32_t list = op & 0xff;
        int i;
        
        if (op & 0x0100u) {
            list |= 1U << R_PC;
        }
        
        for (i = 0; i < 16; i++) {
        
            if (list & (1U << i)) {
            
                cpu->r[i] = read32 (cpu, cpu->r[R_SP]);
                cpu->r[R_SP] += 4;
            
            }
        
        }
        
        if (list & (1U << R_PC)) {
            branch_to (cpu, cpu->r[R_PC]);
        } else {
            cpu->r[R_PC] = next_pc | 1U;
        }
        
        return;
    
    }
    
    /* STMIA/LDMIA */
    if ((op & 0xf000U) == 0xc000U) {
    
        unsigned l = (op >> 11) & 1;
        unsigned rb = (op >> 8) & 7;
        
        uint32_t list = op & 0xff;
        uint32_t addr = cpu->r[rb];
        
        int i;
        
        for (i = 0; i < 8; i++) {
        
            if (list & (1U << i)) {
            
                if (l) {
                    cpu->r[i] = read32 (cpu, addr);
                } else {
                    write32 (cpu, addr, cpu->r[i]);
                }
                
                addr += 4;
            
            }
        
        }
        
        cpu->r[rb] = addr;
        cpu->r[R_PC] = next_pc | 1U;
        
        return;
    
    }
    
    /* Conditional branch / SVC */
    if ((op & 0xf000U) == 0xd000U) {
    
        unsigned cond = (op >> 8) & 0xf;
        int32_t off = (int8_t) (op & 0xff);
        
        if (cond == 0xf) {
        
            if (cpu->linux_abi && !(op & 0xff)) {
            
                linux_syscall (cpu);
                cpu->r[R_PC] = next_pc | 1U;
                
                return;
            
            }
            
            cpu->running = 0;
            return;
        
        }
        
        if (cond == 0xe) {
        
            report_at (program_name, 0, REPORT_ERROR, "undefined Thumb instruction %04x at %08x", op, pc | 1U);
            exit (EXIT_FAILURE);
        
        }
        
        if (thumb_cond_passed(cpu, cond)) {
            branch_to (cpu, (next_pc + (off << 1)) | 1U);
        } else {
            cpu->r[R_PC] = next_pc | 1U;
        }
        
        return;
    
    }
    
    /* Unconditional branch */
    if ((op & 0xf800U) == 0xe000U) {
    
        int32_t off = op & 0x7ff;
        
        if (off & 0x400) {
            off |= ~0x7ff;
        }
        
        branch_to (cpu, (next_pc + (off << 1)) | 1U);
        return;
    
    }
    
    report_at (program_name, 0, REPORT_ERROR, "unimplemented Thumb instruction %04x at %08x", op, pc | 1U);
    exit (EXIT_FAILURE);

}

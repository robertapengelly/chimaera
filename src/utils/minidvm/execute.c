/******************************************************************************
 * @file            execute.c
 *****************************************************************************/
#include "mini_dalvik_vm.h"

typedef struct {

    Value regs[DVM_MAX_REGS];
    int reg_count;
    
    Value last_result;
    const DexFile *dex;
    
    int trace;

} ExecContext;

Obj *g_pending_exception = NULL;

static void set_reg_value (ExecContext *ctx, int reg, Value v) {

    if (ctx == NULL || reg < 0 || reg >= ctx->reg_count) {
        return;
    }
    
    ctx->regs[reg] = v;
    
    if ((v.kind == VAL_WIDE || v.kind == VAL_DOUBLE) && reg + 1 < ctx->reg_count) {
        ctx->regs[reg + 1] = v;
    }

}

static int value_get_float (Value v, float *out) {

    u4 bits;
    
    if (out == NULL) {
        return 0;
    }
    
    if (v.kind == VAL_DOUBLE) {
    
        *out = (float)v.d;
        return 1;
    
    }
    
    if (v.kind != VAL_INT) {
        return 0;
    }
    
    bits = (u4) v.i;
    
    memcpy (out, &bits, sizeof (*out));
    return 1;

}

static Value value_float (float v) {

    u4 bits;

    memcpy (&bits, &v, sizeof (bits));
    return value_int ((long)(s4)bits);

}

static int value_get_double (Value v, double *out) {

    unsigned long long bits;
    
    if (out == NULL) {
        return 0;
    }
    
    if (v.kind == VAL_DOUBLE) {
    
        *out = v.d;
        return 1;
    
    }
    
    if (v.kind != VAL_WIDE) {
        return 0;
    }
    
    bits = v.wide;
    
    memcpy (out, &bits, sizeof (*out));
    return 1;

}

static int gather_invoke_args (const DexFile *dex, ExecContext *ctx, u4 insns_off, u4 pc, int is_range, int has_this, Value *args, int *out_argc, u4 *out_method_idx) {

    u2 insn;
    u2 ref;
    
    const char *cls;
    const char *name;
    
    u2 proto_idx;
    
    int param_count;
    int dst_i;
    int p_i;
    
    if (dex == NULL || ctx == NULL || args == NULL || out_argc == NULL || out_method_idx == NULL) {
        return 0;
    }
    
    insn = dex_read_u2 (dex, insns_off + pc * 2);
    ref = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
    
    *out_method_idx = ref;
    
    if (!dex_method_name_parts (dex, ref, &cls, &name, &proto_idx)) {
        return 0;
    }
    
    param_count = proto_param_count (dex, proto_idx);
    
    if (param_count < 0) {
        return 0;
    }
    
    dst_i = 0;
    
    if (!is_range) {
    
        int count = (insn >> 12) & 0x0f;
        int regs5[5];
        
        u2 word3 = dex_read_u2 (dex, insns_off + (pc + 2) * 2);
        
        regs5[0] = word3 & 0x0f;
        regs5[1] = (word3 >> 4) & 0x0f;
        regs5[2] = (word3 >> 8) & 0x0f;
        regs5[3] = (word3 >> 12) & 0x0f;
        regs5[4] = (insn >> 8) & 0x0f;
        
        if (count < 0 || count > 5) {
            return 0;
        }
        
        if (has_this) {
        
            if (count <= 0) {
                return 0;
            }
            
            if (regs5[0] >= ctx->reg_count) {
                return 0;
            }
            
            args[dst_i++] = ctx->regs[regs5[0]];
            
            for (p_i = 0; p_i < param_count; p_i++) {
            
                int src_index = 1 + p_i;
                
                if (src_index >= count || regs5[src_index] >= ctx->reg_count) {
                    return 0;
                }
                
                args[dst_i++] = ctx->regs[regs5[src_index]];
            
            }
        
        } else {
        
            for (p_i = 0; p_i < param_count; p_i++) {
            
                if (p_i >= count || regs5[p_i] >= ctx->reg_count) {
                    return 0;
                }
                
                args[dst_i++] = ctx->regs[regs5[p_i]];
            
            }
        
        }
    
    } else {
    
        int count = (insn >> 8) & 0xff;
        int first = dex_read_u2 (dex, insns_off + (pc + 2) * 2);
        
        if (has_this) {
        
            if (count <= 0 || first >= ctx->reg_count) {
                return 0;
            }
            
            args[dst_i++] = ctx->regs[first];
            
            for (p_i = 0; p_i < param_count; p_i++) {
            
                int r = first + 1 + p_i;
                
                if (r >= ctx->reg_count) {
                    return 0;
                }
                
                args[dst_i++] = ctx->regs[r];
            
            }
        
        } else {
        
            for (p_i = 0; p_i < param_count; p_i++) {
            
                int r = first + p_i;
                
                if (p_i >= count || r >= ctx->reg_count) {
                    return 0;
                }
                
                args[dst_i++] = ctx->regs[r];
            
            }
        
        }
    
    }
    
    *out_argc = dst_i;
    return 1;

}

static int exec_method_by_code_args (const DexFile *dex, const char *class_desc, const char *method_name, u4 method_idx, u4 code_off, int trace, Value *args, int argc, Value *ret) {

    ExecContext ctx;
    
    u2 registers_size;
    u2 ins_size;
    u4 insns_size;
    u4 insns_off;
    u4 pc;
    
    int i;
    (void) method_idx;
    
    registers_size = dex_read_u2 (dex, code_off + 0);
    ins_size = dex_read_u2 (dex, code_off + 2);
    
    insns_size = dex_read_u4 (dex, code_off + 12);
    insns_off = code_off + 16;
    
    if (registers_size > DVM_MAX_REGS || !dex_bounds_ok (dex, insns_off, insns_size * 2)) {
        return 0;
    }
    
    memset (&ctx, 0, sizeof (ctx));
    ctx.reg_count = (int) registers_size;
    
    ctx.trace = trace;
    ctx.dex = dex;
    
    ctx.last_result = value_int (0);
    
    /* copy incoming args */
    if (argc > 0) {
    
        int base = (int) registers_size - (int) ins_size;
        
        if (argc > (int) ins_size || argc > (int) registers_size) {
        
            fprintf (stderr, "bad invoke frame setup for %s->%s: registers=%u ins=%u argc=%d\n", class_desc, method_name, (unsigned) registers_size, (unsigned) ins_size, argc);
            return 0;
        
        }
        
        for (i = 0; i < argc; i++) {
            set_reg_value (&ctx, base + i, args[i]);
        }
    
    }
    
    pc = 0;
    
    while (pc < insns_size) {
    
        u2 insn;
        u1 op;
        
        insn = dex_read_u2 (dex, insns_off + pc * 2);
        op = (u1)(insn & 0xff);
        
        if (trace) {
            printf ("pc=%u op=%02x\n", pc, (unsigned) op);
        }
        
        switch (op) {
        
            case 0x00:
            
                pc += 1;
                break;
            
            case 0x01: {                /* move */
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.regs[b]);
                
                pc += 1;
                break;
            
            }
            
            case 0x02: {                /* move/from16 */
            
                int a = (insn >> 8) & 0xff;
                int b;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                b = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.regs[b]);
                
                pc += 2;
                break;
            
            }
            
            case 0x03: {                /* move/16 */
            
                int a;
                int b;
                
                if (pc + 2 >= insns_size) {
                    return 0;
                }
                
                a = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                b = dex_read_u2 (dex, insns_off + (pc + 2) * 2);
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.regs[b]);
                
                pc += 3;
                break;
            
            }
            
            case 0x04: {                /* move-wide */
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a + 1 >= ctx.reg_count || b + 1 >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.regs[b]);
                
                pc += 1;
                break;
            
            }
            
            case 0x05: {                /* move-wide/from16 */
            
                int a = (insn >> 8) & 0xff;
                int b;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                b = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                if (a + 1 >= ctx.reg_count || b + 1 >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.regs[b]);
                
                pc += 2;
                break;
            
            }
            
            case 0x06: {                /* move-wide/16 */
            
                int a;
                int b;
                
                if (pc + 2 >= insns_size) {
                    return 0;
                }
                
                a = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                b = dex_read_u2 (dex, insns_off + (pc + 2) * 2);
                
                if (a + 1 >= ctx.reg_count || b + 1 >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.regs[b]);
                
                pc += 3;
                break;
            
            }
            
            case 0x0a: {
            
                int a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.last_result);
                
                pc += 1;
                break;
            
            }
            
            case 0x0b: {
            
                int a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.last_result);
                
                pc += 1;
                break;
            
            }
            
            case 0x0c: {
            
                int a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.last_result);
                
                pc += 1;
                break;
            
            }
            
            case 0x0e:
            
                *ret = value_int (0);
                return 1;
            
            case 0x0f: {
            
                int a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count) {
                    return 0;
                }
                
                *ret = ctx.regs[a];
                return 1;
            
            }
            
            case 0x10: {
            
                int a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count) {
                    return 0;
                }
                
                *ret = ctx.regs[a];
                return 1;
            
            }
            
            case 0x11: {
            
                int a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count) {
                    return 0;
                }
                
                *ret = ctx.regs[a];
                return 1;
            
            }
            
            case 0x12: {
            
                int a = (insn >> 8) & 0x0f;
                s4 lit = sx4 ((insn >> 12) & 0x0f);
                
                if (a >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_int (lit));
                
                pc += 1;
                break;
            
            }
            
            case 0x13: {
            
                int a;
                s4 lit;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                lit = sx16 (dex_read_u2 (dex, insns_off + (pc + 1) * 2));
                set_reg_value (&ctx, a, value_int (lit));
                
                pc += 2;
                break;
            
            }
            
            case 0x14: {
            
                int a;
                
                s4 lit32;
                u4 lo, hi, v;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 2 >= insns_size) {
                    return 0;
                }
                
                lo = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                hi = dex_read_u2 (dex, insns_off + (pc + 2) * 2);
                
                v = lo | (hi << 16);
                lit32 = (s4)v;
                
                set_reg_value (&ctx, a, value_int (lit32));
                
                pc += 3;
                break;
            
            }
            
            case 0x15: {
            
                int a;
                s4 lit;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                lit = (s4) (dex_read_u2 (dex, insns_off + (pc + 1) * 2) << 16);
                set_reg_value (&ctx, a, value_int (lit));
                
                pc += 2;
                break;
            
            }
            
            case 0x16: {
            
                int a;
                s4 lit;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                lit = sx16 (dex_read_u2 (dex, insns_off + (pc + 1) * 2));
                set_reg_value (&ctx, a, value_wide ((unsigned long long) (long long) lit));
                
                pc += 2;
                break;
            
            }
            
            case 0x17: {
            
                int a;
                
                u4 lo, hi, v;
                s4 lit;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 2 >= insns_size) {
                    return 0;
                }
                
                lo = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                hi = dex_read_u2 (dex, insns_off + (pc + 2) * 2);
                
                v = lo | (hi << 16);
                lit = (s4)v;
                
                set_reg_value (&ctx, a, value_wide ((unsigned long long) (long long) lit));
                
                pc += 3;
                break;
            
            }
            
            case 0x19: {
            
                int a;
                u2 high16;
                
                unsigned long long bits;
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                high16 = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                bits = ((unsigned long long) high16) << 48;
                
                set_reg_value (&ctx, a, value_wide (bits));
                
                pc += 2;
                break;
            
            }
            
            case 0x1a: {
            
                int a;
                
                u2 string_idx;
                const char *s;
                
                Obj *o;
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                string_idx = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                if (!dex_get_string (dex, string_idx, &s)) {
                    return 0;
                }
                
                o = obj_new_string(s);
                
                if (o == NULL) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_obj (o));
                
                pc += 2;
                break;
            
            }
            
            case 0x21: {                /* array-length */
            
                int a;
                int b;
                
                a = (insn >> 8) & 0x0f;
                b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || ctx.regs[b].kind != VAL_OBJ || ctx.regs[b].obj == NULL || ctx.regs[b].obj->type != OBJ_ARRAY) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_int (ctx.regs[b].obj->array_len));
                
                pc += 1;
                break;
            
            }
            
            case 0x22: { // new-instance vAA, type@BBBB
            
                int a;
                
                u2 type_idx;
                const char *desc;
                
                Obj *o;
                
                /* 1. Correctly extract register AA */
                a = (insn >> 8) & 0xff; 
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                /* 2. Correctly read type_idx from the current DEX */
                type_idx = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                if (!dex_get_type_descriptor (dex, type_idx, &desc)) {
                    return 0;
                }
                
                /* 3. TRIGGER DYNAMIC LOAD: Check if class exists; if not, load .dex */
                if (find_dex_for_class (desc) == NULL) {
                    try_dynamic_load (desc);
                }
                
                /* 4. Create the object using your working helper */
                o = obj_new_for_desc (desc);
                
                if (o == NULL) {
                    return 0;
                }
                
                /* 5. Safely set the register and increment PC by 2 (the size of new-instance) */
                set_reg_value (&ctx, a, value_obj (o));
                
                pc += 2;
                break;
            
            }
            
            case 0x27: {                /* throw vAA */
            
                int a;
                
                Obj *exception;
                u4 handler;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[a].kind != VAL_OBJ || ctx.regs[a].obj == NULL) {
                
                    fprintf (stderr, "throw requires a non-null object reference in v%d\n", a);
                    return 0;
                
                }
                
                exception = ctx.regs[a].obj;
                g_pending_exception = exception;
                
                if (exception->class_desc != NULL && dex_find_exception_handler (dex, code_off, pc, exception->class_desc, &handler)) {
                
                    pc = handler;
                    break;
                
                }
                
                return 0;
            
            }
            
            case 0x28: {
            
                s4 off;
                s4 target;
                
                off = sx8 ((insn >> 8) & 0xff);
                target = (s4) pc + off;
                
                if (target < 0 || (u4) target >= insns_size) {
                    return 0;
                }
                
                pc = (u4) target;
                break;
            
            }
            
            case 0x29: {
            
                s4 off;
                s4 target;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                off = sx16 (dex_read_u2 (dex, insns_off + (pc + 1) * 2));
                target = (s4) pc + off;
                
                if (target < 0 || (u4) target >= insns_size) {
                    return 0;
                }
                
                pc = (u4) target;
                break;
            
            }
            
            case 0x2d: {                /* cmp-long vAA, vBB, vCC */
            
                int a;
                int b;
                int c;
                
                u2 pair;
                
                long long x;
                long long y;
                
                int result;
                a = (insn >> 8) & 0xff;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                b = pair & 0xff;
                c = (pair >> 8) & 0xff;
                
                if (a >= ctx.reg_count || b + 1 >= ctx.reg_count || c + 1 >= ctx.reg_count || ctx.regs[b].kind != VAL_WIDE || ctx.regs[c].kind != VAL_WIDE) {
                    return 0;
                }
                
                x = (long long) ctx.regs[b].wide;
                y = (long long) ctx.regs[c].wide;
                
                result = x < y ? -1 : (x > y ? 1 : 0);
                set_reg_value (&ctx, a, value_int (result));
                
                pc += 2;
                break;
            
            }
            
            case 0x2e:      case 0x2f: {                    /* cmpl-float/cmpg-float vAA, vBB, vCC */
            
                int a;
                int b;
                int c;
                
                u2 pair;
                
                float x;
                float y;
                
                int result;
                a = (insn >> 8) & 0xff;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                b = pair & 0xff;
                c = (pair >> 8) & 0xff;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || c >= ctx.reg_count || !value_get_float (ctx.regs[b], &x) || !value_get_float (ctx.regs[c], &y)) {
                    return 0;
                }
                
                if (isnan (x) || isnan (y)) {
                    result = op == 0x2e ? -1 : 1;
                } else {
                    result = x < y ? -1 : (x > y ? 1 : 0);
                }
                
                set_reg_value (&ctx, a, value_int (result));
                
                pc += 2;
                break;
            
            }
            
            case 0x30:      case 0x31: {                    /* cmpl-double/cmpg-double vAA, vBB, vCC */
            
                int a;
                int b;
                int c;
                
                u2 pair;
                
                double x;
                double y;
                
                int result;
                a = (insn >> 8) & 0xff;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                b = pair & 0xff;
                c = (pair >> 8) & 0xff;
                
                if (a >= ctx.reg_count || b + 1 >= ctx.reg_count || c + 1 >= ctx.reg_count || !value_get_double (ctx.regs[b], &x) || !value_get_double (ctx.regs[c], &y)) {
                    return 0;
                }
                
                if (isnan (x) || isnan (y)) {
                    result = op == 0x30 ? -1 : 1;
                } else {
                    result = x < y ? -1 : (x > y ? 1 : 0);
                }
                
                set_reg_value (&ctx, a, value_int (result));
                
                pc += 2;
                break;
            
            }
            
            case 0x32:      case 0x33:      case 0x34:      case 0x35:      case 0x36:      case 0x37: {
            
                int a;
                int b;
                
                s4 off;
                s4 target;
                
                int take;
                
                a = (insn >> 8) & 0x0f;
                b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                off = sx16 (dex_read_u2 (dex, insns_off + (pc + 1) * 2));
                
                if (ctx.regs[a].kind != VAL_INT || ctx.regs[b].kind != VAL_INT) {
                    return 0;
                }
                
                take = 0;
                
                if (op == 0x32) {
                    take = (ctx.regs[a].i == ctx.regs[b].i);
                }
                
                if (op == 0x33) {
                    take = (ctx.regs[a].i != ctx.regs[b].i);
                }
                
                if (op == 0x34) {
                    take = (ctx.regs[a].i <  ctx.regs[b].i);
                }
                
                if (op == 0x35) {
                    take = (ctx.regs[a].i >= ctx.regs[b].i);
                }
                
                if (op == 0x36) {
                    take = (ctx.regs[a].i >  ctx.regs[b].i);
                }
                
                if (op == 0x37) {
                    take = (ctx.regs[a].i <= ctx.regs[b].i);
                }
                
                if (take) {
                
                    target = (s4) pc + off;
                    
                    if (target < 0 || (u4) target >= insns_size) {
                        return 0;
                    }
                    
                    pc = (u4) target;
                
                } else {
                    pc += 2;
                }
                
                break;
            
            }
            
            case 0x07: {                /* move-object */
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.regs[b]);
                
                pc += 1;
                break;
            
            }
            
            case 0x08: {                /* move-object/from16 */
            
                int a = (insn >> 8) & 0xff;
                int b;
                
                u2 word2;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                word2 = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                b = word2;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, ctx.regs[b]);
                
                pc += 2;
                break;
            
            }
            
            case 0x09: {                /* move-object/16 */
            
                u2 a16, b16;
                
                if (pc + 2 >= insns_size) {
                    return 0;
                }
                
                a16 = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                b16 = dex_read_u2 (dex, insns_off + (pc + 2) * 2);
                
                if ((int)a16 >= ctx.reg_count || (int)b16 >= ctx.reg_count) {
                    return 0;
                }
                
                set_reg_value (&ctx, (int)a16, ctx.regs[(int)b16]);
                
                pc += 3;
                break;
            
            }
            
            case 0x0d: {                /* move-exception vAA */
            
                int a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || g_pending_exception == NULL) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_obj (g_pending_exception));
                g_pending_exception = NULL;
                
                pc += 1;
                break;
            
            }
            
            case 0x18: {
            
                unsigned long long bits;
                int a;
                
                u4 w0, w1, w2, w3;
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 4 >= insns_size) {
                    return 0;
                }
                
                w0 = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                w1 = dex_read_u2 (dex, insns_off + (pc + 2) * 2);
                w2 = dex_read_u2 (dex, insns_off + (pc + 3) * 2);
                w3 = dex_read_u2 (dex, insns_off + (pc + 4) * 2);
                
                bits = (unsigned long long) w0 | ((unsigned long long) w1 << 16) | ((unsigned long long) w2 << 32) | ((unsigned long long) w3 << 48);
                set_reg_value (&ctx, a, value_wide (bits));
                
                pc += 5;
                break;
            
            }
            
            case 0x1c: {
            
                int a;
                u2 type_idx;
                
                const char *desc;
                Obj *o;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                type_idx = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                if (!dex_get_type_descriptor (dex, type_idx, &desc)) {
                    return 0;
                }
                
                o = obj_new_class(desc);
                
                if (o == NULL) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_obj (o));
                
                pc += 2;
                break;
            
            }
            
            case 0x1f: {
            
                pc += 2;
                break;
            
            }
            
            case 0x23: {
            
                int a;
                int b;
                
                u2 type_idx;
                Obj *o;
                
                a = (insn >> 8) & 0x0f;
                b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                type_idx = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                (void) type_idx;
                
                if (ctx.regs[b].kind != VAL_INT) {
                    return 0;
                }
                
                o = obj_new_array ((int)ctx.regs[b].i);
                
                if (o == NULL) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_obj (o));
                
                pc += 2;
                break;
            
            }
            
            case 0x4b:  /* aput */      case 0x4c:  /* aput-wide */     case 0x4e:  /* aput-boolean */      case 0x4f:  /* aput-byte */     case 0x50:  /* aput-char */     case 0x51:  /* aput-short */ {
            
                int a, b, c;
                u2 word2;
                
                a = (insn >> 8) & 0xff;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                word2 = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                b = word2 & 0xff;
                c = (word2 >> 8) & 0xff;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || c >= ctx.reg_count || ctx.regs[b].kind != VAL_OBJ || ctx.regs[c].kind != VAL_INT || !array_set (ctx.regs[b].obj, (int) ctx.regs[c].i, ctx.regs[a])) {
                    return 0;
                }
                
                pc += 2;
                break;
            
            }
            
            case 0x38:      case 0x39:      case 0x3a:      case 0x3b:      case 0x3c:      case 0x3d: {
            
                int a;
                
                s4 off;
                s4 target;
                
                int take;
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                off = sx16 (dex_read_u2 (dex, insns_off + (pc + 1) * 2));
                
                if (ctx.regs[a].kind == VAL_OBJ) {
                    take = op == 0x38 ? ctx.regs[a].obj == NULL : op == 0x39 ? ctx.regs[a].obj != NULL : 0;
                } else if (ctx.regs[a].kind == VAL_INT) {
                    take = op == 0x38 ? ctx.regs[a].i == 0 : op == 0x39 ? ctx.regs[a].i != 0 : op == 0x3a ? ctx.regs[a].i <  0 : op == 0x3b ? ctx.regs[a].i >= 0 : op == 0x3c ? ctx.regs[a].i >  0 : ctx.regs[a].i <= 0;
                } else {
                    take = 0;
                }
                
                if (take) {
                
                    target = (s4) pc + off;
                    
                    if (target < 0 || (u4) target >= insns_size) {
                        return 0;
                    }
                    
                    pc = (u4) target;
                
                } else {
                    pc += 2;
                }
                
                break;
            
            }
            
            case 0x44:  /* aget */      case 0x45:  /* aget-wide */     case 0x46:  /* aget-object */       case 0x47:  /* aget-boolean */      case 0x48:  /* aget-byte */     case 0x49:  /* aget-char */     case 0x4a: { /* aget-short */
            
                int a;
                int b;
                int c;
                
                u2 pair;
                Value value;
                
                a = (insn >> 8) & 0xff;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                b = pair & 0xff;
                c = (pair >> 8) & 0xff;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || c >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_OBJ || ctx.regs[b].obj == NULL || ctx.regs[c].kind != VAL_INT) {
                    return 0;
                }
                
                if (!array_get (ctx.regs[b].obj, (int)ctx.regs[c].i, &value)) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value);
                
                pc += 2;
                break;
            
            }
            
            case 0x4d: {
            
                int a;
                int b;
                int c;
                
                u2 pair;
                a = (insn >> 8) & 0xff;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                b = pair & 0xff;
                c = (pair >> 8) & 0xff;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || c >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_OBJ || ctx.regs[b].obj == NULL || ctx.regs[c].kind != VAL_INT) {
                    return 0;
                }
                
                if (!array_set (ctx.regs[b].obj, (int)ctx.regs[c].i, ctx.regs[a])) {
                    return 0;
                }
                
                pc += 2;
                break;
            
            }
            
            case 0x52:  /* iget */      case 0x53:  /* iget-wide */     case 0x54:  /* iget-object */       case 0x55:  /* iget-boolean */      case 0x56:  /* iget-byte */     case 0x57:  /* iget-char */     case 0x58:  /* iget-short */ {
            
                int a;
                int b;
                
                u2 field_idx;
                Value fv;
                
                a = (insn >> 8) & 0x0f;
                b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_OBJ || ctx.regs[b].obj == NULL) {
                    return 0;
                }
                
                field_idx = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                if (!obj_get_field (ctx.regs[b].obj, field_idx, &fv)) {
                
                    const char *field_class;
                    const char *field_name;
                    
                    if (ctx.regs[b].obj->type == OBJ_STRING && dex_field_name_parts (dex, field_idx, &field_class, &field_name) && strcmp (field_name, "value") == 0) {
                    
                        Obj *array;
                        
                        size_t len;
                        size_t i;
                        
                        len = strlen (ctx.regs[b].obj->u.string.text);
                        
                        if ((array = obj_new_array ((int) len)) == NULL) {
                            return 0;
                        }
                        
                        for (i = 0; i < len; i++) {
                            array->array_elems[i] = value_int ((unsigned char) ctx.regs[b].obj->u.string.text[i]);
                        }
                        
                        fv = value_obj (array);
                        
                        if (!obj_set_field (ctx.regs[b].obj, field_idx, fv)) {
                            return 0;
                        }
                    
                    } else {
                    
                        fprintf (stderr, "iget miss: obj=%p field_idx=%u\n", (void *) ctx.regs[b].obj, (unsigned) field_idx);
                        return 0;
                    
                    }
                
                }
                
                if (op == 0x55) {
                    fv = value_int (fv.i ? 1 : 0);
                } else if (op == 0x56) {
                    fv = value_int ((signed char) (fv.i & 0xff));
                } else if (op == 0x57) {
                    fv = value_int (fv.i & 0xffff);
                } else if (op == 0x58) {
                    fv = value_int ((short) (fv.i & 0xffff));
                }
                
                set_reg_value (&ctx, a, fv);
                
                pc += 2;
                break;
            
            }
            
            case 0x59:  /* iput */      case 0x5a:  /* iput-wide */     case 0x5b:  /* iput-object */       case 0x5c:  /* iput-boolean */      case 0x5d:  /* iput-byte */     case 0x5e:  /* iput-char */     case 0x5f:  /* iput-short */ {
            
                int a;
                int b;
                
                u2 field_idx;
                Value v;
                
                a = (insn >> 8) & 0x0f;
                b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                field_idx = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                if (ctx.regs[b].kind != VAL_OBJ || ctx.regs[b].obj == NULL) {
                    return 0;
                }
                
                v = ctx.regs[a];
                
                if (op == 0x59 && v.kind == VAL_DOUBLE) {
                    v = value_int ((long)v.d);
                } else if (op == 0x5c) {
                    v = value_int ((v.kind == VAL_DOUBLE) ? (v.d != 0.0) : (v.i ? 1 : 0));
                } else if (op == 0x5d) {
                
                    long n = (v.kind == VAL_DOUBLE) ? (long) v.d : v.i;
                    v = value_int ((signed char) (n & 0xff));
                
                } else if (op == 0x5e) {
                
                    long n = (v.kind == VAL_DOUBLE) ? (long) v.d : v.i;
                    v = value_int (n & 0xffff);
                
                } else if (op == 0x5f) {
                
                    long n = (v.kind == VAL_DOUBLE) ? (long) v.d : v.i;
                    v = value_int ((short) (n & 0xffff));
                
                }
                
                if (!obj_set_field (ctx.regs[b].obj, field_idx, v)) {
                    return 0;
                }
                
                pc += 2;
                break;
            
            }
            
            case 0x60:      case 0x61:      case 0x62:      case 0x63:      case 0x64:      case 0x65:      case 0x66: {
            
                int a;
                
                u2 field_idx;
                Value v;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                field_idx = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                if (!get_static_object_field (dex, field_idx, &v)) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, v);
                
                pc += 2;
                break;
            
            }
            
            case 0x67:      case 0x68:      case 0x69:      case 0x6a:      case 0x6b:      case 0x6c:      case 0x6d: {
            
                int a;
                u2 field_idx;
                
                a = (insn >> 8) & 0xff;
                
                if (a >= ctx.reg_count || pc + 1 >= insns_size) {
                    return 0;
                }
                
                field_idx = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                {
                
                    Value v = ctx.regs[a];
                    
                    if (op == 0x67 && v.kind == VAL_DOUBLE) {
                        v = value_int ((long) v.d);
                    } else if (op == 0x6a) {
                        v = value_int ((v.kind == VAL_DOUBLE) ? (v.d != 0.0) : (v.i ? 1 : 0));
                    } else if (op == 0x6b) {
                    
                        long n = (v.kind == VAL_DOUBLE) ? (long) v.d : v.i;
                        v = value_int ((signed char) (n & 0xff));
                    
                    } else if (op == 0x6c) {
                    
                        long n = (v.kind == VAL_DOUBLE) ? (long) v.d : v.i;
                        v = value_int (n & 0xffff);
                    
                    } else if (op == 0x6d) {
                    
                        long n = (v.kind == VAL_DOUBLE) ? (long) v.d : v.i;
                        v = value_int ((short) (n & 0xffff));
                    
                    }
                    
                    if (!set_static_field_value (dex, field_idx, v)) {
                        return 0;
                    }
                
                }
                
                pc += 2;
                break;
            
            }
            
            case 0x6e:      case 0x6f:      case 0x70:      case 0x71:      case 0x74: {
            
                Value args[16];
                int argc;
                
                u4 target_method;
                Value call_ret = value_int (0);
                
                int is_range = (op == 0x74);
                int has_this = (op != 0x71);
                
                if (!gather_invoke_args (dex, &ctx, insns_off, pc, is_range, has_this, args, &argc, &target_method)) {
                    return 0;
                }
                
                {
                
                    const char *tcls, *tname;
                    u4 target_code_off;
                    
                    if (!dex_method_name_parts (dex, target_method, &tcls, &tname, NULL)) {
                        return 0;
                    }

                    /**
                     * A method implemented in the DEX must take precedence over
                     * an API builtin with the same name.  In particular, a
                     * compiled java.lang.Math.random() must not be replaced by
                     * the host C rand() builtin.
                     */
                    if (dex_get_code_off_by_method_idx (dex, target_method, &target_code_off) && target_code_off != 0) {
                    
                        if (!exec_method_by_code_args (dex, tcls, tname, target_method, target_code_off, trace, args, argc, &call_ret)) {
                        
                            u4 handler;
                            
                            if (g_pending_exception != NULL && g_pending_exception->class_desc != NULL && dex_find_exception_handler (dex, code_off, pc, g_pending_exception->class_desc, &handler)) {
                            
                                pc = handler;
                                break;
                            
                            }
                            
                            return 0;
                        
                        }
                    
                    } else if (call_builtin (dex, target_method, args, argc, &call_ret)) {
                        /* Handled by the API compatibility layer. */
                    }
                    
                    /* 2. Skip System/Object calls */
                    else if ((strncmp (tcls, "Landroid/", 9) == 0) || (strncmp (tcls, "Ljava/", 6) == 0)) {
                        call_ret = value_int (0);
                    }
                    
                    /* 3. Handle Cross-DEX calls (Dynamic Loading) */
                    else {
                    
                        const DexFile *found_dex = find_dex_for_class (tcls);
                        
                        if (!found_dex && try_dynamic_load (tcls)) {
                            found_dex = &g_loaded_dexes[g_dex_count - 1];
                        }
                        
                        if (found_dex) {
                        
                            u4 f_method_idx, f_code_off;
                            
                            if (dex_find_method_code (found_dex, tcls, tname, &f_method_idx, &f_code_off)) {
                            
                                if (!exec_method_by_code_args (found_dex, tcls, tname, f_method_idx, f_code_off, trace, args, argc, &call_ret)) {
                                
                                    u4 handler;
                                    
                                    if (g_pending_exception != NULL && g_pending_exception->class_desc != NULL && dex_find_exception_handler (dex, code_off, pc, g_pending_exception->class_desc, &handler)) {
                                    
                                        pc = handler;
                                        break;
                                    
                                    }
                                    
                                    return 0;
                                
                                }
                            
                            } else {
                                return 0;
                            }
                        
                        } else {
                        
                            fprintf (stderr, "[VM] Failed to resolve class: %s\n", tcls);
                            return 0;
                        
                        }
                    
                    }
                
                }
                
                ctx.last_result = call_ret;
                
                pc += 3;
                break;
            
            }
            
            case 0x90:      case 0x91:      case 0x92:      case 0x93:      case 0x94:      case 0x95:      case 0x96:      case 0x97:      case 0x98:      case 0x99:      case 0x9a: {
            
                int a = (insn >> 8) & 0xff;
                u2 pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                int b = pair & 0xff;
                int c = (pair >> 8) & 0xff;
                
                if (pc + 1 >= insns_size || a >= ctx.reg_count || b >= ctx.reg_count || c >= ctx.reg_count || ctx.regs[b].kind != VAL_INT || ctx.regs[c].kind != VAL_INT) {
                    return 0;
                }
                
                if ((op == 0x93 || op == 0x94) && ctx.regs[c].i == 0) {
                    return 0;
                }
                
                if (op == 0x90) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i + ctx.regs[c].i));
                }
                
                if (op == 0x91) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i - ctx.regs[c].i));
                }
                
                if (op == 0x92) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i * ctx.regs[c].i));
                }
                
                if (op == 0x93) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i / ctx.regs[c].i));
                }
                
                if (op == 0x94) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i % ctx.regs[c].i));
                }
                
                if (op == 0x95) {
                    set_reg_value (&ctx, a, value_int ((s4) ctx.regs[b].i & (s4) ctx.regs[c].i));
                }
                
                if (op == 0x96) {
                    set_reg_value (&ctx, a, value_int ((s4) ctx.regs[b].i | (s4) ctx.regs[c].i));
                }
                
                if (op == 0x97) {
                    set_reg_value (&ctx, a, value_int ((s4) ctx.regs[b].i ^ (s4) ctx.regs[c].i));
                }
                
                if (op == 0x98) {
                    set_reg_value (&ctx, a, value_int ((s4) ((u4) ctx.regs[b].i << ((u4) ctx.regs[c].i & 31U))));
                }
                
                if (op == 0x99) {
                    set_reg_value (&ctx, a, value_int ((s4) ctx.regs[b].i >> ((u4) ctx.regs[c].i & 31U)));
                }
                
                if (op == 0x9a) {
                    set_reg_value (&ctx, a, value_int ((s4) ((u4) ctx.regs[b].i >> ((u4) ctx.regs[c].i & 31U))));
                }
                
                pc += 2;
                break;
            
            }
            
            case 0x9b:      case 0x9c:      case 0x9d:      case 0x9e:      case 0x9f:      case 0xa0:      case 0xa1:      case 0xa2:      case 0xa3:      case 0xa4:      case 0xa5: {
            
                int a = (insn >> 8) & 0xff;
                u2 pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                int b = pair & 0xff;
                int c = (pair >> 8) & 0xff;
                
                long long x, y, r = 0;
                unsigned int shift;
                
                if (pc + 1 >= insns_size || a + 1 >= ctx.reg_count || b + 1 >= ctx.reg_count || c >= ctx.reg_count || ctx.regs[b].kind != VAL_WIDE) {
                    return 0;
                }
                
                x = (long long) ctx.regs[b].wide;
                
                if (op >= 0xa3) {
                
                    if (ctx.regs[c].kind != VAL_INT) {
                        return 0;
                    }
                    
                    shift = (unsigned int) ctx.regs[c].i & 63U;
                    
                    if (op == 0xa3) {
                        r = (long long) ((unsigned long long) x << shift);
                    }
                    
                    if (op == 0xa4) {
                        r = x >> shift;
                    }
                    
                    if (op == 0xa5) {
                        r = (long long) ((unsigned long long) x >> shift);
                    }
                
                } else {
                
                    if (c + 1 >= ctx.reg_count || ctx.regs[c].kind != VAL_WIDE) {
                        return 0;
                    }
                    
                    y = (long long) ctx.regs[c].wide;
                    
                    if ((op == 0x9e || op == 0x9f) && y == 0) {
                        return 0;
                    }
                    
                    if (op == 0x9b) {
                        r = x + y;
                    }
                    
                    if (op == 0x9c) {
                        r = x - y;
                    }
                    
                    if (op == 0x9d) {
                        r = x * y;
                    }
                    
                    if (op == 0x9e) {
                        r = x / y;
                    }
                    
                    if (op == 0x9f) {
                        r = x % y;
                    }
                    
                    if (op == 0xa0) {
                        r = x & y;
                    }
                    
                    if (op == 0xa1) {
                        r = x | y;
                    }
                    
                    if (op == 0xa2) {
                        r = x ^ y;
                    }
                
                }
                
                set_reg_value (&ctx, a, value_wide ((unsigned long long) r));
                
                pc += 2;
                break;
            
            }
            
            case 0xa6:      case 0xa7:      case 0xa8:      case 0xa9:      case 0xaa: {
            
                int a = (insn >> 8) & 0xff;
                u2 pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                int b = pair & 0xff;
                int c = (pair >> 8) & 0xff;
                
                float x, y, r = 0.0f;

                if (pc + 1 >= insns_size || a >= ctx.reg_count || b >= ctx.reg_count || c >= ctx.reg_count || !value_get_float (ctx.regs[b], &x) || !value_get_float (ctx.regs[c], &y)) {
                    return 0;
                }
                
                if (op == 0xa6) {
                    r = x + y;
                }
                
                if (op == 0xa7) {
                    r = x - y;
                }
                
                if (op == 0xa8) {
                    r = x * y;
                }
                
                if (op == 0xa9) {
                    r = x / y;
                }
                
                if (op == 0xaa) {
                    r = fmodf (x, y);
                }
                
                set_reg_value (&ctx, a, value_float (r));
                
                pc += 2;
                break;
            
            }
            
            case 0xab:      case 0xac:      case 0xad:      case 0xae:      case 0xaf: {
            
                int a = (insn >> 8) & 0xff;
                u2 pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                int b = pair & 0xff;
                int c = (pair >> 8) & 0xff;
                
                double x, y, r = 0.0;
                
                if (pc + 1 >= insns_size || a + 1 >= ctx.reg_count || b + 1 >= ctx.reg_count || c + 1 >= ctx.reg_count || !value_get_double (ctx.regs[b], &x) || !value_get_double (ctx.regs[c], &y)) {
                    return 0;
                }
                
                if (op == 0xab) {
                    r = x + y;
                }
                
                if (op == 0xac) {
                    r = x - y;
                }
                
                if (op == 0xad) {
                    r = x * y;
                }
                
                if (op == 0xae) {
                    r = x / y;
                }
                
                if (op == 0xaf) {
                    r = fmod (x, y);
                }
                
                set_reg_value (&ctx, a, value_double (r));
                
                pc += 2;
                break;
            
            
            }
            
            case 0xb0:      case 0xb1:      case 0xb2:      case 0xb3:      case 0xb4:      case 0xb5:      case 0xb6:      case 0xb7:      case 0xb8:      case 0xb9:      case 0xba: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || ctx.regs[a].kind != VAL_INT || ctx.regs[b].kind != VAL_INT) {
                    return 0;
                }
                
                if ((op == 0xb3 || op == 0xb4) && ctx.regs[b].i == 0) {
                    return 0;
                }
                
                if (op == 0xb0) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[a].i + ctx.regs[b].i));
                }
                
                if (op == 0xb1) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[a].i - ctx.regs[b].i));
                }
                
                if (op == 0xb2) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[a].i * ctx.regs[b].i));
                }
                
                if (op == 0xb3) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[a].i / ctx.regs[b].i));
                }
                
                if (op == 0xb4) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[a].i % ctx.regs[b].i));
                }
                
                if (op == 0xb5) {
                    set_reg_value (&ctx, a, value_int ((s4) ctx.regs[a].i & (s4) ctx.regs[b].i));
                }
                
                if (op == 0xb6) {
                    set_reg_value (&ctx, a, value_int ((s4) ctx.regs[a].i | (s4) ctx.regs[b].i));
                }
                
                if (op == 0xb7) {
                    set_reg_value (&ctx, a, value_int ((s4) ctx.regs[a].i ^ (s4) ctx.regs[b].i));
                }
                
                if (op == 0xb8) {
                    set_reg_value (&ctx, a, value_int ((s4) ((u4) ctx.regs[a].i << ((u4) ctx.regs[b].i & 31U))));
                }
                
                if (op == 0xb9) {
                    set_reg_value (&ctx, a, value_int ((s4) ctx.regs[a].i >> ((u4) ctx.regs[b].i & 31U)));
                }
                
                if (op == 0xba) {
                    set_reg_value (&ctx, a, value_int ((s4) ((u4) ctx.regs[a].i >> ((u4) ctx.regs[b].i & 31U))));
                }
                
                pc += 1;
                break;
            
            }
            
            case 0xbb:      case 0xbc:      case 0xbd:      case 0xbe:      case 0xbf:      case 0xc0:      case 0xc1:      case 0xc2:      case 0xc3:      case 0xc4:      case 0xc5: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                long long x, y, r = 0;
                unsigned int shift;
                
                if (a + 1 >= ctx.reg_count || b >= ctx.reg_count || ctx.regs[a].kind != VAL_WIDE) {
                    return 0;
                }
                
                x = (long long) ctx.regs[a].wide;
                
                if (op >= 0xc3) {
                
                    if (ctx.regs[b].kind != VAL_INT) {
                        return 0;
                    }
                    
                    shift = (unsigned int) ctx.regs[b].i & 63U;
                    
                    if (op == 0xc3) {
                        r = (long long) ((unsigned long long) x << shift);
                    }
                    
                    if (op == 0xc4) {
                        r = x >> shift;
                    }
                    
                    if (op == 0xc5) {
                        r = (long long) ((unsigned long long) x >> shift);
                    }
                
                } else {
                
                    if (b + 1 >= ctx.reg_count || ctx.regs[b].kind != VAL_WIDE) {
                        return 0;
                    }
                    
                    y = (long long) ctx.regs[b].wide;
                    
                    if ((op == 0xbe || op == 0xbf) && y == 0) {
                        return 0;
                    }
                    
                    if (op == 0xbb) {
                        r = x + y;
                    }
                    
                    if (op == 0xbc) {
                        r = x - y;
                    }
                    
                    if (op == 0xbd) {
                        r = x * y;
                    }
                    
                    if (op == 0xbe) {
                        r = x / y;
                    }
                    
                    if (op == 0xbf) {
                        r = x % y;
                    }
                    
                    if (op == 0xc0) {
                        r = x & y;
                    }
                    
                    if (op == 0xc1) {
                        r = x | y;
                    }
                    
                    if (op == 0xc2) {
                        r = x ^ y;
                    }
                
                }
                
                set_reg_value (&ctx, a, value_wide ((unsigned long long) r));
                
                pc += 1;
                break;
            
            }
            
            case 0xc6:      case 0xc7:      case 0xc8:      case 0xc9:      case 0xca: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                float x, y, r = 0.0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count || !value_get_float (ctx.regs[a], &x) || !value_get_float (ctx.regs[b], &y)) {
                    return 0;
                }
                
                if (op == 0xc6) {
                    r = x + y;
                }
                
                if (op == 0xc7) {
                    r = x - y;
                }
                
                if (op == 0xc8) {
                    r = x * y;
                }
                
                if (op == 0xc9) {
                    r = x / y;
                }
                
                if (op == 0xca) {
                    r = fmodf (x, y);
                }
                
                set_reg_value (&ctx, a, value_float (r));
                
                pc += 1;
                break;
            
            }
            
            case 0xcb:      case 0xcc:      case 0xcd:      case 0xce:      case 0xcf: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                double x, y, r = 0.0;
                
                if (a + 1 >= ctx.reg_count || b + 1 >= ctx.reg_count || !value_get_double (ctx.regs[a], &x) || !value_get_double (ctx.regs[b], &y)) {
                    return 0;
                }
                
                if (op == 0xcb) {
                    r = x + y;
                }
                
                if (op == 0xcc) {
                    r = x - y;
                }
                
                if (op == 0xcd) {
                    r = x * y;
                }
                
                if (op == 0xce) {
                    r = x / y;
                }
                
                if (op == 0xcf) {
                    r = fmod (x, y);
                }
                
                set_reg_value (&ctx, a, value_double (r));
                
                pc += 1;
                break;
            
            }
            
            case 0xd8:      case 0xd9:      case 0xda:      case 0xdb:      case 0xdc: {
            
                int a;
                u2 pair;
                
                int b;
                s4 lit8;
                
                a = (insn >> 8) & 0xff;
                
                if (pc + 1 >= insns_size) {
                    return 0;
                }
                
                pair = dex_read_u2 (dex, insns_off + (pc + 1) * 2);
                
                b = pair & 0xff;
                lit8 = sx8 ((pair >> 8) & 0xff);
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_INT) {
                    return 0;
                }
                
                if ((op == 0xdb || op == 0xdc) && lit8 == 0) {
                    return 0;
                }
                
                if (op == 0xd8) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i + lit8));
                }
                
                if (op == 0xd9) {
                    set_reg_value (&ctx, a, value_int (lit8 - ctx.regs[b].i));
                }
                
                if (op == 0xda) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i * lit8));
                }
                
                if (op == 0xdb) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i / lit8));
                }
                
                if (op == 0xdc) {
                    set_reg_value (&ctx, a, value_int (ctx.regs[b].i % lit8));
                }
                
                pc += 2;
                break;
            
            }
            
            case 0x81: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_INT) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_wide ((unsigned long long) (long long) (s4) ctx.regs[b].i));
                
                pc += 1;
                break;
            
            }
            
            case 0x83: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_INT) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_double ((double) ctx.regs[b].i));
                
                pc += 1;
                break;
            
            }
            
            case 0x84: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_WIDE) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_int ((long) (s4) (u4) ctx.regs[b].wide));
                
                pc += 1;
                break;
            
            }
            
            case 0x86: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_WIDE) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_double ((double) (long long) ctx.regs[b].wide));
                
                pc += 1;
                break;
            
            }
            
            case 0x8a: {
            
                int a = (insn >> 8) & 0x0f;
                int b = (insn >> 12) & 0x0f;
                
                if (a >= ctx.reg_count || b >= ctx.reg_count) {
                    return 0;
                }
                
                if (ctx.regs[b].kind != VAL_DOUBLE) {
                    return 0;
                }
                
                set_reg_value (&ctx, a, value_int ((long) ctx.regs[b].d));
                
                pc += 1;
                break;
            
            }
            
            default:
            
                fprintf (stderr, "unsupported opcode 0x%02x in %s->%s at pc=%u\n", (unsigned) op, class_desc, method_name, pc);
                return 0;
        
        }
    
    }
    
    return 0;

}

int exec_method_by_code (const DexFile *dex, const char *class_desc, const char *method_name, u4 method_idx, u4 code_off, int trace, Value *ret) {
    return exec_method_by_code_args (dex, class_desc, method_name, method_idx, code_off, trace, NULL, 0, ret);
}

int exec_method (const DexFile *dex, const char *class_desc, const char *method_name, int trace, Value *ret) {

    u4 method_idx;
    u4 code_off;
    
    if (dex == NULL || class_desc == NULL || method_name == NULL || ret == NULL) {
        return 0;
    }
    
    if (!dex_find_method_code (dex, class_desc, method_name, &method_idx, &code_off)) {
    
        fprintf (stderr, "method not found: %s %s\n", class_desc, method_name);
        return 0;
    
    }
    
    if (code_off == 0) {
    
        fprintf (stderr, "method has no code: %s %s\n", class_desc, method_name);
        return 0;
    
    }
    
    return exec_method_by_code (dex, class_desc, method_name, method_idx, code_off, trace, ret);

}

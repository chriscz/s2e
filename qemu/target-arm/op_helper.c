/*
 *  ARM helper routines
 *
 *  Copyright (c) 2005-2007 CodeSourcery, LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "exec.h"
#include "helpers.h"
#include <assert.h>

#define SIGNBIT (uint32_t)0x80000000
#define SIGNBIT64 ((uint64_t)1 << 63)

#ifdef S2E_LLVM_LIB
void klee_make_symbolic(void *addr, unsigned nbytes, const char *name);
uint8_t klee_int8(const char *name);
uint16_t klee_int16(const char *name);
uint32_t klee_int32(const char *name);
void uint32_to_string(uint32_t n, char *str);
void trace_port(char *buf, const char *prefix, uint32_t port, uint32_t pc);

uint8_t klee_int8(const char *name) {
    uint8_t ret;
    klee_make_symbolic(&ret, sizeof(ret), name);
    return ret;
}

uint16_t klee_int16(const char *name) {
    uint16_t ret;
    klee_make_symbolic(&ret, sizeof(ret), name);
    return ret;
}

uint32_t klee_int32(const char *name) {
    uint32_t ret;
    klee_make_symbolic(&ret, sizeof(ret), name);
    return ret;
}

//Helpers to avoid relying on sprintf that does not work properly
static char hextable[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
'c', 'd', 'e', 'f'};
void uint32_to_string(uint32_t n, char *str)
{
  str[0] = hextable[(n >> 28)];
  str[1] = hextable[((n >> 24) & 0xF)];
  str[2] = hextable[((n >> 20) & 0xF)];
  str[3] = hextable[((n >> 16) & 0xF)];
  str[4] = hextable[((n >> 12) & 0xF)];
  str[5] = hextable[((n >> 8) & 0xF)];
  str[6] = hextable[((n >> 4) & 0xF)];
  str[7] = hextable[((n >> 0) & 0xF)];
}

void trace_port(char *buf, const char *prefix, uint32_t port, uint32_t pc)
{
    while(*prefix) {
        *buf = *prefix;
        ++buf; ++prefix;
    }

    uint32_to_string(port, buf);
    buf+=8;
    *buf = '_';
    buf++;
    uint32_to_string(pc, buf);
    buf+=8;
    *buf = 0;
}

#endif

#ifndef S2E_LLVM_LIB
#ifdef CONFIG_S2E
struct CPUARMState* env = 0;
#endif
#endif


uint64_t helper_do_interrupt(void);
uint64_t helper_do_interrupt(void)
{
    do_interrupt(env);
    return 0;
}

void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit();
}

/* thread support */

static spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void cpu_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void cpu_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

uint32_t HELPER(neon_tbl)(uint32_t ireg, uint32_t def,
                          uint32_t rn, uint32_t maxindex)
{
    uint32_t val;
    uint32_t tmp;
    int index;
    int shift;
    uint64_t *table;
    table = (uint64_t *)&env->vfp.regs[rn];
    val = 0;
    for (shift = 0; shift < 32; shift += 8) {
        index = (ireg >> shift) & 0xff;
        if (index < maxindex) {
            tmp = (table[index >> 3] >> ((index & 7) << 3)) & 0xff;
            val |= tmp << shift;
        } else {
            val |= def & (0xff << shift);
        }
    }
    return val;
}

#if !defined(CONFIG_USER_ONLY)

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

#if defined(CONFIG_S2E) && !defined(S2E_LLVM_LIB)
#undef MMUSUFFIX
#define MMUSUFFIX _mmu_s2e_trace
#define _raw _raw_s2e_trace

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

#undef _raw
#endif

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill (target_ulong addr, target_ulong page_addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    if(env != cpu_single_env)
        env = cpu_single_env;

#ifdef CONFIG_S2E
    s2e_on_tlb_miss(g_s2e, g_s2e_state, addr, is_write);
    ret = cpu_arm_handle_mmu_fault(env, page_addr,
                                   is_write, mmu_idx, 1);
#else
    ret = cpu_arm_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
#endif

    if (unlikely(ret)) {

#ifdef CONFIG_S2E
        /* In S2E we pass page address instead of addr to cpu_arm_handle_mmu_fault,
           since the latter can be symbolic while the former is always concrete.
           To compensate, we reset fault address here. */
        if(env->exception_index == EXCP_PREFETCH_ABORT || env->exception_index == EXCP_DATA_ABORT) {
            assert(1 && "handle coprocessor exception properly");
        }
        if(use_icount)
            cpu_restore_icount(env);
#endif

        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (uintptr_t)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }

#ifdef CONFIG_S2E
s2e_on_page_fault(g_s2e, g_s2e_state, addr, is_write);
#endif

        raise_exception(env->exception_index);
    }
    if(saved_env != env)
    	env = saved_env;
}
#endif

#ifdef CONFIG_S2E
#include <s2e/s2e_qemu.h>
#endif

/* FIXME: Pass an axplicit pointer to QF to CPUState, and move saturating
   instructions into helper.c  */
uint32_t HELPER(add_setq)(uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (((res ^ a) & SIGNBIT) && !((a ^ b) & SIGNBIT))
        env->QF = 1;
    return res;
}

uint32_t HELPER(add_saturate)(uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (((res ^ a) & SIGNBIT) && !((a ^ b) & SIGNBIT)) {
        env->QF = 1;
        res = ~(((int32_t)a >> 31) ^ SIGNBIT);
    }
    return res;
}

uint32_t HELPER(sub_saturate)(uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (((res ^ a) & SIGNBIT) && ((a ^ b) & SIGNBIT)) {
        env->QF = 1;
        res = ~(((int32_t)a >> 31) ^ SIGNBIT);
    }
    return res;
}

uint32_t HELPER(double_saturate)(int32_t val)
{
    uint32_t res;
    if (val >= 0x40000000) {
        res = ~SIGNBIT;
        env->QF = 1;
    } else if (val <= (int32_t)0xc0000000) {
        res = SIGNBIT;
        env->QF = 1;
    } else {
        res = val << 1;
    }
    return res;
}

uint32_t HELPER(add_usaturate)(uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (res < a) {
        env->QF = 1;
        res = ~0;
    }
    return res;
}

uint32_t HELPER(sub_usaturate)(uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (res > a) {
        env->QF = 1;
        res = 0;
    }
    return res;
}

/* Signed saturation.  */
static inline uint32_t do_ssat(int32_t val, int shift)
{
    int32_t top;
    uint32_t mask;

    top = val >> shift;
    mask = (1u << shift) - 1;
    if (top > 0) {
        env->QF = 1;
        return mask;
    } else if (top < -1) {
        env->QF = 1;
        return ~mask;
    }
    return val;
}

/* Unsigned saturation.  */
static inline uint32_t do_usat(int32_t val, int shift)
{
    uint32_t max;

    max = (1u << shift) - 1;
    if (val < 0) {
        env->QF = 1;
        return 0;
    } else if (val > max) {
        env->QF = 1;
        return max;
    }
    return val;
}

/* Signed saturate.  */
uint32_t HELPER(ssat)(uint32_t x, uint32_t shift)
{
    return do_ssat(x, shift);
}

/* Dual halfword signed saturate.  */
uint32_t HELPER(ssat16)(uint32_t x, uint32_t shift)
{
    uint32_t res;

    res = (uint16_t)do_ssat((int16_t)x, shift);
    res |= do_ssat(((int32_t)x) >> 16, shift) << 16;
    return res;
}

/* Unsigned saturate.  */
uint32_t HELPER(usat)(uint32_t x, uint32_t shift)
{
    return do_usat(x, shift);
}

/* Dual halfword unsigned saturate.  */
uint32_t HELPER(usat16)(uint32_t x, uint32_t shift)
{
    uint32_t res;

    res = (uint16_t)do_usat((int16_t)x, shift);
    res |= do_usat(((int32_t)x) >> 16, shift) << 16;
    return res;
}

void HELPER(wfi)(void)
{
    env->exception_index = EXCP_HLT;
    env->halted = 1;
    cpu_loop_exit();
}

void HELPER(exception)(uint32_t excp)
{
    env->exception_index = excp;
    cpu_loop_exit();
}

uint32_t HELPER(cpsr_read)(void)
{
    return cpsr_read(env) & ~CPSR_EXEC;
}

void HELPER(cpsr_write)(uint32_t val, uint32_t mask)
{
    cpsr_write(env, val, mask);
}

/* Access to user mode registers from privileged modes.  */
uint32_t HELPER(get_user_reg)(uint32_t regno)
{
    uint32_t val;

    if (regno == 13) {
        val = RR_cpu(env,banked_r13[0]);
    } else if (regno == 14) {
        val = RR_cpu(env,banked_r14[0]);
    }else if (regno == 15) {
            	val = env->regs[regno];
    } else if (regno >= 8
               && (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        val = RR_cpu(env,usr_regs[regno - 8]);
    } else {
        val = RR_cpu(env,regs[regno]);
    }
    return val;
}

void HELPER(set_user_reg)(uint32_t regno, uint32_t val)
{
    if (regno == 13) {
        WR_cpu(env,banked_r13[0],val);
    } else if (regno == 14) {
        WR_cpu(env,banked_r14[0],val);
    } else if (regno == 15) {
    	env->regs[regno] = val;
    } else if (regno >= 8
               && (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        WR_cpu(env,usr_regs[regno - 8],val);
    } else {
        WR_cpu(env,regs[regno],val);
    }
}

/* ??? Flag setting arithmetic is awkward because we need to do comparisons.
   The only way to do that in TCG is a conditional branch, which clobbers
   all our temporaries.  For now implement these as helper functions.  */

uint32_t HELPER (add_cc)(uint32_t a, uint32_t b)
{
    uint32_t result;
    result = a + b;
    WR_cpu(env,NF,result);
    WR_cpu(env,ZF,result);
    WR_cpu(env,CF,(result < a));
    WR_cpu(env,VF,((a ^ b ^ -1) & (a ^ result)));
    return result;
}

uint32_t HELPER(adc_cc)(uint32_t a, uint32_t b)
{
    uint32_t result;
    if (!(RR_cpu(env,CF))) {
        result = a + b;
        WR_cpu(env,CF,(result < a));
    } else {
        result = a + b + 1;
        WR_cpu(env,CF,(result <= a));
    }
    WR_cpu(env,VF,((a ^ b ^ -1) & (a ^ result)));
    WR_cpu(env,NF,result);
    WR_cpu(env,ZF,result);
    return result;
}

uint32_t HELPER(sub_cc)(uint32_t a, uint32_t b)
{
    uint32_t result;
    result = a - b;
    WR_cpu(env,NF,result);
    WR_cpu(env,ZF,result);
    WR_cpu(env,CF,(a >= b));
    WR_cpu(env,VF,((a ^ b) & (a ^ result)));
    return result;
}

uint32_t HELPER(sbc_cc)(uint32_t a, uint32_t b)
{
    uint32_t result;
    if (!(RR_cpu(env,CF))) {
        result = a - b - 1;
        WR_cpu(env,CF,(a > b));
    } else {
        result = a - b;
        WR_cpu(env,CF,(a >= b));
    }
    WR_cpu(env,VF,((a ^ b) & (a ^ result)));
    WR_cpu(env,NF,result);
    WR_cpu(env,ZF,result);
    return result;
}

/* Similarly for variable shift instructions.  */

uint32_t HELPER(shl)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32)
        return 0;
    return x << shift;
}

uint32_t HELPER(shr)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32)
        return 0;
    return (uint32_t)x >> shift;
}

uint32_t HELPER(sar)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32)
        shift = 31;
    return (int32_t)x >> shift;
}

uint32_t HELPER(shl_cc)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            WR_cpu(env,CF,(x & 1));
        else
            WR_cpu(env,CF,0);
        return 0;
    } else if (shift != 0) {
        WR_cpu(env,CF,((x >> (32 - shift)) & 1));
        return x << shift;
    }
    return x;
}

uint32_t HELPER(shr_cc)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            WR_cpu(env,CF,((x >> 31) & 1));
        else
            WR_cpu(env,CF,0);
        return 0;
    } else if (shift != 0) {
        WR_cpu(env,CF,((x >> (shift - 1)) & 1));
        return x >> shift;
    }
    return x;
}

uint32_t HELPER(sar_cc)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        WR_cpu(env,CF,((x >> 31) & 1));
        return (int32_t)x >> 31;
    } else if (shift != 0) {
        WR_cpu(env,CF,((x >> (shift - 1)) & 1));
        return (int32_t)x >> shift;
    }
    return x;
}

uint32_t HELPER(ror_cc)(uint32_t x, uint32_t i)
{
    int shift1, shift;
    shift1 = i & 0xff;
    shift = shift1 & 0x1f;
    if (shift == 0) {
        if (shift1 != 0)
            WR_cpu(env,CF,((x >> 31) & 1));
        return x;
    } else {
        WR_cpu(env,CF,((x >> (shift - 1)) & 1));
        return ((uint32_t)x >> shift) | (x << (32 - shift));
    }
}

uint64_t HELPER(neon_add_saturate_s64)(uint64_t src1, uint64_t src2)
{
    uint64_t res;

    res = src1 + src2;
    if (((res ^ src1) & SIGNBIT64) && !((src1 ^ src2) & SIGNBIT64)) {
        env->QF = 1;
        res = ((int64_t)src1 >> 63) ^ ~SIGNBIT64;
    }
    return res;
}

uint64_t HELPER(neon_add_saturate_u64)(uint64_t src1, uint64_t src2)
{
    uint64_t res;

    res = src1 + src2;
    if (res < src1) {
        env->QF = 1;
        res = ~(uint64_t)0;
    }
    return res;
}

uint64_t HELPER(neon_sub_saturate_s64)(uint64_t src1, uint64_t src2)
{
    uint64_t res;

    res = src1 - src2;
    if (((res ^ src1) & SIGNBIT64) && ((src1 ^ src2) & SIGNBIT64)) {
        env->QF = 1;
        res = ((int64_t)src1 >> 63) ^ ~SIGNBIT64;
    }
    return res;
}

uint64_t HELPER(neon_sub_saturate_u64)(uint64_t src1, uint64_t src2)
{
    uint64_t res;

    if (src1 < src2) {
        env->QF = 1;
        res = 0;
    } else {
        res = src1 - src2;
    }
    return res;
}

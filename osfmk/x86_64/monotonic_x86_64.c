/*
 * Copyright (c) 2017 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <i386/cpu_data.h>
#include <i386/cpuid.h>
#include <i386/lapic.h>
#include <i386/mp.h>
#include <i386/proc_reg.h>
#include <kern/assert.h> /* static_assert, assert */
#include <kern/monotonic.h>
#include <os/overflow.h>
#include <sys/errno.h>
#include <sys/monotonic.h>
#include <x86_64/monotonic.h>
#include <kern/kpc.h>

/*
 * Sanity check the compiler.
 */

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif /* !defined(__has_builtin) */
#if !__has_builtin(__builtin_ia32_rdpmc)
#error requires __builtin_ia32_rdpmc builtin
#endif /* !__has_builtin(__builtin_ia32_rdpmc) */

#pragma mark core counters

bool mt_core_supported = false;

/*
 * PMC[0-3]_{RD,WR} allow reading and writing the fixed PMCs.
 *
 * There are separate defines for access type because the read side goes through
 * the rdpmc instruction, which has a different counter encoding than the msr
 * path.
 */
#define PMC_FIXED_RD(CTR) ((UINT64_C(1) << 30) | (CTR))
#define PMC_FIXED_WR(CTR) (MSR_IA32_PERF_FIXED_CTR0 + (CTR))
#define S3_2_C15_C0_0_RD PMC_FIXED_RD(0)
#define S3_2_C15_C0_0_WR PMC_FIXED_WR(0)
#define S3_2_C15_C1_0_RD PMC_FIXED_RD(1)
#define S3_2_C15_C1_0_WR PMC_FIXED_WR(1)
#define S3_2_C15_C2_0_RD PMC_FIXED_RD(2)
#define S3_2_C15_C2_0_WR PMC_FIXED_WR(2)
#define S3_2_C15_C3_0_RD PMC_FIXED_RD(3)
#define S3_2_C15_C3_0_WR PMC_FIXED_WR(3)

struct mt_cpu *
mt_cur_cpu(void)
{
	return &current_cpu_datap()->cpu_monotonic;
}

uint64_t
mt_core_snap(unsigned int ctr)
{
	if (!mt_core_supported || ctr >= kpc_fixed_count()) {
		return 0;
	}

	switch (ctr) {
	case 0:
		return __builtin_ia32_rdpmc(S3_2_C15_C0_0_RD);
	case 1:
		return __builtin_ia32_rdpmc(S3_2_C15_C1_0_RD);
	case 2:
		return __builtin_ia32_rdpmc(S3_2_C15_C2_0_RD);
	case 3:
		return __builtin_ia32_rdpmc(S3_2_C15_C3_0_RD);
	default:
		panic("monotonic: invalid core counter read: %u", ctr);
		__builtin_unreachable();
	}
}

void
mt_core_set_snap(unsigned int ctr, uint64_t count)
{
	if (!mt_core_supported || ctr >= kpc_fixed_count()) {
		return;
	}

	switch (ctr) {
	case 0:
		wrmsr64(S3_2_C15_C0_0_WR, count);
		break;
	case 1:
		wrmsr64(S3_2_C15_C1_0_WR, count);
		break;
	case 2:
		wrmsr64(S3_2_C15_C2_0_WR, count);
		break;
	case 3:
		wrmsr64(S3_2_C15_C3_0_WR, count);
		break;
	default:
		panic("monotonic: invalid core counter write: %u", ctr);
		__builtin_unreachable();
	}
}

/*
 * MSR_IA32_PERF_FIXED_CTR_CTRL controls which rings fixed counters are
 * enabled in and if they deliver PMIs.
 *
 * Each fixed counters has 4 bits: [0:1] controls which ring it's enabled in,
 * [2] counts all hardware threads in each logical core (we don't want this),
 * and [3] enables PMIs on overflow.
 */

/*
 * Fixed counters are enabled in all rings, so each _SINGLE definition is
 * compounded in successive nibbles, dependent upon how many fixed counters
 * are reported by cpuid.
 */
#define FIXED_CTR_CTRL_INIT_SINGLE (0x8)
#define FIXED_CTR_CTRL_ENABLE_SINGLE (0x3)

static inline void
mt_fixed_counter_set_ctrl_mask(uint8_t ctrlbits)
{
	uint64_t mask = 0;

	assert((ctrlbits & 0xF0) == 0);

	for (uint32_t i = 0; i < kpc_fixed_count(); i++) {
		mask |= ctrlbits << (4 * i);
	}

	wrmsr64(MSR_IA32_PERF_FIXED_CTR_CTRL, mask);
}


/*
 * GLOBAL_CTRL controls which counters are enabled -- the high 32-bits control
 * the fixed counters and the lower half is for the configurable counters.
 */

#define GLOBAL_CTRL 0x38f

/*
 * Fixed counters are always enabled -- and there are three of them (except on
 * Icelake, where there are four).
 */
#define GLOBAL_CTRL_FIXED_EN_3FIXED (((UINT64_C(1) << 3) - 1) << 32)
#define GLOBAL_CTRL_FIXED_EN_4FIXED (((UINT64_C(1) << 4) - 1) << 32)

/*
 * GLOBAL_STATUS reports the state of counters, like those that have overflowed.
 */
#define GLOBAL_STATUS 0x38e

#define CTR_MAX ((UINT64_C(1) << 48) - 1)
#define CTR_FIX_POS(CTR) ((UINT64_C(1) << (CTR)) << 32)

#define GLOBAL_OVF 0x390

static void mt_check_for_pmi(struct mt_cpu *mtc, x86_saved_state_t *state);

static void
enable_counters(void)
{
	mt_fixed_counter_set_ctrl_mask(FIXED_CTR_CTRL_INIT_SINGLE | FIXED_CTR_CTRL_ENABLE_SINGLE);

	int fixed_count = kpc_fixed_count();
	uint64_t global_en = (fixed_count == 4) ? GLOBAL_CTRL_FIXED_EN_4FIXED : GLOBAL_CTRL_FIXED_EN_3FIXED;
	if (kpc_get_running() & KPC_CLASS_CONFIGURABLE_MASK) {
		global_en |= kpc_get_configurable_pmc_mask(KPC_CLASS_CONFIGURABLE_MASK);
	}

	wrmsr64(GLOBAL_CTRL, global_en);
}

static void
disable_counters(void)
{
	wrmsr64(GLOBAL_CTRL, 0);
}

static void
core_down(cpu_data_t *cpu)
{
	if (!mt_core_supported) {
		return;
	}
	assert(ml_get_interrupts_enabled() == FALSE);
	struct mt_cpu *mtc = &cpu->cpu_monotonic;

	disable_counters();
	mt_mtc_update_fixed_counts(mtc, NULL, NULL);
	mtc->mtc_active = false;
}

static void
core_up(cpu_data_t *cpu)
{
	struct mt_cpu *mtc;

	if (!mt_core_supported) {
		return;
	}

	assert(ml_get_interrupts_enabled() == FALSE);

	mtc = &cpu->cpu_monotonic;

	for (uint32_t i = 0; i < kpc_fixed_count(); i++) {
		mt_core_set_snap(i, mtc->mtc_snaps[i]);
	}
	enable_counters();
	mtc->mtc_active = true;
}

void
mt_cpu_down(cpu_data_t *cpu)
{
	core_down(cpu);
}

void
mt_cpu_up(cpu_data_t *cpu)
{
	boolean_t intrs_en;
	intrs_en = ml_set_interrupts_enabled(FALSE);
	core_up(cpu);
	ml_set_interrupts_enabled(intrs_en);
}

uint64_t
mt_count_pmis(void)
{
	uint64_t npmis = 0;
	for (unsigned int i = 0; i < real_ncpus; i++) {
		cpu_data_t *cpu = cpu_data_ptr[i];
		npmis += cpu->cpu_monotonic.mtc_npmis;
	}
	return npmis;
}

static void
mt_check_for_pmi(struct mt_cpu *mtc, x86_saved_state_t *state)
{
	uint64_t status = rdmsr64(GLOBAL_STATUS);

	mtc->mtc_npmis += 1;

	if (mtc->mtc_active) {
		disable_counters();
	}
	for (uint32_t i = 0; i < kpc_fixed_count(); i++) {
		if (status & CTR_FIX_POS(i)) {
			uint64_t prior = CTR_MAX - mtc->mtc_snaps[i];
			assert(prior <= CTR_MAX);
			prior += 1; /* wrapped */

			uint64_t delta = mt_mtc_update_count(mtc, i);
			mtc->mtc_counts[i] += delta;

			if (mt_microstackshots && mt_microstackshot_ctr == i) {
				bool user_mode = false;
				if (state) {
					x86_saved_state64_t *state64 = saved_state64(state);
					user_mode = (state64->isf.cs & 0x3) != 0;
				}
				KDBG_RELEASE(KDBG_EVENTID(DBG_MONOTONIC, DBG_MT_DEBUG, 1),
				    mt_microstackshot_ctr, user_mode);
				mt_microstackshot_pmi_handler(user_mode, mt_microstackshot_ctx);
			} else if (mt_debug) {
				KDBG(KDBG_EVENTID(DBG_MONOTONIC, DBG_MT_DEBUG, 2),
				    mt_microstackshot_ctr, i);
			}

			mtc->mtc_snaps[i] = mt_core_reset_values[i];
			mt_core_set_snap(i, mt_core_reset_values[i]);
		}
	}

	/* if any of the configurable counters overflowed, tell kpc */
	if (status & ((UINT64_C(1) << 4) - 1)) {
		extern void kpc_pmi_handler(void);
		kpc_pmi_handler();
	}

	if (mtc->mtc_active) {
		enable_counters();
	}
}

static int
mt_pmi_x86_64(x86_saved_state_t *state)
{
	assert(ml_get_interrupts_enabled() == FALSE);
	mt_check_for_pmi(mt_cur_cpu(), state);
	return 0;
}

void
mt_ownership_change(bool __unused available)
{
}

static void
mt_microstackshot_start_remote(__unused void *arg)
{
	struct mt_cpu *mtc = mt_cur_cpu();

	mt_fixed_counter_set_ctrl_mask(FIXED_CTR_CTRL_INIT_SINGLE);

	for (uint32_t i = 0; i < kpc_fixed_count(); i++) {
		uint64_t delta = mt_mtc_update_count(mtc, i);
		mtc->mtc_counts[i] += delta;
		mt_core_set_snap(i, mt_core_reset_values[i]);
		mtc->mtc_snaps[i] = mt_core_reset_values[i];
	}

	mt_fixed_counter_set_ctrl_mask(FIXED_CTR_CTRL_INIT_SINGLE | FIXED_CTR_CTRL_ENABLE_SINGLE);
}

int
mt_microstackshot_start_arch(uint64_t period)
{
	if (!mt_core_supported) {
		return ENOTSUP;
	}

	uint64_t reset_value = 0;
	int ovf = os_sub_overflow(CTR_MAX, period, &reset_value);
	if (ovf) {
		return ERANGE;
	}

	mt_core_reset_values[mt_microstackshot_ctr] = CTR_MAX - period;
	mp_cpus_call(CPUMASK_ALL, ASYNC, mt_microstackshot_start_remote,
	    NULL);
	return 0;
}

void
mt_early_init(void)
{
	if (PE_parse_boot_argn("-nomt_core", NULL, 0)) {
		return;
	}
	i386_cpu_info_t *info = cpuid_info();
	if (info->cpuid_arch_perf_leaf.version >= 2) {
		lapic_set_pmi_func((i386_intr_func_t)mt_pmi_x86_64);
		mt_core_supported = true;
	}
}

static int
core_init(__unused mt_device_t dev)
{
	return ENOTSUP;
}

#pragma mark common hooks

struct mt_device mt_devices[] = {
	[0] = {
		.mtd_name = "core",
		.mtd_init = core_init
	}
};

static_assert(
	(sizeof(mt_devices) / sizeof(mt_devices[0])) == MT_NDEVS,
	"MT_NDEVS macro should be same as the length of mt_devices");

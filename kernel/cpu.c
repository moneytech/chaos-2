/* ------------------------------------------------------------------------ *\
**
**  This file is part of the Chaos Kernel, and is made available under
**  the terms of the GNU General Public License version 2.
**
**  Copyright (C) 2017 - Benjamin Grange <benjamin.grange@epitech.eu>
**
\* ------------------------------------------------------------------------ */

#include <kconfig.h>
#include <kernel/cpu.h>
#include <kernel/interrupts.h>
#include <kernel/thread.h>

/*
** Implemented in each architecture.
** Kernel's Bootstrp processor boot stack.
** Will be used as the scheduler's stack.
*/
extern virtaddr_t bsp_kernel_stack_top[];
extern virtaddr_t bsp_kernel_stack_bottom[];

/* Number of CPUs on the current system. */
uint ncpu = 0;

/* The bootstrap processor. Only used at boot time. */
__section(".boot_memory")
static struct cpu __bsp;
__section(".boot_memory")
struct cpu *bsp = &__bsp;

/* An array for all possible cpus. */
struct cpu cpus[KCONFIG_MAX_CPUS] = { 0 };

/* Tells if the BSP has been remaped to it's corresponding entry within the cpu table. */
bool bsp_remapped = false;

void
cpu_remap_bsp(void)
{
	struct cpu *cpu;

	bsp_remapped = true;
	cpu = current_cpu();
	*cpu = *bsp;
	cpu->bsp = true;
	cpu->scheduler_stack = bsp_kernel_stack_bottom;
	cpu->scheduler_stack_top = bsp_kernel_stack_top;
	bsp = NULL;
}

void
cpu_push_ints(void)
{
	struct cpu *cpu;
	int_state_t state;

	cpu = current_cpu();
	push_interrupts_state(&state);
	disable_interrupts();
	if (cpu->int_count == 0)
		cpu->int_state = state;
	++cpu->int_count;
}

void
cpu_pop_ints(void)
{
	struct cpu *cpu;

	cpu = current_cpu();
#if KCONFIG_DEBUG_LOCKS
	assert(!are_interrupts_enable());
	assert(cpu->int_count);
#endif
	--cpu->int_count;
	if (cpu->int_count == 0)
		pop_interrupts_state(&cpu->int_state);
}

/*
** Returns the current cpu's index.
*/
size_t
current_cpu_id(void)
{
	return (current_cpu() - cpus);
}

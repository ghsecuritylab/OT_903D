
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

#include "proc-macros.S"

#define MAX_AREA_SIZE	32768

#define CACHE_DLINESIZE	32

#define CACHE_DSEGMENTS	16

#define CACHE_DENTRIES	64

#define CACHE_DLIMIT	32768

	.text
ENTRY(cpu_arm1022_proc_init)
	mov	pc, lr

ENTRY(cpu_arm1022_proc_fin)
	stmfd	sp!, {lr}
	mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, ip
	bl	arm1022_flush_kern_cache_all
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000 		@ ...i............
	bic	r0, r0, #0x000e 		@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ldmfd	sp!, {pc}

	.align	5
ENTRY(cpu_arm1022_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
#ifdef CONFIG_MMU
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
#endif
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x000f 		@ ............wcam
	bic	ip, ip, #0x1100 		@ ...i...s........
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0

	.align	5
ENTRY(cpu_arm1022_do_idle)
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	mov	pc, lr

/* ================================= CACHE ================================ */

	.align	5
ENTRY(arm1022_flush_user_cache_all)
	/* FALLTHROUGH */
ENTRY(arm1022_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mov	r1, #(CACHE_DSEGMENTS - 1) << 5	@ 16 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean+invalidate D index
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 5
	bcs	1b				@ segments 15 to 0
#endif
	tst	r2, #VM_EXEC
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
#endif
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

ENTRY(arm1022_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #CACHE_DLIMIT
	bhs	__flush_whole_cache

#ifndef CONFIG_CPU_DCACHE_DISABLE
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	tst	r2, #VM_EXEC
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
#endif
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

ENTRY(arm1022_coherent_kern_range)
	/* FALLTHROUGH */

ENTRY(arm1022_coherent_user_range)
	mov	ip, #0
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
#endif
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mcr	p15, 0, r0, c7, c5, 1		@ invalidate I entry
#endif
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

ENTRY(arm1022_flush_kern_dcache_area)
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	add	r1, r0, r1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

arm1022_dma_inv_range:
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	tst	r0, #CACHE_DLINESIZE - 1
	bic	r0, r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

arm1022_dma_clean_range:
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

ENTRY(arm1022_dma_flush_range)
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

ENTRY(arm1022_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	arm1022_dma_clean_range
	bcs	arm1022_dma_inv_range
	b	arm1022_dma_flush_range
ENDPROC(arm1022_dma_map_area)

ENTRY(arm1022_dma_unmap_area)
	mov	pc, lr
ENDPROC(arm1022_dma_unmap_area)

ENTRY(arm1022_cache_fns)
	.long	arm1022_flush_kern_cache_all
	.long	arm1022_flush_user_cache_all
	.long	arm1022_flush_user_cache_range
	.long	arm1022_coherent_kern_range
	.long	arm1022_coherent_user_range
	.long	arm1022_flush_kern_dcache_area
	.long	arm1022_dma_map_area
	.long	arm1022_dma_unmap_area
	.long	arm1022_dma_flush_range

	.align	5
ENTRY(cpu_arm1022_dcache_clean_area)
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mov	ip, #0
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
#endif
	mov	pc, lr

/* =============================== PageTable ============================== */

	.align	5
ENTRY(cpu_arm1022_switch_mm)
#ifdef CONFIG_MMU
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mov	r1, #(CACHE_DSEGMENTS - 1) << 5	@ 16 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean+invalidate D index
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 5
	bcs	1b				@ segments 15 to 0
#endif
	mov	r1, #0
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mcr	p15, 0, r1, c7, c5, 0		@ invalidate I cache
#endif
	mcr	p15, 0, r1, c7, c10, 4		@ drain WB
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, r1, c8, c7, 0		@ invalidate I & D TLBs
#endif
	mov	pc, lr
        
	.align	5
ENTRY(cpu_arm1022_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext
	mov	r0, r0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
#endif
#endif /* CONFIG_MMU */
	mov	pc, lr

	__INIT

	.type	__arm1022_setup, #function
__arm1022_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs on v4
#endif
	adr	r5, arm1022_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0		@ get control register v4
	bic	r0, r0, r5
	orr	r0, r0, r6
#ifdef CONFIG_CPU_CACHE_ROUND_ROBIN
	orr	r0, r0, #0x4000 		@ .R..............
#endif
	mov	pc, lr
	.size	__arm1022_setup, . - __arm1022_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * .011 1001 ..11 0101
	 * 
	 */
	.type	arm1022_crval, #object
arm1022_crval:
	crval	clear=0x00007f3f, mmuset=0x00003935, ucset=0x00001930

	__INITDATA

	.type	arm1022_processor_functions, #object
arm1022_processor_functions:
	.word	v4t_early_abort
	.word	legacy_pabort
	.word	cpu_arm1022_proc_init
	.word	cpu_arm1022_proc_fin
	.word	cpu_arm1022_reset
	.word	cpu_arm1022_do_idle
	.word	cpu_arm1022_dcache_clean_area
	.word	cpu_arm1022_switch_mm
	.word	cpu_arm1022_set_pte_ext
	.size	arm1022_processor_functions, . - arm1022_processor_functions

	.section ".rodata"

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv5te"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v5"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_arm1022_name, #object
cpu_arm1022_name:
	.asciz	"ARM1022"
	.size	cpu_arm1022_name, . - cpu_arm1022_name

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__arm1022_proc_info,#object
__arm1022_proc_info:
	.long	0x4105a220			@ ARM 1022E (v5TE)
	.long	0xff0ffff0
	.long   PMD_TYPE_SECT | \
		PMD_BIT4 | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	.long   PMD_TYPE_SECT | \
		PMD_BIT4 | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__arm1022_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB | HWCAP_EDSP
	.long	cpu_arm1022_name
	.long	arm1022_processor_functions
	.long	v4wbi_tlb_fns
	.long	v4wb_user_fns
	.long	arm1022_cache_fns
	.size	__arm1022_proc_info, . - __arm1022_proc_info

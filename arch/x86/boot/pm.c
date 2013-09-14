/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Prepare the machine for transition to protected mode.
 */

#include "boot.h"
#include <asm/segment.h>

/*
 * Invoke the realmode switch hook if present; otherwise
 * disable all interrupts.
 */
/*
**** ADVANCED BOOT LOADER HOOKS

If the boot loader runs in a particularly hostile environment (such as
LOADLIN, which runs under DOS) it may be impossible to follow the
standard memory location requirements.  Such a boot loader may use the
following hooks that, if set, are invoked by the kernel at the
appropriate time.  The use of these hooks should probably be
considered an absolutely last resort!

IMPORTANT: All the hooks are required to preserve %esp, %ebp, %esi and
%edi across invocation.

  realmode_swtch:
	A 16-bit real mode far subroutine invoked immediately before
	entering protected mode.  The default routine disables NMI, so
	your routine should probably do so, too.

  code32_start:
	A 32-bit flat-mode routine *jumped* to immediately after the
	transition to protected mode, but before the kernel is
	uncompressed.  No segments, except CS, are guaranteed to be
	set up (current kernels do, but older ones do not); you should
	set them up to BOOT_DS (0x18) yourself.

	After completing your hook, you should jump to the address
	that was in this field before your boot loader overwrote it
	(relocated, if appropriate.)

 	커널 개발자가 부트로더 개발자들을 배려? 한것임
	다만, realmode_swtch를 설정할 때는 "cli"와 "Disable NMI"를 해줘야한다. 
 */

static void realmode_switch_hook(void)
{
	if (boot_params.hdr.realmode_swtch) {
		/* lcallw: 함수 호출 */
		/* 결국 realmode_swtch을 호출하라 */
		/* %0은 realmode_swtich를 '*'는 주소를 의미한다. */
		asm volatile("lcallw *%0"
			     : : "m" (boot_params.hdr.realmode_swtch)
			     : "eax", "ebx", "ecx", "edx");
	} else {
		asm volatile("cli");
		/* NMI: None Maskable interrupt 해제*/
		/* reset 버튼, 칩셋 에러, 메모리 충돌, 과 같은 interrupt 들*/
		/* cli만 하면 NMI은 해제가 되지 않는다*/
		outb(0x80, 0x70); /* Disable NMI 0x80값(disable), 0x7F(enable), 0x70포트*/
	   	
		/* NMI  disalble 완전히 이루어 질때까지delay가 필요한거 같다.*/
		/* outb(al, 0x80); */
		io_delay();
	}
}

/*
 * Disable all interrupts at the legacy PIC.
 */
static void mask_all_interrupts(void)
{
	/* 모든 PIC interrupt들을 막는다. */
	outb(0xff, 0xa1);	/* Mask all interrupts on the secondary PIC */
	io_delay();
	outb(0xfb, 0x21);	/* Mask all but cascade on the primary PIC */
	io_delay();
}

/*
 * Reset IGNNE# if asserted in the FPU.
 */
static void reset_coprocessor(void)
{
	outb(0, 0xf0);
	io_delay();
	outb(0, 0xf1);
	io_delay();
}

/*
 * Set up the GDT
 */

struct gdt_ptr {
	u16 len;
	u32 ptr;
} __attribute__((packed));

static void setup_gdt(void)
{
	/* There are machines which are known to not boot with the GDT
	   being 8-byte unaligned.  Intel recommends 16 byte alignment. */
	static const u64 boot_gdt[] __attribute__((aligned(16))) = {
		/* CS: code, read/execute, 4 GB, base 0 */
		/* 0xc09b = P:1 DPL:0 32bit code section */
		[GDT_ENTRY_BOOT_CS] = GDT_ENTRY(0xc09b, 0, 0xfffff),		// GDT_ENTRY_BOOT_CS = 2
		/* DS: data, read/write, 4 GB, base 0 */
		[GDT_ENTRY_BOOT_DS] = GDT_ENTRY(0xc093, 0, 0xfffff),		// GDT_ENTRY_BOOT_DS = 3
		/* TSS: 32-bit tss, 104 bytes, base 4096 */
		/* We only have a TSS here to keep Intel VT happy;
		   we don't actually use it for anything. */
		[GDT_ENTRY_BOOT_TSS] = GDT_ENTRY(0x0089, 4096, 103),		// GDT_ENTRY_BOOT_TSS = 4
	};
	/* Xen HVM incorrectly stores a pointer to the gdt_ptr, instead
	   of the gdt_ptr contents.  Thus, make it static so it will
	   stay in memory, at least long enough that we switch to the
	   proper kernel GDT. */
	static struct gdt_ptr gdt;

	gdt.len = sizeof(boot_gdt)-1;			//zero base
	gdt.ptr = (u32)&boot_gdt + (ds() << 4); //DS:boot_dgt를 gdt.ptr에 저장한다.

	asm volatile("lgdtl %0" : : "m" (gdt));	//GDT 초기화
}

/*
 * Set up the IDT
 */
static void setup_idt(void)
{	
	/* Interrup Descriptor Table을 0으로 초기화 한다. */
	static const struct gdt_ptr null_idt = {0, 0};
	asm volatile("lidtl %0" : : "m" (null_idt));
}

/*
 * Actual invocation sequence
 */
void go_to_protected_mode(void)
{
	/* Hook before leaving real mode, also disables interrupts */
	/* cli, NMI disable 하는게 목적이다. */
	realmode_switch_hook();

	/* Enable the A20 gate */
	if (enable_a20()) {
		puts("A20 gate not responding, unable to boot...\n");
		die();
	}

	/* Reset coprocessor (IGNNE#) */
	reset_coprocessor();

	/* Mask all interrupts in the PIC */
	mask_all_interrupts();

	/* Actual transition to protected mode... */
	/* interrupt table 초기화 */
	setup_idt();
	/* GDT 초기화 */
	setup_gdt();

	/* code32_start: 0x100000로 32-bit kernel image가 로딩된 위치이다.*/
	/* boot_params를 파라메터로 넘겨줬다. */
	/* pmjump.S */
	protected_mode_jump(boot_params.hdr.code32_start,
			    (u32)&boot_params + (ds() << 4));
}

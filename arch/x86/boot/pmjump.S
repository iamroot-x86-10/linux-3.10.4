/* ----------------------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * The actual transition into protected mode
 */

#include <asm/boot.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>
#include <linux/linkage.h>

	.text
	.code16

/*
 * void protected_mode_jump(u32 entrypoint, u32 bootparams);
 */
/* entrypoint는 0x100000
   eax, edx, ecx 순서로 파라메터가 전달된다. 
   */
GLOBAL(protected_mode_jump)
	movl	%edx, %esi		# Pointer to boot_params table

	xorl	%ebx, %ebx		# ebx를 0으로 초기화
	movw	%cs, %bx		# code 세그먼트를 bx에 저장
	shll	$4, %ebx		# ebx = ebx << 4
	addl	%ebx, 2f		# in_pm32 = CS:in_pm32로 저장
	jmp	1f			# Short jump to serialize on 386/486
1:

	movw	$__BOOT_DS, %cx	#cx = 24(GDT_ENTRY_BOOT_DS의위치)
	movw	$__BOOT_TSS, %di #di = 32(GDT_ENTRY_BOOT_TSS의 위치)

	movl	%cr0, %edx		# cr0레지스터의 최하위 1bit를 0x1과 or 연산을 하면
							# protected mode로활성화 된다. 
	orb	$X86_CR0_PE, %dl	# Protected mode
							#define X86_CR0_PE	0x00000001 /* Protection Enable */
	movl	%edx, %cr0		

	#jmp $+2
	#nop
	#nop     만들면서 배우는 OS커널에는 있는데 여기는 없다?? 왜그럴까?? TODO

	# Transition to 32-bit mode
	
	/* jmp __BOOT_CS:in_pm32 */
	/* 66ea가 32bit jmp op code */
	.byte	0x66, 0xea		# ljmpl opcode
2:	.long	in_pm32			# offset CS:in_pm32
	.word	__BOOT_CS		# segment
ENDPROC(protected_mode_jump)

	.code32
	.section ".text32","ax"
GLOBAL(in_pm32)
	#ds ~ ss까지 GTD의DS세그먼트로 초기화 한다.*/
	# Set up data segments for flat 32-bit mode
	movl	%ecx, %ds
	movl	%ecx, %es
	movl	%ecx, %fs
	movl	%ecx, %gs
	movl	%ecx, %ss
	# The 32-bit code sets up its own stack, but this way we do have
	# a valid stack if some debugging hack wants to use it.
	addl	%ebx, %esp	# ebx:esp

	# Set up TR to make Intel VT happy
	# load task regiter
	# TSS를load 한다.
	ltr	%di

	# Clear registers to allow for future extensions to the
	# 32-bit boot protocol
	xorl	%ecx, %ecx
	xorl	%edx, %edx
	xorl	%ebx, %ebx
	xorl	%ebp, %ebp
	xorl	%edi, %edi

	# Set up LDTR to make Intel VT happy
	/* 보통 GDT'는 커널 모드용으로 하나 가 있으며, 
	   LDT는 각 프로세스별로 하나씩 만들어진다.
	   이들 테이블의 시작 위치는 각각 GDTR, 
	   LDTR이라는 레지스터가 가리키고 있다. */
	lldt	%cx

	jmpl	*%eax			# Jump to the 32-bit entrypoint
ENDPROC(in_pm32)

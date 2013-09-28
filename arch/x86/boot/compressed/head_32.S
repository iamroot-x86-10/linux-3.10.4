/*
 *  linux/boot/head.S
 *
 *  Copyright (C) 1991, 1992, 1993  Linus Torvalds
 */

/*
 *  head.S contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00001000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory. [According to comments etc elsewhere on a compressed
 * kernel it will end up at 0x1000 + 1Mb I hope so as I assume this. - AC]
 *
 * Page 0 is deliberately kept safe, since System Management Mode code in
 * laptops may need to access the BIOS data stored there.  This is also
 * useful for future device drivers that either access the BIOS via VM86
 * mode.
 */

/*
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 */
	.text

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page_types.h>
#include <asm/boot.h>
#include <asm/asm-offsets.h>

!! __HEAD는 linux/init.h에 다음과 같이 정의되어 있다.  
!! #define __HEAD		.section	".head.text","ax"

!! ENTRY는 linux/linkage.h에 다음과 같이 정의되어 있다.  
!!   #ifndef ENTRY
!!	  #define ENTRY(name) \
!!	  .globl name; \
!!	  ALIGN; \			.align 4,0x90 4byte align마추고 0x90으로 padding 한다.
!!						0x90 == nop text-section에서는 0을 넣으면 안되지...
!!						http://kjcc2.tistory.com/1215 참고
!! 	 name:
!!	#endif

	__HEAD
ENTRY(startup_32)
#ifdef CONFIG_EFI_STUB	!!확장 펌웨어 인터페이스
						!!EFI는작은 운영체제 수준의 BIOS를 대처하는 펌웨어이다.
	jmp	preferred_addr

	/*
	 * We don't need the return address, so set up the stack so
	 * efi_main() can find its arguments.
	 */
ENTRY(efi_pe_entry)
	add	$0x4, %esp

	call	make_boot_params
	cmpl	$0, %eax
	je	1f
	movl	0x4(%esp), %esi
	movl	(%esp), %ecx
	pushl	%eax
	pushl	%esi
	pushl	%ecx
	sub	$0x4, %esp

ENTRY(efi_stub_entry)
	add	$0x4, %esp
	call	efi_main
	cmpl	$0, %eax
	movl	%eax, %esi
	jne	2f
1:
	/* EFI init failed, so hang. */
	hlt
	jmp	1b
2:
	call	3f
3:
	popl	%eax
	subl	$3b, %eax
	subl	BP_pref_address(%esi), %eax
	add	BP_code32_start(%esi), %eax
	leal	preferred_addr(%eax), %eax
	jmp	*%eax

preferred_addr:
#endif
	cld
	/*
	 * Test KEEP_SEGMENTS flag to see if the bootloader is asking
	 * us to not reload segments
	 */
	/*
	Documentation/x86/boot.txt   
	Bit 6 (write): KEEP_SEGMENTS
	Protocol: 2.07+
	- If 0, reload the segment registers in the 32bit entry point.
	- If 1, do not reload the segment registers in the 32bit entry point.
		Assume that %cs %ds %ss %es are all set to flat segments with
		a base of 0 (or the equivalent for their environment).
	*/
	testb	$(1<<6), BP_loadflags(%esi)	!! esi는 boot_parmas table의 위치이다. pmjump.S의 30번 라인
	jnz	1f	!! KEEP_SEGMENTS가 설정되어 있으면 1로점프
			!! 설정되어있지 않으면  세그먼트 레지스터들을 재설정한다. 		

	/* kbuild에 의해서 BP_loadflags가 정의 된다.*/
	/* asm_offset.c에 다음과 같은 주석이 달려였다. */
	/*
 	 * Generate definitions needed by assembly language modules.
 	 * This code generates raw asm output which is post-processed to extract
 	 * and format the required data.
 	 */
	/* 참고:OFFSET(BP_loadflags, boot_params, hdr.loadflags);
	   => DEFILE(BP_loadflags, offsetof(struct boot_parmas, hdr.loadflags);
	     => asm volatile("\n->" BP_loadflags " %0 " boot_params.hdr.loadflags : : "i" (boot_params.hdr.loadflags))
	cli
	/* 세그먼트 레지스터 재설정 */
	movl	$__BOOT_DS, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
	movl	%eax, %ss
1:

/*
 * Calculate the delta between where we were compiled to run
 * at and where we were actually loaded at.  This can only be done
 * with a short local call on x86.  Nothing  else will tell us what
 * address we are running at.  The reserved chunk of the real-mode
 * data at 0x1e4 (defined as a scratch field) are used as the stack
 * for this calculation. Only 4 bytes are needed.
 */
	leal	(BP_scratch+4)(%esi), %esp	!! boot_params.scratch의 주소를 스택포인터로사용하기위해서
										!! esp에 저장한다.
	call	1f							!! 1:의 위치가 boot_params.scratch에 저장이된다.
										!! call을 하게되면 돌아갈 위치가 스택에 저장되기 때문이다.
										
1:	popl	%ebp						!! ebp에는 이 위치의 주소가 저장된다.
	subl	$1b, %ebp					!! 1b는 컴파일시의 주소값,
										!! ebp는 로드됬을 때 주소값의 delta 를 ebp에 저장한다.

/*
 * %ebp contains the address we are loaded at by the boot loader and %ebx
 * contains the address where we should move the kernel image temporarily
 * for safe in-place decompression.
 */

/*
x86: default CONFIG_PHYSICAL_START and CONFIG_PHYSICAL_ALIGN to 16 MB

Default CONFIG_PHYSICAL_START and CONFIG_PHYSICAL_ALIGN each to 16 MB,
		so that both non-relocatable and relocatable kernels are loaded at
		16 MB by a non-relocating bootloader.  This is somewhat hacky, but it
		appears to be the only way to do this that does not break some some
		set of existing bootloaders.

		We want to avoid the bottom 16 MB because of large page breakup,
		memory holes, and ZONE_DMA.  Embedded systems may need to reduce this,
		or update their bootloaders to be aware of the new min_alignment field.

		[ Impact: performance improvement, avoids problems on some systems ]

		Signed-off-by: H. Peter Anvin <hpa@xxxxxxxxx>
*/
#ifdef CONFIG_RELOCATABLE
	movl	%ebp, %ebx
	/* BP_kernel_alignment는 CONFIG_PHYSICAL_ALIGN으로 0x1000000으로 예상된다.*/
	/* http://www.spinics.net/lists/reiserfs-devel/msg03590.html 에 커널의 .configure 파일을 참고하면 */
	/* CONFIG_PHYSICAL_START=0x1000000
	 * CONFIG_RELOCATABLE=y
	 * CONFIG_PHYSICAL_ALIGN=0x1000000
	 */   
	movl	BP_kernel_alignment(%esi), %eax
	decl	%eax
	addl    %eax, %ebx
	notl	%eax
	andl    %eax, %ebx
	/* 위 코드는 0x1000000단위로align을 해준다. */
#else
	movl	$LOAD_PHYSICAL_ADDR, %ebx
#endif
	/* CONFIG_RELOCATABLE이 enable되어 있으면 ebx는 0x1000000단위로 align된 위치이고
	   그렇지 않으면 0x1000000이 된다. */
!! 2013.09.28 여기까지.

	/* Target address to relocate to for decompression */
	addl	$z_extract_offset, %ebx

	/* Set up the stack */
	leal	boot_stack_end(%ebx), %esp

	/* Zero EFLAGS */
	pushl	$0
	popfl

/*
 * Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	pushl	%esi
	leal	(_bss-4)(%ebp), %esi
	leal	(_bss-4)(%ebx), %edi
	movl	$(_bss - startup_32), %ecx
	shrl	$2, %ecx
	std
	rep	movsl
	cld
	popl	%esi

/*
 * Jump to the relocated address.
 */
	leal	relocated(%ebx), %eax
	jmp	*%eax
ENDPROC(startup_32)

	.text
relocated:

/*
 * Clear BSS (stack is currently empty)
 */
	xorl	%eax, %eax
	leal	_bss(%ebx), %edi
	leal	_ebss(%ebx), %ecx
	subl	%edi, %ecx
	shrl	$2, %ecx
	rep	stosl

/*
 * Adjust our own GOT
 */
	leal	_got(%ebx), %edx
	leal	_egot(%ebx), %ecx
1:
	cmpl	%ecx, %edx
	jae	2f
	addl	%ebx, (%edx)
	addl	$4, %edx
	jmp	1b
2:

/*
 * Do the decompression, and jump to the new kernel..
 */
	leal	z_extract_offset_negative(%ebx), %ebp
				/* push arguments for decompress_kernel: */
	pushl	%ebp		/* output address */
	pushl	$z_input_len	/* input_len */
	leal	input_data(%ebx), %eax
	pushl	%eax		/* input_data */
	leal	boot_heap(%ebx), %eax
	pushl	%eax		/* heap area */
	pushl	%esi		/* real mode pointer */
	call	decompress_kernel
	addl	$20, %esp

#if CONFIG_RELOCATABLE
/*
 * Find the address of the relocations.
 */
	leal	z_output_len(%ebp), %edi

/*
 * Calculate the delta between where vmlinux was compiled to run
 * and where it was actually loaded.
 */
	movl	%ebp, %ebx
	subl	$LOAD_PHYSICAL_ADDR, %ebx
	jz	2f	/* Nothing to be done if loaded at compiled addr. */
/*
 * Process relocations.
 */

1:	subl	$4, %edi
	movl	(%edi), %ecx
	testl	%ecx, %ecx
	jz	2f
	addl	%ebx, -__PAGE_OFFSET(%ebx, %ecx)
	jmp	1b
2:
#endif

/*
 * Jump to the decompressed kernel.
 */
	xorl	%ebx, %ebx
	jmp	*%ebp

/*
 * Stack and heap for uncompression
 */
	.bss
	.balign 4
boot_heap:
	.fill BOOT_HEAP_SIZE, 1, 0
boot_stack:
	.fill BOOT_STACK_SIZE, 1, 0
boot_stack_end:

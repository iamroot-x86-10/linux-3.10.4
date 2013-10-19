/*
 * 분석자- iamroot.org x86 10차
 * 도영주: asradad1@gmail.com
 * 조동현: bigeyeguy@gmail.com
 * 조유준: hogi2271@gmail.com
 * 김택훈: kth7284@gmail.com
 * 이광철: kclee2001@daum.net
 * 이성훈: colso110@gmail.com
 * 이경욱: leeku3@gmail.com
 * 김승환: drk83@naver.com
 * 김혜린: azahling@gmail.com
 * 송경민: gaford@naver.com
 * 홍  석: nuricook@gmail.com
 */

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
	.code32
	.text

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/pgtable_types.h>
#include <asm/page_types.h>
#include <asm/boot.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/asm-offsets.h>

	__HEAD
	/*
	 * __HEAD는 linux/init.h에 다음과 같이 정의되어 있다.  
	 * !! #define __HEAD		.section	".head.text","ax"
	 * ENTRY는 linux/linkage.h에 다음과 같이 정의되어 있다.  
	 * #ifndef ENTRY 
	 *	  #define ENTRY(name) \
	 *	  .globl name; \
	 *	  ALIGN; \			.align 4,0x90 4byte align마추고 0x90으로 padding 한다.
	 *						0x90 == nop text-section에서는 0을 넣으면 안되지...
	 *						http://kjcc2.tistory.com/1215 참고
	 * 	 name:
	 *	#endif
	 */
	.code32
ENTRY(startup_32)
	/*
	 * 32bit entry is 0 and it is ABI so immutable!
	 * If we come here directly from a bootloader,
	 * kernel(text+data+bss+brk) ramdisk, zero_page, command line
	 * all need to be under the 4G limit.
	 */
	cld

	/*
	 * Test KEEP_SEGMENTS flag to see if the bootloader is asking
	 * us to not reload segments
	 */
	testb $(1<<6), BP_loadflags(%esi)  !! esi는 boot_parmas table의 위치이다. pmjump.S의 30번 라인
	/*
	 * Test KEEP_SEGMENTS flag to see if the bootloader is asking
	 * us to not reload segments
	 *
     * Documentation/x86/boot.txt   
	 * Bit 6 (write): KEEP_SEGMENTS
	 * Protocol: 2.07+
	 * - If 0, reload the segment registers in the 32bit entry point.
	 * - If 1, do not reload the segment registers in the 32bit entry point.
	 *	Assume that %cs %ds %ss %es are all set to flat segments with
	 *	a base of 0 (or the equivalent for their environment).
     *
	 * head_32.S에서는 efi setting 하는 code가 있었는데, 이 부분에서 나오지 않고 밑에서 
	 * code32 -> code64로 변경하여 그 부분에서 setting 한다. 
	 */

	jnz 1f 	!! KEEP_SEGMENTS가 설정되어 있으면 1로점프
			!! 설정되어있지 않으면  세그먼트 레지스터들을 재설정한다. 		

	/* 
	 * kbuild에 의해서 BP_loadflags가 정의 된다.
	 * asm_offset.c에 다음과 같은 주석이 달려였다. 
	 *
 	 * Generate definitions needed by assembly language modules.
 	 * This code generates raw asm output which is post-processed to extract
 	 * and format the required data.
 	 */
	
	/* 참고:OFFSET(BP_loadflags, boot_params, hdr.loadflags);
	 *   => DEFILE(BP_loadflags, offsetof(struct boot_parmas, hdr.loadflags);
	 *   => asm volatile("\n->" BP_loadflags " %0 " boot_params.hdr.loadflags : : "i" (boot_params.hdr.loadflags))
	 */

	cli
	/* 세그먼트 레지스터 재설정 */
	movl	$(__BOOT_DS), %eax
	movl	%eax, %ds
	movl	%eax, %es
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
	leal	(BP_scratch+4)(%esi), %esp  !! boot_params.scratch의 주소를 스택포인터로사용하기위해서
										!! esp에 저장한다.

	call	1f   						!! 1:의 위치가 boot_params.scratch에 저장이된다.
										!! call을 하게되면 돌아갈 위치가 스택에 저장되기 때문이다.

1:	popl	%ebp 						!! ebp에는 이 위치의 주소가 저장된다.

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

/* STACK을 먼저 설정하고, CPU가 long mode를 지원하는 지 확인한다. */
/* setup a stack and make sure cpu supports long mode. */
	movl	$boot_stack_end, %eax
	addl	%ebp, %eax
	movl	%eax, %esp

	call	verify_cpu 		!! verify_cpu.S로 jmp
   	/* 
	 *  verify_cpu, returns the status of longmode and SSE in register %eax.
	 *		0: Success    1: Failure
	 */
	testl	%eax, %eax	 	!! eax & eax 이므로 success면 밑으로 failure인 경우 jmp	

	jnz	no_longmode

/*
 * Compute the delta between where we were compiled to run at
 * and where the code will actually run at.
 *
 * %ebp contains the address we are loaded at by the boot loader and %ebx
 * contains the address where we should move the kernel image temporarily
 * for safe in-place decompression.
 */

/* BP_kernel_alignment는 CONFIG_PHYSICAL_ALIGN으로 0x1000000으로 예상된다.*/
/* http://www.spinics.net/lists/reiserfs-devel/msg03590.html 에 커널의 .configure 파일을 참고하면 */
/* CONFIG_PHYSICAL_START=0x1000000
 * CONFIG_RELOCATABLE=y
 * CONFIG_PHYSICAL_ALIGN=0x1000000
 */   
#ifdef CONFIG_RELOCATABLE
	movl	%ebp, %ebx
	movl	BP_kernel_alignment(%esi), %eax
	decl	%eax
	addl	%eax, %ebx
	notl	%eax
	andl	%eax, %ebx
#else
/* CONFIG_RELOCATABLE이 enable되어 있으면 ebx는 0x1000000단위로 align된 위치이고
   그렇지 않으면 0x1000000이 된다. */
	movl	$LOAD_PHYSICAL_ADDR, %ebx
#endif

/* Target address to relocate to for decompression */
/* 
 * z_extract_offset : mkpiggy를 하고 나면 piggy.S가 나온다고한다.
 * http://www.iamroot.org/xe/index.php?mid=Kernel_8_x86&listStyle=webzine&page=2&document_srl=64680
 * z_extract_offset은 kernel생성시에 mkpiggy라는 프로그램을 실행하면서 지정해주는 것으로 보인다.
 * 이광철님의 서버에서 piggy.S값을 봤더니 다음과 같은 값으로 지정되어 있었음
 * 
	.section ".rodata.compressed","a",@progbits 
	.globl z_input_len 
	z_input_len = 3675669 
	.globl z_output_len 
	z_output_len = 8033088 
	.globl z_extract_offset 
	z_extract_offset = 0x431000 
	.globl z_extract_offset_negative 
	z_extract_offset_negative = -0x431000 
	.globl input_data, input_data_end 
	input_data: 
	.incbin "arch/x86/boot/compressed/vmlinux.bin.gz" 
		input_data_end:
 * 	z_extract_offset 이후로 compressed kernel을 복사하고,
 *      앞쪽에 decompressed kernel을 z_extract_offset의 앞쪽(ebx부터)에 옮겨놓는다. 
 * 	ebx부터 decompressed kernel의 이미지가 쓰여진다.
 */

	addl	$z_extract_offset, %ebx

/*
 * Prepare for entering 64 bit mode
 */

	/* Load new GDT with the 64bit segments using 32bit descriptor */
	/*
 	 * 	485 line을 확인하면 다음과 같은 code가 있음
	 *      gdt(%ebp) = ebp + gdt 
	 *	gdt:
	 *	.word	gdt_end - gdt		48byte
	 * 	.long	gdt			gdt adress
	 *	.word	0
	 * 	.quad	0x0000000000000000	
	 *	.quad	0x00af9a000000ffff
	 * 	.quad	0x00cf92000000ffff	
	 *	.quad	0x0080890000000000
	 *	.quad   0x0000000000000000
	 * 	gdt_end:
	 */

	leal	gdt(%ebp), %eax
	movl	%eax, gdt+2(%ebp)		!! .long gdt(gdt label의 주소) + ebp 
	lgdt	gdt(%ebp)			!! lgdt에서 size, address 필요한데 위와 같은 정보를 통해 접근

	/* Enable PAE mode */			!! PAE = Physical Address Extension 32bit architecture에서 4GB이상의 memory를 접근하기 위한 mode
						!! http://ko.wikipedia.org/wiki/%EB%AC%BC%EB%A6%AC_%EC%A3%BC%EC%86%8C_%ED%99%95%EC%9E%A5 참고 
	movl	$(X86_CR4_PAE), %eax
	movl	%eax, %cr4

 /*
  * Build early 4G boot pagetable
  */
	/* Initialize Page tables to 0 */
	leal	pgtable(%ebx), %edi		!! ebp는(커널의 시작주소), ebx(16M(0x1000000) + z_extract_offset)
	xorl	%eax, %eax
	movl	$((4096*6)/4), %ecx		!! ecx에 6K만큼 (24kb/4byte)를 설정한다. 
	rep	stosl				!! pagetable을 24KB만큼 0으로 초기화 한다.

/* PAE Paging 
 * PDPTE(Page-Directory-Pointer Table Entry) Format
 * |-------------------+---------------------------+------------+----------+-----+-----+------+---|
 * | 63  -  M (최대52) |      (M-1)   ~   12       |  11  ~  9  |  8 ~ 5   |  4  |  3  | 2, 1 | 0 |
 * |-------------------+---------------------------+------------+----------+-----+-----+------+---|
 * |   Reserved (0)    | Address of Page Directory |   Ignored  | Reserved | PCD | PWT | Rsvd | 1 |
 * |-------------------+---------------------------+------------+----------+-----+-----+------+---|
 *
 * PDE(Page-Direcotry-Entry) : Page Table
 * |----+-------------------+---------------------------+---------+---+---+---+---+---+---+---+---|
 * | 63 | 62  -  M (최대52) |      (M-1)   ~   12       | 11 ~  8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 * |----+-------------------+---------------------------+---------+---+---+---+---+---+---+---+---|
 * | XD |    Reserved (0)   |   Address of Page Table   | Ignored | 0 |Ign| A |PCD|PWT|U/S|R/W| 1 | 
 * |---+--------------------+---------------------------+---------+---+---+---+---+---+---+---+---|
 * 
 * PDE(Page-Directory-Entry) : 2MB Page Format
 * |----+-------------------+---------------------------+-------------+-----+--------+---+---+---+---+---+---+---+---+---|
 * | 63 | 62  -  M (최대52) |      (M-1)   ~   20       |  19  ~  13  |  12 | 11 ~ 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 * |----+-------------------+---------------------------+-------------+-----+--------+---+---+---+---+---+---+---+---+---|
 * | XD |    Reserved (0)   | Address of 2MB Page frame |   Reserved  | PAT | Ignored| G | 1 | D | A |PCD|PWT|U/S|R/W| 1 | 
 * |---+--------------------+---------------------------+-------------+-----+--------+---+---+---+---+---+---+---+---+---|
 *
 * Paging 단계
 * PDPTE -> PDE(Page Table)-> PDE(2MB Page)
 * Lv. 4 ->          Lv. 3 ->         Lv. 2
 * 
 * PDPTE의 경우 - Build Level 4 
 * 0x1007 
 * 0x1000 => Page Directory의 Address
 * 0x0007 => PDPTE의 권한 설정 (0x0000 0000 0000 0111)  
 * 
 * PDE의 경우   - Build Level 3
 * 0x1007 
 * 0x1000 => Page Directory의 Adress
 * 0x0007 => PDE의 권한 설정   (0x0000 0000 0000 0111)
 *
 * PDE : 2MB Page Format - Build Level 2
 * 0x200000  => Page Frame 의 Adress
 * 0x183     => Page의 권한 설정  (0x0000 0001 1000 0011)
 * 
 */
	/* Build Level 4 */
	leal	pgtable + 0(%ebx), %edi		!! z_extract_offset(ebx) + pgtable => edi  
	leal	0x1007 (%edi), %eax		
	movl	%eax, 0(%edi)			

	/* Build Level 3 */
	leal	pgtable + 0x1000(%ebx), %edi
	leal	0x1007(%edi), %eax			!! 0x1007 + %edi => %eax
	movl	$4, %ecx
1:	movl	%eax, 0x00(%edi)
	addl	$0x00001000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Build Level 2 */
	leal	pgtable + 0x2000(%ebx), %edi
	movl	$0x00000183, %eax
	movl	$2048, %ecx
1:	movl	%eax, 0(%edi)
	addl	$0x00200000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Enable the boot page tables */
	leal	pgtable(%ebx), %eax
	/* PL4의 base주소를 cr3에 저장한다.*/
	movl	%eax, %cr3

	/* Enable Long mode in EFER (Extended Feature Enable Register) */
	movl	$MSR_EFER, %ecx
	/* rdmsr: ecx의주소값을참조하여 msr레지스터값을 eax에 저장한다. */ 
	rdmsr
	/* 8번째 비트를 세팅하여 IA-32e Mode Enable 한다 */ 
	btsl	$_EFER_LME, %eax
	wrmsr

	/* After gdt is loaded */
	/* LDT를 0으로 초기화 한다. LDT를 사용하지 않는다. */
	xorl	%eax, %eax
	lldt	%ax
	/*
	   LTR:
	   Loads the source operand into the segment selector field of the task register. 
	   The source operand (a general-purpose register or a memory location) contains 
	   a segment selector that points to a task state segment (TSS). 
	   After the segment selector is loaded in the task register, the processor uses 
	   the segment selector to locate the segment descriptor for the TSS in the global 
	   descriptor table (GDT).
	*/   
	/* TS descriptor의 내용을 TSS 레지스터로 로드한다. */
	movl    $0x20, %eax
	ltr	%ax

	/*
	 * Setup for the jump to 64bit mode
	 *
	 * When the jump is performend we will be in long mode but
	 * in 32bit compatibility mode with EFER.LME = 1, CS.L = 0, CS.D = 1
	 * (and in turn EFER.LMA = 1).	To jump into 64bit mode we use
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 * We place all of the values on our mini stack so lret can
	 * used to perform that far jump.
	 */
	pushl	$__KERNEL_CS
	leal	startup_64(%ebp), %eax
	pushl	%eax

	/* Enter paged protected Mode, activating Long Mode */
	/* movl를 이용하여 cr0를 설정하기 때문에 X86_CR0_PE를 다시 셋팅하였다 */
	movl	$(X86_CR0_PG | X86_CR0_PE), %eax /* Enable Paging and Protected mode */
	movl	%eax, %cr0

	/* Jump from 32bit compatibility mode into 64bit mode. */
	lret
ENDPROC(startup_32)

	.code64
	.org 0x200
ENTRY(startup_64)
	/*
	 * 64bit entry is 0x200 and it is ABI so immutable!
	 * We come here either from startup_32 or directly from a
	 * 64bit bootloader.
	 * If we come here from a bootloader, kernel(text+data+bss+brk),
	 * ramdisk, zero_page, command line could be above 4G.
	 * We depend on an identity mapped page table being provided
	 * that maps our entire kernel(text+data+bss+brk), zero page
	 * and command line.
	 */
#ifdef CONFIG_EFI_STUB
	/*
	 * The entry point for the PE/COFF executable is efi_pe_entry, so
	 * only legacy boot loaders will execute this jmp.
	 */
	jmp	preferred_addr

ENTRY(efi_pe_entry)
	mov	%rcx, %rdi
	mov	%rdx, %rsi
	pushq	%rdi
	pushq	%rsi
	call	make_boot_params
	cmpq	$0,%rax
	je	1f
	mov	%rax, %rdx
	popq	%rsi
	popq	%rdi

ENTRY(efi_stub_entry)
	call	efi_main
	movq	%rax,%rsi
	cmpq	$0,%rax
	jne	2f
1:
	/* EFI init failed, so hang. */
	hlt
	jmp	1b
2:
	call	3f
3:
	popq	%rax
	subq	$3b, %rax
	subq	BP_pref_address(%rsi), %rax
	add	BP_code32_start(%esi), %eax
	leaq	preferred_addr(%rax), %rax
	jmp	*%rax

preferred_addr:
#endif

	/* Setup data segments. */
	xorl	%eax, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss
	movl	%eax, %fs
	movl	%eax, %gs

	/*
	 * Compute the decompressed kernel start address.  It is where
	 * we were loaded at aligned to a 2M boundary. %rbp contains the
	 * decompressed kernel start address.
	 *
	 * If it is a relocatable kernel then decompress and run the kernel
	 * from load address aligned to 2MB addr, otherwise decompress and
	 * run the kernel from LOAD_PHYSICAL_ADDR
	 *
	 * We cannot rely on the calculation done in 32-bit mode, since we
	 * may have been invoked via the 64-bit entry point.
	 */

	/* Start with the delta to where the kernel will run at. */
#ifdef CONFIG_RELOCATABLE
	leaq	startup_32(%rip) /* - $startup_32 */, %rbp
	movl	BP_kernel_alignment(%rsi), %eax
	decl	%eax
	addq	%rax, %rbp
	notq	%rax
	andq	%rax, %rbp
#else
	movq	$LOAD_PHYSICAL_ADDR, %rbp
#endif

	/* Target address to relocate to for decompression */
	leaq	z_extract_offset(%rbp), %rbx

	/* Set up the stack */
	leaq	boot_stack_end(%rbx), %rsp

	/* Zero EFLAGS */
	pushq	$0
	popfq

/*
 * Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	/* 
	 * 압축된 커널을 z_extract_offset + rbp 위치에 복사한다. 
	 * 커널의 시작부터 복사하는 것이 아니라 커널의 끝에 해당하는
	 * bss 세션의 제외한 윗부분 부터 거꾸로 복사한다. 
	 * arch/x86/boot/compressed/vmlinux.lds.S 여기에서 압축된 커널의
	 * 세션 구조를 확인 할 수 있다.
	 */
	pushq	%rsi
	leaq	(_bss-8)(%rip), %rsi	!! 압축된 커널의 마지막 위치
	leaq	(_bss-8)(%rbx), %rdi	!! 압축된 커널을 옮길 위치
	movq	$_bss /* - $startup_32 */, %rcx !! arch/x86/boot/compressed/vmlinux.lds.S에서 
											!! start_32를 0으로 가정하고 있기 때문에 
											!! - start_32를 주석 처리한 것같다.
	shrq	$3, %rcx	!! 8bytes 단위로 나눴다. 즉, 8byte단위로 복사를 하겠다.
	std
	rep	movsq
	cld
	popq	%rsi

/*
 * Jump to the relocated address.
 */
	leaq	relocated(%rbx), %rax	!! rax에 reloated + rbx가 rax에 저장
	jmp	*%rax					
	
	/*
	 * 여기 부터는 복사된 커널의 코드 부분에 해당 된다.
	 * 
	 * rip  relative addressing을 사용하지 않으면 아래의 코드에서
	 * _bss 와 같은 심볼에 접근할 수가 없다.
	 */
	.text
relocated:

/*
 * Clear BSS (stack is currently empty)
 */
	xorl	%eax, %eax
	leaq    _bss(%rip), %rdi
	leaq    _ebss(%rip), %rcx
	subq	%rdi, %rcx	
	shrq	$3, %rcx
	rep	stosq

/*
 * Adjust our own GOT
 */
	/*
	 * 공유 라이브러리 오프셋 정보를 제 조정한다.
	 * http://bottomupcs.sourceforge.net/csbu/x3824.htm
	 *
	 * 컴파일 타이밍에 GOT테이블이 만들어진다. 
	 * 그 이후 로더에 의해서 GOT테이블을 갱신하여야 되는데
	 * 그런 부분이 없다. 즉, GOT테이블에는 0을 기준으로 
	 * 오프셋 정보만이 담겨있을 것이다. 그래서 절대 주소값을
	 * 저장해야 되기 때문에 rbx값을 더해주는 작업을 한다.
	 */
	leaq	_got(%rip), %rdx
	leaq	_egot(%rip), %rcx
1:
	cmpq	%rcx, %rdx
	jae	2f
	addq	%rbx, (%rdx)	!! 
	addq	$8, %rdx
	jmp	1b
2:
	
/*
 * Do the decompression, and jump to the new kernel..
 */
	pushq	%rsi			/* Save the real mode argument */
	movq	%rsi, %rdi		/* real mode address */
	leaq	boot_heap(%rip), %rsi	/* malloc area for uncompression */
	leaq	input_data(%rip), %rdx  /* input_data */ !! mkpiggy.c에 의해서 piggy.S 파일이 생성,
													 !! piggy.S에 input_data가 선언되어 있다.
	movl	$z_input_len, %ecx	/* input_len */		 !! 압축된 커널 크기
	movq	%rbp, %r8		/* output target address */ !! 압축을 풀 위치
													 !! http://kkamagui.tistory.com/811 참고하면 64-bit에서 함수호출 
													 !! 규약을 확인할 수 있다. 
													 !! r8레지스터는 5번째 아규먼트에 해당된다.
	call	decompress_kernel
	popq	%rsi

/*
 * Jump to the decompressed kernel.
 */
	jmp	*%rbp

	.code32
no_longmode:
	/* This isn't an x86-64 CPU so hang */
1:
	hlt
	jmp     1b

#include "../../kernel/verify_cpu.S"

	.data
gdt:
	.word	gdt_end - gdt
	.long	gdt
	.word	0
	.quad	0x0000000000000000	/* NULL descriptor */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x0080890000000000	/* TS descriptor */
	.quad   0x0000000000000000	/* TS continued */
gdt_end:

/*
 * Stack and heap for uncompression
 */
/*
 * arch/x86/include/asm/boot.h
 * BOOT_HEAP_SIZE는 BZIP2 인 경우 0x40000
 * 아닌 경우에는 0x8000
 * BOOT_STACK_SIZE는 x86_64인 경우 0x4000
 * 아닌 경우에는 0x1000
 * http://www.cs.utah.edu/dept/old/texinfo/as/as.html#SEC74
 * .fill repeat , size , value
 *
 * result, size and value are absolute expressions. This emits repeat copies of size bytes. Repeat may be zero or more. Size may be zero or more, but if it is more than 8, then it is deemed to have the value 8, compatible with other people's assemblers. The contents of each repeat bytes is taken from an 8-byte number. The highest order 4 bytes are zero. The lowest order 4 bytes are value rendered in the byte-order of an integer on the computer is assembling for. Each size bytes in a repetition is taken from the lowest order size bytes of this number. Again, this bizarre behavior is compatible with other people's assemblers.

size and value are optional. If the second comma and value are absent, value is assumed zero. If the first comma and following tokens are absent, size is assumed to be 1.
 *
 */

	.bss
	.balign 4
boot_heap:
	.fill BOOT_HEAP_SIZE, 1, 0
boot_stack:
	.fill BOOT_STACK_SIZE, 1, 0
boot_stack_end:

/*
 * Space for page tables (not in .bss so not zeroed)
 */
	.section ".pgtable","a",@nobits
	.balign 4096
/*
 *	page table section 24KB를 초기화한다. 
 */ 
pgtable:
	.fill 6*4096, 1, 0

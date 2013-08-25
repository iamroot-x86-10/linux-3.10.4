/*
 *	header.S
 *
 *	Copyright (C) 1991, 1992 Linus Torvalds
 *
 *	Based on bootsect.S and setup.S
 *	modified by more people than can be counted
 *
 *	Rewritten as a common file by H. Peter Anvin (Apr 2007)
 *
 * BIG FAT NOTE: We're in real mode using 64k segments.  Therefore segment
 * addresses must be multiplied by 16 to obtain their respective linear
 * addresses. To avoid confusion, linear addresses are written using leading
 * hex while segment addresses are written as segment:offset.
 *
 */

#include <asm/segment.h>
#include <generated/utsrelease.h>
#include <asm/boot.h>
#include <asm/e820.h>
#include <asm/page_types.h>
#include <asm/setup.h>
#include <asm/bootparam.h>
#include "boot.h"
#include "voffset.h"
#include "zoffset.h"

BOOTSEG		= 0x07C0		/* original address of boot-sector */
SYSSEG		= 0x1000		/* historical load address >> 4 */

#ifndef SVGA_MODE
#define SVGA_MODE ASK_VGA
#endif

#ifndef ROOT_RDONLY
#define ROOT_RDONLY 1
#endif
! 여기서 부터 512 만큼은 bootsector의 이미지에 해당한다. 
! 0x90000에 올라온다. 하지만 링커에의해 _start부터 시작한다.
! 링커없이 시작되면 여기서부터 512 바이트가 MBR에 적혀 부트로더 역활을 하게된다.
	.code16			! [bits 16]과 똑같다.
	.section ".bstext", "ax" ! ".bstext"를 선언한다. "ax"는 flag이다.

	.global bootsect_start
bootsect_start:
#ifdef CONFIG_EFI_STUB
	# "MZ", MS-DOS header
	.byte 0x4d
	.byte 0x5a
#endif

	# Normalize the start address
	ljmp	$BOOTSEG, $start2

start2:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	xorw	%sp, %sp
	sti
	cld

	movw	$bugger_off_msg, %si

msg_loop:
	lodsb
	andb	%al, %al
	jz	bs_die
	movb	$0xe, %ah
	movw	$7, %bx
	int	$0x10
	jmp	msg_loop

bs_die:
	# Allow the user to press a key, then reboot
	xorw	%ax, %ax
	int	$0x16
	int	$0x19

	# int 0x19 should never return.  In case it does anyway,
	# invoke the BIOS reset code...
	ljmp	$0xf000,$0xfff0

#ifdef CONFIG_EFI_STUB
	.org	0x3c
	#
	# Offset to the PE header.
	#
	.long	pe_header
#endif /* CONFIG_EFI_STUB */

	.section ".bsdata", "a"
bugger_off_msg:
	.ascii	"Direct floppy boot is not supported. "
	.ascii	"Use a boot loader program instead.\r\n"
	.ascii	"\n"
	.ascii	"Remove disk and press any key to reboot ...\r\n"
	.byte	0

#ifdef CONFIG_EFI_STUB
pe_header:
	.ascii	"PE"
	.word 	0

coff_header:
#ifdef CONFIG_X86_32
	.word	0x14c				# i386
#else
	.word	0x8664				# x86-64
#endif
	.word	3				# nr_sections
	.long	0 				# TimeDateStamp
	.long	0				# PointerToSymbolTable
	.long	1				# NumberOfSymbols
	.word	section_table - optional_header	# SizeOfOptionalHeader
#ifdef CONFIG_X86_32
	.word	0x306				# Characteristics.
						# IMAGE_FILE_32BIT_MACHINE |
						# IMAGE_FILE_DEBUG_STRIPPED |
						# IMAGE_FILE_EXECUTABLE_IMAGE |
						# IMAGE_FILE_LINE_NUMS_STRIPPED
#else
	.word	0x206				# Characteristics
						# IMAGE_FILE_DEBUG_STRIPPED |
						# IMAGE_FILE_EXECUTABLE_IMAGE |
						# IMAGE_FILE_LINE_NUMS_STRIPPED
#endif

optional_header:
#ifdef CONFIG_X86_32
	.word	0x10b				# PE32 format
#else
	.word	0x20b 				# PE32+ format
#endif
	.byte	0x02				# MajorLinkerVersion
	.byte	0x14				# MinorLinkerVersion

	# Filled in by build.c
	.long	0				# SizeOfCode

	.long	0				# SizeOfInitializedData
	.long	0				# SizeOfUninitializedData

	# Filled in by build.c
	.long	0x0000				# AddressOfEntryPoint

	.long	0x0200				# BaseOfCode
#ifdef CONFIG_X86_32
	.long	0				# data
#endif

extra_header_fields:
#ifdef CONFIG_X86_32
	.long	0				# ImageBase
#else
	.quad	0				# ImageBase
#endif
	.long	0x20				# SectionAlignment
	.long	0x20				# FileAlignment
	.word	0				# MajorOperatingSystemVersion
	.word	0				# MinorOperatingSystemVersion
	.word	0				# MajorImageVersion
	.word	0				# MinorImageVersion
	.word	0				# MajorSubsystemVersion
	.word	0				# MinorSubsystemVersion
	.long	0				# Win32VersionValue

	#
	# The size of the bzImage is written in tools/build.c
	#
	.long	0				# SizeOfImage

	.long	0x200				# SizeOfHeaders
	.long	0				# CheckSum
	.word	0xa				# Subsystem (EFI application)
	.word	0				# DllCharacteristics
#ifdef CONFIG_X86_32
	.long	0				# SizeOfStackReserve
	.long	0				# SizeOfStackCommit
	.long	0				# SizeOfHeapReserve
	.long	0				# SizeOfHeapCommit
#else
	.quad	0				# SizeOfStackReserve
	.quad	0				# SizeOfStackCommit
	.quad	0				# SizeOfHeapReserve
	.quad	0				# SizeOfHeapCommit
#endif
	.long	0				# LoaderFlags
	.long	0x6				# NumberOfRvaAndSizes

	.quad	0				# ExportTable
	.quad	0				# ImportTable
	.quad	0				# ResourceTable
	.quad	0				# ExceptionTable
	.quad	0				# CertificationTable
	.quad	0				# BaseRelocationTable

	# Section table
section_table:
	#
	# The offset & size fields are filled in by build.c.
	#
	.ascii	".setup"
	.byte	0
	.byte	0
	.long	0
	.long	0x0				# startup_{32,64}
	.long	0				# Size of initialized data
						# on disk
	.long	0x0				# startup_{32,64}
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	0x60500020			# Characteristics (section flags)

	#
	# The EFI application loader requires a relocation section
	# because EFI applications must be relocatable. The .reloc
	# offset & size fields are filled in by build.c.
	#
	.ascii	".reloc"
	.byte	0
	.byte	0
	.long	0
	.long	0
	.long	0				# SizeOfRawData
	.long	0				# PointerToRawData
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	0x42100040			# Characteristics (section flags)

	#
	# The offset & size fields are filled in by build.c.
	#
	.ascii	".text"
	.byte	0
	.byte	0
	.byte	0
	.long	0
	.long	0x0				# startup_{32,64}
	.long	0				# Size of initialized data
						# on disk
	.long	0x0				# startup_{32,64}
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	0x60500020			# Characteristics (section flags)

#endif /* CONFIG_EFI_STUB */

	# Kernel attributes; used by setup.  This is part 1 of the
	# header, from the old boot sector.

	# offset 495 
	.section ".header", "a"
	.globl	sentinel
sentinel:	.byte 0xff, 0xff        /* Used to detect broken loaders */


	!@_@! 아래 값들은 리눅스를 빌드할 때 채워지는 값으로 빌더에서 정보를 읽어서 저장하는 것임
	.globl	hdr
hdr:
setup_sects:	.byte 0			/* Filled in by build.c */
root_flags:	.word ROOT_RDONLY
syssize:	.long 0			/* Filled in by build.c */
ram_size:	.word 0			/* Obsolete */
vid_mode:	.word SVGA_MODE
root_dev:	.word 0			/* Filled in by build.c */
boot_flag:	.word 0xAA55

	# 여기 까지가 lilo와 grub 이 나오기 이전에 MBR영역에 해당하는 부분이다.
	# offset 512, entry point
	.globl	_start ! 0x902000 
_start:		# 링커에의해서 시작번지가 지정된다. setup.ld를 참조하자
		# Explicitly enter this as bytes, or the assembler
		# tries to generate a 3-byte jump here, which causes
		# everything else to push off to the wrong offset.
		.byte	0xeb		# short (2-byte) jump
		.byte	start_of_setup-1f	# header signature 까지는 2바이트 밖에 허용이 안된다.
									# "HdrS"는 3byte(?)에 위치 하여야 한다.
									# 부트로더들이 3byte이후의 signature, version number
									# 참고하기 때문에...
									# 1f는 레이블이라는 의미이다.
									# start_of_setup레이블에서 1레이블을 뺀것이다.
1:

	# Part 2 of the header, from the old setup.S

		.ascii	"HdrS"		# header signature
		.word	0x020c		# header version number (>= 0x0105)
					# or else old loadlin-1.5 will fail)
		.globl realmode_swtch
realmode_swtch:	.word	0, 0		# default_switch, SETUPSEG
start_sys_seg:	.word	SYSSEG		# obsolete and meaningless, but just
					# in case something decided to "use" it
		.word	kernel_version-512 # pointing to kernel version string
					# above section of header is compatible
					# with loadlin-1.5 (header v1.5). Don't
					# change it.

type_of_loader:	.byte	0		# 0 means ancient bootloader, newer
					# bootloaders know to change this.
					# See Documentation/x86/boot.txt for
					# assigned ids

# flags, unused bits must be zero (RFU) bit within loadflags
loadflags:
		.byte	LOADED_HIGH	# The kernel is to be loaded high

setup_move_size: .word  0x8000		# size to move, when setup is not
					# loaded at 0x90000. We will move setup
					# to 0x90000 then just before jumping
					# into the kernel. However, only the
					# loader knows how much data behind
					# us also needs to be loaded.

code32_start:				# here loaders can put a different
					# start address for 32-bit code.
		.long	0x100000	# 0x100000 = default for big kernel

ramdisk_image:	.long	0		# address of loaded ramdisk image
					# Here the loader puts the 32-bit
					# address where it loaded the image.
					# This only will be read by the kernel.

ramdisk_size:	.long	0		# its size in bytes

bootsect_kludge:
		.long	0		# obsolete

heap_end_ptr:	.word	_end+STACK_SIZE-512
					# (Header version 0x0201 or later)
					# space from here (exclusive) down to
					# end of setup code can be used by setup
					# for local heap purposes.

ext_loader_ver:
		.byte	0		# Extended boot loader version
ext_loader_type:
		.byte	0		# Extended boot loader type

cmd_line_ptr:	.long	0		# (Header version 0x0202 or later)
					# If nonzero, a 32-bit pointer
					# to the kernel command line.
					# The command line should be
					# located between the start of
					# setup and the end of low
					# memory (0xa0000), or it may
					# get overwritten before it
					# gets read.  If this field is
					# used, there is no longer
					# anything magical about the
					# 0x90000 segment; the setup
					# can be located anywhere in
					# low memory 0x10000 or higher.

ramdisk_max:	.long 0x7fffffff
					# (Header version 0x0203 or later)
					# The highest safe address for
					# the contents of an initrd
					# The current kernel allows up to 4 GB,
					# but leave it at 2 GB to avoid
					# possible bootloader bugs.

kernel_alignment:  .long CONFIG_PHYSICAL_ALIGN	#physical addr alignment
						#required for protected mode
						#kernel
#ifdef CONFIG_RELOCATABLE
relocatable_kernel:    .byte 1
#else
relocatable_kernel:    .byte 0
#endif
min_alignment:		.byte MIN_KERNEL_ALIGN_LG2	# minimum alignment

xloadflags:
#ifdef CONFIG_X86_64
# define XLF0 XLF_KERNEL_64			/* 64-bit kernel */
#else
# define XLF0 0
#endif

#if defined(CONFIG_RELOCATABLE) && defined(CONFIG_X86_64)
   /* kernel/boot_param/ramdisk could be loaded above 4g */
# define XLF1 XLF_CAN_BE_LOADED_ABOVE_4G
#else
# define XLF1 0
#endif

#ifdef CONFIG_EFI_STUB
# ifdef CONFIG_X86_64
#  define XLF23 XLF_EFI_HANDOVER_64		/* 64-bit EFI handover ok */
# else
#  define XLF23 XLF_EFI_HANDOVER_32		/* 32-bit EFI handover ok */
# endif
#else
# define XLF23 0
#endif
			.word XLF0 | XLF1 | XLF23

cmdline_size:   .long   COMMAND_LINE_SIZE-1     #length of the command line,
                                                #added with boot protocol
                                                #version 2.06

hardware_subarch:	.long 0			# subarchitecture, added with 2.07
						# default to 0 for normal x86 PC

hardware_subarch_data:	.quad 0

payload_offset:		.long ZO_input_data
payload_length:		.long ZO_z_input_len

setup_data:		.quad 0			# 64-bit physical pointer to
						# single linked list of
						# struct setup_data

pref_address:		.quad LOAD_PHYSICAL_ADDR	# preferred load addr

#define ZO_INIT_SIZE	(ZO__end - ZO_startup_32 + ZO_z_extract_offset)
#define VO_INIT_SIZE	(VO__end - VO__text)
#if ZO_INIT_SIZE > VO_INIT_SIZE
#define INIT_SIZE ZO_INIT_SIZE
#else
#define INIT_SIZE VO_INIT_SIZE
#endif
init_size:		.long INIT_SIZE		# kernel initialization size
handover_offset:
#ifdef CONFIG_EFI_STUB
  			.long 0x30		# offset to the handover
						# protocol entry point
#else
			.long 0
#endif

# End of setup header #####################################################

	.section ".entrytext", "ax"
start_of_setup:
# Force %es = %ds
						!! lilo에서 ds, es를 0x90000으로 설정하였다.
	movw	%ds, %ax	!! AT&T 문법에서는 ds => ax로 저장한다.
	movw	%ax, %es
	cld

# Apparently some ancient versions of LILO invoked the kernel with %ss != %ds,
# which happened to work by accident for the old code.  Recalculate the stack
# pointer if %ss is invalid.  Otherwise leave it alone, LOADLIN sets up the
# stack behind its own code, so we can't blindly put it directly past the heap.

	movw	%ss, %dx	!! 0x90000
	cmpw	%ax, %dx	# %ds == %ss?
	movw	%sp, %dx
	je	2f		# -> assume %sp is reasonably set
				!! 2f => forward 아래로 처음 만나는 레이블로 뛰어라

	# Invalid %ss, make up a new stack
	movw	$_end, %dx
	testb	$CAN_USE_HEAP, loadflags
	jz	1f
	movw	heap_end_ptr, %dx		!! heap을 사용하게되면 스택을
									!! heap_end_ptr + STACK_SIZE만큼 사용하겠다.

1:	addw	$STACK_SIZE, %dx		!! heap을 사용하지 않으면
									!! _end + STACK_SIZE만클 사용하겠다.
	jnc	2f
	xorw	%dx, %dx	# Prevent wraparound

2:	# Now %dx should point to the end of our stack space
	andw	$~3, %dx	# dword align (might as well...)
						!! stack의 end가 0인지 아닌지를 검사
	jnz	3f
						!! 0이면 0xfffc를 넣어준다.
	movw	$0xfffc, %dx	# Make sure we're not zero
3:	movw	%ax, %ss	!! 스택의 시작은 0x90000으로 하고
						!! 스택의 끝은 레이블 1,2,3에 의해 설정되어진다.
	movzwl	%dx, %esp	# Clear upper half of %esp
						!! esp의 하위16비트는 dx값으로 복사하고
						!! 상위 16비트는 0으로 채운다.
	sti			# Now we should have a working stack

# We will have entered with %cs = %ds+0x20, normalize %cs so
# it is on par with the other segments.
	pushw	%ds
	pushw	$6f
	lretw				!! lretw가 cs + eip 를 가지고 far 점프를 하는데
						!! 이때 스택에 저장되어 있는 두값을 pop 해서
						!! eip(6f가저장)에 먼저 저장하고, cs(ds가 저장)에 저장하여
						!! 점프를 하는데 그렇게 되면 바로 아래줄로 
						!! 뛰어가게 된다.
						!! 스택검사와 cs와 ds를 같게 만들어주기 위한 트릭이다.
6:

# Check signature at end of setup
	cmpl	$0x5a5aaa55, setup_sig
	jne	setup_bad

# Zero the bss
	movw	$__bss_start, %di
	movw	$_end+3, %cx
	xorl	%eax, %eax
	subw	%di, %cx
	shrw	$2, %cx
	rep; stosl			!! EAX -> es:di 로 저장

# Jump to C code (should not return)
	calll	main

# Setup corrupt somehow...
setup_bad:
	movl	$setup_corrupt, %eax
	calll	puts
	# Fall through...

	.globl	die
	.type	die, @function
die:
	hlt
	jmp	die

	.size	die, .-die

	.section ".initdata", "a"
setup_corrupt:
	.byte	7
	.string	"No setup signature found...\n"

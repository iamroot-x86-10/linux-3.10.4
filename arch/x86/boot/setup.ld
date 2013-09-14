/*
 * setup.ld
 *
 * Linker script for the i386 setup code
 */
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)	/* 출력될 목적파일의 타겟머신환경 결정 */
ENTRY(_start)		/* 이 링크스크립트로 링크된 목적파일의 
					   entry point로 _start 심볼을 사용하도록 한다.*/

SECTIONS
{
	. = 0;	/* 위치카운터를 0으로 세팅한다. 
			   원래 초기값도 0이긴 하다.*/
	.bstext		: { *(.bstext) }	/* {*(.bstext)}라는 뜻는 출력될 파일에 
									   .bstext섹션에 링크대상의 모든 .bstext섹션의 
									   정보를 모아서 생성하라는 뜻이다.*/
	.bsdata		: { *(.bsdata) }

	. = 495;	/* 위치카운터를 495으로 세팅한다.
		     	   이전 bstext와 bsdata가 495가 되지 
				   않더라도 빈공간을 두고 495에서 생성된다.*/
	.header		: { *(.header) }
	.entrytext	: { *(.entrytext) }
	.inittext	: { *(.inittext) }
	.initdata	: { *(.initdata) }
	__end_init = .;	/*__end_init을 현재 위치카운터 값으로 초기화한다.
					 결과적으로 __end_init은 header,entrytext,inittext,initdata 
					 섹션이 끝난 위치를 가리킨다. */

	.text		: { *(.text) }	/* 모든 입력된 오브젝트의 .text섹션이 이 위치에 할당된다. */
	.text32		: { *(.text32) }

	. = ALIGN(16);	/* 현재 위치카운터를 16정렬로 재정렬한뒤에 갱신한다. 
					   값은 당연히 16정렬이 맞춰지는 가장작은 큰 수가 된다.
			           현재 위치카운터가 31이라면 위 명령어로 인해 위치카운터는 32가 된다. */
	.rodata		: { *(.rodata*) }

	.videocards	: {
		video_cards = .;	/* 전역심볼 video_cards 를 설정하고 
							   값을 현재 위치카운터로 설정한다.*/
		*(.videocards)		/* 모든 입력된 오브젝트의 .videocards섹션값을 할당한다. */
							/* #define __videocard struct card_info __attribute__((section(".videocards"))) */
		video_cards_end = .;	/* 전역심볼 video_cards_end를 설정한다. 
								   결과적으로 video.c에서 video_cards 심볼을 이용해서,
					     		   video-bios,mode,vesa,vga의 정보를 가져올 수 있는 것 같다.*/
	}

	. = ALIGN(16);
	.data		: { *(.data*) }

	.signature	: {
		setup_sig = .;	/* setup_sig라는 심볼을 통해 아래 0x5a5aaa55를 접근할 수 있도록 앞에 설정한다. */
		LONG(0x5a5aaa55)
	}


	. = ALIGN(16);
	.bss		:
	{
		__bss_start = .;
		*(.bss)
		__bss_end = .;
	}
	. = ALIGN(16);
	_end = .;

	/DISCARD/ : { *(.note*) }	/* 입력파일에 .note섹션이 있더라고 출력물에 포함되지 않고 버려진다.
					     		   결과적으로 출력파일의 맨 끝이 _end가 된다. */
	/*
	 * The ASSERT() sink to . is intentional, for binutils 2.14 compatibility:
	 */
	 /* ASSERT(exp, msg) --> exp가 0인지 확인해서 0이면 msg를 출력하고 링크를 중단한다. */
	. = ASSERT(_end <= 0x8000, "Setup too big!");	/* 출력파일의 맨 마지막위치를 
													   _end로 설정하는데, 이가 8000을 넘으면 링크하지 않음. */
	. = ASSERT(hdr == 0x1f1, "The setup header has the wrong offset!");
	/* hdr은 header.S에 정의되는데, 이 offset을 확인하는 것 같다. 
	    0x1f1 == 497이다. header.S에 보면 .header섹션에서 
		.glob로 496에 sentiel, 497에 hdr이 할당되는 것을 확인하는 것으로 보인다.*/
	/* Necessary for the very-old-loader check to work... */
	. = ASSERT(__end_init <= 5*512, "init sections too big!");
}

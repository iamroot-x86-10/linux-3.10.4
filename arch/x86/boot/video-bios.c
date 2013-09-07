/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Standard video BIOS modes
 *
 * We have two options for this; silent and scanned.
 */

#include "boot.h"
#include "video.h"

static __videocard video_bios;

/* Set a conventional BIOS mode */
static int set_bios_mode(u8 mode);

static int bios_set_mode(struct mode_info *mi)
{
	return set_bios_mode(mi->mode - VIDEO_FIRST_BIOS);
}

static int set_bios_mode(u8 mode)
{
	struct biosregs ireg, oreg;
	u8 new_mode;

	initregs(&ireg);
	ireg.al = mode;		/* AH=0x00 Set Video Mode */
	intcall(0x10, &ireg, NULL);
	
	ireg.ah = 0x0f;		/* Get Current Video Mode */
	intcall(0x10, &ireg, &oreg);

	/* TODO: 의미를 모르겠다.*/
	do_restore = 1;		/* Assume video contents were lost */
	
	/* set 하고 get 해서 mode가 설정되어 있는지 확인한다. */
	/* Not all BIOSes are clean with the top bit */
	new_mode = oreg.al & 0x7f;

	if (new_mode == mode)
		return 0;	/* Mode change OK */

#ifndef _WAKEUP
	if (new_mode != boot_params.screen_info.orig_video_mode) {
		/* Mode setting failed, but we didn't end up where we
		   started.  That's bad.  Try to revert to the original
		   video mode. */
		ireg.ax = boot_params.screen_info.orig_video_mode;
		intcall(0x10, &ireg, NULL);
	}
#endif
	return -1;
}

/* 등록되지 않은 TEXT모드만 추가한다. */
static int bios_probe(void)
{
	u8 mode;
#ifdef _WAKEUP
	u8 saved_mode = 0x03;
#else
	u8 saved_mode = boot_params.screen_info.orig_video_mode;
#endif
	u16 crtc;
	struct mode_info *mi;
	int nmodes = 0;
	
	/* CGA이면 return 한다.
	   VESA Probe는 adapter를 셋팅하는 부분이 없다.
	   VGA Probe에서 adapter를 셋팅한다.
	   아마도 VGA라고 생각하고 넘어가도 되겠다.
	   */
	if (adapter != ADAPTER_EGA && adapter != ADAPTER_VGA)
		return 0;
	
	/* FS REGISTER를 0으로 셋팅한다.*/
	set_fs(0);
	crtc = vga_crtc();	

	video_bios.modes = GET_HEAP(struct mode_info, 0);

   /* 0x0100 to 0x017f - standard BIOS modes. The ID is a BIOS video mode number
	(as presented to INT 10, function 00) increased by 0x0100.
	Documetaion/svag.txt 참고

	0x00 ~ 0x13가지는 정의 되어 있고 0x14 ~ 0x7F까지는 지정되어 있지 않다.
	int 0x10 0 참고
	그래서 VIDEO_FIRST_BIOS+0x14 ~ VIDEO_FIRST_BIOS+0x7F까지 루프를 돌면서
	VIDEO MODE를 셋팅한다.
	*/
	for (mode = 0x14; mode <= 0x7f; mode++) {
		if (!heap_free(sizeof(struct mode_info)))
			break;
		
		/* mode가 이미 define 되어 있으면 다음 모드를 보고
		   그렇지 않으면 추가 작업을 한다. */
		if (mode_defined(VIDEO_FIRST_BIOS+mode))
			continue;
		
		/* set_bios_mode()가 성공하면 추가작업을 하고
		   실패 하면 다음 모드를 set한다.*/
		if (set_bios_mode(mode))
			continue;

		/* Try to verify that it's a text mode. */

		/* http://wiki.osdev.org/VGA_Hardware 를 참고 하여 다음을 찾아내었다. */
		/* Attribute Controller: make graphics controller disabled */
		/* Mode Control: mode 3h (80x25 text mode) 일 때는 추가 작업을 한다.*/ 
		if (in_idx(0x3c0, 0x10) & 0x01)
			continue;

		/* Graphics Controller: verify Alpha addressing enabled */
		/* Miscellaneous Register: mode 3h (80x25 text mode) 일 때는 추가 작업을 한다.*/
		if (in_idx(0x3ce, 0x06) & 0x01)
			continue;

		/* CRTC cursor location low should be zero(?) */
		/* 0x3d4-0x0f는 low커서의 위치를 가져온다.
		   0x3d4-0x0e는 high커서의 위치를 가져온다.
		   커서의 low byte가 0이면 추가작업을 한다.
		 */
		if (in_idx(crtc, 0x0f))
			continue;

		mi = GET_HEAP(struct mode_info, 1);
		mi->mode = VIDEO_FIRST_BIOS+mode;
		mi->depth = 0;	/* text */
		/* 해상도를 읽어 온다. */
		mi->x = rdfs16(0x44a);
		mi->y = rdfs8(0x484)+1;
		nmodes++;
	}
	/* 0x03 80x25 16color text 로 설정한다. (_WAKEUP이 define되어 있다면.) */
	set_bios_mode(saved_mode);

	return nmodes;
}

static __videocard video_bios =
{
	.card_name	= "BIOS",
	.probe		= bios_probe,
	.set_mode	= bios_set_mode,
	.unsafe		= 1,
	.xmode_first	= VIDEO_FIRST_BIOS,
	.xmode_n	= 0x80,
};

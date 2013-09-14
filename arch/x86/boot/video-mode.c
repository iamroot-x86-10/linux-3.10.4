/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007-2008 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/i386/boot/video-mode.c
 *
 * Set the video mode.  This is separated out into a different
 * file in order to be shared with the ACPI wakeup code.
 */

#include "boot.h"
#include "video.h"
#include "vesa.h"

/*
 * Common variables
 */
int adapter;			/* 0=CGA/MDA/HGC, 1=EGA, 2=VGA+ */
u16 video_segment;
int force_x, force_y;	/* Don't query the BIOS for cols/rows */

int do_restore;		/* Screen contents changed during mode flip */
int graphic_mode;	/* Graphic mode with linear frame buffer */

/* Probe the video drivers and have them generate their mode lists. */
void probe_cards(int unsafe)
{
	struct card_info *card;
	static u8 probed[2]; //!!unsafe = 0 or 1값으로 지정
	/* unsafe probe와 safe probe의 차이점을 모르고 있다. */

	/* 다음에 다시 호출되면 실행하지 않음 */
	if (probed[unsafe])
		return;

	probed[unsafe] = 1;

	/* video card 정보를 다 가지고 있는 section
	 * .videocards (setup.ld 참조)에 card 정보를 배열로 가지고 있음
	 * 여기에서 card 정보를 하나씩 가져와서, mode를 조사함 
	 * unsafe = 0 인 경우, vga, vesa 방식(video-vga.c, video-vesa.c)의 video_card를 조사
	 * unsafe = 1 인 경우, bios 방식(video-biods.c) 의 vide_card를 조사
	 */
	for (card = video_cards; card < video_cards_end; card++) {
		if (card->unsafe == unsafe) {
			if (card->probe)
				/* 
				 * VGA vide card의 경우: CGA = 1, EGA = 2, VGA = 7
				 */
				card->nmodes = card->probe();
			else
				card->nmodes = 0;
		}
	}
}

/* Test if a mode is defined */
int mode_defined(u16 mode)
{
	struct card_info *card;
	struct mode_info *mi;
	int i;

	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			if (mi->mode == mode)
				return 1;
		}
	}

	return 0;
}

/* Set mode (without recalc) */
static int raw_set_mode(u16 mode, u16 *real_mode)
{
	int nmode, i;
	struct card_info *card;
	struct mode_info *mi;

	/* Drop the recalc bit if set */

   /* From Documentaion/svga.txt
	  If you add 0x8000 to the mode ID, the program will try to recalculate
vertical display timing according to mode parameters, which can be used to
eliminate some annoying bugs of certain VGA BIOSes (usually those used for
cards with S3 chipsets and old Cirrus Logic BIOSes) -- mainly extra lines at the
end of the display.*/
	mode &= ~VIDEO_RECALC;  // ~VIDEO_RECALC => 0x7FFF

	/* Scan for mode based on fixed ID, position, or resolution */
	nmode = 0;
	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			int visible = mi->x || mi->y;

			/* 모드 값은 3가지 형태로 저장되어 있는데
			   그중 하나만 걸리면 set_mode()를 호출하고 리턴한다. */
			if ((mode == nmode && visible) ||	// 사용자가 선택한 번호를 모드로 설정한 경우
			    mode == mi->mode ||				// 기본 모드 설정
			    mode == (mi->y << 8)+mi->x) {	//VESA가 모드를 이렇게 설정하였다.
				*real_mode = mi->mode;
				return card->set_mode(mi);
			}

			if (visible)
				nmode++;
		}
	}

	/* Nothing found?  Is it an "exceptional" (unprobed) mode? */
	for (card = video_cards; card < video_cards_end; card++) {
		if (mode >= card->xmode_first &&
		    mode < card->xmode_first+card->xmode_n) {
			struct mode_info mix;
			*real_mode = mix.mode = mode;
			mix.x = mix.y = 0;
			return card->set_mode(&mix);
		}
	}

	/* Otherwise, failure... */
	return -1;
}

/*
 * Recalculate the vertical video cutoff (hack!)
 */
static void vga_recalc_vertical(void)
{
	unsigned int font_size, rows;
	u16 crtc;
	u8 pt, ov;

	set_fs(0);
	font_size = rdfs8(0x485); /* BIOS: font size (pixels) */
	rows = force_y ? force_y : rdfs8(0x484)+1; /* Text rows */

	rows *= font_size;	/* Visible scan lines */
	rows--;			/* ... minus one */

	crtc = vga_crtc(); //0x3d4 로 예상하고 있다.

	pt = in_idx(crtc, 0x11); //verical retrace end: 
	pt &= ~0x80;		/* Unlock CR0-7 */   //7F
	out_idx(pt, crtc, 0x11);

	out_idx((u8)rows, crtc, 0x12); /* Lower height register */

	ov = in_idx(crtc, 0x07); /* Overflow register */
	ov &= 0xbd;
	ov |= (rows >> (8-1)) & 0x02;
	ov |= (rows >> (9-6)) & 0x40;
	out_idx(ov, crtc, 0x07);
}

/* Set mode (with recalc if specified) */
int set_mode(u16 mode)
{
	int rv;
	u16 real_mode;

	/* Very special mode numbers... */
	if (mode == VIDEO_CURRENT_MODE)
		return 0;	/* Nothing to do... */
	else if (mode == NORMAL_VGA)
		mode = VIDEO_80x25;
	else if (mode == EXTENDED_VGA)
		mode = VIDEO_8POINT;
	
	/* 0 이면 성공, -1이면 실패*/
	rv = raw_set_mode(mode, &real_mode);
	if (rv)
		return rv;

	if (mode & VIDEO_RECALC)
		vga_recalc_vertical();

	/* Save the canonical mode number for the kernel, not
	   an alias, size specification or menu position */
#ifndef _WAKEUP
	boot_params.hdr.vid_mode = real_mode;
#endif
	return 0;
}

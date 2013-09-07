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
 * Get EDD BIOS disk information
 */

#include "boot.h"
#include <linux/edd.h>

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)

/*
 * Read the MBR (first sector) from a specific device.
 */
static int read_mbr(u8 devno, void *buf)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ax = 0x0201;		/* Legacy Read, one sector */
	ireg.cx = 0x0001;		/* Sector 0-0-1 */
	ireg.dl = devno;
	ireg.bx = (size_t)buf;

	intcall(0x13, &ireg, &oreg);

	return -(oreg.eflags & X86_EFLAGS_CF); /* 0 or -1 */
}

static u32 read_mbr_sig(u8 devno, struct edd_info *ei, u32 *mbrsig)
{
	int sector_size;
	char *mbrbuf_ptr, *mbrbuf_end;
	u32 buf_base, mbr_base;
	extern char _end[];
	u16 mbr_magic;

	sector_size = ei->params.bytes_per_sector;
	if (!sector_size)
		sector_size = 512; /* Best available guess */

	/* Produce a naturally aligned buffer on the heap */
	/* 실제 커널의 끝값이 된다. */
	/* (u32)&_end 에서 '&'는 주소확장때문에 사용*/
	buf_base = (ds() << 4) + (u32)&_end;
	/* _end는 커널의 끝을 의미한다. */
	mbr_base = (buf_base+sector_size-1) & ~(sector_size-1);	// 셋터사이즈로 자르는 것이다. & 0xFE00
	mbrbuf_ptr = _end + (mbr_base-buf_base);
	mbrbuf_end = mbrbuf_ptr + sector_size;

	/* Make sure we actually have space on the heap... */
	if (!(boot_params.hdr.loadflags & CAN_USE_HEAP))
		return -1;
	if (mbrbuf_end > (char *)(size_t)boot_params.hdr.heap_end_ptr)
		return -1;

	memset(mbrbuf_ptr, 0, sector_size);
	if (read_mbr(devno, mbrbuf_ptr))
		return -1;

	*mbrsig = *(u32 *)&mbrbuf_ptr[EDD_MBR_SIG_OFFSET];
	mbr_magic = *(u16 *)&mbrbuf_ptr[510];

	/* check for valid MBR magic */
	return mbr_magic == 0xAA55 ? 0 : -1;
}

static int get_edd_info(u8 devno, struct edd_info *ei)
{
	struct biosregs ireg, oreg;

	memset(ei, 0, sizeof *ei);

	/* Check Extensions Present */
	initregs(&ireg);
	ireg.ah = 0x41;
	ireg.bx = EDDMAGIC1;	//0x55aa가 들어간다.
	ireg.dl = devno;
	intcall(0x13, &ireg, &oreg);
	/* CX에 1 - Device Access using the packet structre
	   		2 - Drive Locking and Ejecting
	 		4 - Enhanced Disk Drive Support (EDD)*/

	/* Set On Not Present, Clear If Present*/
	/* Extensions Present를 지원여부를 판단한다.*/
	if (oreg.eflags & X86_EFLAGS_CF)
		return -1;	/* No extended information */

	/* 성공했다면 0xAA5H가 들어와야 된다.*/
	if (oreg.bx != EDDMAGIC2)
		return -1;

	ei->device  = devno;
	ei->version = oreg.ah;		 /* EDD version number */
	ei->interface_support = oreg.cx; /* EDD functionality subsets */

	/* Extended Get Device Parameters */
	/* http://en.wikipedia.org/wiki/INT_13H */
	ei->params.length = sizeof(ei->params);
	ireg.ah = 0x48;
	ireg.si = (size_t)&ei->params;
	intcall(0x13, &ireg, &oreg);

	/* Get legacy CHS parameters */

	/* Ralf Brown recommends setting ES:DI to 0:0 */
	ireg.ah = 0x08;
	ireg.es = 0;
	intcall(0x13, &ireg, &oreg);

	/* CF 설정되어 있지않으면 성공한것이다.*/
	if (!(oreg.eflags & X86_EFLAGS_CF)) {
		/* CX= [7:6] [15:8]logical last index of cylinders = number_of - 1 (because index starts with 0)
		       [5:0] logical last index of sectors per track = number_of (because index starts with 1) */
		/* cylinder 개수*/
		ei->legacy_max_cylinder = oreg.ch + ((oreg.cl & 0xc0) << 2);
		/* header의 개수 */
		ei->legacy_max_head = oreg.dh;
		/* sector 개수 */
		ei->legacy_sectors_per_track = oreg.cl & 0x3f;
	}

	return 0;
}

// edd : Enhanced Disk Drive
/* int 13h의 특정영역이 EDD관련 영역이다. 
   BIOS에서 EDD를 지원할 수 있는지 확인할 수 있게
   int 13h을 이용하고 제공할 것이다. */
void query_edd(void)
{
	char eddarg[8];
	int do_mbr = 1;
#ifdef CONFIG_EDD_OFF
	int do_edd = 0;
#else
	int do_edd = 1;
#endif
	int be_quiet;
	int devno;
	struct edd_info ei, *edp;
	u32 *mbrptr;
	
	/* default 로 do_edd =1, do_mbr=1일 거 같다.*/
	if (cmdline_find_option("edd", eddarg, sizeof eddarg) > 0) {
		if (!strcmp(eddarg, "skipmbr") || !strcmp(eddarg, "skip")) {
			/* skip 이 있으면 mbr을 안본다.*/
			do_edd = 1;
			do_mbr = 0;
		}	/* on/off는 edd의 사용여부이다.*/
		else if (!strcmp(eddarg, "off"))
			do_edd = 0;
		else if (!strcmp(eddarg, "on"))
			do_edd = 1;
	}

	// debug 메시지가 보이지 않는다.
	be_quiet = cmdline_find_option_bool("quiet");

	edp    = boot_params.eddbuf;	//struct edd_info * 6개
	mbrptr = boot_params.edd_mbr_sig_buffer; //__u32 * 16 개

	if (!do_edd)
		return;

	/* Bugs in OnBoard or AddOnCards Bios may hang the EDD probe,
	 * so give a hint if this happens.
	 */

	if (!be_quiet)
		printf("Probing EDD (edd=off to disable)... ");

	/* EDD_MBAR_SIG_MAX = 16 */
	/* 0x80 first HDD위치의 devno 가 0x80 */
	/* partition 된 개수 만클 루프를 돈다.? */
	/* devno는 물리적인 HDD 인가? 실제로 물리적인 HDD는 16개까지 안된다. */
	/* 결론:  mbr이 최대 16개까지만 지원한다. HDD 개수, partition 개수 상관없이*/
	for (devno = 0x80; devno < 0x80+EDD_MBR_SIG_MAX; devno++) {
		/*
		 * Scan the BIOS-supported hard disks and query EDD
		 * information...
		 */
		if (!get_edd_info(devno, &ei)
		    && boot_params.eddbuf_entries < EDDMAXNR) {
			memcpy(edp, &ei, sizeof ei);
			edp++;
			boot_params.eddbuf_entries++;
		}

		if (do_mbr && !read_mbr_sig(devno, &ei, mbrptr++))
			boot_params.edd_mbr_sig_buf_entries = devno-0x80+1;
	}

	if (!be_quiet)
		printf("ok\n");
}

#endif

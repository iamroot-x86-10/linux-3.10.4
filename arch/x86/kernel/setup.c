/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *
 *  Memory region support
 *	David Parsons <orc@pell.chi.il.us>, July-August 1999
 *
 *  Added E820 sanitization routine (removes overlapping memory regions);
 *  Brian Moyle <bmoyle@mvista.com>, February 2001
 *
 * Moved CPU detection code to cpu/${cpu}.c
 *    Patrick Mochel <mochel@osdl.org>, March 2002
 *
 *  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *  Alex Achenbach <xela@slit.de>, December 2002.
 *
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/sfi.h>
#include <linux/apm_bios.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/edd.h>
#include <linux/iscsi_ibft.h>
#include <linux/nodemask.h>
#include <linux/kexec.h>
#include <linux/dmi.h>
#include <linux/pfn.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <linux/init_ohci1394_dma.h>
#include <linux/kvm_para.h>
#include <linux/dma-contiguous.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/delay.h>

#include <linux/kallsyms.h>
#include <linux/cpufreq.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>

#include <linux/percpu.h>
#include <linux/crash_dump.h>
#include <linux/tboot.h>
#include <linux/jiffies.h>

#include <video/edid.h>

#include <asm/mtrr.h>
#include <asm/apic.h>
#include <asm/realmode.h>
#include <asm/e820.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/efi.h>
#include <asm/timer.h>
#include <asm/i8259.h>
#include <asm/sections.h>
#include <asm/io_apic.h>
#include <asm/ist.h>
#include <asm/setup_arch.h>
#include <asm/bios_ebda.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/bugs.h>

#include <asm/vsyscall.h>
#include <asm/cpu.h>
#include <asm/desc.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>

#include <asm/paravirt.h>
#include <asm/hypervisor.h>
#include <asm/olpc_ofw.h>

#include <asm/percpu.h>
#include <asm/topology.h>
#include <asm/apicdef.h>
#include <asm/amd_nb.h>
#include <asm/mce.h>
#include <asm/alternative.h>
#include <asm/prom.h>

/*
 * max_low_pfn_mapped: highest direct mapped pfn under 4GB
 * max_pfn_mapped:     highest direct mapped pfn over 4GB
 *
 * The direct mapping only covers E820_RAM regions, so the ranges and gaps are
 * represented by pfn_mapped
 */
unsigned long max_low_pfn_mapped;
unsigned long max_pfn_mapped;

#ifdef CONFIG_DMI
RESERVE_BRK(dmi_alloc, 65536);
#endif


static __initdata unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end = (unsigned long)__brk_base;

#ifdef CONFIG_X86_64
int default_cpu_present_to_apicid(int mps_cpu)
{
	return __default_cpu_present_to_apicid(mps_cpu);
}

int default_check_phys_apicid_present(int phys_apicid)
{
	return __default_check_phys_apicid_present(phys_apicid);
}
#endif

struct boot_params boot_params;

/*
 * Machine setup..
 */
static struct resource data_resource = {
	.name	= "Kernel data",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};


#ifdef CONFIG_X86_32
/* cpu data as detected by the assembly code in head.S */
struct cpuinfo_x86 new_cpu_data __cpuinitdata = {
	.wp_works_ok = -1,
};
/* common cpu data for all cpus */
struct cpuinfo_x86 boot_cpu_data __read_mostly = {
	.wp_works_ok = -1,
};
EXPORT_SYMBOL(boot_cpu_data);

unsigned int def_to_bigsmp;

/* for MCA, but anyone else can use it if they want */
unsigned int machine_id;
unsigned int machine_submodel_id;
unsigned int BIOS_revision;

struct apm_info apm_info;
EXPORT_SYMBOL(apm_info);

#if defined(CONFIG_X86_SPEEDSTEP_SMI) || \
	defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
struct ist_info ist_info;
EXPORT_SYMBOL(ist_info);
#else
struct ist_info ist_info;
#endif

#else
struct cpuinfo_x86 boot_cpu_data __read_mostly = {
	.x86_phys_bits = MAX_PHYSMEM_BITS,
};
EXPORT_SYMBOL(boot_cpu_data);
#endif


#if !defined(CONFIG_X86_PAE) || defined(CONFIG_X86_64)
unsigned long mmu_cr4_features;
#else
unsigned long mmu_cr4_features = X86_CR4_PAE;
#endif

/* Boot loader ID and version as integers, for the benefit of proc_dointvec */
int bootloader_type, bootloader_version;

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);

extern int root_mountflags;

unsigned long saved_video_mode;

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE];
#ifdef CONFIG_CMDLINE_BOOL
static char __initdata builtin_cmdline[COMMAND_LINE_SIZE] = CONFIG_CMDLINE;
#endif

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
/**
 * copy_edd() - Copy the BIOS EDD information
 *              from boot_params into a safe place.
 *
 */
static inline void __init copy_edd(void)
{
	memcpy(edd.mbr_signature, boot_params.edd_mbr_sig_buffer,
			sizeof(edd.mbr_signature));
	memcpy(edd.edd_info, boot_params.eddbuf, sizeof(edd.edd_info));
	edd.mbr_signature_nr = boot_params.edd_mbr_sig_buf_entries;
	edd.edd_info_nr = boot_params.eddbuf_entries;
}
#else
static inline void __init copy_edd(void)
{
}
#endif

void * __init extend_brk(size_t size, size_t align)
{
	size_t mask = align - 1;
	void *ret;

	BUG_ON(_brk_start == 0);
	BUG_ON(align & mask);

	_brk_end = (_brk_end + mask) & ~mask;
	BUG_ON((char *)(_brk_end + size) > __brk_limit);

	ret = (void *)_brk_end;
	_brk_end += size;

	memset(ret, 0, size);

	return ret;
}

#ifdef CONFIG_X86_32
static void __init cleanup_highmap(void)
{
}
#endif

static void __init reserve_brk(void)
{
	if (_brk_end > _brk_start)
		memblock_reserve(__pa_symbol(_brk_start),
				_brk_end - _brk_start);

	/* Mark brk area as locked down and no longer taking any
	   new allocations */
	_brk_start = 0;
}

#ifdef CONFIG_BLK_DEV_INITRD

static u64 __init get_ramdisk_image(void)
{
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;

	ramdisk_image |= (u64)boot_params.ext_ramdisk_image << 32;

	return ramdisk_image;
}
static u64 __init get_ramdisk_size(void)
{
	u64 ramdisk_size = boot_params.hdr.ramdisk_size;

	ramdisk_size |= (u64)boot_params.ext_ramdisk_size << 32;

	return ramdisk_size;
}

#define MAX_MAP_CHUNK	(NR_FIX_BTMAPS << PAGE_SHIFT)
static void __init relocate_initrd(void)
{
	/* Assume only end is not page aligned */
	u64 ramdisk_image = get_ramdisk_image();
	u64 ramdisk_size  = get_ramdisk_size();
	u64 area_size     = PAGE_ALIGN(ramdisk_size);
	u64 ramdisk_here;
	unsigned long slop, clen, mapaddr;
	char *p, *q;

	/* We need to move the initrd down into directly mapped mem */
	ramdisk_here = memblock_find_in_range(0, PFN_PHYS(max_pfn_mapped),
			area_size, PAGE_SIZE);

	if (!ramdisk_here)
		panic("Cannot find place for new RAMDISK of size %lld\n",
				ramdisk_size);

	/* Note: this includes all the mem currently occupied by
	   the initrd, we rely on that fact to keep the data intact. */
	memblock_reserve(ramdisk_here, area_size);
	initrd_start = ramdisk_here + PAGE_OFFSET;
	initrd_end   = initrd_start + ramdisk_size;
	printk(KERN_INFO "Allocated new RAMDISK: [mem %#010llx-%#010llx]\n",
			ramdisk_here, ramdisk_here + ramdisk_size - 1);

	q = (char *)initrd_start;

	/* Copy the initrd */
	while (ramdisk_size) {
		slop = ramdisk_image & ~PAGE_MASK;
		clen = ramdisk_size;
		if (clen > MAX_MAP_CHUNK-slop)
			clen = MAX_MAP_CHUNK-slop;
		mapaddr = ramdisk_image & PAGE_MASK;
		p = early_memremap(mapaddr, clen+slop);
		memcpy(q, p+slop, clen);
		early_iounmap(p, clen+slop);
		q += clen;
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}

	ramdisk_image = get_ramdisk_image();
	ramdisk_size  = get_ramdisk_size();
	printk(KERN_INFO "Move RAMDISK from [mem %#010llx-%#010llx] to"
			" [mem %#010llx-%#010llx]\n",
			ramdisk_image, ramdisk_image + ramdisk_size - 1,
			ramdisk_here, ramdisk_here + ramdisk_size - 1);
}

static void __init early_reserve_initrd(void)
{
	/* Assume only end is not page aligned */
	u64 ramdisk_image = get_ramdisk_image();
	u64 ramdisk_size  = get_ramdisk_size();
	u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);

	if (!boot_params.hdr.type_of_loader ||
			!ramdisk_image || !ramdisk_size)
		return;		/* No initrd provided by bootloader */

	memblock_reserve(ramdisk_image, ramdisk_end - ramdisk_image);
}
static void __init reserve_initrd(void)
{
	/* Assume only end is not page aligned */
	/* reserve_early_initrd에서 memblock_reserve를 통해서 ramdisk 영역을 잡아 놓은 
	 * 영역의 위치와 size를 가져온다.
	 * ramdisk_image : 0x1fa04000
	 * ramdisk_size : [5ec000]
	 * ramdisk_end : 0x1fff0000
	 */
	u64 ramdisk_image = get_ramdisk_image();
	u64 ramdisk_size  = get_ramdisk_size();
	u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);
	u64 mapped_size;

	if (!boot_params.hdr.type_of_loader ||
			!ramdisk_image || !ramdisk_size)
		return;		/* No initrd provided by bootloader */

	initrd_start = 0;

	mapped_size = memblock_mem_size(max_pfn_mapped);
	if (ramdisk_size >= (mapped_size>>1))
		panic("initrd too large to handle, "
				"disabling initrd (%lld needed, %lld available)\n",
				ramdisk_size, mapped_size>>1);

	printk(KERN_INFO "RAMDISK: [mem %#010llx-%#010llx]\n", ramdisk_image,
			ramdisk_end - 1);

	/* 이미 ramdisk가 존재 하는 경우 그 영역을 사용하고, 
	 * 아닌 경우 새로 memblock에 영역을 잡아서 (relocate_initrd())
	 * 원래 ramdisk의 영역을  제거한다. 
	 */
	if (pfn_range_is_mapped(PFN_DOWN(ramdisk_image),
				PFN_DOWN(ramdisk_end))) {
		/* All are mapped, easy case */
		initrd_start = ramdisk_image + PAGE_OFFSET;
		initrd_end = initrd_start + ramdisk_size;
		return;
	}

	relocate_initrd();

	memblock_free(ramdisk_image, ramdisk_end - ramdisk_image);
}
#else
static void __init early_reserve_initrd(void)
{
}
static void __init reserve_initrd(void)
{
}
#endif /* CONFIG_BLK_DEV_INITRD */

static void __init parse_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;
	/*
	 * arch/x86/boot/header.S LINE 418
	 * boot_params.hdr.setup_data: # 64-bit physical pointer to 
	 * 								single linked list of struct setup_data
	 */
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		u32 data_len, map_len;

		/*  PAGE_SIZE - (pa_data & ((PAGE_SIZE-1)) */
		/*  1 0000 0000 0000 - (pa_data & (0000 1111 1111 1111) */
		/*  1 0000 0000 0000 - (pa_data */
		map_len = max(PAGE_SIZE - (pa_data & ~PAGE_MASK),
				(u64)sizeof(struct setup_data));

		/*
		 * pa_data는 physical memory 영역이므로 
		 * fix_bitmap에 페이지 테이블을 작성해두었다. 
		 * data는 pa_data의 virtual address이다.
		 */
		data = early_memremap(pa_data, map_len);

		/*
		 * data가 NULL로 리턴되면???
		 */
		data_len = data->len + sizeof(struct setup_data);
		if (data_len > map_len) {
			early_iounmap(data, map_len);
			data = early_memremap(pa_data, data_len);
			map_len = data_len;
		}

		switch (data->type) {
			/*
			 * EFI가 설정되어 있으면 128개 이상의 e820 entry를
			 * 설정할 수 있다. 실제 bootparam에서는 max 128개의
			 * entry만 저장한다. 
			 * 따라서 setup_memory_map()에서 bootparam.e820_map에 대한처리를
			 * 하고 그이상의 entry여 이 함수에서 처리한다. 
			 */
			case SETUP_E820_EXT:
				parse_e820_ext(data);
				break;
				/*
				 * Device Tree Binary
				 */
			case SETUP_DTB:
				add_dtb(pa_data);
				break;
			default:
				/*
				 * PCI와관련된 map정보는 E820에 포함되었다고 판단된다. 
				 */
				break;
		}
		pa_data = data->next;
		early_iounmap(data, map_len);
	}
}
/*
 * setup_data list의 메모리 위치리를
 * 커널이 사용하는 영역이라 표시하는 함수
 */
static void __init e820_reserve_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;
	int found = 0;

	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		e820_update_range(pa_data, sizeof(*data)+data->len,
				/*
				 * 커널에서만 사용하도록 예약한다. 
				 */
				E820_RAM, E820_RESERVED_KERN);
		found = 1;
		pa_data = data->next;
		early_iounmap(data, sizeof(*data));
	}
	if (!found)
		return;

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
	memcpy(&e820_saved, &e820, sizeof(struct e820map));
	printk(KERN_INFO "extended physical RAM map:\n");
	e820_print_map("reserve setup_data");
}

static void __init memblock_x86_reserve_range_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;

	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		memblock_reserve(pa_data, sizeof(*data) + data->len);
		pa_data = data->next;
		early_iounmap(data, sizeof(*data));
	}
}

/*
 * --------- Crashkernel reservation ------------------------------
 */

#ifdef CONFIG_KEXEC

/*
 * Keep the crash kernel below this limit.  On 32 bits earlier kernels
 * would limit the kernel to the low 512 MiB due to mapping restrictions.
 * On 64bit, old kexec-tools need to under 896MiB.
 */
#ifdef CONFIG_X86_32
# define CRASH_KERNEL_ADDR_LOW_MAX	(512 << 20)
# define CRASH_KERNEL_ADDR_HIGH_MAX	(512 << 20)
#else
# define CRASH_KERNEL_ADDR_LOW_MAX	(896UL<<20)
# define CRASH_KERNEL_ADDR_HIGH_MAX	MAXMEM
#endif

static void __init reserve_crashkernel_low(void)
{
#ifdef CONFIG_X86_64
	const unsigned long long alignment = 16<<20;	/* 16M */
	unsigned long long low_base = 0, low_size = 0;
	unsigned long total_low_mem;
	unsigned long long base;
	bool auto_set = false;
	int ret;

	total_low_mem = memblock_mem_size(1UL<<(32-PAGE_SHIFT));
	/* crashkernel=Y,low */
	ret = parse_crashkernel_low(boot_command_line, total_low_mem,
			&low_size, &base);
	if (ret != 0) {
		/*
		 * two parts from lib/swiotlb.c:
		 *	swiotlb size: user specified with swiotlb= or default.
		 *	swiotlb overflow buffer: now is hardcoded to 32k.
		 *		We round it to 8M for other buffers that
		 *		may need to stay low too.
		 */
		low_size = swiotlb_size_or_default() + (8UL<<20);
		auto_set = true;
	} else {
		/* passed with crashkernel=0,low ? */
		if (!low_size)
			return;
	}

	low_base = memblock_find_in_range(low_size, (1ULL<<32),
			low_size, alignment);

	if (!low_base) {
		if (!auto_set)
			pr_info("crashkernel low reservation failed - No suitable area found.\n");

		return;
	}

	memblock_reserve(low_base, low_size);
	pr_info("Reserving %ldMB of low memory at %ldMB for crashkernel (System low RAM: %ldMB)\n",
			(unsigned long)(low_size >> 20),
			(unsigned long)(low_base >> 20),
			(unsigned long)(total_low_mem >> 20));
	crashk_low_res.start = low_base;
	crashk_low_res.end   = low_base + low_size - 1;
	insert_resource(&iomem_resource, &crashk_low_res);
#endif
}

/*
 * reserve_crashkernel
 * 
 */
static void __init reserve_crashkernel(void)
{
	const unsigned long long alignment = 16<<20;	/* 16M */
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	bool high = false;
	int ret;

	total_mem = memblock_phys_mem_size();

	/* crashkernel=XM */
	ret = parse_crashkernel(boot_command_line, total_mem,
			&crash_size, &crash_base);
	if (ret != 0 || crash_size <= 0) {
		/* crashkernel=X,high */
		ret = parse_crashkernel_high(boot_command_line, total_mem,
				&crash_size, &crash_base);
		if (ret != 0 || crash_size <= 0)
			return;
		high = true;
	}

	/* 0 means: find the address automatically */
	if (crash_base <= 0) {
		/*
		 *  kexec want bzImage is below CRASH_KERNEL_ADDR_MAX
		 */
		crash_base = memblock_find_in_range(alignment,
				high ? CRASH_KERNEL_ADDR_HIGH_MAX :
				CRASH_KERNEL_ADDR_LOW_MAX,
				crash_size, alignment);

		if (!crash_base) {
			pr_info("crashkernel reservation failed - No suitable area found.\n");
			return;
		}

	} else {
		unsigned long long start;

		start = memblock_find_in_range(crash_base,
				crash_base + crash_size, crash_size, 1<<20);
		if (start != crash_base) {
			pr_info("crashkernel reservation failed - memory is in use.\n");
			return;
		}
	}
	memblock_reserve(crash_base, crash_size);

	printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
			"for crashkernel (System RAM: %ldMB)\n",
			(unsigned long)(crash_size >> 20),
			(unsigned long)(crash_base >> 20),
			(unsigned long)(total_mem >> 20));

	crashk_res.start = crash_base;
	crashk_res.end   = crash_base + crash_size - 1;
	insert_resource(&iomem_resource, &crashk_res);

	if (crash_base >= (1ULL<<32))
		reserve_crashkernel_low();
}
#else
static void __init reserve_crashkernel(void)
{
}
#endif

static struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1", .start = 0x20, .end = 0x21,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0", .start = 0x40, .end = 0x43,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1", .start = 0x50, .end = 0x53,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x60, .end = 0x60,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x64, .end = 0x64,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma page reg", .start = 0x80, .end = 0x8f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic2", .start = 0xa0, .end = 0xa1,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma2", .start = 0xc0, .end = 0xdf,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "fpu", .start = 0xf0, .end = 0xff,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO }
};

void __init reserve_standard_io_resources(void)
{
	int i;

	/* request I/O space for devices used on all i[345]86 PCs */
	// standard_io_resources[i] 중에, ioport_resource랑 충돌나지 않는
	// 모든 resource들을 추가한다.
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);

}

static __init void reserve_ibft_region(void)
{
	unsigned long addr, size = 0;

	addr = find_ibft_region(&size);

	if (size)
		memblock_reserve(addr, size);
}

static bool __init snb_gfx_workaround_needed(void)
{
#ifdef CONFIG_PCI
	int i;
	u16 vendor, devid;
	static const __initconst u16 snb_ids[] = {
		0x0102,
		0x0112,
		0x0122,
		0x0106,
		0x0116,
		0x0126,
		0x010a,
	};

	/* Assume no if something weird is going on with PCI */
	if (!early_pci_allowed())
		return false;

	vendor = read_pci_config_16(0, 2, 0, PCI_VENDOR_ID);
	if (vendor != 0x8086)
		return false;

	devid = read_pci_config_16(0, 2, 0, PCI_DEVICE_ID);
	for (i = 0; i < ARRAY_SIZE(snb_ids); i++)
		if (devid == snb_ids[i])
			return true;
#endif

	return false;
}

/*
 * Sandy Bridge graphics has trouble with certain ranges, exclude
 * them from allocation.
 */
static void __init trim_snb_memory(void)
{
	static const __initconst unsigned long bad_pages[] = {
		0x20050000,
		0x20110000,
		0x20130000,
		0x20138000,
		0x40004000,
	};
	int i;

	if (!snb_gfx_workaround_needed())
		return;

	printk(KERN_DEBUG "reserving inaccessible SNB gfx pages\n");

	/*
	 * Reserve all memory below the 1 MB mark that has not
	 * already been reserved.
	 */
	memblock_reserve(0, 1<<20);

	for (i = 0; i < ARRAY_SIZE(bad_pages); i++) {
		if (memblock_reserve(bad_pages[i], PAGE_SIZE))
			printk(KERN_WARNING "failed to reserve 0x%08lx\n",
					bad_pages[i]);
	}
}

/*
 * Here we put platform-specific memory range workarounds, i.e.
 * memory known to be corrupt or otherwise in need to be reserved on
 * specific platforms.
 *
 * If this gets used more widely it could use a real dispatch mechanism.
 */
static void __init trim_platform_memory_ranges(void)
{
	trim_snb_memory();
}

static void __init trim_bios_range(void)
{
	/*
	 * A special case is the first 4Kb of memory;
	 * This is a BIOS owned area, not kernel ram, but generally
	 * not listed as such in the E820 table.
	 *
	 * This typically reserves additional memory (64KiB by default)
	 * since some BIOSes are known to corrupt low memory.  See the
	 * Kconfig help text for X86_RESERVE_LOW.
	 */
	e820_update_range(0, PAGE_SIZE, E820_RAM, E820_RESERVED);

	/*
	 * special case: Some BIOSen report the PC BIOS
	 * area (640->1Mb) as ram even though it is not.
	 * take them out.
	 */
	e820_remove_range(BIOS_BEGIN, BIOS_END - BIOS_BEGIN, E820_RAM, 1);

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
}

/* called before trim_bios_range() to spare extra sanitize */
static void __init e820_add_kernel_range(void)
{
	u64 start = __pa_symbol(_text);
	u64 size = __pa_symbol(_end) - start;

	/*
	 * Complain if .text .data and .bss are not marked as E820_RAM and
	 * attempt to fix it by adding the range. We may have a confused BIOS,
	 * or the user may have used memmap=exactmap or memmap=xxM$yyM to
	 * exclude kernel range. If we really are running on top non-RAM,
	 * we will crash later anyways.
	 */
	if (e820_all_mapped(start, start + size, E820_RAM))
		return;

	pr_warn(".text .data .bss are not marked as E820_RAM!\n");
	e820_remove_range(start, size, E820_RAM, 0);
	e820_add_region(start, size, E820_RAM);
}

static unsigned reserve_low = CONFIG_X86_RESERVE_LOW << 10;

static int __init parse_reservelow(char *p)
{
	unsigned long long size;

	if (!p)
		return -EINVAL;

	size = memparse(p, &p);

	if (size < 4096)
		size = 4096;

	if (size > 640*1024)
		size = 640*1024;

	reserve_low = size;

	return 0;
}

early_param("reservelow", parse_reservelow);

static void __init trim_low_memory_range(void)
{
	memblock_reserve(0, ALIGN(reserve_low, PAGE_SIZE));
}

/*
 * Determine if we were loaded by an EFI loader.  If so, then we have also been
 * passed the efi memmap, systab, etc., so we should use these data structures
 * for initialization.  Note, the efi init code path is determined by the
 * global efi_enabled. This allows the same kernel image to be used on existing
 * systems (with a traditional BIOS) as well as on EFI systems.
 */
/*
 * setup_arch - architecture-specific boot-time initializations
 *
 * Note: On x86_64, fixmaps are ready for use even before this is called.
 */

void __init setup_arch(char **cmdline_p)
{
	/*
	 * __bss_stop - _text: 커널 사이즈
	 * 커널이 존재하는 구역은 reserve영역으로 지정한다.
	 *
	 * memblock의 reserve는 메모리를 사용해서는 안되는 역역을 명시해둔다.
	 * 아마 뒤에 메모리를 항당할 때 여기를 참조하여 할당가능 영역을 계산할 거같다.
	 */
	memblock_reserve(__pa_symbol(_text),
			(unsigned long)__bss_stop - (unsigned long)_text);

	early_reserve_initrd();

	/*
	 * At this point everything still needed from the boot loader
	 * or BIOS or kernel text should be early reserved or marked not
	 * RAM in e820. All other memory is free game.
	 */

#ifdef CONFIG_X86_32
	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));
	visws_early_detect();

	/*
	 * copy kernel address range established so far and switch
	 * to the proper swapper page table
	 */
	clone_pgd_range(swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	load_cr3(swapper_pg_dir);
	__flush_tlb_all();
#else
	printk(KERN_INFO "Command line: %s\n", boot_command_line);
#endif

	/*
	 * If we have OLPC OFW, we might end up relocating the fixmap due to
	 * reserve_top(), so do this before touching the ioremap area.
	 */
	olpc_ofw_detect();

	early_trap_init();
	early_cpu_init();

	/*
	 * boot-ioremap을 위한 pmd, pte(256)개를새롭게(?) 할당하였다.
	 * boot-ioremap이 뭐하는것인지는 아직 모르겠음
	 *
	 * 256 temporary boot-time mappings, used by early_ioremap(),
	 * before ioremap() is functional.
	 *
	 * slot_virt[] 배열을 설정(FIX_BTMAP_BEGIN ~ FIX_BTMAP_END)
	 *
	 */
	early_ioremap_init();

	setup_olpc_ofw_pgd();

	/*
	 * arch/x86/boot/tool/build.c에서 DEFAULT_ROOT_DEV(0:0)를 설정한다.
	 * 그래서 아마도 ROOT_DEV는 0으로 초기화 될거 같다.
	 */
	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);

	screen_info = boot_params.screen_info;
	edid_info = boot_params.edid_info;
#ifdef CONFIG_X86_32
	apm_info.bios = boot_params.apm_bios_info;
	ist_info = boot_params.ist_info;
	if (boot_params.sys_desc_table.length != 0) {
		machine_id = boot_params.sys_desc_table.table[0];
		machine_submodel_id = boot_params.sys_desc_table.table[1];
		BIOS_revision = boot_params.sys_desc_table.table[2];
	}
#endif
	saved_video_mode = boot_params.hdr.vid_mode;
	/*
	 * Documentation/x86/boot.txt에 boot로드에 따른 type번호를 
	 * 확인 할 수 있다.
	 */
	bootloader_type = boot_params.hdr.type_of_loader;
	if ((bootloader_type >> 4) == 0xe) {
		bootloader_type &= 0xf;			// 0x4
		// 0x15 << 4 -> 0x150
		// booload_type = 0x154 
		bootloader_type |= (boot_params.hdr.ext_loader_type+0x10) << 4;
	}
	bootloader_version  = bootloader_type & 0xf;
	bootloader_version |= boot_params.hdr.ext_loader_ver << 4;

	/*
	 * boot_params.hdr.ram_size에는 ram 사이즈 뿐만 아니라
	 * start, prompt, doload정보가 함께 저장되어 있다.
	 * 그러한 정보를 받아오는데 어떻게 쓰이는지는 다음에 알아보기로한다.
	 */
#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((boot_params.hdr.ram_size & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((boot_params.hdr.ram_size & RAMDISK_LOAD_FLAG) != 0);
#endif
	/*
	 * EFI 그래픽등을 지원하는 발전된 CMOS/BIOS
	 */
#ifdef CONFIG_EFI
	if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
				"EL32", 4)) {
		set_bit(EFI_BOOT, &x86_efi_facility);
	} else if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
				"EL64", 4)) {
		set_bit(EFI_BOOT, &x86_efi_facility);
		set_bit(EFI_64BIT, &x86_efi_facility);
	}

	/*
	 * efi 사용하면 efi를 위한 메모리 영역을
	 * reserve해준다.
	 */
	if (efi_enabled(EFI_BOOT)) 
		efi_memblock_x86_reserve_range();
#endif

	/*
	 * arch/x86/platform/ 폴더 아래에 x86_init을 설정해주는
	 * 부분이 있다.
	 *
	 * arch/x86/kernel/x86_init.c에 default x86_init이 정의되어 있다.
	 */
	x86_init.oem.arch_setup();
	// 12월 14일 분석 끝

	iomem_resource.end = (1ULL << boot_cpu_data.x86_phys_bits) - 1;

	/*
	 * BIOS에서 가저온 physical memory 정보를 바탕으로
	 * 메모리 멥을 setup()한다. e820, e820_saved에 저장된다.
	 */
	setup_memory_map();

	/*
	 * bootparam으로부터 SETUP_E820_EXT, SETUP_DTB에 대한 정보를
	 * 설정한다. 
	 */
	parse_setup_data();

	/* update the e820_saved too */
	e820_reserve_setup_data();

	copy_edd();

	/* 
	 * 01F2/2	ALL	root_flags	If set, the root is mounted readonly
	 * use the "ro" or "rw" options on the comand line insted.
	 *
	 * boot_parmas.hdr.root_flags가 0일 때는 아마도 부트파람에서 "rw"
	 * 가 들어온것으로 예상된다.
	 */
	if (!boot_params.hdr.root_flags) /* root_flgs가 없으면*/
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;

	/* 64k alignment slob space */
	/* malloc 과 brk와의 관계
	 * 참고답변 1.
	 *
	 * brk 시스템콜은 단지 프로세스의 데이터 세그먼트의 영역을 넓혀주는 
	 * 일만 하는 것입니다. malloc 은 C-library에 있는 것이며, 데이터 
	 * 세그먼트의 남는 영역을 잘 활용하여 heap용도로 사용하는 것일 뿐이고, 
	 * 모자랄 경우 데이터 세그먼트를 늘여달라고 커널에 요청하는 것 뿐입니다.
	 *
	 * 즉, 커널 모드에서는 데이터 세그먼트의 증가를 요청 받을 뿐이지, 
	 * 그 늘어난 양이 반드시 새로운 메모리 할당을 위해 사용되리라는 보장이 없습니다.
	 *
	 * 커널에서 malloc을 찾는 것이 혹시 디버깅을 하려는 것에서라면,
	 * LD_PRELOAD를 통해서 malloc 함수가 들어 있는 shared object를 c library보다 
	 * 먼저 읽게 하여 malloc 함수를 override 하는 것이 좋습니다.
	 *
	 * 참고답변 2.
	 * malloc(), calloc(), free() 함수는 C library에 있는 메모리 관리 함수입니다. 
	 * C library의 메모리 관리자는 brk() 시스템 콜을 이용하여 heap이라는 영역을 
	 * 할당받은 후 이 영역을 자신이 직접 관리합니다. 그 영역 안에서 malloc() 같은 
	 * 메모리 할당 요구를 처리합니다. 처음에 어느정도 크기를 미리 heap 용도로 
	 * 받아두었다가 나중에 메모리가 더 필요하게 되는 경우에만 brk()를 호출하여 
	 * heap 영역을 키우게 됩니다. brk()는 heap 영역을 줄이는 용도로도 사용할 수 있지만 
	 * 보통의 C library는 일단 할당받은 heap 영역을 줄이지 않습니다. 
	 * malloc() 함수가 불렸다고 바로 brk() 시스템 콜이 불리는 것은 아닙니다.
	 *
	 */
	init_mm.brk = _brk_end;

	/*
	 * __pa_symbol(_text) = __phys_addr_symbol(__phys_reloc_hide((unsigned long)(_text)))
	 * => __phys_addr_symbol((unsigned long)(_text))
	 * => ((unsigned long)(_text) - __START_KERNEL_map + phys_base)
	 * => (unsigned long)(_text) - 0xffffffff80000000UL + phys_base)
	 */
	code_resource.start = __pa_symbol(_text);
	code_resource.end = __pa_symbol(_etext)-1;
	data_resource.start = __pa_symbol(_etext);
	data_resource.end = __pa_symbol(_edata)-1;
	bss_resource.start = __pa_symbol(__bss_start);
	bss_resource.end = __pa_symbol(__bss_stop)-1;

#ifdef CONFIG_CMDLINE_BOOL
#ifdef CONFIG_CMDLINE_OVERRIDE
	strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
#else
	if (builtin_cmdline[0]) {
		/* append boot loader cmdline to builtin */
		strlcat(builtin_cmdline, " ", COMMAND_LINE_SIZE);
		strlcat(builtin_cmdline, boot_command_line, COMMAND_LINE_SIZE);
		strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	}
#endif
#endif

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	/*
	 * x86_configure_nx() is called before parse_early_param() to detect
	 * whether hardware doesn't support NX (so that the early EHCI debug
	 * console setup can safely call set_fixmap()). It may then be called
	 * again from within noexec_setup() during parsing early parameters
	 * to honor the respective command line option.
	 */
	x86_configure_nx();


	/*
	 * __init과 관련된 setup 함수들이 parse_early_param()에서 동작한다.
	 * */
	parse_early_param();

	x86_report_nx();

	/* after early param, so could get panic from serial */
	memblock_x86_reserve_range_setup_data();
	/* 2014.01.11일 분석 끝*/
	/*
	 * acpi 기능이 제공되며 커널이 사용할 수 있는 상황일 때,
	 * acpi_mps_check()는 0을 리턴한다.
	 */
	if (acpi_mps_check()) {
#ifdef CONFIG_X86_LOCAL_APIC
		disable_apic = 1;
#endif
		/*
		 * boot_cpu_data->x86_capability --> X86_FEATURE_APIC를 세팅
		 * cpu_caps_cleared --> X86_FEATURE_APIC를 세팅
		 */
		setup_clear_cpu_cap(X86_FEATURE_APIC);
	}

#ifdef CONFIG_PCI
	/*
	 * kernel command line에 'earlydump'가 들어있을 경우, pci_early_dump_regs = 1이 세팅
	 */
	if (pci_early_dump_regs)
		early_dump_pci_devices();
#endif

	/*
	 * parse_early_param()에서 e820관련 __init 함수가 동작하고, userdef == 1로 세팅되어,
	 * finish_e820_parsing()에서 다시한번 sanitize_e820_map()을 진행한다.
	 */
	finish_e820_parsing();

	if (efi_enabled(EFI_BOOT))
		efi_init();
	/* 2014.01.18 스터디 완료 */
	/*
	 * DMI: Direct Media Interface
	 *  2004년 이후의 모든 인텔 칩셋들은 
	 *  이 인터페이스를 사용한다. 
	 *  ICH6이후 DMI 인터페이스라고 불려왔지만 인텔은 
	 *  특정한 디바이스들만의 조합만 지원한다
	 */
	dmi_scan_machine();
	dmi_set_dump_stack_arch_desc();

	/*
	 * VMware detection requires dmi to be available, so this
	 * needs to be done after dmi_scan_machine, for the BP.
	 */
	init_hypervisor_platform();
	/*
	 * Extended BIOS Data Area (EBDA)에서 ROM 영역을 read하여
	 * iomem_resource(PCI mem)에 추가한다.
	 *
	 * 0x000C0000  0x000C7FFF  32 KiB (typically)  ROM  Video BIOS
	 * 0x000C8000  0x000EFFFF  160 KiB (typically) ROMs and unusable space Mapped hardware & Misc 
	 * 0x000F0000  0x000FFFFF  64 KiB              ROM  Motherboard BIOS  
	 *
	 * http://blog.daum.net/english_100/80 참조
	 *
	 * struct resource {
	 resource_size_t start;   // start 와 end 는 이 리소스의 범위를 나타냄
	 resource_size_t end;
	 const char *name;
	 unsigned long flags;
	 struct resource *parent, *sibling, *child;
	 };

	 * iomem_resource 는 resource 구조체 형 변수로서 io memory 리소스들의
	 * 트리 구조상 루트 역할을 한다. 리소스를 트리에 삽입하는 방법은
	 * requst_resource(root, new) 함수를 통해 이루어지는데 이 함수는 new 가
	 * root 의 범위 내에 존재하는가를 체크하여 그 법위내에 존재하는 리소스이면
	 * 그 child들과 비교하여 겹치는 것이 없을 경우 child들 사이에 순서에 맞추어
	 * 삽입하여 다른 child 들과 sibling 으로 연결해 준다. 이때 삽이이 성공하면
	 * 0 를 반환하고 new 와 겹치는 child 가 있는 경우 이 child 의 주소를
	 * 반환한다.
	 * 또하나의 삽입함수로 insert_resource(root , new) 함수가 있는데 이는
	 * 사실상 request_resource()  함수를 포함하는 함수이다. 소스코드상
	 * request_resource() 함수는 바로 __request_resource() 함수를 호출함으로써
	 * 임무가 완성되지만 insert_request() 함수는 __request_request() 함수를
	 * 이용해 그다음의 작업을 수행하기 때문이다. 앞에서 __request_resource()
	 * 함수를 수행함에 있어 new 가 겹치는 child를 발견했을 때 단지 그 주소만
	 * 반환하고 끝냈지만 insert_resource() 함수는 그 겹치는 형태가 어떤
	 * 모양인가를 체크해 new 의 범위가 child의 범위를 포함하는 경우에는
	 * 그 child 자리에 new를 삽입하고 new 에 포함되는 child들은 원래있던
	 * 자리에서 빼내 new의 child로 재배치 하는 작업을 수행한다.
	 */

	x86_init.resources.probe_roms();

	/* after parse_early_param, so could debug it */
	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);

	// kernel .text .data .bss e820.map에 E820_RAM type으로 mark
	// 되어 있는지 확인하고 안되어 있는 경우 e820.map에 add한다.
	e820_add_kernel_range();

	/*
	 * e820 update range: 0000000000000000 - 0000000000010000 (usable) ==> (reserved)  
	 * e820 update range: 0000000000000000 - 0000000000001000 (usable) ==> (reserved)  
	 * e820 remove range: 00000000000a0000 - 0000000000100000 (usable)         
	 *
	 * 0 ~ 4KB는 모든 Bios에서 사용하는 영역
	 * 0 ~ 640KB는 X86_RESERVE_LOW config를 이용하여 자신의 Bios에서 사용하는
	 * 영역을 설정할 수 있다. Default는 64KB이다.
	 * 일부 PC Bios에서 Bios 영역을 1MB로 잘못 Report하는 경우 때문에
	 * 640KB ~ 1MB를 사용가능 영역으로 설정한다. 
	 */

	trim_bios_range();
#ifdef CONFIG_X86_32
	if (ppro_with_ram_bug()) {
		e820_update_range(0x70000000ULL, 0x40000ULL, E820_RAM,
				E820_RESERVED);
		sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
		printk(KERN_INFO "fixed physical RAM map:\n");
		e820_print_map("bad_ppro");
	}
#else
	early_gart_iommu_check();
#endif

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	// e820mapdp에서 맨 마지막 Page Frame Number를 찾아온다.
	// max_pfn = last_pfn = 0xdbcf7 <- youngjoo 8GB 기준
	max_pfn = e820_end_of_ram_pfn();

	/* update e820 for memory not covered by WB MTRRs */
	/* MTRR을 초기화 한다. */
	mtrr_bp_init();
	if (mtrr_trim_uncached_memory(max_pfn))
		max_pfn = e820_end_of_ram_pfn();

#ifdef CONFIG_X86_32
	/* max_low_pfn get updated here */
	find_low_pfn_range();
#else
	num_physpages = max_pfn;

	/* 2014.03.01 시작 */
	/* cpu에 x2apic 기능이 있고, enable되어 있다면,
	 * x2apic_preenabled = x2apic_mode = 1; 한다.
	 */
	check_x2apic();

	/* How many end-of-memory variables you have, grandma! */
	/* need this before calling reserve_initrd */
	/* 현재 물리메모리가 4G보다 큰지를 max_pfn을 통해 검사 */
	/* max_low_pfn: 4G미만 주소상에서 e820엔트리중 가장 큰 주소값의 pfn */
	if (max_pfn > (1UL<<(32 - PAGE_SHIFT)))
		max_low_pfn = e820_end_of_low_ram_pfn();
	else
		max_low_pfn = max_pfn;

	high_memory = (void *)__va(max_pfn * PAGE_SIZE - 1) + 1;
#endif

	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();

	/*   */
	reserve_ibft_region();

	early_alloc_pgt_buf();

	/*
	 * Need to conclude brk, before memblock_x86_fill()
	 *  it could use memblock_find_in_range, could overlap with
	 *  brk area.
	 */
	/* 이전 early_alloc_pgt_buf()을 통해 사용한 20K만큼의 공간을 reserve */
	reserve_brk();

	/* pmd중에,
	 * START_KERNEL ~ _text의 영역과,
	 * _brk_end ~ .end까지의 영역을 0으로 초기화.
	 */
	cleanup_highmap();

	//#define ISA_END_ADDRESS		0x100000
	memblock.current_limit = ISA_END_ADDRESS;
	memblock_x86_fill();

	/*
	 * The EFI specification says that boot service code won't be called
	 * after ExitBootServices(). This is, in fact, a lie.
	 */
	//// #define EFI_MEMMAP		4	/* Can we use EFI memory map? */
	if (efi_enabled(EFI_MEMMAP))
		efi_reserve_boot_services();

	/* 2014.03.01 ended. 여기서부터 하시면 되요... */
	/* 2014.03.15 시작 */
	/* preallocate 4k for mptable mpc */
	// mptable을 위한 공간만 생성함.
	early_reserve_e820_mpc_new();

#ifdef CONFIG_X86_CHECK_BIOS_CORRUPTION
	/* 기본 64K이하 주소에서 reserved되지 않은 영역을 찾아 reserve해준뒤에,
	 * scan_area[]에 포함시키고 종료한다. 이 영역은 0으로 초기화되었기 때문에(64K미만 전체영역)
	 * 차후에는 scan_area[]를 통해서 bios나 기타 이유로 64K이하 영역이 변경되는 사항을 찾을 수 있을 것으로 보임
	 */
	setup_bios_corruption_check();
#endif

#ifdef CONFIG_X86_32
	printk(KERN_DEBUG "initial memory mapped: [mem 0x00000000-%#010lx]\n",
			(max_pfn_mapped<<PAGE_SHIFT) - 1);
#endif

	/*
	 * realmode진입을 위한 binary가 메모리에 존재하며,
	 * real_mode_blob ~ real_mode_blob_end. 2개의 symbol로 해당 메모리에 접근할 수 있다.
	 * 해당영역을 reverse 해준다.
	 */
	reserve_real_mode();

	// Sandy Bridge graphics 들 중에 추가 Page를 사용하는 칩을 위한
	// reserve를 해준다.
	trim_platform_memory_ranges();
	trim_low_memory_range();

	/*
	 * 2014.03.15 스터디중 완료못함
	 */
	init_mem_mapping();

	early_trap_pf_init();

	/* AP를 깨우면 real_mode부터 시작한다. secondary_startup_64를 시작할 수 있도록 한다 */
	setup_real_mode();

	memblock.current_limit = get_max_mapped();
	dma_contiguous_reserve(0);

	/*
	 * NOTE: On x86-32, only from this point on, fixmaps are ready for use.
	 */

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	/* firewire 1394 setting x86 */
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif
	/* Allocate bigger log buffer */
	setup_log_buf(1);

	reserve_initrd();

#if defined(CONFIG_ACPI) && defined(CONFIG_BLK_DEV_INITRD)
	acpi_initrd_override((void *)initrd_start, initrd_end - initrd_start);
#endif

	/* KEXEC에서 사용할 memory 를 reserve 한다. */
	reserve_crashkernel();

	/* vSMP 기능을 지원하는 경우, initialze 한다 */
	vsmp_init();

	/* 특정한 DMI(port 0x80 override)를 사용하는 vender(HP)의 경우, io_delay callback을 통해서,
	   io_delay관련 setting을 해준다 */
	io_delay_init();

	/*
	 * Parse the ACPI tables for possible boot-time SMP configuration.
	 * ACPI를 설정한다. 
	 */
	acpi_boot_table_init();

	early_acpi_boot_init();

	/*
	 * NUMA를 사용하지 않는다.
	 * X86에서는 사용하지 않고, itanium architecture에서 사용한다. 
	 */
	initmem_init();
	/*
	 * 16MB (ZONE_DMA) 이하의 영역 중 전체 pfn개수 중에서 free pfn을 빼서 (reserved)
	 * DMA 영역으로 설정한다. 
	 */
	memblock_find_dma_reserve();

#ifdef CONFIG_KVM_GUEST
	kvmclock_init();
#endif

	x86_init.paging.pagetable_init();

	if (boot_cpu_data.cpuid_level >= 0) {
		/* A CPU has %cr4 if and only if it has CPUID */
		mmu_cr4_features = read_cr4();
		// mmu_cr4_features = 0xB0
		// 0000 0000 0000 0000 0000 0000 1011 0000
		// setted PSE = page size extention
		// setted PAE = Physical Address Extension
		// setted PGE = page global enable
		// 2개가 세팅되어있는 상태
		if (trampoline_cr4_features)
			*trampoline_cr4_features = mmu_cr4_features;
	}

#ifdef CONFIG_X86_32
	/* sync back kernel address range */
	clone_pgd_range(initial_page_table + KERNEL_PGD_BOUNDARY,
			swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);
#endif

	// Trusted boot를 지원하는 시스템을 위한 환경설정
	// server는 따로 적용되어있지 않아서 skip함.
	tboot_probe();

#ifdef CONFIG_X86_64
	// vsyscall, vvar를 위한 page table을 생성함
	map_vsyscall();
#endif

	// 64는 없음.skip
	generic_apic_probe();

	// 도영주님꺼.
	early_quirks();

	/*
	 * Read APIC and some other early information from ACPI tables.
	 */
	// 도영주님꺼 part2
	acpi_boot_init();
	sfi_init();
	x86_dtb_init();

	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		get_smp_config();

	// 현재 시스템에 사용가능한 core의 총개수(disable되어있는 것 포함)
	// 를 extern int nr_cpu_ids에 저장
	prefill_possible_map();

	// 잘몰라서 현상만 저장
	// bizaid server desktop
	// cpu  |  apicid  | __apicid_to_node[apicid]
	//  0         0              -1
	//  1         2              -1
	//  아무것도 하는 일이 없음
	//  우리 생각에. NUMA코드를 사용하는 UMA이기 때문에
	//  별도로 apic를 지정할 필요가 없어서 __apicid_to_node[]가 전부 -1로 초기화된 후에
	//  수정된 적이 없다.
	init_cpu_to_node();

	// 도영주님꺼 part3
	init_apic_mappings();
	if (x86_io_apic_ops.init)
		// 일단 타긴탑니다.
		x86_io_apic_ops.init();

	// unsupport KVM
	kvm_guest_init();

	//e820_res에 e820.map[i]를 모두 저장하고,
	//map_entries에 e820_saved.map[i]를 모두 저장한다.
	e820_reserve_resources();
	//안에 설명있음.
	e820_mark_nosave_regions(max_low_pfn);
	
	/* request I/O space for devices used on all i[345]86 PCs */
	// standard_io_resources[i] 중에, ioport_resource랑 충돌나지 않는
	// 모든 resource들을 추가한다.
	x86_init.resources.reserve_resources();

	// PCI를 위한 메모리공간을 검색해서 pci_mem_start에 주소를 할당해준다.
	// 공간을 찾을 수 없을 경우에 max_pfn + 1MiB위치를 pci_mem_start로 사용한다.
	e820_setup_gap();

	
	// CONFIG_VT = Virtual Terminal
#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled(EFI_BOOT) || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
	x86_init.oem.banner();

	// x86 없음
	x86_init.timers.wallclock_init();

	// intel thermal features를 위한 설정
	mcheck_init();

	// cpu별로 nop을 몇번줄건지를 찾아서 설정
	arch_init_ideal_nops();

	// CLOCK_TICK_RATE= #define PIT_TICK_RATE 1193182ul
	// 현재까지 진행된 clock수랑 CLOCK_TICK_RATE를 통해서 현재 시간을 확인한다.
	register_refined_jiffies(CLOCK_TICK_RATE);

#ifdef CONFIG_EFI
	/* Once setup is done above, unmap the EFI memory map on
	 * mismatched firmware/kernel archtectures since there is no
	 * support for runtime services.
	 */
	if (efi_enabled(EFI_BOOT) && !efi_is_native()) {
		pr_info("efi: Setup done, disabling due to 32/64-bit mismatch\n");
		efi_unmap_memmap();
	}
#endif
}

#ifdef CONFIG_X86_32

static struct resource video_ram_resource = {
	.name	= "Video RAM area",
	.start	= 0xa0000,
	.end	= 0xbffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

void __init i386_reserve_resources(void)
{
	request_resource(&iomem_resource, &video_ram_resource);
	reserve_standard_io_resources();
}

#endif /* CONFIG_X86_32 */

#include <linux/gfp.h>
#include <linux/initrd.h>
#include <linux/ioport.h>
#include <linux/swap.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>	/* for max_low_pfn */

#include <asm/cacheflush.h>
#include <asm/e820.h>
#include <asm/init.h>
#include <asm/page.h>
#include <asm/page_types.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/proto.h>
#include <asm/dma.h>		/* for MAX_DMA_PFN */
#include <asm/microcode.h>

#include "mm_internal.h"

static unsigned long __initdata pgt_buf_start;
static unsigned long __initdata pgt_buf_end;
static unsigned long __initdata pgt_buf_top;

static unsigned long min_pfn_mapped;

static bool __initdata can_use_brk_pgt = true;

/*
 * Pages returned are already directly mapped.
 *
 * Changing that is likely to break Xen, see commit:
 *
 *    279b706 x86,xen: introduce x86_init.mapping.pagetable_reserve
 *
 * for detailed information.
 */
/* 
 * page를 num만큼 할당해서 할당한 페이지를 0으로 초기화 한뒤에
 * 시작page 의 virtual address 를 넘겨준다.
 */
__ref void *alloc_low_pages(unsigned int num)
{
	unsigned long pfn;
	int i;

	if (after_bootmem) {
		unsigned int order;

		order = get_order((unsigned long)num << PAGE_SHIFT);
		return (void *)__get_free_pages(GFP_ATOMIC | __GFP_NOTRACK |
						__GFP_ZERO, order);
	}

	//pgt_buf_start=pgt_buf_end = 0x01fda, num = 1 pgt_buf_top = 0x01fdf
	if ((pgt_buf_end + num) > pgt_buf_top || !can_use_brk_pgt) {
		unsigned long ret;
		if (min_pfn_mapped >= max_pfn_mapped)
			panic("alloc_low_page: ran out of memory");
		ret = memblock_find_in_range(min_pfn_mapped << PAGE_SHIFT,
					max_pfn_mapped << PAGE_SHIFT,
					PAGE_SIZE * num , PAGE_SIZE);
		if (!ret)
			panic("alloc_low_page: can not alloc memory");
		memblock_reserve(ret, PAGE_SIZE * num);
		pfn = ret >> PAGE_SHIFT;
	} else {
		//영주꺼 참고용 pfn = 0x01fda, pgt_buf_end + num= 0x01fdb
		pfn = pgt_buf_end;
		pgt_buf_end += num;
		printk(KERN_DEBUG "BRK [%#010lx, %#010lx] PGTABLE\n",
			pfn << PAGE_SHIFT, (pgt_buf_end << PAGE_SHIFT) - 1);
	}

	for (i = 0; i < num; i++) {
		void *adr;

		adr = __va((pfn + i) << PAGE_SHIFT);
		clear_page(adr);
	}

	return __va(pfn << PAGE_SHIFT);
}

/* need 4 4k for initial PMD_SIZE, 4k for 0-ISA_END_ADDRESS */
#define INIT_PGT_BUF_SIZE	(5 * PAGE_SIZE)
RESERVE_BRK(early_pgt_alloc, INIT_PGT_BUF_SIZE);
void  __init early_alloc_pgt_buf(void)
{
	/* INIT_PGT_BUF_SIZE -> 4K * 5개 */
	unsigned long tables = INIT_PGT_BUF_SIZE;
	phys_addr_t base;

	base = __pa(extend_brk(tables, PAGE_SIZE));

	/*
	 *  |----------------------------------------------|
	 *                 |      20k	     |
	 *                 start             top
	 *                 end
	 */
	 // pgt_buf_start = pgt_buf_end = 0x01fda, pgt_buf_top = 0x01fdf
	pgt_buf_start = base >> PAGE_SHIFT;
	pgt_buf_end = pgt_buf_start;
	pgt_buf_top = pgt_buf_start + (tables >> PAGE_SHIFT);
}

int after_bootmem;

int direct_gbpages
#ifdef CONFIG_DIRECT_GBPAGES
				= 1
#endif
;

static void __init init_gbpages(void)
{
#ifdef CONFIG_X86_64
	if (direct_gbpages && cpu_has_gbpages)
		printk(KERN_INFO "Using GB pages for direct mapping\n");
	else
		direct_gbpages = 0;
#endif
}

struct map_range {
	unsigned long start;
	unsigned long end;
	unsigned page_size_mask;
};

static int page_size_mask;

static void __init probe_page_size_mask(void)
{
	// 현재 서버는 CONFIG_DIRECT_GBPAGES=y로 설정되어있으나, cpu가 제공하지 않음(셀러론)
	init_gbpages();

#if !defined(CONFIG_DEBUG_PAGEALLOC) && !defined(CONFIG_KMEMCHECK)
	/*
	 * For CONFIG_DEBUG_PAGEALLOC, identity mapping will use small pages.
	 * This will simplify cpa(), which otherwise needs to support splitting
	 * large pages into small in interrupt context, etc.
	 */
	if (direct_gbpages)
		page_size_mask |= 1 << PG_LEVEL_1G;
	if (cpu_has_pse)
		page_size_mask |= 1 << PG_LEVEL_2M;
#endif

	/* Enable PSE if available */
	if (cpu_has_pse)
		set_in_cr4(X86_CR4_PSE);

	/* Enable PGE if available */
	if (cpu_has_pge) {
		set_in_cr4(X86_CR4_PGE);
		__supported_pte_mask |= _PAGE_GLOBAL;
	}
}

#ifdef CONFIG_X86_32
#define NR_RANGE_MR 3
#else /* CONFIG_X86_64 */
#define NR_RANGE_MR 5
#endif

static int __meminit save_mr(struct map_range *mr, int nr_range,
			     unsigned long start_pfn, unsigned long end_pfn,
			     unsigned long page_size_mask)
{
	if (start_pfn < end_pfn) {
		if (nr_range >= NR_RANGE_MR)
			panic("run out of range for init_memory_mapping\n");
		mr[nr_range].start = start_pfn<<PAGE_SHIFT;
		mr[nr_range].end   = end_pfn<<PAGE_SHIFT;
		mr[nr_range].page_size_mask = page_size_mask;
		nr_range++;
	}

	return nr_range;
}

/*
 * adjust the page_size_mask for small range to go with
 *	big page size instead small one if nearby are ram too.
 */
static void __init_refok adjust_range_page_size_mask(struct map_range *mr,
							 int nr_range)
{
	int i;

	/* probe_page_size_mask()에서 현재 서버 기준에서 1G는 하지 못하고, 2MB 페이징은 가능한 상태이다. */
	for (i = 0; i < nr_range; i++) {
		if ((page_size_mask & (1<<PG_LEVEL_2M)) &&
		    !(mr[i].page_size_mask & (1<<PG_LEVEL_2M))) {
			unsigned long start = round_down(mr[i].start, PMD_SIZE);
			// mr[0].stat = 0, start는 0이 된다.
			unsigned long end = round_up(mr[i].end, PMD_SIZE);
			// mr[0].end = 100000, end = 2MB이 된다.

#ifdef CONFIG_X86_32
			if ((end >> PAGE_SHIFT) > max_low_pfn)
				continue;
#endif

			/* 2014.04.01 재복습 및 adjust_range_page_size_mask() 분석 */
			// 2014.04.01 memblock.memory를 출력해보고,,, start(0)이 왜 regions에서
			// 검색되지 않는지를 찾는중...
			if (memblock_is_region_memory(start, end - start))
				mr[i].page_size_mask |= 1<<PG_LEVEL_2M;
		}
		if ((page_size_mask & (1<<PG_LEVEL_1G)) &&
		    !(mr[i].page_size_mask & (1<<PG_LEVEL_1G))) {
			unsigned long start = round_down(mr[i].start, PUD_SIZE);
			unsigned long end = round_up(mr[i].end, PUD_SIZE);

			if (memblock_is_region_memory(start, end - start))
				mr[i].page_size_mask |= 1<<PG_LEVEL_1G;
		}
	}
}

static int __meminit split_mem_range(struct map_range *mr, int nr_range,
				     unsigned long start,
				     unsigned long end)
{
	unsigned long start_pfn, end_pfn, limit_pfn;
	unsigned long pfn;
	int i;

	// #1: limit_pfn = 256
	// #2: limit_pfn = 0x11e600
	limit_pfn = PFN_DOWN(end);

	/* head if not big page alignment ? */
	// #1: ptn = start_pfn = 0
	// #2: ptn = start_pfn = 0x11e400
	pfn = start_pfn = PFN_DOWN(start);
#ifdef CONFIG_X86_32
	/*
	 * Don't use a large page for the first 2/4MB of memory
	 * because there are often fixed size MTRRs in there
	 * and overlapping MTRRs into large pages can cause
	 * slowdowns.
	 */
	if (pfn == 0)
		end_pfn = PFN_DOWN(PMD_SIZE);
	else
		end_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
#else /* CONFIG_X86_64 */
	// #1: PFN_DOWN(PMD_SIZE) = 2MB/4K = 512 = 0x200
	// #2: end_pfn = 0x11e400
	end_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
#endif
	if (end_pfn > limit_pfn)
		// #1: end_pfn = limit_pfn = 256
		end_pfn = limit_pfn;
	if (start_pfn < end_pfn) {
		// #1: nr_range = 1
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);
		// #1: pfn = 256
		pfn = end_pfn;
	}

	/* big page (2M) range */
	// #1: start_pfn = 512
	// #2: start_pfn = 0x11e400
	start_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
#ifdef CONFIG_X86_32
	end_pfn = round_down(limit_pfn, PFN_DOWN(PMD_SIZE));
#else /* CONFIG_X86_64 */
	// #1: PFN_DOWN(PUD_SIZE) = 1G/4K = 256K = 0x40000
	// #2: end_pfn = 0x11e400 = 0x140000
	end_pfn = round_up(pfn, PFN_DOWN(PUD_SIZE));
	// #2: end_pfn = 0x11e600
	if (end_pfn > round_down(limit_pfn, PFN_DOWN(PMD_SIZE)))
		end_pfn = round_down(limit_pfn, PFN_DOWN(PMD_SIZE));
#endif

	if (start_pfn < end_pfn) {
		//nr_range = 1
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask & (1<<PG_LEVEL_2M));
		//pf = 0x11e600
		pfn = end_pfn;
	}

#ifdef CONFIG_X86_64
	/* big page (1G) range */
	start_pfn = round_up(pfn, PFN_DOWN(PUD_SIZE));
	end_pfn = round_down(limit_pfn, PFN_DOWN(PUD_SIZE));
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask &
				 ((1<<PG_LEVEL_2M)|(1<<PG_LEVEL_1G)));
		pfn = end_pfn;
	}

	/* tail is not big page (1G) alignment */
	start_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
	end_pfn = round_down(limit_pfn, PFN_DOWN(PMD_SIZE));
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask & (1<<PG_LEVEL_2M));
		pfn = end_pfn;
	}
#endif

	/* tail is not big page (2M) alignment */
	start_pfn = pfn;
	// #1: start_pfn = 256 = end_pfn
	// #2: start_pfn = 0x11e600
	end_pfn = limit_pfn;
	// #1: end_pfn = 256
	// #2: end_pfn = 0x11e600
	nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);
	// #1: nr_range = 1

	/*
	 * after_bootmem을 위한 mem_init()의 호출여부를 알 수 없음...
	 * 14.4.1 추가
	 *	- 아직 after_bootmem을 세팅하지 않았음.
	 */
	// #1: nr_range(1)일때, 2M, 1G를 위한 page_size_mask가 설정되지 않는다.
	// #2: nr_range(1)일때, 2M, 1G를 위한 page_size_mask가 설정되지 않는다.
	if (!after_bootmem)
		adjust_range_page_size_mask(mr, nr_range);

	/* try to merge same page size and continuous */
	// #1: nr_range(1)일 때, skip.
	// #2: nr_range(1)일 때, skip.
	for (i = 0; nr_range > 1 && i < nr_range - 1; i++) {
		unsigned long old_start;
		if (mr[i].end != mr[i+1].start ||
		    mr[i].page_size_mask != mr[i+1].page_size_mask)
			continue;
		/* move it */
		old_start = mr[i].start;
		memmove(&mr[i], &mr[i+1],
			(nr_range - 1 - i) * sizeof(struct map_range));
		mr[i--].start = old_start;
		nr_range--;
	}

	for (i = 0; i < nr_range; i++)
		printk(KERN_DEBUG " [mem %#010lx-%#010lx] page %s\n",
				mr[i].start, mr[i].end - 1,
			(mr[i].page_size_mask & (1<<PG_LEVEL_1G))?"1G":(
			 (mr[i].page_size_mask & (1<<PG_LEVEL_2M))?"2M":"4k"));

	// #1: return nr_range(1)
	// #2: return nr_range(1)
	return nr_range;
}

struct range pfn_mapped[E820_X_MAX];
int nr_pfn_mapped;

//start_pfn = 0, end_pfn = 256
static void add_pfn_range_mapped(unsigned long start_pfn, unsigned long end_pfn)
{
	// E820MAX = 128 + 3 * MAX_NUMNODES (1 << 10 = 1024) = 3200
	// nr_pfn_mapped = 0, start_pfn = 0, end_pfn = 256
	nr_pfn_mapped = add_range_with_merge(pfn_mapped, E820_X_MAX,
					     nr_pfn_mapped, start_pfn, end_pfn);
	// nr_pfn_mapped = 1, E820MAX = 3200
	nr_pfn_mapped = clean_sort_range(pfn_mapped, E820_X_MAX);

	// nr_pfn_mapped = 1, max_pfn_mapped = 0, end_pfn = 256
	max_pfn_mapped = max(max_pfn_mapped, end_pfn);

	//max_pfn_mapped = 256, start_pfn =0, (1UL << (32-PAGE_SHIFT)) = 1 << 20 = 1MB
	//max_low_pfn_mapped = 0, min(end_pfn, 1UL<<(32-PAGE_SHIFT)) = 256
	if (start_pfn < (1UL<<(32-PAGE_SHIFT)))
		max_low_pfn_mapped = max(max_low_pfn_mapped,
					 min(end_pfn, 1UL<<(32-PAGE_SHIFT)));
	//max_low_pfn_mapped = 256
}

bool pfn_range_is_mapped(unsigned long start_pfn, unsigned long end_pfn)
{
	int i;

	for (i = 0; i < nr_pfn_mapped; i++)
		if ((start_pfn >= pfn_mapped[i].start) &&
		    (end_pfn <= pfn_mapped[i].end))
			return true;

	return false;
}

/*
 * Setup the direct mapping of the physical memory at PAGE_OFFSET.
 * This runs before bootmem is initialized and gets pages directly from
 * the physical memory. To access them they are temporarily mapped.
 */
//#2: start = 0x11e400000, end = 0x11e5fffff
unsigned long __init_refok init_memory_mapping(unsigned long start,
					       unsigned long end)
{
	struct map_range mr[NR_RANGE_MR];
	unsigned long ret = 0;
	int nr_range, i;

	pr_info("init_memory_mapping: [mem %#010lx-%#010lx]\n",
	       start, end - 1);

	memset(mr, 0, sizeof(mr));
	/* 2014.04.01 재복습 및 adjust_range_page_size_mask() 분석
	 * 다시 들어가야함... memblock관련 처리 안됨 */
	nr_range = split_mem_range(mr, 0, start, end);
	// #1: nr_range = 1

	for (i = 0; i < nr_range; i++)
		// #1: mr[0].start = 0, end = 1MB, page_size_mask = 0
		ret = kernel_physical_mapping_init(mr[i].start, mr[i].end,
						   mr[i].page_size_mask);

	// start >> PAGE_SHIFT = 0, ret >> PAGE_SHIFT = 256
	// 2014. 3. 22. 여기까지 
	add_pfn_range_mapped(start >> PAGE_SHIFT, ret >> PAGE_SHIFT);

	//#1: return 256
	return ret >> PAGE_SHIFT;
}

/*
 * We need to iterate through the E820 memory map and create direct mappings
 * for only E820_RAM and E820_KERN_RESERVED regions. We cannot simply
 * create direct mappings for all pfns from [0 to max_low_pfn) and
 * [4GB to max_pfn) because of possible memory holes in high addresses
 * that cannot be marked as UC by fixed/variable range MTRRs.
 * Depending on the alignment of E820 ranges, this may possibly result
 * in using smaller size (i.e. 4K instead of 2M or 1G) page tables.
 *
 * init_mem_mapping() calls init_range_memory_mapping() with big range.
 * That range would have hole in the middle or ends, and only ram parts
 * will be mapped in init_range_memory_mapping().
 */
//#1: start = [0x0000011e400000] last_start = [0x0000011e600000]
static unsigned long __init init_range_memory_mapping(
					   unsigned long r_start,
					   unsigned long r_end)
{
	unsigned long start_pfn, end_pfn;
	unsigned long mapped_ram_size = 0;
	int i;

	//MAX_NUMNODES = 10
	//memblock region 영역을 하나씩 돌면서 start_pfn과 end_pfn을 얻어온다.
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, NULL) {
		//PFN_PHYS(start_pfn) = start_pfn의 실제 물리주소를 나타낸다.
		// __val = __val < __min ? __min: __val;	
		// __val > __max ? __max: __val; 
		u64 start = clamp_val(PFN_PHYS(start_pfn), r_start, r_end);
		u64 end = clamp_val(PFN_PHYS(end_pfn), r_start, r_end);
		if (start >= end)
			continue;

		/*
		 * if it is overlapping with brk pgt, we need to
		 * alloc pgt buf from memblock instead.
		 */
	//#1 : [                  start : 0x11e400000 -          end: 0x11e5fffff]	
	//#1 : pgt_buf_top << PAGE_SHIFT[0x0001df8000], pgt_buf_end [0x0001df5000]
		can_use_brk_pgt = max(start, (u64)pgt_buf_end<<PAGE_SHIFT) >=
				    min(end, (u64)pgt_buf_top<<PAGE_SHIFT);
	//#1 : can_use_brk_pgt = 1
	//#1 : mapped_ram_size = 0x00000200000
		init_memory_mapping(start, end);
		mapped_ram_size += end - start;
		can_use_brk_pgt = true;
	}

	//return : mapped_ram_size = 0x00000200000
	return mapped_ram_size;
}

/* (PUD_SHIFT-PMD_SHIFT)/2 */
#define STEP_SIZE_SHIFT 5
void __init init_mem_mapping(void)
{
	unsigned long end, real_end, start, last_start;
	unsigned long step_size;
	unsigned long addr;
	unsigned long mapped_ram_size = 0;
	unsigned long new_mapped_ram_size;

	/* 
	 * probe_page_size_mask() 에서 아래의 3가지 사항을 검사하고, 가능하면 enable한다.
	 * gbpage enable.
	 * page size extention enable.
	 * page global enable. */
	probe_page_size_mask();

#ifdef CONFIG_X86_64
	end = max_pfn << PAGE_SHIFT;
#else
	end = max_low_pfn << PAGE_SHIFT;
#endif

	/* the ISA range is always mapped regardless of memory holes */
	// 2014.04.05 드디어 완료.
	init_memory_mapping(0, ISA_END_ADDRESS);

	/* xen has big range in reserved near end of ram, skip it at first.*/
	//ISA_END_ADDRESS = 1MB, end = max_pfn << PAGE_SHIFT , PMD_SIZE = 2MB
	// max_pfn = last_pfn = 0xdbcf7 <- youngjoo 8GB 기준
	// ISA_END_ADDRESS = 1MB, end = max_pfn(11e600) << PAGE_SHIFT, PMD_SIZE = 2MB

	// 2014.04.08 memblock_find_in_range() 까지 진행함.
	addr = memblock_find_in_range(ISA_END_ADDRESS, end, PMD_SIZE, PMD_SIZE);
	//addr = cand = [0x0000011e400000]
	//real_end =  [0x00000100000000] mem.size = [1e600000]
	real_end = addr + PMD_SIZE;

	/* step_size need to be small so pgt_buf from BRK could cover it */
	step_size = PMD_SIZE; /* 2MB */
	max_pfn_mapped = 0; /* will get exact value next */
	min_pfn_mapped = real_end >> PAGE_SHIFT; /* real end / 4k */
	last_start = start = real_end;

	/*
	 * We start from the top (end of memory) and go to the bottom.
	 * The memblock_find_in_range() gets us a block of RAM from the
	 * end of RAM in [min_pfn_mapped, max_pfn_mapped) used as new pages
	 * for page table.
	 */
	while (last_start > ISA_END_ADDRESS) {
		//#1: last_start = [0x0000011e600000] step_size = [0x00000000200000]
		if (last_start > step_size) {
			start = round_down(last_start - 1, step_size);
			if (start < ISA_END_ADDRESS)
				start = ISA_END_ADDRESS;
			//#1: start = [0x0000011e400000], step_size = [0x00000000200000]
			//#2: start = [0x0000011c000000], step_size = [0x00000004000000]
		} else
			start = ISA_END_ADDRESS;
		//#1: start = [0x0000011e400000 mapped_ram_size = 0x00000200000] 
		//#2: start = [0x0000011e400000 mapped_ram_size = 0x00000200000] 
		new_mapped_ram_size = init_range_memory_mapping(start,
							last_start);
		//#1: new_mapped_ram_size = 0x00000000200000
		//14.4.10 여기까지 
		last_start = start;
		min_pfn_mapped = last_start >> PAGE_SHIFT;
		/* only increase step_size after big range get mapped */
		if (new_mapped_ram_size > mapped_ram_size)
			step_size <<= STEP_SIZE_SHIFT;
		mapped_ram_size += new_mapped_ram_size;
	}

	if (real_end < end)
		init_range_memory_mapping(real_end, end);

#ifdef CONFIG_X86_64
	if (max_pfn > max_low_pfn) {
		/* can we preseve max_low_pfn ?*/
		max_low_pfn = max_pfn;
	}
#else
	early_ioremap_page_table_range_init();
#endif

	load_cr3(swapper_pg_dir);
	__flush_tlb_all();

	early_memtest(0, max_pfn_mapped << PAGE_SHIFT);
}

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.
 *
 *
 * On x86, access has to be given to the first megabyte of ram because that area
 * contains bios code and data regions used by X and dosemu and similar apps.
 * Access has to be given to non-kernel-ram areas as well, these contain the PCI
 * mmio resources as well as potential bios/acpi data regions.
 */
int devmem_is_allowed(unsigned long pagenr)
{
	if (pagenr < 256)
		return 1;
	if (iomem_is_exclusive(pagenr << PAGE_SHIFT))
		return 0;
	if (!page_is_ram(pagenr))
		return 1;
	return 0;
}

void free_init_pages(char *what, unsigned long begin, unsigned long end)
{
	unsigned long addr;
	unsigned long begin_aligned, end_aligned;

	/* Make sure boundaries are page aligned */
	begin_aligned = PAGE_ALIGN(begin);
	end_aligned   = end & PAGE_MASK;

	if (WARN_ON(begin_aligned != begin || end_aligned != end)) {
		begin = begin_aligned;
		end   = end_aligned;
	}

	if (begin >= end)
		return;

	addr = begin;

	/*
	 * If debugging page accesses then do not free this memory but
	 * mark them not present - any buggy init-section access will
	 * create a kernel page fault:
	 */
#ifdef CONFIG_DEBUG_PAGEALLOC
	printk(KERN_INFO "debug: unmapping init [mem %#010lx-%#010lx]\n",
		begin, end - 1);
	set_memory_np(begin, (end - begin) >> PAGE_SHIFT);
#else
	/*
	 * We just marked the kernel text read only above, now that
	 * we are going to free part of that, we need to make that
	 * writeable and non-executable first.
	 */
	set_memory_nx(begin, (end - begin) >> PAGE_SHIFT);
	set_memory_rw(begin, (end - begin) >> PAGE_SHIFT);

	printk(KERN_INFO "Freeing %s: %luk freed\n", what, (end - begin) >> 10);

	for (; addr < end; addr += PAGE_SIZE) {
		memset((void *)addr, POISON_FREE_INITMEM, PAGE_SIZE);
		free_reserved_page(virt_to_page(addr));
	}
#endif
}

void free_initmem(void)
{
	free_init_pages("unused kernel memory",
			(unsigned long)(&__init_begin),
			(unsigned long)(&__init_end));
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
#ifdef CONFIG_MICROCODE_EARLY
	/*
	 * Remember, initrd memory may contain microcode or other useful things.
	 * Before we lose initrd mem, we need to find a place to hold them
	 * now that normal virtual memory is enabled.
	 */
	save_microcode_in_initrd();
#endif

	/*
	 * end could be not aligned, and We can not align that,
	 * decompresser could be confused by aligned initrd_end
	 * We already reserve the end partial page before in
	 *   - i386_start_kernel()
	 *   - x86_64_start_kernel()
	 *   - relocate_initrd()
	 * So here We can do PAGE_ALIGN() safely to get partial page to be freed
	 */
	free_init_pages("initrd memory", start, PAGE_ALIGN(end));
}
#endif

void __init zone_sizes_init(void)
{
	// MAX_NR_ZONES = 3
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA]		= MAX_DMA_PFN;
#endif
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32]	= MAX_DMA32_PFN;
#endif
	max_zone_pfns[ZONE_NORMAL]	= max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM]	= max_pfn;
#endif

	//
	//max_low_pfn : 11e600

	free_area_init_nodes(max_zone_pfns);
}


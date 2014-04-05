/*
 * Range add and subtract
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sort.h>
#include <linux/string.h>
#include <linux/range.h>

// *range = pfn_mapped az=  E820MAX = 128 + 3 * MAX_NUMNODES (1 << 6 = 64) = 320
// nr_range = nr_pfn_mapped = 0, start = start_pfn = 0, end = end_pfn = 256
// 0 ~ ISA_ADDRESS (1MB)까지의 영역을 하나의 range로 할당, return nr_rage = 1
int add_range(struct range *range, int az, int nr_range, u64 start, u64 end)
{
	if (start >= end)
		return nr_range;

	/* Out of slots: */
	if (nr_range >= az)
		return nr_range;

	range[nr_range].start = start;
	range[nr_range].end = end;

	nr_range++;

	return nr_range;
}

// *range = pfn_mapped az=  E820MAX = 128 + 3 * MAX_NUMNODES (1 << 6 = 64) = 320
// nr_range = nr_pfn_mapped = 0, start = start_pfn = 0, end = end_pfn = 256
int add_range_with_merge(struct range *range, int az, int nr_range,
		     u64 start, u64 end)
{
	int i;

	if (start >= end)
		return nr_range;

	/* get new start/end: */
	//nr_range = 0 
	for (i = 0; i < nr_range; i++) {
		u64 common_start, common_end;

		if (!range[i].end)
			continue;

		common_start = max(range[i].start, start);
		common_end = min(range[i].end, end);
		if (common_start > common_end)
			continue;

		/* new start/end, will add it back at last */
		start = min(range[i].start, start);
		end = max(range[i].end, end);

		memmove(&range[i], &range[i + 1],
			(nr_range - (i + 1)) * sizeof(range[i]));
		range[nr_range - 1].start = 0;
		range[nr_range - 1].end   = 0;
		nr_range--;
		i--;
	}

	/* Need to add it: */
	return add_range(range, az, nr_range, start, end);
}

void subtract_range(struct range *range, int az, u64 start, u64 end)
{
	int i, j;

	if (start >= end)
		return;

	for (j = 0; j < az; j++) {
		if (!range[j].end)
			continue;

		if (start <= range[j].start && end >= range[j].end) {
			range[j].start = 0;
			range[j].end = 0;
			continue;
		}

		if (start <= range[j].start && end < range[j].end &&
		    range[j].start < end) {
			range[j].start = end;
			continue;
		}


		if (start > range[j].start && end >= range[j].end &&
		    range[j].end > start) {
			range[j].end = start;
			continue;
		}

		if (start > range[j].start && end < range[j].end) {
			/* Find the new spare: */
			for (i = 0; i < az; i++) {
				if (range[i].end == 0)
					break;
			}
			if (i < az) {
				range[i].end = range[j].end;
				range[i].start = end;
			} else {
				pr_err("%s: run out of slot in ranges\n",
					__func__);
			}
			range[j].end = start;
			continue;
		}
	}
}

static int cmp_range(const void *x1, const void *x2)
{
	const struct range *r1 = x1;
	const struct range *r2 = x2;
	s64 start1, start2;

	start1 = r1->start;
	start2 = r2->start;

	return start1 - start2;
}

// pfn_mapped , E820MAX = 3200
int clean_sort_range(struct range *range, int az)
{
	// k = 3199, nr_range = 3200
	int i, j, k = az - 1, nr_range = az;

	// #1: k = 3199, range[0]에만 값이 있다.
	for (i = 0; i < k; i++) {
		if (range[i].end)
			continue;
		// i = 1, j = 3199, 맨마지막 end가 있는 위치를 찾아서 k에 할당.
		// 아니면 j = i
		for (j = k; j > i; j--) {
			// #1: 일때 찾지못함.
			if (range[j].end) {
				k = j;
				break;
			}
		}
		if (j == i)
			break;

		range[i].start = range[k].start;
		range[i].end   = range[k].end;
		range[k].start = 0;
		range[k].end   = 0;
		k--;
	}
	/* count it */
	// nr_range의 개수를 찾는다.
	for (i = 0; i < az; i++) {
		if (!range[i].end) {
			nr_range = i;
			break;
		}
	}

	/* sort them */
	//range = pfn_mapped, nr_ragnge = 1 , sizeof(strunct range), fn(cmp_range) 
	//in lib/sort.c
	// range[??].start기준으로 sort진행., 올림차순.
	sort(range, nr_range, sizeof(struct range), cmp_range, NULL);

	//#1: return nr_range = 1
	return nr_range;
}

void sort_range(struct range *range, int nr_range)
{
	/* sort them */
	sort(range, nr_range, sizeof(struct range), cmp_range, NULL);
}

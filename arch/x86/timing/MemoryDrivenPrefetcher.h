#ifndef MEMORYDRIVENPREFETCHER_H
#define MEMORYDRIVENPREFETCHER_H

#include "MemoryBehaviorLogger.h"

/*Summary Driven Prefetcher*/
struct x86_SumDrivenPrefetcher
{
	// Boolean SummaryAvailable;

	/*Summary Pushed to Prefetcher*/
	struct x86_mem_behavr_pattern_t stride_pattern_log[MAX_PATTERN_COUNT];
};
#endif
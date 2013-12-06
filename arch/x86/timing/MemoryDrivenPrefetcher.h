#ifndef MEMORYDRIVENPREFETCHER_H
#define MEMORYDRIVENPREFETCHER_H

#include "MemoryBehaviorLogger.h"

/*Summary Driven Prefetcher*/
struct x86_SumDrivenPrefetcher
{
	// Boolean SummaryAvailable;

	/*Summary Pushed to Prefetcher*/
	struct x86_stride_pattern_t stride_pattern_log[MAX_PATTERN_COUNT];
	struct x86_MRU_pattern_t MRU_Instruction_log[MAX_PATTERN_COUNT];
	struct x86_MRU_pattern_t MRU_Data_log[MAX_PATTERN_COUNT];
};
#endif
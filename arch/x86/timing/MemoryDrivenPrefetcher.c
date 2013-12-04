#include "MemoryDrivenPrefetcher.h"
#include "MemoryBehaviorLogger.h"
#include "thread.h"

void Memory_Drived_Prefetch(X86Thread *self)
{
	X86Core *core = self->core;

	/*Not Sure About This, Further research needed*/
	struct linked_list_t *lq = self->lq;
	struct x86_uop_t *load;
	struct mod_client_info_t *client_info;

	/* Get element from load queue. If it is not ready, go to the next one */

    struct x86_mem_behavr_pattern_t *StrideSummary = self->memlogger->stride_pattern_log;
    int *StrideMask = self->memlogger->StridePatternMask;
    int MaskIndex = 0;
    int Maskbit = 0;

    for (int i = 0; i < MAX_PATTERN_COUNT; ++i)
    {
        MaskIndex = (int) (i / 8);
        Maskbit   =  i % 8;

        if (StrideMask[MaskIndex] & (1 << Maskbit))
        {
            for (int j = 0; j < StrideSummary[i].instruction_address_count; ++j)
            {
                mod_access(self->data_mod, mod_access_load,
                           StrideSummary[i].InitialAddress + j * StrideSummary[i].stride, NULL, 
                           NULL, NULL, NULL);
            }

        }

    }
}
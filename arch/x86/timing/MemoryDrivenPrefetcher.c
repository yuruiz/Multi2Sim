#include "thread.h"
#include "MemoryDrivenPrefetcher.h"
#include "MemoryBehaviorLogger.h"
#include <mem-system/module.h>
#include <arch/x86/emu/context.h>


void Memory_Drived_Prefetch(X86Thread *self, X86Context *context)
{
        if (!self)
        {
              return;
        }
	X86Core *core = self->core;
	int context_id = context->pid;

	/*Not Sure About This, Further research needed*/
	// struct linked_list_t *lq = self->lq;
	// struct x86_uop_t *load;
	// struct mod_client_info_t *client_info;

	/* Get element from load queue. If it is not ready, go to the next one */

    struct x86_stride_pattern_t *StrideSummary = context->MemorySummary.stride_pattern_log;
    struct x86_MRU_pattern_t *MRU_I_Summary = context->MemorySummary.MRU_Instruction_log;
    struct x86_MRU_pattern_t *MRU_D_Summary = context->MemorySummary.MRU_Data_log;

    /*Prefetch Stride Pattern Data*/
    for (int i = 0; i < MAX_PATTERN_COUNT; ++i)
    {
        if (StrideSummary[i].InitialAddress == 0)
        {
            continue;
        }
        
    	if (StrideSummary[i].stride * StrideSummary[i].instruction_address_count < L1_BLOCK_SIZE)
    	{
    		mod_access(self->data_mod, mod_access_load,
                   StrideSummary[i].InitialAddress, NULL, NULL, NULL, NULL);
    	}
    	else
    	{
    		int blocks = (int) StrideSummary[i].stride * StrideSummary[i].instruction_address_count / L1_BLOCK_SIZE;

    		for (int i = 0; i < blocks; ++i)
    		{
    			mod_access(self->data_mod, mod_access_load,
                   StrideSummary[i].InitialAddress + i * L1_BLOCK_SIZE, NULL, NULL, NULL, NULL);
    		}
    	}
    }

    /*Prefetch MRU Data*/
    for (int i = 0; i < MAX_PATTERN_COUNT; ++i)
    {
		for (int way = 0; way < MRU_ASSOCIATIVITY; ++way)
		{
			if(MRU_D_Summary[way].tag[way] != 0)
			{
				mod_access(self->data_mod, mod_access_load,
               				MRU_D_Summary[way].tag[way], NULL, NULL, NULL, NULL);
			}
		}
    }

    /*Prefetch MRU Instruction*/
    for (int i = 0; i < MAX_PATTERN_COUNT; ++i)
    {
		for (int way = 0; way < MRU_ASSOCIATIVITY; ++way)
		{
			if(MRU_I_Summary[way].tag[way] != 0)
			{
				mod_access(self->inst_mod, mod_access_load,
               				MRU_I_Summary[way].tag[way], NULL, NULL, NULL, NULL);
			}
		}
    }
}

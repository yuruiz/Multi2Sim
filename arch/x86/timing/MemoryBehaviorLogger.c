#include <lib/mhandle/mhandle.h>
#include "lib/util/class.h"
#include <arch/x86/emu/context.h>
#include "thread.h"
#include "MemoryBehaviorLogger.h"


void x86ThreadFreeMemorySummary(X86Context *self)
{
	// free(self->memlogger);
}

/*Yurui Insert stride pattern into Memory behavior logger*/
void X86InsertInMBL(X86Thread *self, unsigned int address, Patterns pattern)
{
	X86Context *Context = self->ctx;
	struct x86_mem_behavr_logger_t *mem_behav_log = &(self->memlogger);

	int index = (address >> ADDRESS_INDEX_SHIFT) % BUFFER_INDEX_SIZE;

	switch(pattern)
	{
		case DATA_Pattern:
		{
			struct x86_mem_behavr_buffer *buffer = mem_behav_log->buffer;
			struct x86_stride_pattern_t *stride_pattern_logger = Context->MemorySummary.stride_pattern_log;
            struct x86_MRU_pattern_t *MRU_Data_logger = Context->MemorySummary.MRU_Data_log;
			int addressCount = buffer[index].Count;


			buffer[index].Count = (buffer[index].Count + 1) % BUFFER_LENGTH;
			buffer[index].address[addressCount]= address;

			if (addressCount != BUFFER_LENGTH - 1)
			{
				return;
			}

			long temp_difference = 0;
			long difference = 0;

			int stride_pattern_count = 1;
			int stride_pattern_end = 0;
			int stride_pattern_max_length = 2;


			/*locate longest stride pattern within buffer*/
			for (int i = 0; i < BUFFER_LENGTH-2; ++i)
			{
				temp_difference = buffer[index].address[i+1] - buffer[index].address[i];
				if (difference != temp_difference)
				{
					difference = temp_difference;
					stride_pattern_count = 1;
				}
				else
				{
					stride_pattern_count++;
					if (stride_pattern_count > stride_pattern_max_length)
					{
						stride_pattern_max_length = stride_pattern_count;
						stride_pattern_end = i+1;
					}
				}
			}

			/*if address matches stride pattern, add the address into stride pattern logger*/
			if (stride_pattern_end != 0)
			{
				stride_pattern_logger[index].stride = difference;
				stride_pattern_logger[index].InitialAddress = buffer[index].address[stride_pattern_end - stride_pattern_max_length];
				stride_pattern_logger[index].instruction_address_count = stride_pattern_max_length;

				// fprintf(stderr, "Stride pattern with index %d update with stride %d, InitialAddress %d and length %d \n",
				// 	index, stride_pattern_logger[index].stride, stride_pattern_logger[index].InitialAddress,
				// 	stride_pattern_logger[index].instruction_address_count);

			}

            /*MRU Data Record*/
            int Found_way = -1;
            for (int i = 0; i < BUFFER_LENGTH; ++i)
            {
            	int selected_address = buffer[index].address[i];
	            for (int way = 0; way < MRU_ASSOCIATIVITY; ++way)
	            {
	                /*Invalid Way Found*/
	                if(!MRU_Data_logger[index].tag[way])
	                {
	                    MRU_Data_logger[index].tag[way] = selected_address;
	                    Found_way = way;
	                    break;
	                }

	                /*hit*/
	                if ((MRU_Data_logger[index].tag[way] >> ADDRESS_INDEX_SHIFT) == (selected_address >> ADDRESS_INDEX_SHIFT))
	                {
	                	Found_way = way;
	                    break;
	                }
	            }

	            if (Found_way == -1)
	            {
	                for (int way = 0; way < MRU_ASSOCIATIVITY; ++way)
	                {
	                    MRU_Data_logger[index].counter[way]--;

	                    if (MRU_Data_logger[index].counter[way] < 0)
	                    {
	                        MRU_Data_logger[index].counter[way] = MRU_ASSOCIATIVITY - 1;
	                        MRU_Data_logger[index].tag[way] = selected_address;
	                    }
	                }
	            }
            }
			break;
		}

		case Instructioin_Pattern:
		{
			struct x86_MRU_pattern_t *mru_i_pattern_logger = Context->MemorySummary.MRU_Instruction_log;
			int Found_way = -1;

			for (int way = 0; way < MRU_ASSOCIATIVITY; ++way)
			{
				/*Invalid Way Found*/
				if(!mru_i_pattern_logger[index].tag[way])
				{
					mru_i_pattern_logger[index].tag[way] = address;
					Found_way = way;
					break;
				}

				/*hit*/
				if ((mru_i_pattern_logger[index].tag[way] >> ADDRESS_INDEX_SHIFT) == (address >> ADDRESS_INDEX_SHIFT))
				{
					Found_way = way;
					break;
				}
			}

			if (Found_way == -1)
			{
				for (int way = 0; way < MRU_ASSOCIATIVITY; ++way)
				{
					mru_i_pattern_logger[index].counter[way]--;

					if (mru_i_pattern_logger[index].counter[way] < 0)
					{
						mru_i_pattern_logger[index].counter[way] = MRU_ASSOCIATIVITY - 1;
						mru_i_pattern_logger[index].tag[way] = address;
					}
				}
			}
            break;
		}

        default:
            break;
	}
	// fclose(pfile);
}

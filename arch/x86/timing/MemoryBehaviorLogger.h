#ifndef MEMORYBEHAVIORLOGGER_H
#define MEMORYBEHAVIORLOGGER_H

#include "thread.h"

#define MAX_PATTERN_COUNT 256
#define MRU_ASSOCIATIVITY 4

#define BUFFER_INDEX_SIZE 64
#define BUFFER_LENGTH     256

/*To hide the address offset*/
#define L1_BLOCK_SIZE    64
#define ADDRESS_INDEX_SHIFT  6
// #define PATTERN_RECORD_THRESHOULD 2

// #define MAX_INSTRUCTION_ADDRESS_COUNT 1000

typedef enum
{
	Instructioin_Pattern,
	DATA_Pattern
} Patterns;

/*Yurui Memory Behavior Logger Pattern*/
struct x86_stride_pattern_t
{
	/*Recorded Instruction Counter*/
	int context_id;
	int instruction_address_count;
	int stride;
	unsigned int InitialAddress;
};

struct x86_MRU_pattern_t
{
	int context_id[MRU_ASSOCIATIVITY];

	/*Initial Address of the block*/
	unsigned int tag[MRU_ASSOCIATIVITY];
	unsigned int counter[MRU_ASSOCIATIVITY];
};

struct x86_mem_behavr_buffer
{
	int Count;
	unsigned int address[BUFFER_LENGTH];
};


struct x86_mem_behavr_logger_t
{
	/*other Pattern to add*/

	struct x86_mem_behavr_buffer buffer[BUFFER_INDEX_SIZE];

	struct x86_stride_pattern_t stride_pattern_log[MAX_PATTERN_COUNT];
	struct x86_MRU_pattern_t MRU_Instruction_log[MAX_PATTERN_COUNT];
	struct x86_MRU_pattern_t MRU_Data_log[MAX_PATTERN_COUNT];
	/*other Pattern to add*/
};

void x86ThreadInitMemoryBehaviorLogger(X86Thread *self);

void x86ThreadFreeMemoryBehaviorLogger(X86Thread *self);

void X86InsertInMBL(X86Thread *self, unsigned int address, Patterns pattern);

#endif

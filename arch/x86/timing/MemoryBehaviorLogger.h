#ifndef MEMORYBEHAVIORLOGGER_H
#define MEMORYBEHAVIORLOGGER_H

#include "thread.h"

#define MAX_PATTERN_COUNT 256
#define MRU_ASSOCIATIVITY 4;

/*To hide the address offset*/
#define ADDRESS_INDEX_SHIFT       8
// #define PATTERN_RECORD_THRESHOULD 2
#define BUFFER_INDEX_SIZE 64
#define BUFFER_LENGTH     32
// #define MAX_INSTRUCTION_ADDRESS_COUNT 1000

typedef enum
{
	Stride_Pattern;
	MRU_Instructioin;
	MRU_DATA;
 }Patterns;

/*Yurui Memory Behavior Logger Pattern*/
struct x86_stride_pattern_t
{
	/*Recorded Instruction Counter*/
	int context_id;
	int instruction_address_count;
	int stride;
	unsigned int InitialAddress;

	// unsigned int address[MAX_INSTRUCTION_ADDRESS_COUNT];
};

struct x86_MRU_pattern_t
{
	int context_id;

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
	struct x86_MRU_pattern_t MRU_DATA_log[MAX_PATTERN_COUNT];
	/*other Pattern to add*/

	/*Pattern Mask*/

#define MASKLENGTH   MAX_PATTERN_COUNT / 8

	int StridePatternMask[MASKLENGTH];

};

void x86ThreadInitMemoryBehaviorLogger(X86Thread *self);

void x86ThreadFreeMemoryBehaviorLogger(X86Thread *self);

void X86InsertInMBL(X86Thread *self, unsigned int address, Patterns pattern);

#endif

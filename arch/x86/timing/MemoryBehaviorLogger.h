#ifndef MEMORYBEHAVIORLOGGER_H
#define MEMORYBEHAVIORLOGGER_H

#include "thread.h"

/*Yurui Memory Behavior Logger Pattern*/
struct x86_mem_behavr_pattern_t
{
	/*Recorded Instruction Counter*/
	int instruction_address_count;
	int stride;
	unsigned int InitialAddress;

#define MAX_INSTRUCTION_ADDRESS_COUNT 1000
	unsigned int address[MAX_INSTRUCTION_ADDRESS_COUNT];
};

#define ADDRESS_INDEX_SHIFT       8
#define PATTERN_RECORD_THRESHOULD 2
#define BUFFER_INDEX_SIZE 64
#define BUFFER_LENGTH     32

struct x86_mem_behavr_buffer
{
	int Count;
	unsigned int address[BUFFER_LENGTH];
};

struct x86_mem_behavr_logger_t
{
	/*other Pattern to add*/

	struct x86_mem_behavr_buffer buffer[BUFFER_INDEX_SIZE];


#define MAX_PATTERN_COUNT 64

	struct x86_mem_behavr_pattern_t stride_pattern_log[MAX_PATTERN_COUNT];
	struct x86_mem_behavr_pattern_t ptrchase_pattern_log[MAX_PATTERN_COUNT];
	/*other Pattern to add*/

	/*Pattern Mask*/

#define MASKLENGTH   MAX_PATTERN_COUNT / 8

	int StridePatternMask[MASKLENGTH];

};

void x86ThreadInitMemoryBehaviorLogger(X86Thread *self);

void x86ThreadFreeMemoryBehaviorLogger(X86Thread *self);

void X86InsertInMBL(X86Thread *self, unsigned int address);

#endif
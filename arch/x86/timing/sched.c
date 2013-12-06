/*
 *  Multi2Sim
 *  Copyright (C) 2012  Rafael Ubal (ubal@ece.neu.edu)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <arch/x86/emu/context.h>
#include <arch/x86/emu/emu.h>
#include <arch/x86/emu/regs.h>
#include <lib/esim/trace.h>
#include <lib/util/bit-map.h>
#include <lib/util/debug.h>
#include <lib/util/misc.h>

#include "core.h"
#include "cpu.h"
#include "sched.h"
#include "thread.h"
#include "context-queue.h"
#define MIHIR

/*
 * This file contains the implementation of the x86 context scheduler. The following
 * definitions are assumed in the description of the algorithm:
 *   - A node is a pair core/thread where a context can run.
 *   - Map a context to a node: associate a context with a node (core/thread). This
 *     association is done once when the context is created, or during context migration
 *     (e.g., when it changes its thread affinity bitmap). A node has a list of
 *     mapped contexts.
 *   - Unmap a context: remove association between a context and a node. The context
 *     is removed from the node's list of mapped contexts.
 *   - Allocate a context: select the context from the node's list of mapped context
 *     and start effectively executing it, allocating pipeline resources in the node.
 *   - Evict a context: Deallocate pipeline resources from the executing context, but
 *     still keep it in the node's list of mapped contexts.
 *
 * The following is a description of the implemented scheduling algorithm, implemented
 * in function 'X86CpuSchedule()'.
 *
 *   - Only start running if a schedule signal has been received (variable
 *     'emu->schedule_signal' is set, or one of the allocated contexts has
 *     exhaused its quantum. A schedule signal is triggered every time a
 *     context changes any of its state bits.
 *
 *   - Uncheck 'emu->schedule_signal' at this point to allow any of the
 *     subsequent actions to force the scheduler to run in the next cycle.
 *
 *   - Check the list of running contexts for any unmapped context. Map them by
 *     selecting the node that has the lowest number of contexts currently mapped
 *     to it. The context will always execute on that node, unless it changes its
 *     affinity.
 *
 *   - For each node:
 *
 *         - If the allocated context is not in 'running' state, signal eviction.
 *
 *         - If the allocated context has exhausted its quantum, signal eviction.
 *           As an exception, the context will continue running if there is no
 *           other candidate in the mapped context list; in this case, the running
 *           context resets its quantum to keep the scheduler from trying to
 *           evict the context right away.
 *
 *         - If the allocated context lost affinity with the node, signal
 *           eviction.
 *
 *         - If any mapped context other than the allocated context lost
 *           affinity with the node, unmap it.
 *
 *         - If any mapped context other than the allocated context finished
 *           execution, unmap it and free it.
 *
 *         - If there is no allocated context, search the node's list of mapped
 *           contexts for the context in state running and with valid affinity
 *           that was evicted least recently. Allocate it, if found.
 *
 *   - Update global variable 'min_alloc_cycle' with the cycle of the least
 *     recently allocated context. The scheduler needs to be called again due
 *     to quantum expiration only when this variable indicates so.
 */



/*
 * Class 'X86Thread'
 * Additional functions
 */

void X86ThreadUnmapContext(X86Thread *self, X86Context *ctx)
{
	X86Cpu *cpu = self->cpu;

	assert(ctx);
	assert(DOUBLE_LINKED_LIST_MEMBER(self, mapped, ctx));
	assert(X86ContextGetState(ctx, X86ContextMapped));
	assert(!X86ContextGetState(ctx, X86ContextAlloc));

	/* Update context state */
	X86ContextClearState(ctx, X86ContextMapped);

	/* Remove context from node's mapped list */
	DOUBLE_LINKED_LIST_REMOVE(self, mapped, ctx);

	/* Debug */
	X86ContextDebug("#%lld ctx %d unmapped from thread %s\n",
		asTiming(cpu)->cycle, ctx->pid, self->name);

	/* If context is finished, free it. */
	if (X86ContextGetState(ctx, X86ContextFinished))
	{
		/* Trace */
		x86_trace("x86.end_ctx ctx=%d\n", ctx->pid);

		/* Free context */
		delete(ctx);
	}
}


void X86ThreadEvictContextSignal(X86Thread *self, X86Context *context)
{
	assert(context);
	assert(self->ctx == context);
	assert(X86ContextGetState(context, X86ContextAlloc));
	assert(X86ContextGetState(context, X86ContextMapped));
	assert(self->ctx == context);
	assert(DOUBLE_LINKED_LIST_MEMBER(self, mapped, context));
	assert(!context->evict_signal);

	/* Set eviction signal. */
	context->evict_signal = 1;
	X86ContextDebug("#%lld ctx %d signaled for eviction from thread %s\n",
			asTiming(self)->cycle, context->pid, self->name);

	/* If pipeline is already empty for the thread, effective eviction can
	 * happen straight away. */
	if (X86ThreadIsPipelineEmpty(self))
		{
		X86ThreadEvictContext(self, context);
		//printf("\nMIHIR ::^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ \n"); 
		//printf("\nMIHIR ::vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv \n"); 
		X86_ADD_EVICTED_CONTEXT(self,context);
		}

}


void X86ThreadEvictContext(X86Thread *self, X86Context *context)
{
//	printf("\nMIHIR ::EVICT \n"); 
	X86Core *core = self->core;
	X86Cpu *cpu = self->cpu;

	assert(context);
	assert(self->ctx == context);
	assert(X86ContextGetState(context, X86ContextAlloc));
	assert(X86ContextGetState(context, X86ContextMapped));
	assert(!X86ContextGetState(context, X86ContextSpecMode));
	assert(!self->rob_count);
	assert(context->evict_signal);

	/* Update node state */
	self->ctx = NULL;
	self->fetch_neip = 0;

	/* Update context state */
	X86ContextClearState(context, X86ContextAlloc);
	context->evict_cycle = asTiming(cpu)->cycle;
	context->evict_signal = 0;
	#ifdef MIHIR_1
	X86ContextDebug("\nMIHIR ::--------------------------------- \n"); 
	X86ContextDebug("\nMIHIR ::------dirty register prints------ \n"); 
	X86ContextDebug("\nMIHIR ::--------------------------------- \n"); 
	#endif
	#ifdef MIHIR_1
	if(self->backup_regs->eax != 	context->regs->eax )
	{
	X86ContextDebug("\nMIHIR :: EAX Dirty register\n"); 
	self->dirty_reg_bitmap[0] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[0] = 0;
	}

	if(self->backup_regs->ecx != 	context->regs->ecx )
	{
	X86ContextDebug("\nMIHIR :: ECX Dirty register\n"); 
	self->dirty_reg_bitmap[1] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[1] = 0;
	}
	if(self->backup_regs->edx != 	context->regs->edx )
	{
	X86ContextDebug("\nMIHIR ::edx Dirty register\n"); 
	self->dirty_reg_bitmap[2] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[2] = 0;
	}
	if(self->backup_regs->ebx != 	context->regs->ebx )
	{
	X86ContextDebug("\nMIHIR :: ebx Dirty register\n"); 
	self->dirty_reg_bitmap[3] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[3] = 0;
	}
	if(self->backup_regs->esp != 	context->regs->esp )
	{
	X86ContextDebug("\nMIHIR ::esp Dirty register\n"); 
	self->dirty_reg_bitmap[4] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[4] = 0;
	}
	if(self->backup_regs->ebp != 	context->regs->ebp )
	{
	X86ContextDebug("\nMIHIR :: ebp Dirty register\n"); 
	self->dirty_reg_bitmap[5] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[5] = 0;
	}
	if(self->backup_regs->esi != 	context->regs->esi )
	{
	X86ContextDebug("\nMIHIR ::esi Dirty register\n"); 
	self->dirty_reg_bitmap[6] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[6] = 0;
	}
	if(self->backup_regs->edi != 	context->regs->edi )
	{
	X86ContextDebug("\nMIHIR ::edi Dirty register\n"); 
	self->dirty_reg_bitmap[7] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[7] = 0;
	}
	if(self->backup_regs->eip != 	context->regs->eip )
	{
	X86ContextDebug("\nMIHIR ::eip Dirty register\n"); 
	self->dirty_reg_bitmap[8] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[8] = 0;
	}
	if(self->backup_regs->eflags != 	context->regs->eflags )
	{
	X86ContextDebug("\nMIHIR ::eflags Dirty register\n"); 
	self->dirty_reg_bitmap[9] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[9] = 0;
	}
	if(self->backup_regs->es != 	context->regs->es )
	{
	X86ContextDebug("\nMIHIR ::es Dirty register\n"); 
	self->dirty_reg_bitmap[10] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[10] = 0;
	}
	if(self->backup_regs->cs != 	context->regs->cs )
	{
	X86ContextDebug("\nMIHIR :: cs Dirty register\n"); 
	self->dirty_reg_bitmap[11] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[11] = 0;
	}
	if(self->backup_regs->ss != 	context->regs->ss )
	{
	X86ContextDebug("\nMIHIR ::ss Dirty register\n"); 
	self->dirty_reg_bitmap[12] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[12] = 0;
	}
	if(self->backup_regs->ds != 	context->regs->ds )
	{
	X86ContextDebug("\nMIHIR ::ds Dirty register\n"); 
	self->dirty_reg_bitmap[13] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[13] = 0;
	}
	if(self->backup_regs->fs != 	context->regs->fs )
	{
	X86ContextDebug("\nMIHIR ::fs Dirty register\n"); 
	self->dirty_reg_bitmap[14] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[14] = 0;
	}
	if(self->backup_regs->gs != 	context->regs->gs )
	{
	X86ContextDebug("\nMIHIR ::gs Dirty register\n"); 
	self->dirty_reg_bitmap[15] = 1;
	}
	else
	{
	self->dirty_reg_bitmap[15] = 0;
	}

	X86ContextDebug("\nMIHIR ::^^^^^dirty bitmap^^^^^^^^^^^^^^^^ \n"); 
	for(int i = 15; i>= 0; i--)
		X86ContextDebug("%d",self->dirty_reg_bitmap[i]); 
	X86ContextDebug("\nMIHIR ::^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ \n"); 
	X86ContextDebug("\nMIHIR ::^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ \n"); 
	X86ContextDebug("\n----------------------------------\n"); 
	X86ContextDebug("\n-------backup registers-----------\n"); 
	X86ContextDebug("\n----------------------------------\n"); 

	X86ContextDebug("eax = %x\n",self->backup_regs->eax); //MIHIR  
	X86ContextDebug("ecx = %x\n",self->backup_regs->ecx); //MIHIR  
	X86ContextDebug("edx = %x\n",self->backup_regs->edx); //MIHIR  
	X86ContextDebug("ebx = %x\n",self->backup_regs->ebx); //MIHIR  
	X86ContextDebug("esp = %x\n",self->backup_regs->esp); //MIHIR  
	X86ContextDebug("ebp = %x\n",self->backup_regs->ebp); //MIHIR  
	X86ContextDebug("esi = %x\n",self->backup_regs->esi); //MIHIR  
	X86ContextDebug("edi = %x\n",self->backup_regs->edi); //MIHIR  
	X86ContextDebug("eip = %x\n",self->backup_regs->eip); //MIHIR  
	X86ContextDebug("eflags = %x\n", self->backup_regs->eflags); //MIHIR  
	X86ContextDebug("es = %x \n", self->backup_regs->es);//MIHIR  
	X86ContextDebug("cs = %x \n", self->backup_regs->cs);//MIHIR  
	X86ContextDebug("ss = %x \n", self->backup_regs->ss);//MIHIR  
	X86ContextDebug("ds = %x \n",self->backup_regs->ds);//MIHIR  
	X86ContextDebug("fs = %x \n",self->backup_regs->fs);//MIHIR  
	X86ContextDebug("gs = %x \n",self->backup_regs->gs);//MIHIR  





	X86ContextDebug("\n----------------------------------\n"); 
	X86ContextDebug("\n-------AT CONTEXT =%d SWITCH------\n",context->pid); 
	X86ContextDebug("\n----------------------------------\n"); 
	X86ContextDebug("eax = %x\n",context->regs->eax); //MIHIR  
	X86ContextDebug("ecx = %x\n", context->regs->ecx); //MIHIR  
	X86ContextDebug("edx = %x\n", context->regs->edx); //MIHIR  
	X86ContextDebug("ebx = %x\n", context->regs->ebx); //MIHIR  
	X86ContextDebug("esp = %x\n", context->regs->esp); //MIHIR  
	X86ContextDebug("ebp = %x\n", context->regs->ebp); //MIHIR  
	X86ContextDebug("esi = %x\n", context->regs->esi); //MIHIR  
	X86ContextDebug("edi = %x\n", context->regs->edi); //MIHIR  
	X86ContextDebug("eip = %x\n", context->regs->eip); //MIHIR  
	X86ContextDebug("eflags = %x\n", context->regs->eflags); //MIHIR  
	X86ContextDebug("es = %x \n", context->regs->es);//MIHIR  
	X86ContextDebug("cs = %x \n", context->regs->cs);//MIHIR  
	X86ContextDebug("ss = %x \n", context->regs->ss);//MIHIR  
	X86ContextDebug("ds = %x \n",context->regs->ds);//MIHIR  
	X86ContextDebug("fs = %x \n",context->regs->fs);//MIHIR  
	X86ContextDebug("gs = %x \n",context->regs->gs);//MIHIR  
	X86ContextDebug("\n----------------------------------\n"); 
	#endif
	self->backup_regs = context->regs;
	/* Debug */
	X86ContextDebug("#%lld ctx %d evicted from thread %s\n",
			asTiming(cpu)->cycle, context->pid, self->name);

	/* Trace */
	x86_trace("x86.unmap_ctx ctx=%d core=%d thread=%d\n",
			context->pid, core->id, self->id_in_core);
}


void X86ThreadSchedule(X86Thread *self)
{
	X86Cpu *cpu = self->cpu;
	X86Context *ctx;
	X86Context *tmp_ctx;

	int node;

	/* Actions for the context allocated to this node. */
	node = self->id_in_cpu;
	ctx = self->ctx;
	if (ctx)
	{
		assert(X86ContextGetState(ctx, X86ContextAlloc));
		assert(X86ContextGetState(ctx, X86ContextMapped));

		/* Context not in 'running' state */
		if (!ctx->evict_signal && !X86ContextGetState(ctx, X86ContextRunning))
			X86ThreadEvictContextSignal(self, ctx);

		/* Context lost affinity with node */
		if (!ctx->evict_signal && !bit_map_get(ctx->affinity, node, 1))
			X86ThreadEvictContextSignal(self, ctx);

		/* Context quantum expired */
		if (!ctx->evict_signal && asTiming(cpu)->cycle >= ctx->alloc_cycle
				+ x86_cpu_context_quantum)
		{
			int found = 0;

			/* Find a running context mapped to the same node */
			DOUBLE_LINKED_LIST_FOR_EACH(self, mapped, tmp_ctx)
			{
				if (tmp_ctx != ctx && X86ContextGetState(tmp_ctx,
						X86ContextRunning))
				{
					found = 1;
					break;
				}
			}

			/* If a context was found, there are other candidates
			 * for allocation in the node. We need to evict the
			 * current context. If there are no other running
			 * candidates, there is no need to evict. But we
			 * update the allocation time, so that the
			 * scheduler is not called constantly hereafter. */
			if (found)
				X86ThreadEvictContextSignal(self, ctx);
			else
				ctx->alloc_cycle = asTiming(cpu)->cycle;
		}
	}

	/* Actions for mapped contexts, other than the allocated context if any. */
	DOUBLE_LINKED_LIST_FOR_EACH(self, mapped, ctx)
	{
		/* Ignore the currently allocated context */
		if (ctx == self->ctx)
			continue;

		/* Unmap a context if it lost affinity with the node or if it
		 * finished execution. */
		if (!bit_map_get(ctx->affinity, node, 1) ||
				X86ContextGetState(ctx, X86ContextFinished))
			{//MIHIR  
				X86ThreadUnmapContext(self, ctx);
				//MIHIR  
				X86_REMOVE_EVICTED_CONTEXT(self,ctx);
			}	//MIHIR  
	}

	/* If node is available, try to allocate a context mapped to it. */
	if (!self->ctx)
	{
		/* Search the mapped context with the oldest 'evict_cycle'
		 * that is state 'running' and has affinity with the node. */
		ctx = NULL;
		#ifndef MIHIR 
		DOUBLE_LINKED_LIST_FOR_EACH(self, mapped, tmp_ctx)
		{
			/* No affinity */
			if (!bit_map_get(tmp_ctx->affinity, node, 1))
				continue;

			/* Not running */
			if (!X86ContextGetState(tmp_ctx, X86ContextRunning))
				continue;

			/* Good candidate */
			if (!ctx || ctx->evict_cycle > tmp_ctx->evict_cycle)
				ctx = tmp_ctx;
		}
		#else
		ctx = LOAD_BEST_CONTEXT(self);
		printf("best thread returned is %d",ctx->pid);		
		#endif

		/* Allocate context if found */
		if (ctx)
			X86CpuAllocateContext(cpu, ctx);
	}
}




/*
 * Class 'X86Cpu'
 * Additional functions
 */


void X86CpuAllocateContext(X86Cpu *self, X86Context *ctx)
{
	int core_index = ctx->core_index;
	int thread_index = ctx->thread_index;

	X86Core *core;
	X86Thread *thread;

	core = self->cores[core_index];
	thread = core->threads[thread_index];

	assert(!thread->ctx);
	assert(X86ContextGetState(ctx, X86ContextMapped));
	assert(!X86ContextGetState(ctx, X86ContextAlloc));
	assert(!ctx->evict_signal);

	/* Update context state */
	ctx->alloc_cycle = asTiming(self)->cycle;
	X86ContextSetState(ctx, X86ContextAlloc);

	/* Update node state */
	//MIHIR  
/*	if(thread->ctx)
	{
	X86ContextDebug("\nMIHIR ::before context allocation \n"); 
	X86ContextDebug("eax = %x\n",thread->ctx->regs->eax); //MIHIR  
	X86ContextDebug("ecx = %x\n", thread->ctx->regs->ecx); //MIHIR  
	X86ContextDebug("edx = %x\n", thread->ctx->regs->edx); //MIHIR  
	X86ContextDebug("ebx = %x\n", thread->ctx->regs->ebx); //MIHIR  
	X86ContextDebug("esp = %x\n", thread->ctx->regs->esp); //MIHIR  
	X86ContextDebug("ebp = %x\n", thread->ctx->regs->ebp); //MIHIR  
	X86ContextDebug("esi = %x\n", thread->ctx->regs->esi); //MIHIR  
	X86ContextDebug("edi = %x\n", thread->ctx->regs->edi); //MIHIR  
	X86ContextDebug("eip = %x\n", thread->ctx->regs->eip); //MIHIR  
	X86ContextDebug("eflags = %x\n", thread->ctx->regs->eflags); //MIHIR  
	X86ContextDebug("es = %x \n", thread->ctx->regs->es);//MIHIR  
	X86ContextDebug("cs = %x \n", thread->ctx->regs->cs);//MIHIR  
	X86ContextDebug("ss = %x \n", thread->ctx->regs->ss);//MIHIR  
	X86ContextDebug("ds = %x \n",thread->ctx->regs->ds);//MIHIR  
	X86ContextDebug("fs = %x \n",thread->ctx->regs->fs);//MIHIR  
	X86ContextDebug("gs = %x \n",thread->ctx->regs->gs);//MIHIR  
	}*/
	//MIHIR  

  	thread->ctx = ctx; //original code
	#ifdef MIHIR_1
	X86ContextDebug("\nMIHIR ::registers in context %d\n",ctx->pid); 
	X86ContextDebug("eax = %x\n",ctx->regs->eax); //MIHIR  
	X86ContextDebug("ecx = %x\n",ctx->regs->ecx); //MIHIR  
	X86ContextDebug("edx = %x\n",ctx->regs->edx); //MIHIR  
	X86ContextDebug("ebx = %x\n",ctx->regs->ebx); //MIHIR  
	X86ContextDebug("esp = %x\n",ctx->regs->esp); //MIHIR  
	X86ContextDebug("ebp = %x\n",ctx->regs->ebp); //MIHIR  
	X86ContextDebug("esi = %x\n",ctx->regs->esi); //MIHIR  
	X86ContextDebug("edi = %x\n",ctx->regs->edi); //MIHIR  
	X86ContextDebug("eip = %x\n",ctx->regs->eip); //MIHIR  
	X86ContextDebug("eflags = %x\n",ctx->regs->eflags); //MIHIR  
	X86ContextDebug("es = %x \n",   ctx->regs->es);//MIHIR  
	X86ContextDebug("cs = %x \n",   ctx->regs->cs);//MIHIR  
	X86ContextDebug("ss = %x \n",   ctx->regs->ss);//MIHIR  
	X86ContextDebug("ds = %x \n",   ctx->regs->ds);//MIHIR  
	X86ContextDebug("fs = %x \n",   ctx->regs->fs);//MIHIR  
	X86ContextDebug("gs = %x \n",	ctx->regs->gs);//MIHIR  

	X86ContextDebug("\nMIHIR ::next ip b4 = %x \n",thread->fetch_neip); 
	#endif
	thread->fetch_neip = ctx->regs->eip;
	#ifdef MIHIR_1
	X86ContextDebug("\nMIHIR ::next ip after = %x \n",thread->fetch_neip); 
	X86ContextDebug("\n@@@@@@@@@@@@@@@@@@@@\n");

	X86ContextDebug("\nMIHIR ::after context allocation by context object %d\n",ctx->pid); 
	X86ContextDebug("eax = %x\n",thread->ctx->regs->eax); //MIHIR  
	X86ContextDebug("ecx = %x\n", thread->ctx->regs->ecx); //MIHIR  
	X86ContextDebug("edx = %x\n", thread->ctx->regs->edx); //MIHIR  
	X86ContextDebug("ebx = %x\n", thread->ctx->regs->ebx); //MIHIR  
	X86ContextDebug("esp = %x\n", thread->ctx->regs->esp); //MIHIR  
	X86ContextDebug("ebp = %x\n", thread->ctx->regs->ebp); //MIHIR  
	X86ContextDebug("esi = %x\n", thread->ctx->regs->esi); //MIHIR  
	X86ContextDebug("edi = %x\n", thread->ctx->regs->edi); //MIHIR  
	X86ContextDebug("eip = %x\n", thread->ctx->regs->eip); //MIHIR  
	X86ContextDebug("eflags = %x\n", thread->ctx->regs->eflags); //MIHIR  
	X86ContextDebug("es = %x \n", thread->ctx->regs->es);//MIHIR  
	X86ContextDebug("cs = %x \n", thread->ctx->regs->cs);//MIHIR  
	X86ContextDebug("ss = %x \n", thread->ctx->regs->ss);//MIHIR  
	X86ContextDebug("ds = %x \n",thread->ctx->regs->ds);//MIHIR  
	X86ContextDebug("fs = %x \n",thread->ctx->regs->fs);//MIHIR  
	X86ContextDebug("gs = %x \n",thread->ctx->regs->gs);//MIHIR  

	//MIHIR  
/*
	if(thread->dirty_reg_bitmap[0])
	{
		thread->ctx->regs->eax 	= ctx->regs->eax	;
	}
	if(thread->dirty_reg_bitmap[1])
	{
		thread->ctx->regs->ecx 	= ctx->regs->ecx	;
	}
	if(thread->dirty_reg_bitmap[2])
	{
		thread->ctx->regs->edx 	= ctx->regs->edx	;
	}
	if(thread->dirty_reg_bitmap[3])
	{
		thread->ctx->regs->ebx 	= ctx->regs->ebx	;
	}
	if(thread->dirty_reg_bitmap[4])
	{
		thread->ctx->regs->esp 	= ctx->regs->esp	;
	}
	if(thread->dirty_reg_bitmap[5])
	{
		thread->ctx->regs->ebp 	= ctx->regs->ebp	;
	}
	if(thread->dirty_reg_bitmap[6])
	{
		thread->ctx->regs->esi 	= ctx->regs->esi	;
	}
	if(thread->dirty_reg_bitmap[7])
	{
		thread->ctx->regs->edi 	= ctx->regs->edi	;
	}
	if(thread->dirty_reg_bitmap[8])
	{
		thread->ctx->regs->eip 	= ctx->regs->eip	;
	}
	if(thread->dirty_reg_bitmap[9])
	{
		thread->ctx->regs->eflags 	= ctx->regs->eflags	;
	}
	if(thread->dirty_reg_bitmap[10])
	{
		thread->ctx->regs->es 	= ctx->regs->es	;
	}
	if(thread->dirty_reg_bitmap[11])
	{
		thread->ctx->regs->cs 	= ctx->regs->cs	;
	}
	if(thread->dirty_reg_bitmap[12])
	{
		thread->ctx->regs->ss 	= ctx->regs->ss	;
	}
	if(thread->dirty_reg_bitmap[13])
	{
		thread->ctx->regs->ds 	= ctx->regs->ds	;
	}
	if(thread->dirty_reg_bitmap[14])
	{
		thread->ctx->regs->fs 	= ctx->regs->fs	;
	}
	if(thread->dirty_reg_bitmap[15])
	{
		thread->ctx->regs->gs 	= ctx->regs->gs	;
	}*/
/*	X86ContextDebug("\nMIHIR ::after context allocation by dirty registers %d\n",ctx->pid); 
	X86ContextDebug("eax = %x\n",thread->ctx->regs->eax); //MIHIR  
	X86ContextDebug("ecx = %x\n", thread->ctx->regs->ecx); //MIHIR  
	X86ContextDebug("edx = %x\n", thread->ctx->regs->edx); //MIHIR  
	X86ContextDebug("ebx = %x\n", thread->ctx->regs->ebx); //MIHIR  
	X86ContextDebug("esp = %x\n", thread->ctx->regs->esp); //MIHIR  
	X86ContextDebug("ebp = %x\n", thread->ctx->regs->ebp); //MIHIR  
	X86ContextDebug("esi = %x\n", thread->ctx->regs->esi); //MIHIR  
	X86ContextDebug("edi = %x\n", thread->ctx->regs->edi); //MIHIR  
	X86ContextDebug("eip = %x\n", thread->ctx->regs->eip); //MIHIR  
	X86ContextDebug("eflags = %x\n", thread->ctx->regs->eflags); //MIHIR  
	X86ContextDebug("es = %x \n", thread->ctx->regs->es);//MIHIR  
	X86ContextDebug("cs = %x \n", thread->ctx->regs->cs);//MIHIR  
	X86ContextDebug("ss = %x \n", thread->ctx->regs->ss);//MIHIR  
	X86ContextDebug("ds = %x \n",thread->ctx->regs->ds);//MIHIR  
	X86ContextDebug("fs = %x \n",thread->ctx->regs->fs);//MIHIR  
	X86ContextDebug("gs = %x \n",thread->ctx->regs->gs);//MIHIR  
*/
	X86ContextDebug("\n@@@@@@@@@@@@@@@@@@@@\n");
	//MIHIR  


	#endif

	/* Debug */
	X86ContextDebug("#%lld ctx %d in thread %s allocated\n",
		asTiming(self)->cycle, ctx->pid, thread->name);

	/* Trace */
	x86_trace("x86.map_ctx ctx=%d core=%d thread=%d ppid=%d\n",
		ctx->pid, core_index, thread_index,
		ctx->parent ? ctx->parent->pid : 0);
}


void X86CpuMapContext(X86Cpu *self, X86Context *ctx)
{
	int min_core;
	int min_thread;

	int core;
	int thread;
	int node;
	int available;
	assert(!X86ContextGetState(ctx, X86ContextAlloc));
	assert(!X86ContextGetState(ctx, X86ContextMapped));
	assert(!ctx->evict_signal);

	/* From the nodes (core/thread) that the context has affinity with, find
	 * the one with the smalled number of contexts mapped. */
	min_core = -1;
	min_thread = -1;
	node = 0;
	available	= 0;
	for (core = 0; core < x86_cpu_num_cores; core++)
	{
		for (thread = 0; thread < x86_cpu_num_threads; thread++)
		{
			/* Context does not have affinity with this thread */
			node = core * x86_cpu_num_threads + thread;
			if (!bit_map_get(ctx->affinity, node, 1))
				continue;
			
			#ifdef MIHIR 
				available = CAN_MAP_CONTEXT(self->cores[core]->threads[thread]);
				if(!available)
				{
					continue;
				}                   
			#endif 

			/* Check if this thread is better */
			if (min_core < 0 ||
					self->cores[core]->threads[thread]
						->mapped_list_count <
					self->cores[min_core]->threads[min_thread]
						->mapped_list_count)
			{
				min_core = core;
				min_thread = thread;
			}
		}
	}

	/* Final values */
	core = min_core;
	thread = min_thread;
	if (core < 0 || thread < 0)
		panic("%s: no node with affinity found", __FUNCTION__);
	#ifdef MIHIR 
	//MIHIR  if there is a context with free buffers then this can be mapped otherwise dont map it till a location gets vacant in the context buffer
	if(available)
	{
	#endif 
		printf("\nMIHIR :: Space available in the context queue \n"); 
		/* Update context state */
		ctx->core_index = core;
		ctx->thread_index = thread;
		X86ContextSetState(ctx, X86ContextMapped);

		/* Add context to the node's mapped list */
		DOUBLE_LINKED_LIST_INSERT_TAIL(self->cores[core]->threads[thread],
				mapped, ctx);

		X86_ADD_EVICTED_CONTEXT(self->cores[core]->threads[thread],ctx);
		/* Debug */
		X86ContextDebug("#%lld ctx %d mapped to node %d/%d\n",
			asTiming(self)->cycle, ctx->pid, core, thread);
	#ifdef MIHIR 
	}
	#endif
}


void X86CpuUpdateMinAllocCycle(X86Cpu *self)
{
	X86Context *ctx;

	int i;
	int j;

	self->min_alloc_cycle = asTiming(self)->cycle;
	for (i = 0; i < x86_cpu_num_cores; i++)
	{
		for (j = 0; j < x86_cpu_num_threads; j++)
		{
			ctx = self->cores[i]->threads[j]->ctx;
			if (ctx && !ctx->evict_signal &&
					ctx->alloc_cycle < self->min_alloc_cycle)
				self->min_alloc_cycle = ctx->alloc_cycle;
		}
	}
}


void X86CpuSchedule(X86Cpu *self)
{
	X86Emu *emu = self->emu;
	X86Context *ctx;

	int quantum_expired;

	int i;
	int j;

	/* Check if any context quantum could have expired */
	quantum_expired = asTiming(self)->cycle >= self->min_alloc_cycle +
			x86_cpu_context_quantum;

	/* Check for quick scheduler end. The only way to effectively execute
	 * the scheduler is that either a quantum expired or a signal to
	 * reschedule has been flagged. */
	if (!quantum_expired && !emu->schedule_signal)
		return;

	/* OK, we have to schedule. Uncheck the schedule signal here, since
	 * upcoming actions might set it again for a second scheduler call. */
	emu->schedule_signal = 0;
	X86ContextDebug("#%lld schedule\n", asTiming(self)->cycle);

	/* Check if there is any running context that is currently not mapped
	 * to any node (core/thread); for example, a new context, or a
	 * context that has changed its affinity. */
	DOUBLE_LINKED_LIST_FOR_EACH(emu, running, ctx)
		if (!X86ContextGetState(ctx, X86ContextMapped))
			X86CpuMapContext(self, ctx);

	/* Scheduling done individually for each node (core/thread) */
	for (i = 0; i < x86_cpu_num_cores; i++)
		for (j = 0; j < x86_cpu_num_threads; j++)
			X86ThreadSchedule(self->cores[i]->threads[j]);

	/* Update oldest allocation time of allocated contexts to determine
	 * when is the next time the scheduler should be invoked. */
	X86CpuUpdateMinAllocCycle(self);
}

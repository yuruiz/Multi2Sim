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


#include <lib/esim/trace.h>
#include <lib/util/list.h>

#include <time.h>
#include <stdlib.h>

#include "core.h"
#include "cpu.h"
#include "context-queue.h"
#include <arch/x86/emu/context.h>
#include "thread.h"
#include <lib/util/debug.h>
#include <lib/mhandle/mhandle.h>

#define MIHIR
//#define ROUND_ROBIN_POLICY
//#define RAND_POLICY
//#define PROJDBG
#define LRU_POLICY
#define write_pointer self->ctx_buffer_write_pointer
#define occupancy     self->ctx_buffer_occupancy

int x86_ctx_queue_size;
int x86_project_debug_category;
//int best_thread;
//int write_pointer;
//int occupancy;
typedef	struct	memory_summary
{
	long long	  start_address;
	long long	  end_address;
	int		  stride;
	long long	  size;
}mem_summary;

typedef struct x86_inactive_context_buffer_entry
{
	X86Context 	inactive_ctx;
	long long	eviction_time;
	long long	context_hits;
	long long	context_misses;	
	int		ctx_id;
	mem_summary  	mem_smry;
	int	 	vacant;
	int		turns_to_run;
	int 		current_best_thread;
	int 		valid;
	int		idle_time;
}ctx_bff_entry;


void X86ThreadInitCtxQueue(X86Thread *self)
{
	printf("\nMIHIR ::context buffer constructor called------------------ \n"); 
	printf("\nMIHIR :: size of context queue is %d \n",x86_ctx_queue_size); 
	self->ctx_queue = list_create_with_size(x86_ctx_queue_size);
	self->best_thread 	= 0;
	self->running_time 	= 0;
	self->sched_time	= 0;

}


void X86ThreadFreeCtxQueue(X86Thread *self)
{
	struct list_t *ctx_queue;
	X86Context *ctx;

	ctx_queue = self->ctx_queue;
	ctx       = self->ctx; 
	while (list_count(ctx_queue))
	{
		ctx = list_remove_at(ctx_queue, 0);
		ctx->in_ctx_queue = 0;
	//MIHIR  	x86_uop_free_if_not_queued(uop);
	}
	list_free(ctx_queue);

}


void X86ThreadRecoverCtxQueue(X86Thread *self)
{
	/*X86Core *core = self->core;
	X86Cpu *cpu = self->cpu;

	struct list_t *uop_queue = self->uop_queue;
	struct x86_uop_t *uop;

	while (list_count(uop_queue))
	{
		uop = list_get(uop_queue, list_count(uop_queue) - 1);
		assert(uop->thread == self);
		if (!uop->specmode)
			break;
		list_remove_at(uop_queue, list_count(uop_queue) - 1);
		uop->in_uop_queue = 0;
*/
		/* Trace */
/*		if (x86_tracing())
		{
			x86_trace("x86.inst id=%lld core=%d stg=\"sq\"\n",
				uop->id_in_core, core->id);
			X86CpuAddToTraceList(cpu, uop);
		}
*/
		/* Free */
//		x86_uop_free_if_not_queued(uop);
//	}
}


void X86_ADD_EVICTED_CONTEXT(X86Thread *self,X86Context *ctx)	//TODO rename this
{

	struct list_t *ctx_queue;
	ctx_queue = self->ctx_queue;
	assert(ctx_queue != NULL);
	//printf("\nMIHIR :: passed the assertion \n"); 
	#ifdef MIHIR 
		//X86ProjectDebug("\nMIHIR ::CALLED THE CONTEXT ADD \n"); 
		//X86ProjectDebug("\nMIHIR ::CONTEXT TO ADD is %d \n",ctx->pid); 
		//X86ProjectDebug("context evicted at %lld\n",asTiming(self->cpu)->cycle); 
	#endif 
	printf("\nMIHIR ::CONTEXT TO ADD is %d \n",ctx->pid); 
	//MIHIR  
	//TODO put assertion on context being allocated
	//MIHIR  
		
	ctx_bff_entry	*ctx_loc;

	//assert(ctx_loc!=NULL);
	//printf("\nMIHIR ::ctx_loc assertion passed \n"); 


	int iter1 = 0;
	int found = 0;
	int iter2 = 0;	
	int empty_buffer_found = 0;
	int empty_buffer=0;
	ctx_bff_entry	 *ctx_iter1;
	ctx_bff_entry	 *ctx_iter2;
	LIST_FOR_EACH(ctx_queue,iter1)
		{
			#ifdef MIHIR 
				#ifdef PROJDBG
					X86ProjectDebug("\nMIHIR ::************************************************************** \n"); 
					X86ProjectDebug("\nMIHIR ::searching for context in context buffer to place the context\n"); 
					X86ProjectDebug("\nMIHIR ::search iter item %d\n",iter1); 
					X86ProjectDebug("\nMIHIR ::searching for the process ID = %d\n",ctx->pid);
				#else
					//printf("\nMIHIR ::************************************************************** \n"); 
					//printf("\nMIHIR ::searching for context in context buffer to place the context\n"); 
					//printf("\nMIHIR ::search iter item %d\n",iter1); 
					//printf("\nMIHIR ::searching for the process ID = %d\n",ctx->pid);
				#endif 


			#endif 
			ctx_iter1 = list_get(ctx_queue, iter1);
			if(ctx_iter1->ctx_id == ctx->pid)
			{
				found = 1;
				break;
			} 	
			if(!ctx_iter1->valid & !empty_buffer_found)
			{
				empty_buffer = iter1;
				empty_buffer_found = 1;
				printf("\nMIHIR :: EMPTY BUFFER FOUND. WILL PLACE NEXT INCOMING THREAD IN THE BUFFER %d \n",empty_buffer); 	
			}
			//printf("\nMIHIR ::inactive_thread[%d] eviction time = %lld \n",iter,ctx_iter->eviction_time); 
			//printf("\nMIHIR ::inactive_thread[%d] ctx_id        = %d  \n",iter,ctx_iter->ctx_id);
		}
	//printf("\nMIHIR :: iter1 where found = %d \n",iter1); 
	if(!found)
	{

		//MIHIR  	  need to create a new entry only if the context is new entry to buffer
		#ifdef MIHIR
			#ifdef PROJDBG
				X86ProjectDebug("MIHIR :: Did not find the context in the queue"); 
				X86ProjectDebug("\nMIHIR ::need to create a new entry in the context buffer \n"); 
			#else
				//printf("MIHIR :: Did not find the context in the queue"); 
				//printf("\nMIHIR ::need to create a new entry in the context buffer \n"); 
			#endif
		#endif
		ctx_loc = xcalloc(1, sizeof(struct x86_inactive_context_buffer_entry));
	}
	else
	{
		#ifdef MIHIR 
			#ifdef PROJDBG
				X86ProjectDebug("\nMIHIR :: found the context in location %d\n",iter1);
			#else
				//printf("\nMIHIR :: found the context in location %d\n",iter1);
			#endif
		#endif 
		ctx_loc = list_get(ctx_queue, iter1);
	}
	#ifdef MIHIR 
		#ifdef PROJDBG
			X86ProjectDebug("\nMIHIR ::************************************************************** \n"); 
		#else 
			//printf("\nMIHIR ::************************************************************** \n"); 
		#endif 
	#endif 
	ctx_loc->inactive_ctx		= *ctx;
	ctx_loc->eviction_time		= asTiming(self->cpu)->cycle;
	ctx_loc->ctx_id 		= ctx->pid;
	ctx_loc->turns_to_run++;
	ctx_loc->valid			= 1;
	ctx_loc->idle_time		= 0;
	
	self->running_time = 	asTiming(self->cpu)->cycle	- self->sched_time; 
	printf("\nMIHIR :: time = %lld , sched_time = %lld \n",asTiming(self->cpu)->cycle,self->sched_time); 
	
	self->sched_time = asTiming(self->cpu)->cycle ;

	LIST_FOR_EACH(ctx_queue,iter2)
	{
		ctx_iter2 = list_get(ctx_queue, iter2);
		if(ctx_iter2->ctx_id == ctx->pid)
		{
			continue;
		}
		else
		{
			ctx_iter2->idle_time = ctx_iter2->idle_time + self->running_time;
			list_set(ctx_queue, iter2, ctx_iter2);
		}
	}

	/*ctx_loc->context_hits		=;
        ctx_loc->context_misses		=;	*/
	/*ctx_loc->mem_smry	=	*/

	if(!found)
	{
		if(empty_buffer_found)
		{
			list_set(ctx_queue, empty_buffer, ctx_loc);
		}
		else
		{
			list_add(ctx_queue,ctx_loc);
			write_pointer++;
		}
	}
	else
	{
		list_set(ctx_queue, iter1, ctx_loc);
	}

	occupancy++;

	assert(X86ContextGetState(ctx_loc->inactive_ctx, X86ContextMapped));
	assert(!X86ContextGetState(ctx_loc->inactive_ctx, X86ContextAlloc));
	
	#ifdef MIHIR
		#ifdef PROJDBG
			X86ProjectDebug("\n INACTIVE_CONTEXT %d placed in context buffer",ctx_loc->ctx_id);
			X86ProjectDebug("context evicted at %lld\n",ctx_loc->eviction_time); 
		#else 
			//printf("\n INACTIVE_CONTEXT %d placed in context buffer",ctx_loc->ctx_id);
			//printf("context evicted at %lld\n",ctx_loc->eviction_time); 
		#endif 
	#endif
	//printf("\nMIHIR ::b4 add to context \n"); 
	//printf("\nMIHIR ::after add to context \n"); 
	ctx_bff_entry	 *ctx_iter;
	int iter = 0;
	LIST_FOR_EACH(ctx_queue,iter)
		{
			ctx_iter = list_get(ctx_queue, iter);
			#ifdef MIHIR
				#ifdef PROJDBG
					X86ProjectDebug("\nMIHIR ::============================================================== \n"); 
					X86ProjectDebug("\nMIHIR ::iter item %d\n",iter); 
					X86ProjectDebug("\nMIHIR ::inactive_thread[%d] eviction time = %lld \n",iter,ctx_iter->eviction_time); 
					X86ProjectDebug("\nMIHIR ::inactive_thread[%d] ctx_id        = %d  \n",iter,ctx_iter->ctx_id);
					X86ProjectDebug("\nMIHIR ::============================================================== \n"); 
				#else
					//printf("\nMIHIR ::============================================================== \n"); 
					//printf("\nMIHIR ::iter item %d\n",iter); 
					//printf("\nMIHIR ::inactive_thread[%d] eviction time = %lld \n",iter,ctx_iter->eviction_time); 
					//printf("\nMIHIR ::inactive_thread[%d] ctx_id        = %d  \n",iter,ctx_iter->ctx_id);
					//printf("\nMIHIR ::============================================================== \n"); 
				#endif
			#endif
		}

}

struct X86Context *LOAD_BEST_CONTEXT(X86Thread *self)
{
	//printf("\nMIHIR :: scheduled at %lld \n",self->sched_time); 
	int 		outgoing_best_thread;
	struct list_t 	*ctx_queue;
	X86Context 	*ctx;
	int		is_starving	= 0;
	int		iter1 		= 0;

	outgoing_best_thread = self->best_thread;
	ctx_bff_entry	 *ctx_iter1;
	ctx_queue	= self->ctx_queue;

	//ctx_queue = self->ctx_queue;
//	printf("\nMIHIR ::outgoing_best_thread = %d \n",outgoing_best_thread); 
//	printf("\nMIHIR ::occupancy \n"); 

	ctx_iter1 = list_get(ctx_queue, outgoing_best_thread);
	ctx_iter1->current_best_thread = 0;
	list_set(ctx_queue, outgoing_best_thread,ctx_iter1);

	LIST_FOR_EACH(ctx_queue,iter1)
	{
		ctx_iter1 = list_get(ctx_queue, iter1);
		printf("\nMIHIR :: max_idle_time = %d \n",max_idle_time); 
		printf("idle time[%d] = %lld ",iter1,ctx_iter1->idle_time); 
		if(ctx_iter1->idle_time >=max_idle_time)
		{ 
			printf("\nMIHIR :: context %d is starving \n",ctx_iter1->ctx_id);  
			is_starving = 1;
			break;
		}
	}

		
	
	if(is_starving)
	{
		self->best_thread = iter1;
	}
	else
	{
		#ifdef LRU_POLICY
			self->best_thread = RANK_MRU(self,outgoing_best_thread);
		#endif 
		#ifdef	RAND_POLICY
			self->best_thread = RANK_RAND(self,asTiming(self->cpu)->cycle);
		#endif
		#ifdef ROUND_ROBIN_POLICY
			self->best_thread = RANK_ROUND_ROBIN(self);
		#endif
		#ifdef  MEM_RANK_POLICY
		#endif
	}
	ctx_iter1 = list_get(ctx_queue, self->best_thread);
	ctx_iter1->current_best_thread = 1;
	ctx_iter1->valid	= 0;
	list_set(ctx_queue, self->best_thread,ctx_iter1);
	ctx = &ctx_iter1->inactive_ctx;	
	return ctx; 
	
}

//returns the thread evicted most recently
int RANK_MRU(X86Thread *self,int outgoing_best_thread)
{

	struct list_t *ctx_queue;
	ctx_queue = self->ctx_queue;
	assert(ctx_queue != NULL);
	//ctx_bff_entry	*ctx_loc;
	int iter1;
	int best_ctx_loc;	
	ctx_bff_entry	 *ctx_iter1;
	//MIHIR  best_ctx_loc = (outgoing_best_thread+1)%x86_ctx_queue_size;
	printf("\nMIHIR ::============ \n"); 
	printf("\nMIHIR :: outgoing_best_thread = %d\n",outgoing_best_thread); 
	best_ctx_loc = (outgoing_best_thread+1)%write_pointer;
	ctx_iter1 = list_get(ctx_queue,best_ctx_loc);
	int most_recent_eviction =  ctx_iter1->eviction_time;

	//MIHIR  for ((iter1) = (outgoing_best_thread+2)%x86_ctx_queue_size; (iter) < (outgoing_best_thread+list_count((list)))%x86_ctx_queue_size; iter1 = (iter+1)%x86_ctx_queue_size)
	for ((iter1) = (outgoing_best_thread+2)%write_pointer; (iter1) < (outgoing_best_thread+list_count((ctx_queue)))%write_pointer; iter1 = (iter1+1)%write_pointer)
	{
		/*if(iter1 >= write_pointer)
		{
			continue;
		}*/
		assert(iter1 < write_pointer);
		ctx_iter1 = list_get(ctx_queue,iter1);
		
		printf("\nMIHIR :: ctx_id = %d , iter1 = %d , valid = %d , eviction = %lld ,turns_to_run = %d ,idle_time = %lld\n",ctx_iter1->ctx_id,
		iter1,ctx_iter1->valid,ctx_iter1->eviction_time,ctx_iter1->turns_to_run,ctx_iter1->idle_time);
		/*if(!iter1)
		{
			if(!outgoing_best_thread)
			{
			}	
		}*/
		if(most_recent_eviction<ctx_iter1->eviction_time)
		{
			most_recent_eviction 	= ctx_iter1->eviction_time;
			best_ctx_loc	 	= iter1;
		}
	}
	printf("\nMIHIR :: best_ctx_loc = %d \n",best_ctx_loc); 
	return best_ctx_loc;
}


int RANK_RAND(X86Thread *self,long long seed)
{
	int *rand_array;
	int i;
	rand_array = (int*)xcalloc(write_pointer, sizeof(int)); 
	srand(seed);
	for(i=0; i< write_pointer ; i++)
	{
	       rand_array[i]= rand();
	}
	int max_position = 0;
	int max_value 	 = rand_array[0];
	for(i=0; i< write_pointer ; i++)
	{
		if(i == self->best_thread)
			continue;
		if(max_value<rand_array[i])
		{
		       	max_position = i     ;
			max_value = rand_array[i];
		}
	}
	return max_position;
}

int RANK_ROUND_ROBIN(X86Thread *self)
{
	return ((self->best_thread + 1)%write_pointer);
}


void X86_REMOVE_EVICTED_CONTEXT(X86Thread *self,X86Context *ctx)	//TODO rename this
{

	struct list_t *ctx_queue;
	ctx_queue = self->ctx_queue;
	assert(ctx_queue != NULL);
	//printf("\nMIHIR :: passed the assertion \n"); 
	#ifdef MIHIR 
		X86ProjectDebug("\nMIHIR ::CALLED THE REMOVE EVICT CONTEXT \n"); 
		X86ProjectDebug("\nMIHIR ::CONTEXT TO remove is %d \n",ctx->pid); 
		X86ProjectDebug("context unmapped from the inactive context buffer at %lld\n",asTiming(self->cpu)->cycle); 
	#endif 
	//MIHIR  
	//TODO put assertion on context being allocated
	//MIHIR  
		
	ctx_bff_entry	*ctx_loc;


	int iter1 = 0;
	int found = 0;

	ctx_bff_entry	 *ctx_iter1;
	LIST_FOR_EACH(ctx_queue,iter1)
		{
			#ifdef MIHIR 
				#ifdef PROJDBG
					X86ProjectDebug("\nMIHIR ::************************************************************** \n"); 
					X86ProjectDebug("\nMIHIR ::searching for context in context buffer to remove from the buffer\n"); 
					X86ProjectDebug("\nMIHIR ::search iter item %d\n",iter1); 
					X86ProjectDebug("\nMIHIR ::searching for the process ID = %d\n",ctx->pid);
				#else
					printf("\nMIHIR ::************************************************************** \n"); 
					printf("\nMIHIR ::searching for context in context buffer to remove\n"); 
					printf("\nMIHIR ::search iter item %d\n",iter1); 
					printf("\nMIHIR ::searching for the process ID = %d\n",ctx->pid);
				#endif 


			#endif 
			ctx_iter1 = list_get(ctx_queue, iter1);
			if(ctx_iter1->ctx_id == ctx->pid)
			{
				found = 1;
				break;
			} 	
		}
	assert(found);
	#ifdef MIHIR 
		#ifdef PROJDBG
			X86ProjectDebug("\nMIHIR :: found the context in location %d\n",iter1);
		#else
			printf("\nMIHIR :: found the context in location %d\n",iter1);
		#endif
	#endif 
	ctx_loc = list_get(ctx_queue, iter1);
	//printf("\nMIHIR :: context summary on the thread is  \n"); 
	//printf("\nMIHIR ::************************************************************** \n"); 
	//printf("\nMIHIR ::ctx_loc->eviction_time = %lld \n",ctx_loc->eviction_time ); 
	//printf("\nMIHIR ::ctx_loc->ctx_id        = %d \n"  ,ctx_loc->ctx_id        ); 
	//printf("\nMIHIR ::ctx_loc->turns_to_run  = %d \n"  ,ctx_loc->turns_to_run  ); 
	//printf("\nMIHIR ::************************************************************** \n"); 
	/*ctx_loc->context_hits		=;
        ctx_loc->context_misses		=;	*/
	/*ctx_loc->mem_smry	=	*/

	#ifdef MIHIR
		#ifdef PROJDBG
			X86ProjectDebug("\n inactive_context %d remove from context buffer",ctx_loc->ctx_id);
			X86ProjectDebug("context unmapped at %lld\n",ctx_loc->eviction_time); 
		#else 
			printf("\n inactive_context %d remove from context buffer",ctx_loc->ctx_id);
			printf("context unmapped at %lld\n",ctx_loc->eviction_time); 
		#endif 
	#endif
	ctx_loc->valid			= 0;
	list_set(ctx_queue, iter1, ctx_loc);
	occupancy--;
}

int CAN_MAP_CONTEXT(X86Thread *self)
{
	if(occupancy == x86_ctx_queue_size)
		return 0;
	return 1;
}

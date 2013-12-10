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


#include <lib/util/linked-list.h>
#include <lib/esim/trace.h>
#include <lib/util/list.h>
#include <arch/x86/emu/context.h>

#include "core.h"
#include "cpu.h"
#include "event-queue.h"
#include "thread.h"
#include "uop.h"
#include "bpred.h"
#include "MemoryDrivenPrefetcher.h"

extern int x86_bpred_twolevel_l1size;  /* Two-level adaptive predictor: level-1 size */


/*
 * Class 'X86Core'
 */

void X86CoreInitEventQueue(X86Core *self)
{
	self->event_queue = linked_list_create();
}


void X86CoreFreeEventQueue(X86Core *self)
{
	struct x86_uop_t *uop;

	while (linked_list_count(self->event_queue))
	{
		uop = X86CoreExtractFromEventQueue(self);
		x86_uop_free_if_not_queued(uop);
	}
	linked_list_free(self->event_queue);
}


static int eventq_compare(const void *item1, const void *item2)
{
	const struct x86_uop_t *uop1 = item1;
	const struct x86_uop_t *uop2 = item2;
	return uop1->when != uop2->when ? uop1->when - uop2->when
		: uop1->id - uop2->id;
}


void X86CoreInsertInEventQueue(X86Core *self, struct x86_uop_t *uop)
{
	struct linked_list_t *event_queue = self->event_queue;
	struct x86_uop_t *item;

	assert(!uop->in_event_queue);
	linked_list_head(event_queue);
	for (;;)
	{
		item = linked_list_get(event_queue);
		if (!item || eventq_compare(uop, item) < 0)
			break;
		linked_list_next(event_queue);
	}
	linked_list_insert(event_queue, uop);
	uop->in_event_queue = 1;
}


struct x86_uop_t *X86CoreExtractFromEventQueue(X86Core *self)
{
	struct linked_list_t *event_queue = self->event_queue;
	struct x86_uop_t *uop;

	if (!linked_list_count(event_queue))
		return NULL;

	linked_list_head(event_queue);
	uop = linked_list_get(event_queue);
	assert(x86_uop_exists(uop));
	assert(uop->in_event_queue);
	linked_list_remove(event_queue);
	uop->in_event_queue = 0;
	return uop;
}

static int X86ThreadIPredictorCacheMissEventUpdate(X86Thread *self, struct x86_uop_t *uop)
{
	unsigned int bht_index;
	unsigned int bhr;  
	struct x86_bpred_t *bpred = self->bpred;
	bht_index = uop->eip & (x86_bpred_twolevel_l1size - 1);
	bhr = bpred->twolevel_bht[bht_index];
        unsigned int eip = uop->eip^bhr;
      
       struct x86_inst_pred_t *pred = 0;
      
       // Find the instr ptr
       for (int i =0; i<MAX_PRED_BUF; i++)
       {
            if (self->ipred[i].pc == eip)
               { 
                    pred = &(self->ipred[i]);
               }
       }
        
       if (pred == 0)
       { 
            return 0;
       } 
       else
       {
	       if (x86_tracing())
	       {
		   x86_trace("Thread[%s]- found PC in ipred for cache miss event : %d", self->name, pred->pc);
	       }
       }
       
       int curr_pattern_ind = pred->curr_pattern_index;
       struct x86_inst_pattern_t *pattern = &pred->pattern_history[curr_pattern_ind];
       enum x86_uinst_flag_t flags = uop->flags;
       float avg_inst_count = 0;
       long long total_inst_count = 0;
       
       int prev_pattern_ind = curr_pattern_ind - 1;
       if (prev_pattern_ind < 0)
       {
          if (pred->total_pattern_processed > MAX_PATTERN_ITER_COUNT)
          {
             prev_pattern_ind = MAX_PATTERN_ITER_COUNT;
          } 
       } 
       struct x86_inst_pattern_t *prev_pattern = &pred->pattern_history[prev_pattern_ind>=0?prev_pattern_ind:0];

       pattern->cache_miss++;
       if (x86_tracing())
       {
	   x86_trace("Thread[%s]- [%d]update on cache miss event : %d, %lld,%lld,%lld,%lld,%lld,%lld, %lld, %d, %lld, %lld, %f\n",
           self->name,
           pred->pc,
           curr_pattern_ind,
           pattern->uinst_total,
           pattern->uinst_int_count,
           pattern->uinst_logic_count,
           pattern->uinst_fp_count,
           pattern->uinst_mem_count,
           pattern->uinst_ctrl_count,
           pattern->cache_miss,
	   pred->next_pred_mem_inst_distance,
           pred->total_pattern_processed,
           total_inst_count, 
           avg_inst_count
           );  
        }
        return 1;
}

static int X86ThreadIPredictorLongLatencyCheck(X86Thread *self, struct x86_uop_t *uop)
{
	unsigned int bht_index;
	unsigned int bhr;  
	struct x86_bpred_t *bpred = self->bpred;
	bht_index = uop->eip & (x86_bpred_twolevel_l1size - 1);
	bhr = bpred->twolevel_bht[bht_index];
        unsigned int eip = uop->eip^bhr;
      
       struct x86_inst_pred_t *pred = 0;
      
       // Find the instr ptr
       for (int i =0; i<MAX_PRED_BUF; i++)
       {
            if (self->ipred[i].pc == eip)
               { 
                    pred = &(self->ipred[i]);
               }
       }
        
       if (pred == 0)
       { 
            return 0;
       } 
       else
       {
	       if (x86_tracing())
	       {
		   x86_trace("Thread[%s]- X86ThreadIPredictorLongLatencyCheck PC in ipred for cache miss event : %d", self->name, pred->pc);
	       }
       }

 
       int curr_pattern_ind = pred->curr_pattern_index;
       struct x86_inst_pattern_t *pattern = &pred->pattern_history[curr_pattern_ind];
               
       int predicted_cycles_for_next_ll_event = 0;

#define PREDICTION_THRESHOLD 5      
       if (curr_pattern_ind > PREDICTION_THRESHOLD) 
       {
               //start1,end1  start2,end2
               //end2 = start2 + (end1 - start1) -issue_when
               struct x86_inst_pattern_t *prev_pattern = &pred->pattern_history[curr_pattern_ind-1];
               predicted_cycles_for_next_ll_event = pattern->start_cycle + 
                        (prev_pattern->end_cycle - prev_pattern->start_cycle) + pattern->uinst_issue_when;  
       } 
       
       return predicted_cycles_for_next_ll_event; 
}

#define ABS(n) { n > 0 ? n : -n; }

static int X86ThreadIPredictorLLEventUpdate(X86Thread *self, struct x86_uop_t *uop)
{
	struct x86_bpred_t *bpred = self->bpred;
	unsigned int bht_index;
	unsigned int bhr;  
	bht_index = uop->eip & (x86_bpred_twolevel_l1size - 1);
	bhr = bpred->twolevel_bht[bht_index];
        unsigned int eip = uop->eip^bhr;

       int created = 0; 
       struct x86_inst_pred_t *pred = 0;
       int curr_pattern_ind = 0;
       struct x86_inst_pattern_t *pattern = 0;
       struct x86_inst_pattern_t *prev_pattern = 0;
       X86Cpu *cpu = self->cpu;
       

       //Find or create a PC in table
       for (int i =0; i<MAX_PRED_BUF; i++)
       {
            if (self->ipred[i].pc == eip)
               { 
                    pred = &(self->ipred[i]);
               }
       }
        
       if (pred == 0)
       {
           if (self->ipred_index <= MAX_PRED_BUF) 
           {
               pred = &(self->ipred[self->ipred_index]);
               self->ipred[self->ipred_index].pc = eip;
               self->ipred_index++;
               created = 1;  
           }else
           {
               return 0;
           }
       } 
       if (x86_tracing())
       {
	   x86_trace("Thread[%s]- added/found PC in ipred for long latency event created[%s] : %d\n", self->name, created?"true":"false",pred->pc);
       }
      
       if (created)
       {
                    pred->curr_pattern_index = 0;
                    pattern = &pred->pattern_history[0];
                    pattern->start_cycle = (asTiming(cpu)->cycle);
                    return 1;
       } 
       
       curr_pattern_ind = pred->curr_pattern_index;
       pattern = &pred->pattern_history[curr_pattern_ind];
       pattern->end_cycle = (asTiming(cpu)->cycle); // The current running pattern ended here
       pattern->life_time = (pattern->end_cycle - pattern->start_cycle);
       
       int prev_pattern_ind = curr_pattern_ind - 1;
       if (prev_pattern_ind < 0)
       {
          if (pred->total_pattern_processed > MAX_PATTERN_ITER_COUNT)
          {
             prev_pattern_ind = MAX_PATTERN_ITER_COUNT;
          } 
       }
       
#define THESHOLD_PATTERN_ITER_COUNT 3
#define THESHOLD_CYCLE_DIFF 50

#define INC_CONF(c, MAX) if (c < MAX) c++;
#define DEC_CONF(c, MIN) if (c > MIN) c--;

       if (curr_pattern_ind > THESHOLD_PATTERN_ITER_COUNT + 1) /* For simplicity taking only values > 3 */
       {
               /* Record the accuracy of prediction */
               pred->accesses++;  
               if (((asTiming(cpu)->cycle) - (pred->when_predicted + pred->remaining_cycles)) < 20)
               {
                    pred->hits++;  
		    INC_CONF(pred->confidence,2); 
               }
               else
               {
		    DEC_CONF(pred->confidence,0); 
               }    
#if 0
	       fprintf(stderr, "Thread[%s] ctx:%d- PC[%d] pattern[%lld, %lld, %lld] remaining cycles:%d when:%lld curr cycles:%lld accesses:%lld hits:%lld conf:%d\n", 
                               self->name, self->ctx->pid, pred->pc, 
			       pred->pattern_history[curr_pattern_ind -2].life_time, 
			       pred->pattern_history[curr_pattern_ind -1].life_time, 
			       pred->pattern_history[curr_pattern_ind].life_time,
			       pred->remaining_cycles, 
			       pred->when_predicted,
			       (asTiming(cpu)->cycle),
                               pred->accesses, pred->hits, pred->confidence);
#endif
       }

       if (curr_pattern_ind > THESHOLD_PATTERN_ITER_COUNT) /* For simplicity taking only values > 3 */
       {

	       /*Make a prediction by identifying the pattern */
#define MAX_THRESHOLD 10
               int diff_array[MAX_THRESHOLD];

               for (int i = 0 ; i < (THESHOLD_PATTERN_ITER_COUNT - 1) ; i++)
               { 
                      pattern = &pred->pattern_history[curr_pattern_ind - i];
                      prev_pattern = &pred->pattern_history[curr_pattern_ind -i -1]; 
                      diff_array[i] = pattern->life_time - prev_pattern->life_time; 
               }

	       int equal_phase = 0;
	       int stable_phase = 0;
	       int alt_phase = 0;

               for (int i = 0 ; i < (THESHOLD_PATTERN_ITER_COUNT - 2) ; i++)
               { 
                      if ((diff_array[i] == 0) && (diff_array[i+1] == 0))
                      {
                                equal_phase++;
                      }
                      else if ((ABS(diff_array[i])) == (ABS(diff_array[i+1])))
		      { 
			        alt_phase++;
		      }
                      else if (((ABS(diff_array[i])) + (ABS(diff_array[i+1]))) < THESHOLD_CYCLE_DIFF)
                      {
                                stable_phase++;
                      }    
               }
		
	       if (equal_phase)
	       {
		       pred->equal_phase = 1; 
		       pred->remaining_cycles = pattern->life_time; 
		       pred->when_predicted = (asTiming(cpu)->cycle);
		       INC_CONF(pred->confidence,2); 
                       fprintf(stderr, "Thread[%s] ctx:%d- PC[%d] equal phase ON: [%d, %d] [%lld, %lld, %lld] remaining cycles:%d curr cycles:%lld access:%lld hits:%lld conf:%d\n", self->name, self->ctx->pid, pred->pc,
diff_array[0],
diff_array[1],
pred->pattern_history[curr_pattern_ind -2].life_time,
pred->pattern_history[curr_pattern_ind -1].life_time,
pred->pattern_history[curr_pattern_ind].life_time,
pred->remaining_cycles, (asTiming(cpu)->cycle),
pred->accesses, pred->hits, pred->confidence);

	       }         
	       else if (alt_phase)
	       {
		       pred->alt_phase=1; 
		       pred->remaining_cycles = pattern->life_time; 
		       pred->when_predicted = (asTiming(cpu)->cycle);
		       INC_CONF(pred->confidence,2); 
                       fprintf(stderr, "Thread[%s] ctx:%d- PC[%d] alt phase ON: [%d, %d]  [%lld, %lld, %lld] remaining cycles:%d curr cycles:%lld access:%lld hits:%lld conf:%d\n", self->name, self->ctx->pid, pred->pc,
diff_array[0],
diff_array[1],
pred->pattern_history[curr_pattern_ind -2].life_time,
pred->pattern_history[curr_pattern_ind -1].life_time,
pred->pattern_history[curr_pattern_ind].life_time,
pred->remaining_cycles, (asTiming(cpu)->cycle),
pred->accesses, pred->hits, pred->confidence);

	       }               
	       else if (stable_phase)
	       {
		       pred->stable_phase=1;  	                       
		       pred->remaining_cycles = pattern->life_time; 
		       pred->when_predicted = (asTiming(cpu)->cycle);
		       //pred->confidence = 2; 
	       fprintf(stderr, "Thread[%s] ctx:%d- PC[%d] No phase is ON: [%d, %d] [%lld, %lld, %lld] remaining cycles:%d curr cycles:%lld access:%lld hits:%lld conf:%d\n", self->name, self->ctx->pid, pred->pc,
diff_array[0],
diff_array[1],
pred->pattern_history[curr_pattern_ind -2].life_time,
pred->pattern_history[curr_pattern_ind -1].life_time,
pred->pattern_history[curr_pattern_ind].life_time,
pred->remaining_cycles, (asTiming(cpu)->cycle),
pred->accesses, pred->hits, pred->confidence);
	       } 
               else //random
               {
		       pred->equal_phase = 0; 
		       pred->alt_phase=0; 
		       pred->stable_phase=0;  	                       
		       pred->remaining_cycles = pattern->life_time; 
		       pred->when_predicted = (asTiming(cpu)->cycle);
	       fprintf(stderr, "Thread[%s] ctx:%d- PC[%d] No phase [%d, %d] [%lld, %lld, %lld] remaining cycles:%d curr cycles:%lld access:%lld hits:%lld conf:%d\n", self->name, self->ctx->pid, pred->pc,
diff_array[0],
diff_array[1],
pred->pattern_history[curr_pattern_ind -2].life_time,
pred->pattern_history[curr_pattern_ind -1].life_time,
pred->pattern_history[curr_pattern_ind].life_time,
pred->remaining_cycles, (asTiming(cpu)->cycle),
pred->accesses, pred->hits, pred->confidence);
              }
 
       }


       enum x86_uinst_flag_t flags = uop->flags;
       long long total_inst_count = 0;
       float avg_inst_count = 0;
       prev_pattern = &pred->pattern_history[prev_pattern_ind>=0?prev_pattern_ind:0];
       total_inst_count += pred->pattern_history[curr_pattern_ind].uinst_total;
       pred->total_pattern_processed++;
       pattern->uinst_issue_when=uop->issue_when;


       if (x86_tracing())
       {
	   x86_trace("Thread[%s]- update on longlatency event dist: %d, %d, %lld,%lld,%lld,%lld,%lld,%lld,%lld, %d, %lld, %lld,  %lld,%lld, %f\n",
           self->name,
           pred->pc,
           curr_pattern_ind,
           pattern->cache_miss,
           pattern->uinst_total,
           pattern->uinst_int_count,
           pattern->uinst_logic_count,
           pattern->uinst_fp_count,
           pattern->uinst_mem_count,
           pattern->uinst_ctrl_count,
	   pred->next_pred_mem_inst_distance,
           pred->total_pattern_processed,
           (pattern->start_cycle - prev_pattern->start_cycle),
           (pattern->start_cycle - prev_pattern->uinst_issue_when),
           total_inst_count, 
           avg_inst_count
           );  
     
           if (prev_pattern_ind >= 0)
           { 
		   x86_trace("Thread[%s]- distance on longlatency event:" 
                              "%d, %lld,%lld,%lld,%lld,%lld,%lld, %lld, %lld, %d, %lld, %lld, %f\n",
		   self->name,
		   curr_pattern_ind - prev_pattern_ind,
		   pattern->uinst_total - prev_pattern->uinst_total,
		   pattern->uinst_int_count - prev_pattern->uinst_int_count,
		   pattern->uinst_logic_count - prev_pattern->uinst_logic_count,
		   pattern->uinst_fp_count - prev_pattern->uinst_fp_count,
		   pattern->uinst_mem_count - prev_pattern->uinst_mem_count,
		   pattern->uinst_ctrl_count - prev_pattern->uinst_ctrl_count,
		   pattern->cache_miss - prev_pattern->cache_miss,
                   pattern->cache_miss,
		   pred->next_pred_mem_inst_distance,
		   pred->total_pattern_processed,
		   total_inst_count, 
		   avg_inst_count
		   );
           }  
       }
 {      
       curr_pattern_ind = (curr_pattern_ind + 1) % MAX_PATTERN_ITER_COUNT;
       pred->curr_pattern_index = curr_pattern_ind;
       //Reset used pattern
       pattern = &(pred->pattern_history[pred->curr_pattern_index]);
       pattern->uinst_int_count=0;
       pattern->uinst_logic_count=0;
       pattern->uinst_fp_count=0;
       pattern->uinst_mem_count=0;
       pattern->uinst_ctrl_count=0;
       pattern->uinst_total=0;
       
       pattern->start_cycle=(asTiming(cpu)->cycle);
       pattern->end_cycle=0;
}
       return 1;
}

static int X86ThreadIPredictorLongLatencyPredict(X86Thread *self)
{
    /*   if (self == 0)
          return 0;
 */
       //For last few uop in queue:
       X86Core *core = self->core;
       X86Cpu *cpu = self->cpu;

       struct list_t *uop_queue = self->uop_queue;
       struct x86_uop_t *uop;
       int inst_count = list_count(uop_queue);
       int min_pred_cycles = 1000000; 
       int pred_cycles = 0; 
       while (inst_count > 0)
       {
	       uop = list_get(uop_queue, inst_count - 1);
	       if (uop->flags & X86_UINST_MEM)
               {
                         pred_cycles = X86ThreadIPredictorLongLatencyCheck(self, uop);   
			 if (pred_cycles < min_pred_cycles)
			 {
                                min_pred_cycles = pred_cycles;
                         }      
               }
               inst_count--;
       }
       return min_pred_cycles;
}

X86Thread * X86ThreadIPredictorNextThread(X86Core *core, X86Thread *self)
{
	int num_thread = x86_cpu_num_threads;
	X86Thread *next = 0;
	X86Thread *best = 0;
        int pred_cycles = 0;
        int max_pred_cycles = 0;
        int i=0;
	do
	{
	        //fprintf(stderr, "loop %d cores' thread \n", i);
		next = core->threads[i];
		if (next == self)
		{
                        i++;      
			continue;
		} 
                //pred_cycles = X86ThreadIPredictorLongLatencyPredict(next); 
                pred_cycles = next->ctx->ll_pred_remaining_cycles; 
                if (pred_cycles > max_pred_cycles)
                {
                        best = next;
                        max_pred_cycles = pred_cycles;
                } 
                i++;      
	} while (i < num_thread);

        return next; 
}

/*
 * Class 'X86Thread'
 */

int X86ThreadLongLatencyInEventQueue(X86Thread *self)
{
	X86Cpu *cpu = self->cpu;
	X86Core *core = self->core;

	struct linked_list_t *event_queue = core->event_queue;
	struct x86_uop_t *uop;
	
	LINKED_LIST_FOR_EACH(event_queue)
	{
		uop = linked_list_get(event_queue);
		if (uop->thread != self)
			continue;
                //Pallavi - Predict a long latency event here
                //Add prediction of ll event per core and choose 
                //Next appropriate thread from here and start prefetching
                //For the incoming thread.
#define TIMEFORPREFETCH 100
                //int pred_cycles = X86ThreadIPredictorLongLatencyCheck(self, uop);
                int pred_cycles = self->ctx->ll_pred_remaining_cycles;
                if ((pred_cycles) <= TIMEFORPREFETCH)
		{
			if (x86_tracing())
			{
                            x86_trace("Pallavi - predicted memory event dist: %d\n", pred_cycles);  
			}
                        //Start Prefetcher for next thread
                        X86Thread *next = X86ThreadIPredictorNextThread(core, self); 
                        //Memory_Drived_Prefetch(next); 
		}

                enum x86_uinst_flag_t flags = uop->flags;
                if ((flags & X86_UINST_MEM) && (asTiming(cpu)->cycle - uop->issue_when > 9))
		{
                        X86ThreadIPredictorCacheMissEventUpdate(self, uop);
			/*if (x86_tracing())
			{
                            x86_trace("Cahce Miss Event:: ipred memory pred dist: \n");
			}*/
		}
		if (asTiming(cpu)->cycle - uop->issue_when > 20)
		{
                        X86ThreadIPredictorLLEventUpdate(self, uop);
			/*if (x86_tracing())
			{
                            x86_trace("LLEvent:: ipred memory pred dist: \n");
			}*/
			return 1;
		}
	}
	return 0;
}


int X86ThreadCacheMissInEventQueue(X86Thread *self)
{
	X86Cpu *cpu = self->cpu;
	X86Core *core = self->core;

	struct linked_list_t *event_queue = core->event_queue;
	struct x86_uop_t *uop;

	LINKED_LIST_FOR_EACH(event_queue)
	{
		uop = linked_list_get(event_queue);
		if (uop->thread != self || uop->uinst->opcode != x86_uinst_load)
			continue;
		if (asTiming(cpu)->cycle - uop->issue_when > 5)
			return 1;
	}
	return 0;
}


void X86ThreadRecoverEventQueue(X86Thread *self)
{
	X86Core *core = self->core;

	struct linked_list_t *event_queue = core->event_queue;
	struct x86_uop_t *uop;

	linked_list_head(event_queue);
	while (!linked_list_is_end(event_queue))
	{
		uop = linked_list_get(event_queue);
		if (uop->thread == self && uop->specmode)
		{
			linked_list_remove(event_queue);
			uop->in_event_queue = 0;
			x86_uop_free_if_not_queued(uop);
			continue;
		}
		linked_list_next(event_queue);
	}
}


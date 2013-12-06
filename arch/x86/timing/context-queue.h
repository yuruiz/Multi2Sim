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

#ifndef X86_ARCH_TIMING_CTX_QUEUE_H
#define X86_ARCH_TIMING_CTX_QUEUE_H

/*
 * Public
 */
extern int x86_ctx_queue_size;
extern long long max_idle_time;


/*
 * Class 'X86Thread'
 */

void X86ThreadInitCtxQueue(X86Thread *self);
void X86ThreadFreeCtxQueue(X86Thread *self);
void X86ThreadRecoverCtxQueue(X86Thread *self);
void X86_ADD_EVICTED_CONTEXT(X86Thread *self,X86Context *ctx);
#define X86ProjectDebug(...) debug(x86_project_debug_category, __VA_ARGS__)
extern int x86_project_debug_category;


extern struct X86Context *LOAD_BEST_CONTEXT(X86Thread *self);
int RANK_MRU(X86Thread *self,int outgoing_best_thread);
int RANK_RAND(X86Thread *self,long long seed);
int RANK_ROUND_ROBIN(X86Thread *self);
extern void X86_REMOVE_EVICTED_CONTEXT(X86Thread *self,X86Context *ctx);
extern int CAN_MAP_CONTEXT(X86Thread *self);







#endif


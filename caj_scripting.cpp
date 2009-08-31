/* Copyright (c) 2009 Aidan Thornton, all rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AIDAN THORNTON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AIDAN THORNTON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cajeput_core.h"
#include "cajeput_world.h"
#include "caj_vm.h"

struct sim_scripts {
  GThread *thread;
  GAsyncQueue *outq;
  GAsyncQueue *inq;
};

struct list_head {
  struct list_head *next, *prev;
};

struct sim_script {
  // section used by scripting thread
  list_head list;
  int is_running;
  script_state *vm;

  // section used by main thread
  
};

#define CAJ_SMSG_SHUTDOWN 0

struct script_msg {
  int msg_type;
  union {
  } u;
};

static void list_head_init(list_head *head) {
  head->next = head; head->prev = head;
}

static void list_remove(list_head *item) {
  item->prev->next = item->next;
  item->next->prev = item->prev;
}

static void list_insert_after(list_head *item, list_head *here) {
  item->next = here->next; item->prev = here;
  here->next->prev = item; here->next = item;
}

static gpointer script_thread(gpointer data) {
  sim_scripts *simscr = (sim_scripts*)data;
  list_head running, waiting;
  list_head_init(&running); list_head_init(&waiting);
  for(;;) {
    for(int i = 0; i < 20; i++) {
      if(running.next == &running); break;

      sim_script *scr = (sim_script*)running.next; 
      assert(scr->is_running);
      if(vm_script_is_idle(scr->vm)) {
	// FIXME - schedule events
      }
      if(vm_script_is_runnable(scr->vm)) {
	vm_run_script(scr->vm, 100);
      } else {
	// deschedule.
	scr->is_running = 0;  list_remove(&scr->list);
	list_insert_after(&scr->list, &waiting);
      }
    }

    script_msg *msg;
    for(;;) {
      if(running.next == &running)
	msg = (script_msg*)g_async_queue_pop(simscr->outq);
      else msg = (script_msg*)g_async_queue_try_pop(simscr->outq);
      if(msg == NULL) break;
      switch(msg->msg_type) {
      case CAJ_SMSG_SHUTDOWN:
	return NULL;
      }
    }
  }
}

static void shutdown_scripting(struct simulator_ctx *sim, void *priv) {
  sim_scripts *simscr = (sim_scripts*)priv;
  {
    script_msg *msg = new script_msg();
    msg->msg_type = CAJ_SMSG_SHUTDOWN;
    g_async_queue_push(simscr->outq, msg);
    g_thread_join(simscr->thread);
  }
}

int caj_scripting_init(int api_version, struct simulator_ctx* sim, 
		       void **priv, struct cajeput_script_hooks *hooks) {
  sim_scripts *simscr = new sim_scripts(); *priv = simscr;
  simscr->thread = g_thread_create(script_thread, simscr, TRUE, NULL);
  simscr->outq = g_async_queue_new();
  simscr->inq = g_async_queue_new();
  assert(simscr->outq != NULL); assert(simscr->inq != NULL); 
  if(simscr->thread == NULL) {
    printf("ERROR: couldn't create script thread\n"); exit(1);
  }

  hooks->shutdown = shutdown_scripting;

  return 1;
}

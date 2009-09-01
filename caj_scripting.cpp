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
#include <fcntl.h>

struct sim_scripts {
  GThread *thread;
  GAsyncQueue *outq;
  GAsyncQueue *inq;
};

struct list_head {
  struct list_head *next, *prev;
};

#define SCR_MT_COMPILING 1
#define SCR_MT_COMPILE_ERROR 2
#define SCR_MT_RUNNING 3
#define SCR_MT_PAUSED 4

struct sim_script {
  // section used by scripting thread
  list_head list;
  int is_running;
  script_state *vm;
  int state_entry; // HACK!

  // section used by main thread
  int mt_state; // state as far as main thread is concerned
  primitive_obj *prim;

  // used by both threads, basically read-only 
  sim_scripts *simscr;
  char *cvm_file; // basically the script executable
};

#define CAJ_SMSG_SHUTDOWN 0
#define CAJ_SMSG_ADD_SCRIPT 1
#define CAJ_SMSG_REMOVE_SCRIPT 2

struct script_msg {
  int msg_type;
  sim_script *scr;
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

static unsigned char *read_file_data(const char *name, int *lenout) {
  int len = 0, maxlen = 512, ret;
  unsigned char *data = (unsigned char*)malloc(maxlen);
  //FILE *f = fopen(name,"r");
  int fd = open(name,O_RDONLY);
  if(fd < 0) return NULL;
  for(;;) {
    //ret = fread(data+len, maxlen-len, 1, f);
    ret = read(fd, data+len, maxlen-len);
    if(ret <= 0) break;
    len += ret;
    if(maxlen-len < 256) {
      maxlen *= 2;
      data = (unsigned char*)realloc(data, maxlen);
    }
  }
  close(fd); *lenout = len; return data;
}

static void st_load_script(sim_script *scr) {
  scr->vm = NULL;
  int len;
  unsigned char *dat = read_file_data(scr->cvm_file, &len);
  if(dat == NULL) { printf("ERROR: can't read script file?!\n"); return; }

  scr->vm = vm_load_script(dat, len);
  if(scr->vm == NULL) { printf("ERROR: couldn't load script\n"); return; }

  vm_prepare_script(scr->vm, scr, NULL, NULL); // FIXME!!!
}

static uint8_t empty_arglist[] = { VM_TYPE_NONE };
static uint8_t detected_arglist[] = { VM_TYPE_INT, VM_TYPE_NONE };


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
#if 0
	if(scr->_state_entry) {
	  vm_call_func_hack(scr->vm,"0:state_entry",empty_arglist);
	}
#endif
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
      case CAJ_SMSG_ADD_SCRIPT:
	st_load_script(msg->scr);
	if(msg->scr->vm != NULL) {
	  // FIXME - should insert at end of running queue
	  list_insert_after(&msg->scr->list, &running);
	} else {
	  // FIXME - what to do?
	  list_insert_after(&msg->scr->list, &waiting);
	}
	break;
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

static void save_script_text_file(sl_string *dat, char *name) {
  int len = dat->len;  if(len > 0 && dat->data[len-1] == 0) len--;
  int fd = open(name, O_WRONLY|O_CREAT|O_EXCL, 0644);
  if(fd < 0) {
    printf("ERROR: couldn't open script file for save\n"); return;
  }
  size_t off = 0; ssize_t ret;
  while(off < len) {
    ret = write(fd, dat->data+off, len-off);
    if(ret <= 0) { perror("ERROR: saving script file"); close(fd); return; }
    off += ret;
  }
  close(fd);
}

// internal function, main thread
static void mt_free_script(sim_script *scr) {
  // FIXME - TODO!!!
}

static void mt_enable_script(sim_script *scr) {
  scr->mt_state = SCR_MT_RUNNING;
  
  script_msg *msg = new script_msg();
  msg->msg_type = CAJ_SMSG_ADD_SCRIPT;
  msg->scr = scr;
  g_async_queue_push(scr->simscr->outq, msg);
}

static void compile_done(GPid pid, gint status,  gpointer data) {
  sim_script *scr = (sim_script*)data;
  g_spawn_close_pid(pid);
  printf("DEBUG: script compile done, result %i\n", status);
  
  if(WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    if(scr->prim != NULL) {
      mt_enable_script(scr);
    }
  } else {
    printf("ERROR: script compile failed\n");
    scr->mt_state = SCR_MT_COMPILE_ERROR;
  }

  if(scr->prim == NULL) {
    mt_free_script(scr);
  }
}

static void* add_script(simulator_ctx *sim, void *priv, primitive_obj *prim, 
			inventory_item *inv, simple_asset *asset) {
  sim_scripts *simscr = (sim_scripts*)priv;
  char *args[4]; 
  char buf[40], srcname[60], binname[60]; GPid pid;
  uuid_unparse(asset->id, buf);
  snprintf(srcname, 60, "script_tmp/%s.lsl", buf); 
  snprintf(binname, 60, "script_tmp/%s.cvm", buf);
  printf("DEBUG: compiling and adding script\n");
  save_script_text_file(&asset->data, srcname);
  
  args[0] = "./lsl_compile"; args[1] = srcname; args[2] = binname; args[3] = 0;
  if(!g_spawn_async(NULL, args, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
		    NULL, NULL, &pid, NULL)) {
    printf("ERROR: couldn't launch script compiler\n"); return NULL;
  }

  sim_script *scr = new sim_script();
  scr->prim = prim; scr->mt_state = SCR_MT_COMPILING;
  scr->is_running = 0; scr->simscr = simscr;
  scr->cvm_file = strdup(binname);
  // we don't bother filling out the first half of the struct yet...

  g_child_watch_add(pid, compile_done, scr);
  return scr;
}

int caj_scripting_init(int api_version, struct simulator_ctx* sim, 
		       void **priv, struct cajeput_script_hooks *hooks) {
  sim_scripts *simscr = new sim_scripts(); *priv = simscr;
  simscr->outq = g_async_queue_new();
  simscr->inq = g_async_queue_new();
  simscr->thread = g_thread_create(script_thread, simscr, TRUE, NULL);
  assert(simscr->outq != NULL); assert(simscr->inq != NULL); 
  if(simscr->thread == NULL) {
    printf("ERROR: couldn't create script thread\n"); exit(1);
  }

  mkdir("script_tmp/", 0755);

  hooks->shutdown = shutdown_scripting;
  hooks->add_script = add_script;

  return 1;
}

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
  simulator_ctx *sim;

  // these are used by both main and scripting threads. Don't modify them.
  GAsyncQueue *to_st;
  GAsyncQueue *to_mt;
  vm_world *vmw;
};

struct list_head {
  struct list_head *next, *prev;
};

#define SCR_MT_COMPILING 1
#define SCR_MT_COMPILE_ERROR 2
#define SCR_MT_RUNNING 3
#define SCR_MT_PAUSED 4

#define SCRIPT_MAGIC 0xd0f87153

struct compiler_output {
  GIOChannel *stdout, *stderr;
  int len, buflen;
  char *buf;
  compile_done_cb cb; void *cb_priv;
};

struct sim_script {
  // section used by scripting thread
  list_head list;
  int is_running;
  script_state *vm;
  int state_entry; // HACK!

  // section used by main thread
  int mt_state; // state as far as main thread is concerned
  primitive_obj *prim;
  compiler_output *comp_out;

  // used by both threads, basically read-only 
  uint32_t magic;
  sim_scripts *simscr;
  char *cvm_file; // basically the script executable
};

#define CAJ_SMSG_SHUTDOWN 0
#define CAJ_SMSG_ADD_SCRIPT 1
#define CAJ_SMSG_REMOVE_SCRIPT 2
#define CAJ_SMSG_LLSAY 3
#define CAJ_SMSG_KILL_SCRIPT 4
#define CAJ_SMSG_SCRIPT_KILLED 5

struct script_msg {
  int msg_type;
  sim_script *scr;
  union {
    struct {
      int32_t channel; char* msg; int chat_type;
    } say;
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

  scr->vm = vm_load_script(dat, len); free(dat);
  if(scr->vm == NULL) { printf("ERROR: couldn't load script\n"); return; }

  vm_prepare_script(scr->vm, scr, scr->simscr->vmw); 
}

static void send_to_mt(sim_scripts *simscr, script_msg *msg) {
  g_async_queue_push(simscr->to_mt, msg);
}

static void do_say(sim_script *scr, int chan, char* message, int chat_type) {
  script_msg *smsg = new script_msg();
  smsg->msg_type = CAJ_SMSG_LLSAY;
  smsg->scr = scr;
  smsg->u.say.channel = chan;
  smsg->u.say.msg = message;
  smsg->u.say.chat_type = chat_type;
  send_to_mt(scr->simscr, smsg);
}

static void llSay_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int chan; char* message;
  vm_func_get_args(st, func_id, &chan, &message);

  printf("DEBUG: llSay on %i: %s\n", chan, message);
  do_say(scr, chan, message, CHAT_TYPE_NORMAL);

  vm_func_return(st, func_id);
}

static void llShout_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int chan; char* message;
  vm_func_get_args(st, func_id, &chan, &message);
  do_say(scr, chan, message, CHAT_TYPE_SHOUT);
  vm_func_return(st, func_id);
}

static void llWhisper_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int chan; char* message;
  vm_func_get_args(st, func_id, &chan, &message);
  do_say(scr, chan, message, CHAT_TYPE_WHISPER);
  vm_func_return(st, func_id);
}

static gpointer script_thread(gpointer data) {
  sim_scripts *simscr = (sim_scripts*)data;
  list_head running, waiting;
  list_head_init(&running); list_head_init(&waiting);
  for(;;) {
    for(int i = 0; i < 20; i++) {
      if(running.next == &running) break;

      sim_script *scr = (sim_script*)running.next; 
      printf("DEBUG: handling script on run queue\n");
      assert(scr->is_running);
      if(vm_script_is_idle(scr->vm)) {
	printf("DEBUG: script idle\n");
	if(scr->state_entry) {
	  printf("DEBUG: calling state_entry\n");
	  scr->state_entry = 0;
	  vm_call_event(scr->vm,"state_entry");
	}
	// FIXME - schedule events
      }
      if(vm_script_is_runnable(scr->vm)) {
	printf("DEBUG: script runnable\n");
	vm_run_script(scr->vm, 100);
      } else {
	printf("DEBUG: removing script from run queue\n");
	// deschedule.
	scr->is_running = 0;  list_remove(&scr->list);
	list_insert_after(&scr->list, &waiting);
      }
    }

    script_msg *msg;
    for(;;) {
      if(running.next == &running)
	msg = (script_msg*)g_async_queue_pop(simscr->to_st);
      else msg = (script_msg*)g_async_queue_try_pop(simscr->to_st);
      if(msg == NULL) break;

      switch(msg->msg_type) {
      case CAJ_SMSG_SHUTDOWN:
	delete msg;
	return NULL;
      case CAJ_SMSG_ADD_SCRIPT:
	printf("DEBUG: handling ADD_SCRIPT\n");
	st_load_script(msg->scr);
	if(msg->scr->vm != NULL) {
	  printf("DEBUG: adding to run queue\n");
	  // FIXME - should insert at end of running queue
	  msg->scr->is_running = 1;
	  list_insert_after(&msg->scr->list, &running);
	} else {
	  printf("DEBUG: failed to load script\n");
	  // FIXME - what to do?
	  msg->scr->is_running = 0;
	  list_insert_after(&msg->scr->list, &waiting);
	}
	break;
      case CAJ_SMSG_KILL_SCRIPT:
	printf("DEBUG: got KILL_SCRIPT\n");
	list_remove(&msg->scr->list);
	msg->msg_type = CAJ_SMSG_SCRIPT_KILLED;
	send_to_mt(simscr, msg); msg = NULL;
	break;
      }

      delete msg;
    }
  }
}

// --------------- main thread code ------------------------

static void send_to_script(sim_scripts *simscr, script_msg *msg) {
  g_async_queue_push(simscr->to_st, msg);
}

static void shutdown_scripting(struct simulator_ctx *sim, void *priv) {
  sim_scripts *simscr = (sim_scripts*)priv;
  {
    script_msg *msg = new script_msg();
    msg->msg_type = CAJ_SMSG_SHUTDOWN;
    g_async_queue_push(simscr->to_st, msg);
    g_thread_join(simscr->thread);
  }

  vm_world_free(simscr->vmw);
  delete simscr;
}

static void save_script_text_file(sl_string *dat, char *name) {
  int len = dat->len;  if(len > 0 && dat->data[len-1] == 0) len--;
  int fd = open(name, O_WRONLY|O_CREAT|O_EXCL, 0644);
  if(fd < 0) {
    printf("ERROR: couldn't open script file for save\n"); return;
  }
  int off = 0; ssize_t ret;
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
  free(scr->cvm_file);
  delete scr;
}

static void mt_enable_script(sim_script *scr) {
  scr->mt_state = SCR_MT_RUNNING;
  
  scr->state_entry = 1; // HACK!!!
  script_msg *msg = new script_msg();
  msg->msg_type = CAJ_SMSG_ADD_SCRIPT;
  msg->scr = scr;
  g_async_queue_push(scr->simscr->to_st, msg);
}

static void flush_compile_output(GIOChannel *source, compiler_output *outp) {
  gsize len_read = 0;
  do {
    if(outp->buflen - outp->len < 256) {
      outp->buflen *= 2;
      outp->buf = (char*)realloc(outp->buf, outp->buflen);
      if(outp->buf == NULL) abort();
    }

    if(G_IO_ERROR_NONE != g_io_channel_read(source, outp->buf+outp->len, 
					    (outp->buflen - outp->len) - 1, 
					    &len_read)) {
      printf("ERROR: couldn't read output from compiler\n"); return;
    }
    outp->len += len_read;
  } while(len_read != 0);
}

static void compile_done(GPid pid, gint status,  gpointer data) {
  int success;
  sim_script *scr = (sim_script*)data;
  g_spawn_close_pid(pid);
  printf("DEBUG: script compile done, result %i\n", status);
  
  if(WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    success = 1;
    if(scr->prim != NULL) {
      mt_enable_script(scr);
    }
  } else {
    printf("ERROR: script compile failed\n");
    success = 0;
    scr->mt_state = SCR_MT_COMPILE_ERROR;
  }

  assert(scr->comp_out != NULL);
  scr->comp_out->buf[scr->comp_out->len] = 0;
  printf("DEBUG: got compiler output: ~%s~\n", scr->comp_out->buf);
  if(scr->comp_out->cb != NULL) {
    scr->comp_out->cb(scr->comp_out->cb_priv, success, scr->comp_out->buf,
		      scr->comp_out->len);
  }
  flush_compile_output(scr->comp_out->stdout, scr->comp_out);
  flush_compile_output(scr->comp_out->stderr, scr->comp_out);
  g_io_channel_shutdown(scr->comp_out->stdout, FALSE, NULL);
  g_io_channel_shutdown(scr->comp_out->stderr, FALSE, NULL);
  g_io_channel_unref(scr->comp_out->stdout);
  g_io_channel_unref(scr->comp_out->stderr);
  free(scr->comp_out->buf); delete scr->comp_out; scr->comp_out = NULL;

  if(scr->prim == NULL) {
    mt_free_script(scr);
  }
}

static gboolean got_compile_output(GIOChannel *source, GIOCondition cond, 
				   gpointer priv) {
  if(cond & G_IO_IN) {
    compiler_output * outp = (compiler_output*)priv;
    if(outp->buflen - outp->len < 256) {
      outp->buflen *= 2;
      outp->buf = (char*)realloc(outp->buf, outp->buflen);
      if(outp->buf == NULL) abort();
    }
    gsize len_read = 0;
    if(G_IO_ERROR_NONE != g_io_channel_read(source, outp->buf+outp->len, 
					    (outp->buflen - outp->len) - 1, 
					    &len_read)) {
    printf("ERROR: couldn't read output from compiler\n"); return TRUE;
    }
    outp->len += len_read;
    return TRUE;
  } 
  if(cond & G_IO_NVAL) return FALSE; // blech. There must be a cleaner way.
  if((cond & (G_IO_IN|G_IO_NVAL)) != cond) {
    printf("FIXME: got_compile_output: unexpected cond\n"); return FALSE;
  }
}

static compiler_output* listen_compiler_output(int stdout_compile, 
					       int stderr_compile) {
  compiler_output* outp = new compiler_output();
  outp->len = 0; outp->buflen = 1024;
  outp->buf = (char*)malloc(outp->buflen);

  // FIXME - not quite Windows-clean.
  outp->stdout = g_io_channel_unix_new(stdout_compile);
  g_io_add_watch(outp->stdout, (GIOCondition)(G_IO_IN|G_IO_NVAL), got_compile_output, outp);
  outp->stderr = g_io_channel_unix_new(stderr_compile);
  g_io_add_watch(outp->stderr, (GIOCondition)(G_IO_IN|G_IO_NVAL), got_compile_output, outp);
  return outp;
}

static void* add_script(simulator_ctx *sim, void *priv, primitive_obj *prim, 
			inventory_item *inv, simple_asset *asset, 
			compile_done_cb cb, void *cb_priv) {
  sim_scripts *simscr = (sim_scripts*)priv;
  char *args[4]; int stdout_compile = -1, stderr_compile = -1;
  char buf[40], srcname[60], binname[60]; GPid pid;
  uuid_unparse(asset->id, buf);
  snprintf(srcname, 60, "script_tmp/%s.lsl", buf); 
  snprintf(binname, 60, "script_tmp/%s.cvm", buf);
  printf("DEBUG: compiling and adding script\n");
  save_script_text_file(&asset->data, srcname);
  
  args[0] = "./lsl_compile"; args[1] = srcname; args[2] = binname; args[3] = 0;
  if(!g_spawn_async_with_pipes(NULL, args, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, 
			       &pid, NULL, &stdout_compile, &stderr_compile, NULL)) {
    printf("ERROR: couldn't launch script compiler\n"); return NULL;
  }

  sim_script *scr = new sim_script();
  scr->prim = prim; scr->mt_state = SCR_MT_COMPILING;
  scr->is_running = 0; scr->simscr = simscr; scr->magic = SCRIPT_MAGIC;
  scr->cvm_file = strdup(binname);
  scr->comp_out = listen_compiler_output(stdout_compile, stderr_compile);
  scr->comp_out->cb = cb; scr->comp_out->cb_priv = cb_priv;

  // we don't bother filling out the first half of the struct yet...

  g_child_watch_add(pid, compile_done, scr);
  return scr;
}

static void kill_script(simulator_ctx *sim, void *priv, void *script) {
  sim_scripts *simscr = (sim_scripts*)priv;
  sim_script *scr = (sim_script*)script;
  assert(scr->magic == SCRIPT_MAGIC);
  scr->prim = NULL;
  if(scr->mt_state == SCR_MT_RUNNING) {
    script_msg *msg = new script_msg();
    msg->msg_type = CAJ_SMSG_KILL_SCRIPT;
    msg->scr = scr;
    send_to_script(simscr, msg);  
  } else if(scr->mt_state != SCR_MT_COMPILING) {
    mt_free_script(scr);
  }
}

static gboolean script_poll_timer(gpointer data) {
  sim_scripts *simscr = (sim_scripts*)data;
  script_msg* msg;
  for(;;) {
    msg = (script_msg*)g_async_queue_try_pop(simscr->to_mt);
    if(msg == NULL) break;

    switch(msg->msg_type) {
    case CAJ_SMSG_LLSAY:
      if(msg->scr->prim != NULL) {
	world_chat_from_prim(simscr->sim, msg->scr->prim, msg->u.say.channel,
			     msg->u.say.msg, msg->u.say.chat_type);
      }
      free(msg->u.say.msg);
      break;
    case CAJ_SMSG_SCRIPT_KILLED:
      assert(msg->scr->prim == NULL);
      mt_free_script(msg->scr);
      break;
    }
    delete msg;
    
  }

  return TRUE;
}

int caj_scripting_init(int api_version, struct simulator_ctx* sim, 
		       void **priv, struct cajeput_script_hooks *hooks) {
  sim_scripts *simscr = new sim_scripts(); *priv = simscr;

  // FIXME - need to check api version!
  
  simscr->sim = sim;
  simscr->vmw = vm_world_new();
  vm_world_add_event(simscr->vmw, "state_entry", VM_TYPE_NONE, 0);
  vm_world_add_func(simscr->vmw, "llSay", VM_TYPE_NONE, llSay_cb, 2, VM_TYPE_INT, VM_TYPE_STR); 
  vm_world_add_func(simscr->vmw, "llShout", VM_TYPE_NONE, llShout_cb, 2, VM_TYPE_INT, VM_TYPE_STR); 
  vm_world_add_func(simscr->vmw, "llWhisper", VM_TYPE_NONE, llWhisper_cb, 2, VM_TYPE_INT, VM_TYPE_STR); 


  simscr->to_st = g_async_queue_new();
  simscr->to_mt = g_async_queue_new();
  simscr->thread = g_thread_create(script_thread, simscr, TRUE, NULL);
  assert(simscr->to_st != NULL); assert(simscr->to_mt != NULL); 
  if(simscr->thread == NULL) {
    printf("ERROR: couldn't create script thread\n"); exit(1);
  }

  g_timeout_add(100, script_poll_timer, simscr); // FIXME - hacky!

  mkdir("script_tmp/", 0755);

  hooks->shutdown = shutdown_scripting;
  hooks->add_script = add_script;
  hooks->kill_script = kill_script;

  return 1;
}
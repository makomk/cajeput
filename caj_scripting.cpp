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
#include "cajeput_user.h"
#include "caj_vm.h"
#include "caj_version.h"
#include "caj_script.h"
#include <fcntl.h>
#include <deque>

/* This code is reasonably robust, but a tad interesting internally.
   A few rules for dealing with the message-passing stuff:
   
   - Once the main thread has begun the process of killing a script, as 
     indicated by the SCR_MT_KILLING state and by scr->prim == NULL, it must
     *not* send any further messages to the script thread regarding said 
     script, nor should it handle any messages from the script thread
     regarding it.
   - After the scripting thread receives the KILL_SCRIPT message, it must 
     remove all references to it and cease touching any data structures related
     to it prior to responding with SCRIPT_KILLED. It's guaranteed not to get
     any further messages for this script after KILL_SCRIPT, and can't send any
     after the SCRIPT_KILLED.
   - Messages are delivered in order.
   - The RPC stuff is easy - it just passes ownership of scr->vm to the main 
     thread temporarily in order for it to do the call there.
   - You may think that you can respond to RPCed native calls asynchronously.
     You may be right, but on your own head be it!
*/

#define DEBUG_CHANNEL 2147483647

struct sim_scripts {
  // used by main thread
  GThread *thread;
  simulator_ctx *sim;

  // used by script thread
  GTimer *timer;

  // these are used by both main and scripting threads. Don't modify them.
  GAsyncQueue *to_st;
  GAsyncQueue *to_mt;
  vm_world *vmw;
};

struct list_head {
  struct list_head *next, *prev;
};

// these are entirely internal. You can change them if you really need to, but
// they must be consecutive and the events must be added via vm_world_add_event
// in numerical order.
#define EVENT_STATE_ENTRY 0
#define EVENT_TOUCH_START 1
#define EVENT_TOUCH 2
#define EVENT_TOUCH_END 3

// internal - various script states for the main thread code.
#define SCR_MT_COMPILING 1
#define SCR_MT_COMPILE_ERROR 2
#define SCR_MT_RUNNING 3
#define SCR_MT_PAUSED 4
#define SCR_MT_KILLING 5

#define SCRIPT_MAGIC 0xd0f87153

struct compiler_output {
  GIOChannel *stdout, *stderr;
  int len, buflen;
  char *buf;
  compile_done_cb cb; void *cb_priv;
};

struct detected_event {
  int event_id;
  uuid_t key, owner;
  char *name;
  caj_vector3 pos, vel;
  caj_quat rot;
  
  detected_event() : name(NULL) {
    uuid_clear(key); uuid_clear(owner);
    pos.x = 0.0f; pos.y = 0.0f; pos.z = 0.0f;
    vel.x = 0.0f; vel.y = 0.0f; vel.z = 0.0f;
    rot.x = 0.0f; rot.y = 0.0f; rot.z = 0.0f; rot.w = 1.0f;
  }

  ~detected_event() {
    free(name);
  }
};

struct sim_script {
  // section used by scripting thread
  list_head list;
  int is_running, in_rpc;
  script_state *vm;
  int state_entry; // HACK!
  double time; // for llGetTime etc
  detected_event *detected;
  std::deque<detected_event*> pending_detect;

  // section used by main thread
  int mt_state; // state as far as main thread is concerned
  primitive_obj *prim;
  compiler_output *comp_out;
  int evmask;

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
#define CAJ_SMSG_RPC 6
#define CAJ_SMSG_RPC_RETURN 7
#define CAJ_SMSG_EVMASK 8
#define CAJ_SMSG_DETECTED 9

typedef void(*script_rpc_func)(script_state *st, sim_script *sc, int func_id);

struct script_msg {
  int msg_type;
  sim_script *scr;
  union {
    struct {
      int32_t channel; char* msg; int chat_type;
    } say;
    struct {
      script_rpc_func rpc_func;
      script_state *st; int func_id;
    } rpc;
    int evmask;
    detected_event *detected;
  } u;
};

#define MAX_QUEUED_EVENTS 32

static void rpc_func_return(script_state *st, sim_script *scr, int func_id);

// -------------- script thread code -----------------------------------------

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

// note - this relies on the string-duplicating property of vm_func_get_args
static void do_say(sim_script *scr, int chan, char* message, int chat_type) {
  script_msg *smsg = new script_msg();
  smsg->msg_type = CAJ_SMSG_LLSAY;
  smsg->scr = scr;
  smsg->u.say.channel = chan;
  smsg->u.say.msg = message;
  smsg->u.say.chat_type = chat_type;
  send_to_mt(scr->simscr, smsg);
}

static void do_rpc(script_state *st, sim_script *scr, int func_id, 
		   script_rpc_func func) {
  scr->in_rpc = 1;
  script_msg *smsg = new script_msg();
  smsg->msg_type = CAJ_SMSG_RPC;
  smsg->scr = scr;
  smsg->u.rpc.rpc_func = func; 
  smsg->u.rpc.st = st; smsg->u.rpc.func_id = func_id;
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

static void llResetTime_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  scr->time = g_timer_elapsed(scr->simscr->timer, NULL);
  vm_func_return(st, func_id);
}

static void llGetTime_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  float time = g_timer_elapsed(scr->simscr->timer, NULL) - scr->time;
  vm_func_set_float_ret(st, func_id, time);
  vm_func_return(st, func_id);
}

#define CONVERT_COLOR(col) (col > 1.0f ? 255 : (col < 0.0f ? 0 : (uint8_t)(col*255)))

// actually called from main thread
static void llSetText_rpc(script_state *st, sim_script *scr, int func_id) {
  char *text; caj_vector3 color; float alpha;
  uint8_t textcol[4];
  vm_func_get_args(st, func_id, &text, &color, &alpha);
  textcol[0] = CONVERT_COLOR(color.x);
  textcol[1] = CONVERT_COLOR(color.y);
  textcol[2] = CONVERT_COLOR(color.z);
  textcol[3] = 255-CONVERT_COLOR(alpha);
  world_prim_set_text(scr->simscr->sim, scr->prim, text, textcol);
  free(text);
  rpc_func_return(st, scr, func_id);
}

static void llSetText_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  do_rpc(st, scr, func_id, llSetText_rpc);
}

static void llApplyImpulse_rpc(script_state *st, sim_script *scr, int func_id) {
  caj_vector3 impulse; int is_local;
  vm_func_get_args(st, func_id, &impulse, &is_local);
  world_prim_apply_impulse(scr->simscr->sim, scr->prim, impulse, is_local);
  rpc_func_return(st, scr, func_id);
}


static void llApplyImpulse_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  do_rpc(st, scr, func_id, llApplyImpulse_rpc);
}

static void llGetPos_rpc(script_state *st, sim_script *scr, int func_id) {
  vm_func_set_vect_ret(st, func_id, &scr->prim->ob.pos);
  rpc_func_return(st, scr, func_id);
}

static void llGetPos_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  do_rpc(st, scr, func_id, llGetPos_rpc);
}
static void llGetRot_rpc(script_state *st, sim_script *scr, int func_id) {
  vm_func_set_rot_ret(st, func_id, &scr->prim->ob.rot);
  rpc_func_return(st, scr, func_id);
}

static void llGetRot_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  do_rpc(st, scr, func_id, llGetRot_rpc);
}


// We're not as paranoid as OpenSim yet, so this isn't restricted. May be
// modified to provide restricted version information to untrusted scripts at
// some point in the future.
static void osGetSimulatorVersion_cb(script_state *st, void *sc_priv, int func_id) {
  vm_func_set_str_ret(st, func_id, CAJ_VERSION_FOR_OS_SCRIPT);
  vm_func_return(st, func_id);
}

static void llDetectedName_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int num;
  vm_func_get_args(st, func_id, &num);
  if(scr->detected != NULL && num == 0 && scr->detected->name != NULL) {
    vm_func_set_str_ret(st, func_id, scr->detected->name);
  } else {
    vm_func_set_str_ret(st, func_id, "");
  }
  vm_func_return(st, func_id);
}

// FIXME - there should be a global definition of these somewhere
static const caj_vector3 zero_vect = { 0.0f, 0.0f, 0.0f };
static const caj_quat zero_rot = { 0.0f, 0.0f, 0.0f, 1.0f };
static const uuid_t zero_uuid= { };

static void llDetectedKey_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int num;
  vm_func_get_args(st, func_id, &num);
  if(scr->detected != NULL && num == 0) {
    vm_func_set_key_ret(st, func_id, scr->detected->key);
  } else {
    vm_func_set_key_ret(st, func_id, zero_uuid);
  }
  vm_func_return(st, func_id);
}


static void llDetectedPos_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int num;
  vm_func_get_args(st, func_id, &num);
  if(scr->detected != NULL && num == 0) {
    vm_func_set_vect_ret(st, func_id, &scr->detected->pos);
  } else {
    vm_func_set_vect_ret(st, func_id, &zero_vect);
  }
  vm_func_return(st, func_id);
}

static void llDetectedRot_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int num;
  vm_func_get_args(st, func_id, &num);
  if(scr->detected != NULL && num == 0) {
    vm_func_set_rot_ret(st, func_id, &scr->detected->rot);
  } else {
    vm_func_set_rot_ret(st, func_id, &zero_rot);
  }
  vm_func_return(st, func_id);
}

static void llDetectedVel_cb(script_state *st, void *sc_priv, int func_id) {
  sim_script *scr = (sim_script*)sc_priv;
  int num;
  vm_func_get_args(st, func_id, &num);
  if(scr->detected != NULL && num == 0) {
    vm_func_set_vect_ret(st, func_id, &scr->detected->vel);
  } else {
    vm_func_set_vect_ret(st, func_id, &zero_vect);
  }
  vm_func_return(st, func_id);
}

static void script_upd_evmask(sim_script *scr) {
  int evmask = 0;
  if(vm_event_has_handler(scr->vm, EVENT_TOUCH_START) ||
     vm_event_has_handler(scr->vm, EVENT_TOUCH_END)) {
    evmask |= CAJ_EVMASK_TOUCH;
  }
  if(vm_event_has_handler(scr->vm, EVENT_TOUCH)) {
    evmask |= CAJ_EVMASK_TOUCH_CONT;
  }
script_msg *smsg = new script_msg();
  smsg->msg_type = CAJ_SMSG_EVMASK;
  smsg->scr = scr;
  smsg->u.evmask = evmask;
  send_to_mt(scr->simscr, smsg);
  
}

static void awaken_script(sim_script *scr, list_head *running) {
  if(!scr->is_running) {
    // yes, we really do schedule this to run next.
    scr->is_running = 1; list_remove(&scr->list);
    list_insert_after(&scr->list, running);
  }
}

static gpointer script_thread(gpointer data) {
  sim_scripts *simscr = (sim_scripts*)data;
  list_head running, waiting;
  list_head_init(&running); list_head_init(&waiting);
  simscr->timer = g_timer_new();
  for(;;) {
    for(int i = 0; i < 20; i++) {
      if(running.next == &running) break;
      sim_script *scr = (sim_script*)running.next; 
      printf("DEBUG: handling script on run queue\n");
      assert(scr->is_running);
      if(scr->in_rpc) {
	// deschedule. Special-cased because we don't have ownership of scr->vm
	// during RPC calls to the main thread.
	scr->is_running = 0; list_remove(&scr->list);
	list_insert_after(&scr->list, &waiting);
      }
      if(vm_script_is_idle(scr->vm)) {
	printf("DEBUG: script idle\n");

	if(scr->detected != NULL) {
	  // FIXME - this is leaked if the script is destroyed at the wrong time
	  delete scr->detected; scr->detected = NULL;
	}

	if(scr->state_entry) {
	  printf("DEBUG: calling state_entry\n");
	  scr->state_entry = 0;
	  vm_call_event(scr->vm,EVENT_STATE_ENTRY);
	} else if(!scr->pending_detect.empty()) {
	  // FIXME - coalesce detected events into one call somehow
	  
	  printf("DEBUG: handing pending detected event\n");
	  scr->detected = scr->pending_detect.front();
	  scr->pending_detect.pop_front();
	  switch(scr->detected->event_id) {
	  case EVENT_TOUCH_START:
	  case EVENT_TOUCH_END:
	  case EVENT_TOUCH:
	    vm_call_event(scr->vm, scr->detected->event_id, 1);
	    break;
	  default:
	    printf("INTERNAL ERROR: unhandled detected event type - impossible!\n");
	    break;
	  }
	}
	// FIXME - schedule events
      }
      if(vm_script_is_runnable(scr->vm)) {
	printf("DEBUG: script runnable\n");
	vm_run_script(scr->vm, 100);
      } else {
	printf("DEBUG: removing script from run queue\n");
	if(vm_script_has_failed(scr->vm)) {
	  do_say(scr, DEBUG_CHANNEL, vm_script_get_error(scr->vm),
		 CHAT_TYPE_NORMAL);
	}
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
	g_timer_destroy(simscr->timer);
	delete msg;
	return NULL;
      case CAJ_SMSG_ADD_SCRIPT:
	printf("DEBUG: handling ADD_SCRIPT\n");
	msg->scr->time = g_timer_elapsed(simscr->timer, NULL);
	st_load_script(msg->scr);
	if(msg->scr->vm != NULL) {
	  printf("DEBUG: adding to run queue\n");
	  // FIXME - should insert at end of running queue
	  msg->scr->is_running = 1;
	  list_insert_after(&msg->scr->list, &running);
	  script_upd_evmask(msg->scr);
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
	if(msg->scr->vm != NULL) vm_free_script(msg->scr->vm);
	msg->scr->vm = NULL;
	msg->msg_type = CAJ_SMSG_SCRIPT_KILLED;
	send_to_mt(simscr, msg); msg = NULL;
	break;
      case CAJ_SMSG_RPC_RETURN:
	printf("DEBUG: got RPC_RETURN\n");
	assert(msg->scr->in_rpc); msg->scr->in_rpc = 0;
	awaken_script(msg->scr, &running);
	break;
      case CAJ_SMSG_DETECTED:
	if(msg->scr->pending_detect.size() >= MAX_QUEUED_EVENTS) {
	  printf("DEBUG: discarding script event due to queue size\n");
	  delete msg->u.detected;
	} else {
	  msg->scr->pending_detect.push_back(msg->u.detected);
	  awaken_script(msg->scr, &running);
	}
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

static void rpc_func_return(script_state *st, sim_script *scr, int func_id) {
  vm_func_return(st, func_id);
  script_msg *smsg = new script_msg();
  smsg->msg_type = CAJ_SMSG_RPC_RETURN;
  smsg->scr = scr;
  send_to_script(scr->simscr, smsg);
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

static void save_script_text_file(caj_string *dat, char *name) {
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

  flush_compile_output(scr->comp_out->stdout, scr->comp_out);
  flush_compile_output(scr->comp_out->stderr, scr->comp_out);
  scr->comp_out->buf[scr->comp_out->len] = 0;
  printf("DEBUG: got compiler output: ~%s~\n", scr->comp_out->buf);

  if(scr->comp_out->cb != NULL) {
    scr->comp_out->cb(scr->comp_out->cb_priv, success, scr->comp_out->buf,
		      scr->comp_out->len);
  }

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
  return TRUE; // shouldn't actually ever reach here...
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
  scr->prim = prim; scr->mt_state = SCR_MT_COMPILING; scr->evmask = 0;
  scr->is_running = 0; scr->simscr = simscr; scr->magic = SCRIPT_MAGIC;
  scr->detected = NULL; scr->in_rpc = 0;
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
    scr->mt_state = SCR_MT_KILLING;
    script_msg *msg = new script_msg();
    msg->msg_type = CAJ_SMSG_KILL_SCRIPT;
    msg->scr = scr;
    send_to_script(simscr, msg);  
  } else if(scr->mt_state != SCR_MT_COMPILING) {
    mt_free_script(scr);
  }
}

static int get_evmask(simulator_ctx *sim, void *priv, void *script) {
  sim_script *scr = (sim_script*)script;
  assert(scr->magic == SCRIPT_MAGIC);
  return scr->evmask;
}

static void send_detected_event(sim_scripts *simscr, sim_script *scr, 
				detected_event *det) {
  script_msg *msg = new script_msg();
  msg->msg_type = CAJ_SMSG_DETECTED;
  msg->scr = scr;
  msg->u.detected = det;
  send_to_script(simscr, msg);
}

static void do_touch(simulator_ctx *sim, void *priv, void *script,
		     user_ctx *user, world_obj *av, int is_start) {
  sim_scripts *simscr = (sim_scripts*)priv;
  sim_script *scr = (sim_script*)script;
  if(is_start ? (scr->evmask & CAJ_EVMASK_TOUCH) :
     (scr->evmask & CAJ_EVMASK_TOUCH_CONT)) {
    detected_event *det = new detected_event();
    det->event_id = is_start ? EVENT_TOUCH_START : EVENT_TOUCH;

    det->name = strdup(user_get_name(user));
    det->pos = av->pos; det->rot = av->rot; det->vel = av->velocity;
    user_get_uuid(user, det->key);
    send_detected_event(simscr, scr, det);
  }
}
static void do_untouch(simulator_ctx *sim, void *priv, void *script,
		     user_ctx *user, world_obj *av) {
  sim_scripts *simscr = (sim_scripts*)priv;
  sim_script *scr = (sim_script*)script;
  if(scr->evmask & CAJ_EVMASK_TOUCH) {
    detected_event *det = new detected_event();
    det->event_id = EVENT_TOUCH_END;

    det->name = strdup(user_get_name(user));
    det->pos = av->pos; det->rot = av->rot; det->vel = av->velocity;
    user_get_uuid(user, det->key);
    send_detected_event(simscr, scr, det);
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
    case CAJ_SMSG_RPC:
      if(msg->scr->prim != NULL) {
	printf("DEBUG: got RPC call in main thread\n");
	msg->u.rpc.rpc_func(msg->u.rpc.st, msg->scr, msg->u.rpc.func_id);
      }
      break;
    case CAJ_SMSG_EVMASK:
      if(msg->scr->prim != NULL) {
	printf("DEBUG: got new script evmask in main thread\n");
	msg->scr->evmask = msg->u.evmask;
	world_set_script_evmask(msg->scr->simscr->sim, msg->scr->prim, 
				msg->scr, msg->u.evmask);
      }
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
  vm_world_add_event(simscr->vmw, "state_entry", VM_TYPE_NONE, EVENT_STATE_ENTRY, 0);
  vm_world_add_event(simscr->vmw, "touch_start", VM_TYPE_NONE, EVENT_TOUCH_START,
		     1, VM_TYPE_INT);
  vm_world_add_event(simscr->vmw, "touch", VM_TYPE_NONE, EVENT_TOUCH,
		     1, VM_TYPE_INT);
  vm_world_add_event(simscr->vmw, "touch_end", VM_TYPE_NONE, EVENT_TOUCH_END,
		     1, VM_TYPE_INT);

  vm_world_add_func(simscr->vmw, "llSay", VM_TYPE_NONE, llSay_cb, 2, VM_TYPE_INT, VM_TYPE_STR); 
  vm_world_add_func(simscr->vmw, "llShout", VM_TYPE_NONE, llShout_cb, 2, VM_TYPE_INT, VM_TYPE_STR); 
  vm_world_add_func(simscr->vmw, "llWhisper", VM_TYPE_NONE, llWhisper_cb, 2, VM_TYPE_INT, VM_TYPE_STR); 
  vm_world_add_func(simscr->vmw, "llResetTime", VM_TYPE_NONE, llResetTime_cb, 0); 
  vm_world_add_func(simscr->vmw, "llGetTime", VM_TYPE_FLOAT, llGetTime_cb, 0); 
  vm_world_add_func(simscr->vmw, "llSetText", VM_TYPE_NONE, llSetText_cb, 3, 
		    VM_TYPE_STR, VM_TYPE_VECT, VM_TYPE_FLOAT); 
  
  vm_world_add_func(simscr->vmw, "llGetPos", VM_TYPE_VECT, llGetPos_cb, 0);
  vm_world_add_func(simscr->vmw, "llGetRot", VM_TYPE_VECT, llGetRot_cb, 0);

  vm_world_add_func(simscr->vmw, "llApplyImpulse", VM_TYPE_NONE, llApplyImpulse_cb, 
		    2, VM_TYPE_VECT, VM_TYPE_INT); 

  vm_world_add_func(simscr->vmw, "llDetectedName", VM_TYPE_STR, llDetectedName_cb,
		    1, VM_TYPE_INT);
  vm_world_add_func(simscr->vmw, "llDetectedPos", VM_TYPE_VECT, llDetectedPos_cb,
		    1, VM_TYPE_INT);
  vm_world_add_func(simscr->vmw, "llDetectedRot", VM_TYPE_ROT, llDetectedRot_cb,
		    1, VM_TYPE_INT);
  vm_world_add_func(simscr->vmw, "llDetectedVel", VM_TYPE_VECT, llDetectedVel_cb,
		    1, VM_TYPE_INT);
  vm_world_add_func(simscr->vmw, "llDetectedKey", VM_TYPE_KEY, llDetectedKey_cb,
		    1, VM_TYPE_INT);

  vm_world_add_func(simscr->vmw, "osGetSimulatorVersion", VM_TYPE_STR, 
		    osGetSimulatorVersion_cb, 0);
  

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
  hooks->get_evmask = get_evmask;
  hooks->do_touch = do_touch;
  hooks->do_untouch = do_untouch;

  return 1;
}

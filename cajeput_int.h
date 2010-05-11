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

/* This is an internal header that should not be used by external modules.
   (Yes, I know caj_omv_udp.cpp uses it, and that needs fixing.) */

#ifndef CAJEPUT_INT_H
#define CAJEPUT_INT_H
#include <string>
#include <map>
#include <vector>
#include <set>
#include <deque>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include "caj_llsd.h"
#include "cajeput_core.h"
#include "cajeput_world.h"
#include "cajeput_user.h"
#include "cajeput_grid_glue.h"
#include "cajeput_user_glue.h"

#define USER_CONNECTION_TIMEOUT 15
#define USER_CONNECTION_TIMEOUT_PAUSED 90

struct cap_descrip;

typedef std::map<std::string,cap_descrip*> named_caps_map;
typedef std::map<std::string,cap_descrip*>::iterator named_caps_iter;

// ---------------- CALLBACKS CODE ---------------

// FIXME - I think this is generating a bunch of practically identical code
// for each possible function pointer type, which is not good!
template <class T> 
struct caj_cb_entry {
  T cb;
  void *priv;
};

template <class T> 
bool operator < (const caj_cb_entry<T> &e1, const caj_cb_entry<T> &e2) {
  return e1.cb < e2.cb || (e1.cb == e2.cb && e1.priv < e2.priv);
}


template <class T>
struct caj_callback {

  // ideally, we'd typedef the iterator too, but C++ ain't that smart
  typedef std::set<caj_cb_entry<T> > cb_set;

  std::set<caj_cb_entry<T> > callbacks;

  void add_callback(T cb, void *priv) {
    caj_cb_entry<T> entry;
    entry.cb = cb; entry.priv = priv;
    callbacks.insert(entry);
  }

  void remove_callback(T cb, void *priv) {
    caj_cb_entry<T> entry;
    entry.cb = cb; entry.priv = priv;
    callbacks.erase(entry);
  }  
};

// -------------------------------------------------------------

struct sl_throttle {
  double time; // time last refilled
  float level, rate; // current reservoir level and flow rate
};

// note - allocated with calloc, can't use C++ stuff
struct avatar_obj {
  struct world_obj ob;
  caj_vector4 footfall;
  primitive_obj *attachments[NUM_ATTACH_POINTS];
};


struct user_ctx {
  struct user_ctx* next;
  char *first_name, *last_name, *name, *group_title;
  uint32_t circuit_code;

  struct user_hooks *userh;
  void *user_priv;

  // this is core enough we can't do without it!
  struct cap_descrip* seed_cap;
  named_caps_map named_caps;

  std::set<user_ctx**> self_ptrs;

  // float main_throttle; // FIXME - is this needed?
  struct sl_throttle throttles[SL_NUM_THROTTLES];

  uint16_t dirty_terrain[16];

  // Event queue stuff. FIXME - seperate this out
  struct {
    caj_llsd *queued;
    caj_llsd *last;
    int ctr;
    SoupMessage *msg;
    double timeout;
  } evqueue;  

  uuid_t session_id;
  uuid_t secure_session_id;
  uuid_t user_id;

  int flags; // AGENT_FLAG_*
  double last_activity;
  float draw_dist;
  struct simulator_ctx* sim;
  struct simgroup_ctx* sgrp;
  struct avatar_obj* av;

  caj_callback<user_generic_cb> delete_hook; // notifies when this user removed

  uint32_t wearable_serial;
  struct caj_string texture_entry, visual_params;
  struct animation_desc default_anim;
  std::vector<animation_desc> anims;
  int32_t anim_seq; // FIXME - seems fishy

  struct teleport_desc *tp_out;

  // FIXME - move this out of struct to save l KB of space per child agent
  struct wearable_desc wearables[SL_NUM_WEARABLES];

  inventory_contents* sys_folders;
  int sys_folders_state; // SYS_FOLDERS_*
  caj_callback<user_generic_cb> sys_folders_cbs;

  void *grid_priv; 

  std::map<uint32_t, int> obj_upd; // FIXME - HACK
  std::deque<uint32_t> deleted_objs;

  int shutdown_ctr; // for slow user removal (AGENT_FLAG_IN_SLOW_REMOVAL)

  // Blech. Remove this/move to seperate struct?
  caj_vector3 start_pos, start_look_at;

  struct obj_chat_listener listen;

  user_ctx(simulator_ctx* our_sim) : sim(our_sim), av(NULL) {
  }
};

struct world_obj;


// goes in world_obj.chat
struct obj_chat_listeners {
  struct world_obj *obj;
  // msg and all strings pointed to by it owned by caller
  std::set<std::pair<int32_t, obj_chat_listener*> > channels;
};

struct asset_cb_desc {
   void(*cb)(struct simgroup_ctx *sgrp, void *priv,
	     struct simple_asset *asset);
  void *cb_priv;
  
  asset_cb_desc( void(*cb_)(struct simgroup_ctx *sgrp, void *priv,
			    struct simple_asset *asset), void *cb_priv_) :
    cb(cb_), cb_priv(cb_priv_) { };
};

#define CAJ_ASSET_PENDING 0
#define CAJ_ASSET_READY 1
#define CAJ_ASSET_MISSING 2

struct asset_desc {
  simple_asset asset;
  int status; // CAJ_ASSET_*
  std::set<asset_cb_desc*> cbs;
};

struct simgroup_ctx {
  std::map<obj_uuid_t,inventory_contents*> inv_lib;
  std::map<obj_uuid_t,texture_desc*> textures;
  std::map<obj_uuid_t,asset_desc*> assets;
  GTimer *timer;

  char *release_notes;
  int release_notes_len;

  int state_flags;

  GKeyFile *config;

  SoupServer *soup;
  SoupSession *soup_session;

  void *grid_priv;
  struct cajeput_grid_hooks gridh;
  
  int hold_off_shutdown;

  uint16_t http_port;

  char *ip_addr;
  std::map<std::string,cap_descrip*> caps;
  std::map<uint64_t, simulator_ctx*> sims;

  caj_callback<sim_generic_cb> sim_added_hook;
};

struct collision_pair {
  uint32_t collidee, collider;
  
collision_pair(uint32_t collidee, uint32_t collider) : 
  collidee(collidee), collider(collider) { }
};

static inline bool operator<(const collision_pair &lhs, 
			     const collision_pair &rhs) {
  return lhs.collidee < rhs.collidee || (lhs.collidee == rhs.collidee &&
					 lhs.collider < rhs.collider);
}

#define CAJEPUT_SIM_READY 1 // TODO
#define CAJEPUT_SGRP_SHUTTING_DOWN 2

typedef std::set<collision_pair> collision_state;

struct simulator_ctx {
  simgroup_ctx *sgrp;
  char *cfg_sect, *shortname;
  struct user_ctx* ctxts;
  char *name;
  uint32_t region_x, region_y;
  uint64_t region_handle;
  float *terrain;
  int state_flags;
  uint16_t udp_port;
  uuid_t region_id, owner;
  std::map<obj_uuid_t,world_obj*> uuid_map;
  std::map<uint32_t,world_obj*> localid_map;
  struct world_octree* world_tree;
  gchar *welcome_message;

  void *phys_priv;
  struct cajeput_physics_hooks physh;

  void *script_priv;
  struct cajeput_script_hooks scripth;

  collision_state *collisions;

  //struct obj_bucket[8][8][32];

  // bunch of callbacks
  caj_callback<sim_generic_cb> shutdown_hook;
};

// ------ CALLBACKS ----------------

void sim_call_shutdown_hook(struct simulator_ctx *sim);

void sim_int_init_udp(struct simulator_ctx *sim);
void world_obj_add_listen(struct simulator_ctx *sim, struct world_obj *ob,
			  int32_t channel, struct obj_chat_listener* listen);

// FIXME - rename and export more widely!
void llsd_soup_set_response(SoupMessage *msg, caj_llsd *llsd);

void user_int_free_texture_sends(struct user_ctx *ctx);


void user_int_event_queue_init(user_ctx *ctx);
void user_int_event_queue_check_timeout(user_ctx *ctx, double time_now);
void user_int_event_queue_free(user_ctx *ctx);

void user_int_caps_init(simulator_ctx *sim, user_ctx *ctx, 
			struct sim_new_user *uinfo);
void user_int_caps_cleanup(user_ctx *ctx);

void user_remove_int(user_ctx **user); // for internal use only.


// FIXME - this really shouldn't be exposed
void user_av_chat_callback(struct simulator_ctx *sim, struct world_obj *obj,
			   const struct chat_message *msg, void *user_data);

// for strictly internal use ONLY! Really! Use equivalents in cajeput_core.h
void user_send_teleport_failed(struct user_ctx* ctx, const char* reason);
void user_send_teleport_progress(struct user_ctx* ctx, const char* msg, uint32_t flags);
void user_send_teleport_complete(struct user_ctx* ctx, struct teleport_desc *tp);



void user_call_delete_hook(struct user_ctx *ctx);

void world_int_dump_prims(simulator_ctx *sim);
void world_int_load_prims(simulator_ctx *sim);

struct world_octree* world_octree_create();
void world_octree_destroy(struct world_octree* tree);
inventory_item* prim_update_script(struct simulator_ctx *sim, struct primitive_obj *prim,
				   uuid_t item_id, int script_running,
				   unsigned char *data, int data_len,
				   compile_done_cb cb, void *cb_priv);
void world_int_init_obj_updates(user_ctx *ctx); // ick - HACK.

void world_save_script_state(simulator_ctx *sim, inventory_item *inv,
			     caj_string *out);

// note - takes ownership of the passed-in buffer
void world_load_script_state(inventory_item *inv, caj_string *state);


// --------- HACKY OBJECT UPDATE STUFF ---------------


// --------- CAPS STUFF -----------------------------

// perhaps caps-related functions should be externally available

struct cap_descrip; // opaque

typedef void (*caps_callback) (SoupMessage *msg, user_ctx *ctx, void *user_data);
struct cap_descrip* user_add_named_cap(struct simulator_ctx *ctx, 
				       const char* name, caps_callback callback,
				       user_ctx* user, void *user_data);

void caj_int_caps_init(simgroup_ctx *sgrp);


#endif

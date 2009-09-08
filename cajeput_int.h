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

#ifndef CAJEPUT_INT_H
#define CAJEPUT_INT_H
#include <string>
#include <map>
#include <vector>
#include <set>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include "caj_llsd.h"
#include "cajeput_core.h"
#include "cajeput_world.h"
#include "cajeput_user.h"

#define USER_CONNECTION_TIMEOUT 15

#define CAJ_VERSION_STRING "Cajeput 0.001"

struct cap_descrip;

typedef std::map<std::string,cap_descrip*> named_caps_map;
typedef std::map<std::string,cap_descrip*>::iterator named_caps_iter;

struct obj_uuid_t {
  uuid_t u;
  obj_uuid_t() {
    uuid_clear(u);
  }
  obj_uuid_t(const obj_uuid_t &u2) {
    *this = u2;
  }
  obj_uuid_t(const uuid_t u2) {
    uuid_copy(u, u2);
  }
};

static inline bool operator < (const obj_uuid_t &u1, const obj_uuid_t &u2) {
  return uuid_compare(u1.u,u2.u) < 0;
};

static inline bool operator <= (const obj_uuid_t &u1, const obj_uuid_t &u2) {
  return uuid_compare(u1.u,u2.u) <= 0;
};

static inline bool operator == (const obj_uuid_t &u1, const obj_uuid_t &u2) {
  return uuid_compare(u1.u,u2.u) == 0;
};

static inline bool operator > (const obj_uuid_t &u1, const obj_uuid_t &u2) {
  return uuid_compare(u1.u,u2.u) > 0;
};

static inline bool operator >= (const obj_uuid_t &u1, const obj_uuid_t &u2) {
  return uuid_compare(u1.u,u2.u) >= 0;
};

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

struct asset_xfer;

struct image_request {
  texture_desc *texture;
  float priority;
  int discard;
  unsigned packet_no;
};

#define CAJ_ANIM_TYPE_NORMAL 0 // normal
#define CAJ_ANIM_TYPE_DEFAULT 1 // default anim
// #define CAJ_ANIM_TYPE_MOVEMENT 2 // walking

struct avatar_obj {
  struct world_obj ob;
};

struct animation_desc {
  uuid_t anim, obj;
  int32_t sequence;
  int caj_type; // internal animation type info - FIXME remove?
};

/* !!!     WARNING   WARNING   WARNING    !!!
   Changing the user_hooks structure breaks ABI compatibility. Also,
   doing anything except inserting a new hook at the end breaks API 
   compatibility, potentially INVISIBLY! 
   Be sure to bump the ABI version after any change.
   !!!     WARNING   WARNING   WARNING    !!!
*/
struct user_hooks {
  void(*teleport_failed)(struct user_ctx* ctx, const char* reason);
  void(*teleport_progress)(struct user_ctx* ctx, const char* msg, 
			   uint32_t flags);
  void(*teleport_complete)(struct user_ctx* ctx, struct teleport_desc *tp);
  void(*remove)(void* user_priv);
 
  void(*disable_sim)(void* user_priv); // HACK
  void(*chat_callback)(void *user_priv, const struct chat_message *msg);

  // these are temporary hacks.
  void(*send_av_full_update)(user_ctx* ctx, user_ctx* av_user);
  void(*send_av_terse_update)(user_ctx* ctx, avatar_obj* av);
  void(*send_av_appearance)(user_ctx* ctx, user_ctx* av_user);
  void(*send_av_animations)(user_ctx* ctx, user_ctx* av_user);
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
  struct avatar_obj* av;

  caj_callback<user_generic_cb> delete_hook; // notifies when this user removed

  uint32_t wearable_serial, appearance_serial; // FIXME - which stuff uses the same serial and which doesn't?
  struct caj_string texture_entry, visual_params;
  struct animation_desc default_anim; // FIXME - merge into list of animations
  std::vector<animation_desc> anims;
  int32_t anim_seq; // FIXME - seems fishy

  struct teleport_desc *tp_out;

  // FIXME - move this out of struct to save l KB of space per child agent
  struct wearable_desc wearables[SL_NUM_WEARABLES];

  void *grid_priv; 

  std::map<uint32_t, int> obj_upd; // FIXME - HACK
  std::vector<uint32_t> deleted_objs;

  int shutdown_ctr; // for slow user removal (AGENT_FLAG_IN_SLOW_REMOVAL)

  user_ctx(simulator_ctx* our_sim) : sim(our_sim), av(NULL) {
  }
};

struct world_obj;

struct chat_message {
  int32_t channel;
  caj_vector3 pos;
  uuid_t source;
  uuid_t owner;
  uint8_t source_type, chat_type;
  char *name;
  char *msg;
};

typedef void(*obj_chat_callback)(struct simulator_ctx *sim, struct world_obj *obj,
				 const struct chat_message *msg, void *user_data);

// goes in world_obj.chat
struct obj_chat_listener {
  struct world_obj *obj;
  // msg and all strings pointed to by it owned by caller
  obj_chat_callback callback;
  void *user_data;
  std::set<int32_t> channels;
};

struct asset_cb_desc {
   void(*cb)(struct simulator_ctx *sim, void *priv,
	     struct simple_asset *asset);
  void *cb_priv;
  
  asset_cb_desc( void(*cb_)(struct simulator_ctx *sim, void *priv,
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

#define CAJEPUT_SIM_READY 1 // TODO
#define CAJEPUT_SIM_SHUTTING_DOWN 2

struct simulator_ctx {
  struct user_ctx* ctxts;
  char *name;
  uint32_t region_x, region_y;
  uint64_t region_handle;
  float *terrain;
  int state_flags, hold_off_shutdown;
  uint16_t http_port, udp_port;
  char *ip_addr;
  uuid_t region_id, owner;
  std::map<obj_uuid_t,world_obj*> uuid_map;
  std::map<uint32_t,world_obj*> localid_map;
  struct world_octree* world_tree;
  SoupServer *soup;
  SoupSession *soup_session;
  GTimer *timer;
  GKeyFile *config;

  std::map<obj_uuid_t,inventory_contents*> inv_lib;

  std::map<obj_uuid_t,texture_desc*> textures;
  std::map<obj_uuid_t,asset_desc*> assets;

  char *release_notes;
  int release_notes_len;
  gchar *welcome_message;

  void *grid_priv;
  struct cajeput_grid_hooks gridh;

  void *phys_priv;
  struct cajeput_physics_hooks physh;

  void *script_priv;
  struct cajeput_script_hooks scripth;

  std::map<std::string,cap_descrip*> caps;
  //struct obj_bucket[8][8][32];

  // bunch of callbacks
  caj_callback<sim_generic_cb> shutdown_hook;
};

// ------ CALLBACKS ----------------

void sim_call_shutdown_hook(struct simulator_ctx *sim);


// should be small enough not to be affected by sane quantisation
#define SL_QUAT_EPS 0.0001

// FIXME - stick this somewhere sane
static int caj_quat_equal(struct caj_quat *q1, struct caj_quat *q2) {
  return fabs(q1->x - q2->x) < SL_QUAT_EPS && 
    fabs(q1->y - q2->y) < SL_QUAT_EPS &&
    fabs(q1->z - q2->z) < SL_QUAT_EPS &&
    fabs(q1->w - q2->w) < SL_QUAT_EPS;
}


void sim_int_init_udp(struct simulator_ctx *sim);
void world_obj_listen_chat(struct simulator_ctx *sim, struct world_obj *ob,
			   obj_chat_callback callback, void *user_data);
void world_obj_add_channel(struct simulator_ctx *sim, struct world_obj *ob,
			   int32_t channel);

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

// called on CompleteAgentMovement - returns success (true/false)
int user_complete_movement(user_ctx *ctx);

user_ctx* sim_bind_user(simulator_ctx *sim, uuid_t user_id, uuid_t session_id,
			uint32_t circ_code, struct user_hooks* hooks);

// FIXME - this really shouldn't be exposed
void user_av_chat_callback(struct simulator_ctx *sim, struct world_obj *obj,
			   const struct chat_message *msg, void *user_data);

void user_update_throttles(struct user_ctx *ctx);

// for strictly internal use ONLY! Really! Use equivalents in cajeput_core.h
void user_send_teleport_failed(struct user_ctx* ctx, const char* reason);
void user_send_teleport_progress(struct user_ctx* ctx, const char* msg, uint32_t flags);
void user_send_teleport_complete(struct user_ctx* ctx, struct teleport_desc *tp);

// takes ownership of the passed LLSD
void user_event_queue_send(user_ctx* ctx, const char* name, caj_llsd *body);

int user_can_modify_object(struct user_ctx* ctx, struct world_obj *obj);

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


// --- this is messy --------------------

#define CAJ_MULTI_UPD_POS 1
#define CAJ_MULTI_UPD_ROT 2
#define CAJ_MULTI_UPD_SCALE 4

struct caj_multi_upd {
  int flags;
  caj_vector3 pos;
  caj_quat rot;
  caj_vector3 scale;
};

void world_multi_update_obj(struct simulator_ctx *sim, struct world_obj *obj,
			    const struct caj_multi_upd *upd);


// --------- HACKY OBJECT UPDATE STUFF ---------------

#define UPDATE_LEVEL_FULL 255
#define UPDATE_LEVEL_POSROT 1 // pos/rot
#define UPDATE_LEVEL_NONE 0

void world_mark_object_updated(simulator_ctx* sim, world_obj *obj, int update_level);

// --------- CAPS STUFF -----------------------------

// perhaps caps-related functions should be externally available

struct cap_descrip; // opaque

typedef void (*caps_callback) (SoupMessage *msg, user_ctx *ctx, void *user_data);
struct cap_descrip* user_add_named_cap(struct simulator_ctx *ctx, 
				       const char* name, caps_callback callback,
				       user_ctx* user, void *user_data);

// ------------ SL constants --------------
#define CHAT_AUDIBLE_FULLY 1
#define CHAT_AUDIBLE_BARELY 0
#define CHAT_AUDIBLE_NO -1

#define CHAT_SOURCE_SYSTEM 0
#define CHAT_SOURCE_AVATAR 1
#define CHAT_SOURCE_OBJECT 2

#endif

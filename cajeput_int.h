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
#include "sl_llsd.h"

#define USER_CONNECTION_TIMEOUT 15


struct cap_descrip;

typedef std::map<std::string,cap_descrip*> named_caps_map;
typedef std::map<std::string,cap_descrip*>::iterator named_caps_iter;

struct sl_throttle {
  double time; // time last refilled
  float level, rate; // current reservoir level and flow rate
};

struct wearable_desc {
  uuid_t asset_id, item_id;
};

struct asset_xfer;

struct user_ctx {
  struct user_ctx* next;
  char *first_name, *last_name, *name, *group_title;
  uint32_t circuit_code;
  uint32_t counter;

  // float main_throttle; // FIXME - is this needed?
  struct sl_throttle throttles[SL_NUM_THROTTLES];

  uuid_t session_id;
  uuid_t secure_session_id;
  uuid_t user_id;
  struct sockaddr_in addr;
  int sock;
  int flags; // AGENT_FLAG_*
  double last_activity;
  float draw_dist;
  struct simulator_ctx* sim;
  struct avatar_obj* av;
  struct cap_descrip* seed_cap;
  named_caps_map named_caps;
  std::vector<uint32_t> pending_acks;
  std::set<uint32_t> seen_packets; // FIXME - clean this up

  uint32_t appearance_serial; // FIXME - which stuff uses the same serial and which doesn't?
  struct sl_string texture_entry, visual_params;

  // FIXME - move this out of struct to save l KB of space per child agent
  struct wearable_desc wearables[SL_NUM_WEARABLES];

  void *grid_priv;

  // icky Linden stuff
  std::map<uint64_t,asset_xfer*> xfers;

  // Event queue stuff. FIXME - seperate this out
  sl_llsd *queued_events;
  sl_llsd *last_eventqueue;
  int event_queue_ctr;
  SoupMessage *event_queue_msg;
  double event_queue_timeout;

  user_ctx(simulator_ctx* our_sim) : sim(our_sim), av(NULL) {
  }
};

struct world_obj;

typedef std::multimap<sl_message_tmpl*,sl_msg_handler> msg_handler_map;
typedef std::multimap<sl_message_tmpl*,sl_msg_handler>::iterator msg_handler_map_iter;

struct chat_message {
  int32_t channel;
  sl_vector3 pos;
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

#define CAJEPUT_SIM_READY 1 // TODO
#define CAJEPUT_SIM_SHUTTING_DOWN 2

struct simulator_ctx {
  struct user_ctx* ctxts;
  char *name;
  uint32_t region_x, region_y;
  uint64_t region_handle;
  int sock, state_flags, hold_off_shutdown;
  uint16_t http_port, udp_port;
  char *ip_addr;
  uuid_t region_id, owner;
  msg_handler_map msg_handlers;
  std::map<obj_uuid_t,world_obj*> uuid_map;
  std::map<uint32_t,world_obj*> localid_map;
  struct world_octree* world_tree;
  SoupServer *soup;
  SoupSession *soup_session;
  GTimer *timer;
  GKeyFile *config;

  uint64_t xfer_id_ctr; // FIXME - should prob. init this, but meh.

  std::map<obj_uuid_t,texture_desc*> textures;

  char *release_notes;
  int release_notes_len;
  gchar *welcome_message;

  void *grid_priv;
  struct cajeput_grid_hooks gridh;

  void *phys_priv;
  struct cajeput_physics_hooks physh;

  std::map<std::string,cap_descrip*> caps;
  //struct obj_bucket[8][8][32];
};



struct avatar_obj {
  struct world_obj ob;
};

// should be small enough not to be affected by sane quantisation
#define SL_QUAT_EPS 0.0001

// FIXME - stick this somewhere sane
static int sl_quat_equal(struct sl_quat *q1, struct sl_quat *q2) {
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

// ------------ SL constants --------------
#define CHAT_AUDIBLE_FULLY 1
#define CHAT_AUDIBLE_BARELY 0
#define CHAT_AUDIBLE_NO -1

#define CHAT_SOURCE_SYSTEM 0
#define CHAT_SOURCE_AVATAR 1
#define CHAT_SOURCE_OBJECT 2

#define CHAT_TYPE_WHISPER 0
#define CHAT_TYPE_NORMAL 1
#define CHAT_TYPE_SHOUT 2
/* 3 is say chat, which is obsolete */
#define CHAT_TYPE_START_TYPING 4
#define CHAT_TYPE_STOP_TYPING 5
#define CHAT_TYPE_DEBUG 6 // what???
/* no 7? */
#define CHAT_TYPE_OWNER_SAY 8
#define CHAT_TYPE_REGION_SAY 0xff // ???

#endif

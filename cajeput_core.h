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


#ifndef CAJEPUT_CORE_H
#define CAJEPUT_CORE_H

#include <uuid/uuid.h>
#include <glib.h>
#include <libsoup/soup.h>
#include "sl_types.h"

#define CAJEPUT_API_VERSION 0x0004

#define WORLD_HEIGHT 4096

struct user_ctx;
struct simulator_ctx;
struct cap_descrip;
struct obj_chat_listeners;

// internal intended to be usable as a mask
#define OBJ_TYPE_PRIM 1
#define OBJ_TYPE_AVATAR 2

struct world_obj {
  int type;
  sl_vector3 pos;
  sl_vector3 scale; // FIXME - set correctly for avatars
  sl_vector3 velocity;
  sl_quat rot;
  uuid_t id;
  uint32_t local_id;
  void *phys;
  struct obj_chat_listener *chat;
};

struct primitive_obj {
  struct world_obj ob;
  uint8_t material, path_curve, profile_curve;
  uint16_t path_begin, path_end; // FIXME - why 16 bits?
  uint8_t path_scale_x, path_scale_y, path_shear_x, path_shear_y;
  int8_t path_twist, path_twist_begin, path_radius_offset;
  int8_t path_taper_x, path_taper_y;
  uint8_t path_revolutions; // slightly oddball one, this.
  int8_t path_skew;
  uint16_t profile_begin, profile_end, profile_hollow; // again, why 16 bits?
  uuid_t creator, owner;
  
};

// --- START sim query code ---

// NB strings returned should NOT be free()d or stored
uint32_t sim_get_region_x(struct simulator_ctx *sim);
uint32_t sim_get_region_y(struct simulator_ctx *sim);
uint64_t sim_get_region_handle(struct simulator_ctx *sim);
char* sim_get_ip_addr(struct simulator_ctx *sim);
char* sim_get_name(struct simulator_ctx *sim);
void sim_get_region_uuid(struct simulator_ctx *sim, uuid_t u);
void sim_get_owner_uuid(struct simulator_ctx *sim, uuid_t u);
uint16_t sim_get_http_port(struct simulator_ctx *sim);
uint16_t sim_get_udp_port(struct simulator_ctx *sim);
void* sim_get_grid_priv(struct simulator_ctx *sim);
void sim_set_grid_priv(struct simulator_ctx *sim, void* p);
float* sim_get_heightfield(struct simulator_ctx *sim);

// These, on the other hand, require you to g_free the returned string
char *sim_config_get_value(struct simulator_ctx *sim, const char* section,
			   const char* key);

// Glue code
void sim_queue_soup_message(struct simulator_ctx *sim, SoupMessage* msg,
			    SoupSessionCallback callback, void* user_data);
void sim_http_add_handler (struct simulator_ctx *sim,
			   const char            *path,
			   SoupServerCallback     callback,
			   gpointer               user_data,
			   GDestroyNotify         destroy);

// --- END sim query code ---


/* ------ CAPS ----- */


/* ------ LOGIN GLUE - FOR GRID  --------- */
struct map_block_info {
  uint32_t x, y; // larger size for future-proofing
  char *name;
  uint8_t access, water_height, num_agents;
  uint32_t flags;
  uuid_t map_image;
  // TODO
};

struct cajeput_grid_hooks {
  void(*do_grid_login)(struct simulator_ctx* sim);
  void(*user_created)(struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void **user_priv);
  void(*fetch_user_inventory)(simulator_ctx *sim, user_ctx *user,
			      void *user_priv);

  /* user entered the region */
  void(*user_entered)(simulator_ctx *sim, user_ctx *user,
		      void *user_priv);

  /* user is logging off */
  void(*user_logoff)(struct simulator_ctx* sim,
		     const uuid_t user_id, const sl_vector3 *pos,
		     const sl_vector3 *look_at);

  void(*cleanup)(struct simulator_ctx* sim);

  /* user context is being deleted */
  void(*user_deleted)(struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void *user_priv);

  void(*do_teleport)(struct simulator_ctx* sim, struct teleport_desc* tp);

  void(*get_texture)(struct simulator_ctx *sim, struct texture_desc *texture);
  void(*map_block_request)(struct simulator_ctx *sim, int min_x, int max_x, 
			   int min_y, int max_y, 
			   void(*cb)(void *priv, struct map_block_info *blocks, 
				     int count),
			   void *cb_priv);
};

// void do_grid_login(struct simulator_ctx* sim);

int cajeput_grid_glue_init(int api_version, struct simulator_ctx *sim, 
			   void **priv, struct cajeput_grid_hooks *hooks);

struct sim_new_user {
  char *first_name;
  char *last_name;
  char *seed_cap;
  uuid_t user_id, session_id, secure_session_id;
  int circuit_code;
  int is_child;
};

// Caller owns the struct and any strings pointed to by it
struct user_ctx* sim_prepare_new_user(struct simulator_ctx *sim,
				      struct sim_new_user *uinfo);

// ICK. For use by OpenSim glue code only.
void user_logoff_user_osglue(struct simulator_ctx *sim, uuid_t agent_id, 
			     uuid_t secure_session_id);


// ----- PHYSICS GLUE -------------

struct cajeput_physics_hooks {
  void(*add_object)(struct simulator_ctx *sim, void *priv,
		    struct world_obj *obj);
  void(*del_object)(struct simulator_ctx *sim, void *priv,
		    struct world_obj *obj);
  int(*update_pos)(struct simulator_ctx *sim, void *priv,
		   struct world_obj *obj); /* FIXME - HACK! */
  void(*set_force)(struct simulator_ctx *sim, void *priv,
		   struct world_obj *obj, sl_vector3 force); /* HACK HACK HACK */
  void(*set_target_velocity)(struct simulator_ctx *sim, void *priv,
			     struct world_obj *obj, sl_vector3 velocity);
  void(*set_avatar_flying)(struct simulator_ctx *sim, void *priv,
			   struct world_obj *obj, int is_flying);
  void(*step)(struct simulator_ctx *sim, void *priv);
  void(*destroy)(struct simulator_ctx *sim, void *priv);
};

int cajeput_physics_init(int api_version, struct simulator_ctx *sim, 
			 void **priv, struct cajeput_physics_hooks *hooks);

// ----------------- SIM HOOKS --------------------------

typedef void(*sim_generic_cb)(simulator_ctx* sim, void* priv);

void sim_add_shutdown_hook(struct simulator_ctx *sim,
			   sim_generic_cb cb, void *priv);
void sim_remove_shutdown_hook(struct simulator_ctx *sim,
			      sim_generic_cb cb, void *priv);

// ----- USER-RELATED STUFF ---------

#define SL_NUM_THROTTLES 7

#define SL_THROTTLE_NONE -1
#define SL_THROTTLE_RESEND 0
#define SL_THROTTLE_LAND 1
#define SL_THROTTLE_WIND 2
#define SL_THROTTLE_CLOUD 3
#define SL_THROTTLE_TASK 4 // object updates
#define SL_THROTTLE_TEXTURE 5
#define SL_THROTTLE_ASSET 6
static const char *sl_throttle_names[] = { "resend","land","wind","cloud","task","texture","asset" };

// Note - it's important this stays in sync with OpenSim
#define SL_WEARABLE_BODY 0
#define SL_WEARABLE_SKIN 1
#define SL_WEARABLE_HAIR 2
#define SL_WEARABLE_EYES 3
#define SL_WEARABLE_SHIRT 4
#define SL_WEARABLE_PANTS 5
#define SL_WEARABLE_SHOES 6
#define SL_WEARABLE_SOCKS 7
#define SL_WEARABLE_JACKET 8
#define SL_WEARABLE_GLOVES 9
#define SL_WEARABLE_UNDERSHIRT 10
#define SL_WEARABLE_UNDERPANTS 11
#define SL_WEARABLE_SKIRT 12

#define SL_NUM_WEARABLES 13 // WARNING: changing this would break ABI compat
static const char *sl_wearable_names[] = {"body","skin","hair","eyes","shirt",
					  "pants","shoes","socks","jacket",
					  "gloves","undershirt","underpants",
					  "skirt"};

// various internal flags
#define AGENT_FLAG_RHR 0x1 // got RegionHandshakeReply
#define AGENT_FLAG_INCOMING 0x2 // expecting agent to enter region - FIXME remove
#define AGENT_FLAG_PURGE 0x4 // agent is being purged
#define AGENT_FLAG_IN_LOGOUT 0x8 // agent is logging out
#define AGENT_FLAG_CHILD 0x10 // is a child agent
#define AGENT_FLAG_ENTERED 0x20 // got CompleteAgentMovement

// FIXME - these are hacks
#define AGENT_FLAG_APPEARANCE_UPD 0x40 // need to send AvatarAppearance to other agents
#define AGENT_FLAG_NEED_OTHER_AVS 0x80 // need to send AvatarAppearance etc for other avs - FIXME set this
#define AGENT_FLAG_ANIM_UPDATE 0x100 // need to send AvatarAnimation to other agents
#define AGENT_FLAG_AV_FULL_UPD 0x200 // need to send full ObjectUpdate for this avatar

#define AGENT_FLAG_TELEPORT_COMPLETE 0x400

void *user_get_grid_priv(struct user_ctx *user);
struct simulator_ctx* user_get_sim(struct user_ctx *user);
void user_get_uuid(struct user_ctx *user, uuid_t u);
void user_get_session_id(struct user_ctx *user, uuid_t u);
void user_get_secure_session_id(struct user_ctx *user, uuid_t u);
uint32_t user_get_circuit_code(struct user_ctx *user);
float user_get_draw_dist(struct user_ctx *user);

// as with sim_get_*, you mustn't free or store the strings
const char* user_get_first_name(struct user_ctx *user);
const char* user_get_last_name(struct user_ctx *user);
const sl_string* user_get_texture_entry(struct user_ctx *user);
const sl_string* user_get_visual_params(struct user_ctx *user);

uint32_t user_get_flags(struct user_ctx *user);
void user_set_flag(struct user_ctx *user, uint32_t flag);
void user_clear_flag(struct user_ctx *user, uint32_t flag);
void user_set_throttles(struct user_ctx *ctx, float rates[]);
void user_set_throttles_block(struct user_ctx* ctx, unsigned char* data,
			      int len);
void user_get_throttles_block(struct user_ctx* ctx, unsigned char* data,
			      int len);

struct wearable_desc {
  uuid_t asset_id, item_id;
};

// again, you don't free or modify this
const wearable_desc* user_get_wearables(struct user_ctx* ctx);

// Shouldn't really be used by most stuff
void user_set_wearable(struct user_ctx *ctx, int id,
		       uuid_t item_id, uuid_t asset_id);
void user_set_wearable_serial(struct user_ctx *ctx, uint32_t serial);

// Semantics of these two are funny. They take ownership of the buffer pointed 
// to by data->data and then set data->data to NULL. 
void user_set_texture_entry(struct user_ctx *user, struct sl_string* data);
void user_set_visual_params(struct user_ctx *user, struct sl_string* data);

struct animation_desc; // FIXME - move this to this header
void user_add_animation(struct user_ctx *ctx, struct animation_desc* anim,
			int replace);
void user_clear_animation_by_type(struct user_ctx *ctx, int caj_type);
void user_clear_animation_by_id(struct user_ctx *ctx, uuid_t anim);

user_ctx *user_find_ctx(struct simulator_ctx *sim, uuid_t agent_id);
user_ctx *user_find_session(struct simulator_ctx *sim, uuid_t agent_id,
			    uuid_t session_id);

void user_send_message(struct user_ctx *user, const char* msg);
void user_session_close(user_ctx* ctx);
void user_reset_timeout(struct user_ctx* ctx);

// used to ensure pointers to the user_ctx are NULLed correctly on removal
void user_add_self_pointer(struct user_ctx** pctx);
void user_del_self_pointer(struct user_ctx** pctx);

// teleport flags (for SL/OMV viewer, but also used internally)
#define TELEPORT_FLAG_SET_HOME 0x1 // not used much
#define TELEPORT_FLAG_SET_LAST 0x2 // ???
#define TELEPORT_TO_LURE 0x4
#define TELEPORT_TO_LANDMARK 0x8
#define TELEPORT_TO_LOCATION 0x10
#define TELEPORT_TO_HOME 0x20
// 0x40 VIA_TELEHUB
// 0x80 VIA_LOGIN
// Ox100 VIA_GODLIKE_LURE
// 0x200 GODLIKE 
// 0x400 911
#define TELEPORT_FLAG_NO_CANCEL 0x800 // disable cancel button - maybe useful
// 0x1000 VIA_REGION_ID
// 0x2000 FLYING, ignored by viewer

// Should only really be used to handle incoming requests from client
void user_teleport_location(struct user_ctx* ctx, uint64_t region_handle,
			    const sl_vector3 *pos, const sl_vector3 *look_at);
void user_teleport_landmark(struct user_ctx* ctx, uuid_t landmark);

// and these should only be used by code that handles teleports
struct teleport_desc {
  struct user_ctx* ctx; // may become NULL;
  uint64_t region_handle;
  sl_vector3 pos, look_at;
  uint32_t flags; // TELEPORT_FLAG_*
  int want_cancel;
  uint32_t sim_ip;
  uint16_t sim_port; // native byte order
  const char *seed_cap;
};
void user_teleport_failed(struct teleport_desc* tp, const char* reason);
void user_teleport_progress(struct teleport_desc* tp, const char* msg);
void user_complete_teleport(struct teleport_desc* tp);		    

// FIXME - HACK
void user_teleport_add_temp_child(struct user_ctx* ctx, uint64_t region,
				  uint32_t sim_ip, uint16_t sim_port,
				  const char* seed_cap);

// ----- MISC STUFF ---------

void sim_shutdown_hold(struct simulator_ctx *sim);
void sim_shutdown_release(struct simulator_ctx *sim);

// FIXME - should these be internal?
void world_insert_obj(struct simulator_ctx *sim, struct world_obj *ob);
void world_remove_obj(struct simulator_ctx *sim, struct world_obj *ob);
void world_send_chat(struct simulator_ctx *sim, struct chat_message* chat);

// FIXME - this should definitely be internal
void world_move_obj_int(struct simulator_ctx *sim, struct world_obj *ob,
			const sl_vector3 &new_pos);

#define CJP_TEXTURE_LOCAL 0x1
#define CJP_TEXTURE_PENDING 0x2
#define CJP_TEXTURE_MISSING 0x4
struct texture_desc {
  uuid_t asset_id;
  int flags;
  int len;
  unsigned char *data;
  int refcnt;
  int width, height, num_discard;
  int *discard_levels;
};

// Note: (a) you assign the UUID, (b) the sim owns and free()s the buffer
void sim_add_local_texture(struct simulator_ctx *sim, uuid_t asset_id, 
			   unsigned char *data, int len, int is_local);
void sim_texture_read_metadata(struct texture_desc *desc);
struct texture_desc *sim_get_texture(struct simulator_ctx *sim, uuid_t asset_id);
void sim_request_texture(struct simulator_ctx *sim, struct texture_desc *desc);

#endif

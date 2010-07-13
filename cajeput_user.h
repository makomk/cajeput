/* Copyright (c) 2009-2010 Aidan Thornton, all rights reserved.
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

#ifndef CAJEPUT_USER_H
#define CAJEPUT_USER_H

#include <uuid/uuid.h>
#include "caj_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct user_ctx;
struct simulator_ctx;
struct cap_descrip;
struct primitive_obj;

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
extern const char *sl_throttle_names[]; // don't forget to update this!

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
#define SL_WEARABLE_ALPHA 13
#define SL_WEARABLE_TATTOO 14

#define SL_NUM_WEARABLES 15 // WARNING: changing this would break ABI compat
extern const char *sl_wearable_names[]; // don't forget to update this!

// various internal flags
#define AGENT_FLAG_RHR 0x1 // got RegionHandshakeReply
#define AGENT_FLAG_INCOMING 0x2 // expecting agent to enter region
#define AGENT_FLAG_PURGE 0x4 // agent is being purged
#define AGENT_FLAG_IN_LOGOUT 0x8 // agent is logging out
#define AGENT_FLAG_CHILD 0x10 // is a child agent
#define AGENT_FLAG_ENTERED 0x20 // got CompleteAgentMovement

// FIXME - these are hacks
#define AGENT_FLAG_APPEARANCE_UPD 0x40 // need to send AvatarAppearance to other agents
#define AGENT_FLAG_NEED_OTHER_AVS 0x80 // need to send AvatarAppearance etc for other avs
#define AGENT_FLAG_ANIM_UPDATE 0x100 // need to send AvatarAnimation to other agents
#define AGENT_FLAG_AV_FULL_UPD 0x200 // need to send full ObjectUpdate for this avatar

#define AGENT_FLAG_TELEPORT_COMPLETE 0x400
#define AGENT_FLAG_IN_SLOW_REMOVAL 0x800 // for teleports
#define AGENT_FLAG_PAUSED 0x1000 // FIXME - actually pause stuff!!!
#define AGENT_FLAG_ALWAYS_RUN 0x2000

typedef void(*user_generic_cb)(user_ctx* user, void* priv);

void *user_get_grid_priv(struct user_ctx *user);
struct simulator_ctx* user_get_sim(struct user_ctx *user);
struct simgroup_ctx* user_get_sgrp(struct user_ctx *user);
void user_get_uuid(struct user_ctx *user, uuid_t u);
void user_get_session_id(struct user_ctx *user, uuid_t u);
void user_get_secure_session_id(struct user_ctx *user, uuid_t u);
int user_check_session(struct user_ctx *user, 
		       uuid_t agent, uuid_t session);
uint32_t user_get_circuit_code(struct user_ctx *user);
float user_get_draw_dist(struct user_ctx *user);

// as with sim_get_*, you mustn't free or store the strings
const char* user_get_first_name(struct user_ctx *user);
const char* user_get_last_name(struct user_ctx *user);
const char* user_get_name(struct user_ctx *user);
const char* user_get_group_title(struct user_ctx *user);
const char* user_get_group_name(struct user_ctx *user);
void user_get_active_group(struct user_ctx *user, uuid_t u);
const caj_string* user_get_texture_entry(struct user_ctx *user);
const caj_string* user_get_visual_params(struct user_ctx *user);
const struct animation_desc* user_get_default_anim(struct user_ctx *user);

void user_get_position(struct user_ctx* user, caj_vector3 *pos);
void user_get_initial_look_at(struct user_ctx* user, caj_vector3 *pos);
struct world_obj* user_get_avatar(struct user_ctx* user);

uint32_t user_get_flags(struct user_ctx *user);
void user_set_flag(struct user_ctx *user, uint32_t flag);
void user_clear_flag(struct user_ctx *user, uint32_t flag);
void user_set_throttles(struct user_ctx *ctx, float rates[]);
void user_set_throttles_block(struct user_ctx* ctx, unsigned char* data,
			      int len);
void user_get_throttles_block(struct user_ctx* ctx, unsigned char* data,
			      int len);

int user_owns_prim(struct user_ctx *ctx, struct primitive_obj *prim);

struct wearable_desc {
  uuid_t asset_id, item_id;
};

// again, you don't free or modify this
const wearable_desc* user_get_wearables(struct user_ctx* ctx);

void user_set_wearable_item_id(struct user_ctx *ctx, int id,
			       const uuid_t item_id);

// Shouldn't really be used by most stuff
void user_set_wearable(struct user_ctx *ctx, int id,
		       const uuid_t item_id, const uuid_t asset_id);
void user_set_wearable_serial(struct user_ctx *ctx, uint32_t serial);
uint32_t user_get_wearable_serial(struct user_ctx *ctx);

// Semantics of these two are funny. They take ownership of the buffer pointed 
// to by data->data and then set data->data to NULL. 
void user_set_texture_entry(struct user_ctx *user, struct caj_string* data);
void user_set_visual_params(struct user_ctx *user, struct caj_string* data);

struct animation_desc {
  uuid_t anim, obj;
  int32_t sequence;
  int caj_type; // internal animation type info - FIXME remove?
};

void user_add_animation(struct user_ctx *ctx, struct animation_desc* anim,
			int replace);
void user_clear_animation_by_type(struct user_ctx *ctx, int caj_type);
void user_clear_animation_by_id(struct user_ctx *ctx, uuid_t anim);

user_ctx *user_find_ctx(struct simulator_ctx *sim, const uuid_t agent_id);
user_ctx *user_find_session(struct simulator_ctx *sim, const uuid_t agent_id,
			    const uuid_t session_id);

void user_send_message(struct user_ctx *user, const char* msg);
void user_send_alert_message(struct user_ctx *ctx, const char* msg,
			     int is_modal);
void user_session_close(user_ctx* ctx, int slowly);
void user_reset_timeout(struct user_ctx* ctx);
void user_set_paused(user_ctx *ctx);
void user_set_unpaused(user_ctx *ctx);

int user_request_god_powers(user_ctx *ctx);
void user_relinquish_god_powers(user_ctx *ctx);

// used to ensure pointers to the user_ctx are NULLed correctly on removal
void user_add_self_pointer(struct user_ctx** pctx);
void user_del_self_pointer(struct user_ctx** pctx);

void caj_uuid_to_name(struct simgroup_ctx *sgrp, uuid_t id, 
		      void(*cb)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
		      void *cb_priv);

void user_fetch_inventory_folder(user_ctx *user, const uuid_t folder_id, 
				 const uuid_t owner_id,
				 void(*cb)(struct inventory_contents* inv, 
					   void* priv),
				 void *cb_priv);
void user_fetch_inventory_item(user_ctx *user, const uuid_t item_id, 
			       const uuid_t owner_id,
			       void(*cb)(struct inventory_item* item, 
					 void* priv),
			       void *cb_priv);

// This fills in the item ID and owner ID (but nothing else). It's guaranteed 
// to not access the inventory_item you pass it after it returns.
void user_add_inventory_item(user_ctx *ctx, struct inventory_item* item,
			     void(*cb)(void* priv, int success, uuid_t item_id), 
			     void *cb_priv);

typedef void (*system_folders_cb)(user_ctx *user, void *priv);

void user_fetch_system_folders(user_ctx *ctx, user_generic_cb cb,
			       void *cb_priv);

// WARNING: this must ONLY be called from the user_fetch_system_folders
// callback. Failure to obey this rule will result in crashes!
// Also, don't free the returned data structure, and don't store pointers
// to it.
struct inventory_folder* user_find_system_folder(user_ctx *ctx,
						 int8_t asset_type);

#define CAJ_TOUCH_START 0
#define CAJ_TOUCH_CONT 1
#define CAJ_TOUCH_END 2

struct caj_touch_info {
  caj_vector3 uv, st;
  int32_t face_index;
  caj_vector3 pos, normal, binormal;
};

void user_prim_touch(struct user_ctx *ctx,struct primitive_obj* prim, 
		     int touch_type, const struct caj_touch_info *info);

// check whether the asset can be retrieved without giving an inventory item
int user_can_access_asset_direct(user_ctx *user, simple_asset *asset);

  // check whether the asset can be retrieved from prim inventory
int user_can_access_asset_task_inv(user_ctx *user, primitive_obj *prim,
				   inventory_item *inv);

// calculate the permissions part of the ObjectUpdate UpdateFlags
uint32_t user_calc_prim_flags(struct user_ctx* ctx, struct primitive_obj *prim);

void user_rez_object(user_ctx *ctx, uuid_t from_prim, uuid_t item_id, 
		     uuid_t owner_id, caj_vector3 pos);
void user_rez_attachment(user_ctx *ctx, uuid_t item_id, uint8_t attach_point);
void user_remove_attachment(struct user_ctx *ctx, struct primitive_obj *prim);
  
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
			    const caj_vector3 *pos, const caj_vector3 *look_at,
			    int is_from_viewer);
void user_teleport_by_region_name(struct user_ctx* ctx, char *region_name,
				  const caj_vector3 *pos, 
				  const caj_vector3 *look_at,
				  int is_from_viewer);
void user_teleport_landmark(struct user_ctx* ctx, uuid_t landmark);

// and these should only be used by code that handles teleports
struct teleport_desc {
  struct user_ctx* ctx; // may become NULL;
  uint64_t region_handle;
  caj_vector3 pos, look_at;
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

struct caj_user_profile {
  uuid_t uuid;
  char *first, *last;
  char *email; char *web_url;

  char *about_text;
  uuid_t profile_image;
  uuid_t partner;

  char *first_life_text;
  uuid_t first_life_image;

  uint64_t home_region; // FIXME - OpenSim-ism, remove?
  uint32_t user_flags;
  uint32_t creation_time; // FIXME - too small?
};

typedef void(*caj_user_profile_cb)(caj_user_profile* profile, void *priv);
void caj_get_user_profile(struct simgroup_ctx *sgrp,  uuid_t id, 
			  caj_user_profile_cb cb, void *cb_priv);

// ----------------- USER HOOKS --------------------------

  // notification that this user context is going away
void user_add_delete_hook(struct user_ctx *ctx,
			   user_generic_cb cb, void *priv);
void user_remove_delete_hook(struct user_ctx *ctx,
			      user_generic_cb cb, void *priv);


// -------------------- INVENTORY STUFF ----------------------------------

void cajeput_get_library_skeleton(struct simgroup_ctx *sgrp, 
				  inventory_folder*** folders,
				  size_t *num_folders);
void cajeput_free_library_skeleton(inventory_folder** folders);

// Stuff for creating temporary descriptions of part of the inventory.
// The sim doesn't store a copy of the inventory.

struct inventory_folder {
  char *name;
  uuid_t folder_id, owner_id, parent_id;
  int8_t asset_type;
};

  // struct inventory_item is in cajeput_core.h (used by objects)

// describes contents of an inventory folder
struct inventory_contents {
  uuid_t folder_id;
  int32_t version, descendents;
  
  unsigned int num_subfolder, alloc_subfolder;
  struct inventory_folder* subfolders;

  unsigned int num_items, alloc_items;
  struct inventory_item* items;
};

struct inventory_contents* caj_inv_new_contents_desc(const uuid_t folder_id);
struct inventory_folder* caj_inv_add_folder(struct inventory_contents* inv,
					    uuid_t folder_id, uuid_t owner_id,
					    const char* name, 
					    int8_t asset_type);
struct inventory_item* caj_add_inventory_item(struct inventory_contents* inv, 
					      const char* name, const char* desc,
					      const char* creator);
struct inventory_item* caj_add_inventory_item_copy(struct inventory_contents* inv,
						   const struct inventory_item *src);
void caj_inv_free_contents_desc(struct inventory_contents* inv);
uint32_t caj_calc_inventory_crc(struct inventory_item* item);

struct inventory_folder* caj_inv_make_folder(uuid_t parent_id, uuid_t folder_id,
					     uuid_t owner_id, const char* name,
					     int8_t asset_type);
  void caj_inv_free_folder(struct inventory_folder* folder);

void caj_inv_copy_item(struct inventory_item *dest,
		       const struct inventory_item *src);

#ifdef __cplusplus
}
#endif

#endif

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

#ifndef CAJEPUT_USER_H
#define CAJEPUT_USER_H

#include <uuid/uuid.h>
#include "sl_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct user_ctx;
struct simulator_ctx;
struct cap_descrip;

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
#define AGENT_FLAG_IN_SLOW_REMOVAL 0x800 // for teleports

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
void user_session_close(user_ctx* ctx, int slowly);
void user_reset_timeout(struct user_ctx* ctx);

// used to ensure pointers to the user_ctx are NULLed correctly on removal
void user_add_self_pointer(struct user_ctx** pctx);
void user_del_self_pointer(struct user_ctx** pctx);

void caj_uuid_to_name(struct simulator_ctx *sim, uuid_t id, 
		      void(*cb)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
		      void *cb_priv);

void user_fetch_inventory_folder(simulator_ctx *sim, user_ctx *user, 
				 uuid_t folder_id,
				 void(*cb)(struct inventory_contents* inv, 
					   void* priv),
				 void *cb_priv);

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

// -------------------- INVENTORY STUFF ----------------------------------

// Stuff for creating temporary descriptions of part of the inventory.
// The sim doesn't store a copy of the inventory.

struct inventory_folder {
  char *name;
  uuid_t folder_id, owner_id, parent_id;
  int8_t inv_type;
};

struct inventory_item {
  char *name;
  uuid_t item_id, folder_id, owner_id;

  char *creator_id;
  uuid_t creator_as_uuid;
  char *description;

  uint32_t next_perms, current_perms, base_perms;
  uint32_t everyone_perms, group_perms;

  int8_t inv_type, asset_type;
  uint8_t sale_type;
  int8_t group_owned; // a boolean

  uuid_t asset_id, group_id;

  uint32_t flags;
  int32_t sale_price;
  int32_t creation_date;
  // ...

};

// describes contents of an inventory folder
struct inventory_contents {
  uuid_t folder_id;
  int32_t version, descendents;
  
  unsigned int num_subfolder, alloc_subfolder;
  struct inventory_folder* subfolders;

  unsigned int num_items, alloc_items;
  struct inventory_item* items;
};


struct inventory_contents* caj_inv_new_contents_desc(uuid_t folder_id);
struct inventory_folder* caj_inv_add_folder(struct inventory_contents* inv,
					    uuid_t folder_id, uuid_t owner_id,
					    const char* name, int8_t inv_type);
struct inventory_item* caj_add_inventory_item(struct inventory_contents* inv, 
					      const char* name, const char* desc,
					      const char* creator);
void caj_inv_free_contents_desc(struct inventory_contents* inv);
uint32_t caj_calc_inventory_crc(struct inventory_item* item);

#ifdef __cplusplus
}
#endif

#endif
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
#include "caj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAJEPUT_API_VERSION 0x000c

struct user_ctx;
struct simulator_ctx;
struct cap_descrip;

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
void* sim_get_script_priv(struct simulator_ctx *sim);
void sim_set_script_priv(struct simulator_ctx *sim, void* p);
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

  // not used by MapBlockRequest, but kinda handy for other stuff
  char *sim_ip;
  int sim_port, http_port;
  uuid_t region_id;  
};

  // FIXME - move this out of cajeput_core.h
struct cajeput_grid_hooks {
  void(*do_grid_login)(struct simulator_ctx* sim);
  void(*user_created)(struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void **user_priv);

  /* user entered the region */
  void(*user_entered)(simulator_ctx *sim, user_ctx *user,
		      void *user_priv);

  /* user is logging off */
  void(*user_logoff)(struct simulator_ctx* sim,
		     const uuid_t user_id, const caj_vector3 *pos,
		     const caj_vector3 *look_at);

  void(*cleanup)(struct simulator_ctx* sim);

  /* user context is being deleted */
  void(*user_deleted)(struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void *user_priv);

  void(*do_teleport)(struct simulator_ctx* sim, struct teleport_desc* tp);

  void(*get_texture)(struct simulator_ctx *sim, struct texture_desc *texture);
  void(*get_asset)(struct simulator_ctx *sim, struct simple_asset *asset);
  void(*map_block_request)(struct simulator_ctx *sim, int min_x, int max_x, 
			   int min_y, int max_y, 
			   void(*cb)(void *priv, struct map_block_info *blocks, 
				     int count),
			   void *cb_priv);
  void(*map_name_request)(struct simulator_ctx* sim, const char* name,
			  void(*cb)(void* cb_priv, struct map_block_info* blocks, 
				    int count),
			  void *cb_priv);

  // interesting interesting function...
  void(*fetch_inventory_folder)(simulator_ctx *sim, user_ctx *user,
				void *user_priv, uuid_t folder_id,
				void(*cb)(struct inventory_contents* inv, 
					  void* priv),
				void *cb_priv);
  void (*fetch_inventory_item)(simulator_ctx *sim, user_ctx *user,
			       void *user_priv, uuid_t item_id,
			       void(*cb)(struct inventory_item* item, 
					 void* priv),
			       void *cb_priv);

  void(*uuid_to_name)(struct simulator_ctx *sim, uuid_t id, 
		      void(*cb)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
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


// ----------------- SIM HOOKS --------------------------

typedef void(*sim_generic_cb)(simulator_ctx* sim, void* priv);

void sim_add_shutdown_hook(struct simulator_ctx *sim,
			   sim_generic_cb cb, void *priv);
void sim_remove_shutdown_hook(struct simulator_ctx *sim,
			      sim_generic_cb cb, void *priv);

// ----- MISC STUFF ---------


struct simple_asset { // for makeshift scripting stuff
  char *name, *description;
  int8_t type; // FIXME - is this right?
  uuid_t id;
  caj_string data;
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

  simple_asset *asset_hack; // evil HACK for prim scripts
  void *priv; // used for scripts
};

void sim_shutdown_hold(struct simulator_ctx *sim);
void sim_shutdown_release(struct simulator_ctx *sim);

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
void sim_texture_finished_load(texture_desc *desc);
void sim_asset_finished_load(struct simulator_ctx *sim, 
			     struct simple_asset *asset, int success);
struct texture_desc *sim_get_texture(struct simulator_ctx *sim, uuid_t asset_id);
void sim_request_texture(struct simulator_ctx *sim, struct texture_desc *desc);

  // FIXME - Move these to their own header...
#define ASSET_TEXTURE 0
#define ASSET_SOUND 1
#define ASSET_CALLING_CARD 2
#define ASSET_LANDMARK 3
  // 4 is some old scripting system
#define ASSET_CLOTHING 5
#define ASSET_OBJECT 6
#define ASSET_NOTECARD 7
#define ASSET_CATEGORY 8 // inventory folder
#define ASSET_ROOT 9 // inventory root folder
#define ASSET_LSL_TEXT 10
#define ASSET_LSL_BYTECODE 11
#define ASSET_TGA_TEXTURE 12
#define ASSET_BODY_PART 13
#define ASSET_TRASH 14 // category marker
#define ASSET_SNAPSHOT 15 // category marker
#define ASSET_LOST_FOUND 16 // category marker
#define ASSET_WAV_SOUND 17
#define ASSET_TGA_IMAGE 18
#define ASSET_JPEG_IMAGE 19
#define ASSET_ANIMATION 20
#define ASSET_GESTURE 21

  // another bunch of SL flags, this time permissions
#define PERM_TRANSFER (1 << 13)
#define PERM_MODIFY (1 << 14)
#define PERM_COPY (1 << 15)
  // #define PERM_ENTER_PARCEL (1 << 16)
  // #define PERM_TERRAFORM (1 << 17)
  // #define PERM_OWNER_DEBT (1 << 18)
#define PERM_MOVE (1 << 19)
#define PERM_DAMAGE (1 << 20)

void sim_get_asset(struct simulator_ctx *sim, uuid_t asset_id,
		   void(*cb)(struct simulator_ctx *sim, void *priv,
			     struct simple_asset *asset), void *cb_priv);
#ifdef __cplusplus
}
#endif

#endif

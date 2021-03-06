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

#ifndef CAJEPUT_CORE_H
#define CAJEPUT_CORE_H

#include <uuid/uuid.h>
#include <glib.h>
#include <libsoup/soup.h>
#include "caj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAJEPUT_API_VERSION 0x0015

struct user_ctx;
struct simulator_ctx;
struct simgroup_ctx;
struct cap_descrip;
struct caj_logger;

// --- START sim query code ---

struct simgroup_ctx* sim_get_simgroup(struct simulator_ctx* sim);

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
struct simulator_ctx* caj_local_sim_by_region_handle(struct simgroup_ctx *sgrp,
						     uint64_t region_handle);
struct caj_logger* caj_get_logger(struct simgroup_ctx *sgrp);
void* caj_get_grid_priv(struct simgroup_ctx *sgrp);
void caj_set_grid_priv(struct simgroup_ctx *sgrp, void* p);
void* sim_get_grid_priv(struct simulator_ctx *sim);
void* sim_get_script_priv(struct simulator_ctx *sim);
void sim_set_script_priv(struct simulator_ctx *sim, void* p);
float* sim_get_heightfield(struct simulator_ctx *sim);
float sim_get_terrain_height(struct simulator_ctx *sim, int x, int y);

double caj_get_timer(struct simgroup_ctx *sgrp);

// These, on the other hand, require you to g_free the returned string
char *sgrp_config_get_value(struct simgroup_ctx *sim, const char* section,
			    const char* key);
gboolean sgrp_config_get_bool(struct simgroup_ctx *sgrp, const char* section,
			      const char* key, GError **error);
char *sim_config_get_value(struct simulator_ctx *sim, const char* key,
			   GError **error);
gint sim_config_get_integer(struct simulator_ctx *sim, const char* key,
			   GError **error);

// Glue code
void sim_queue_soup_message(struct simulator_ctx *sim, SoupMessage* msg,
			    SoupSessionCallback callback, void* user_data);
void caj_queue_soup_message(struct simgroup_ctx *sgrp, SoupMessage* msg,
			    SoupSessionCallback callback, void* user_data);
void caj_http_add_handler (struct simgroup_ctx *sgrp,
			   const char            *path,
			   SoupServerCallback     callback,
			   gpointer               user_data,
			   GDestroyNotify         destroy);

// --- END sim query code ---


// ----------------- SIM HOOKS --------------------------

typedef void(*sim_generic_cb)(simulator_ctx* sim, void* priv);

void sim_add_shutdown_hook(struct simulator_ctx *sim,
			   sim_generic_cb cb, void *priv);
void sim_remove_shutdown_hook(struct simulator_ctx *sim,
			      sim_generic_cb cb, void *priv);

void cajeput_add_sim_added_hook(struct simgroup_ctx *sgrp,
				sim_generic_cb cb, void *priv);

// ------ MAP STUFF -----------------

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

typedef void(*caj_find_regions_cb)(void* cb_priv, 
				   struct map_block_info* blocks, 
				   int count);
typedef void(*caj_find_region_cb)(void* cb_priv, 
				   struct map_block_info* block);
void caj_map_block_request(struct simgroup_ctx *sgrp, int min_x, int max_x, 
			   int min_y, int max_y, caj_find_regions_cb cb,
			   void *cb_priv);
void caj_map_name_request(struct simgroup_ctx* sgrp, const char* name,
			  caj_find_regions_cb cb, void *cb_priv);
void caj_map_region_by_name(struct simgroup_ctx* sgrp, const char* name,
			    caj_find_region_cb cb, void *cb_priv);
void caj_map_region_by_uuid(struct simgroup_ctx* sgrp, const uuid_t id,
			    caj_find_region_cb cb, void *cb_priv);


// ----- MISC STUFF ---------

// this is actually the usual asset description, and needs renaming
struct simple_asset {
  char *name, *description;
  int8_t type;
  uuid_t id;
  caj_string data;
};

typedef struct permission_flags {
  uint32_t next, current, base, everyone, group;
} permission_flags;

struct inventory_item {
  char *name;
  uuid_t item_id, folder_id, owner_id;

  char *creator_id;
  uuid_t creator_as_uuid;
  char *description;

  permission_flags perms;

  int8_t inv_type, asset_type;
  uint8_t sale_type;
  int8_t group_owned; // a boolean

  uuid_t asset_id, group_id;

  uint32_t flags;
  int32_t sale_price;
  int32_t creation_date;
  // ...

  simple_asset *asset_hack; // evil HACK for prim scripts
  void *spriv; // used for scripts
};

void caj_shutdown_hold(struct simgroup_ctx *sgrp);
void caj_shutdown_release(struct simgroup_ctx *sgrp);
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
struct texture_desc *caj_get_texture(struct simgroup_ctx *sgrp, uuid_t asset_id);
void caj_request_texture(struct simgroup_ctx *sgrp, struct texture_desc *desc);


// these should probably be in their own header, but never mind.
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

  // more SL flags, almost but not quite the same as the asset ones
#define INV_TYPE_TEXTURE 0
#define INV_TYPE_SOUND 1
#define INV_TYPE_CALLING_CARD 2
#define INV_TYPE_LANDMARK 3
  // 4 and 5 aren't used anymore
#define INV_TYPE_OBJECT 6
#define INV_TYPE_NOTECARD 7
#define INV_TYPE_CATEGORY 8
#define INV_TYPE_ROOT 9
#define INV_TYPE_LSL 10
  // 11-14 not used anymore
#define INV_TYPE_SNAPSHOT 15
  // 16 not used anymore
#define INV_TYPE_ATTACHMENT 17 // curious?
#define INV_TYPE_WEARABLE 18 
#define INV_TYPE_ANIMATION 19
#define INV_TYPE_GESTURE 20


  // another bunch of SL flags, this time permissions
  // FIXME - what do we do with the low 4 or 5 bits?
#define PERM_TRANSFER (1 << 13)
#define PERM_MODIFY (1 << 14)
#define PERM_COPY (1 << 15)
  // #define PERM_ENTER_PARCEL (1 << 16)
  // #define PERM_TERRAFORM (1 << 17)
  // #define PERM_OWNER_DEBT (1 << 18)
#define PERM_MOVE (1 << 19)
#define PERM_DAMAGE (1 << 20)

#define PERM_FULL_PERMS 0x7fffffff

  // yet more SL flags, this time for inventory items
  // FIXME - move to cajeput_user.h?
#define INV_LANDMARK_IS_VISITED 0x1

#define INV_OBJECT_SET_PERMS 0x100 // set perms from inv item
#define INV_OBJECT_SET_SALE 0x1000
#define INV_OBJECT_SET_BASE_PERMS 0x10000
#define INV_OBJECT_SET_CURRENT_PERMS 0x20000
#define INV_OBJECT_SET_GROUP_PERMS 0x40000
#define INV_OBJECT_SET_EVERYONE_PERMS 0x80000
#define INV_OBJECT_SET_NEXT_PERMS 0x100000

  // flags that can't be cleared.
  // FIXME - this could probably be made less strict
#define INV_OBJECT_STICKY_FLAGS (INV_OBJECT_SET_PERMS| \
				 INV_OBJECT_SET_BASE_PERMS| \
				 INV_OBJECT_SET_CURRENT_PERMS|	\
				 INV_OBJECT_SET_GROUP_PERMS| \
				 INV_OBJECT_SET_EVERYONE_PERMS	\
				 INV_OBJECT_SET_NEXT_PERMS)


typedef void (*caj_put_asset_cb)(uuid_t asset_id, void *priv);

void caj_get_asset(struct simgroup_ctx *sgrp, uuid_t asset_id,
		   void(*cb)(struct simgroup_ctx *sgrp, void *priv,
			     struct simple_asset *asset), void *cb_priv);
void caj_put_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset,
		   caj_put_asset_cb cb, void *cb_priv);

  // forces all other permissions to be a subset of the base ones
void caj_sanitise_perms(struct permission_flags *perms);

void cajeput_get_library_owner(struct simgroup_ctx *sgrp, uuid_t out);
void cajeput_get_library_root(struct simgroup_ctx *sgrp, uuid_t out);

#ifdef __cplusplus
}
#endif

#endif

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

#ifndef CAJEPUT_GRID_GLUE_H
#define CAJEPUT_GRID_GLUE_H

#include <uuid/uuid.h>
// #include <glib.h>
// #include <libsoup/soup.h>
#include "caj_types.h"
#include "cajeput_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// This ought to be moved somewhere more sensible
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

struct cajeput_grid_hooks {
  void(*do_grid_login)(struct simgroup_ctx *sgrp, 
		       struct simulator_ctx* sim);
  void(*user_created)(struct simgroup_ctx *sgrp,
		      struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void **user_priv);

  /* user entered the region */
  void(*user_entered)(struct simgroup_ctx *sgrp,
		      simulator_ctx *sim, user_ctx *user,
		      void *user_priv);

  /* user is logging off */
  void(*user_logoff)(struct simgroup_ctx *sgrp, struct simulator_ctx* sim,
		     const uuid_t user_id, const uuid_t session_id,
		     const caj_vector3 *pos, const caj_vector3 *look_at);

  void(*cleanup)(struct simgroup_ctx* sgrp);

  /* user context is being deleted */
  void(*user_deleted)(struct simgroup_ctx *sgrp,
		      struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void *user_priv);

  void(*do_teleport)(struct simgroup_ctx *sgrp,
		     struct simulator_ctx* sim, 
		     struct teleport_desc* tp);

  void(*get_texture)(struct simgroup_ctx *sgrp, struct texture_desc *texture);
  void(*get_asset)(struct simgroup_ctx *sgrp, struct simple_asset *asset);
  void(*put_asset)(struct simgroup_ctx *sgrp, struct simple_asset *asset,
		   caj_put_asset_cb cb, void *cb_priv);
  void(*map_block_request)(struct simgroup_ctx *sgrp, int min_x, int max_x, 
			   int min_y, int max_y, 
			   caj_find_regions_cb cb, void *cb_priv);
  void(*map_name_request)(struct simgroup_ctx* sgrp, const char* name,
			  caj_find_regions_cb cb, void *cb_priv);
  void(*map_region_by_name)(struct simgroup_ctx* sgrp, const char* name,
			    caj_find_region_cb cb, void *cb_priv);

  // interesting interesting function...
  void(*fetch_inventory_folder)(simgroup_ctx *sgrp, user_ctx *user,
				void *user_priv, uuid_t folder_id,
				void(*cb)(struct inventory_contents* inv, 
					  void* priv),
				void *cb_priv);
  void (*fetch_inventory_item)(simgroup_ctx *sgrp, user_ctx *user,
			       void *user_priv, uuid_t item_id,
			       void(*cb)(struct inventory_item* item, 
					 void* priv),
			       void *cb_priv);
  void (*fetch_system_folders)(simgroup_ctx *sgrp, user_ctx *user,
			       void *user_priv);

  void(*add_inventory_item)(simgroup_ctx *sgrp, user_ctx *user,
			    void *user_priv, inventory_item *inv,
			    void(*cb)(void* priv, int success, uuid_t item_id),
			    void *cb_priv);

  void(*uuid_to_name)(struct simgroup_ctx *sgrp, uuid_t id, 
		      void(*cb)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
		      void *cb_priv);
  void (*user_profile_by_id)(struct simgroup_ctx *sgrp, uuid_t id, 
			     caj_user_profile_cb cb, void *cb_priv);
};

// void do_grid_login(struct simulator_ctx* sim);

int cajeput_grid_glue_init(int api_version, struct simgroup_ctx *sgrp, 
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

void user_set_start_pos(user_ctx *ctx, const caj_vector3 *pos,
			const caj_vector3 *look_at);

// ICK. This function is for use by OpenSim glue code only.
void user_logoff_user_osglue(struct simulator_ctx *sim, uuid_t agent_id, 
			    uuid_t secure_session_id);

void caj_texture_finished_load(texture_desc *desc);
void caj_asset_finished_load(struct simgroup_ctx *sgrp, 
			     struct simple_asset *asset, int success);

// This is a bit odd in that it gives up ownership of the inventory_contents
// struct and data referenced to it to the OpenSim core code. The other
// inventory stuff doesn't work this way.
void user_set_system_folders(struct user_ctx *ctx, 
			     struct inventory_contents* inv);

#ifdef __cplusplus
}
#endif

#endif

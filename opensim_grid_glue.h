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

#ifndef OPENSIM_GRID_GLUE_H
#define OPENSIM_GRID_GLUE_H
#include <map>
#include <string>
#include "cajeput_grid_glue.h"

struct user_name {
  char *first, *last;
};

struct grid_glue_ctx {
  simgroup_ctx *sgrp;
  gchar *userserver, *gridserver, *assetserver;
  gchar *inventoryserver;
  gchar *user_recvkey, *asset_recvkey, *grid_recvkey;
  gchar *user_sendkey, *asset_sendkey, *grid_sendkey;
  uuid_t region_secret;

  // std::map<obj_uuid_t,user_name> uuid_name_cache; // TODO
};

struct user_grid_glue {
  int refcnt;
  user_ctx *ctx;
  char *enter_callback_uri; // callback for teleports
  std::map<uint64_t,std::string> child_seeds;
};

struct os_teleport_desc {
  simulator_ctx *our_sim; // for internal use
  char *sim_ip;
  int sim_port, http_port;
  teleport_desc* tp;
  char *caps_path;
};


void user_grid_glue_ref(user_grid_glue *user_glue);
void user_grid_glue_deref(user_grid_glue *user_glue);

#define GRID_PRIV_DEF(sim) struct grid_glue_ctx* grid = (struct grid_glue_ctx*) sim_get_grid_priv(sim);
#define GRID_PRIV_DEF_SGRP(sgrp) struct grid_glue_ctx* grid = (struct grid_glue_ctx*) caj_get_grid_priv(sgrp);
#define USER_PRIV_DEF(priv) struct user_grid_glue* user_glue = (struct user_grid_glue*) (priv);
#define USER_PRIV_DEF2(user) struct user_grid_glue* user_glue = (struct user_grid_glue*) user_get_grid_priv(user);

void fetch_inventory_folder(simgroup_ctx *sgrp, user_ctx *user,
			    void *user_priv, uuid_t folder_id,
			    void(*cb)(struct inventory_contents* inv, 
				      void* priv),
			    void *cb_priv);

void fetch_inventory_item(simgroup_ctx *sgrp, user_ctx *user,
			  void *user_priv, uuid_t item_id,
			  void(*cb)(struct inventory_item* item, 
				    void* priv),
			  void *cb_priv);
void fetch_system_folders(simgroup_ctx *sgrp, user_ctx *user,
			  void *user_priv);
void add_inventory_item(simgroup_ctx *sgrp, user_ctx *user,
			void *user_priv, inventory_item *inv,
			void(*cb)(void* priv, int success, uuid_t item_id),
			void *cb_priv);

void osglue_agent_rest_handler(SoupServer *server,
			       SoupMessage *msg,
			       const char *path,
			       GHashTable *query,
			       SoupClientContext *client,
			       gpointer user_data);

typedef void(*validate_session_cb)(void* state, int is_ok);
void osglue_validate_session(struct simgroup_ctx* sgrp, const char* agent_id,
			     const char *session_id, grid_glue_ctx* grid,
			     validate_session_cb callback, void *priv);

void osglue_teleport_send_agent(simulator_ctx* sim, teleport_desc *tp,
				os_teleport_desc *tp_priv);
void osglue_teleport_failed(os_teleport_desc *tp_priv, const char* reason);

void osglue_get_texture(struct simgroup_ctx *sgrp, struct texture_desc *texture);
void osglue_get_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset);
void osglue_put_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset,
		      caj_put_asset_cb cb, void *cb_priv);
#endif

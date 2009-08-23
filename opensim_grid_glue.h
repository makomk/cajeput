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

struct grid_glue_ctx {
  gchar *userserver, *gridserver, *assetserver;
  gchar *inventoryserver;
  gchar *user_recvkey, *asset_recvkey, *grid_recvkey;
  gchar *user_sendkey, *asset_sendkey, *grid_sendkey;
  uuid_t region_secret;
};

struct user_grid_glue {
  int refcnt;
  user_ctx *ctx;
  char *enter_callback_uri; // callback for teleports
  std::map<uint64_t,std::string> child_seeds;
};
void user_grid_glue_ref(user_grid_glue *user_glue);
void user_grid_glue_deref(user_grid_glue *user_glue);

#define GRID_PRIV_DEF(sim) struct grid_glue_ctx* grid = (struct grid_glue_ctx*) sim_get_grid_priv(sim);
#define USER_PRIV_DEF(priv) struct user_grid_glue* user_glue = (struct user_grid_glue*) (priv);
#define USER_PRIV_DEF2(user) struct user_grid_glue* user_glue = (struct user_grid_glue*) user_get_grid_priv(user);

void fetch_inventory_folder(simulator_ctx *sim, user_ctx *user,
			    void *user_priv, uuid_t folder_id,
			    void(*cb)(struct inventory_contents* inv, 
				      void* priv),
			    void *cb_priv);
#endif

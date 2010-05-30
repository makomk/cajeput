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

#include "cajeput_core.h"
#include "cajeput_plugin.h"
#include "cajeput_user.h"
#include "cajeput_grid_glue.h"
#include "caj_types.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <cassert>
#include <sqlite3.h>

#include <set>
#include <vector>
#include <map>
#include <string>

struct standalone_grid_ctx {
  simgroup_ctx *sgrp;
  std::set<simulator_ctx *> sims;
  sqlite3 *db;
};

#define GRID_PRIV_DEF(sim) struct standalone_grid_ctx* grid = (struct standalone_grid_ctx*) sim_get_grid_priv(sim);
#define GRID_PRIV_DEF_SGRP(sgrp) struct standalone_grid_ctx* grid = (struct standalone_grid_ctx*) caj_get_grid_priv(sgrp);
#define USER_PRIV_DEF(priv) struct user_grid_glue* user_glue = (struct user_grid_glue*) (priv);
#define USER_PRIV_DEF2(user) struct user_grid_glue* user_glue = (struct user_grid_glue*) user_get_grid_priv(user);

struct user_grid_glue {
  int refcnt;
  user_ctx *ctx;
  std::map<uint64_t,std::string> child_seeds;
};


static void do_grid_login(struct simgroup_ctx *sgrp,
			  struct simulator_ctx* sim) {
  GRID_PRIV_DEF_SGRP(sgrp);
  grid->sims.insert(sim);
}

static void fill_map_info(struct simulator_ctx *sim, map_block_info *info) {
  info->x = sim_get_region_x(sim);
  info->y = sim_get_region_y(sim);
  info->name = sim_get_name(sim);
  info->access = 21; // FIXME - ???

  info->water_height = 20; info->num_agents = 0;
  info->flags = 0;
  uuid_clear(info->map_image); // TODO!
  
  info->sim_ip = sim_get_ip_addr(sim);
  info->sim_port = sim_get_udp_port(sim);
  info->http_port = sim_get_http_port(sim);
  sim_get_region_uuid(sim, info->region_id);
}

static void map_block_request(struct simgroup_ctx *sgrp, int min_x, int max_x, 
			      int min_y, int max_y, 
			      caj_find_regions_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  std::vector<simulator_ctx *> sims;
  for(std::set<simulator_ctx *>::iterator iter = grid->sims.begin();
      iter != grid->sims.end(); iter++) {
    uint32_t x = sim_get_region_x(*iter), y = sim_get_region_y(*iter);
    // FIXME - are these boundaries correct?
    if((x >= min_x) && (x < max_x) && (y >= min_y) && (y < max_y)) {
       sims.push_back(*iter);
    }
  }

  size_t count = sims.size();
  map_block_info * info = (map_block_info*)calloc(count, sizeof(map_block_info));

  for(size_t i = 0; i < count; i++) {
    fill_map_info(sims[i], info+i);
  }
  
  cb(cb_priv, info, count);
#if 0
  for(size_t i = 0; i < count; i++) {
    free(info[i].name); free(info[i].sim_ip);
  }
#endif
  free(info);
}

static void map_name_request(struct simgroup_ctx* sgrp, const char* name,
				caj_find_regions_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  std::vector<simulator_ctx *> sims;
  int prefix_len = strlen(name);
  for(std::set<simulator_ctx *>::iterator iter = grid->sims.begin();
      iter != grid->sims.end(); iter++) {
    if(strncmp(name, sim_get_name(*iter), prefix_len) == 0) {
       sims.push_back(*iter);
    }
  }
  
  size_t count = sims.size();
  map_block_info * info = (map_block_info*)calloc(count, sizeof(map_block_info));
  
  for(size_t i = 0; i < count; i++) {
    fill_map_info(sims[i], info+i);
  }
  
  cb(cb_priv, info, count);
#if 0
  for(size_t i = 0; i < count; i++) {
    free(info[i].name); free(info[i].sim_ip);
  }
#endif
  free(info);  
}

void map_region_by_name(struct simgroup_ctx* sgrp, const char* name,
			   caj_find_region_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  simulator_ctx *sim = NULL;
  for(std::set<simulator_ctx *>::iterator iter = grid->sims.begin();
      iter != grid->sims.end(); iter++) {
    if(strcmp(name, sim_get_name(*iter)) == 0) {
      sim = *iter;
    }
  }
  
  if(sim = NULL) {
    cb(cb_priv, NULL);
  } else {
    struct map_block_info info;
    fill_map_info(sim, &info);
    cb(cb_priv, &info);
#if 0
    free(info.name); free(info.sim_ip);
#endif
  }
}

#if 0 // not used
static void req_region_info(struct simulator_ctx* sim, uint64_t handle,
			    caj_find_region_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  simulator_ctx *sim = caj_local_sim_by_region_handle(sgrp, handle);
  
  if(sim = NULL) {
    cb(cb_priv, NULL);
  } else {
    struct map_block_info info;
    fill_map_info(sim, &info);
    cb(cb_priv, &info);
#if 0
    free(info.name); free(info.sim_ip);
#endif
  }  
}
#endif

static void user_created(struct simgroup_ctx *sgrp,
			 struct simulator_ctx* sim,
			 struct user_ctx* user,
			 void **user_priv) {
  // GRID_PRIV_DEF(sim);
  user_grid_glue *user_glue = new user_grid_glue();
  user_glue->ctx = user;
  user_glue->refcnt = 1;
  *user_priv = user_glue;
}


void user_grid_glue_ref(user_grid_glue *user_glue) {
  user_glue->refcnt++;
}

void user_grid_glue_deref(user_grid_glue *user_glue) {
  user_glue->refcnt--;
  if(user_glue->refcnt == 0) {
    assert(user_glue->ctx == NULL);
    delete user_glue;
  }
}

static void user_deleted(struct simgroup_ctx *sgrp,
			 struct simulator_ctx* sim,
			 struct user_ctx* user,
			 void *user_priv) {
  USER_PRIV_DEF(user_priv);
  user_glue->ctx = NULL;
  user_grid_glue_deref(user_glue);
}

static void user_entered(struct simgroup_ctx *sgrp,
			 simulator_ctx *sim, user_ctx *user,
			 void *user_priv) {
  USER_PRIV_DEF(user_priv);
  GRID_PRIV_DEF(sim);

#if 0 // TODO - FIXME - implement our own equivalent!
  if(user_glue->enter_callback_uri != NULL) {
    printf("DEBUG: calling back to %s on avatar entry\n",
	   user_glue->enter_callback_uri);

    // FIXME - shouldn't we have to, y'know, identify ourselves somehow?

    SoupMessage *msg = 
      soup_message_new ("DELETE", user_glue->enter_callback_uri);
    // FIXME - should we send a body at all?
    soup_message_set_request (msg, "text/plain",
			      SOUP_MEMORY_STATIC, "", 0);
    sim_shutdown_hold(sim);
    caj_queue_soup_message(sgrp, msg,
			   user_entered_callback_resp, sim);
    free(user_glue->enter_callback_uri);
    user_glue->enter_callback_uri = NULL;  
  }
#endif


  // user_update_presence_v2(sgrp, sim, user); - TODO - FIXME
}

static void user_logoff(struct simgroup_ctx *sgrp, struct simulator_ctx* sim,
			const uuid_t user_id, const uuid_t session_id,
			const caj_vector3 *pos, const caj_vector3 *look_at) { 
  // FIXME - TODO
}

struct os_teleport_desc {
  simulator_ctx *our_sim; // for internal use
  simulator_ctx *dest_sim;
  char *sim_ip;
  int sim_port, http_port;
  teleport_desc* tp;
};

static void osglue_teleport_failed(os_teleport_desc *tp_priv, const char* reason) {
  user_teleport_failed(tp_priv->tp, reason);
  free(tp_priv->sim_ip);
  sim_shutdown_release(tp_priv->dest_sim);
  delete tp_priv;
}

static void fill_in_child_caps(user_ctx *user, user_grid_glue *src) {
  USER_PRIV_DEF2(user);
  assert(user_glue != NULL);
  user_glue->child_seeds = src->child_seeds;
}

void do_teleport_send_agent(simulator_ctx* sim, teleport_desc *tp,
			    os_teleport_desc *tp_priv) {
  char seed_cap[50];
  USER_PRIV_DEF2(tp->ctx);
  user_teleport_progress(tp,"sending_dest"); // FIXME - is this right?

  if(user_glue->child_seeds.count(tp->region_handle) == 0) {
    uuid_t u; char buf[40];
    uuid_generate(u); uuid_unparse(u, buf);
    user_glue->child_seeds[tp->region_handle] = buf;
  }

  struct sim_new_user uinfo;
  user_get_uuid(tp->ctx, uinfo.user_id);
  user_get_session_id(tp->ctx, uinfo.session_id);
  user_get_secure_session_id(tp->ctx, uinfo.secure_session_id);
  uinfo.first_name = (char*)user_get_first_name(tp->ctx);
  uinfo.last_name = (char*)user_get_last_name(tp->ctx);
  uinfo.circuit_code = user_get_circuit_code(tp->ctx);
  snprintf(seed_cap,50,"%s0000/", 
	   user_glue->child_seeds[tp->region_handle].c_str());
  uinfo.seed_cap = seed_cap;
  uinfo.is_child = FALSE;

  user_ctx *new_user = sim_prepare_new_user(tp_priv->dest_sim, &uinfo);
  if(new_user == NULL) {
    osglue_teleport_failed(tp_priv,"???"); // FIXME
    return;
  }
  
  fill_in_child_caps(new_user, user_glue);
  
  {
      const wearable_desc* wearables = user_get_wearables(tp->ctx);
      for(int i = 0; i < SL_NUM_WEARABLES; i++) {
	user_set_wearable(new_user, i, wearables[i].item_id,
			  wearables[i].asset_id);
      }
  }

  user_set_start_pos(new_user, &tp->pos, &tp->pos);
  
  // FIXME - TODO - callback stuff.

  user_set_flag(new_user, AGENT_FLAG_INCOMING);
  {
    unsigned char data[SL_NUM_THROTTLES*4];
    user_get_throttles_block(tp->ctx, data, SL_NUM_THROTTLES*4);
    user_set_throttles_block(new_user, data, SL_NUM_THROTTLES*4);
  }
  {
    caj_string te; 
    caj_string_copy(&te, user_get_texture_entry(tp->ctx));
    user_set_texture_entry(new_user, &te);
  }
  {
    caj_string vp; 
    caj_string_copy(&vp, user_get_visual_params(tp->ctx));
    user_set_visual_params(new_user, &vp);
  }

  if(user_get_flags(tp->ctx) & AGENT_FLAG_ALWAYS_RUN)
    user_set_flag(new_user, AGENT_FLAG_ALWAYS_RUN);
  else user_clear_flag(new_user, AGENT_FLAG_ALWAYS_RUN);

  sim_shutdown_release(tp_priv->dest_sim);

  {
    teleport_desc *tp = tp_priv->tp;
    int seed_len = strlen(tp_priv->sim_ip)+70;
    char *seed_cap = (char*)malloc(seed_len);
    snprintf(seed_cap, seed_len, "http://%s:%i/CAPS/%s0000/", 
	     tp_priv->sim_ip, tp_priv->sim_port, 
	     user_glue->child_seeds[tp->region_handle].c_str());
    tp->seed_cap = seed_cap;
    
    user_complete_teleport(tp);
    
    free(seed_cap); free(tp_priv->sim_ip);
    delete tp_priv;
  }
}

static void do_teleport_resolve_cb(SoupAddress *addr,
				   guint status,
				   gpointer priv) {
  os_teleport_desc* tp_priv = (os_teleport_desc*)priv;
  sim_shutdown_release(tp_priv->our_sim);

  if(tp_priv->tp->ctx == NULL) {
    osglue_teleport_failed(tp_priv,"cancelled");
  } else if(status != SOUP_STATUS_OK) {
    osglue_teleport_failed(tp_priv,"Couldn't resolve sim IP address");
  } else {
    int len;
    struct sockaddr_in *saddr = (struct sockaddr_in*)
      soup_address_get_sockaddr(addr, &len);
    if(saddr == NULL || saddr->sin_family != AF_INET) {
      // FIXME - need to restrict resolution to IPv4
      osglue_teleport_failed(tp_priv, "FIXME: Got a non-AF_INET address for sim");
    } else {
      teleport_desc* tp = tp_priv->tp;
      tp->sim_port = tp_priv->sim_port;
      tp->sim_ip = saddr->sin_addr.s_addr;
      do_teleport_send_agent(tp_priv->our_sim, tp, tp_priv);
    }
  }
  g_object_unref(addr);
}

static void do_teleport(struct simgroup_ctx* sgrp,
			struct simulator_ctx* sim, struct teleport_desc* tp) {
  //GRID_PRIV_DEF(sim);
 
  // FIXME - handle teleport via region ID
  simulator_ctx *dest = caj_local_sim_by_region_handle(sgrp, tp->region_handle);
  if(dest == NULL) {
    user_teleport_failed(tp, "Couldn't find destination region");
  } else {
    user_teleport_progress(tp, "resolving");
    os_teleport_desc *tp_priv = new os_teleport_desc();
    tp_priv->our_sim = user_get_sim(tp->ctx);
    tp_priv->dest_sim = dest;
    tp_priv->sim_ip = strdup(sim_get_ip_addr(dest));
    tp_priv->sim_port = sim_get_udp_port(dest);
    tp_priv->http_port = sim_get_http_port(dest);
    tp_priv->tp = tp;

    // FIXME - use provided region handle

    // FIXME - do we really need to hold simulator shutdown here?
    sim_shutdown_hold(tp_priv->our_sim);
    sim_shutdown_hold(tp_priv->dest_sim);
    SoupAddress *addr = soup_address_new(tp_priv->sim_ip, 0);
    soup_address_resolve_async(addr, g_main_context_default(),
			       NULL, do_teleport_resolve_cb, tp_priv);
  }
}

static void cleanup(struct simgroup_ctx* sgrp) {
  GRID_PRIV_DEF_SGRP(sgrp);
  sqlite3_close(grid->db);
  delete grid;
}

static void fetch_inventory_folder(simgroup_ctx *sgrp, user_ctx *user,
				   void *user_priv, uuid_t folder_id,
				   void(*cb)(struct inventory_contents* inv, 
					     void* priv),
				   void *cb_priv) {
  // TODO
  cb(NULL, cb_priv);
}

static void fetch_inventory_item(simgroup_ctx *sgrp, user_ctx *user,
				 void *user_priv, uuid_t item_id,
				 void(*cb)(struct inventory_item* item, 
					   void* priv),
				 void *cb_priv) {
  // TODO
  cb(NULL, cb_priv);  
}

 static void add_inventory_item(simgroup_ctx *sgrp, user_ctx *user,
				void *user_priv, inventory_item *inv,
				void(*cb)(void* priv, int success, uuid_t item_id),
				void *cb_priv) {
   // TODO
   uuid_t u;
   uuid_clear(u); cb(cb_priv, FALSE, u);
 }

void fetch_system_folders(simgroup_ctx *sgrp, user_ctx *user,
			  void *user_priv) {
  // FIXME - TODO
}

void get_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset) {
  GRID_PRIV_DEF_SGRP(sgrp);
  // FIXME - TODO
  caj_asset_finished_load(sgrp, asset, FALSE);
}

void put_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset,
	       caj_put_asset_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  // FIXME - TODO
  uuid_t zero_uuid; uuid_clear(zero_uuid);
  cb(zero_uuid, cb_priv);
}

static void user_profile_by_id(struct simgroup_ctx *sgrp, uuid_t id, 
			       caj_user_profile_cb cb, void *cb_priv) {
  // FIXME - TODO
  cb(NULL, cb_priv);
}

static void uuid_to_name(struct simgroup_ctx *sgrp, uuid_t id, 
			    void(*cb)(uuid_t uuid, const char* first, 
				      const char* last, void *priv),
			    void *cb_priv) {
  // FIXME - TODO
  cb(id, NULL, NULL, cb_priv);
}

static void login_to_simulator(SoupServer *server,
			       SoupMessage *msg,
			       GValueArray *params,
			       struct simgroup_ctx* sgrp) {
  GRID_PRIV_DEF_SGRP(sgrp);
  GHashTable *args = NULL;
  uuid_t u; char buf[64]; char seed_cap[64];
  uuid_t agent_id;
  GHashTable *hash;
  struct simulator_ctx *sim; user_ctx *user;
  char *first, *last, *passwd, *s;
  const char *error_msg = "Error logging in";
  struct sim_new_user uinfo;
  // GError *error = NULL;
  printf("DEBUG: Got a login_to_simulator call\n");
  if(params->n_values != 1 || 
     !soup_value_array_get_nth (params, 0, G_TYPE_HASH_TABLE, &args)) 
    goto bad_args;

  if(!soup_value_hash_lookup(args,"first",G_TYPE_STRING,
			     &first)) goto bad_args;
  if(!soup_value_hash_lookup(args,"last",G_TYPE_STRING,
			     &last)) goto bad_args;
  if(!soup_value_hash_lookup(args,"passwd",G_TYPE_STRING,
			     &passwd)) goto bad_args;

  {
    sqlite3_stmt *stmt; int rc;
    rc = sqlite3_prepare_v2(grid->db, "SELECT first_name, last_name, id, passwd_salt, passwd_sha256 FROM users WHERE first_name = ? AND last_name = ?;", -1, &stmt, NULL);
    if( rc ) {
      fprintf(stderr, "Can't prepare user lookup: %s\n", sqlite3_errmsg(grid->db));
      goto out_fail;
    }
    if(sqlite3_bind_text(stmt, 1, first, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_text(stmt, 2, last, -1, SQLITE_TRANSIENT)) {
      fprintf(stderr, "Can't bind sqlite params: %s\n", sqlite3_errmsg(grid->db));
      sqlite3_finalize(stmt);
      goto out_fail;
    }
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_DONE) {
      fprintf(stderr, "ERROR: user does not exist\n");
      sqlite3_finalize(stmt);
      error_msg = "Incorrect username or password";
      goto out_fail;
    } else if(rc != SQLITE_ROW) {
      fprintf(stderr, "Error executing statement: %s\n", sqlite3_errmsg(grid->db));
      sqlite3_finalize(stmt);
      goto out_fail;
    }
    // FIXME - load first and last name from DB?
    const unsigned char *id_str = sqlite3_column_text(stmt, 2);
    const unsigned char *pw_salt = sqlite3_column_text(stmt, 3);
    const unsigned char *pw_sha256 = sqlite3_column_text(stmt, 4);

    GChecksum *ck = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(ck, pw_salt, strlen((const char*)pw_salt));
    g_checksum_update(ck, (guchar*)passwd, strlen(passwd));
    const gchar *our_sha256 = g_checksum_get_string(ck);

    // FIXME - in theory this leads to a potential timing attack.
    if(strcasecmp(our_sha256, (const char*)pw_sha256) != 0) {
      fprintf(stderr, "ERROR: incorrect password entered\n");
      sqlite3_finalize(stmt);
      error_msg = "Incorrect username or password";
      goto out_fail;
    }

    uuid_parse((const char*)id_str, agent_id);
    sqlite3_finalize(stmt);

  }

  {
    // Pick a sim, any sim. FIXME - do this right.
    std::set<simulator_ctx *>::iterator iter = grid->sims.begin();
    if(iter == grid->sims.end()) goto out_fail;
    sim = *iter;
  }

  uinfo.first_name = first; uinfo.last_name = last;
  uuid_copy(uinfo.user_id, agent_id);
  uuid_generate_random(uinfo.session_id);
  uuid_generate_random(uinfo.secure_session_id);
  uinfo.circuit_code = 123456; // FIXME!
  uuid_generate_random(u); uuid_unparse(u, buf);
  snprintf(seed_cap,50,"%s0000/", buf);
  uinfo.seed_cap = seed_cap;
  uinfo.is_child = FALSE;

  user = sim_prepare_new_user(sim, &uinfo);
  if(user == NULL) {
    goto out_fail;
  }

  // FIXME - need to save caps string.

  hash = soup_value_hash_new();
  soup_value_hash_insert(hash, "login", G_TYPE_STRING, "true");
  soup_value_hash_insert(hash, "first_name", G_TYPE_STRING, first);
  soup_value_hash_insert(hash, "last_name", G_TYPE_STRING, last);
  uuid_unparse(uinfo.user_id, buf);
  soup_value_hash_insert(hash, "agent_id", G_TYPE_STRING, buf);
  uuid_unparse(uinfo.session_id, buf);
  soup_value_hash_insert(hash, "session_id", G_TYPE_STRING, buf);
  uuid_unparse(uinfo.secure_session_id, buf);
  soup_value_hash_insert(hash, "secure_session_id", G_TYPE_STRING, buf);
  soup_value_hash_insert(hash, "circuit_code", G_TYPE_INT, uinfo.circuit_code);
  soup_value_hash_insert(hash, "sim_ip", G_TYPE_STRING, sim_get_ip_addr(sim));
  soup_value_hash_insert(hash, "region_x", G_TYPE_INT, 
			 256*sim_get_region_x(sim));
  soup_value_hash_insert(hash, "region_y", G_TYPE_INT, 
			 256*sim_get_region_y(sim));
  // FIXME - ??? suspect http_port setting here is wrong.
  soup_value_hash_insert(hash, "http_port", G_TYPE_INT, sim_get_http_port(sim)); 
  soup_value_hash_insert(hash, "sim_port", G_TYPE_INT, sim_get_udp_port(sim));
  {
    const char *sim_ip = sim_get_ip_addr(sim);
    int seed_len = strlen(sim_ip)+70;
    char *seed_cap_full = (char*)malloc(seed_len);
    snprintf(seed_cap_full, seed_len, "http://%s:%i/CAPS/%s", 
	     sim_ip, sim_get_http_port(sim), seed_cap);
    soup_value_hash_insert(hash, "seed_capability", G_TYPE_STRING, seed_cap_full);
    free(seed_cap_full);
  }
  soup_value_hash_insert(hash, "home", G_TYPE_STRING, 
			 "{'region_handle':[r256000,r256000], 'position':[r128.1942,r127.823,r23.24884], 'look_at':[r0.7071068,r0.7071068,r0]}"); // FIXME
  soup_value_hash_insert(hash, "start_location", G_TYPE_STRING, "last"); // FIXME
  soup_value_hash_insert(hash, "look_at", G_TYPE_STRING, "r128,r128,r70]'"); // FIXME!
  soup_value_hash_insert(hash, "seconds_since_epoch", G_TYPE_INT,
			 time(NULL)); // FIXME
  soup_value_hash_insert(hash, "message", G_TYPE_STRING, "Wecome to Cajeput");
  soup_value_hash_insert(hash, "agent_access", G_TYPE_STRING, "M");
  soup_value_hash_insert(hash, "agent_access_max", G_TYPE_STRING, "A");

  { 
    // FIXME!!!
    uuid_parse("e219aca4-ed7a-4118-946a-35c270b3b09f", u); // HACK to get startup
    uuid_unparse(u, buf);
    GHashTable *hash2 = soup_value_hash_new_with_vals("folder_id", G_TYPE_STRING,
						      buf, NULL);
    GValueArray *arr = soup_value_array_new_with_vals(G_TYPE_HASH_TABLE, hash2,
						      G_TYPE_INVALID);
    soup_value_hash_insert(hash, "inventory-root", G_TYPE_VALUE_ARRAY, arr);
    g_hash_table_unref(hash2);
    g_value_array_free(arr);
  }
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "inventory-skeleton", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }
  { 
    cajeput_get_library_root(sgrp, u); uuid_unparse(u, buf);
    GHashTable *hash2 = soup_value_hash_new_with_vals("folder_id", G_TYPE_STRING,
						      buf, NULL);
    GValueArray *arr = soup_value_array_new_with_vals(G_TYPE_HASH_TABLE, hash2,
						      G_TYPE_INVALID);
    soup_value_hash_insert(hash, "inventory-lib-root", G_TYPE_VALUE_ARRAY, arr);
    g_hash_table_unref(hash2);
    g_value_array_free(arr);
  }
  { 
    cajeput_get_library_owner(sgrp, u); uuid_unparse(u, buf);
    GHashTable *hash2 = soup_value_hash_new_with_vals("agent_id", G_TYPE_STRING,
						      buf, NULL);
    GValueArray *arr = soup_value_array_new_with_vals(G_TYPE_HASH_TABLE, hash2,
						      G_TYPE_INVALID);
    soup_value_hash_insert(hash, "inventory-lib-owner", G_TYPE_VALUE_ARRAY, arr);
    g_hash_table_unref(hash2);
    g_value_array_free(arr);
  }
  { 
    GValueArray *arr = soup_value_array_new();
    inventory_folder **lib_folders; size_t count;
    cajeput_get_library_skeleton(sgrp, &lib_folders, &count);
    for(size_t i = 0; i < count; i++) {
      inventory_folder *fold = lib_folders[i];
      uuid_unparse(fold->parent_id, buf);
      GHashTable *hash2 = 
	soup_value_hash_new_with_vals("parent_id", G_TYPE_STRING, buf, 
				      "version", G_TYPE_INT, 1, /* FIXME! */
				      "type_default", G_TYPE_INT, fold->asset_type,
				      "name", G_TYPE_STRING, fold->name,
				      NULL);
      uuid_unparse(fold->folder_id, buf);
      soup_value_hash_insert(hash2, "folder_id", G_TYPE_STRING, buf);
      soup_value_array_append(arr, G_TYPE_HASH_TABLE, hash2);
      g_hash_table_unref(hash2);
    }
    soup_value_hash_insert(hash, "inventory-skel-lib", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "initial-outfit", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "gestures", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "event_categories", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "event_notifications", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "classified_categories", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
#if 0
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "adult_compliant", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
#endif
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "buddy-list", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
  { 
    GHashTable *hash2 = soup_value_hash_new_with_vals("allow_first_life",
						      G_TYPE_STRING, "Y",
						      NULL);
    GValueArray *arr = soup_value_array_new_with_vals(G_TYPE_HASH_TABLE, hash2,
						      G_TYPE_INVALID);
    soup_value_hash_insert(hash, "ui-config", G_TYPE_VALUE_ARRAY, arr);
    g_hash_table_unref(hash2);
    g_value_array_free(arr);
  }  
#if 0
  { 
    GValueArray *arr = soup_value_array_new();
    soup_value_hash_insert(hash, "tutorial_settings", G_TYPE_VALUE_ARRAY, arr);
    g_value_array_free(arr);
  }  
#endif
  { 
    // FIXME - set these correctly
    GHashTable *hash2 = 
      soup_value_hash_new_with_vals("daylight_savings", G_TYPE_STRING, "N",
				    "stipend_since_login", G_TYPE_STRING, "N",
				    "gendered", G_TYPE_STRING, "Y",
				    "ever_logged_in", G_TYPE_STRING, "Y",
				    NULL);
    GValueArray *arr = soup_value_array_new_with_vals(G_TYPE_HASH_TABLE, hash2,
						      G_TYPE_INVALID);
    soup_value_hash_insert(hash, "login-flags", G_TYPE_VALUE_ARRAY, arr);
    g_hash_table_unref(hash2);
    g_value_array_free(arr);
  }  
  { 
    GHashTable *hash2 = soup_value_hash_new();
    GValueArray *arr = soup_value_array_new_with_vals(G_TYPE_HASH_TABLE, hash2,
						      G_TYPE_INVALID);
    soup_value_hash_insert(hash, "global-textures", G_TYPE_VALUE_ARRAY, arr);
    g_hash_table_unref(hash2);
    g_value_array_free(arr);
  }  

  // TODO: home, start_location, look_at
  // OPTIONS TODO: inventory-root, inventory-skeleton, inventory-lib-root, 
  //    inventory-lib-owner, inventory-skel-lib, initial-outfit, gestures, 
  //    event_categories, event_notifications, classified_categories, 
  //    adult_compliant, buddy-list, ui-config, tutorial_setting, login-flags, 
  //    global-textures
  soup_xmlrpc_set_response(msg, G_TYPE_HASH_TABLE, hash);
  g_hash_table_destroy(hash);
  g_value_array_free(params);
  return;
  
  
 out_fail:
  hash = soup_value_hash_new();
  soup_value_hash_insert(hash, "login", G_TYPE_STRING, "false");
  soup_value_hash_insert(hash, "reason", G_TYPE_STRING, "???");
  soup_value_hash_insert(hash, "message", G_TYPE_STRING, "Error logging in");
  soup_xmlrpc_set_response(msg, G_TYPE_HASH_TABLE, hash);
  g_hash_table_destroy(hash);
  g_value_array_free(params);
  return;

 bad_args:
  soup_xmlrpc_set_fault(msg, SOUP_XMLRPC_FAULT_SERVER_ERROR_INVALID_METHOD_PARAMETERS,
			"Bad arguments");  
  g_value_array_free(params);
  return;
  
}

// FIXME - move this to core?
static void xmlrpc_handler (SoupServer *server,
				SoupMessage *msg,
				const char *path,
				GHashTable *query,
				SoupClientContext *client,
				gpointer user_data) {
  struct simgroup_ctx* sgrp = (struct simgroup_ctx*) user_data;
  char *method_name;
  GValueArray *params;

  if(strcmp(path,"/") != 0) {
    printf("DEBUG: request for unhandled path %s\n",
	   path);
    if (msg->method == SOUP_METHOD_POST) {
      printf("DEBUG: POST data is ~%s~\n",
	     msg->request_body->data);
    }
    soup_message_set_status(msg,404);
    return;
  }

  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  if(!soup_xmlrpc_parse_method_call(msg->request_body->data,
				    msg->request_body->length,
				    &method_name, &params)) {
    printf("Couldn't parse XMLRPC method call\n");
    printf("DEBUG: ~%s~\n", msg->request_body->data);
    soup_message_set_status(msg,500);
    return;
  }

  printf("DEBUG XMLRPC call: ~%s~\n", msg->request_body->data);

  if(strcmp(method_name, "login_to_simulator") == 0) {
    login_to_simulator(server, msg, params, sgrp);
    
  } else {
    printf("DEBUG: unknown xmlrpc method %s called\n", method_name);
    g_value_array_free(params);
    soup_xmlrpc_set_fault(msg, SOUP_XMLRPC_FAULT_SERVER_ERROR_REQUESTED_METHOD_NOT_FOUND,
			  "Method %s not found", method_name);
  }
  g_free(method_name);
}


int cajeput_grid_glue_init(int api_major, int api_minor,
			   struct simgroup_ctx *sgrp, void **priv,
			   struct cajeput_grid_hooks *hooks) {
  if(api_major != CAJEPUT_API_VERSION_MAJOR || 
     api_minor < CAJEPUT_API_VERSION_MINOR) 
    return false;

  struct standalone_grid_ctx *grid = new standalone_grid_ctx;
  grid->sgrp = sgrp;
  *priv = grid;

  int rc;
  rc = sqlite3_open("standalone.sqlite", &grid->db);
  if( rc ){
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(grid->db));
    sqlite3_close(grid->db);
    delete grid; return false;
  }

  hooks->do_grid_login = do_grid_login;
  hooks->map_block_request = map_block_request;
  hooks->map_name_request = map_name_request;
  hooks->map_region_by_name = map_region_by_name;
  hooks->user_created = user_created;
  hooks->user_deleted = user_deleted;
  hooks->user_entered = user_entered;
  hooks->do_teleport = do_teleport;
  hooks->fetch_inventory_folder = fetch_inventory_folder;
  hooks->fetch_inventory_item = fetch_inventory_item;
  hooks->fetch_system_folders = fetch_system_folders;

  hooks->uuid_to_name = uuid_to_name;
  hooks->user_profile_by_id = user_profile_by_id;
  hooks->user_logoff = user_logoff;
  hooks->add_inventory_item = add_inventory_item;

  hooks->get_asset = get_asset;
  hooks->put_asset = put_asset;
  hooks->cleanup = cleanup;

  caj_http_add_handler(sgrp, "/", xmlrpc_handler, 
		       sgrp, NULL);

  return true;
}

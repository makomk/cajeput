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
#include "caj_parse_nini.h"
#include "caj_logging.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <cassert>
#include <sqlite3.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <set>
#include <vector>
#include <map>
#include <string>

struct standalone_grid_ctx {
  simgroup_ctx *sgrp;
  std::set<simulator_ctx *> sims;
  std::map<obj_uuid_t, simulator_ctx*> sim_by_uuid;
  sqlite3 *db;
  caj_logger *log;
};

#define CAJ_LOGGER (grid->log)

#define GRID_PRIV_DEF(sim) struct standalone_grid_ctx* grid = (struct standalone_grid_ctx*) sim_get_grid_priv(sim);
#define GRID_PRIV_DEF_SGRP(sgrp) struct standalone_grid_ctx* grid = (struct standalone_grid_ctx*) caj_get_grid_priv(sgrp);
#define USER_PRIV_DEF(priv) struct user_grid_glue* user_glue = (struct user_grid_glue*) (priv);
#define USER_PRIV_DEF2(user) struct user_grid_glue* user_glue = (struct user_grid_glue*) user_get_grid_priv(user);

struct user_grid_glue {
  int refcnt;
  user_ctx *ctx;
  user_grid_glue *prev_user;
  std::map<uint64_t,std::string> child_seeds;
};


static void do_grid_login(struct simgroup_ctx *sgrp,
			  struct simulator_ctx* sim) {
  uuid_t u;
  GRID_PRIV_DEF_SGRP(sgrp);
  grid->sims.insert(sim);
  sim_get_region_uuid(sim, u);
  grid->sim_by_uuid[u] = sim;
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
  user_glue->prev_user = NULL;
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
    // We shouldn't ever get circular references here.
    // In fact, we shouldn't ever have more than one layer of recursion.
    if(user_glue->prev_user != NULL) 
      user_grid_glue_deref(user_glue->prev_user);
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

  if(user_glue->prev_user != NULL) {
    CAJ_DEBUG("DEBUG: calling back to previous region on avatar entry\n");
    user_ctx *old_ctx = user_glue->prev_user->ctx;
    if(old_ctx != NULL && 
       (user_get_flags(old_ctx) & AGENT_FLAG_TELEPORT_COMPLETE)) {
      CAJ_DEBUG("DEBUG: releasing teleported user\n");
      user_session_close(old_ctx, true); // needs the delay...
    }
    user_grid_glue_deref(user_glue->prev_user);
    user_glue->prev_user = NULL;
  }

  // user_update_presence_v2(sgrp, sim, user); - TODO - FIXME
}

static void user_logoff(struct simgroup_ctx *sgrp, struct simulator_ctx* sim,
			const uuid_t user_id, const uuid_t session_id,
			const caj_vector3 *pos, const caj_vector3 *look_at) { 
  GRID_PRIV_DEF(sim);
  sqlite3_stmt *stmt; int rc; uuid_t u; user_ctx *ctx;
  char user_id_str[40], region_id_str[40];
  char pos_str[80], look_at_str[80];

  rc = sqlite3_prepare_v2(grid->db, "UPDATE users SET last_region=?, last_pos=?, last_look_at=? WHERE id=?;", -1, &stmt, NULL);
  if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare last_pos update statement: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out;
  }

  // FIXME - TODO

  uuid_unparse(user_id, user_id_str);
  sim_get_region_uuid(sim, u);
  uuid_unparse(u, region_id_str);
  snprintf(pos_str, 80, "<%f,%f,%f>", pos->x, pos->y, pos->z);
  snprintf(look_at_str, 80, "<%f,%f,%f>", look_at->x, look_at->y, look_at->z);
  if(sqlite3_bind_text(stmt, 1, region_id_str, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 2, pos_str, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 3, look_at_str, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 4, user_id_str, -1, SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	      sqlite3_errmsg(grid->db));
    goto out_finalize;
  }

  if(sqlite3_step(stmt) != SQLITE_DONE) {
    CAJ_ERROR("ERROR: Can't update last position on logout: %s\n", 
	    sqlite3_errmsg(grid->db));
  }

  sqlite3_finalize(stmt);

  ctx = user_find_ctx(sim, user_id);
  if(ctx == NULL) {
    CAJ_ERROR("ERROR: can't find context for user in user_logoff\n");
    goto out;
  }

  rc = sqlite3_exec(grid->db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
  if(rc) {
    CAJ_ERROR("ERROR: Can't begin transaction: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out;
  }  

  rc = sqlite3_prepare_v2(grid->db, "DELETE FROM wearables WHERE user_id=?;", -1, &stmt, NULL);
  if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare wearables clear statement: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out;
  }

  if(sqlite3_bind_text(stmt, 1, user_id_str, -1, SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_et_finalize;
  }

  if(sqlite3_step(stmt) != SQLITE_DONE) {
    CAJ_ERROR("ERROR: Can't clear wearables from DB on logout: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_et_finalize;
  }
  
  sqlite3_finalize(stmt);
  
  rc = sqlite3_prepare_v2(grid->db, "INSERT INTO wearables(user_id, " 
			  "wearable_id, item_id, asset_id) VALUES (?, ?, ?, ?);",
			  -1, &stmt, NULL);

  if( rc ) {
    CAJ_ERROR("ERROR: Can't prepare wearables INSERT statement: %s\n", 
	    sqlite3_errmsg(grid->db));
    sqlite3_exec(grid->db, "END TRANSACTION;", NULL, NULL, NULL);
    goto out;
  }

  for(int i = 0; i < SL_NUM_WEARABLES; i++) {
    const wearable_desc* wearables = user_get_wearables(ctx);
    char item_id_str[40], asset_id_str[40];
    uuid_unparse(wearables[i].item_id, item_id_str);
    uuid_unparse(wearables[i].asset_id, asset_id_str);
    if(sqlite3_bind_text(stmt, 1, user_id_str, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_int(stmt, 2, i) ||
       sqlite3_bind_text(stmt, 3, item_id_str, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_text(stmt, 4, asset_id_str, -1, SQLITE_TRANSIENT)) {
      CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	      sqlite3_errmsg(grid->db));
      continue;
    }

    if(sqlite3_step(stmt) != SQLITE_DONE) {
      CAJ_ERROR("ERROR: Can't clear insert wearable into DB on logout: %s\n", 
	      sqlite3_errmsg(grid->db));
    }

    sqlite3_reset(stmt);
  }

 out_et_finalize:
  sqlite3_exec(grid->db, "END TRANSACTION;", NULL, NULL, NULL);
 out_finalize:
  sqlite3_finalize(stmt);
 out:
  return;
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

static void set_prev_region(user_ctx *user, user_grid_glue *prev) {
  USER_PRIV_DEF2(user);
  assert(user_glue != NULL);
  user_grid_glue_ref(prev);
  user_glue->prev_user = prev;
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

  set_prev_region(new_user, user_glue);

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

// FIXME - NULL handling for UUID columns is spectacularly dodgy!

#define INV_ITEM_COLUMNS " user_id, item_id, folder_id, name, description, " \
  "creator, inv_type, asset_type, asset_id, base_perms, " \
  "current_perms, next_perms, group_perms, everyone_perms, " \
  "group_id, group_owned, sale_price, sale_type, flags, " \
  " creation_date "

static bool inv_item_from_sqlite(standalone_grid_ctx *grid, 
				 sqlite3_stmt *stmt, inventory_item *item) {
  if(uuid_parse((const char*)sqlite3_column_text(stmt, 0), item->owner_id)) {
    CAJ_ERROR("ERROR: invalid item ID in inventory: %s\n", 
	    sqlite3_column_text(stmt, 0));
    return false;
  }
  if(uuid_parse((const char*)sqlite3_column_text(stmt, 1), item->item_id)) {
    CAJ_ERROR("ERROR: invalid item ID in inventory: %s\n", 
	    sqlite3_column_text(stmt, 1));
    return false;
  }
  if(uuid_parse((const char*)sqlite3_column_text(stmt, 2), item->folder_id)) {
    CAJ_ERROR("ERROR: invalid folder ID in inventory: %s\n", 
	    sqlite3_column_text(stmt, 2));
    return false;
  }
  item->name = (char*)sqlite3_column_text(stmt, 3);
  item->description = (char*)sqlite3_column_text(stmt, 4);
  item->creator_id = (char*)sqlite3_column_text(stmt, 5);
  if(uuid_parse(item->creator_id, item->creator_as_uuid)) {
    CAJ_INFO("DEBUG: invalid creator ID in inventory: %s\n", 
	    sqlite3_column_text(stmt, 5));
    uuid_clear(item->creator_as_uuid);
  }
  item->inv_type = sqlite3_column_int(stmt, 6);
  item->asset_type = sqlite3_column_int(stmt, 7);
  if(uuid_parse((const char*)sqlite3_column_text(stmt, 8), item->asset_id)) {
    CAJ_ERROR("ERROR: invalid asset ID in inventory: %s\n", 
	    sqlite3_column_text(stmt, 8));
    return false;
  }
  item->perms.base = sqlite3_column_int(stmt, 9);
  item->perms.current = sqlite3_column_int(stmt, 10);
  item->perms.next = sqlite3_column_int(stmt, 11);
  item->perms.group = sqlite3_column_int(stmt, 12);
  item->perms.everyone = sqlite3_column_int(stmt, 13);
  uuid_clear(item->group_id); item->group_owned = FALSE; // FIXME!
  item->sale_price = sqlite3_column_int(stmt, 16);
  item->sale_type = sqlite3_column_int(stmt, 17);
  item->flags = sqlite3_column_int(stmt, 18);
  item->creation_date = sqlite3_column_int(stmt, 19);

  return true;
}

static void fetch_inventory_folder(simgroup_ctx *sgrp, user_ctx *user,
				   void *user_priv, const uuid_t folder_id,
				   void(*cb)(struct inventory_contents* inv, 
					     void* priv),
				   void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  struct inventory_contents* inv = caj_inv_new_contents_desc(folder_id);
  sqlite3_stmt *stmt; int rc; char buf[40], buf2[40]; uuid_t user_id, u;

  rc = sqlite3_prepare_v2(grid->db, "SELECT folder_id, name, version, asset_type FROM inventory_folders WHERE user_id = ? AND parent_id = ?;", -1, &stmt, NULL);
  if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare inventory folder query: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out_fail;
  }

  user_get_uuid(user, user_id);
  uuid_unparse(user_id, buf);
  uuid_unparse(folder_id, buf2);
  if(sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 2, buf2, -1, SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
  }

  for(;;) {
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_DONE) 
      break;
    
    if(rc != SQLITE_ROW) {
      CAJ_ERROR("ERROR executing statement: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out_fail_finalize;
    }

    // SELECT folder_id, name, version, asset_type 

    if(uuid_parse((const char*)sqlite3_column_text(stmt, 0) /*folder_id*/, u)) {
       CAJ_ERROR("ERROR: invalid folder ID in inventory: %s\n", 
	       sqlite3_column_text(stmt, 0));
      goto out_fail_finalize;
    }
    caj_inv_add_folder(inv, u, user_id,
		       (const char*)sqlite3_column_text(stmt, 1) /* name */,
		       sqlite3_column_int(stmt, 3) /* asset_type */);
  }

  sqlite3_finalize(stmt);

  rc = sqlite3_prepare_v2(grid->db, "SELECT " INV_ITEM_COLUMNS 
			  " FROM inventory_items WHERE "
			  " user_id = ? AND folder_id = ?;", -1, &stmt, NULL);
  if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare inventory items query: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out_fail;
  }

  user_get_uuid(user, user_id);
  uuid_unparse(user_id, buf);
  uuid_unparse(folder_id, buf2);
  if(sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 2, buf2, -1, SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
  }

  
  for(;;) {
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_DONE) 
      break;
    
    if(rc != SQLITE_ROW) {
      CAJ_ERROR("ERROR executing statement: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out_fail_finalize;
    }

    // SELECT user_id, item_id, folder_id, name, description, creator, inv_type, 
    // asset_type, asset_id, base_perms, current_perms, next_perms, group_perms,
    // everyone_perms, group_id, group_owned, sale_price, sale_type, flags, 
    // creation_date
    struct inventory_item item_from_db;
    if(!inv_item_from_sqlite(grid, stmt, &item_from_db))
      goto out_fail_finalize;
    caj_add_inventory_item_copy(inv, &item_from_db);
  }

  sqlite3_finalize(stmt);
  cb(inv, cb_priv);
  caj_inv_free_contents_desc(inv);
  return;

 out_fail_finalize:
  sqlite3_finalize(stmt);
 out_fail:
  caj_inv_free_contents_desc(inv);
  cb(NULL, cb_priv);
}

static void fetch_inventory_item(simgroup_ctx *sgrp, user_ctx *user,
				 void *user_priv, const uuid_t item_id,
				 void(*cb)(struct inventory_item* item, 
					   void* priv),
				 void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  sqlite3_stmt *stmt; int rc; char buf[40], buf2[40]; uuid_t user_id;
  
  rc = sqlite3_prepare_v2(grid->db, "SELECT " INV_ITEM_COLUMNS 
			  " FROM inventory_items WHERE "
			  " user_id = ? AND item_id = ?;", -1, &stmt, NULL);
  if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare inventory item query: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out_fail;
  }

  user_get_uuid(user, user_id);
  uuid_unparse(user_id, buf);
  uuid_unparse(item_id, buf2);
  if(sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 2, buf2, -1, SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
  }

  rc = sqlite3_step(stmt);

  if(rc == SQLITE_DONE) { 
    CAJ_INFO("DEBUG: inventory item not found\n");
    goto out_fail_finalize;
  }
    
  if(rc != SQLITE_ROW) {
    CAJ_ERROR("ERROR executing statement: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
  }

  { 
    struct inventory_item item;
    if(!inv_item_from_sqlite(grid, stmt, &item))
      goto out_fail_finalize;
    cb(&item, cb_priv); 
  }
  sqlite3_finalize(stmt);
  return;

 out_fail_finalize:
  sqlite3_finalize(stmt);
 out_fail:
  cb(NULL, cb_priv);  
}

static void add_inventory_item(simgroup_ctx *sgrp, user_ctx *user,
			       void *user_priv, inventory_item *inv,
			       void(*cb)(void* priv, int success, uuid_t item_id),
			       void *cb_priv) {
   GRID_PRIV_DEF_SGRP(sgrp);
   sqlite3_stmt *stmt; int rc; 
   uuid_t user_id, u;
   char user_id_str[40], folder_id_str[40], item_id_str[40];
   char asset_id_str[40], group_id_str[40];


   rc = sqlite3_prepare_v2(grid->db, "INSERT INTO inventory_items "
			  "(user_id, item_id, folder_id, name, description, creator, "
			  "inv_type, asset_type, asset_id, base_perms, "
			  "current_perms, next_perms, group_perms, everyone_perms, "
			  "group_id, group_owned, sale_price, sale_type, flags, "
			  " creation_date) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
			  "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);

   if( rc ) {
     CAJ_ERROR("ERROR: Can't prepare add inventory item query: %s\n", 
	     sqlite3_errmsg(grid->db));
     goto out_fail;
   }
   
   user_get_uuid(user, user_id);
   uuid_unparse(user_id, user_id_str);
   uuid_unparse(inv->folder_id, folder_id_str);
   uuid_unparse(inv->item_id, item_id_str);
   uuid_unparse(inv->asset_id, asset_id_str);
   uuid_unparse(inv->group_id, group_id_str);

   if(sqlite3_bind_text(stmt, 1, user_id_str, -1, SQLITE_TRANSIENT) ||
      sqlite3_bind_text(stmt, 2, item_id_str, -1, SQLITE_TRANSIENT) ||
      sqlite3_bind_text(stmt, 3, folder_id_str, -1, SQLITE_TRANSIENT) ||
      sqlite3_bind_text(stmt, 4, inv->name, -1, SQLITE_TRANSIENT) ||
      sqlite3_bind_text(stmt, 5, inv->description, -1, SQLITE_TRANSIENT) ||
      sqlite3_bind_text(stmt, 6, inv->creator_id, -1, SQLITE_TRANSIENT) ||
      sqlite3_bind_int(stmt, 7, inv->inv_type) ||
      sqlite3_bind_int(stmt, 8, inv->asset_type) ||
      sqlite3_bind_text(stmt, 9, asset_id_str, -1, SQLITE_TRANSIENT) ||
      sqlite3_bind_int(stmt, 10, inv->perms.base) ||
      sqlite3_bind_int(stmt, 11, inv->perms.current) ||
      sqlite3_bind_int(stmt, 12, inv->perms.next) ||
      sqlite3_bind_int(stmt, 13, inv->perms.group) ||
      sqlite3_bind_int(stmt, 14, inv->perms.everyone) ||
      sqlite3_bind_text(stmt, 15, group_id_str, -1, SQLITE_TRANSIENT) ||
      sqlite3_bind_int(stmt, 16, inv->group_owned) ||
      sqlite3_bind_int(stmt, 17, inv->sale_price) ||
      sqlite3_bind_int(stmt, 18, inv->sale_type) ||
      sqlite3_bind_int(stmt, 19, inv->flags) ||
      sqlite3_bind_int(stmt, 20, inv->creation_date)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
   }

   rc = sqlite3_step(stmt);
    
   if(rc != SQLITE_DONE) {
     CAJ_ERROR("ERROR executing inventory item add statement: %s\n", 
	     sqlite3_errmsg(grid->db));
     goto out_fail_finalize;
   }

   sqlite3_finalize(stmt);
   cb(cb_priv, TRUE, inv->item_id);
   return;

 out_fail_finalize:
  sqlite3_finalize(stmt);
 out_fail:
   uuid_clear(u); cb(cb_priv, FALSE, u);
 }

void fetch_system_folders(simgroup_ctx *sgrp, user_ctx *user,
			  void *user_priv) {
  // FIXME - TODO
}

void get_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset) {
  GRID_PRIV_DEF_SGRP(sgrp);
  sqlite3_stmt *stmt; int rc; char buf[40]; const unsigned char* dat;

  rc = sqlite3_prepare_v2(grid->db, "SELECT name, description, asset_type, data FROM assets WHERE id = ?;", -1, &stmt, NULL);
  if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare get_asset query: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out_fail;
  }

  uuid_unparse(asset->id, buf);
  if(sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
  }

   rc = sqlite3_step(stmt);

  if(rc == SQLITE_DONE) {
    uuid_unparse(asset->id, buf);
    CAJ_ERROR("ERROR: Asset %s not in database\n", buf);
    goto out_fail_finalize; // asset not found.
  } else if(rc != SQLITE_ROW) {
     CAJ_ERROR("ERROR: Can't execute get_asset query: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
  }

  asset->name = strdup((const char*)sqlite3_column_text(stmt, 0));
  asset->description = strdup((const char*)sqlite3_column_text(stmt, 1));
  asset->type = sqlite3_column_int(stmt, 2);
  dat = (const unsigned char*)sqlite3_column_blob(stmt, 3);
  caj_string_set_bin(&asset->data, dat, sqlite3_column_bytes(stmt, 3));
  sqlite3_finalize(stmt);
  caj_asset_finished_load(sgrp, asset, TRUE);
  return;

 out_fail_finalize:
  sqlite3_finalize(stmt);
 out_fail:
  caj_asset_finished_load(sgrp, asset, FALSE);
}

void put_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset,
	       caj_put_asset_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  sqlite3_stmt *stmt; int rc; char buf[40];

  rc = sqlite3_prepare_v2(grid->db, "INSERT INTO assets (id, name, description, asset_type, data) values (?, ?, ?, ?, ?);", -1, &stmt, NULL);
  if( rc ) {
    CAJ_ERROR("ERROR: Can't prepare asset load query: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail;
  }

  uuid_unparse(asset->id, buf);
  if(sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 2, asset->name, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 3, asset->description, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_int(stmt, 4, asset->type) ||
     sqlite3_bind_blob(stmt, 5, asset->data.data, asset->data.len, 
		       SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
  }

  
  // FIXME - generate new UUID if we have a collision?
  rc = sqlite3_step(stmt);

  if(rc != SQLITE_DONE) {
    CAJ_ERROR("ERROR: Can't add asset: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_finalize;
  }
  
  sqlite3_finalize(stmt);
  cb(asset->id, cb_priv);
  return;

 out_fail_finalize:
  sqlite3_finalize(stmt);
 out_fail:
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

GRID_PRIV_DEF_SGRP(sgrp);
  sqlite3_stmt *stmt; int rc; uuid_t u;
  char user_id_str[40];

  rc = sqlite3_prepare_v2(grid->db, "SELECT first_name, last_name FROM users WHERE id=?;", -1, &stmt, NULL);
  if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare uuid to name statement: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out;
  }

  uuid_unparse(id, user_id_str);
  if(sqlite3_bind_text(stmt, 1, user_id_str, -1, SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_finalize;
  }

  rc = sqlite3_step(stmt);
  if(rc == SQLITE_DONE) {
    CAJ_WARN("WARNING: user not found in UUID-to-name lookup for %s\n",
	   user_id_str);
    goto out_finalize;
  } else if(rc != SQLITE_ROW) {
    CAJ_ERROR("ERROR: Can't lookup name from UUID: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_finalize;
  }

  cb(id, (const char*)sqlite3_column_text(stmt, 0), 
     (const char*)sqlite3_column_text(stmt, 1), cb_priv);
  sqlite3_finalize(stmt);
  return;

 out_finalize:
  sqlite3_finalize(stmt);
 out:
  cb(id, NULL, NULL, cb_priv);
}

struct initial_folder_info {
  const char* name;
  int asset_type;
};

static struct initial_folder_info initial_folders[] = {
  { "Animations", ASSET_ANIMATION },
  { "Body Parts", ASSET_BODY_PART },
  { "Calling Cards", ASSET_CALLING_CARD },
  { "Clothing", ASSET_CLOTHING },
  { "Gestures", ASSET_GESTURE },
  { "Landmarks", ASSET_LANDMARK },
  { "Lost And Found", ASSET_LOST_FOUND },
  { "Notecards", ASSET_NOTECARD },
  { "Objects", ASSET_OBJECT },
  { "Photo Album", ASSET_SNAPSHOT },
  { "Scripts", ASSET_LSL_TEXT },
  { "Sounds", ASSET_SOUND },
  { "Textures", ASSET_TEXTURE },
  { "Trash", ASSET_TRASH },
  { }
};

static bool create_user_inv(standalone_grid_ctx *grid, const uuid_t user_id,
			    uuid_t inv_root_out) {
  sqlite3_stmt *stmt; int rc; char buf[64];
  char user_id_str[40], folder_id_str[40], parent_id_str[40];
  

  uuid_generate(inv_root_out);
  rc = sqlite3_exec(grid->db, "BEGIN TRANSACTION;", NULL, NULL, 
		    NULL);
  if(rc) {
    CAJ_ERROR("ERROR: Can't begin transaction: %s\n", 
	    sqlite3_errmsg(grid->db));
    return false;
  }

  rc = sqlite3_prepare_v2(grid->db, "INSERT INTO inventory_folders (user_id, folder_id, parent_id, name, version, asset_type) VALUES (?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
  if( rc ) {
    CAJ_ERROR("ERROR: Can't prepare folder create: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_fail_nofree;
  }

  uuid_unparse(user_id, user_id_str);
  uuid_unparse(inv_root_out, folder_id_str);
  if(sqlite3_bind_text(stmt, 1, user_id_str, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_text(stmt, 2, folder_id_str, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_null(stmt, 3) /* parent_id */ || 
     sqlite3_bind_text(stmt, 4, "My Inventory", -1, SQLITE_STATIC) ||
     sqlite3_bind_int(stmt, 5, 1) /* version */ ||
     sqlite3_bind_int(stmt, 6, ASSET_ROOT) /* asset_type */ ||
     sqlite3_step(stmt) != SQLITE_DONE) {
    CAJ_ERROR("ERROR: Can't create inventory root folder: %s\n",
	    sqlite3_errmsg(grid->db));
    goto out_fail;
  }

  uuid_unparse(inv_root_out, parent_id_str);
  for(int i = 0; initial_folders[i].name != NULL; i++) {
    uuid_t u; uuid_generate(u);
    uuid_unparse(u, folder_id_str);
    sqlite3_reset(stmt);
   
    if(sqlite3_bind_text(stmt, 1, user_id_str, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_text(stmt, 2, folder_id_str, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_text(stmt, 3, parent_id_str, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_text(stmt, 4, initial_folders[i].name, -1, SQLITE_STATIC) ||
       sqlite3_bind_int(stmt, 5, 1) /* version */ ||
       sqlite3_bind_int(stmt, 6, initial_folders[i].asset_type) ||
       sqlite3_step(stmt) != SQLITE_DONE) {
      CAJ_ERROR("ERROR: Can't create initial inventory folder %s: %s\n",
	      initial_folders[i].name, sqlite3_errmsg(grid->db));
      goto out_fail;
    }
  }

  sqlite3_finalize(stmt);
  if(sqlite3_exec(grid->db, "COMMIT TRANSACTION;", NULL, NULL, NULL)) {
    CAJ_ERROR("ERROR: Can't commit initial inventory creation: %s\n",
	    sqlite3_errmsg(grid->db));
    return false;
  } else {
    return true;
  }

 out_fail:
  sqlite3_finalize(stmt);
 out_fail_nofree:
  sqlite3_exec(grid->db, "ROLLBACK TRANSACTION;", NULL, NULL, NULL);
  return false;
}

static bool find_inv_root(standalone_grid_ctx *grid, const uuid_t user_id,
			 uuid_t inv_root_out) {
  sqlite3_stmt *stmt; int rc; char buf[64];
  rc = sqlite3_prepare_v2(grid->db, "SELECT folder_id FROM inventory_folders WHERE user_id = ? AND parent_id IS NULL AND asset_type = ?", -1, &stmt, NULL);
  if( rc ) {
    CAJ_ERROR("ERROR: Can't prepare inv_root lookup: %s\n", 
	    sqlite3_errmsg(grid->db));
    return false;
  }

  uuid_unparse(user_id, buf);

  if(sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT) ||
     sqlite3_bind_int(stmt, 2, ASSET_ROOT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    sqlite3_finalize(stmt);
    return false;
  }

  rc = sqlite3_step(stmt);
  if(rc == SQLITE_DONE) {
    CAJ_INFO("INFO: no inventory root found, need to create one\n");
    sqlite3_finalize(stmt);
    return create_user_inv(grid, user_id, inv_root_out);
  } else if(rc != SQLITE_ROW) {
    CAJ_ERROR("ERROR executing statement: %s\n", sqlite3_errmsg(grid->db));
    sqlite3_finalize(stmt);
    return false;
  }

  const char *s = (const char*) sqlite3_column_text(stmt, 0);
  rc = uuid_parse(s, inv_root_out);
  sqlite3_finalize(stmt);
  return !rc;
}

// FIXME - this needs to actually report errors to its caller.
static GValueArray* build_inventory_skeleton(standalone_grid_ctx *grid, 
					     const uuid_t user_id) {
  GValueArray *skel = soup_value_array_new();
  sqlite3_stmt *stmt; int rc; char buf[40];

  // FIXME - can this use an index?
  rc = sqlite3_prepare_v2(grid->db, "SELECT folder_id, parent_id, name, version, asset_type FROM inventory_folders WHERE user_id = ?;", -1, &stmt, NULL);
  if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare inventory skeleton query: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out;
  }

  uuid_unparse(user_id, buf);
  if(sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT)) {
    CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", 
	    sqlite3_errmsg(grid->db));
    goto out_finalize;
  }

  for(;;) {
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_DONE) 
      break;
    
    if(rc != SQLITE_ROW) {
      CAJ_ERROR("ERROR executing statement: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out_finalize;
    }

    // SELECT folder_id, parent_id, name, version, asset_type 
    GHashTable *hash2 = 
      soup_value_hash_new_with_vals("folder_id", G_TYPE_STRING,
				    sqlite3_column_text(stmt, 0),
				    "parent_id", G_TYPE_STRING,
				    sqlite3_column_text(stmt, 1),
				    "name", G_TYPE_STRING,
				    sqlite3_column_text(stmt, 2),
				    "version", G_TYPE_INT, 
				    (int)sqlite3_column_int(stmt, 3),
				    "type_default", G_TYPE_INT, 
				    (int)sqlite3_column_int(stmt, 4),
				    NULL);
    soup_value_array_append(skel, G_TYPE_HASH_TABLE, hash2);
    g_hash_table_unref(hash2);
  }

 out_finalize:
  sqlite3_finalize(stmt);
 out:
  return skel;
}

static void set_default_wearables(user_ctx *user) {
  uuid_t item_id, asset_id;
  
  uuid_parse("66c41e39-38f9-f75a-024e-585989bfab73", asset_id);
  uuid_generate(item_id);  
  user_set_wearable(user, SL_WEARABLE_BODY, item_id, asset_id);

  uuid_parse("77c41e39-38f9-f75a-024e-585989bbabbb", asset_id);
  uuid_generate(item_id);  
  user_set_wearable(user, SL_WEARABLE_SKIN, item_id, asset_id);

  uuid_parse("00000000-38f9-1111-024e-222222111110", asset_id);
  uuid_generate(item_id);  
  user_set_wearable(user, SL_WEARABLE_SHIRT, item_id, asset_id);

  uuid_parse("00000000-38f9-1111-024e-222222111120", asset_id);
  uuid_generate(item_id);  
  user_set_wearable(user, SL_WEARABLE_PANTS, item_id, asset_id);

  uuid_parse("d342e6c0-b9d2-11dc-95ff-0800200c9a66", asset_id);
  uuid_generate(item_id);  
  user_set_wearable(user, SL_WEARABLE_HAIR, item_id, asset_id);

}

static void login_to_simulator(SoupServer *server,
			       SoupMessage *msg,
			       GValueArray *params,
			       struct simgroup_ctx* sgrp) {
  GRID_PRIV_DEF_SGRP(sgrp);
  GHashTable *args = NULL;
  uuid_t u; char buf[64]; char seed_cap[64];
  uuid_t agent_id; caj_vector3 pos, look_at;
  GHashTable *hash;
  struct simulator_ctx *sim = NULL; user_ctx *user;
  char *first, *last, *passwd, *s;
  const char *error_msg = "Error logging in";
  struct sim_new_user uinfo;
  // GError *error = NULL;
  CAJ_INFO("INFO: Got a login_to_simulator call\n");
  if(params->n_values != 1 || 
     !soup_value_array_get_nth (params, 0, G_TYPE_HASH_TABLE, &args)) 
    goto bad_args;

  if(!soup_value_hash_lookup(args,"first",G_TYPE_STRING,
			     &first)) goto bad_args;
  if(!soup_value_hash_lookup(args,"last",G_TYPE_STRING,
			     &last)) goto bad_args;
  if(!soup_value_hash_lookup(args,"passwd",G_TYPE_STRING,
			     &passwd)) goto bad_args;

  pos.x = pos.y = 128.0f; pos.z = 25.0f; look_at = pos;

  {
    sqlite3_stmt *stmt; int rc;
    rc = sqlite3_prepare_v2(grid->db, "SELECT first_name, last_name, id, passwd_salt, passwd_sha256, last_region, last_pos, last_look_at FROM users WHERE first_name = ? AND last_name = ?;", -1, &stmt, NULL);
    if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare user lookup: %s\n", sqlite3_errmsg(grid->db));
      goto out_fail;
    }
    if(sqlite3_bind_text(stmt, 1, first, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_text(stmt, 2, last, -1, SQLITE_TRANSIENT)) {
      CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", sqlite3_errmsg(grid->db));
      sqlite3_finalize(stmt);
      goto out_fail;
    }
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_DONE) {
      CAJ_ERROR("ERROR: user does not exist\n");
      sqlite3_finalize(stmt);
      error_msg = "Incorrect username or password";
      goto out_fail;
    } else if(rc != SQLITE_ROW) {
       CAJ_ERROR("ERROR: Couldn't execute statement: %s\n", sqlite3_errmsg(grid->db));
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
      CAJ_ERROR("ERROR: incorrect password entered\n");
      g_checksum_free(ck);
      sqlite3_finalize(stmt);
      error_msg = "Incorrect username or password";
      goto out_fail;
    }

    g_checksum_free(ck);

    if(sim == NULL) {
      if(sqlite3_column_type(stmt, 5) != SQLITE_NULL && 
	 uuid_parse((const char*)sqlite3_column_text(stmt, 5), u) == 0) {
	std::map<obj_uuid_t, simulator_ctx*>::iterator iter =
	  grid->sim_by_uuid.find(u);
	if(iter != grid->sim_by_uuid.end()) 
	  sim = iter->second;
	// FIXME - should we report that the chosen sim is unavailable?
      }

      float tx, ty, tz;
      if(sim != NULL && sqlite3_column_type(stmt, 6) != SQLITE_NULL &&
	 sscanf((const char*)sqlite3_column_text(stmt, 6), "<%f, %f, %f>",
		&tx, &ty, &tz) == 3) {
	CAJ_DEBUG("DEBUG: got initial position <%f, %f, %f>\n", tx, ty, tz);
	pos.x = tx; pos.y = ty; pos.z = tz;
	
	if(sqlite3_column_type(stmt, 7) != SQLITE_NULL && 
	   sscanf((const char*)sqlite3_column_text(stmt, 7), "<%f, %f, %f>",
		  &tx, &ty, &tz) == 3) {
	  look_at.x = tx; look_at.y = ty; look_at.z = tz;
	} else {
	  look_at = pos;
	}	
      }
    }
    uuid_parse((const char*)id_str, agent_id);
    sqlite3_finalize(stmt);
  }

  if(sim == NULL) {
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
  user_set_start_pos(user, &pos, &look_at);

  // FIXME - need to save caps string.

  set_default_wearables(user);
  
  {
    sqlite3_stmt *stmt; int rc;
    rc = sqlite3_prepare_v2(grid->db, "SELECT wearable_id, item_id, asset_id FROM wearables WHERE user_id = ?;", -1, &stmt, NULL);
    if( rc ) {
      CAJ_ERROR("ERROR: Can't prepare user lookup: %s\n", sqlite3_errmsg(grid->db));
      goto out_fail;
    }

    uuid_unparse(uinfo.user_id, buf);
    if(sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT)) {
      CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", sqlite3_errmsg(grid->db));
      sqlite3_finalize(stmt);
      goto out_fail;
    }
    rc = sqlite3_step(stmt);

    while(rc == SQLITE_ROW) {
      int wearable_id = sqlite3_column_int(stmt, 0);
      uuid_t item_id, asset_id;
      if(uuid_parse((const char*)sqlite3_column_text(stmt, 1), item_id)
	 != 0 || uuid_parse((const char*)sqlite3_column_text(stmt, 2), 
			      asset_id) != 0) {
	CAJ_ERROR("ERROR: bad wearable entry in DB\n");
	goto next_wearable;
      }
      user_set_wearable(user, wearable_id, item_id, asset_id);
    next_wearable:
      rc = sqlite3_step(stmt);
    }

    if(rc != SQLITE_DONE) {
      CAJ_ERROR("ERROR: when loading wearables: %s\n", 
	      sqlite3_errmsg(grid->db));
      sqlite3_finalize(stmt);
      goto out_fail;
    }
  }

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
  snprintf(buf, 64, "[r%i,r%i,r%i]", (int)look_at.x, (int)look_at.y, (int)look_at.z);
  soup_value_hash_insert(hash, "look_at", G_TYPE_STRING, buf);
  soup_value_hash_insert(hash, "seconds_since_epoch", G_TYPE_INT,
			 time(NULL)); // FIXME
  soup_value_hash_insert(hash, "message", G_TYPE_STRING, "Welcome to Cajeput");
  soup_value_hash_insert(hash, "agent_access", G_TYPE_STRING, "M");
  soup_value_hash_insert(hash, "agent_access_max", G_TYPE_STRING, "A");

  { 
    uuid_parse("e219aca4-ed7a-4118-946a-35c270b3b09f", u); // HACK to get startup
    if(!find_inv_root(grid, agent_id, u)) {
      CAJ_ERROR("ERROR: couldn't find inventory root folder\n");
      // FIXME - error out here.
    }
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
    GValueArray *arr = build_inventory_skeleton(grid, agent_id);
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
    inventory_folder **lib_folders = NULL; size_t count;
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
    cajeput_free_library_skeleton(lib_folders);
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
  GRID_PRIV_DEF_SGRP(sgrp);
  char *method_name;
  GValueArray *params;

  if(strcmp(path,"/") != 0) {
    CAJ_DEBUG("DEBUG: request for unhandled path %s\n",
	   path);
    if (msg->method == SOUP_METHOD_POST) {
      CAJ_DEBUG("DEBUG: POST data is ~%s~\n",
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
    CAJ_WARN("WARNING: Couldn't parse XMLRPC method call\n");
    CAJ_DEBUG("DEBUG: ~%s~\n", msg->request_body->data);
    soup_message_set_status(msg,500);
    return;
  }

  CAJ_DEBUG("DEBUG XMLRPC call: ~%s~\n", msg->request_body->data);

  if(strcmp(method_name, "login_to_simulator") == 0) {
    login_to_simulator(server, msg, params, sgrp);
    
  } else {
    CAJ_INFO("INFO: unknown xmlrpc method %s called\n", method_name);
    g_value_array_free(params);
    soup_xmlrpc_set_fault(msg, SOUP_XMLRPC_FAULT_SERVER_ERROR_REQUESTED_METHOD_NOT_FOUND,
			  "Method %s not found", method_name);
  }
  g_free(method_name);
}

static char *read_whole_file(const char *name, int *lenout) {
  int len = 0, maxlen = 512, ret;
  char *data = (char*)malloc(maxlen);
  //FILE *f = fopen(name,"r");
  int fd = open(name,O_RDONLY);
  if(fd < 0) return NULL;
  for(;;) {
    //ret = fread(data+len, maxlen-len, 1, f);
    ret = read(fd, data+len, maxlen-len);
    if(ret <= 0) break;
    len += ret;
    if(maxlen-len < 256) {
      maxlen += 512;
      data = (char*)realloc(data, maxlen);
    }
  }
  close(fd); *lenout = len; return data;
}

static void load_assets_2(sqlite3_stmt *stmt, standalone_grid_ctx *grid, 
			  gchar* filename) {
  char path[256]; char *path_off; int path_space; 
  gchar** sect_list;
  int rc; char buf[40]; 
  snprintf(path,256,"assets/%s",filename);

  GKeyFile *cfg = caj_parse_nini_xml(path);
  if(cfg == NULL) {
    CAJ_WARN("WARNING: couldn't load asset set file %s\n", filename);
    return;
  }

  path_off = strrchr(path, '/');
  if(path_off == NULL) path_off = path;
  else path_off++;
  path_space = 256 - (path_off - path);

  sect_list = g_key_file_get_groups(cfg, NULL);

  for(int i = 0; sect_list[i] != NULL; i++) {
    int datalen; char *data;
    gchar* assetid_str = g_key_file_get_value(cfg, sect_list[i], "assetID", NULL);
    gchar* name = g_key_file_get_value(cfg, sect_list[i], "name", NULL);
    gchar* assettype_str = g_key_file_get_value(cfg, sect_list[i], "assetType", NULL);
    gchar* assetfile = g_key_file_get_value(cfg, sect_list[i], "fileName", NULL);
    if(assetid_str == NULL || name == NULL || assettype_str == NULL || 
       assetfile == NULL) {
      CAJ_ERROR("ERROR: bad entry %s in asset set %s\n", sect_list[i], filename);
      goto out_cont;
    }

    strncpy(path_off, assetfile, path_space);
    path_off[path_space-1] = 0;
    data = read_whole_file(path, &datalen);

    if(data == NULL) {
      CAJ_ERROR("ERROR: can't read asset file %s\n", path);
      goto out_cont;
    }

    if(sqlite3_bind_blob(stmt, 5, data, datalen, free) || 
       sqlite3_bind_text(stmt, 1, assetid_str, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_text(stmt, 3, "Default asset", -1, SQLITE_TRANSIENT) ||
       sqlite3_bind_int(stmt, 4, atoi(assettype_str))) {
      CAJ_ERROR("ERROR: Can't bind sqlite params: %s\n", sqlite3_errmsg(grid->db));
      goto out_cont;
    }

    rc = sqlite3_step(stmt);
    
    if(rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) {
      CAJ_ERROR("ERROR executing asset add statement: %s\n", 
	      sqlite3_errmsg(grid->db));
      goto out_cont;
    }

  out_cont:
    g_free(assetid_str); g_free(name); g_free(assettype_str); g_free(assetfile);
    sqlite3_reset(stmt);
  }
  g_strfreev(sect_list);
 out:
  g_key_file_free(cfg);
}

static bool load_initial_assets(standalone_grid_ctx *grid) {
  sqlite3_stmt *stmt; int rc;
  GKeyFile *asset_sets = caj_parse_nini_xml("assets/AssetSets.xml");
  if(asset_sets == NULL) {
    CAJ_ERROR("ERROR: couldn't load assets/AssetSets.xml\n");
    return false;
  }
  
  rc = sqlite3_prepare_v2(grid->db, "INSERT INTO assets (id, name, description, asset_type, data) values (?, ?, ?, ?, ?);", -1, &stmt, NULL);
  if( rc ) {
    CAJ_ERROR("ERROR: Can't prepare asset load query: %s\n", 
	    sqlite3_errmsg(grid->db));
    return false;
  }

  gchar** sect_list = g_key_file_get_groups(asset_sets, NULL);

  for(int i = 0; sect_list[i] != NULL; i++) {
    gchar* filename = g_key_file_get_value(asset_sets, sect_list[i], "file", NULL);

    if(filename == NULL) {
      CAJ_WARN("WARNING: bad section %s in assets/AssetSets.xml\n", sect_list[i]);
    } else {
      load_assets_2(stmt, grid, filename);
    }
    g_free(filename);
  }
  sqlite3_finalize(stmt);
  g_strfreev(sect_list);
  g_key_file_free(asset_sets);
  return true;
}


int schema_version_cb(void *priv, int argc, char **argv, char **col_names) {
  int *schema_version = (int*)priv;
  if(argc > 0 && argv != NULL && argv[0] != NULL) {
    *schema_version = atoi(argv[0]);
  }
  return 0;
}

#define CUR_SCHEMA_VERSION 1

const char *create_db_stmts[] = {
  "CREATE TABLE assets (id varchar(36) primary key not null, name text, description text, asset_type int not null, data blob not null);",
  "CREATE TABLE inventory_folders (user_id varchar(36) not null, folder_id varchar(36) not null, parent_id varchar(36), name text not null, version integer not null, asset_type integer not null, PRIMARY KEY(user_id, folder_id), FOREIGN KEY(user_id, parent_id) REFERENCES inventory_folders(user_id, folder_id) ON DELETE CASCADE ON UPDATE RESTRICT);",
  "CREATE TABLE inventory_items (user_id varchar(36) not null, item_id varchar(36) not null, folder_id varchar(36) not null, name text not null, description text not null, creator text, inv_type integer not null, asset_type integer not null, asset_id varchar(36) not null, base_perms integer not null, current_perms integer not null, next_perms integer not null, group_perms integer not null, everyone_perms integer not null, group_id varchar(36) not null, group_owned integer not null, sale_price integer not null default 0, sale_type integer not null default 0, flags integer not null default 0, creation_date integer not null default 0, PRIMARY KEY(user_id, item_id), FOREIGN KEY(user_id, folder_id) REFERENCES inventory_folders(user_id, folder_id) ON DELETE CASCADE ON UPDATE RESTRICT);",
  "CREATE TABLE users (first_name varchar(30) not null, last_name varchar(30) not null, id varchar(36) primary key not null, session_id varchar(36), passwd_salt varchar(10), passwd_sha256 varchar(64), time_created integer not null, home_region varchar(36), home_pos text, home_look_at text, last_region varchar(36), last_pos text, last_look_at text);",
  "CREATE TABLE wearables (user_id varchar(36) NOT NULL, wearable_id int NOT NULL, item_id varchar(36) NOT NULL, asset_id varchar(36) NOT NULL);",
  "CREATE INDEX folder_parent_index ON inventory_folders(user_id, parent_id);",
  "CREATE INDEX item_folder_index ON inventory_items(user_id, folder_id);",
  "CREATE UNIQUE INDEX wearable_id_index ON wearables(user_id, wearable_id);",
  "CREATE INDEX wearable_index ON wearables(user_id);",
  "CREATE TABLE schema_version (name varchar[64] PRIMARY KEY, version int);",
  "INSERT INTO schema_version (name, version) VALUES ('cajeput_standalone', 1);",
  "COMMIT TRANSACTION;",
  NULL
};

bool init_db(struct standalone_grid_ctx *grid) {
  int rc; int schema_version = 0;
  rc = sqlite3_open("standalone.sqlite", &grid->db);
  if( rc ){
    CAJ_ERROR("ERROR: Can't open database: %s\n", sqlite3_errmsg(grid->db));
    sqlite3_close(grid->db); return false;
  }

  rc = sqlite3_exec(grid->db, "PRAGMA foreign_keys = ON;", NULL, NULL, 
		    NULL);
  if(rc) {
    CAJ_ERROR("ERROR: Can't enable foreign keys: %s\n", 
	    sqlite3_errmsg(grid->db));
    sqlite3_close(grid->db); return false;
  }

  rc = sqlite3_exec(grid->db, "SELECT version FROM schema_version WHERE "
		    "name = 'cajeput_standalone';", schema_version_cb, 
		    &schema_version, NULL);
  if(rc && rc != SQLITE_ERROR) {
     CAJ_ERROR("ERROR: Can't query schema version: %i %s\n", 
	     rc, sqlite3_errmsg(grid->db));
    sqlite3_close(grid->db); return false;
  }

  if(schema_version > CUR_SCHEMA_VERSION) {
    CAJ_ERROR("ERROR: Schema version %i is too new\n", 
	    schema_version);
    sqlite3_close(grid->db); return false;
  } else if(schema_version == 0) {
    CAJ_PRINT("Creating initial DB, please wait...\n");
    if(sqlite3_exec(grid->db, "BEGIN TRANSACTION;", NULL, NULL, 
		    NULL)) {
      CAJ_ERROR("ERROR: cannot begin transaction!");
      sqlite3_close(grid->db); return false;
    }
    for(int i = 0; create_db_stmts[i] != NULL; i++) {
      if(sqlite3_exec(grid->db, create_db_stmts[i], NULL, NULL, 
		    NULL)) {
	CAJ_ERROR("ERROR: cannot execute statement: %s",
		create_db_stmts[i]);
	sqlite3_exec(grid->db, "ROLLBACK TRANSACTION;", NULL, NULL, 
		     NULL);
	sqlite3_close(grid->db); return false;
      }
    }
  } else if(schema_version < CUR_SCHEMA_VERSION) {
  }
  return true;
}

int cajeput_grid_glue_init(int api_major, int api_minor,
			   struct simgroup_ctx *sgrp, void **priv,
			   struct cajeput_grid_hooks *hooks) {
  if(api_major != CAJEPUT_API_VERSION_MAJOR || 
     api_minor < CAJEPUT_API_VERSION_MINOR) 
    return false;

  struct standalone_grid_ctx *grid = new standalone_grid_ctx;
  grid->sgrp = sgrp; grid->log = caj_get_logger(sgrp);
  *priv = grid;

  if(!init_db(grid)) {
    delete grid; return false;
  }

  CAJ_PRINT("Loading initial assets to database...\n");
  if(!load_initial_assets(grid)) {
    CAJ_ERROR("ERROR: can't load initial assets\n");
    sqlite3_close(grid->db);
    delete grid; return false;
  }
  CAJ_PRINT("Initial assets loaded.\n");

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

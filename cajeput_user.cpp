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

#define DEBUG_CHANNEL 2147483647

#include "cajeput_core.h"
#include "cajeput_int.h"
#include "cajeput_anims.h"
#include "cajeput_grid_glue.h"
#include "caj_helpers.h"
#include <cassert>
#include <stdio.h>

const char *sl_throttle_names[] = { "resend","land","wind","cloud","task","texture","asset" };
const char *sl_wearable_names[] = {"body","skin","hair","eyes","shirt",
					  "pants","shoes","socks","jacket",
					  "gloves","undershirt","underpants",
					  "skirt"};

// possible states of sys_folders requests:
#define SYS_FOLDERS_NOT_REQUESTED 0
#define SYS_FOLDERS_PENDING 1
#define SYS_FOLDERS_LOADED 2
#define SYS_FOLDERS_IN_CALLBACK 3
#define SYS_FOLDERS_BAD_STATE 4 

void user_reset_timeout(struct user_ctx* ctx) {
  ctx->last_activity = g_timer_elapsed(ctx->sgrp->timer, NULL);
}


void caj_uuid_to_name(struct simgroup_ctx *sgrp, uuid_t id, 
		      void(*cb)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
		      void *cb_priv) {
  sgrp->gridh.uuid_to_name(sgrp, id, cb, cb_priv);
}

user_ctx *user_find_session(struct simulator_ctx *sim, uuid_t agent_id,
			    uuid_t session_id) {
  for(user_ctx *ctx = sim->ctxts; ctx != NULL; ctx = ctx->next) {
    if(uuid_compare(ctx->user_id, agent_id) == 0 &&
       uuid_compare(ctx->session_id, session_id) == 0) {
      return ctx;
    }
  }
  return NULL;
}

user_ctx *user_find_ctx(struct simulator_ctx *sim, uuid_t agent_id) {
  for(user_ctx *ctx = sim->ctxts; ctx != NULL; ctx = ctx->next) {
    if(uuid_compare(ctx->user_id, agent_id) == 0) {
      return ctx;
    }
  }
  return NULL;
}

void *user_get_grid_priv(struct user_ctx *user) {
  return user->grid_priv;
}

struct simulator_ctx* user_get_sim(struct user_ctx *user) {
  return user->sim;
}

void user_get_uuid(struct user_ctx *user, uuid_t u) {
  uuid_copy(u, user->user_id);
}

void user_get_session_id(struct user_ctx *user, uuid_t u) {
  uuid_copy(u, user->session_id);
}

void user_get_secure_session_id(struct user_ctx *user, uuid_t u) {
  uuid_copy(u, user->secure_session_id);
}

uint32_t user_get_circuit_code(struct user_ctx *user) {
  return user->circuit_code;
}

const char* user_get_first_name(struct user_ctx *user) {
  return user->first_name;
}

const char* user_get_last_name(struct user_ctx *user) {
  return user->last_name;
}

const char* user_get_name(struct user_ctx *user) {
  return user->name;
}

const caj_string* user_get_texture_entry(struct user_ctx *user) {
  return &user->texture_entry;
}

const caj_string* user_get_visual_params(struct user_ctx *user) {
  return &user->visual_params;
}

const wearable_desc* user_get_wearables(struct user_ctx* user) {
  return user->wearables;
}

void user_get_position(struct user_ctx* user, caj_vector3 *pos) {
  if(user->av != NULL) {
    *pos = user->av->ob.world_pos;
  } else {
    pos->x = 0.0f; pos->y = 0.0f; pos->z = 0.0f;
  }
}

float user_get_draw_dist(struct user_ctx *user) {
  return user->draw_dist;
}

uint32_t user_get_flags(struct user_ctx *user) {
  return user->flags;
}
void user_set_flag(struct user_ctx *user, uint32_t flag) {
  user->flags |= flag;
}
void user_clear_flag(struct user_ctx *user, uint32_t flag) {
  user->flags &= ~flag;
}

void user_add_self_pointer(struct user_ctx** pctx) {
  (*pctx)->self_ptrs.insert(pctx);
}

void user_del_self_pointer(struct user_ctx** pctx) {
  (*pctx)->self_ptrs.erase(pctx);
  *pctx = NULL;
}


void user_set_texture_entry(struct user_ctx *user, struct caj_string* data) {
  caj_string_free(&user->texture_entry);
  user->texture_entry = *data;
  data->data = NULL;
  user->flags |= AGENT_FLAG_APPEARANCE_UPD; // FIXME - send full update instead?
}

void user_set_visual_params(struct user_ctx *user, struct caj_string* data) {
  caj_string_free(&user->visual_params);
  user->visual_params = *data;
  data->data = NULL;
  user->flags |= AGENT_FLAG_APPEARANCE_UPD;
}

void user_set_wearable_serial(struct user_ctx *ctx, uint32_t serial) {
  ctx->wearable_serial = serial;
}

void user_set_wearable(struct user_ctx *ctx, int id,
		       uuid_t item_id, uuid_t asset_id) {
  if(id >= SL_NUM_WEARABLES) {
    printf("ERROR: user_set_wearable bad id %i\n",id);
    return;
  }
  uuid_copy(ctx->wearables[id].item_id, item_id);
  uuid_copy(ctx->wearables[id].asset_id, asset_id);
}

void user_set_throttles(struct user_ctx *ctx, float rates[]) {
  double time_now = g_timer_elapsed(ctx->sgrp->timer, NULL);
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    ctx->throttles[i].time = time_now;
    ctx->throttles[i].level = 0.0f;
    ctx->throttles[i].rate = rates[i] / 8.0f;
    
  }
}

void user_set_throttles_block(struct user_ctx* ctx, unsigned char* data,
			      int len) {
  float throttles[SL_NUM_THROTTLES];

  if(len < SL_NUM_THROTTLES*4) {
    printf("Error: AgentThrottle with not enough data\n");
    return;
  }

  printf("DEBUG: got new throttles:\n");
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    throttles[i] =  caj_bin_to_float_le(data + 4*i);
    printf("  throttle %s: %f\n", sl_throttle_names[i], throttles[i]);
    user_set_throttles(ctx, throttles);
  }
}

void user_get_throttles_block(struct user_ctx* ctx, unsigned char* data,
			      int len) {
  if(len < SL_NUM_THROTTLES*4) {
    printf("Error: AgentThrottle with not enough data\n");
    return;
  } else {
    len =  SL_NUM_THROTTLES*4;
  }

  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    caj_float_to_bin_le(data + i*4, ctx->throttles[i].rate * 8.0f);
  }
}

void user_reset_throttles(struct user_ctx *ctx) {
  double time_now = g_timer_elapsed(ctx->sgrp->timer, NULL);
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    ctx->throttles[i].time = time_now;
    ctx->throttles[i].level = 0.0f;
  }
}

void user_update_throttles(struct user_ctx *ctx) {
  double time_now = g_timer_elapsed(ctx->sgrp->timer, NULL);
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    assert(time_now >=  ctx->throttles[i].time); // need monotonic time
    ctx->throttles[i].level += ctx->throttles[i].rate * 
      (time_now - ctx->throttles[i].time);

    if(ctx->throttles[i].level > ctx->throttles[i].rate * 0.3f) {
      // limit maximum reservoir level to 0.3 sec of data
      ctx->throttles[i].level = ctx->throttles[i].rate * 0.3f;
    }
    ctx->throttles[i].time = time_now;
  }  
}

// FIXME - move this somewhere saner!
static void set_default_anim(struct user_ctx* ctx, uuid_t anim) {
  if(uuid_compare(ctx->default_anim.anim, anim) != 0) {
    uuid_copy(ctx->default_anim.anim, anim);
    ctx->default_anim.sequence = ctx->anim_seq++; // FIXME
    ctx->default_anim.caj_type = CAJ_ANIM_TYPE_DEFAULT;
    uuid_copy(ctx->default_anim.obj, ctx->user_id);
    ctx->flags |= AGENT_FLAG_ANIM_UPDATE;
  }
}

// FIXME - move these to a header.
#define AGENT_CONTROL_AT_POS (1<<0)
#define AGENT_CONTROL_AT_NEG (1<<1)
#define AGENT_CONTROL_LEFT_POS (1<<2)
#define AGENT_CONTROL_LEFT_NEG (1<<3)
#define AGENT_CONTROL_UP_POS (1<<4)
#define AGENT_CONTROL_UP_NEG (1<<5)
#define AGENT_CONTROL_FLY (1<<13)

void user_set_control_flags(struct user_ctx *ctx, uint32_t control_flags) {
  if(ctx->av != NULL) {
    int is_flying = (control_flags & AGENT_CONTROL_FLY) != 0;
    int is_running = (ctx->flags & AGENT_FLAG_ALWAYS_RUN) != 0;
    caj_vector3 velocity; 
    velocity.x = 0.0f; velocity.y = 0.0f; velocity.z = 0.0f;
    if(control_flags & AGENT_CONTROL_AT_POS)
      velocity.x = is_flying ? 6.0 : (is_running ? 4.0 : 2.0);
    if(control_flags & AGENT_CONTROL_AT_NEG)
      velocity.x =  is_flying ? 4.0 : -1.5;
    if(control_flags & AGENT_CONTROL_LEFT_POS)
      velocity.y = 1.5;
    if(control_flags & AGENT_CONTROL_LEFT_NEG)
      velocity.y = -1.5;
    if(control_flags & AGENT_CONTROL_UP_POS)
      velocity.z = 4.0;
    if(control_flags & AGENT_CONTROL_UP_NEG)
      velocity.z = -4.0;
    caj_mult_vect3_quat(&velocity,&ctx->av->ob.rot,&velocity);
    ctx->sim->physh.set_avatar_flying(ctx->sim,ctx->sim->phys_priv,&ctx->av->ob,is_flying);
    ctx->sim->physh.set_target_velocity(ctx->sim,ctx->sim->phys_priv,&ctx->av->ob,velocity);

    // FIXME - iffy
    if(is_flying) {
      // we don't have any auto-land code here. Since we send the right
      // foot plane, the viewer takes care of it for us. I eventually managed
      // to figure out why the OpenSim code works, though, and it's screwy.

      if(control_flags & (AGENT_CONTROL_AT_POS|AGENT_CONTROL_AT_NEG)) {
	set_default_anim(ctx, fly_anim);
      } else if(control_flags & AGENT_CONTROL_UP_POS) {
	set_default_anim(ctx, hover_up_anim);
      } else if(control_flags & AGENT_CONTROL_UP_NEG) {
	set_default_anim(ctx, hover_down_anim);
      } else {
	set_default_anim(ctx, hover_anim);
      }
    } else if(is_running && (control_flags & AGENT_CONTROL_AT_POS)) {
 	set_default_anim(ctx, run_anim);
    } else {
      if(control_flags & (AGENT_CONTROL_AT_POS|AGENT_CONTROL_AT_NEG|
			  AGENT_CONTROL_LEFT_POS|AGENT_CONTROL_LEFT_NEG)) {
	set_default_anim(ctx, walk_anim);
      } else {
	set_default_anim(ctx, stand_anim);
      }
    }
  }  
}

// FIXME - optimise this
void user_add_animation(struct user_ctx *ctx, struct animation_desc* anim,
			int replace) {
  if(replace) {
    // FIXME - is the replace functionality actually useful?
    int found = 0;
    for(std::vector<animation_desc>::iterator iter = ctx->anims.begin();
	iter != ctx->anims.end(); /* nothing */) {
      if(uuid_compare(iter->anim, anim->anim) == 0 || 
	 iter->caj_type == anim->caj_type) {
	if(found) {
	  ctx->flags |= AGENT_FLAG_ANIM_UPDATE;
	  iter = ctx->anims.erase(iter);
	  continue;
	} else if(uuid_compare(iter->anim, anim->anim) == 0 && 
	 iter->caj_type == anim->caj_type) {
	  found = 1; /* do nothing - FIXME update other stuff */
	} else {
	  ctx->flags |= AGENT_FLAG_ANIM_UPDATE;
	  *iter = *anim; found = 1;
	}
      }
      iter++;
    }    
  } else {
    for(std::vector<animation_desc>::iterator iter = ctx->anims.begin();
	iter != ctx->anims.end(); iter++) {
      if(uuid_compare(iter->anim, anim->anim) == 0) {
	iter->caj_type = anim->caj_type;
	return; // FIXME - update other stuff
      }
    }
    ctx->anims.push_back(*anim);
    ctx->flags |= AGENT_FLAG_ANIM_UPDATE;
  }
}

void user_clear_animation_by_type(struct user_ctx *ctx, int caj_type) {
  for(std::vector<animation_desc>::iterator iter = ctx->anims.begin();
      iter != ctx->anims.end(); /* nothing */) {
    if(iter->caj_type == caj_type) {
      ctx->flags |= AGENT_FLAG_ANIM_UPDATE;
      iter = ctx->anims.erase(iter);
    } else {
      iter++;
    }
  }
}

void user_clear_animation_by_id(struct user_ctx *ctx, uuid_t anim) {
  for(std::vector<animation_desc>::iterator iter = ctx->anims.begin();
      iter != ctx->anims.end(); /* nothing */) {
    if(uuid_compare(iter->anim, anim) == 0) {
      ctx->flags |= AGENT_FLAG_ANIM_UPDATE;
      iter = ctx->anims.erase(iter);
    } else {
      iter++;
    }
  } 
}

void user_av_chat_callback(struct simulator_ctx *sim, struct world_obj *obj,
			   const struct chat_message *msg, void *user_data) {
  struct user_ctx* ctx = (user_ctx*)user_data;
  if(ctx->userh != NULL && ctx->userh->chat_callback != NULL)
    ctx->userh->chat_callback(ctx->user_priv, msg);
}


void user_send_message(struct user_ctx *ctx, const char* msg) {
  struct chat_message chat;
  chat.source_type = CHAT_SOURCE_SYSTEM;
  chat.chat_type = CHAT_TYPE_NORMAL;
  uuid_clear(chat.source); // FIXME - set this to something?
  uuid_clear(chat.owner);
  chat.name = (char*)"Cajeput";
  chat.msg = (char*)msg;

  // slightly evil hack
  user_av_chat_callback(ctx->sim, NULL, &chat, ctx);
}

void user_fetch_inventory_folder(simgroup_ctx *sgrp, user_ctx *user, 
				 uuid_t folder_id, uuid_t owner_id,
				  void(*cb)(struct inventory_contents* inv, 
					    void* priv),
				  void *cb_priv) {
  if(uuid_compare(owner_id, user->user_id) == 0) {
    sgrp->gridh.fetch_inventory_folder(sgrp,user,user->grid_priv,
				       folder_id,cb,cb_priv);
  } else {
    std::map<obj_uuid_t,inventory_contents*>::iterator iter = 
      sgrp->inv_lib.find(folder_id);
    if(iter == sgrp->inv_lib.end()) {
      cb(NULL, cb_priv);
    } else {
      cb(iter->second, cb_priv);
    }
  }
}

void user_fetch_inventory_item(user_ctx *user, uuid_t item_id, uuid_t owner_id,
			       void(*cb)(struct inventory_item* item, 
					 void* priv),
			       void *cb_priv) {
  if(uuid_compare(owner_id, user->user_id) == 0) {
    user->sgrp->gridh.fetch_inventory_item(user->sgrp,user,user->grid_priv,
					   item_id,cb,cb_priv);
  } else {
#if 0 // FIXME - handle library right!
    std::map<obj_uuid_t,inventory_contents*>::iterator iter = 
      sim->inv_lib.find(folder_id);
    if(iter == sim->inv_lib.end()) {
      sim->gridh.fetch_inventory_folder(sim,user,user->grid_priv,
					folder_id,cb,cb_priv);
    } else {
      cb(iter->second, cb_priv);
    }
#endif
    cb(NULL, cb_priv);
  }
}

void user_add_inventory_item(user_ctx *ctx, struct inventory_item* item,
			     void(*cb)(void* priv, int success, uuid_t item_id), 
			     void *cb_priv) {
  uuid_generate(item->item_id);
  uuid_copy(item->owner_id, ctx->user_id);
  ctx->sgrp->gridh.add_inventory_item(ctx->sgrp, ctx, ctx->grid_priv,
				      item, cb, cb_priv);
}

void user_set_system_folders(struct user_ctx *ctx, 
			     struct inventory_contents* inv) {
  if(ctx->sys_folders != NULL) 
    caj_inv_free_contents_desc(ctx->sys_folders);

  ctx->sys_folders = inv;
  ctx->sys_folders_state = SYS_FOLDERS_IN_CALLBACK;

  for(caj_callback<user_generic_cb>::cb_set::iterator iter = 
	ctx->sys_folders_cbs.callbacks.begin(); 
      iter != ctx->sys_folders_cbs.callbacks.end(); iter++) {
    iter->cb(ctx, iter->priv);
  }
  ctx->sys_folders_cbs.callbacks.clear();
  ctx->sys_folders_state = SYS_FOLDERS_LOADED;
}

struct inventory_folder* user_find_system_folder(user_ctx *ctx,
						 int8_t asset_type) {
  assert(ctx->sys_folders_state == SYS_FOLDERS_IN_CALLBACK);

  if(ctx->sys_folders == NULL) return NULL;

  for(unsigned int i = 0; i < ctx->sys_folders->num_subfolder; i++) {
    if(ctx->sys_folders->subfolders[i].asset_type == asset_type)
      return &ctx->sys_folders->subfolders[i];
  }
  return NULL;
}

// don't rely on the cb == NULL behaviour; it may change in future.
void user_fetch_system_folders(user_ctx *ctx, user_generic_cb cb,
			       void *cb_priv) {
  if(ctx->sys_folders_state == SYS_FOLDERS_NOT_REQUESTED) {
    ctx->sys_folders_state = SYS_FOLDERS_PENDING;
    ctx->sgrp->gridh.fetch_system_folders(ctx->sgrp, ctx, ctx->grid_priv);
  }

  if(ctx->sys_folders_state == SYS_FOLDERS_LOADED) {
    ctx->sys_folders_state = SYS_FOLDERS_IN_CALLBACK;
    if(cb != NULL) cb(ctx, cb_priv);
    ctx->sys_folders_state = SYS_FOLDERS_LOADED;
  } else if(ctx->sys_folders_state == SYS_FOLDERS_IN_CALLBACK) {
    // this is permitted, so I suppose we should handle it...
    if(cb != NULL) cb(ctx, cb_priv);
  } else {
    assert(ctx->sys_folders_state == SYS_FOLDERS_PENDING);
    if(cb != NULL) ctx->sys_folders_cbs.add_callback(cb, cb_priv);
  }
}

static void free_system_folders(user_ctx *ctx) {
  // this is a valid (if undocumented) use of user_set_system_folders
  user_set_system_folders(ctx, NULL);
  ctx->sys_folders_state = SYS_FOLDERS_BAD_STATE;
}

static void sanitise_teleport_pos(simulator_ctx *sim, caj_vector3 *pos) {
  if(!finite(pos->x)) pos->x = 128.0f;
  else if(pos->x <= 0.0f) pos->x = 1.0f;
  else if(pos->x >= WORLD_REGION_SIZE) pos->x = WORLD_REGION_SIZE - 1.0f;

  if(!finite(pos->y)) pos->y = 128.0f;
  else if(pos->y <= 0.0f) pos->y = 1.0f;
  else if(pos->y >= WORLD_REGION_SIZE) pos->y = WORLD_REGION_SIZE - 1.0f;
  
  if(!finite(pos->z)) pos->z = 1.0f;
  else if(pos->z <= 0.0f) pos->z = 1.0f;
  else if(pos->z >= WORLD_HEIGHT) pos->z = WORLD_HEIGHT - 1.0f;  

  float min_height = 1.0f + sim_get_terrain_height(sim, pos->x, pos->y);
  if(pos->z < min_height) pos->z = min_height;
}

static teleport_desc* begin_teleport(struct user_ctx* ctx) {
  if(ctx->tp_out != NULL) {
    printf("!!! ERROR: can't teleport while teleporting!\n");
    return NULL;    
  } else if(ctx->av == NULL) {
    printf("!!! ERROR: can't teleport with no body!\n");
    return NULL;    
  }
  teleport_desc* desc = new teleport_desc();
  desc->ctx = ctx;
  user_add_self_pointer(&desc->ctx);
  ctx->tp_out = desc;
  return desc;
}

static void del_teleport_desc(teleport_desc* desc) {
  if(desc->ctx != NULL) {
    assert(desc->ctx->tp_out == desc);
    desc->ctx->tp_out = NULL;
    user_del_self_pointer(&desc->ctx);
  }
  delete desc;
}

void user_teleport_failed(struct teleport_desc* tp, const char* reason) {
  if(tp->ctx != NULL && tp->ctx->userh->teleport_failed != NULL) {
    tp->ctx->userh->teleport_failed(tp->ctx, reason);
  }
  del_teleport_desc(tp);
}

// In theory, we can send arbitrary strings, but that seems to be bugged.
void user_teleport_progress(struct teleport_desc* tp, const char* msg) {
  if(tp->ctx != NULL && tp->ctx->userh->teleport_progress != NULL) 
    tp->ctx->userh->teleport_progress(tp->ctx, msg, tp->flags);
}

static void user_complete_teleport_int(struct teleport_desc* tp, int is_local) {
  if(tp->ctx != NULL) {
    printf("DEBUG: completing teleport\n");
    tp->ctx->flags |= AGENT_FLAG_TELEPORT_COMPLETE;
    // FIXME - need to check hook not NULL?
    if(is_local)
      tp->ctx->userh->teleport_local(tp->ctx, tp);
    else
      tp->ctx->userh->teleport_complete(tp->ctx, tp);
    
  }
  del_teleport_desc(tp);
}

void user_complete_teleport(struct teleport_desc* tp) {
  user_complete_teleport_int(tp, FALSE);
}

// for after region handle is resolved...
static void do_real_teleport(struct teleport_desc* tp) {
  if(tp->ctx == NULL) {
    user_teleport_failed(tp,"cancelled");
  } else if(tp->region_handle == tp->ctx->sim->region_handle) {
    if(tp->ctx->av == NULL) {
      // FIXME - not quite right?
      user_teleport_failed(tp,"Your avatar disappeared!");
      return;
    }
    // FIXME - this is horribly hacky!
    sanitise_teleport_pos(tp->ctx->sim, &tp->pos);
    struct caj_multi_upd pos_upd;
    pos_upd.flags = CAJ_MULTI_UPD_POS;
    pos_upd.pos = tp->pos;
    
    world_multi_update_obj(tp->ctx->sim, &tp->ctx->av->ob, &pos_upd);
    user_complete_teleport_int(tp, TRUE);
  } else {
    simgroup_ctx *sgrp = tp->ctx->sgrp;
    sgrp->gridh.do_teleport(sgrp,tp->ctx->sim, tp);
  }
}

void user_teleport_location(struct user_ctx* ctx, uint64_t region_handle,
			    const caj_vector3 *pos, const caj_vector3 *look_at,
			    int is_from_viewer) {
  teleport_desc* desc = begin_teleport(ctx);
  if(desc == NULL) return;
  desc->region_handle = region_handle;
  desc->pos = *pos;
  desc->look_at = *look_at;
  desc->flags = TELEPORT_TO_LOCATION;
  if(!is_from_viewer && ctx->userh->teleport_begin != NULL) {
    // FIXME - should this be unconditional? Dunno...
    ctx->userh->teleport_begin(ctx, desc);
  }
  do_real_teleport(desc);
}

void tp_region_name_cb(void* cb_priv, struct map_block_info* block) {
  teleport_desc* tp = (teleport_desc*)cb_priv;
  if(tp->ctx == NULL) {
    user_teleport_failed(tp,"cancelled");
  } else if(block == NULL) {
    user_teleport_failed(tp,"Destination region not found");
  } else {
    tp->region_handle = ((uint64_t)block->x <<40)|((uint64_t)block->y << 8);
    do_real_teleport(tp);
  }
}

void user_teleport_by_region_name(struct user_ctx* ctx, char *region_name,
				  const caj_vector3 *pos, 
				  const caj_vector3 *look_at,
				  int is_from_viewer) {
  teleport_desc* desc = begin_teleport(ctx);
  if(desc == NULL) return;
  desc->region_handle = 0;
  desc->pos = *pos;
  desc->look_at = *look_at;
  desc->flags = TELEPORT_TO_LOCATION;
  if(!is_from_viewer && ctx->userh->teleport_begin != NULL) {
    ctx->userh->teleport_begin(ctx, desc);
  }
  ctx->sgrp->gridh.map_region_by_name(ctx->sgrp, region_name, 
				      tp_region_name_cb, desc);
}

void user_teleport_landmark(struct user_ctx* ctx, uuid_t landmark) {
  teleport_desc* desc = begin_teleport(ctx);
  if(desc == NULL) return;
  // FIXME - todo
  if(uuid_is_null(landmark)) {
    desc->flags = TELEPORT_TO_HOME;
    user_teleport_failed(desc,"FIXME: teleport home not supported");
  } else {
    desc->flags = TELEPORT_TO_LOCATION;
    user_teleport_failed(desc,"FIXME: teleport to landmark not supported");
  }
}

// FIXME - HACK
void user_teleport_add_temp_child(struct user_ctx* ctx, uint64_t region,
				  uint32_t sim_ip, uint16_t sim_port,
				  const char* seed_cap) {
  // FIXME - need to move this into general EnableSimulator handling once
  // we have such a thing.
  caj_llsd *body = llsd_new_map();
  caj_llsd *sims = llsd_new_array();
  caj_llsd *info = llsd_new_map();

  llsd_map_append(info, "IP", llsd_new_binary(&sim_ip,4)); // big-endian?
  llsd_map_append(info, "Port", llsd_new_int(sim_port));
  llsd_map_append(info, "Handle", llsd_new_from_u64(region));

  llsd_array_append(sims, info);
  llsd_map_append(body,"SimulatorInfo",sims);
  user_event_queue_send(ctx,"EnableSimulator",body);
}

static void debug_prepare_new_user(struct simulator_ctx *sim,
				   struct sim_new_user *uinfo) {
  char user_id[40], session_id[40];
  uuid_unparse(uinfo->user_id,user_id);
  uuid_unparse(uinfo->session_id,session_id);
  printf("DEBUG: %s expecting new user %s %s, user_id=%s, session_id=%s, "
	 "circuit_code=%lu (%s)\n", sim->shortname,
	 uinfo->first_name, uinfo->last_name,
	 user_id, session_id, (unsigned long)uinfo->circuit_code,
	 uinfo->is_child ? "child" : "main");
}

static float throttle_init[SL_NUM_THROTTLES] = {
  64000.0, 64000.0, 64000.0, 64000.0, 64000.0, 
  64000.0, 64000.0,
};

struct user_ctx* sim_prepare_new_user(struct simulator_ctx *sim,
				      struct sim_new_user *uinfo) {
  struct user_ctx* ctx;
  ctx = new user_ctx(sim); 
  ctx->userh = NULL; ctx->user_priv = NULL;

  ctx->draw_dist = 0.0f;
  ctx->circuit_code = uinfo->circuit_code;
  ctx->tp_out = NULL;

  ctx->texture_entry.len = 0;
  ctx->texture_entry.data = NULL;
  ctx->visual_params.len = 0;
  ctx->visual_params.data = NULL;
  ctx->appearance_serial = ctx->wearable_serial = 0;
  memset(ctx->wearables, 0, sizeof(ctx->wearables));

  uuid_copy(ctx->default_anim.anim, stand_anim);
  ctx->default_anim.sequence = 1;
  uuid_clear(ctx->default_anim.obj); // FIXME - is this right?
  ctx->default_anim.caj_type = CAJ_ANIM_TYPE_DEFAULT;
  ctx->anim_seq = 2;

  debug_prepare_new_user(sim, uinfo);

  uuid_copy(ctx->user_id, uinfo->user_id);
  uuid_copy(ctx->session_id, uinfo->session_id);
  uuid_copy(ctx->secure_session_id, uinfo->secure_session_id);

  ctx->sim = sim; ctx->sgrp = sim->sgrp;
  ctx->next = sim->ctxts; sim->ctxts = ctx;
  if(uinfo->is_child) {
    ctx->flags = AGENT_FLAG_CHILD;
  } else {
    ctx->flags = AGENT_FLAG_INCOMING;
  }
  ctx->first_name = strdup(uinfo->first_name);
  ctx->last_name = strdup(uinfo->last_name);
  ctx->name = (char*)malloc(2+strlen(ctx->first_name)+strlen(ctx->last_name));
  sprintf(ctx->name, "%s %s", ctx->first_name, ctx->last_name);
  ctx->group_title = strdup(""); // strdup("Very Foolish Tester");
  user_reset_timeout(ctx);
  ctx->sys_folders = NULL;
  ctx->sys_folders_state = SYS_FOLDERS_NOT_REQUESTED;

  ctx->start_pos.x = 128.0f; ctx->start_pos.y = 128.0f; 
  ctx->start_pos.z = 60.0f; ctx->start_look_at = ctx->start_pos;

  user_set_throttles(ctx,throttle_init);

  sim->sgrp->gridh.user_created(sim->sgrp,sim,ctx,&ctx->grid_priv);

  // HACK
  world_int_init_obj_updates(ctx);

  for(int i = 0; i < 16; i++) ctx->dirty_terrain[i] = 0xffff;

  user_int_caps_init(sim, ctx, uinfo);

  return ctx;
}

user_ctx* sim_bind_user(simulator_ctx *sim, uuid_t user_id, uuid_t session_id,
			uint32_t circ_code, struct user_hooks* hooks) {
  user_ctx* ctx;
  for(ctx = sim->ctxts; ctx != NULL; ctx = ctx->next) {
    if(ctx->circuit_code == circ_code &&
       uuid_compare(ctx->user_id, user_id) == 0 &&
       uuid_compare(ctx->session_id, session_id) == 0) 
      break;
  }
  if(ctx == NULL) return NULL;
  
  if(ctx->userh != NULL && ctx->userh != hooks) {
    printf("ERROR: module tried to bind already-claimed user\n");
  }

  ctx->userh = hooks;
  return ctx;
}

void user_set_start_pos(user_ctx *ctx, const caj_vector3 *pos,
			const caj_vector3 *look_at) {
  ctx->start_pos = *pos; ctx->start_look_at = *look_at;
}

int user_complete_movement(user_ctx *ctx) {
  if(!(ctx->flags & AGENT_FLAG_INCOMING)) {
    printf("ERROR: unexpected CompleteAgentMovement for %s %s\n",
	   ctx->first_name, ctx->last_name);
    return false;
  }
  ctx->flags &= ~AGENT_FLAG_CHILD;
  ctx->flags |= AGENT_FLAG_ENTERED | AGENT_FLAG_APPEARANCE_UPD |  
    AGENT_FLAG_ANIM_UPDATE | AGENT_FLAG_AV_FULL_UPD;
  if(ctx->av == NULL) {
    ctx->av = (struct avatar_obj*)calloc(sizeof(struct avatar_obj),1);
    ctx->av->ob.type = OBJ_TYPE_AVATAR;
    sanitise_teleport_pos(ctx->sim, &ctx->start_pos);
    ctx->av->ob.local_pos = ctx->start_pos;
    ctx->av->ob.rot.x = ctx->av->ob.rot.y = ctx->av->ob.rot.z = 0.0f;
    ctx->av->ob.rot.w = 1.0f;
    ctx->av->ob.velocity.x = ctx->av->ob.velocity.y = ctx->av->ob.velocity.z = 0.0f;
    // yes, this is right, even though footfall's not a quaternion
    ctx->av->footfall.x = ctx->av->footfall.y = 0.0f;
    ctx->av->footfall.z = 0.0f; ctx->av->footfall.w = 1.0f;
    
    uuid_copy(ctx->av->ob.id, ctx->user_id);
    world_insert_obj(ctx->sim, &ctx->av->ob);
    world_obj_listen_chat(ctx->sim,&ctx->av->ob,user_av_chat_callback,ctx);
    world_obj_add_channel(ctx->sim,&ctx->av->ob,0);
    world_obj_add_channel(ctx->sim,&ctx->av->ob,DEBUG_CHANNEL);

    ctx->sgrp->gridh.user_entered(ctx->sgrp, ctx->sim, ctx, ctx->grid_priv);
    user_fetch_system_folders(ctx, NULL, NULL);
  }

  return true;
}

void user_remove_int(user_ctx **user) {
  user_ctx* ctx = *user;
  simgroup_ctx *sgrp = ctx->sgrp;
  if(ctx->av != NULL) {
    if(!(ctx->flags & (AGENT_FLAG_CHILD|AGENT_FLAG_TELEPORT_COMPLETE))) {
      // FIXME - should set look_at correctly
      sgrp->gridh.user_logoff(sgrp, ctx->sim, ctx->user_id, ctx->session_id,
			      &ctx->av->ob.world_pos, &ctx->av->ob.world_pos);
    }
    world_delete_avatar(ctx->sim, ctx->av);
    ctx->av = NULL;
  }

  printf("Removing user %s %s\n", ctx->first_name, ctx->last_name);

  // If we're logging out, sending DisableSimulator causes issues
  if(ctx->userh != NULL  && !(ctx->flags & AGENT_FLAG_IN_LOGOUT) &&
     ctx->userh->disable_sim != NULL) {
    // HACK HACK HACK - should be in main hook?
    ctx->userh->disable_sim(ctx->user_priv); // FIXME
  } 

  // probably want to do this before the delete hook
  free_system_folders(ctx);

  user_call_delete_hook(ctx);

  // mustn't do this in user_session_close; that'd break teleport
  user_int_event_queue_free(ctx);

  sgrp->gridh.user_deleted(sgrp, ctx->sim,ctx,ctx->grid_priv);

  user_int_caps_cleanup(ctx);

  if(ctx->userh != NULL) { // non-optional hook!
    ctx->userh->remove(ctx->user_priv);
  }

  caj_string_free(&ctx->texture_entry);
  caj_string_free(&ctx->visual_params);


  free(ctx->first_name);
  free(ctx->last_name);
  free(ctx->name);
  free(ctx->group_title);

  for(std::set<user_ctx**>::iterator iter = ctx->self_ptrs.begin();
      iter != ctx->self_ptrs.end(); iter++) {
    user_ctx **pctx = *iter;
    assert(*pctx == ctx);
    *pctx = NULL;
  }
  
  *user = ctx->next;
  delete ctx;
}

// FIXME - purge the texture sends here
void user_session_close(user_ctx* ctx, int slowly) {
  if(ctx->av != NULL) {
    // FIXME - code duplication
    if(!(ctx->flags & (AGENT_FLAG_CHILD|AGENT_FLAG_TELEPORT_COMPLETE))) {
      ctx->sgrp->gridh.user_logoff(ctx->sgrp, ctx->sim, ctx->user_id, 
				   ctx->session_id, &ctx->av->ob.world_pos,
				   &ctx->av->ob.world_pos);
    }
    world_delete_avatar(ctx->sim, ctx->av);
    ctx->av = NULL;
  }
  if(slowly) {
    ctx->flags |= AGENT_FLAG_IN_SLOW_REMOVAL;
    ctx->shutdown_ctr = 3; // 2-3 second delay.
  } else {
    ctx->flags |= AGENT_FLAG_PURGE;
  }
}

/* This doesn't really belong here - it's OpenSim glue-specific */
void user_logoff_user_osglue(struct simulator_ctx *sim, uuid_t agent_id, 
			     uuid_t secure_session_id) {
  for(user_ctx** user = &sim->ctxts; *user != NULL; ) {
    // Weird verification rules, but they seem to work...
    if(uuid_compare((*user)->user_id,agent_id) == 0 && 
       (secure_session_id == NULL || 
	uuid_compare((*user)->secure_session_id,secure_session_id) == 0))
      user_remove_int(user);
    else user = &(*user)->next;
  }
}

void caj_get_user_profile(struct simgroup_ctx *sgrp,  uuid_t id, 
			  caj_user_profile_cb cb, void *cb_priv) {
  sgrp->gridh.user_profile_by_id(sgrp, id, cb, cb_priv);
}

struct rez_object_desc {
  user_ctx* ctx;
  uuid_t inv_item_id;
  caj_vector3 pos;
  permission_flags inv_perms;
  uint32_t inv_flags;
  uint8_t is_attachment, attach_point;
  char *name, *description;
  
  rez_object_desc() : name(NULL), description(NULL) { }
  ~rez_object_desc() {
    free(name); free(description);
  }
};

#include "opensim_xml_glue.h"

struct os_object_shape {
  int profile_curve;
  caj_string tex_entry, extra_params;
  int path_begin, path_curve, path_end;
  int path_radius_offset, path_revolutions, path_scale_x, path_scale_y;
  int path_shear_x, path_shear_y, path_skew, path_taper_x, path_taper_y;
  int path_twist, path_twist_begin, pcode;
  int profile_begin, profile_end, profile_hollow;
  caj_vector3 scale;
  int state;
  
  // duplicate the data in  profile_curve
  char *profile_shape, *hollow_shape;

  // sculpt, flexi and light are fully described by the extra_params
  uuid_t sculpt_texture; int sculpt_type;
  // ??? sculpt_data;
  int sculpt_entry; // boolean

  int flexi_softness;
  float flexi_tension, flexi_drag, flexi_gravity, flexi_wind;
  float flexi_force_x, flexi_force_y, flexi_force_z;
  int flexi_entry; // boolean

  float light_color_r, light_color_g, light_color_b, light_color_a;
  float light_radius, light_cutoff, light_falloff, light_intensity;
  int light_entry; // boolean
};

struct os_object_part {
  int allow_drop; // bool
  uuid_t creator_id, folder_id;
  int inv_serial;
  inventory_item **inv; int inv_count;
  int object_flags; // actually should be uint32_t
  uuid_t id;
  int local_id; // actually should be uint32_t
  char *name;
  int material;
  int pass_touches; // boolean;
  // uint64_t region_handle;
  int script_pin;
  caj_vector3 group_pos, offset_pos;
  caj_quat rot_offset;
  caj_vector3 velocity, rot_velocity, angular_vel;
  caj_vector3 accel;
  char *description;
  // ?? colour
  char *text, *sit_name, *touch_name;
  int link_num, click_action;
  struct os_object_shape shape;
  caj_vector3 scale; // yes, as well as shape.scale!
  int update_flag; // not convinced this is interesting to us
  caj_quat sit_target_rot; caj_vector3 sit_target_pos;
  caj_quat sit_target_rot_ll; caj_vector3 sit_target_pos_ll;
  int parent_id; // should be uint32_t; uninteresting.
  int creation_date; // should be uint32_t or long, I think.
  int category, sale_price, sale_type, ownership_cost;
  uuid_t group_id, owner_id, last_owner_id;
  int base_perms, current_perms, group_perms;
  int everyone_perms, next_perms;
};



static xml_serialisation_desc deserialise_vect3[] = {
  { "X", XML_STYPE_FLOAT, offsetof(caj_vector3, x) },
  { "Y", XML_STYPE_FLOAT, offsetof(caj_vector3, y) },
  { "Z", XML_STYPE_FLOAT, offsetof(caj_vector3, z) },
  { NULL }
};

static xml_serialisation_desc deserialise_quat[] = {
  { "X", XML_STYPE_FLOAT, offsetof(caj_quat, x) },
  { "Y", XML_STYPE_FLOAT, offsetof(caj_quat, y) },
  { "Z", XML_STYPE_FLOAT, offsetof(caj_quat, z) },
  { "W", XML_STYPE_FLOAT, offsetof(caj_quat, w) },
  { NULL }
};

static xml_serialisation_desc deserialise_object_shape[] = {
  { "ProfileCurve", XML_STYPE_INT, offsetof(os_object_shape, profile_curve) },
  { "TextureEntry", XML_STYPE_BASE64, offsetof(os_object_shape, tex_entry) },
  { "ExtraParams", XML_STYPE_BASE64, offsetof(os_object_shape, extra_params) },
  { "PathBegin", XML_STYPE_INT, offsetof(os_object_shape, path_begin) },
  { "PathCurve", XML_STYPE_INT, offsetof(os_object_shape, path_curve) },
  { "PathEnd", XML_STYPE_INT, offsetof(os_object_shape, path_end) },
  { "PathRadiusOffset", XML_STYPE_INT, offsetof(os_object_shape, path_radius_offset) },
  { "PathRevolutions", XML_STYPE_INT, offsetof(os_object_shape, path_revolutions) },
  { "PathScaleX", XML_STYPE_INT, offsetof(os_object_shape, path_scale_x) },
  { "PathScaleY", XML_STYPE_INT, offsetof(os_object_shape, path_scale_y) },
  { "PathShearX", XML_STYPE_INT, offsetof(os_object_shape, path_shear_x) },
  { "PathShearY", XML_STYPE_INT, offsetof(os_object_shape, path_shear_y) },
  { "PathSkew", XML_STYPE_INT, offsetof(os_object_shape, path_skew) },
  { "PathTaperX", XML_STYPE_INT, offsetof(os_object_shape, path_taper_x) },
  { "PathTaperY", XML_STYPE_INT, offsetof(os_object_shape, path_taper_y) },
  { "PathTwist", XML_STYPE_INT, offsetof(os_object_shape, path_twist) },
  { "PathTwistBegin", XML_STYPE_INT, offsetof(os_object_shape, path_twist_begin) },
  { "PCode", XML_STYPE_INT, offsetof(os_object_shape, pcode) },
  { "ProfileBegin", XML_STYPE_INT, offsetof(os_object_shape, profile_begin) },
  { "ProfileEnd", XML_STYPE_INT, offsetof(os_object_shape, profile_end) },
  { "ProfileHollow", XML_STYPE_INT, offsetof(os_object_shape, profile_hollow) },
  { "Scale", XML_STYPE_STRUCT, offsetof(os_object_shape, scale), deserialise_vect3 },
  { "State", XML_STYPE_INT, offsetof(os_object_shape, state) },
  { "ProfileShape", XML_STYPE_STRING, offsetof(os_object_shape, profile_shape) },
  { "HollowShape", XML_STYPE_STRING, offsetof(os_object_shape, hollow_shape) },
  { "SculptTexture", XML_STYPE_UUID, offsetof(os_object_shape, sculpt_texture) },
  { "SculptType", XML_STYPE_INT, offsetof(os_object_shape, sculpt_type) },
  { "SculptData", XML_STYPE_SKIP, 0 }, // FIXME!!! (not sure of format yet)

  { "FlexiSoftness", XML_STYPE_INT, offsetof(os_object_shape, flexi_softness) },
  { "FlexiTension", XML_STYPE_FLOAT, offsetof(os_object_shape, flexi_tension) },
  { "FlexiDrag", XML_STYPE_FLOAT, offsetof(os_object_shape, flexi_drag) },
  { "FlexiGravity", XML_STYPE_FLOAT, offsetof(os_object_shape, flexi_gravity) },
  { "FlexiWind", XML_STYPE_FLOAT, offsetof(os_object_shape, flexi_wind) },
  { "FlexiForceX", XML_STYPE_FLOAT, offsetof(os_object_shape, flexi_force_x) },
  { "FlexiForceY", XML_STYPE_FLOAT, offsetof(os_object_shape, flexi_force_y) },
  { "FlexiForceZ", XML_STYPE_FLOAT, offsetof(os_object_shape, flexi_force_z) },

  { "LightColorR", XML_STYPE_FLOAT, offsetof(os_object_shape, light_color_r) },
  { "LightColorG", XML_STYPE_FLOAT, offsetof(os_object_shape, light_color_g) },
  { "LightColorB", XML_STYPE_FLOAT, offsetof(os_object_shape, light_color_b) },
  { "LightColorA", XML_STYPE_FLOAT, offsetof(os_object_shape, light_color_a) },
  { "LightRadius", XML_STYPE_FLOAT, offsetof(os_object_shape, light_radius) },
  { "LightCutoff", XML_STYPE_FLOAT, offsetof(os_object_shape, light_cutoff) },
  { "LightFalloff", XML_STYPE_FLOAT, offsetof(os_object_shape, light_falloff) },
  { "LightIntensity", XML_STYPE_FLOAT, offsetof(os_object_shape, light_intensity) },

  { "FlexiEntry", XML_STYPE_BOOL, offsetof(os_object_shape, flexi_entry) },
  { "LightEntry", XML_STYPE_BOOL, offsetof(os_object_shape, light_entry) },
  { "SculptEntry", XML_STYPE_BOOL, offsetof(os_object_shape, sculpt_entry) },
  
  { NULL }
};

/* ...<Scale><X>0.5</X><Y>0.5</Y><Z>0.5</Z></Scale><UpdateFlag>0</UpdateFlag><SitTargetOrientation><X>0</X><Y>0</Y><Z>0</Z><W>1</W></SitTargetOrientation><SitTargetPosition><X>0</X><Y>0</Y><Z>0</Z></SitTargetPosition><SitTargetPositionLL><X>0</X><Y>0</Y><Z>0</Z></SitTargetPositionLL><SitTargetOrientationLL><X>0</X><Y>0</Y><Z>0</Z><W>1</W></SitTargetOrientationLL><ParentID>0</ParentID><CreationDate>1250356050</CreationDate><Category>0</Category><SalePrice>0</SalePrice><ObjectSaleType>0</ObjectSaleType><OwnershipCost>0</OwnershipCost><GroupID><Guid>00000000-0000-0000-0000-000000000000</Guid></GroupID><OwnerID><Guid>cc358b45-99a0-46d7-b643-4a8038901f74</Guid></OwnerID><LastOwnerID><Guid>cc358b45-99a0-46d7-b643-4a8038901f74</Guid></LastOwnerID><BaseMask>2147483647</BaseMask><OwnerMask>2147483647</OwnerMask><GroupMask>0</GroupMask><EveryoneMask>0</EveryoneMask><NextOwnerMask>2147483647</NextOwnerMask><Flags>None</Flags><CollisionSound><Guid>00000000-0000-0000-0000-000000000000</Guid></CollisionSound><CollisionSoundVolume>0</CollisionSoundVolume></SceneObjectPart></RootPart><OtherParts /></SceneObjectGroup> */

struct os_task_inv_item {
  char *name, *description;
  uuid_t item_id, old_item_id, asset_id, creator_id, group_id;
  uuid_t owner_id, last_owner_id, parent_id, part_id;
  int base_perms, current_perms, group_perms;
  int everyone_perms, next_perms;
  int creation_date, flags;
  int inv_type, asset_type;
  uuid_t perms_granter; int perms_mask; // ???
};

static xml_serialisation_desc deserialise_task_inv_item[] = {
  { "AssetID", XML_STYPE_UUID, offsetof(os_task_inv_item, asset_id) },
  { "BasePermissions", XML_STYPE_INT, offsetof(os_task_inv_item, base_perms) },
  { "CreationDate", XML_STYPE_INT, offsetof(os_task_inv_item, creation_date) },
  { "CreatorID", XML_STYPE_UUID, offsetof(os_task_inv_item, creator_id) },
  { "Description", XML_STYPE_STRING, offsetof(os_task_inv_item, description) },  
  { "EveryonePermissions", XML_STYPE_INT, offsetof(os_task_inv_item, everyone_perms) },
  { "Flags", XML_STYPE_INT, offsetof(os_task_inv_item, flags) },
  { "GroupID", XML_STYPE_UUID, offsetof(os_task_inv_item, group_id) },
  { "GroupPermissions", XML_STYPE_INT, offsetof(os_task_inv_item, group_perms) },
  { "InvType", XML_STYPE_INT, offsetof(os_task_inv_item, inv_type) },
  { "ItemID", XML_STYPE_UUID, offsetof(os_task_inv_item, item_id) },
  { "OldItemID", XML_STYPE_UUID, offsetof(os_task_inv_item, old_item_id) },
  { "LastOwnerID", XML_STYPE_UUID, offsetof(os_task_inv_item, last_owner_id) },
  { "Name", XML_STYPE_STRING, offsetof(os_task_inv_item, name) },  
  { "NextPermissions", XML_STYPE_INT, offsetof(os_task_inv_item, next_perms) },
  { "OwnerID", XML_STYPE_UUID, offsetof(os_task_inv_item, owner_id) },
  { "CurrentPermissions", XML_STYPE_INT, offsetof(os_task_inv_item, current_perms) },
  { "ParentID", XML_STYPE_UUID, offsetof(os_task_inv_item, parent_id) },
  { "ParentPartID", XML_STYPE_UUID, offsetof(os_task_inv_item, part_id) },
  { "PermsGranter", XML_STYPE_UUID, offsetof(os_task_inv_item, perms_granter) },
  { "PermsMask", XML_STYPE_INT, offsetof(os_task_inv_item, perms_mask) },
  { "Type", XML_STYPE_INT, offsetof(os_task_inv_item, asset_type) },
  { NULL }
};

// FIXME - this is likely to leak memory in the failure case.
// FIXME - also need to restrict number of inventory items.
static OSGLUE_XML_CALLBACK(deserialise_prim_inv) {
  std::vector<inventory_item*> inv;
  os_object_part *part = (os_object_part*)out;
  for( ; node != NULL; node = node->next) {
    if (node->type != XML_ELEMENT_NODE) continue;
    if(strcmp((char*)node->name, "TaskInventoryItem") != 0) {
      printf("ERROR: got unexpected node %s, wanted TaskInventoryItem\n",
	     (char*)node->name);
      continue;
    }
    os_task_inv_item taskinv;
    if(osglue_deserialise_xml(doc, node->children, deserialise_task_inv_item,
			      &taskinv)) {
      printf("DEBUG: got task inventory item %s\n", taskinv.name);
      inventory_item *item = world_prim_alloc_inv_item();
      item->name = strdup(taskinv.name); 
      item->description = strdup(taskinv.description);
      item->creator_id = (char*)malloc(37); 
      uuid_unparse(taskinv.creator_id, item->creator_id);
      uuid_copy(item->creator_as_uuid, taskinv.creator_id);

      uuid_copy(item->item_id, taskinv.item_id);
      uuid_copy(item->folder_id, taskinv.parent_id);
      uuid_copy(item->owner_id, taskinv.owner_id);
      uuid_copy(item->asset_id, taskinv.asset_id);

      item->perms.base = taskinv.base_perms;
      item->perms.current = taskinv.current_perms;
      item->perms.group = taskinv.group_perms;
      item->perms.everyone = taskinv.everyone_perms;
      item->perms.next = taskinv.next_perms;

      item->inv_type = taskinv.inv_type; 
      item->asset_type = taskinv.asset_type;
      item->sale_type = 0; // FIXME - magic number!
      item->sale_price = 10;

      item->group_owned = FALSE; // FIXME - not set in task inv?
      uuid_copy(item->group_id, taskinv.group_id);
      item->flags = taskinv.flags;
      item->creation_date = taskinv.creation_date;

      inv.push_back(item);
      xmlFree(taskinv.name); xmlFree(taskinv.description);
    } else {
      printf("ERROR: couldn't deserialise prim inventory item\n");
    }
  }
  part->inv_count = inv.size();
  part->inv = new inventory_item*[inv.size()];
  for(int i = 0; i < part->inv_count; i++) {
    part->inv[i] = inv[i];
  }
  return TRUE;
}

static xml_serialisation_desc deserialise_object_part[] = {
  { "AllowedDrop", XML_STYPE_BOOL, offsetof(os_object_part, allow_drop) },
  { "CreatorID", XML_STYPE_UUID, offsetof(os_object_part, creator_id) },
  { "FolderID", XML_STYPE_UUID, offsetof(os_object_part, folder_id) },
  { "InventorySerial", XML_STYPE_INT, offsetof(os_object_part, inv_serial) },
  { "TaskInventory", XML_STYPE_CUSTOM, 0, (void*)deserialise_prim_inv },
  { "ObjectFlags", XML_STYPE_INT,  offsetof(os_object_part, object_flags) },
  { "UUID",  XML_STYPE_UUID, offsetof(os_object_part, id) },
  { "LocalId", XML_STYPE_INT, offsetof(os_object_part, local_id) },
  { "Name", XML_STYPE_STRING, offsetof(os_object_part, name) },
  { "Material", XML_STYPE_INT, offsetof(os_object_part, material) },
  { "PassTouches", XML_STYPE_BOOL, offsetof(os_object_part, pass_touches) },
  { "RegionHandle", XML_STYPE_SKIP, 0 }, // FIXME
  { "ScriptAccessPin", XML_STYPE_INT,  offsetof(os_object_part, script_pin) },
  { "GroupPosition", XML_STYPE_STRUCT, offsetof(os_object_part, group_pos), deserialise_vect3 },
  { "OffsetPosition", XML_STYPE_STRUCT, offsetof(os_object_part, offset_pos), deserialise_vect3 },
  { "RotationOffset", XML_STYPE_STRUCT, offsetof(os_object_part, rot_offset), deserialise_quat },
  { "Velocity", XML_STYPE_STRUCT, offsetof(os_object_part, velocity), deserialise_vect3 },
  { "RotationalVelocity", XML_STYPE_STRUCT, offsetof(os_object_part, rot_velocity), deserialise_vect3 },
  { "AngularVelocity", XML_STYPE_STRUCT, offsetof(os_object_part, angular_vel), deserialise_vect3 },
  { "Acceleration", XML_STYPE_STRUCT, offsetof(os_object_part, accel), deserialise_vect3 },
  { "Description", XML_STYPE_STRING, offsetof(os_object_part, description) },
  { "Color", XML_STYPE_SKIP, 0 }, // FIXME - format? Presumably for hover text.
  { "Text", XML_STYPE_STRING, offsetof(os_object_part, text) },
  { "SitName", XML_STYPE_STRING, offsetof(os_object_part, sit_name) },
  { "TouchName", XML_STYPE_STRING, offsetof(os_object_part, touch_name) },
  { "LinkNum", XML_STYPE_INT, offsetof(os_object_part, link_num) },
  { "ClickAction", XML_STYPE_INT, offsetof(os_object_part, click_action) },
  { "Shape", XML_STYPE_STRUCT, offsetof(os_object_part, shape), deserialise_object_shape },
  { "Scale", XML_STYPE_STRUCT, offsetof(os_object_part, scale), deserialise_vect3 },
  { "UpdateFlag", XML_STYPE_INT, offsetof(os_object_part, update_flag) },
  { "SitTargetOrientation", XML_STYPE_STRUCT, offsetof(os_object_part, sit_target_rot), deserialise_quat },
  { "SitTargetPosition", XML_STYPE_STRUCT, offsetof(os_object_part, sit_target_pos), deserialise_vect3 },
  { "SitTargetPositionLL", XML_STYPE_STRUCT, offsetof(os_object_part, sit_target_pos_ll), deserialise_vect3 },
  { "SitTargetOrientationLL", XML_STYPE_STRUCT, offsetof(os_object_part, sit_target_rot_ll), deserialise_quat },
  { "ParentID", XML_STYPE_INT, offsetof(os_object_part, parent_id) },
  { "CreationDate", XML_STYPE_INT, offsetof(os_object_part, creation_date) },
  { "Category", XML_STYPE_INT, offsetof(os_object_part, category) },
  { "SalePrice", XML_STYPE_INT, offsetof(os_object_part, sale_price) },
  { "ObjectSaleType", XML_STYPE_INT, offsetof(os_object_part, sale_type) },
  { "OwnershipCost", XML_STYPE_INT, offsetof(os_object_part, ownership_cost) },
  { "GroupID", XML_STYPE_UUID, offsetof(os_object_part, group_id) },
  { "OwnerID", XML_STYPE_UUID, offsetof(os_object_part, owner_id) },
  { "LastOwnerID", XML_STYPE_UUID, offsetof(os_object_part, last_owner_id) },

  { "BaseMask", XML_STYPE_INT, offsetof(os_object_part, base_perms) },
  { "OwnerMask", XML_STYPE_INT, offsetof(os_object_part, current_perms) },
  { "GroupMask", XML_STYPE_INT, offsetof(os_object_part, group_perms) },
  { "EveryoneMask", XML_STYPE_INT, offsetof(os_object_part, everyone_perms) },
  { "NextOwnerMask", XML_STYPE_INT, offsetof(os_object_part, next_perms) },
  
  { NULL }
};

// FIXME - this is starting to suffer from code duplication.
static int check_node(xmlNodePtr node, const char* name) {
  if(node == NULL) {
    printf("ERROR: missing %s node\n", name); 
    return 0;
  } else if(strcmp((char*)node->name, name) != 0) {
    printf("ERROR: unexpected node: wanted %s, got %s\n",
	   name, (char*)node->name);
    return 0;
  }
  return 1;
}

static primitive_obj* prim_from_os_xml_part(xmlDocPtr doc, xmlNodePtr node, int is_child) {
  os_object_part objpart;
  if(!check_node(node, "SceneObjectPart")) return NULL;
  if(!osglue_deserialise_xml(doc, node->children, deserialise_object_part,
			     &objpart)) return NULL;

  primitive_obj *prim = world_begin_new_prim(NULL); // FIXME - pass sim.
  uuid_copy(prim->creator, objpart.creator_id);
  prim->inv.serial = objpart.inv_serial;
  // FIXME - copy inventory.
  prim->flags = objpart.object_flags;
  // uuid_copy(prim->ob.id, objpart.id) - do we ever want to do this? Really?

  free(prim->name); prim->name = strdup(objpart.name);
  free(prim->description); prim->description = strdup(objpart.description);
  // FIXME - parse and set text colour
  free(prim->hover_text); prim->hover_text = strdup(objpart.text);
  free(prim->sit_name); prim->sit_name = strdup(objpart.sit_name);
  free(prim->touch_name); prim->touch_name = strdup(objpart.touch_name);
  
  prim->material = objpart.material;
  if(is_child) 
    prim->ob.local_pos = objpart.offset_pos;
  else prim->ob.local_pos = objpart.group_pos;
  prim->ob.rot = objpart.rot_offset; // is this right?
  prim->ob.velocity = objpart.velocity;
  // TODO - restore angular velocity and acceleration
  // should also restore click action once we support it!

  prim->profile_curve = objpart.shape.profile_curve;
  caj_string_free(&prim->tex_entry);
  caj_string_steal(&prim->tex_entry, &objpart.shape.tex_entry);
  prim_set_extra_params(prim, &objpart.shape.extra_params);
  prim->path_begin = objpart.shape.path_begin;
  prim->path_curve = objpart.shape.path_curve;
  prim->path_end = objpart.shape.path_end;
  prim->path_radius_offset = objpart.shape.path_radius_offset;
  prim->path_revolutions = objpart.shape.path_revolutions;
  prim->path_scale_x = objpart.shape.path_scale_x;
  prim->path_scale_y = objpart.shape.path_scale_y;
  prim->path_shear_x = objpart.shape.path_shear_x;
  prim->path_shear_y = objpart.shape.path_shear_y;
  prim->path_skew = objpart.shape.path_skew;
  prim->path_taper_x = objpart.shape.path_taper_x;
  prim->path_taper_y = objpart.shape.path_taper_y;
  prim->path_twist = objpart.shape.path_twist;
  prim->path_twist_begin = objpart.shape.path_twist_begin;
  // FIXME - check PCode is the expected one!
  prim->profile_begin = objpart.shape.profile_begin;
  prim->profile_end = objpart.shape.profile_end;
  prim->profile_hollow = objpart.shape.profile_hollow;
  prim->ob.scale = objpart.shape.scale;
  // FIXME - do something with objpart.shape.state?
  // handle ProfileShape and HollowShape? They duplicate ProfileCurve

  prim->perms.base = objpart.base_perms;
  prim->perms.current = objpart.current_perms;
  prim->perms.group = objpart.group_perms;
  prim->perms.everyone = objpart.everyone_perms;
  prim->perms.next = objpart.next_perms;
  
  prim->creation_date = objpart.creation_date;
  // prim->category = objpart.category;
  prim->sale_type = objpart.sale_type;
  prim->sale_price = objpart.sale_price;

  world_prim_set_inv(prim, objpart.inv, objpart.inv_count);
  prim->inv.serial = objpart.inv_serial;
  delete[] objpart.inv;

  // FIXME - TODO

  xmlFree(objpart.name); xmlFree(objpart.description); xmlFree(objpart.text); 
  xmlFree(objpart.sit_name); xmlFree(objpart.touch_name); 
  xmlFree(objpart.shape.profile_shape); xmlFree(objpart.shape.hollow_shape);
  caj_string_free(&objpart.shape.tex_entry);
  caj_string_free(&objpart.shape.extra_params);  
  return prim;
}

struct os_script_state {
  char *state;
  int is_running;
};

// HACK! this isn't actually serialised with the matching code
// by OpenSim. In particular, case of boolean vars is wrong.
static xml_serialisation_desc deserialise_script_state[] = {
  { "State", XML_STYPE_STRING, offsetof(os_script_state, state) },
  { "Running", XML_STYPE_BOOL, offsetof(os_script_state, is_running) },
  { NULL }
};

typedef std::map<obj_uuid_t,os_script_state> script_state_map;

// FIXME - this ought to skip whitespace!
static primitive_obj *prim_from_os_xml(const caj_string *data,
				       script_state_map &states) {
  xmlDocPtr doc;
  xmlNodePtr node;
  primitive_obj * prim;
  doc = xmlReadMemory((const char*)data->data, data->len, "prim.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: prim XML parse failed\n"); return NULL;
  }
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "SceneObjectGroup") != 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto free_fail;
  }
  node = node->children; 
  
  if(!check_node(node, "RootPart")) goto free_fail;
  prim = prim_from_os_xml_part(doc, node->children, FALSE);

  if(prim == NULL) goto free_fail;

  node = node->next;
  if(!check_node(node, "OtherParts")) goto free_fail;
  prim->num_children = 0;
  for(xmlNodePtr part_node = node->children; part_node != NULL; part_node = part_node->next) {
    if(!check_node(part_node, "Part")) {
      prim->num_children = 0; goto free_prim_fail;
    }
    prim->num_children++;
  }
  prim->children = (primitive_obj**)malloc(sizeof(primitive_obj*)*
					   (prim->num_children));
    
  prim->num_children = 0;
  for(xmlNodePtr part_node = node->children; part_node != NULL; part_node = part_node->next) {
    if(!check_node(part_node, "Part")) goto free_prim_fail;
    primitive_obj *child = prim_from_os_xml_part(doc, part_node->children, TRUE);
    if(child == NULL) goto free_prim_fail;
    child->ob.parent = &prim->ob;
    prim->children[prim->num_children++] = child;
  }

  node = node->next;
  if(node != NULL && strcmp((char*)node->name, "GroupScriptStates") == 0) {
    printf("DEBUG: got script states info\n");
    for(xmlNodePtr st_node = node->children; st_node != NULL; 
	st_node = st_node->next) {
      if(strcmp((char*)st_node->name, "SavedScriptState") != 0)
	continue;

      uuid_t item_id; os_script_state state;
      char *for_uuid = (char*)xmlGetProp(st_node, (const xmlChar*)"UUID");
      if(for_uuid == NULL) continue;
      if(uuid_parse(for_uuid, item_id)) { xmlFree(for_uuid); continue; }
      printf("DEBUG: got script state for %s\n", for_uuid);
      xmlFree(for_uuid);

      xmlNodePtr st_node2 = st_node->children;
      if(st_node2 == NULL || strcmp((char*)st_node2->name, "ScriptState") != 0)
	continue;

      if(states.count(item_id)) continue;

      if(!osglue_deserialise_xml(doc, st_node2->children, 
				 deserialise_script_state, &state)) {
	printf("ERROR: couldn't deserialise OpenSim script state!\n");
	continue;
      }
      
      states[item_id] = state;
    }
  }

  xmlFreeDoc(doc); return prim;
  
 free_prim_fail: 
  for(int i = 0; i < prim->num_children; i++) 
    world_free_prim(prim->children[i]);
  world_free_prim(prim);
 free_fail:
  xmlFreeDoc(doc); return NULL;
}

// yes, this is messy enough that it gets its own function...
// and it's still not quite correct. FIXME!
// In particular, handling of linksets isn't sane!
static void rez_object_set_perms(primitive_obj *prim,
				 rez_object_desc *desc) {
  // OK, first we do permission propagation on owner change.
  // We don't want to do this after applying the inventory permissions,
  // because the new owner may have edited the next owner perms.
  if(uuid_compare(prim->owner, desc->ctx->user_id) != 0) {
    prim->perms.base &= prim->perms.next;
  }
  uuid_copy(prim->owner, desc->ctx->user_id);

  // Now we apply the inventory permissions, based on the flags set.
  // Base perms are ANDed, the rest are just plain set
  if(desc->inv_flags & (INV_OBJECT_SET_PERMS|INV_OBJECT_SET_BASE_PERMS))
    prim->perms.base &= desc->inv_perms.base;
  
  if(desc->inv_flags & (INV_OBJECT_SET_PERMS|INV_OBJECT_SET_CURRENT_PERMS))
    prim->perms.current = desc->inv_perms.current;
  
  if(desc->inv_flags & (INV_OBJECT_SET_PERMS|INV_OBJECT_SET_NEXT_PERMS))
    prim->perms.next = desc->inv_perms.next;

  if(desc->inv_flags & (INV_OBJECT_SET_PERMS|INV_OBJECT_SET_GROUP_PERMS))
    prim->perms.group = desc->inv_perms.group;

  if(desc->inv_flags & (INV_OBJECT_SET_PERMS|INV_OBJECT_SET_EVERYONE_PERMS))
    prim->perms.everyone = desc->inv_perms.everyone;

  // Finally, apply the new base perms
  caj_sanitise_perms(&prim->perms);

  // and now we do the same with the child prims. FIXME - not quite right.
  for(int i = 0; i < prim->num_children; i++)
    rez_object_set_perms(prim->children[i], desc);

  // FIXME - TODO handle inventory!

  // Sane handling of linksets is hard. We don't want users to be able to
  // screw permissions up by linking objects, but we also need them to be able
  // to set perms sanely for the entire linkset by editing the root. The client
  // doesn't even permit editing of child permissions.
}

// FIXME: we need to renumber inventory items, and then this won't work right
// anymore! (Otherwise, we'll get issues linking identical objects.)
static void rez_object_enable_scripts(simulator_ctx *sim, primitive_obj *prim,
				      script_state_map &states) {
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *item = prim->inv.items[i];
    if(item->inv_type != INV_TYPE_LSL) continue;

    script_state_map::iterator iter = states.find(item->item_id);
    if(iter == states.end()) {
      char buf[40]; uuid_unparse(item->item_id, buf);
      printf("WARNING: missing script state for %s\n", buf);
      continue;
    }

    if(iter->second.is_running) {
      world_prim_start_rezzed_script(sim, prim, item);
    }
  }
  for(int i = 0; i < prim->num_children; i++)
    rez_object_enable_scripts(sim, prim->children[i], states);
}

static void rez_obj_asset_callback(simgroup_ctx *sgrp, void* priv, simple_asset *asset) {
  rez_object_desc *desc = (rez_object_desc*)priv;
  if(desc->ctx == NULL) {
    delete desc;
  } else if(asset == NULL) {
    user_send_message(desc->ctx, "ERROR: missing asset trying to rez object");
    user_del_self_pointer(&desc->ctx); delete desc;
  } else if(desc->ctx->av == NULL) {
    user_send_message(desc->ctx, "ERROR: you must have an avatar to attach something");
    user_del_self_pointer(&desc->ctx); delete desc;
  } else {
    script_state_map states;
    char buf[asset->data.len+1]; // FIXME!!!!!
    memcpy(buf, asset->data.data, asset->data.len);
    buf[asset->data.len] = 0;
    printf("DEBUG: Got object asset data: ~%s~\n", buf);
    primitive_obj *prim = prim_from_os_xml(&asset->data, states);
    if(prim != NULL) {
      uuid_copy(prim->inv_item_id, desc->inv_item_id);
      rez_object_set_perms(prim, desc);

      free(prim->name); free(prim->description);
      prim->name = desc->name; desc->name = NULL;
      prim->description = desc->description; desc->description = NULL;

      if(desc->is_attachment) {
	if(desc->attach_point == ATTACH_TO_LAST) {
	  if(prim->attach_point <= 0 || prim->attach_point >= NUM_ATTACH_POINTS)
	    desc->attach_point = ATTACH_L_HAND;
	  else desc->attach_point = prim->attach_point;
	}
	world_add_attachment(desc->ctx->sim, desc->ctx->av, prim, desc->attach_point);
      } else {
	prim->ob.local_pos = desc->pos;
	world_insert_obj(desc->ctx->sim, &prim->ob);
      }
      rez_object_enable_scripts(desc->ctx->sim, prim, states);
    }
    for(std::map<obj_uuid_t,os_script_state>::iterator iter = states.begin();
	iter != states.end(); iter++) {
      xmlFree(iter->second.state);
    }
    // FIXME - free script state info!
    user_del_self_pointer(&desc->ctx); delete desc;
  }
}

static void rez_obj_inv_callback(struct inventory_item* item, void* priv) {
  rez_object_desc *desc = (rez_object_desc*)priv;
  if(desc->ctx == NULL) {
    delete desc;
  } else if(item == NULL) {
    user_del_self_pointer(&desc->ctx); delete desc;
  } else if(item->asset_type != ASSET_OBJECT) {
    user_send_message(desc->ctx, "ERROR: RezObject or similar on an inventory item that's not an object");
    user_del_self_pointer(&desc->ctx); delete desc;
  } else if((item->perms.current & PERM_COPY) == 0) {
    // FIXME - what if the user isn't the inventory owner?
    user_send_message(desc->ctx, "FIXME - we don't support rezzing no-copy inventory");
    user_del_self_pointer(&desc->ctx); delete desc;
  } else {
    // FIXME - what if the user isn't the inventory owner? Perms could be wrong
    desc->inv_perms = item->perms; desc->inv_flags = item->flags;
    desc->name = strdup(item->name);
    desc->description = strdup(item->description);
    caj_get_asset(desc->ctx->sgrp, item->asset_id, rez_obj_asset_callback, desc);
  }
}

void user_rez_object(user_ctx *ctx, uuid_t from_prim, uuid_t item_id, 
		     uuid_t owner_id, caj_vector3 pos) {
  if(uuid_is_null(from_prim)) {
    rez_object_desc *desc = new rez_object_desc();
    desc->ctx = ctx; user_add_self_pointer(&desc->ctx);
    uuid_copy(desc->inv_item_id, item_id); // FIXME - don't do this from library!
    desc->is_attachment = FALSE;
    desc->pos = pos; // FIXME - do proper raycast
    user_fetch_inventory_item(ctx, item_id, owner_id,
			      rez_obj_inv_callback, desc);
  } else {
    // FIXME - ????
  }

}


void user_rez_attachment(user_ctx *ctx, uuid_t item_id, uint8_t attach_point) {
  if(attach_point >= NUM_ATTACH_POINTS) {
    user_send_message(ctx, "ERROR: Bad attachment point");
  }

  rez_object_desc *desc = new rez_object_desc();
  desc->ctx = ctx; user_add_self_pointer(&desc->ctx);
  uuid_copy(desc->inv_item_id, item_id);
  desc->is_attachment = TRUE; desc->attach_point = attach_point;
  // FIXME - handle attaching library items (fun!)
  user_fetch_inventory_item(ctx, item_id, ctx->user_id,
			    rez_obj_inv_callback, desc);
}

void user_remove_attachment(struct user_ctx *ctx, struct primitive_obj *prim) {
  if(ctx->av == NULL || prim->ob.parent != &ctx->av->ob) {
    printf("WARNING: attempt to detach non-attachment\n"); return;
  }

  // FIXME - save the attachment back to inventory.
  world_delete_prim(ctx->sim, prim);
}

#if 0 // TODO - finish this
static int prim_part_to_osxml(xmlTextWriterPtr writer,
			      primitive_obj *prim) {
  os_object_part objpart;

  return osglue_serialise_xml(writer, deserialise_object_part,
			      &objpart);
}

static void prim_to_osxml(primitive_obj *prim, caj_string *out) {
  out->data = NULL; out->len = 0;

  xmlTextWriterPtr writer;
  xmlBufferPtr buf;
  buf = xmlBufferCreate();
  if(buf == NULL) goto out_fail;
  writer = xmlNewTextWriterMemory(buf, 0);
  if(writer == NULL) goto out_freebuf;

  if(xmlTextWriterStartDocument(writer,NULL,"UTF-8",NULL) < 0)
    goto out_freewriter;

  if(xmlTextWriterStartElement(writer, BAD_CAST "SceneObjectGroup") < 0) 
    goto out_freewriter;

  if(xmlTextWriterStartElement(writer, BAD_CAST "RootPart") < 0) 
    goto out_freewriter;
  
  if(!prim_part_to_osxml(writer, prim)) goto out_freewriter;

  if(xmlTextWriterEndElement(writer) < 0) goto out_freewriter;

  if(xmlTextWriterStartElement(writer, BAD_CAST "OtherParts") < 0) 
    goto out_freewriter;

  // TODO - write child prims

  if(xmlTextWriterEndElement(writer) < 0) goto out_freewriter;
  if(xmlTextWriterEndElement(writer) < 0) goto out_freewriter;
  if(xmlTextWriterEndDocument(writer) < 0) {
    printf("DEBUG: couldn't end XML document\n"); goto out_freewriter;
  }

 out_freewriter:
  xmlFreeTextWriter(writer);
 out_freebuf:
  xmlBufferFree(buf);
 out_fail:
  return;
}
#endif

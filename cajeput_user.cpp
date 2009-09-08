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
#include <cassert>

void user_reset_timeout(struct user_ctx* ctx) {
  ctx->last_activity = g_timer_elapsed(ctx->sim->timer, NULL);
}


void caj_uuid_to_name(struct simulator_ctx *sim, uuid_t id, 
		      void(*cb)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
		      void *cb_priv) {
  sim->gridh.uuid_to_name(sim, id, cb, cb_priv);
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
  double time_now = g_timer_elapsed(ctx->sim->timer, NULL);
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    ctx->throttles[i].time = time_now;
    ctx->throttles[i].level = 0.0f;
    ctx->throttles[i].rate = rates[i] / 8.0f;
    
  }
}

static float sl_unpack_float(unsigned char *buf) {
  float f;
  // FIXME - need to swap byte order if necessary.
  memcpy(&f,buf,sizeof(float));
  return f;
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
    throttles[i] =  sl_unpack_float(data + 4*i);
    printf("  throttle %s: %f\n", sl_throttle_names[i], throttles[i]);
    user_set_throttles(ctx, throttles);
  }
}

void user_get_throttles_block(struct user_ctx* ctx, unsigned char* data,
			      int len) {
  float throttles[SL_NUM_THROTTLES];

  if(len < SL_NUM_THROTTLES*4) {
    printf("Error: AgentThrottle with not enough data\n");
    return;
  } else {
    len =  SL_NUM_THROTTLES*4;
  }

  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    throttles[i] = ctx->throttles[i].rate * 8.0f;
  }

  // FIXME - endianness
  memcpy(data, throttles, len);
}

void user_reset_throttles(struct user_ctx *ctx) {
  double time_now = g_timer_elapsed(ctx->sim->timer, NULL);
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    ctx->throttles[i].time = time_now;
    ctx->throttles[i].level = 0.0f;
  }
}

void user_update_throttles(struct user_ctx *ctx) {
  double time_now = g_timer_elapsed(ctx->sim->timer, NULL);
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

// FIXME - remove???
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
  uuid_clear(chat.source); // FIXME - set this?
  uuid_clear(chat.owner);
  chat.name = "Cajeput";
  chat.msg = (char*)msg;

  // FIXME - evil hack
  user_av_chat_callback(ctx->sim, NULL, &chat, ctx);
}

void user_fetch_inventory_folder(simulator_ctx *sim, user_ctx *user, 
				 uuid_t folder_id,
				  void(*cb)(struct inventory_contents* inv, 
					    void* priv),
				  void *cb_priv) {
  std::map<obj_uuid_t,inventory_contents*>::iterator iter = 
       sim->inv_lib.find(folder_id);
  if(iter == sim->inv_lib.end()) {
    sim->gridh.fetch_inventory_folder(sim,user,user->grid_priv,
				      folder_id,cb,cb_priv);
  } else {
    cb(iter->second, cb_priv);
  }
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

// for after region handle is resolved...
static void do_real_teleport(struct teleport_desc* tp) {
  if(tp->ctx == NULL) {
    user_teleport_failed(tp,"cancelled");
  } else if(tp->region_handle == tp->ctx->sim->region_handle) {
    user_teleport_failed(tp, "FIXME: Local teleport not supported");
  } else {
    //user_teleport_failed(tp, "FIXME: Teleports not supported");
    simulator_ctx *sim = tp->ctx->sim;
    sim->gridh.do_teleport(sim, tp);
  }
}

void user_teleport_failed(struct teleport_desc* tp, const char* reason) {
  if(tp->ctx != NULL) {
    // FIXME - need to check hook not NULL
    tp->ctx->userh->teleport_failed(tp->ctx, reason);
  }
  del_teleport_desc(tp);
}

// In theory, we can send arbitrary strings, but that seems to be bugged.
void user_teleport_progress(struct teleport_desc* tp, const char* msg) {
  // FIXME - need to check hook not NULL
  if(tp->ctx != NULL) 
    tp->ctx->userh->teleport_progress(tp->ctx, msg, tp->flags);
}

void user_complete_teleport(struct teleport_desc* tp) {
  if(tp->ctx != NULL) {
    printf("DEBUG: completing teleport\n");
    tp->ctx->flags |= AGENT_FLAG_TELEPORT_COMPLETE;
    // FIXME - need to check hook not NULL
    tp->ctx->userh->teleport_complete(tp->ctx, tp);
  }
  del_teleport_desc(tp);
}

void user_teleport_location(struct user_ctx* ctx, uint64_t region_handle,
			    const caj_vector3 *pos, const caj_vector3 *look_at) {
  teleport_desc* desc = begin_teleport(ctx);
  if(desc == NULL) return;
  desc->region_handle = region_handle;
  desc->pos = *pos;
  desc->look_at = *look_at;
  desc->flags = TELEPORT_TO_LOCATION;
  do_real_teleport(desc);
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

static void debug_prepare_new_user(struct sim_new_user *uinfo) {
  char user_id[40], session_id[40];
  uuid_unparse(uinfo->user_id,user_id);
  uuid_unparse(uinfo->session_id,session_id);
  printf("Expecting new user %s %s, user_id=%s, session_id=%s, "
	 "circuit_code=%lu (%s)\n", uinfo->first_name, uinfo->last_name,
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

  ctx->texture_entry.data = NULL;
  ctx->visual_params.data = NULL;
  ctx->appearance_serial = ctx->wearable_serial = 0;
  memset(ctx->wearables, 0, sizeof(ctx->wearables));

  uuid_copy(ctx->default_anim.anim, stand_anim);
  ctx->default_anim.sequence = 1;
  uuid_clear(ctx->default_anim.obj); // FIXME - is this right?
  ctx->default_anim.caj_type = CAJ_ANIM_TYPE_DEFAULT;
  ctx->anim_seq = 2;

  debug_prepare_new_user(uinfo);

  uuid_copy(ctx->user_id, uinfo->user_id);
  uuid_copy(ctx->session_id, uinfo->session_id);
  uuid_copy(ctx->secure_session_id, uinfo->secure_session_id);

  ctx->sim = sim;
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

  user_set_throttles(ctx,throttle_init);

  sim->gridh.user_created(sim,ctx,&ctx->grid_priv);

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
    ctx->av->ob.pos.x = 128.0f; // FIXME - correct position!
    ctx->av->ob.pos.y = 128.0f;
    ctx->av->ob.pos.z = 60.0f;
    ctx->av->ob.rot.x = ctx->av->ob.rot.y = ctx->av->ob.rot.z = 0.0f;
    ctx->av->ob.rot.w = 1.0f;
    ctx->av->ob.velocity.x = ctx->av->ob.velocity.y = ctx->av->ob.velocity.z = 0.0f;
    uuid_copy(ctx->av->ob.id, ctx->user_id);
    world_insert_obj(ctx->sim, &ctx->av->ob);
    world_obj_listen_chat(ctx->sim,&ctx->av->ob,user_av_chat_callback,ctx);
    world_obj_add_channel(ctx->sim,&ctx->av->ob,0);
    world_obj_add_channel(ctx->sim,&ctx->av->ob,DEBUG_CHANNEL);

    ctx->sim->gridh.user_entered(ctx->sim, ctx, ctx->grid_priv);
  }

  return true;
}

void user_remove_int(user_ctx **user) {
  user_ctx* ctx = *user;
  simulator_ctx *sim = ctx->sim;
  if(ctx->av != NULL) {
    world_remove_obj(ctx->sim, &ctx->av->ob);
    if(!(ctx->flags & (AGENT_FLAG_CHILD|AGENT_FLAG_TELEPORT_COMPLETE))) {
      sim->gridh.user_logoff(sim, ctx->user_id,
			     &ctx->av->ob.pos, &ctx->av->ob.pos);
    }
    free(ctx->av); ctx->av = NULL;
  }

  printf("Removing user %s %s\n", ctx->first_name, ctx->last_name);

  // If we're logging out, sending DisableSimulator causes issues
  // HACK - also, for now teleports are also problematic - FIXME
  if(ctx->userh != NULL  && !(ctx->flags & (AGENT_FLAG_IN_LOGOUT|
					    AGENT_FLAG_TELEPORT_COMPLETE)) &&
     ctx->userh->disable_sim != NULL) {
    // HACK HACK HACK - should be in main hook?
    ctx->userh->disable_sim(ctx->user_priv); // FIXME
  } 

  user_call_delete_hook(ctx);

  // mustn't do this in user_session_close; that'd break teleport
  user_int_event_queue_free(ctx);

  sim->gridh.user_deleted(ctx->sim,ctx,ctx->grid_priv);

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
  simulator_ctx *sim = ctx->sim;
  if(ctx->av != NULL) {
    // FIXME - code duplication
    world_remove_obj(ctx->sim, &ctx->av->ob);
    if(!(ctx->flags & (AGENT_FLAG_CHILD|AGENT_FLAG_TELEPORT_COMPLETE))) {
      sim->gridh.user_logoff(sim, ctx->user_id,
			     &ctx->av->ob.pos, &ctx->av->ob.pos);
    }
    free(ctx->av); ctx->av = NULL;
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

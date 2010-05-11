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

#ifndef CAJEPUT_USER_GLUE_H
#define CAJEPUT_USER_GLUE_H

#include <uuid/uuid.h>
#include "caj_types.h"
#include <stdint.h>
#include "caj_llsd.h"

/* !!!     WARNING   WARNING   WARNING    !!!
   Changing the user_hooks structure breaks ABI compatibility. Also,
   doing anything except inserting a new hook at the end breaks API 
   compatibility, potentially INVISIBLY! 
   Be sure to bump the ABI version after any change.
   !!!     WARNING   WARNING   WARNING    !!!
*/
struct user_hooks {
  void(*teleport_begin)(struct user_ctx* ctx, struct teleport_desc *tp);
  void(*teleport_failed)(struct user_ctx* ctx, const char* reason);
  void(*teleport_progress)(struct user_ctx* ctx, const char* msg, 
			   uint32_t flags);
  void(*teleport_complete)(struct user_ctx* ctx, struct teleport_desc *tp);
  void(*teleport_local)(struct user_ctx* ctx, struct teleport_desc *tp);
  void(*remove)(void* user_priv);
 
  void(*disable_sim)(void* user_priv); // HACK
  void(*chat_callback)(void *user_priv, const struct chat_message *msg);
  void(*alert_message)(void *user_priv, const char* msg, int is_modal);

  // these are temporary hacks.
  void(*send_av_full_update)(user_ctx* ctx, user_ctx* av_user);
  void(*send_av_terse_update)(user_ctx* ctx, world_obj* av);
  void(*send_av_appearance)(user_ctx* ctx, user_ctx* av_user);
  void(*send_av_animations)(user_ctx* ctx, user_ctx* av_user);

  void(*script_dialog)(void *user_priv, primitive_obj* prim,
		       char *msg, int num_buttons, char** buttons,
		       int32_t channel);
};

#define CAJ_ANIM_TYPE_NORMAL 0 // normal
#define CAJ_ANIM_TYPE_DEFAULT 1 // default anim


// called on CompleteAgentMovement - returns success (true/false)
int user_complete_movement(user_ctx *ctx);

user_ctx* sim_bind_user(simulator_ctx *sim, uuid_t user_id, uuid_t session_id,
			uint32_t circ_code, struct user_hooks* hooks);
void *user_get_priv(struct user_ctx *user);
void user_set_priv(struct user_ctx *user, void *priv);

// takes ownership of the passed LLSD
void user_event_queue_send(user_ctx* ctx, const char* name, caj_llsd *body);

void user_set_draw_dist(struct user_ctx *ctx, float far);
void user_set_control_flags(struct user_ctx *ctx, uint32_t control_flags,
			    const caj_quat *rot);

void user_update_throttles(struct user_ctx *ctx);
void user_throttle_expend(struct user_ctx *ctx, int id, float amount);
float user_throttle_level(struct user_ctx *ctx, int id);

int user_can_modify_object(struct user_ctx* ctx, struct world_obj *obj);
int user_can_delete_object(struct user_ctx* ctx, struct world_obj *obj);
int user_can_copy_prim(struct user_ctx* ctx, struct primitive_obj *prim);

void user_duplicate_prim(struct user_ctx* ctx, struct primitive_obj *prim,
			 caj_vector3 position);

uint16_t* user_get_dirty_terrain_array(struct user_ctx *user);

// hacky object update stuff
uint32_t user_get_next_deleted_obj(user_ctx *ctx);
int user_has_pending_deleted_objs(user_ctx *ctx);
int user_get_next_updated_obj(user_ctx *ctx, uint32_t *localid, int *flags);

// FIXME - this is really hacky and wrong
int32_t user_get_an_anim_seq(struct user_ctx *ctx);

#endif

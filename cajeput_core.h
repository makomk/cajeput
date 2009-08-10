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
#include "sl_types.h"

#define CAJEPUT_API_VERSION 0x0002

#define WORLD_HEIGHT 4096

struct user_ctx;
struct simulator_ctx;
struct cap_descrip;
struct obj_chat_listeners;

// intended to be usable as a mask
#define OBJ_TYPE_PRIM 1
#define OBJ_TYPE_AVATAR 2

struct world_obj {
  int type;
  sl_vector3 pos;
  sl_quat rot;
  uuid_t id;
  uint32_t local_id;
  void *phys;
  struct obj_chat_listener *chat;
};

// --- START sim query code ---

// NB strings returned should NOT be free()d or stored
uint32_t sim_get_region_x(struct simulator_ctx *sim);
uint32_t sim_get_region_y(struct simulator_ctx *sim);
uint64_t sim_get_region_handle(struct simulator_ctx *sim);
char* sim_get_name(struct simulator_ctx *sim);
void sim_get_region_uuid(struct simulator_ctx *sim, uuid_t u);
void sim_get_owner_uuid(struct simulator_ctx *sim, uuid_t u);
uint16_t sim_get_http_port(struct simulator_ctx *sim);
uint16_t sim_get_udp_port(struct simulator_ctx *sim);
void* sim_get_grid_priv(struct simulator_ctx *sim);
void sim_set_grid_priv(struct simulator_ctx *sim, void* p);

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
struct cajeput_grid_hooks {
  void(*do_grid_login)(struct simulator_ctx* sim);
  void(*user_created)(struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void **user_priv);
  void(*fetch_user_inventory)(simulator_ctx *sim, user_ctx *user,
			      void *user_priv);
  void(*user_logoff)(struct simulator_ctx* sim,
		     const uuid_t user_id, const sl_vector3 *pos,
		     const sl_vector3 *look_at);
  void(*cleanup)(struct simulator_ctx* sim);
  void(*user_deleted)(struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void *user_priv);
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
};

// Caller owns the struct and any strings pointed to by it
void sim_prepare_new_user(struct simulator_ctx *sim,
			  struct sim_new_user *uinfo);

// ICK. For use by OpenSim glue code only.
void user_logoff_user_osglue(struct simulator_ctx *sim, uuid_t agent_id, 
			     uuid_t secure_session_id);

// ----- PHYSICS GLUE -------------

struct cajeput_physics_hooks {
  void(*add_object)(struct simulator_ctx *sim, void *priv,
		    struct world_obj *obj);
  void(*del_object)(struct simulator_ctx *sim, void *priv,
		    struct world_obj *obj);
  int(*update_pos)(struct simulator_ctx *sim, void *priv,
		   struct world_obj *obj); /* FIXME - HACK! */
  void(*set_force)(struct simulator_ctx *sim, void *priv,
		   struct world_obj *obj, sl_vector3 force); /* HACK HACK HACK */
  void(*step)(struct simulator_ctx *sim, void *priv);
  void(*destroy)(struct simulator_ctx *sim, void *priv);
};

int cajeput_physics_init(int api_version, struct simulator_ctx *sim, 
			 void **priv, struct cajeput_physics_hooks *hooks);

// ----- MISC STUFF ---------

void *user_get_grid_priv(struct user_ctx *user);
void user_get_uuid(struct user_ctx *user, uuid_t u);
void user_get_session_id(struct user_ctx *user, uuid_t u);

void user_session_close(user_ctx* ctx);
void user_reset_timeout(struct user_ctx* ctx);

void sim_shutdown_hold(struct simulator_ctx *sim);
void sim_shutdown_release(struct simulator_ctx *sim);

// FIXME - should these be internal?
void world_insert_obj(struct simulator_ctx *sim, struct world_obj *ob);
void world_remove_obj(struct simulator_ctx *sim, struct world_obj *ob);
void world_send_chat(struct simulator_ctx *sim, struct chat_message* chat);

// FIXME - this should definitely be internal
void world_move_obj_int(struct simulator_ctx *sim, struct world_obj *ob,
			const sl_vector3 &new_pos);

#endif

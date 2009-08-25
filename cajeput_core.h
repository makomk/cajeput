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

#ifdef __cplusplus
extern "C" {
#endif

#define CAJEPUT_API_VERSION 0x0006

struct user_ctx;
struct simulator_ctx;
struct cap_descrip;

// --- START sim query code ---

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
void* sim_get_grid_priv(struct simulator_ctx *sim);
void sim_set_grid_priv(struct simulator_ctx *sim, void* p);
float* sim_get_heightfield(struct simulator_ctx *sim);

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
  void(*do_grid_login)(struct simulator_ctx* sim);
  void(*user_created)(struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void **user_priv);

  /* user entered the region */
  void(*user_entered)(simulator_ctx *sim, user_ctx *user,
		      void *user_priv);

  /* user is logging off */
  void(*user_logoff)(struct simulator_ctx* sim,
		     const uuid_t user_id, const sl_vector3 *pos,
		     const sl_vector3 *look_at);

  void(*cleanup)(struct simulator_ctx* sim);

  /* user context is being deleted */
  void(*user_deleted)(struct simulator_ctx* sim,
		      struct user_ctx* user,
		      void *user_priv);

  void(*do_teleport)(struct simulator_ctx* sim, struct teleport_desc* tp);

  void(*get_texture)(struct simulator_ctx *sim, struct texture_desc *texture);
  void(*map_block_request)(struct simulator_ctx *sim, int min_x, int max_x, 
			   int min_y, int max_y, 
			   void(*cb)(void *priv, struct map_block_info *blocks, 
				     int count),
			   void *cb_priv);
  void(*map_name_request)(struct simulator_ctx* sim, const char* name,
			  void(*cb)(void* cb_priv, struct map_block_info* blocks, 
				    int count),
			  void *cb_priv);

  // interesting interesting function...
  void(*fetch_inventory_folder)(simulator_ctx *sim, user_ctx *user,
				void *user_priv, uuid_t folder_id,
				void(*cb)(struct inventory_contents* inv, 
					  void* priv),
				void *cb_priv);

  void(*uuid_to_name)(struct simulator_ctx *sim, uuid_t id, 
		      void(*cb)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
		      void *cb_priv);
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
  int is_child;
};

// Caller owns the struct and any strings pointed to by it
struct user_ctx* sim_prepare_new_user(struct simulator_ctx *sim,
				      struct sim_new_user *uinfo);

// ICK. For use by OpenSim glue code only.
void user_logoff_user_osglue(struct simulator_ctx *sim, uuid_t agent_id, 
			     uuid_t secure_session_id);


// ----------------- SIM HOOKS --------------------------

typedef void(*sim_generic_cb)(simulator_ctx* sim, void* priv);

void sim_add_shutdown_hook(struct simulator_ctx *sim,
			   sim_generic_cb cb, void *priv);
void sim_remove_shutdown_hook(struct simulator_ctx *sim,
			      sim_generic_cb cb, void *priv);

// ----- MISC STUFF ---------

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
void sim_texture_finished_load(texture_desc *desc);
struct texture_desc *sim_get_texture(struct simulator_ctx *sim, uuid_t asset_id);
void sim_request_texture(struct simulator_ctx *sim, struct texture_desc *desc);

#ifdef __cplusplus
}
#endif

#endif

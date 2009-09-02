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

#ifndef CAJEPUT_WORLD_H
#define CAJEPUT_WORLD_H

#include <uuid/uuid.h>
#include "sl_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct user_ctx;
struct simulator_ctx;

struct obj_chat_listeners;

#define WORLD_HEIGHT 4096

// internal intended to be usable as a mask
#define OBJ_TYPE_PRIM 1
#define OBJ_TYPE_AVATAR 2

struct world_obj {
  int type;
  sl_vector3 pos;
  sl_vector3 scale; // FIXME - set correctly for avatars
  sl_vector3 velocity;
  sl_quat rot;
  uuid_t id;
  uint32_t local_id;
  void *phys;
  struct obj_chat_listener *chat;
};

#define MATERIAL_STONE   0
#define MATERIAL_METAL   1
#define MATERIAL_GLASS   2
#define MATERIAL_WOOD    3
#define MATERIAL_FLESH   4
#define MATERIAL_PLASTIC 5
#define MATERIAL_RUBBER  6
#define MATERIAL_LIGHT   7 // ???

struct primitive_obj {
  struct world_obj ob; // must be first!
  uint32_t crc_counter;
  uint8_t sale_type;

  uint8_t material, path_curve, profile_curve;
  uint16_t path_begin, path_end; // FIXME - why 16 bits?
  uint8_t path_scale_x, path_scale_y, path_shear_x, path_shear_y;
  int8_t path_twist, path_twist_begin, path_radius_offset;
  int8_t path_taper_x, path_taper_y;
  uint8_t path_revolutions; // slightly oddball one, this.
  int8_t path_skew;
  uint16_t profile_begin, profile_end, profile_hollow; // again, why 16 bits?
  uuid_t creator, owner;
  uint32_t base_perms, owner_perms, group_perms, everyone_perms, next_perms;
  int32_t sale_price;
  char *name, *description;
  sl_string tex_entry;

  struct {
    unsigned int num_items, alloc_items;
    struct inventory_item** items;
    uint32_t serial; // FIXME - uint16_t
    char *filename; // ick. For working with the horrible Xfer system.
    // Clear filename and increment serial on inv updates
  } inv;
};

#define CHAT_TYPE_WHISPER 0
#define CHAT_TYPE_NORMAL 1
#define CHAT_TYPE_SHOUT 2
/* 3 is say chat, which is obsolete */
#define CHAT_TYPE_START_TYPING 4
#define CHAT_TYPE_STOP_TYPING 5
#define CHAT_TYPE_DEBUG 6 // what???
/* no 7? */
#define CHAT_TYPE_OWNER_SAY 8
#define CHAT_TYPE_REGION_SAY 0xff // ???

// -------- SCRIPTING GLUE --------------------

  typedef void(*compile_done_cb)(void *priv, int success, char* output, int output_len);

  struct cajeput_script_hooks {
    
    void* (*add_script)(simulator_ctx *sim, void *priv, primitive_obj *prim, 
			inventory_item *inv, simple_asset *asset, 
			compile_done_cb cb, void *cb_priv);
    void (*kill_script)(simulator_ctx *sim, void *priv, void *script);

    void(*shutdown)(struct simulator_ctx *sim, void *priv);
  };

  int caj_scripting_init(int api_version, struct simulator_ctx* sim, 
			 void **priv, struct cajeput_script_hooks *hooks);


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
  void(*set_target_velocity)(struct simulator_ctx *sim, void *priv,
			     struct world_obj *obj, sl_vector3 velocity);
  void(*set_avatar_flying)(struct simulator_ctx *sim, void *priv,
			   struct world_obj *obj, int is_flying);
  void(*step)(struct simulator_ctx *sim, void *priv);
  void(*destroy)(struct simulator_ctx *sim, void *priv);
};

int cajeput_physics_init(int api_version, struct simulator_ctx *sim, 
			 void **priv, struct cajeput_physics_hooks *hooks);

// FIXME - should these be internal?
void world_insert_obj(struct simulator_ctx *sim, struct world_obj *ob);
void world_remove_obj(struct simulator_ctx *sim, struct world_obj *ob);

struct world_obj* world_object_by_id(struct simulator_ctx *sim, uuid_t id);
struct world_obj* world_object_by_localid(struct simulator_ctx *sim, uint32_t id);
struct primitive_obj* world_begin_new_prim(struct simulator_ctx *sim);
void world_send_chat(struct simulator_ctx *sim, struct chat_message* chat);

void world_chat_from_prim(struct simulator_ctx *sim, struct primitive_obj* prim,
			  int32_t chan, char *msg, int chat_type);

// FIXME - this should definitely be internal
void world_move_obj_int(struct simulator_ctx *sim, struct world_obj *ob,
			const sl_vector3 &new_pos);

  // don't ask. Really. Also, don't free or store returned string.
  char* world_prim_upd_inv_filename(struct primitive_obj* prim);

void user_rez_script(struct user_ctx *ctx, struct primitive_obj *prim,
		     const char *name, const char *descrip, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif

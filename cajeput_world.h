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

#ifndef CAJEPUT_WORLD_H
#define CAJEPUT_WORLD_H

#include <uuid/uuid.h>
#include "caj_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct user_ctx;
struct simulator_ctx;

struct obj_chat_listeners;

#define WORLD_HEIGHT 4096
#define WORLD_REGION_SIZE 256

// internal, intended to be usable as a mask
#define OBJ_TYPE_PRIM 1
#define OBJ_TYPE_AVATAR 2

struct world_obj {
  int type;
  caj_vector3 local_pos, world_pos;
  caj_vector3 scale; // FIXME - set correctly for avatars
  caj_vector3 velocity;
  caj_quat rot;
  uuid_t id;
  uint32_t local_id;
  struct world_obj *parent;
  void *phys;
  struct obj_chat_listeners *chat;
};

// again internal, flags
// if you add new ones, don't forget to update the object update code
// appropriately.
// Also, in order to avoid the physics code breaking, please:
//  - change the property, then mark as updated, not the other way around
//  - between adding, reordering or removing the children of a parent prim and
//    marking it as CAJ_OBJUPD_CHILDREN, do NOT mark ANY of its child prims with
//    any update except  CAJ_OBJUPD_PARENT
//  - when adding a linkset, do NOT mark any of the children as anything except
//    CAJ_OBJUPD_CREATED until the parent prim has been CAJ_OBJUPD_CREATED.
//  Thank you.
#define CAJ_OBJUPD_POSROT 0x1
#define CAJ_OBJUPD_CREATED 0x2 // newly-created object
#define CAJ_OBJUPD_SCALE 0x4
#define CAJ_OBJUPD_SHAPE 0x8
#define CAJ_OBJUPD_TEXTURE 0x10
#define CAJ_OBJUPD_FLAGS 0x20
#define CAJ_OBJUPD_MATERIAL 0x40
#define CAJ_OBJUPD_TEXT 0x80
#define CAJ_OBJUPD_PARENT 0x100 // object reparented
#define CAJ_OBJUPD_CHILDREN 0x200 // object's children changed; FIXME - make sure this is sent
#define CAJ_OBJUPD_EXTRA_PARAMS 0x400

  // bunch of SL constants
#define MATERIAL_STONE   0
#define MATERIAL_METAL   1
#define MATERIAL_GLASS   2
#define MATERIAL_WOOD    3
#define MATERIAL_FLESH   4
#define MATERIAL_PLASTIC 5
#define MATERIAL_RUBBER  6
#define MATERIAL_LIGHT   7 // ???

  // bunch more SL constants (ObjectUpdate.RegionData.UpdateFlags)
#define PRIM_FLAG_PHYSICAL 0x1
#define PRIM_FLAG_CREATE_SELECTED 0x2
#define PRIM_FLAG_CAN_MODIFY 0x4
#define PRIM_FLAG_CAN_COPY 0x8
#define PRIM_FLAG_ANY_OWNER 0x10
#define PRIM_FLAG_YOU_OWNER 0x20
#define PRIM_FLAG_SCRIPTED 0x40
#define PRIM_FLAG_TOUCH 0x80
#define PRIM_FLAG_CAN_MOVE 0x100
#define PRIM_FLAG_TAKES_PAYMENT 0x200
#define PRIM_FLAG_PHANTOM 0x400
#define PRIM_FLAG_INVENTORY_EMPTY 0x800
  // bunch of joint flags omitted here
#define PRIM_FLAG_ALLOW_INV_DROP 0x10000
#define PRIM_FLAG_CAN_TRANSFER 0x20000
#define PRIM_FLAG_GROUP_OWNER 0x40000
#define PRIM_FLAG_OBJECT_YOU_OFFICER 0x80000
#define PRIM_FLAG_CAMERA_DECOUPLED 0x100000
#define PRIM_FLAG_ANIM_SOURCE 0x200000 // viewer local
#define PRIM_FLAG_CAMERA_SOURCE 0x400000 // viewer local
#define PRIM_FLAG_CAST_SHADOWS   0x800000
  // bunch of server-only flags omitted here
#define PRIM_FLAG_OWNER_MODIFY 0x10000000
#define PRIM_FLAG_TEMP_ON_REZ 0x20000000
#define PRIM_FLAG_TEMPORARY 0x40000000
#define OBJ_UPD_FLAG_ZLIB_COMPRESSED 0x80000000 // ick

  // even more SL constants.
#define ATTACH_TO_LAST 0 // attach to previous location.

#define ATTACH_CHEST 1
#define ATTACH_HEAD 2
#define ATTACH_L_SHOULDER 3
#define ATTACH_R_SHOULDER 4
#define ATTACH_L_HAND 5
#define ATTACH_R_HAND 6
#define ATTACH_L_FOOT 7
#define ATTACH_R_FOOT 8
#define ATTACH_BACK 9 // spine?
#define ATTACH_PELVIS 10
#define ATTACH_MOUTH 11
#define ATTACH_CHIN 12
#define ATTACH_L_EAR 13
#define ATTACH_R_EAR 14
#define ATTACH_L_EYE 15
#define ATTACH_R_EYE 16
#define ATTACH_NOSE 17
#define ATTACH_R_UPPER_ARM 18 // is this right way around?
#define ATTACH_R_LOWER_ARM 19
#define ATTACH_L_UPPER_ARM 20
#define ATTACH_L_LOWER_ARM 21
#define ATTACH_R_HIP 22
#define ATTACH_R_UPPER_LEG 23
#define ATTACH_R_LOWER_LEG 24
#define ATTACH_L_HIP 25
#define ATTACH_L_UPPER_LEG 26
#define ATTACH_L_LOWER_LEG 27
#define ATTACH_BELLY 28
#define ATTACH_R_PEC 29 // FIXME - may be backwards
#define ATTACH_L_PEC 30

#define FIRST_HUD_ATTACH_POINT 31
#define ATTACH_HUD_CENTER_2 31
#define ATTACH_HUD_TOP_RIGHT 32
#define ATTACH_HUD_TOP_CENTER 33
#define ATTACH_HUD_TOP_LEFT 34
#define ATTACH_HUD_CENTER_1 35
#define ATTACH_HUD_BOTTOM_LEFT 36
#define ATTACH_HUD_BOTTOM 37
#define ATTACH_HUD_BOTTOM_RIGHT 38

#define NUM_ATTACH_POINTS 39

// WARNING! If you change this structure, you must fix both the dump/restore
// code in cajeput_dump.cpp and world_begin_new_prim/world_delete_prim in 
// cajeput_main.cpp. Oh, and bump the ABI revision in cajeput_core.h.
struct primitive_obj {
  struct world_obj ob; // must be first!
  uint32_t crc_counter;
  uint8_t sale_type;

  uint8_t material, path_curve, profile_curve;
  uint16_t path_begin, path_end;
  uint8_t path_scale_x, path_scale_y, path_shear_x, path_shear_y;
  int8_t path_twist, path_twist_begin, path_radius_offset;
  int8_t path_taper_x, path_taper_y;
  uint8_t path_revolutions; // slightly oddball one, this.
  int8_t path_skew;
  uint8_t attach_point; // may be set for non-attachments
  uint16_t profile_begin, profile_end, profile_hollow;
  uuid_t creator, owner;
  uuid_t inv_item_id;
  permission_flags perms;
  int32_t sale_price;
  uint32_t flags; // PRIM_FLAG_*
  uint32_t caj_flags; // not used yet
  char *name, *description;
  caj_string tex_entry;
  caj_string extra_params;

  int num_children;
  struct primitive_obj **children;

  char *sit_name, *touch_name;
  char *hover_text; uint8_t text_color[4];
  int32_t creation_date; // FIXME - should probably bigger (year 2037 bug!)

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
#define CHAT_TYPE_DEBUG 6 // script debug messages. 
/* no 7? */
#define CHAT_TYPE_OWNER_SAY 8
#define CHAT_TYPE_REGION_SAY 0xff // ???

#define CHAT_AUDIBLE_FULLY 1
#define CHAT_AUDIBLE_BARELY 0
#define CHAT_AUDIBLE_NO -1

#define CHAT_SOURCE_SYSTEM 0
#define CHAT_SOURCE_AVATAR 1
#define CHAT_SOURCE_OBJECT 2

// -------- SCRIPTING GLUE --------------------

  typedef void(*compile_done_cb)(void *priv, int success, const char* output, 
				 int output_len);

  struct cajeput_script_hooks {
    // most of these hooks are mandatory.
    void* (*add_script)(simulator_ctx *sim, void *priv, primitive_obj *prim, 
			inventory_item *inv, simple_asset *asset, 
			compile_done_cb cb, void *cb_priv);
    void (*save_script)(simulator_ctx *sim, void *priv, void *script,
			caj_string *out);
    void* (*restore_script)(simulator_ctx *sim, void *priv, primitive_obj *prim,
			    inventory_item *inv, caj_string *out);
    void (*kill_script)(simulator_ctx *sim, void *priv, void *script);

    int (*get_evmask)(simulator_ctx *sim, void *priv, void *script);
    void (*touch_event)(simulator_ctx *sim, void *priv, void *script,
			user_ctx *user, world_obj *av, int touch_type,
			const struct caj_touch_info *info);
    void (*collision_event)(simulator_ctx *sim, void *priv, void *script,
			    world_obj *collider, int coll_type);
    void (*link_message)(simulator_ctx *sim, void *priv, void *script,
			 int sender_num,  int num, char *str, char *id);

    void(*shutdown)(struct simulator_ctx *sim, void *priv);

    // these are optional if you don't use any chat listeners.
    void (*disable_listens)(simulator_ctx *sim, void *priv, void *script);
    void (*reenable_listens)(simulator_ctx *sim, void *priv, void *script);
  };

  int caj_scripting_init(int api_version, struct simulator_ctx* sim, 
			 void **priv, struct cajeput_script_hooks *hooks);

  void world_script_link_message(struct simulator_ctx* sim, 
				 struct primitive_obj *prim, int link_num, 
				 int num, char *str, char *id);


struct chat_message {
  int32_t channel;
  caj_vector3 pos;
  uuid_t source;
  uuid_t owner;
  uint8_t source_type, chat_type;
  char *name;
  char *msg;
};

typedef void(*obj_chat_callback)(struct simulator_ctx *sim, struct world_obj *obj,
				 const struct chat_message *msg, 
				 struct obj_chat_listener *listen, void *user_data);

struct obj_chat_listener {
  // int32_t chan;
  struct world_obj *obj;
  obj_chat_callback callback;
  void *user_data;  
};

struct script_chat_listener {
  struct obj_chat_listener l; // must be first item.
  int32_t chan;
  struct primitive_obj *prim; // not the same as l.obj!
  struct simulator_ctx *sim; // TODO: figure out some way to remove this.
};


  void world_script_add_listen(struct script_chat_listener *listen);
  void world_script_remove_listen(struct script_chat_listener *listen);
  

// ----- PHYSICS GLUE -------------

struct caj_phys_collision {
  uint32_t collidee, collider;
#if 0 // FIXME - TODO
  caj_vector3 collider_pos;
  caj_quat collider_rot;
#endif
};

struct cajeput_physics_hooks {
  /* upd_object also does adding of objects */
  void(*upd_object)(struct simulator_ctx *sim, void *priv,
		    struct world_obj *obj, int update_flags);
  void(*del_object)(struct simulator_ctx *sim, void *priv,
		    struct world_obj *obj);
  /* void(*set_force)(struct simulator_ctx *sim, void *priv,
     struct world_obj *obj, caj_vector3 force); */ /* HACK HACK HACK */
  void(*set_target_velocity)(struct simulator_ctx *sim, void *priv,
			     struct world_obj *obj, caj_vector3 velocity);
  void(*set_avatar_flying)(struct simulator_ctx *sim, void *priv,
			   struct world_obj *obj, int is_flying);
  void(*destroy)(struct simulator_ctx *sim, void *priv);
  void(*apply_impulse)(struct simulator_ctx *sim, void *priv,
		       struct world_obj *obj, caj_vector3 impulse,
		       int is_local);
};

int cajeput_physics_init(int api_version, struct simulator_ctx *sim, 
			 void **priv, struct cajeput_physics_hooks *hooks);

void avatar_set_footfall(struct simulator_ctx *sim, struct world_obj *av,
			 const caj_vector4 *footfall);

void world_update_collisions(struct simulator_ctx *sim, 
			     struct caj_phys_collision *collisions, int count);

  // for use by the physics engine only
void world_move_obj_from_phys(struct simulator_ctx *sim, struct world_obj *ob,
			      const caj_vector3 *new_pos);


// ---------- MISC WORLD STUFF ---------------------------------------

void world_insert_obj(struct simulator_ctx *sim, struct world_obj *ob);

void world_add_attachment(struct simulator_ctx *sim, struct avatar_obj *av, 
			  struct primitive_obj *prim, uint8_t attach_point);

void world_delete_prim(struct simulator_ctx *sim, struct primitive_obj *prim);
void world_delete_avatar(struct simulator_ctx *sim, struct avatar_obj *av);

// Should only be used for prims that haven't been added to the world.
// Otherwise, use world_delete_prim which frees the prim too. Also, doesn't 
// free child prims or remove the prim from its parent.
void world_free_prim(struct primitive_obj *prim);

struct world_obj* world_object_by_id(struct simulator_ctx *sim, uuid_t id);
struct world_obj* world_object_by_localid(struct simulator_ctx *sim, uint32_t id);

struct primitive_obj* world_get_root_prim(struct primitive_obj *prim);
struct primitive_obj* world_prim_by_link_id(struct simulator_ctx* sim, 
					    struct primitive_obj *prim, 
					    int link_num);

struct primitive_obj* world_begin_new_prim(struct simulator_ctx *sim);
struct inventory_item* world_prim_alloc_inv_item(void);
void world_send_chat(struct simulator_ctx *sim, struct chat_message* chat);

void world_chat_from_prim(struct simulator_ctx *sim, struct primitive_obj* prim,
			  int32_t chan, const char *msg, int chat_type);
void world_chat_from_user(struct user_ctx* ctx,
			  int32_t chan, const char *msg, int chat_type);
void world_prim_set_text(struct simulator_ctx *sim, struct primitive_obj* prim,
			 const char *text, uint8_t color[4]);
void world_set_script_evmask(struct simulator_ctx *sim, struct primitive_obj* prim,
			     void *script_priv, int evmask);
void prim_set_extra_params(struct primitive_obj *prim, const caj_string *params);
void prim_delete_extra_param(struct primitive_obj *prim, uint16_t param_type);
int prim_set_extra_param(struct primitive_obj *prim, uint16_t param_type,
			 const caj_string *param_data);

inventory_item* world_prim_find_inv(struct primitive_obj *prim, uuid_t item_id);
void world_prim_mark_inv_updated(struct primitive_obj *prim);
int world_prim_delete_inv(struct simulator_ctx *sim, struct primitive_obj *prim, 
			  uuid_t item_id);

// note that this function is interesting. USE WITH CARE!
void world_prim_set_inv(struct primitive_obj *prim, inventory_item** inv,
			int inv_count);

  // also an interesting one. FIXME - should be removed when we add support
  // for restoring OpenSim script states.
void world_prim_start_rezzed_script(struct simulator_ctx *sim, 
				    struct primitive_obj *prim, 
				    struct inventory_item *item);

  // don't ask. Really. Also, don't free or store returned string.
  char* world_prim_upd_inv_filename(struct primitive_obj* prim);

  // stuff for the scripting engine
void world_prim_apply_impulse(struct simulator_ctx *sim, struct primitive_obj* prim,
			      caj_vector3 impulse, int is_local);


void user_rez_script(struct user_ctx *ctx, struct primitive_obj *prim,
		     const char *name, const char *descrip, uint32_t flags,
		     const permission_flags *perms);

void world_prim_link(struct simulator_ctx *sim,  struct primitive_obj* main, 
		     struct primitive_obj* child);

  // FIXME - where should this go?
void user_send_script_dialog(user_ctx *ctx, primitive_obj* prim,
			     char *msg, int num_buttons, char** buttons,
			     int32_t channel);

// --- this is messy --------------------

#define CAJ_MULTI_UPD_POS 1
#define CAJ_MULTI_UPD_ROT 2
#define CAJ_MULTI_UPD_SCALE 4
#define CAJ_MULTI_UPD_LINKSET 8

struct caj_multi_upd {
  int flags;
  caj_vector3 pos;
  caj_quat rot;
  caj_vector3 scale;
};

void world_multi_update_obj(struct simulator_ctx *sim, struct world_obj *obj,
			    const struct caj_multi_upd *upd);

// ----- Code to handle llSetPrimitiveParams family --------
// Note that this is quite interesting to use. In particular, be
// *very* careful what you do between world_prim_spp_begin and 
// world_prim_spp_end, and don't forget to call world_prim_spp_end!

struct world_spp_ctx {
  struct simulator_ctx *sim;
  struct primitive_obj *prim;
  int objupd;
};

void world_prim_spp_begin(struct simulator_ctx *sim, 
			  struct primitive_obj *prim, 
			  struct world_spp_ctx *spp);
int world_prim_spp_set_shape(struct world_spp_ctx *spp, 
			     int shape, int hollow);
int world_prim_spp_set_profile_cut(struct world_spp_ctx *spp, 
				   float profile_begin, float profile_end);
int world_prim_spp_set_hollow(struct world_spp_ctx *spp, float hollow);
int world_prim_spp_set_twist(struct world_spp_ctx *spp, 
			     float twist_begin, float twist_end);
int world_prim_spp_set_path_taper(struct world_spp_ctx *spp, 
				  float top_size_x, float top_size_y,
				  float top_shear_x, float top_shear_y);
int world_prim_spp_remove_light(struct world_spp_ctx *spp);
int world_prim_spp_point_light(struct world_spp_ctx *spp, 
			       const caj_vector3* color, float intensity,
			       float radius, float falloff);
int world_prim_spp_set_material(struct world_spp_ctx *spp, int material);
int world_prim_spp_set_text(struct world_spp_ctx *spp,
			     const char *text, uint8_t color[4]);
void world_prim_spp_end(struct world_spp_ctx *spp);


#ifdef __cplusplus
}
#endif

#endif

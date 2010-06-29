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
#include "cajeput_world.h"
#include "cajeput_int.h"
#include "cajeput_prim.h"
#include "caj_script.h"
#include "caj_helpers.h"
#include "cajeput_user_glue.h"
#include <cassert>
#include <stdio.h>

struct script_info {
private:
  int m_refcnt;
public:
  void *priv;
  caj_string state;
  simulator_ctx *sim; // FIXME - sim and prim may be NULL!
  primitive_obj *prim;
  inventory_item *inv;

  script_info(simulator_ctx *sim_, primitive_obj *prim_, inventory_item *inv_)
    : m_refcnt(1), priv(NULL), sim(sim_), prim(prim_), inv(inv_) {
    state.len = 0; state.data = NULL;
  }

  ~script_info() {
    caj_string_free(&state);
  }

  void ref(void) { m_refcnt++; }
  void unref(void) {
    m_refcnt--;
    if(m_refcnt == 0) delete this;
  }
};

static void mark_deleted_obj_for_updates(simulator_ctx* sim, world_obj *obj);

static void world_update_global_pos_int(struct simulator_ctx *sim, struct world_obj *ob);
static void world_move_root_obj_int(struct simulator_ctx *sim, struct world_obj *ob,
				    const caj_vector3 &new_pos);

// --- START octree code ---

#define OCTREE_VERT_SCALE 64
#define OCTREE_HORIZ_SCALE 4
#define OCTREE_DEPTH 6
#define OCTREE_WIDTH (1<<OCTREE_DEPTH)

#if (OCTREE_WIDTH*OCTREE_HORIZ_SCALE) != WORLD_REGION_SIZE
#error World octree has bad horizontal size
#endif

#if (OCTREE_WIDTH*OCTREE_VERT_SCALE) != WORLD_HEIGHT
#error World octree has bad vertical size
#endif

#define OCTREE_MAGIC 0xc913e31cUL
#define OCTREE_LEAF_MAGIC 0x5b1ad072UL

#define OCTREE_CHECK_MAGIC(tree) assert(tree->magic == OCTREE_MAGIC);
#define OCTREE_LEAF_CHECK_MAGIC(leaf) assert(leaf->magic == OCTREE_LEAF_MAGIC);

// NB: not a C struct. In particular, I'm not sure the standard
// guarantees a pointer to the struct is the same as a pointer
// to its first member.
struct world_octree {
  uint32_t magic;
  struct world_octree* nodes[8];
  struct world_octree* parent;
  std::set<int32_t> chat_mask;

  world_octree(world_octree* pparent) : magic(OCTREE_MAGIC), parent(pparent) {
    for(int i = 0; i < 8; i++) nodes[i] = NULL;
    //memset(&nodes,0,sizeof(nodes));
  }
};

typedef std::multimap<int32_t,obj_chat_listener*> octree_chat_map;
typedef std::multimap<int32_t,obj_chat_listener*>::iterator octree_chat_map_iter;

struct world_ot_leaf {
  uint32_t magic;
  struct world_octree* parent;
  std::set<world_obj*> objects;
  octree_chat_map chat_map;
  world_ot_leaf(world_octree* pparent) : magic(OCTREE_LEAF_MAGIC), parent(pparent) {
  }
};

struct world_octree* world_octree_create() {
  //struct world_octree* ot = new world_octree();
  //memset(&ot->nodes,0,sizeof(ot->nodes));
  return new world_octree(NULL);
}

#define OCTREE_IDX(x,y,z,level) ((((x)>>(level))&1)<<2|(((y)>>(level))&1)<<1|(((z)>>(level))&1)<<0)

static void octree_coord_fix(int &x, int &y, int &z) {
  x /= OCTREE_HORIZ_SCALE;
  y /= OCTREE_HORIZ_SCALE;
  z /= OCTREE_VERT_SCALE;
  if(x >= OCTREE_WIDTH) x = OCTREE_WIDTH - 1;
  if(y >= OCTREE_WIDTH) y = OCTREE_WIDTH - 1;
  if(z >= OCTREE_WIDTH) z = OCTREE_WIDTH - 1;
  if(x < 0) x = 0;
  if(y < 0) y = 0;
  if(z < 0) z = 0;
}

struct world_ot_leaf* world_octree_find(struct world_octree* tree, int x, int y, int z, int add) {
  octree_coord_fix(x,y,z);
  for(int i = OCTREE_DEPTH - 1; i >= 0; i--) {
    struct world_octree** tp = tree->nodes+OCTREE_IDX(x,y,z,i);
    if(*tp == NULL) {
      if(!add) {
	return NULL;
      } else if(i > 0) {
	*tp = new world_octree(tree);
      } else {
	*tp = (world_octree*)new world_ot_leaf(tree);
      }
    }
    tree = *tp;
  }
  return (world_ot_leaf*) tree;
}

static void real_octree_destroy(struct world_octree* tree, int depth) {
  for(int i = 0; i < 8; i++) {
    struct world_octree* tp = tree->nodes[i];
    if(tp != NULL) {
      if(depth > 0) {
	real_octree_destroy(tp, depth-1);
      } else {
	delete (world_ot_leaf*)tp;
      }
    }
  }
  delete tree;
}

/* Note - for use in sim shutdown only. Please remove all objects first */
void world_octree_destroy(struct world_octree* tree) {
  real_octree_destroy(tree, OCTREE_DEPTH - 1);
}

void world_octree_insert(struct world_octree* tree, struct world_obj* obj) {
  struct world_ot_leaf* leaf = world_octree_find(tree, (int)obj->world_pos.x,
						 (int)obj->world_pos.y,
						 (int)obj->world_pos.z, true);
  leaf->objects.insert(obj);
}

void octree_add_chat(struct world_ot_leaf* leaf, int32_t channel,
		     struct obj_chat_listener *listen) {
  leaf->chat_map.insert(std::pair<int32_t,obj_chat_listener*>(channel,listen));
  for(world_octree *tree = leaf->parent; tree != NULL; tree = tree->parent) {
    std::set<int32_t>::iterator iter = tree->chat_mask.lower_bound(channel);
    if(iter != tree->chat_mask.end() && *iter == channel)
      break;
    tree->chat_mask.insert(iter, channel);
  }
}

void octree_del_chat(struct world_ot_leaf* leaf, int32_t channel,
		     struct obj_chat_listener *listen) {
  std::pair<octree_chat_map_iter,octree_chat_map_iter> span =
    leaf->chat_map.equal_range(channel);
  int count = 0;
  for(octree_chat_map_iter iter = span.first; iter != span.second;) {
    octree_chat_map_iter next = iter; next++;
    assert(next != iter);
    if(iter->second == listen) {
      leaf->chat_map.erase(iter);
    } else count++;
    iter = next;
  }

  if(count) return;
  struct world_octree* tree = leaf->parent;
  for(int i = 0; i < 8; i++) {
    leaf = (world_ot_leaf*)tree->nodes[i];
    if(leaf != NULL) {
      if(leaf->chat_map.find(channel) != leaf->chat_map.end())
	return;
    }
  }
  tree->chat_mask.erase(channel);
  tree = tree->parent;

  while(tree != NULL) {
    for(int i = 0; i < 8; i++) {
      struct world_octree* subtree = tree->nodes[i];
      if(subtree && subtree->chat_mask.find(channel) != subtree->chat_mask.end())
	return;
    }
    tree->chat_mask.erase(channel);
    tree = tree->parent;
    
  }


  // FIXME -  need to cleanup parent nodes
}

void world_octree_move(struct world_octree* tree, struct world_obj* obj,
		       const caj_vector3 &new_pos) {
  // FIXME - need to improve efficiency;
  struct world_ot_leaf* old_leaf = world_octree_find(tree, (int)obj->world_pos.x,
						     (int)obj->world_pos.y,
						     (int)obj->world_pos.z, false);
  
  struct world_ot_leaf* new_leaf = world_octree_find(tree, (int)new_pos.x,
						     (int)new_pos.y,
						     (int)new_pos.z, true);
  if(old_leaf == new_leaf) return;
  old_leaf->objects.erase(obj);
  new_leaf->objects.insert(obj);
  if(obj->chat != NULL) {
    for(std::set<std::pair<int32_t, obj_chat_listener*> >::iterator iter = 
	  obj->chat->channels.begin(); iter != obj->chat->channels.end();
	iter++) {
      octree_add_chat(new_leaf, iter->first, iter->second);
      octree_del_chat(old_leaf, iter->first, iter->second);
    }
  }
}

// FIXME - this and the move function need to clean up unused nodes
void world_octree_delete(struct world_octree* tree, struct world_obj* obj) {
  struct world_ot_leaf* leaf = world_octree_find(tree, (int)obj->world_pos.x,
						 (int)obj->world_pos.y,
						 (int)obj->world_pos.z, false);
  leaf->objects.erase(obj);
  if(obj->chat != NULL) {
    for(std::set<std::pair<int32_t, obj_chat_listener*> >::iterator iter = 
	  obj->chat->channels.begin(); iter != obj->chat->channels.end();
	iter++) {
      octree_del_chat(leaf, iter->first, iter->second);
    }
  }
}

static void real_octree_send_chat(struct simulator_ctx *sim, struct world_octree* tree, 
				  struct chat_message* chat,
				  float range, int depth) {
  OCTREE_CHECK_MAGIC(tree)
  if(tree->chat_mask.find(chat->channel) == tree->chat_mask.end())
    return;
  for(int i = 0; i < 8; i++) {
    struct world_octree* tp = tree->nodes[i];
    if(tp != NULL) {
      // FIXME - should filter out out-of-range octree nodes
      if(depth > 0) {
	real_octree_send_chat(sim, tp, chat, range, depth-1);
      } else {
        world_ot_leaf* leaf = (world_ot_leaf*)tp;
	OCTREE_LEAF_CHECK_MAGIC(leaf);
	std::pair<octree_chat_map_iter,octree_chat_map_iter> span =
	  leaf->chat_map.equal_range(chat->channel);
	// int count = 0; // ??? what was this meant for?
	for(octree_chat_map_iter iter = span.first; iter != span.second;iter++) {
	  obj_chat_listener* listen = iter->second;
	  if(caj_vect3_dist(&listen->obj->world_pos, &chat->pos) < range)
	    listen->callback(sim, listen->obj, chat, listen, listen->user_data);
	}
      }
    }
  }
}

void world_send_chat(struct simulator_ctx *sim, struct chat_message* chat) {
  float range = 40.0f;
  switch(chat->chat_type) {
  case CHAT_TYPE_WHISPER:
    range = 10.0f; break;
  case CHAT_TYPE_NORMAL: 
    range = 20.0f; break;
  case CHAT_TYPE_SHOUT:
    range = 100.0f; break;
  }
  printf("DEBUG: Sending chat message from %s @ (%f, %f, %f) range %f: %s\n",
	 chat->name, chat->pos.x, chat->pos.y, chat->pos.z,
	 range, chat->msg);
  real_octree_send_chat(sim,sim->world_tree,chat,range,OCTREE_DEPTH-1);
}

// ---- END of octree code ---

// FIXME - this actually isn't quite what we want.
static void object_compute_global_pos(struct world_obj *ob, caj_vector3 *pos_out) {
  if(ob->parent == NULL) {
    *pos_out = ob->local_pos;
  } else if(ob->parent->type == OBJ_TYPE_PRIM) {
    caj_vector3 offset;
    caj_mult_vect3_quat(&offset, &ob->parent->rot, &ob->local_pos);
    *pos_out = ob->parent->world_pos + offset;
  } else {
    // probably an attachment.
    *pos_out = ob->parent->world_pos;
  }
}

void world_chat_from_prim(struct simulator_ctx *sim, struct primitive_obj* prim,
			  int32_t chan, const char *msg, int chat_type) {
  struct chat_message chat;
  chat.channel = chan;
  chat.msg = (char*)msg; // FIXME - correct type?
  chat.chat_type = chat_type;
  chat.pos = prim->ob.world_pos;
  chat.name = prim->name;
  uuid_copy(chat.source,prim->ob.id);
  uuid_copy(chat.owner,prim->owner);
  chat.source_type = CHAT_SOURCE_OBJECT;

  if(chat_type == CHAT_TYPE_OWNER_SAY) {
    user_ctx *ctx = user_find_ctx(sim, prim->owner);
    if(ctx != NULL) {
      if(ctx->userh != NULL && ctx->userh->chat_callback != NULL)
	ctx->userh->chat_callback(ctx->user_priv, &chat);
    } else {
      printf("DEBUG: discarding llOwnerSay for absent user\n");
    }
  } else {
    world_send_chat(sim, &chat);
  }
}

void world_chat_from_user(struct user_ctx* ctx,
			  int32_t chan, const char *msg, int chat_type) {
  struct chat_message chat;
  if(ctx->av == NULL) return; // I have no mouth and I must scream...
  if(chat_type == CHAT_TYPE_WHISPER || chat_type == CHAT_TYPE_NORMAL ||
     chat_type == CHAT_TYPE_SHOUT) { 
    chat.channel = chan;
    chat.pos = ctx->av->ob.world_pos;
    chat.name = ctx->name;
    uuid_copy(chat.source,ctx->user_id);
    uuid_clear(chat.owner); // FIXME - ???
    chat.source_type = CHAT_SOURCE_AVATAR;
    chat.chat_type = chat_type;
    chat.msg = (char*)msg;
    world_send_chat(ctx->sim, &chat);
  }  
}


/* WARNING: do not call this until the object has been added to the octree.
   Seriously, just don't. It's not a good idea */
void world_obj_add_listen(struct simulator_ctx *sim, struct world_obj *ob,
			  int32_t channel, struct obj_chat_listener* listen) {
  if(ob->chat == NULL) {
    ob->chat = new obj_chat_listeners();
    ob->chat->obj = ob;
  }
  struct world_ot_leaf* leaf = world_octree_find(sim->world_tree, 
						 (int)ob->world_pos.x,
						 (int)ob->world_pos.y,
						 (int)ob->world_pos.z, false);
  assert(leaf != NULL);
  listen->obj = ob;
  ob->chat->channels.insert(std::pair<int, obj_chat_listener*>(channel, 
							       listen));
  octree_add_chat(leaf, channel, listen);
}


void world_obj_remove_listen(struct simulator_ctx *sim, struct world_obj *ob,
			     int32_t channel, struct obj_chat_listener* listen) {
  assert(ob->chat != NULL);
  struct world_ot_leaf* leaf = world_octree_find(sim->world_tree, 
						 (int)ob->world_pos.x,
						 (int)ob->world_pos.y,
						 (int)ob->world_pos.z, false);
  assert(leaf != NULL);
  ob->chat->channels.erase(std::pair<int, obj_chat_listener*>(channel, 
							       listen));
  octree_del_chat(leaf, channel, listen);
}

static world_obj *prim_find_listen_obj(primitive_obj* prim) {
  world_obj *cur = &prim->ob;
  while(cur->parent != NULL && cur->type != OBJ_TYPE_AVATAR)
    cur = cur->parent;
  return cur;
}

void world_script_add_listen(struct script_chat_listener *listen) {
  world_obj *root = prim_find_listen_obj(listen->prim);
  world_obj_add_listen(listen->sim, root, listen->chan, 
		       &listen->l);
}

void world_script_remove_listen(struct script_chat_listener *listen) {
  world_obj *root = prim_find_listen_obj(listen->prim);
  if(root->chat == NULL) return; // prim is probably being removed
  world_obj_remove_listen(listen->sim, root, listen->chan, 
			  &listen->l);
}

void world_add_attachment(struct simulator_ctx *sim, struct avatar_obj *av, 
			  struct primitive_obj *prim, uint8_t attach_point) {
  assert(attach_point < NUM_ATTACH_POINTS && attach_point != ATTACH_TO_LAST);

  prim->attach_point = attach_point;

  world_obj *ob = &prim->ob;
  ob->parent = &av->ob;
  sim->uuid_map.insert(std::pair<obj_uuid_t,world_obj*>(obj_uuid_t(ob->id),ob));
  ob->local_id = (uint32_t)random();
  object_compute_global_pos(ob, &ob->world_pos);
  //FIXME - generate local ID properly.
  sim->localid_map.insert(std::pair<uint32_t,world_obj*>(ob->local_id,ob));
  
  for(int i = 0; i < prim->num_children; i++) {
    world_insert_obj(sim, &prim->children[i]->ob);
  }

  // FIXME - save old attachment back to inventory
  if(av->attachments[attach_point] != NULL)
    world_delete_prim(sim, av->attachments[attach_point]);
  av->attachments[attach_point] = prim;

  world_mark_object_updated(sim, &av->ob, CAJ_OBJUPD_CHILDREN);
  world_mark_object_updated(sim, ob, CAJ_OBJUPD_CREATED);
}

void world_insert_obj(struct simulator_ctx *sim, struct world_obj *ob) {
  sim->uuid_map.insert(std::pair<obj_uuid_t,world_obj*>(obj_uuid_t(ob->id),ob));
  ob->local_id = (uint32_t)random();
  object_compute_global_pos(ob, &ob->world_pos);
  //FIXME - generate local ID properly.
  sim->localid_map.insert(std::pair<uint32_t,world_obj*>(ob->local_id,ob));
  world_octree_insert(sim->world_tree, ob);

  if(ob->type == OBJ_TYPE_PRIM) {
    primitive_obj *prim = (primitive_obj*)ob;
    for(int i = 0; i < prim->num_children; i++) {
      world_insert_obj(sim, &prim->children[i]->ob);
    }

    for(unsigned i = 0; i < prim->inv.num_items; i++) {
      inventory_item *inv = prim->inv.items[i];
      
      if(inv->inv_type == INV_TYPE_LSL && inv->spriv != NULL) {
	script_info *sinfo = (script_info*)inv->spriv;
	if(sinfo->state.len > 0 && sim->scripth.restore_script != NULL)
	  sinfo->priv = sim->scripth.restore_script(sim, sim->script_priv,
						    prim, inv, &sinfo->state);
      }
    }
  }

  world_mark_object_updated(sim, ob, CAJ_OBJUPD_CREATED);
}

static void world_remove_obj(struct simulator_ctx *sim, struct world_obj *ob) {
  sim->localid_map.erase(ob->local_id);
  world_octree_delete(sim->world_tree, ob);
  sim->physh.del_object(sim,sim->phys_priv,ob);
  mark_deleted_obj_for_updates(sim, ob);
  delete ob->chat; ob->chat = NULL;
}

struct world_obj* world_object_by_id(struct simulator_ctx *sim, const uuid_t id) {
  std::map<obj_uuid_t,world_obj*>::iterator iter = sim->uuid_map.find(id);
  if(iter == sim->uuid_map.end()) 
    return NULL;
  return iter->second;
}


struct world_obj* world_object_by_localid(struct simulator_ctx *sim, uint32_t id) {
  std::map<uint32_t,world_obj*>::iterator iter = sim->localid_map.find(id);
  if(iter == sim->localid_map.end()) 
    return NULL;
  return iter->second;
}

struct primitive_obj* world_get_root_prim(struct primitive_obj *prim) {
  while(prim->ob.parent != NULL && prim->ob.parent->type == OBJ_TYPE_PRIM) {
    prim = (primitive_obj*)prim->ob.parent;
  }
  return prim;
}

// NOTE: if you're adding new fields to prims and want them to be initialised
// properly, you *must* edit cajeput_dump.cpp as well as here, since it doesn't
// use world_begin_new_prim when revivifying loaded prims.
// You probably also need to update clone_prim below.
// Plus, you might want to update prim_from_os_xml (though it's not essential).
struct primitive_obj* world_begin_new_prim(struct simulator_ctx *sim) {
  struct primitive_obj *prim = new primitive_obj();
  memset(prim, 0, sizeof(struct primitive_obj));
  uuid_generate(prim->ob.id);
  prim->ob.type = OBJ_TYPE_PRIM;
  prim->ob.scale.x = prim->ob.scale.y = prim->ob.scale.z = 1.0f;
  prim->ob.rot.x = 0.0f; prim->ob.rot.y = 0.0f; prim->ob.rot.z = 0.0f; 
  prim->ob.rot.w = 1.0f; prim->crc_counter = 0;
  prim->profile_curve = PROFILE_SHAPE_SQUARE | PROFILE_HOLLOW_DEFAULT;
  prim->path_curve = PATH_CURVE_STRAIGHT;
  prim->path_scale_x = 100; prim->path_scale_y = 100;  
  prim->name = strdup("Object");
  prim->description = strdup("");
  prim->perms.next = prim->perms.current = prim->perms.base = PERM_FULL_PERMS;
  prim->perms.group = prim->perms.everyone = 0;
  prim->flags = 0; prim->caj_flags = 0; prim->ob.phys = NULL;
  prim->attach_point = 0;

  prim->ob.parent = NULL; prim->children = NULL;
  prim->num_children = 0;

  prim->inv.num_items = prim->inv.alloc_items = 0;
  prim->inv.items = NULL; prim->inv.serial = 0;
  prim->inv.filename = NULL;

  // tad hacky; the first byte is actually a count, not a terminating null
  caj_string_set_bin(&prim->extra_params, (const unsigned char*)"", 1); 

  prim->hover_text = strdup(""); 
  memset(prim->text_color, 0, sizeof(prim->text_color)); // technically redundant
  prim->sit_name = strdup(""); prim->touch_name = strdup("");
  prim->creation_date = time(NULL);

  prim->sit_target.x = 0.0f; prim->sit_target.y = 0.0f;
  prim->sit_target.z = 0.0f;
  prim->sit_rot.x = 0.0f; prim->sit_rot.y = 0.0f; prim->sit_rot.z = 0.0f;
  prim->sit_rot.w = 1.0f;
  prim->avatar_sitting = NULL;
  prim->num_avatars = 0;
  prim->avatars = NULL;
  return prim;
}

#define MAX_EXTRA_PARAMS_LEN 4096

// currently just a dumb wrapper, but I expect to smarten it up later
void prim_set_extra_params(struct primitive_obj *prim, const caj_string *params) {
  caj_string_free(&prim->extra_params);
  if(params->len < 1 || params->len > MAX_EXTRA_PARAMS_LEN) 
    caj_string_set_bin(&prim->extra_params, (const unsigned char*)"", 1);
  else caj_string_copy(&prim->extra_params, params);
}

// internal helper function.
static void copy_rm_extra_param(caj_string &ep, caj_string &new_ep,
				uint16_t param_type) {
  assert(ep.len >= 1);

  int count = ep.data[0], new_count = 0;
  int offset = 1, new_offset = 1;
  for(int i = 0; i < count; i++) {
    if(ep.len - offset < 6) break;
    uint16_t ptype = ep.data[offset] | ((uint16_t)ep.data[offset+1]<<8);
    uint32_t plen = caj_bin_to_uint32_le(ep.data+2);
    if(plen > (uint32_t)(ep.len - offset - 6)) break;
    if(ptype != param_type) {
      memcpy(new_ep.data+new_offset, ep.data+offset, plen+6);
      new_count++;
    }
    offset += 6 + plen;
  }

  new_ep.data[0] = new_count; new_ep.len = new_offset;
}

void prim_delete_extra_param(struct primitive_obj *prim, uint16_t param_type) {
  caj_string new_ep;
  assert( prim->extra_params.len >= 1);
  new_ep.len = 0; 
  new_ep.data = (unsigned char*)malloc(prim->extra_params.len);

  copy_rm_extra_param( prim->extra_params, new_ep, param_type);
  prim_set_extra_params(prim, &new_ep);
  free(new_ep.data);
}

int prim_set_extra_param(struct primitive_obj *prim, uint16_t param_type,
			 const caj_string *param_data) {
  if(param_data->len > MAX_EXTRA_PARAMS_LEN) return FALSE;
  assert( prim->extra_params.len >= 1);

  caj_string new_ep;
  new_ep.len = 0; 
  new_ep.data = (unsigned char*)malloc(prim->extra_params.len+param_data->len+6);
  copy_rm_extra_param(prim->extra_params, new_ep, param_type);

  new_ep.data[new_ep.len] = param_type & 0xff;
  new_ep.data[new_ep.len+1] = (param_type>>8) & 0xff;
  caj_uint32_to_bin_le(new_ep.data+new_ep.len+2, param_data->len);
  memcpy(new_ep.data+new_ep.len+6, param_data->data, param_data->len);
  new_ep.len += 6 + param_data->len;
  
  if(new_ep.len > MAX_EXTRA_PARAMS_LEN || new_ep.data[0] == 255) {
    printf("DEBUG: prim_set_extra_param: no more space!\n");
    free(new_ep.data); return FALSE;
  } else {
    new_ep.data[0]++;
    prim_set_extra_params(prim, &new_ep);
    free(new_ep.data); return TRUE;
  }
}

inventory_item* world_prim_alloc_inv_item(void) {
  inventory_item *inv = new inventory_item();
  inv->asset_hack = NULL; inv->spriv = NULL;
  return inv;
}

static void prim_free_inv_item(inventory_item *inv) {
  free(inv->name); free(inv->description); free(inv->creator_id);

  if(inv->asset_hack != NULL) {
    free(inv->asset_hack->name); free(inv->asset_hack->description);
    caj_string_free(&inv->asset_hack->data);
    delete inv->asset_hack;
  }
  delete inv;
}

void world_free_prim(struct primitive_obj *prim) {
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];
    prim_free_inv_item(inv);
  }
  free(prim->name); free(prim->description); free(prim->inv.filename);
  caj_string_free(&prim->tex_entry); caj_string_free(&prim->extra_params);
  free(prim->hover_text); free(prim->sit_name); free(prim->touch_name);
  free(prim->inv.items); free(prim->children); delete prim;
}

void world_delete_avatar(struct simulator_ctx *sim, struct avatar_obj *av) {
  if(av->ob.parent != NULL)
    world_unsit_avatar_now(sim, &av->ob);

  world_remove_obj(sim, &av->ob);

  for(int i = 0; i < NUM_ATTACH_POINTS; i++) {
    if(av->attachments[i] != NULL)
      world_delete_prim(sim, av->attachments[i]);
  }
  free(av);
}

static void linkset_unsit_all_avatars(struct simulator_ctx *sim, 
				   struct primitive_obj* prim) {
  // actually works on child prims too, not just linksets.

  while(prim->num_avatars > 0) 
    world_unsit_avatar_now(sim, prim->avatars[0]);
  
  if(prim->avatar_sitting != NULL)
    world_unsit_avatar_now(sim, prim->avatar_sitting);
}

void world_delete_prim(struct simulator_ctx *sim, struct primitive_obj *prim) {
  linkset_unsit_all_avatars(sim, prim);

  world_remove_obj(sim, &prim->ob);

  if(prim->children != NULL) {
    // tad inefficient, this - actually O(num_children^2)
    for(int i = prim->num_children - 1; i >= 0; i--) {
      world_delete_prim(sim, prim->children[i]);
    }
  }

  if(prim->ob.parent != NULL) {
    assert(prim->ob.parent->type == OBJ_TYPE_PRIM || 
	   prim->ob.parent->type == OBJ_TYPE_AVATAR); // FIXME?
    if(prim->ob.parent->type == OBJ_TYPE_PRIM) {
      primitive_obj *parent = (primitive_obj*) prim->ob.parent;
      for(int i = 0; i < parent->num_children; i++) {
	if(parent->children[i] == prim) {
	  parent->num_children--;
	  for( ; i < parent->num_children; i++)
	    parent->children[i] = parent->children[i+1];
	}
      }
    } else if(prim->ob.parent->type == OBJ_TYPE_AVATAR) {
      avatar_obj *av = (avatar_obj*) prim->ob.parent;
      assert(prim->attach_point != 0 && prim->attach_point < NUM_ATTACH_POINTS);
      assert(av->attachments[prim->attach_point] == prim);
      av->attachments[prim->attach_point] = NULL;
    }
  }

  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->spriv != NULL) {
      script_info *sinfo = (script_info*)inv->spriv;
      if(sinfo->priv != NULL)
	sim->scripth.kill_script(sim, sim->script_priv, sinfo->priv);
      sinfo->inv = NULL; sinfo->unref(); inv->spriv = NULL;
    }
  }
  
  world_free_prim(prim);
}

static void prim_disable_listens(struct simulator_ctx *sim, 
			  struct primitive_obj* prim) {
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->spriv != NULL) {
      script_info *sinfo = (script_info*)inv->spriv;
      if(sinfo->priv != NULL && sim->scripth.disable_listens != NULL)
	sim->scripth.disable_listens(sim, sim->script_priv, sinfo->priv);
    }
  }

  for(unsigned i = 0; i < prim->num_children; i++) {
    prim_disable_listens(sim, prim->children[i]);
  }
}

static void prim_reenable_listens(struct simulator_ctx *sim, 
			  struct primitive_obj* prim) {
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->spriv != NULL) {
      script_info *sinfo = (script_info*)inv->spriv;
      if(sinfo->priv != NULL && sim->scripth.reenable_listens != NULL)
	sim->scripth.reenable_listens(sim, sim->script_priv, sinfo->priv);
    }
  }

  for(unsigned i = 0; i < prim->num_children; i++) {
    prim_reenable_listens(sim, prim->children[i]);
  }
}

void world_prim_link(struct simulator_ctx *sim,  struct primitive_obj* main, 
		     struct primitive_obj* child) {
  if(main->ob.parent != NULL || child->ob.parent != NULL || child->num_children != 0) {
    printf("FIXME - can't handle this prim linking case");
    return;
  }
  if(main->num_children > 255) {
    printf("ERROR: tried linking prim with too many children\n");
    return;
  }

  // any avatars sitting on the child linkset must be unsat, since avatars 
  // must not have a child prim as their parent object.
  linkset_unsit_all_avatars(sim, child);

  main->children = (primitive_obj**)realloc(main->children, 
			   sizeof(primitive_obj*)*(main->num_children+1));
  main->children[main->num_children++] = child;
  // need to move all listeners to the new root prim
  prim_disable_listens(sim, child);
  child->ob.parent = &main->ob;
  prim_reenable_listens(sim, child);

  world_mark_object_updated(sim, &child->ob, CAJ_OBJUPD_PARENT);
  world_mark_object_updated(sim, &main->ob, CAJ_OBJUPD_CHILDREN);
  
  // FIXME - this isn't quite right. Need to handle rotation
  child->ob.local_pos.x -= main->ob.local_pos.x;
  child->ob.local_pos.y -= main->ob.local_pos.y;
  child->ob.local_pos.z -= main->ob.local_pos.z;
  
}

static bool calc_sit_by_target(struct primitive_obj *seat,
			       struct caj_sit_info *info_out, bool &has_sit_target) {
  if(seat->sit_target.x != 0.0f || seat->sit_target.y != 0.0f || 
     seat->sit_target.z != 0.0f) {
    has_sit_target = true;
    if(seat->avatar_sitting == NULL) {
      uuid_copy(info_out->target, seat->ob.id);
      info_out->offset = seat->sit_target;
      info_out->rot = seat->sit_rot;
      return TRUE;
    }
  }
  return FALSE;
}

int world_avatar_begin_sit(struct simulator_ctx *sim, struct world_obj *av,
			   struct primitive_obj *seat, struct caj_sit_info *info_out) {
  bool has_sit_target = false;

  assert(av->type == OBJ_TYPE_AVATAR);
  primitive_obj *root = world_get_root_prim(seat);

  if(av->parent != NULL) return FALSE;

  if(calc_sit_by_target(root, info_out, has_sit_target))
    return TRUE;

  for(int i = 0; i < root->num_children; i++) {
    if(calc_sit_by_target(root->children[i], info_out, has_sit_target))
      return TRUE;
  }

  // if any prim on the linkset has a sit target, we can't then go and sit on 
  // a prim without one
  if(has_sit_target) return FALSE;

  // Finally, fall back to the original chosen prim.
  uuid_copy(info_out->target, seat->ob.id);
  // note that the caller is expected to fill in info_out->offset appropriately!
  // FIXME - calculate it more accurately ourselves, if possible.
  info_out->rot.x = 0.0f; info_out->rot.y = 0.0f; info_out->rot.z = 0.0f; 
  info_out->rot.w = 1.0f;
  return TRUE;
}

// NOTE: can't safely be used on its own until avatar update code is cleaned up.
// Use user_complete_sit instead!
int world_avatar_complete_sit(struct simulator_ctx *sim, struct world_obj *av,
			      const struct caj_sit_info *info) {
  assert(av->type == OBJ_TYPE_AVATAR);
  if(av->parent != NULL) return FALSE;

  struct world_obj *obj = world_object_by_id(sim, info->target);
  if(obj == NULL || obj->type != OBJ_TYPE_PRIM)
    return FALSE;
  primitive_obj *prim = (primitive_obj*)obj;
  primitive_obj *root = world_get_root_prim(prim);
  
  if(prim->avatar_sitting != NULL) return FALSE;

  prim->avatar_sitting = av; 

  root->avatars = (world_obj**)realloc(root->avatars, sizeof(world_obj*) *
				       (root->num_avatars+1));
  root->avatars[root->num_avatars++] = av;
  av->parent = &root->ob;
  ((avatar_obj*)av)->sitting_on = prim;
  // FIXME - the position calculation is actually more complex than this.
  av->local_pos = info->offset + (prim->ob.world_pos - root->ob.world_pos);
  av->rot = info->rot; // FIXME - rotation calculation here is wrong!

  world_update_global_pos_int(sim, av);

  world_mark_object_updated(sim, &root->ob, CAJ_OBJUPD_AVATARS);
  world_mark_object_updated(sim, av, CAJ_OBJUPD_PARENT | CAJ_OBJUPD_POSROT);
  world_mark_object_updated(sim, &prim->ob, CAJ_OBJUPD_AV_ON_SEAT);
  return TRUE;
}

void world_unsit_avatar_now(struct simulator_ctx *sim, struct world_obj *av) {
  struct avatar_obj *real_av = (avatar_obj*)av;
  assert(av->type == OBJ_TYPE_AVATAR);
  if(av->parent == NULL) {
    assert(real_av->sitting_on == NULL);
    return;
  }

  assert(real_av->sitting_on != NULL);
  primitive_obj *seat = real_av->sitting_on;
  primitive_obj *root = world_get_root_prim(seat);
  
  assert(seat->avatar_sitting == av);
  assert(av->parent == &root->ob);
  caj_vector3 new_pos;
  object_compute_global_pos(av, &new_pos);
  new_pos.z += 0.5f;
  //world_update_global_pos_int(sim, av);
  seat->avatar_sitting = NULL;
  av->parent = NULL; 
  //av->local_pos = av->world_pos;
  world_move_root_obj_int(sim, av, new_pos);
  
  int i;
  for(i = 0; i < root->num_avatars; i++) {
    if(root->avatars[i] == av) {
      for(int j = i; j < root->num_avatars-1; j++)
	root->avatars[j] = root->avatars[j+1];
      break;
    }
  }
  assert(i < root->num_avatars);
  root->num_avatars--;

  world_mark_object_updated(sim, av, CAJ_OBJUPD_PARENT); // FIXME
  world_mark_object_updated(sim, &root->ob, CAJ_OBJUPD_AVATARS);
  world_mark_object_updated(sim, &seat->ob, CAJ_OBJUPD_AV_ON_SEAT);

  // FIXME - HACK
  {
    struct user_ctx *ctx = user_find_ctx(sim, av->id);
    if(ctx != NULL) ctx->flags |= AGENT_FLAG_AV_FULL_UPD;
  }
}

int world_unsit_avatar_via_script(struct simulator_ctx *sim,
				  struct primitive_obj *src_prim,
				  uuid_t avatar_id) {
  world_obj *avatar = world_object_by_id(sim, avatar_id);
  if(avatar == NULL || avatar->type != OBJ_TYPE_AVATAR ||
     avatar->parent == NULL)
    return FALSE;
  src_prim = world_get_root_prim(src_prim);
  // FIXME - should also allow unsit if avatar is over land owned 
  // by the prim owner. 
  if(&src_prim->ob != avatar->parent)
    return FALSE;
  world_unsit_avatar_now(sim, avatar);
  return TRUE;
}

char* world_prim_upd_inv_filename(struct primitive_obj* prim) {
  if(prim->inv.filename != NULL) return prim->inv.filename;

  // knowing LL, they probably rely on the exact format of this somewhere.
  
  char buf[40]; uuid_t u;
  uuid_generate_random(u);   uuid_unparse_lower(u, buf);
  prim->inv.filename = (char*)malloc(51);
  snprintf(prim->inv.filename, 51, "inventory_%s.tmp", buf);
  return prim->inv.filename;
}

void world_prim_set_text(struct simulator_ctx *sim, struct primitive_obj* prim,
			 const char *text, uint8_t color[4]) {
  int len = strlen(text); if(len > 254) len = 254;
  free(prim->hover_text); prim->hover_text = (char*)malloc(len+1);
  memcpy(prim->hover_text, text, len); prim->hover_text[len] = 0;
  memcpy(prim->text_color, color, sizeof(prim->text_color));
  world_mark_object_updated(sim, &prim->ob, CAJ_OBJUPD_TEXT);
}

void world_set_script_evmask(struct simulator_ctx *sim, struct primitive_obj* prim,
			     void *script_priv, int evmask) {
  int prim_evmask = 0;
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];
    if(inv->inv_type == INV_TYPE_LSL && inv->asset_type == ASSET_LSL_TEXT 
       && inv->spriv != NULL) {
      script_info *sinfo = (script_info*)inv->spriv;
      if(sinfo->priv != NULL)
	prim_evmask |= sim->scripth.get_evmask(sim, sim->script_priv, sinfo->priv);
    }
  }
  
  uint32_t newflags = prim->flags & ~(uint32_t)PRIM_FLAG_TOUCH;
  if(prim_evmask & (CAJ_EVMASK_TOUCH|CAJ_EVMASK_TOUCH_CONT)) {
    newflags |= PRIM_FLAG_TOUCH;
  }
  if(newflags != prim->flags) {
    prim->flags = newflags;
    world_mark_object_updated(sim, &prim->ob, CAJ_OBJUPD_FLAGS);
  }
}

void user_prim_touch(struct user_ctx *ctx, struct primitive_obj* prim, 
		     int touch_type, const struct caj_touch_info *info) {
  struct simulator_ctx *sim = ctx->sim;
  printf("DEBUG: in user_prim_touch, type %i\n", touch_type);
  int handled = 0;

  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->asset_type == ASSET_LSL_TEXT && inv->spriv != NULL &&
       ((script_info*)inv->spriv)->priv != NULL) {
      script_info* sinfo = (script_info*)inv->spriv;
      int evmask = sim->scripth.get_evmask(sim, sim->script_priv,
					   sinfo->priv);

      if(evmask & (CAJ_EVMASK_TOUCH_CONT | CAJ_EVMASK_TOUCH)) {
	handled = 1; // any touch handler will do for this

	// we leave finer-grained filtering to the script engine
	printf("DEBUG: sending touch event %i to script\n", touch_type);
	sim->scripth.touch_event(sim, sim->script_priv, sinfo->priv,
				 ctx, &ctx->av->ob, touch_type, info);
      } else {
	 printf("DEBUG: ignoring script not interested in touch event\n");
      }
    }
  }
  if(prim->ob.parent != NULL && prim->ob.parent->type == OBJ_TYPE_PRIM &&
     !handled) {
    printf("DEBUG: passing touch event to parent prim\n");
    user_prim_touch(ctx, (primitive_obj*)prim->ob.parent, 
		    touch_type, info);
  }
}

void world_prim_mark_inv_updated(struct primitive_obj *prim) {
  prim->inv.serial++; free(prim->inv.filename); prim->inv.filename = NULL;
  
}

static void prim_add_inventory(struct primitive_obj *prim, inventory_item *inv) {
  if(prim->inv.num_items >= prim->inv.alloc_items) {
    prim->inv.alloc_items = prim->inv.alloc_items == 0 ? 8 : prim->inv.alloc_items*2;
    prim->inv.items = (inventory_item**)realloc(prim->inv.items, 
						prim->inv.alloc_items*sizeof(inventory_item**));
    if(prim->inv.items == NULL) abort();
  }

  prim->inv.items[prim->inv.num_items++] = inv;
  world_prim_mark_inv_updated(prim);
}

void world_prim_set_inv(struct primitive_obj *prim, inventory_item** inv,
			int inv_count) {
  assert(prim->inv.num_items == 0 && prim->inv.items == NULL);
  if(inv_count <= 0) return;

  prim->inv.items = (inventory_item**)calloc(inv_count,
					     sizeof(inventory_item**));
  if(prim->inv.items == NULL) return;

  prim->inv.alloc_items = prim->inv.num_items = inv_count;
  for(int i = 0; i < inv_count; i++) {
    prim->inv.items[i] = inv[i];
  }
  prim->inv.serial++;
  // no update, since this is intended for objects not added to the world
  // yet.
}

inventory_item* world_prim_find_inv(struct primitive_obj *prim, uuid_t item_id) {
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];
    if(uuid_compare(inv->item_id, item_id) == 0) return inv;
  }
  return NULL;
}

int world_prim_delete_inv(struct simulator_ctx *sim, struct primitive_obj *prim, 
			  uuid_t item_id) {
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];
    if(uuid_compare(inv->item_id, item_id) == 0) {
      if(inv->spriv != NULL) {
	script_info* sinfo = (script_info*)inv->spriv;
	if(sinfo->priv != NULL)
	  sim->scripth.kill_script(sim, sim->script_priv, sinfo->priv);
	sinfo->inv = NULL; sinfo->unref(); inv->spriv = NULL;
      }

      prim_free_inv_item(inv);
      prim->inv.num_items--;
      for(/* carry on */; i < prim->inv.num_items; i++) {
	prim->inv.items[i] = prim->inv.items[i+1];
      }

      world_prim_mark_inv_updated(prim);
      return TRUE;
    }
  } 
  return FALSE;
}

struct script_update_cb_hack {
  compile_done_cb cb; void *cb_priv;
  script_update_cb_hack(compile_done_cb cb_, void *cb_priv_) : 
    cb(cb_), cb_priv(cb_priv_) { }
};

// evil hack. Note that this is totally untested, YMMV.
gboolean script_update_cb_hack_f(gpointer data) {
  script_update_cb_hack* hack = (script_update_cb_hack*)data;
  hack->cb(hack->cb_priv,0,"No scripting engine",19);
  delete hack; return FALSE;
}

inventory_item* prim_update_script(struct simulator_ctx *sim, struct primitive_obj *prim,
				   uuid_t item_id, int script_running,
				   unsigned char *data, int data_len,
				   compile_done_cb cb, void *cb_priv) {
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    if(uuid_compare(prim->inv.items[i]->item_id, item_id) == 0) {
      inventory_item *inv = prim->inv.items[i];
      if(inv->asset_type != ASSET_LSL_TEXT) {
	printf("ERROR: attempt to update a script, but item isn't a script!\n");
	return NULL;
      }
      script_info* sinfo = (script_info*)inv->spriv;
      if(sinfo == NULL) {
	sinfo = new script_info(sim, prim, inv); sinfo->priv = NULL;
	inv->spriv = sinfo;
      }
      if(sinfo->priv != NULL) {
	sim->scripth.kill_script(sim, sim->script_priv, sinfo->priv);
	sinfo->priv = NULL;
      }

      uuid_generate(inv->asset_id); // changed by update

      if(inv->asset_hack == NULL) {
	simple_asset *asset = new simple_asset();
	asset->name = strdup(inv->name); 
	asset->description = strdup(inv->description);
	asset->type = ASSET_LSL_TEXT;
	asset->data.len = 0; asset->data.data = NULL;
	inv->asset_hack = asset;
      }
      uuid_copy(inv->asset_hack->id, inv->asset_id);
      caj_string_free(&inv->asset_hack->data);
      caj_string_set_bin(&inv->asset_hack->data, data, data_len);

      if(sim->scripth.add_script != NULL) {
	sinfo->priv = sim->scripth.add_script(sim, sim->script_priv, prim, inv,
					      inv->asset_hack, cb, cb_priv);
      } else {
	// we need to call the callback somehow, but there's an 
	// ordering issue - the caller has to fill in the asset ID first
	g_timeout_add(1, script_update_cb_hack_f, 
		      new script_update_cb_hack(cb,cb_priv));
      }
      return inv;
    }
  }
  return NULL;
}

// FIXME - need to make the item name unique!!
void user_rez_script(struct user_ctx *ctx, struct primitive_obj *prim,
		     const char *name, const char *descrip, uint32_t flags,
		     const permission_flags *perms) {
  inventory_item *inv = NULL;

  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    if(strcmp(name, prim->inv.items[i]->name) == 0) {
      inv = prim->inv.items[i];
    }
  }
  if(inv != NULL) {
    printf("FIXME: item name clash in user_rez_script");
    user_send_message(ctx, "FIXME: item name clash in user_rez_script");
    return;
  }

  inv = world_prim_alloc_inv_item();
  
  inv->name = strdup(name); inv->description = strdup(descrip);

  // FIXME - this isn't quite right...
  uuid_copy(inv->folder_id, prim->ob.id);
  uuid_copy(inv->owner_id, ctx->user_id);
  uuid_copy(inv->creator_as_uuid, ctx->user_id);
  
  uuid_generate(inv->item_id);
  uuid_generate(inv->asset_id);

  inv->creator_id = (char*)malloc(40); uuid_unparse(ctx->user_id, inv->creator_id);

  inv->perms = *perms;

  printf("DEBUG: rez script perms base 0x%x owner 0x%x group 0x%x everyone 0x%x next 0x%x\n",
	 perms->base, perms->current, perms->group, perms->everyone, perms->next);

  inv->asset_type = ASSET_LSL_TEXT; 
  inv->inv_type = INV_TYPE_LSL;
  inv->sale_type = 0; // FIXME
  inv->group_owned = 0; // FIXME - set from message?
  inv->sale_price = 0;
  inv->creation_date = time(NULL);
  inv->flags = flags;

  // FIXME - bit iffy here
  simple_asset *asset = new simple_asset();
  asset->name = strdup(name); 
  asset->description = strdup(descrip);
  uuid_copy(asset->id, inv->asset_id);
  asset->type = ASSET_LSL_TEXT;
  caj_string_set(&asset->data, "default\n{\n  state_entry() {\n    llSay(0, \"Script running\");\n  }\n}\n");
  inv->asset_hack = asset;

  script_info* sinfo = new script_info(ctx->sim, prim, inv);
  sinfo->priv = NULL; inv->spriv = sinfo;

  prim_add_inventory(prim, inv);
  
  // FIXME - check whether it should be created running...
  if(ctx->sim->scripth.add_script != NULL) {
    sinfo->priv = ctx->sim->scripth.add_script(ctx->sim, ctx->sim->script_priv, 
					     prim, inv, asset, NULL, NULL);
  } else {
    sinfo->priv = NULL;
  }
}

static void start_script_asset_cb(struct simgroup_ctx *sgrp, void *priv,
				  struct simple_asset *asset) {
  script_info* sinfo = (script_info*)priv;
  if(asset != NULL && sinfo->inv != NULL && sinfo->priv == NULL &&
     uuid_compare(asset->id, sinfo->inv->asset_id) == 0) {
    sinfo->priv = sinfo->sim->scripth.add_script(sinfo->sim, 
						 sinfo->sim->script_priv, 
						 sinfo->prim, sinfo->inv, 
						 asset, NULL, NULL);
  }
  sinfo->unref();
}

void world_prim_start_rezzed_script(struct simulator_ctx *sim, 
				    struct primitive_obj *prim, 
				    struct inventory_item *item) {
  assert(item->inv_type == INV_TYPE_LSL);
  script_info* sinfo = (script_info*) item->spriv;
  if(sinfo == NULL) {
    sinfo = new script_info(sim, prim, item);
    sinfo->priv = NULL; item->spriv = sinfo;
  }
  
  assert(sinfo->priv == NULL);

  if(sim->scripth.add_script != NULL) {
    sinfo->ref(); sinfo->sim = sim; sinfo->prim = prim;
    caj_get_asset(sim->sgrp, item->asset_id, start_script_asset_cb, sinfo);
  }
}

void world_save_script_state(simulator_ctx *sim, inventory_item *inv,
			     caj_string *out) {
  script_info* sinfo = (script_info*)inv->spriv;
  if(sinfo == NULL || sinfo->priv == NULL || 
     sim->scripth.save_script == NULL) {
    out->data = NULL; out->len = 0; return;
  }

  sim->scripth.save_script(sim, sim->script_priv, sinfo->priv, out);
}

void world_load_script_state(inventory_item *inv, caj_string *state) {
  assert(inv->spriv == NULL);
  assert(inv->inv_type == INV_TYPE_LSL);
  script_info *sinfo = new script_info(NULL, NULL, inv);
  caj_string_steal(&sinfo->state, state);
  inv->spriv = sinfo;
}

static void world_move_root_obj_int(struct simulator_ctx *sim, struct world_obj *ob,
			     const caj_vector3 &new_pos) {
  assert(ob->parent == NULL);
  world_octree_move(sim->world_tree, ob, new_pos);
  ob->local_pos = new_pos; ob->world_pos = new_pos;
}

static void world_update_global_pos_int(struct simulator_ctx *sim, struct world_obj *ob) {
  caj_vector3 new_pos;
  object_compute_global_pos(ob, &new_pos);
  world_octree_move(sim->world_tree, ob, new_pos);
  ob->world_pos = new_pos;
}

static void world_move_obj_local_int(struct simulator_ctx *sim, struct world_obj *ob,
				     const caj_vector3 &new_pos) {
  ob->local_pos = new_pos;
  world_update_global_pos_int(sim, ob);
}

// FIXME - validate position, rotation, scale!
// FIXME - handle the LINKSET flags correctly.
void world_multi_update_obj(struct simulator_ctx *sim, struct world_obj *obj,
			    const struct caj_multi_upd *upd) {
  if(upd->flags & CAJ_MULTI_UPD_POS) {
    if(!(upd->flags & CAJ_MULTI_UPD_LINKSET) && obj->type == OBJ_TYPE_PRIM) {
      primitive_obj *prim = (primitive_obj*)obj;
      caj_vector3 delt =  obj->local_pos - upd->pos;
      caj_quat invrot; 
      invrot.x = -obj->rot.x; invrot.y = -obj->rot.y;
      invrot.z = -obj->rot.z; invrot.w = obj->rot.w;
      caj_mult_vect3_quat(&delt, &invrot, &delt);
      for(int i = 0; i < prim->num_children; i++) {
	// Shouldn't be setting this directly. FIXME
	prim->children[i]->ob.local_pos = prim->children[i]->ob.local_pos + delt;
	world_mark_object_updated(sim, &prim->children[i]->ob, CAJ_OBJUPD_POSROT);
      }
    }
    world_move_obj_local_int(sim, obj, upd->pos);
  }
  if(upd->flags & CAJ_MULTI_UPD_ROT) {
    if(!(upd->flags & CAJ_MULTI_UPD_LINKSET) && obj->type == OBJ_TYPE_PRIM) {
      primitive_obj *prim = (primitive_obj*)obj;
      caj_quat invrot; 
      invrot.x = -obj->rot.x; invrot.y = -obj->rot.y;
      invrot.z = -obj->rot.z; invrot.w = obj->rot.w;
      for(int i = 0; i < prim->num_children; i++) {
	// Shouldn't be setting this directly. FIXME
	caj_mult_vect3_quat(&prim->children[i]->ob.local_pos, &invrot,
			    &prim->children[i]->ob.local_pos);
	caj_mult_vect3_quat(&prim->children[i]->ob.local_pos, &upd->rot,
			    &prim->children[i]->ob.local_pos);
	caj_mult_quat_quat(&prim->children[i]->ob.rot, 
			   &prim->children[i]->ob.rot, &invrot);
	caj_mult_quat_quat(&prim->children[i]->ob.rot, 
			   &prim->children[i]->ob.rot, &upd->rot);
	world_mark_object_updated(sim, &prim->children[i]->ob, CAJ_OBJUPD_POSROT);
      }
    }
    obj->rot = upd->rot;
  }
  if(upd->flags & CAJ_MULTI_UPD_SCALE) {
    if((upd->flags & CAJ_MULTI_UPD_LINKSET) && obj->type == OBJ_TYPE_PRIM) {
      // FIXME - scale child prims
    }
    obj->scale = upd->scale;
  }

  if(obj->type == OBJ_TYPE_PRIM) {
      primitive_obj *prim = (primitive_obj*)obj;
      for(int i = 0; i < prim->num_children; i++) {
	world_update_global_pos_int(sim, &prim->children[i]->ob);
      }

      for(int i = 0; i < prim->num_avatars; i++) {
	world_update_global_pos_int(sim, prim->avatars[i]);
      }
  }

  int objupd = 0;
  if(upd->flags & CAJ_MULTI_UPD_SCALE)
    objupd |= CAJ_OBJUPD_SCALE;
  if(upd->flags & (CAJ_MULTI_UPD_POS|CAJ_MULTI_UPD_ROT))
     objupd |= CAJ_OBJUPD_POSROT;
  world_mark_object_updated(sim, obj, objupd);
}

// ----- Code to handle llSetPrimitiveParams family --------
// Note that this is quite interesting to use and comes with 
// major caveats!


void world_prim_spp_begin(struct simulator_ctx *sim, 
			  struct primitive_obj *prim, 
			  struct world_spp_ctx *spp) {
  spp->sim = sim; spp->prim = prim; spp->objupd = 0;
}


int world_prim_spp_set_shape(struct world_spp_ctx *spp, 
			     int shape, int hollow) {
  switch(shape) {
  case PRIM_TYPE_BOX:
    spp->prim->profile_curve = PROFILE_SHAPE_SQUARE | 
      (PROFILE_HOLLOW_MASK & hollow);
    spp->prim->path_curve = PATH_CURVE_STRAIGHT;
    break;
  case PRIM_TYPE_CYLINDER:
    spp->prim->profile_curve = PROFILE_SHAPE_CIRCLE | 
      (PROFILE_HOLLOW_MASK & hollow);
    spp->prim->path_curve = PATH_CURVE_STRAIGHT;
    break;
  case PRIM_TYPE_PRISM:
    spp->prim->profile_curve = PROFILE_SHAPE_EQUIL_TRI | 
      (PROFILE_HOLLOW_MASK & hollow);
    spp->prim->path_curve = PATH_CURVE_STRAIGHT;
    break;
  case PRIM_TYPE_SPHERE:
    spp->prim->profile_curve = PROFILE_SHAPE_SEMICIRC | 
      (PROFILE_HOLLOW_MASK & hollow);
    spp->prim->path_curve = PATH_CURVE_CIRCLE;
    break;
  case PRIM_TYPE_TORUS:
    spp->prim->profile_curve = PROFILE_SHAPE_CIRCLE | 
      (PROFILE_HOLLOW_MASK & hollow);
    spp->prim->path_curve = PATH_CURVE_CIRCLE;
    break;
  case PRIM_TYPE_TUBE:
    spp->prim->profile_curve = PROFILE_SHAPE_SQUARE | 
      (PROFILE_HOLLOW_MASK & hollow);
    spp->prim->path_curve = PATH_CURVE_CIRCLE;
    break;
  case PRIM_TYPE_RING:
    spp->prim->profile_curve = PROFILE_SHAPE_EQUIL_TRI | 
      (PROFILE_HOLLOW_MASK & hollow);
    spp->prim->path_curve = PATH_CURVE_CIRCLE;
    break;
  default: return FALSE;
  }
  spp->objupd |= CAJ_OBJUPD_SHAPE;
  return TRUE;
}

int world_prim_spp_set_profile_cut(struct world_spp_ctx *spp, 
				   float profile_begin, float profile_end) {
  if(!isfinite(profile_begin) || profile_begin < 0.0f) {
    profile_begin = 0.0f;
  }
  if(profile_begin > 0.95f) profile_begin =  0.95f;
  if(!isfinite(profile_end))
    profile_end = 1.0f;
  if(profile_end < profile_begin+0.05f) 
    profile_end = profile_begin+0.05f;
  if(profile_end > 1.00f) profile_end = 1.00f;

  spp->prim->profile_begin = (int)(50000*profile_begin);
  spp->prim->profile_end = (int)(50000*(1.0f-profile_end));
  spp->objupd |= CAJ_OBJUPD_SHAPE;
  return TRUE;
}


int world_prim_spp_set_hollow(struct world_spp_ctx *spp, float hollow) {
  if(!isfinite(hollow) || hollow < 0.0f) {
    hollow = 0.0f;
  }
  if(hollow > 0.95f) hollow =  0.95f;
  spp->prim->profile_hollow = (int)(50000*hollow);
  spp->objupd |= CAJ_OBJUPD_SHAPE;
  return TRUE;
}

int world_prim_spp_set_twist(struct world_spp_ctx *spp, 
			     float twist_begin, float twist_end) {
  // FIXME - limit correctly for box/cylinder/prism
  if(!isfinite(twist_begin)) twist_begin = 0.0f;
  if(twist_begin < -1.0f) twist_begin = -1.0f;
  if(twist_begin > 1.0f) twist_begin = 1.0f;

  if(!isfinite(twist_end)) twist_end = 0.0f;
  if(twist_end < -1.0f) twist_end = -1.0f;
  if(twist_end > 1.0f) twist_end = 1.0f;

  // the explicit cast to int is critical!
  spp->prim->path_twist_begin = (int)(twist_begin*100);
  spp->prim->path_twist = (int)(twist_end*100);
  spp->objupd |= CAJ_OBJUPD_SHAPE;
  return TRUE;
}

int world_prim_spp_set_path_taper(struct world_spp_ctx *spp, 
				  float top_size_x, float top_size_y,
				  float top_shear_x, float top_shear_y) {
  if(!isfinite(top_shear_x)) top_shear_x = 0.0f;
  if(top_shear_x < -0.5f) top_shear_x = -0.5f;
  if(top_shear_x > 0.5f) top_shear_x = 0.5f;

  if(!isfinite(top_shear_y)) top_shear_y = 0.0f;
  if(top_shear_y < -0.5f) top_shear_y = -0.5f;
  if(top_shear_y > 0.5f) top_shear_y = 0.5f;

  if(!isfinite(top_size_x)) top_size_x = 1.0f;
  if(top_size_x < 0.0f) top_size_x = 0.0f;
  if(top_size_x > 2.0f) top_size_x = 2.0f;

  if(!isfinite(top_size_y)) top_size_y = 1.0f;
  if(top_size_y < 0.0f) top_size_y = 0.0f;
  if(top_size_y > 2.0f) top_size_y = 2.0f;
  
  spp->prim->path_shear_x = (int)(100*top_shear_x);
  spp->prim->path_shear_y = (int)(100*top_shear_y);

  // are they screwing with us on purpose? Seems like it...
  spp->prim->path_scale_x = (int)(100*(2.0f-top_size_x));
  spp->prim->path_scale_y = (int)(100*(2.0f-top_size_y));
  
  spp->objupd |= CAJ_OBJUPD_SHAPE;
  return TRUE;
}

int world_prim_spp_remove_light(struct world_spp_ctx *spp) {
  prim_delete_extra_param(spp->prim, PRIM_EXTRA_PARAM_LIGHT);
  spp->objupd |= CAJ_OBJUPD_EXTRA_PARAMS;
  return TRUE;
}

#define CONVERT_COLOR(col) (col > 1.0f ? 255 : (col < 0.0f ? 0 : (uint8_t)(col*255)))

int world_prim_spp_point_light(struct world_spp_ctx *spp, 
			       const caj_vector3* color, float intensity,
			       float radius, float falloff) {
  caj_string ep; unsigned char ep_data[16];
  ep_data[0] = CONVERT_COLOR(color->x); 
  ep_data[1] = CONVERT_COLOR(color->y); 
  ep_data[2] = CONVERT_COLOR(color->z); 
  ep_data[3] = CONVERT_COLOR(intensity); 
  caj_float_to_bin_le(ep_data+4, radius);
  caj_float_to_bin_le(ep_data+8, 0.0f); // unused?
  caj_float_to_bin_le(ep_data+12, falloff);

  ep.data = ep_data; ep.len = 16;
  int success = prim_set_extra_param(spp->prim, PRIM_EXTRA_PARAM_LIGHT, &ep);
  spp->objupd |= CAJ_OBJUPD_EXTRA_PARAMS;
  return success;
}

int world_prim_spp_set_material(struct world_spp_ctx *spp, int material) {
  spp->prim->material = material;
  spp->objupd |= CAJ_OBJUPD_MATERIAL;
  return TRUE;  
}

// FIXME - code duplication with world_prim_set_text
int world_prim_spp_set_text(struct world_spp_ctx *spp,
			    const char *text, uint8_t color[4]) {
  primitive_obj *prim = spp->prim;
  int len = strlen(text); if(len > 254) len = 254;
  free(prim->hover_text); prim->hover_text = (char*)malloc(len+1);
  memcpy(prim->hover_text, text, len); prim->hover_text[len] = 0;
  memcpy(prim->text_color, color, sizeof(prim->text_color));
  spp->objupd |= CAJ_OBJUPD_TEXT;
  return TRUE;    
}

void world_prim_spp_end(struct world_spp_ctx *spp) {
  world_mark_object_updated(spp->sim, &spp->prim->ob, spp->objupd);
}

// ---------------------------------

uint32_t user_calc_prim_perms(struct user_ctx* ctx, struct primitive_obj *prim) {
  uint32_t perms = prim->perms.everyone;
  if(uuid_compare(ctx->user_id, prim->owner) == 0) {
    perms |= prim->perms.current;
  }
  return perms & prim->perms.base; // ???
}

int user_can_modify_object(struct user_ctx* ctx, struct world_obj *obj) {
  if(obj->type != OBJ_TYPE_PRIM) return false;
  return (user_calc_prim_perms(ctx,(primitive_obj*)obj) & PERM_MODIFY) != 0;
}

int user_can_delete_object(struct user_ctx* ctx, struct world_obj *obj) {
  if(obj->type != OBJ_TYPE_PRIM) return false;
  // FIXME - probably not quite right
  // FIXME - handle locked objects here?
  return uuid_compare(((primitive_obj*)obj)->owner, ctx->user_id) == 0;
}


int user_can_copy_prim(struct user_ctx* ctx, struct primitive_obj *prim) {
  uint32_t perms = user_calc_prim_perms(ctx, prim);
  // FIXME - should allow other people to copy prims too
  return (uuid_compare(ctx->user_id, prim->owner) == 0 && (perms & PERM_COPY));
}

static primitive_obj * clone_prim(primitive_obj *prim, int faithful) {
  primitive_obj *newprim = new primitive_obj();
  *newprim = *prim;
  newprim->name = strdup(prim->name);
  newprim->description = strdup(prim->description);
  newprim->hover_text = strdup(faithful ? prim->hover_text : "");
  newprim->sit_name = strdup(faithful ? prim->sit_name : "");
  newprim->touch_name = strdup(faithful ? prim->touch_name : "");
  caj_string_copy(&newprim->tex_entry, &prim->tex_entry);
  caj_string_copy(&newprim->extra_params, &prim->extra_params);
  uuid_generate(newprim->ob.id);
  newprim->ob.phys = NULL;

  // FIXME - clone children!
  prim->ob.parent = NULL; prim->children = NULL;
  prim->num_children = 0;

  // FIXME - clone inventory too
  newprim->inv.num_items = newprim->inv.alloc_items = 0;
  newprim->inv.items = NULL;
  newprim->inv.filename = NULL;

  // FIXME - clear sit target?

  // obviously, any avatar won't be sitting on the new prim too.
  prim->avatar_sitting = NULL;
  prim->num_avatars = 0;
  prim->avatars = NULL; 

  return newprim;
}

void user_duplicate_prim(struct user_ctx* ctx, struct primitive_obj *prim,
			 caj_vector3 position) {
  // FIXME - should allow other people to copy prims too  
  if(uuid_compare(ctx->user_id, prim->owner) != 0) return;
  
  // the duplication loses certain properties, as in Second Life
  // (e.g hover text)
  primitive_obj *newprim = clone_prim(prim, 0);
  newprim->ob.local_pos = position;
  world_insert_obj(ctx->sim, &newprim->ob);
}

uint32_t user_calc_prim_flags(struct user_ctx* ctx, struct primitive_obj *prim) {
  uint32_t flags = 0; uint32_t perms = user_calc_prim_perms(ctx, prim);
  if(uuid_compare(ctx->user_id, prim->owner) == 0) {
    // owner can always move. FIXME - PRIM_FLAG_OWNER_MODIFY?
    flags |= PRIM_FLAG_YOU_OWNER|PRIM_FLAG_CAN_MOVE|PRIM_FLAG_OWNER_MODIFY;
  }
  if(perms & PERM_MOVE) flags |= PRIM_FLAG_CAN_MOVE;
  if(perms & PERM_MODIFY) flags |= PRIM_FLAG_CAN_MODIFY;
  if(perms & PERM_COPY) flags |= PRIM_FLAG_CAN_COPY;
  if(perms & PERM_TRANSFER) flags |= PRIM_FLAG_CAN_TRANSFER;
  return flags;
}

void avatar_set_footfall(struct simulator_ctx *sim, struct world_obj *obj,
			 const caj_vector4 *footfall) {
  assert(obj->type == OBJ_TYPE_AVATAR);
  avatar_obj *av = (avatar_obj*)obj;
  av->footfall = *footfall;
}

void avatar_get_footfall(struct world_obj *obj, caj_vector4 *footfall) {
  assert(obj->type == OBJ_TYPE_AVATAR);
  avatar_obj *av = (avatar_obj*)obj;
  *footfall = av->footfall;
}

// --- START of part of hacky object update code. FIXME - remove this ---
// The API provided by world_mark_object_updated/world_move_obj_from_phys will 
// probably now remain the same after the object update code is rewritten,
// though.

void world_int_init_obj_updates(user_ctx *ctx) {
  struct simulator_ctx* sim = ctx->sim;
  for(std::map<uint32_t,world_obj*>::iterator iter = sim->localid_map.begin();
      iter != sim->localid_map.end(); iter++) {
    world_obj *obj = iter->second;
    if(obj->type == OBJ_TYPE_PRIM) {
      ctx->obj_upd[obj->local_id] = CAJ_OBJUPD_CREATED;
    }
  }
}

uint32_t user_get_next_deleted_obj(user_ctx *ctx) {
  std::deque<uint32_t>::iterator diter = ctx->deleted_objs.begin();
  if(diter != ctx->deleted_objs.end()) {
    uint32_t localid = *diter; 
    ctx->deleted_objs.erase(diter); 
    return localid;
  } else {
    return 0;
  }
}

int user_has_pending_deleted_objs(user_ctx *ctx) {
  return ctx->deleted_objs.size() != 0;
}

int user_get_next_updated_obj(user_ctx *ctx, uint32_t *localid, int *flags) {
  std::map<uint32_t, int>::iterator iter = ctx->obj_upd.begin();
  if(iter != ctx->obj_upd.end()) {
    *localid = iter->first; *flags = iter->second; 
    ctx->obj_upd.erase(iter); return TRUE;
  } else {
    return FALSE;
  }
}

static void mark_deleted_obj_for_updates(simulator_ctx* sim, world_obj *obj) {
  // interestingly, this does handle avatars as well as prims.

  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    user->obj_upd.erase(obj->local_id);
    user->deleted_objs.push_back(obj->local_id);
  }
}

static void mark_object_updated_nophys(simulator_ctx* sim, world_obj *obj, 
				       int update_level) {
  if(obj->type != OBJ_TYPE_PRIM) return;

  primitive_obj *prim = (primitive_obj*)obj;
  prim->crc_counter++;

  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    std::map<uint32_t, int>::iterator cur = user->obj_upd.find(obj->local_id);
    if(cur == user->obj_upd.end()) {
      user->obj_upd[obj->local_id] = update_level; 
    } else {
      cur->second |= update_level;
    }
  }

  // FIXME - optimise this to avoid unnecesary checks.
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];
    if(inv->inv_type == INV_TYPE_LSL && inv->asset_type == ASSET_LSL_TEXT 
       && inv->spriv != NULL) {
      script_info *sinfo = (script_info*)inv->spriv;
      if(sinfo->priv != NULL) {
	int evmask = sim->scripth.get_evmask(sim, sim->script_priv, sinfo->priv);
	if((evmask & CAJ_EVMASK_PRIM_CHANGED) && 
	   sim->scripth.prim_change_event != NULL) {
	  sim->scripth.prim_change_event(sim, sim->script_priv, sinfo->priv, 
					 update_level);
	}
      }
    }
  }
}

void world_mark_object_updated(simulator_ctx* sim, world_obj *obj, int update_level) {
  sim->physh.upd_object(sim, sim->phys_priv, obj, update_level);
  mark_object_updated_nophys(sim, obj, update_level);
}

void world_move_obj_from_phys(struct simulator_ctx *sim, struct world_obj *ob,
			      const caj_vector3 *new_pos) {
  world_move_root_obj_int(sim, ob, *new_pos);
  mark_object_updated_nophys(sim, ob, CAJ_OBJUPD_POSROT);
}

// ------- END of object update code -----------------

void world_prim_apply_impulse(struct simulator_ctx *sim, struct primitive_obj* prim,
			      caj_vector3 impulse, int is_local) {
  sim->physh.apply_impulse(sim, sim->phys_priv, &prim->ob, impulse, is_local);
}

static void send_prim_collision(struct simulator_ctx *sim, struct primitive_obj* prim, 
				int coll_type, struct world_obj *collider) {
  //printf("DEBUG: in send_prim_collision, type %i\n", coll_type);
  int handled = 0;

  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->asset_type == ASSET_LSL_TEXT && inv->spriv != NULL &&
       ((script_info*)inv->spriv)->priv != NULL) {
      script_info* sinfo = (script_info*)inv->spriv;
      int evmask = sim->scripth.get_evmask(sim, sim->script_priv,
					   sinfo->priv);

      if(evmask & (CAJ_EVMASK_COLLISION_CONT | CAJ_EVMASK_COLLISION)) {
	handled = 1; // any touch handler will do for this

	// we leave finer-grained filtering to the script engine
	printf("DEBUG: sending collision event %i to script\n", coll_type);
	sim->scripth.collision_event(sim, sim->script_priv, sinfo->priv,
				     collider, coll_type);
      } else {
	//printf("DEBUG: ignoring script not interested in collision event\n");
      }
    }
  }
  if(prim->ob.parent != NULL && prim->ob.parent->type == OBJ_TYPE_PRIM &&
     !handled) {
    //printf("DEBUG: passing collision event to parent prim\n");
    send_prim_collision(sim, (primitive_obj*)prim->ob.parent, coll_type, collider);
  }
}


void world_update_collisions(struct simulator_ctx *sim, 
			     struct caj_phys_collision *collisions, int count) {
  collision_state *new_collisions = new collision_state();
  for(int i = 0; i < count; i++) {
    caj_phys_collision *coll = collisions+i;
    world_obj *obj = world_object_by_localid(sim, coll->collidee);
    world_obj *collider = world_object_by_localid(sim, coll->collider);
    if(obj == NULL || collider == NULL || obj->type != OBJ_TYPE_PRIM)
      continue;

    primitive_obj *prim = (primitive_obj*)obj;

    // FIXME - should apply some sort of filtering to events.
    if(sim->collisions->count(collision_pair(coll->collidee, coll->collider))) {
      send_prim_collision(sim, prim, CAJ_COLLISION_CONT, collider);
      sim->collisions->erase(collision_pair(coll->collidee, coll->collider));
    } else {
      send_prim_collision(sim, prim, CAJ_COLLISION_START, collider);
    }
    new_collisions->insert(collision_pair(coll->collidee, coll->collider));
  }

  for(collision_state::iterator iter = sim->collisions->begin();
      iter != sim->collisions->end(); iter++) {
    world_obj *obj = world_object_by_localid(sim, iter->collidee);
    world_obj *collider = world_object_by_localid(sim, iter->collider);
    if(obj == NULL || collider == NULL || obj->type != OBJ_TYPE_PRIM)
      continue;
    primitive_obj *prim = (primitive_obj*)obj;
    send_prim_collision(sim, prim, CAJ_COLLISION_END, collider);
  }

  delete sim->collisions; sim->collisions = new_collisions;
}

struct primitive_obj* world_prim_by_link_id(struct simulator_ctx* sim, 
					    struct primitive_obj *prim, 
					    int link_num) {
  primitive_obj *root = world_get_root_prim(prim);
  if(link_num < 0) {
    if(link_num == LINK_THIS) return prim;
    else return NULL;
  } else if(link_num == 0 || link_num == 1) {
    return root;
  } else {
    link_num -= 2;
    if(link_num < root->num_children) {
      primitive_obj *child = root->children[link_num];
      return child;
    } else return NULL;
  }
}

static void send_link_message(struct simulator_ctx* sim, 
			       struct primitive_obj *prim, int sender_num, 
			       int num, char *str, char *id) {
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->asset_type == ASSET_LSL_TEXT && inv->spriv != NULL &&
       ((script_info*)inv->spriv)->priv != NULL) {
      script_info* sinfo = (script_info*)inv->spriv;
      sim->scripth.link_message(sim, sim->script_priv, sinfo->priv,
				sender_num, num, str, id);
    }
  }
}

void world_script_link_message(struct simulator_ctx* sim, 
			       struct primitive_obj *prim, int link_num, 
			       int num, char *str, char *id) {
  primitive_obj *root = world_get_root_prim(prim);
  int sender_num = 0;
  if(prim == root) {
    // LL weirdness. Search me. FIXXE - check this against SL proper!
    sender_num = root->num_children == 0 ? 0 : 1;
  } else {
    // FIXME - must be a more efficient way!
    for(int i = 0; i < root->num_children; i++) {
      if(root->children[i] == prim) {
	sender_num = i + 2; break;
      }
    }
  }
  if(link_num < 0) {
    switch(link_num) {
    case LINK_SET:
      send_link_message(sim, root, sender_num, num, str, id);
      // fall through
    case LINK_ALL_CHILDREN:
      for(int i = 0; i < root->num_children; i++)
	send_link_message(sim, root->children[i], sender_num, num, str, id);
      break;
    case LINK_THIS:
      send_link_message(sim, prim, sender_num, num, str, id);
      break;
    case LINK_ALL_OTHERS:
      {
	int skip = sender_num - 2;
	if(skip >= 0) {
	  send_link_message(sim, root, sender_num, num, str, id);
	}
	for(int i = 0; i < root->num_children; i++) {
	  if(i != skip)
	    send_link_message(sim, root->children[i], sender_num, num, str, id);
	}
      }
      // FIXME - TODO!!!
      break;
    }
  } else if(link_num == 0 || link_num == 1) {
    send_link_message(sim, root, sender_num, num, str, id);
  } else {
    link_num -= 2;
    if(link_num < root->num_children)
      send_link_message(sim, root->children[link_num], sender_num, num, str, id);
  }
}

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

#include "cajeput_core.h"
#include "cajeput_world.h"
#include "cajeput_int.h"
#include "cajeput_prim.h"
#include "caj_script.h"
#include <cassert>

static void mark_new_obj_for_updates(simulator_ctx* sim, world_obj *obj);
static void mark_deleted_obj_for_updates(simulator_ctx* sim, world_obj *obj);

// --- START octree code ---

#define OCTREE_VERT_SCALE 64
#define OCTREE_HORIZ_SCALE 4
#define OCTREE_DEPTH 6
#define OCTREE_WIDTH (1<<OCTREE_DEPTH)

#if (OCTREE_WIDTH*OCTREE_HORIZ_SCALE) != 256
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
  struct world_ot_leaf* leaf = world_octree_find(tree, (int)obj->pos.x,
						 (int)obj->pos.y,
						 (int)obj->pos.z, true);
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
  struct world_ot_leaf* old_leaf = world_octree_find(tree, (int)obj->pos.x,
						     (int)obj->pos.y,
						(int)obj->pos.z, false);
  
  struct world_ot_leaf* new_leaf = world_octree_find(tree, (int)new_pos.x,
						     (int)new_pos.y,
						     (int)new_pos.z, true);
  if(old_leaf == new_leaf) return;
  old_leaf->objects.erase(obj);
  new_leaf->objects.insert(obj);
  if(obj->chat != NULL) {
    for(std::set<int32_t>::iterator iter = obj->chat->channels.begin(); 
	iter != obj->chat->channels.end(); iter++) {
      octree_add_chat(new_leaf, *iter, obj->chat);
      octree_del_chat(old_leaf, *iter, obj->chat);
    }
  }
}

// FIXME - this and the move function need to clean up unused nodes
void world_octree_delete(struct world_octree* tree, struct world_obj* obj) {
  struct world_ot_leaf* leaf = world_octree_find(tree, (int)obj->pos.x,
						 (int)obj->pos.y,
						 (int)obj->pos.z, false);
  leaf->objects.erase(obj);
  if(obj->chat != NULL) {
    for(std::set<int32_t>::iterator iter = obj->chat->channels.begin(); 
	iter != obj->chat->channels.end(); iter++) {
      octree_del_chat(leaf, *iter, obj->chat);
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
	  if(caj_vect3_dist(&listen->obj->pos, &chat->pos) < range)
	    listen->callback(sim, listen->obj, chat, listen->user_data);
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

void world_chat_from_prim(struct simulator_ctx *sim, struct primitive_obj* prim,
			  int32_t chan, char *msg, int chat_type) {
  struct chat_message chat;
  chat.channel = chan;
  chat.msg = msg;
  chat.chat_type = chat_type;
  chat.pos = prim->ob.pos;
  chat.name = prim->name;
  uuid_copy(chat.source,prim->ob.id);
  uuid_copy(chat.owner,prim->owner);
  chat.source_type = CHAT_SOURCE_OBJECT;
  world_send_chat(sim, &chat);
   
}


/* Note: listener is removed by world_remove_obj */
void world_obj_listen_chat(struct simulator_ctx *sim, struct world_obj *ob,
			   obj_chat_callback callback, void *user_data) {
  assert(ob->chat == NULL);
  ob->chat = new obj_chat_listener();
  ob->chat->obj = ob;
  ob->chat->callback = callback;
  ob->chat->user_data = user_data;
}

/* WARNING: do not call this until the object has been added to the octree.
   Seriously, just don't. It's not a good idea */
void world_obj_add_channel(struct simulator_ctx *sim, struct world_obj *ob,
			   int32_t channel) {
  assert(ob->chat != NULL);
  struct world_ot_leaf* leaf = world_octree_find(sim->world_tree, 
						 (int)ob->pos.x,
						 (int)ob->pos.y,
						 (int)ob->pos.z, false);
  assert(leaf != NULL);
  ob->chat->channels.insert(channel);
  octree_add_chat(leaf, channel, ob->chat);
}

void world_insert_obj(struct simulator_ctx *sim, struct world_obj *ob) {
  sim->uuid_map.insert(std::pair<obj_uuid_t,world_obj*>(obj_uuid_t(ob->id),ob));
  ob->local_id = (uint32_t)random();
  //FIXME - generate local ID properly.
  sim->localid_map.insert(std::pair<uint32_t,world_obj*>(ob->local_id,ob));
  world_octree_insert(sim->world_tree, ob);
  sim->physh.add_object(sim,sim->phys_priv,ob);

  mark_new_obj_for_updates(sim, ob);
}

void world_remove_obj(struct simulator_ctx *sim, struct world_obj *ob) {
  sim->localid_map.erase(ob->local_id);
  world_octree_delete(sim->world_tree, ob);
  sim->physh.del_object(sim,sim->phys_priv,ob);
  mark_deleted_obj_for_updates(sim, ob);
  delete ob->chat; ob->chat = NULL;
}

struct world_obj* world_object_by_id(struct simulator_ctx *sim, uuid_t id) {
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

// NOTE: if you're adding new fields to prims and want them to be initialised
// properly, you *must* edit cajeput_dump.cpp as well as here, since it doesn't
// use world_begin_new_prim when revivifying loaded prims.
// You may also need to update clone_prim below.
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
  prim->next_perms = prim->owner_perms = prim->base_perms = 0x7fffffff;
  prim->group_perms = prim->everyone_perms = 0;
  prim->flags = 0;

  prim->inv.num_items = prim->inv.alloc_items = 0;
  prim->inv.items = NULL; prim->inv.serial = 0;
  prim->inv.filename = NULL;

  prim->hover_text = strdup(""); 
  memset(prim->text_color, 0, sizeof(prim->text_color)); // technically redundant
  return prim;
}

void world_delete_prim(struct simulator_ctx *sim, struct primitive_obj *prim) {
  world_remove_obj(sim, &prim->ob);

  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->priv != NULL) {
      sim->scripth.kill_script(sim, sim->script_priv, inv->priv);
      inv->priv = NULL;
    }
    free(inv->name); free(inv->description); free(inv->creator_id);

    if(inv->asset_hack != NULL) {
      free(inv->asset_hack->name); free(inv->asset_hack->description);
      caj_string_free(&inv->asset_hack->data);
      delete inv->asset_hack;
    }
    delete inv;
  }
  free(prim->name); free(prim->description); free(prim->inv.filename);
  caj_string_free(&prim->tex_entry); free(prim->hover_text);
  free(prim->inv.items); delete prim;
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
  world_mark_object_updated(sim, &prim->ob, UPDATE_LEVEL_FULL);
}

void world_set_script_evmask(struct simulator_ctx *sim, struct primitive_obj* prim,
			     void *script_priv, int evmask) {
  int prim_evmask = 0;
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];
    if(/* inv->inv_type == ???  && - FIXME */ inv->asset_type == ASSET_LSL_TEXT 
       && inv->priv != NULL) {
      prim_evmask |= sim->scripth.get_evmask(sim, sim->script_priv, inv->priv);
    }
  }
  
  uint32_t newflags = prim->flags & ~(uint32_t)PRIM_FLAG_TOUCH;
  if(prim_evmask & (CAJ_EVMASK_TOUCH|CAJ_EVMASK_TOUCH_CONT)) {
    newflags |= PRIM_FLAG_TOUCH;
  }
  if(newflags != prim->flags) {
    prim->flags = newflags;
    // FIXME - don't really want a full update here!
    world_mark_object_updated(sim, &prim->ob, UPDATE_LEVEL_FULL);
  }
}

void user_touch_prim(struct simulator_ctx *sim, struct user_ctx *ctx,
		     struct primitive_obj* prim, int is_start) {
  printf("DEBUG: in user_touch_prim\n");
  // FIXME - this is going to become a whole lot more complex once we start
  // supporting linksets in Cajeput
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->asset_type == ASSET_LSL_TEXT && inv->priv != NULL) {
      int evmask = sim->scripth.get_evmask(sim, sim->script_priv, inv->priv);
      if(is_start ? (evmask & CAJ_EVMASK_TOUCH) : (evmask & CAJ_EVMASK_TOUCH_CONT)) {
	 printf("DEBUG: sending touch event to script\n");
	sim->scripth.do_touch(sim, sim->script_priv, inv->priv,
			      ctx, &ctx->av->ob, is_start);
      } else {
	 printf("DEBUG: ignoring script not interested in touch event\n");
      }
    }
  }
}

void user_untouch_prim(struct simulator_ctx *sim, struct user_ctx *ctx,
		     struct primitive_obj* prim) {
  // FIXME - this is going to become a whole lot more complex once we start
  // supporting linksets in Cajeput. Will probably want to merge with 
  // user_touch_prim at that point...
  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    inventory_item *inv = prim->inv.items[i];

    if(inv->asset_type == ASSET_LSL_TEXT && inv->priv != NULL) {
      int evmask = sim->scripth.get_evmask(sim, sim->script_priv, inv->priv);
      if(evmask & CAJ_EVMASK_TOUCH) {
	printf("DEBUG: sending untouch event to script\n");
	sim->scripth.do_untouch(sim, sim->script_priv, inv->priv,
				ctx, &ctx->av->ob);
      } else {
	 printf("DEBUG: ignoring script not interested in untouch event\n");
      }
    }
  }
}


static void prim_add_inventory(struct primitive_obj *prim, inventory_item *inv) {
  if(prim->inv.num_items >= prim->inv.alloc_items) {
    prim->inv.alloc_items = prim->inv.alloc_items == 0 ? 8 : prim->inv.alloc_items*2;
    prim->inv.items = (inventory_item**)realloc(prim->inv.items, 
						prim->inv.alloc_items*sizeof(inventory_item**));
    if(prim->inv.items == NULL) abort();
  }

  prim->inv.items[prim->inv.num_items++] = inv;
  prim->inv.serial++; free(prim->inv.filename); prim->inv.filename = NULL;
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
      if(inv->priv != NULL) {
	sim->scripth.kill_script(sim, sim->script_priv, inv->priv);
	inv->priv = NULL;
      }

      uuid_generate(inv->asset_id); // changed by update

      assert(inv->asset_hack != NULL); // FIXME
      uuid_copy(inv->asset_hack->id, inv->asset_id);
      caj_string_free(&inv->asset_hack->data);
      caj_string_set_bin(&inv->asset_hack->data, data, data_len);
      if(sim->scripth.add_script != NULL) {
	inv->priv = sim->scripth.add_script(sim, sim->script_priv, prim, inv, 
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
		     const char *name, const char *descrip, uint32_t flags) {
  inventory_item *inv = NULL;

  for(unsigned i = 0; i < prim->inv.num_items; i++) {
    if(strcmp(name, prim->inv.items[i]->name) == 0) {
      inv = prim->inv.items[i];
    }
  }
  if(inv != NULL) {
    printf("FIXME: item name clash in user_rez_script");
  }

  inv = new inventory_item();
  
  inv->name = strdup(name); inv->description = strdup(descrip);

  uuid_copy(inv->folder_id, prim->ob.id);
  uuid_copy(inv->owner_id, ctx->user_id);
  uuid_copy(inv->creator_as_uuid, ctx->user_id);
  
  uuid_generate(inv->item_id);
  uuid_generate(inv->asset_id);

  inv->creator_id = (char*)malloc(40); uuid_unparse(ctx->user_id, inv->creator_id);

  // FIXME - use client-provided perms
  inv->base_perms = inv->current_perms = inv->next_perms = 0x7fffffff;
  inv->group_perms = inv->everyone_perms = 0;

  inv->asset_type = ASSET_LSL_TEXT; 
  inv->inv_type = 10; // FIXME
  inv->sale_type = 0; // FIXME
  inv->group_owned = 0;
  inv->sale_price = 0;
  inv->creation_date = 0; // FIXME FIXME
  inv->flags = flags;

  // FIXME - bit iffy here
  simple_asset *asset = new simple_asset();
  asset->name = strdup(name); 
  asset->description = strdup(descrip);
  uuid_copy(asset->id, inv->asset_id);
  asset->type = ASSET_LSL_TEXT;
  caj_string_set(&asset->data, "default\n{  state_entry() {\n    llSay(0, \"Script running\");\n  }\n}\n");
  inv->asset_hack = asset;

  prim_add_inventory(prim, inv);
  
  // FIXME - check whether it should be created running...
  if(ctx->sim->scripth.add_script != NULL) {
    inv->priv = ctx->sim->scripth.add_script(ctx->sim, ctx->sim->script_priv, 
					     prim, inv, asset, NULL, NULL);
  } else {
    inv->priv = NULL;
  }
}

static void world_insert_demo_objects(struct simulator_ctx *sim) {
  struct primitive_obj *prim = world_begin_new_prim(sim);
  prim->ob.pos.x = 128.0f; prim->ob.pos.y = 128.0f; prim->ob.pos.z = 25.0f;
  world_insert_obj(sim, &prim->ob);
}

void world_move_obj_int(struct simulator_ctx *sim, struct world_obj *ob,
			const caj_vector3 &new_pos) {
  world_octree_move(sim->world_tree, ob, new_pos);
  ob->pos = new_pos;
}

// FIXME - validate position, rotation, scale!
// FIXME - handle the LINKSET flag correctly.
void world_multi_update_obj(struct simulator_ctx *sim, struct world_obj *obj,
			    const struct caj_multi_upd *upd) {
  if(upd->flags & CAJ_MULTI_UPD_POS) {
    world_move_obj_int(sim, obj, upd->pos);
  }
  if(upd->flags & CAJ_MULTI_UPD_ROT) {
    obj->rot = upd->rot;
  }
  if(upd->flags & CAJ_MULTI_UPD_SCALE) {
    obj->scale = upd->scale;
  }

  // FIXME - do we really need a full update in the scale case?
  world_mark_object_updated(sim, obj, (upd->flags & CAJ_MULTI_UPD_SCALE) ?
			    UPDATE_LEVEL_FULL : UPDATE_LEVEL_POSROT);
}


uint32_t user_calc_prim_perms(struct user_ctx* ctx, struct primitive_obj *prim) {
  uint32_t perms = prim->everyone_perms;
  if(uuid_compare(ctx->user_id, prim->owner) == 0) {
    perms |= prim->owner_perms;
  }
  return perms & prim->base_perms; // ???
}

int user_can_modify_object(struct user_ctx* ctx, struct world_obj *obj) {
  if(obj->type != OBJ_TYPE_PRIM) return false;
  return user_calc_prim_perms(ctx,(primitive_obj*)obj); // FIXME!
}

int user_can_copy_prim(struct user_ctx* ctx, struct primitive_obj *prim) {
  uint32_t perms = user_calc_prim_perms(ctx, prim);
  // FIXME - should allow other people to copy prims too
  return (uuid_compare(ctx->user_id, prim->owner) == 0 && (perms & PERM_COPY));
}

static primitive_obj * clone_prim(primitive_obj *prim) {
  primitive_obj *newprim = new primitive_obj();
  *newprim = *prim;
  newprim->name = strdup(prim->name);
  newprim->description = strdup(prim->description);
  newprim->hover_text = strdup(prim->hover_text);
  caj_string_copy(&newprim->tex_entry, &prim->tex_entry);
  uuid_generate(newprim->ob.id);
  
  // FIXME - clone inventory too
  newprim->inv.num_items = newprim->inv.alloc_items = 0;
  newprim->inv.items = NULL;
  newprim->inv.filename = NULL;

  return newprim;
}

void user_duplicate_prim(struct user_ctx* ctx, struct primitive_obj *prim,
			 caj_vector3 position) {
  // FIXME - should allow other people to copy prims too  
  if(uuid_compare(ctx->user_id, prim->owner) != 0) return;
  
  primitive_obj *newprim = clone_prim(prim);
  newprim->ob.pos = position;
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

// --- START of part of hacky object update code. FIXME - remove this ---

// FIXME - move this in with rest of the object update code 
void world_int_init_obj_updates(user_ctx *ctx) {
  struct simulator_ctx* sim = ctx->sim;
  for(std::map<uint32_t,world_obj*>::iterator iter = sim->localid_map.begin();
      iter != sim->localid_map.end(); iter++) {
    world_obj *obj = iter->second;
    if(obj->type == OBJ_TYPE_PRIM) {
      ctx->obj_upd[obj->local_id] = UPDATE_LEVEL_FULL;
    }
  }
}

static void mark_new_obj_for_updates(simulator_ctx* sim, world_obj *obj) {
  if(obj->type != OBJ_TYPE_PRIM) return;

  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    user->obj_upd[obj->local_id] = UPDATE_LEVEL_FULL;
  }
}

static void mark_deleted_obj_for_updates(simulator_ctx* sim, world_obj *obj) {
  // interestingly, this does handle avatars as well as prims.

  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    user->obj_upd.erase(obj->local_id);
    user->deleted_objs.push_back(obj->local_id);
  }
}


void world_mark_object_updated(simulator_ctx* sim, world_obj *obj, int update_level) {
  if(obj->type != OBJ_TYPE_PRIM) return;

  primitive_obj *prim = (primitive_obj*)obj;
  prim->crc_counter++;

  if(update_level == UPDATE_LEVEL_POSROT) 
    sim->physh.upd_object_pos(sim, sim->phys_priv, obj);
  else
    sim->physh.upd_object_full(sim, sim->phys_priv, obj);

  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    std::map<uint32_t, int>::iterator cur = user->obj_upd.find(obj->local_id);
    if(cur == user->obj_upd.end() || cur->second < update_level) {
      user->obj_upd[obj->local_id] = update_level; 
    }
  }
}

void world_move_obj_from_phys(struct simulator_ctx *sim, struct world_obj *ob,
			      const caj_vector3 *new_pos) {
  world_move_obj_int(sim, ob, *new_pos);

  if(ob->type != OBJ_TYPE_PRIM) return;

  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    user->obj_upd[ob->local_id] = UPDATE_LEVEL_POSROT; 
  }
}

void world_prim_apply_impulse(struct simulator_ctx *sim, struct primitive_obj* prim,
			      caj_vector3 impulse, int is_local) {
  sim->physh.apply_impulse(sim, sim->phys_priv, &prim->ob, impulse, is_local);
}

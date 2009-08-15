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

#include "sl_messages.h"
#include "sl_llsd.h"
#include "cajeput_core.h"
#include "cajeput_udp.h"
#include "cajeput_int.h"
#include "cajeput_j2k.h"
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <glib.h>
#include <libsoup/soup.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <cassert>

struct simulator_ctx;
struct avatar_obj;
struct world_octree;
struct cap_descrip;


static void user_remove_int(user_ctx **user);

// --- START sim query code ---

uint32_t sim_get_region_x(struct simulator_ctx *sim) {
  return sim->region_x;
}
uint32_t sim_get_region_y(struct simulator_ctx *sim) {
  return sim->region_y;
}
uint64_t sim_get_region_handle(struct simulator_ctx *sim) {
  return sim->region_handle;
}
char* sim_get_name(struct simulator_ctx *sim) {
  return sim->name;
}
char* sim_get_ip_addr(struct simulator_ctx *sim) {
  return sim->ip_addr;
}
void sim_get_region_uuid(struct simulator_ctx *sim, uuid_t u) {
  uuid_copy(u, sim->region_id);
}
void sim_get_owner_uuid(struct simulator_ctx *sim, uuid_t u) {
  uuid_copy(u, sim->owner);
}
uint16_t sim_get_http_port(struct simulator_ctx *sim) {
  return sim->http_port;
}
uint16_t sim_get_udp_port(struct simulator_ctx *sim) {
  return sim->udp_port;
}
void* sim_get_grid_priv(struct simulator_ctx *sim) {
  return sim->grid_priv;
}
void sim_set_grid_priv(struct simulator_ctx *sim, void* p) {
  sim->grid_priv = p;
}
// --- END sim query code ---


char *sim_config_get_value(struct simulator_ctx *sim, const char* section,
			   const char* key) {
  return g_key_file_get_value(sim->config,section,key,NULL);
}

void sim_queue_soup_message(struct simulator_ctx *sim, SoupMessage* msg,
			    SoupSessionCallback callback, void* user_data) {
  soup_session_queue_message(sim->soup_session, msg, callback, user_data);
}

void sim_http_add_handler (struct simulator_ctx *sim,
			   const char            *path,
			   SoupServerCallback     callback,
			   gpointer               user_data,
			   GDestroyNotify         destroy) {
  soup_server_add_handler(sim->soup,path,callback,user_data,destroy);
}

void sim_shutdown_hold(struct simulator_ctx *sim) {
  sim->hold_off_shutdown++;
}

void sim_shutdown_release(struct simulator_ctx *sim) {
  sim->hold_off_shutdown--;
}


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
		       const sl_vector3 &new_pos) {
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
	int count = 0;
	for(octree_chat_map_iter iter = span.first; iter != span.second;iter++) {
	  obj_chat_listener* listen = iter->second;
	  if(sl_vect3_dist(&listen->obj->pos, &chat->pos) < range)
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
  //FIXME - doesn't work since uuid_t is an array.
  sim->uuid_map.insert(std::pair<obj_uuid_t,world_obj*>(obj_uuid_t(ob->id),ob));
  ob->local_id = (uint32_t)random();
  //FIXME - generate local ID properly.
  sim->localid_map.insert(std::pair<uint32_t,world_obj*>(ob->local_id,ob));
  world_octree_insert(sim->world_tree, ob);
  sim->physh.add_object(sim,sim->phys_priv,ob);
}

void world_remove_obj(struct simulator_ctx *sim, struct world_obj *ob) {
  sim->localid_map.erase(ob->local_id);
  world_octree_delete(sim->world_tree, ob);
  sim->physh.del_object(sim,sim->phys_priv,ob);
  delete ob->chat; ob->chat = NULL;
}

void world_move_obj_int(struct simulator_ctx *sim, struct world_obj *ob,
			const sl_vector3 &new_pos) {
  world_octree_move(sim->world_tree, ob, new_pos);
  ob->pos = new_pos;
}

void user_reset_timeout(struct user_ctx* ctx) {
  ctx->last_activity = g_timer_elapsed(ctx->sim->timer, NULL);
}


static GMainLoop *main_loop;

// ------- START message handling code -----------------


// ------ END message handling code -------

// Texture-related stuff


// may want to move this to a thread, but it's reasonably fast since it 
// doesn't have to do a full decode
void sim_texture_read_metadata(struct texture_desc *desc) {
  struct cajeput_j2k info;
  if(cajeput_j2k_info(desc->data, desc->len, &info)) {
    assert(info.num_discard > 0);
    desc->width = info.width; desc->height = info.height;
    desc->num_discard = info.num_discard;
    desc->discard_levels = new int[info.num_discard];
    memcpy(desc->discard_levels, info.discard_levels, 
	   info.num_discard*sizeof(int));
  } else {
    char buf[40]; uuid_unparse(desc->asset_id, buf);
    printf("WARNING: texture metadata read failed for %s\n");
    desc->num_discard = 1;
    desc->discard_levels = new int[1];
    desc->discard_levels[0] = desc->len;
  }
}

// FIXME - actually clean up the textures we allocate
void sim_add_local_texture(struct simulator_ctx *sim, uuid_t asset_id, 
			   unsigned char *data, int len, int is_local) {
  texture_desc *desc = new texture_desc();
  uuid_copy(desc->asset_id, asset_id);
  desc->flags = is_local ?  CJP_TEXTURE_LOCAL : 0; // FIXME - code duplication
  desc->data = data; desc->len = len;
  desc->refcnt = 0; 
  desc->width = desc->height = desc->num_discard = 0;
  desc->discard_levels = NULL;
  sim_texture_read_metadata(desc);
  sim->textures[asset_id] = desc;

  if(is_local) {
    char asset_str[40], fname[80]; int fd;
    uuid_unparse(asset_id, asset_str);
    sprintf(fname, "temp_assets/%s.jpc", asset_str);
    fd = open(fname, O_WRONLY|O_CREAT|O_EXCL, 0644);
    if(fd < 0) {
      printf("Warning: couldn't open %s for temp texture save\n",
	     fname);
    } else {
      int ret = write(fd, data, len);
      if(ret != len) {
	if(ret < 0) perror("save local texture");
	printf("Warning: couldn't write full texure to %s: %i/%i\n",
	       fname, ret, len);
      }
      close(fd);
    }
  }
}

struct texture_desc *sim_get_texture(struct simulator_ctx *sim, uuid_t asset_id) {
  texture_desc *desc;
  std::map<obj_uuid_t,texture_desc*>::iterator iter =
    sim->textures.find(asset_id);
  if(iter != sim->textures.end()) {
    desc = iter->second;
  } else {
    desc = new texture_desc();
    uuid_copy(desc->asset_id, asset_id);
    desc->flags = 0;
    desc->data = NULL; desc->len = 0;
    desc->refcnt = 0;
    desc->width = desc->height = desc->num_discard = 0;
    desc->discard_levels = NULL;
    sim->textures[asset_id] = desc;
  }
  desc->refcnt++; return desc;
}

void sim_request_texture(struct simulator_ctx *sim, struct texture_desc *desc) {
  // FIXME - use disk-based cache
  if(desc->data == NULL && (desc->flags & 
		     (CJP_TEXTURE_PENDING | CJP_TEXTURE_MISSING)) == 0) {
    desc->flags |= CJP_TEXTURE_PENDING;
    sim->gridh.get_texture(sim, desc);
  }
}

static void sl_float_to_int16(unsigned char* out, float val, float range) {
  uint16_t ival = (uint16_t)((val+range)*32768/range);
  out[0] = ival & 0xff;
  out[1] = (ival >> 8) & 0xff;
}

#define PCODE_PRIM 9
#define PCODE_AV 47
#define PCODE_GRASS 95
#define PCODE_NEWTREE 111
#define PCODE_PARTSYS 143 /* ??? */
#define PCODE_TREE 255

// FIXME - incomplete
static void send_av_full_update(user_ctx* ctx, user_ctx* av_user) {
  avatar_obj* av = av_user->av;
  char name[0x100]; unsigned char obj_data[60];
  SL_DECLMSG(ObjectUpdate,upd);
  SL_DECLBLK(ObjectUpdate,RegionData,rd,&upd);
  rd->RegionHandle = ctx->sim->region_handle;
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation

  SL_DECLBLK(ObjectUpdate,ObjectData,objd,&upd);
  objd->ID = av->ob.local_id;
  objd->State = 0;
  uuid_copy(objd->FullID, av->ob.id);
  objd->PCode = PCODE_AV;
  objd->Scale.x = 1.0f; objd->Scale.y = 1.0f; objd->Scale.z = 1.0f;

  // FIXME - endianness issues
  memcpy(obj_data, &av->ob.pos, 12); 
  memset(obj_data+12, 0, 12); // velocity
  memset(obj_data+24, 0, 12); // accel
  memcpy(obj_data+36, &av->ob.rot, 12); 
  memset(obj_data+48, 0, 12);
  sl_string_set_bin(&objd->ObjectData, obj_data, 60);

  objd->ParentID = 0;
  objd->UpdateFlags = 0; // TODO

  sl_string_copy(&objd->TextureEntry, &ctx->texture_entry);
  //objd->TextureEntry.len = 0;
  objd->TextureAnim.len = 0;
  objd->Data.len = 0;
  objd->Text.len = 0;
  memset(objd->TextColor, 0, 4);
  objd->MediaURL.len = 0;
  objd->PSBlock.len = 0;
  objd->ExtraParams.len = 0;

  memset(objd->OwnerID,0,16);
  memset(objd->Sound,0,16);

  // FIXME - copied from OpenSim
  objd->UpdateFlags = 61 + (9 << 8) + (130 << 16) + (16 << 24);
  objd->PathCurve = 16;
  objd->ProfileCurve = 1;
  objd->PathScaleX = 100;
  objd->PathScaleY = 100;
  objd->ParentID = 0;
  objd->Material = 4;
  // END FIXME
  
  name[0] = 0;
  snprintf(name,0xff,"FirstName STRING RW SV %s\nLastName STRING RW SV %s\nTitle STRING RW SV %s",
	   av_user->first_name,av_user->last_name,av_user->group_title); // FIXME
  sl_string_set(&objd->NameValue,name);

  sl_send_udp(ctx, &upd);
}

static void send_av_terse_update(user_ctx* ctx, avatar_obj* av) {
  unsigned char dat[0x3C];
  SL_DECLMSG(ImprovedTerseObjectUpdate,terse);
  SL_DECLBLK(ImprovedTerseObjectUpdate,RegionData,rd,&terse);
  rd->RegionHandle = ctx->sim->region_handle;
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation
  SL_DECLBLK(ImprovedTerseObjectUpdate,ObjectData,objd,&terse);
  objd->TextureEntry.data = NULL;
  objd->TextureEntry.len = 0;

  dat[0] = av->ob.local_id & 0xff;
  dat[1] = (av->ob.local_id >> 8) & 0xff;
  dat[2] = (av->ob.local_id >> 16) & 0xff;
  dat[3] = (av->ob.local_id >> 24) & 0xff;
  dat[4] = 0; // state - ???
  dat[5] = 1; // object is an avatar
  
  // FIXME - copied from OpenSim
  memset(dat+6,0,16);
  dat[0x14] = 128; dat[0x15] = 63;
  
  // FIXME - correct endianness
  memcpy(dat+0x16, &av->ob.pos, 12); 

  // Velocity
  sl_float_to_int16(dat+0x22, 0.0f, 128.0f);
  sl_float_to_int16(dat+0x24, 0.0f, 128.0f);
  sl_float_to_int16(dat+0x26, 0.0f, 128.0f);

  // Acceleration
  sl_float_to_int16(dat+0x28, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x2A, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x2C, 0.0f, 64.0f);
 
  // Rotation
  sl_float_to_int16(dat+0x2E, av->ob.rot.x, 1.0f);
  sl_float_to_int16(dat+0x30, av->ob.rot.y, 1.0f);
  sl_float_to_int16(dat+0x32, av->ob.rot.z, 1.0f);
  sl_float_to_int16(dat+0x34, av->ob.rot.w, 1.0f);

  // Rotational velocity
  sl_float_to_int16(dat+0x36, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x38, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x3A, 0.0f, 64.0f);

 
  sl_string_set_bin(&objd->Data, dat, 0x3C);
  sl_send_udp(ctx, &terse);
}

static void send_av_appearance(user_ctx* ctx, user_ctx* av_user) {
  avatar_obj* av = av_user->av;
  char name[0x100]; unsigned char obj_data[60];
  SL_DECLMSG(AvatarAppearance,aa);
  SL_DECLBLK(AvatarAppearance,Sender,sender,&aa);
  uuid_copy(sender->ID, av_user->user_id);
  sender->IsTrial = 0;
  SL_DECLBLK(AvatarAppearance,ObjectData,objd,&aa);
  sl_string_copy(&objd->TextureEntry, &av_user->texture_entry);

  // FIXME - this is horribly, horribly inefficient
  if(av_user->visual_params.data != NULL) {
      for(int i = 0; i < av_user->visual_params.len; i++) {
	SL_DECLBLK(AvatarAppearance,VisualParam,param,&aa);
	param->ParamValue = av_user->visual_params.data[i];
      }
  }
  
  sl_send_udp(ctx, &aa);
}

static gboolean av_update_timer(gpointer data) {
  struct simulator_ctx* sim = (simulator_ctx*)data;
  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    /* HACK - FIXME do this right */
    if(user->av != NULL) {
      sim->physh.update_pos(sim, sim->phys_priv, &user->av->ob);
#if 0
      printf("DEBUG: user %s %s now at %f %f %f\n",
	     user->first_name, user->last_name,
	     user->av->ob.pos.x, user->av->ob.pos.y,
	     user->av->ob.pos.z);
#endif
    }
  }
  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    // don't send anything prior to RegionHandshakeReply
    if((user->flags & AGENT_FLAG_RHR) == 0) continue;

    for(user_ctx* user2 = sim->ctxts; user2 != NULL; user2 = user2->next) {
      struct avatar_obj *av = user2->av;
      if(av == NULL) continue;
      send_av_full_update(user, user2);
      if((user2->flags & AGENT_FLAG_APPEARANCE_UPD ||
	 user->flags & AGENT_FLAG_NEED_APPEARANCE) && user != user2) {
	// FIXME - shouldn't send AvatarAppearance to self?
	send_av_appearance(user, user2);
      }
    }
    user_clear_flag(user, AGENT_FLAG_NEED_APPEARANCE);
  }
  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    if(user->av != NULL)
      user_clear_flag(user,  AGENT_FLAG_APPEARANCE_UPD);
  }
  return TRUE;
}


// -- START of caps code --

// Note - length of this is hardcoded places
// Also note - the OpenSim grid server kinda assumes this path.
#define CAPS_PATH "/CAPS"

typedef void (*caps_callback) (SoupMessage *msg, user_ctx *ctx, void *user_data);

struct cap_descrip {
  struct simulator_ctx *sim;
  char *cap;
  caps_callback callback;
  user_ctx *ctx;
  void *user_data;
  void *udata_2;
};

struct cap_descrip* caps_new_capability_named(struct simulator_ctx *ctx,
					      caps_callback callback,
					      user_ctx* user, void *user_data, char* name) {
  int len = strlen(name);
  struct cap_descrip *desc = new cap_descrip();
  desc->sim = ctx; 
  desc->callback = callback;
  desc->ctx = user;
  desc->user_data = user_data;
  desc->cap = (char*)malloc(len+2);
  strcpy(desc->cap, name);
  if(len == 0 || desc->cap[len-1] != '/') {
    desc->cap[len] = '/'; desc->cap[len+1] = 0;
  }
  printf("DEBUG: Adding capability %s\n", desc->cap);
  ctx->caps[desc->cap] = desc;
  
  return desc;
}

struct cap_descrip* caps_new_capability(struct simulator_ctx *ctx,
					caps_callback callback,
					user_ctx* user, void *user_data) {
  uuid_t uuid; char name[40];

  uuid_generate_random(uuid);
  uuid_unparse_lower(uuid, name);
  name[36] = '/'; name[37] = 0;

  return caps_new_capability_named(ctx, callback, user, user_data, name);
}

void caps_remove(struct cap_descrip* desc) {
  if(desc == NULL) return;
  desc->sim->caps.erase(desc->cap);
  free(desc->cap);
  delete desc;
}

struct cap_descrip* user_add_named_cap(struct simulator_ctx *ctx, 
				       const char* name, caps_callback callback,
				       user_ctx* user, void *user_data) {
  cap_descrip* cap =  caps_new_capability(ctx, callback, user, user_data);
  user->named_caps[name] = cap;
  return cap;
}

char *caps_get_uri(struct cap_descrip* desc) {
  // FIXME - iffy memory handling
  char *uri = (char*)malloc(200);
  // FIXME - hardcoded port/IP
  sprintf(uri,"http://%s:%i" CAPS_PATH "/%s", desc->sim->ip_addr,
	  (int)desc->sim->http_port,desc->cap);
  return uri;
}

static void caps_handler (SoupServer *server,
			  SoupMessage *msg,
			  const char *path,
			  GHashTable *query,
			  SoupClientContext *client,
			  gpointer user_data) {
  struct simulator_ctx* ctx = (struct simulator_ctx*) user_data;
  printf("Got request: %s %s\n",msg->method, path);
  if(strncmp(path,CAPS_PATH "/",6) == 0) {
    path += 6;
    std::map<std::string,cap_descrip*>::iterator it;
    it = ctx->caps.find(path);
    if(it != ctx->caps.end()) {
      struct cap_descrip* desc = it->second;
      soup_message_set_status(msg,500);
      desc->callback(msg, desc->ctx, desc->user_data);
      return;
    }
  }
  soup_message_set_status(msg,404);
}

void llsd_soup_set_response(SoupMessage *msg, sl_llsd *llsd) {
  char *str;
  str = llsd_serialise_xml(llsd);
  if(str == NULL) {
    printf("DEBUG: couldn't serialise LLSD to send response\n");
    soup_message_set_status(msg,400);
  }
  printf("DEBUG: sending seeds caps response {{%s}}\n", str);
  soup_message_set_status(msg,200);
  // FIXME - should find away to avoid these gratuitous copies
  soup_message_set_response(msg,"application/xml", SOUP_MEMORY_COPY,
			    str,strlen(str));
  free(str);
}

void seed_caps_callback(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  // TODO
  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  sl_llsd *llsd, *resp; 
  if(msg->request_body->length > 65536) goto fail;
  llsd = llsd_parse_xml(msg->request_body->data, msg->request_body->length);
  if(llsd == NULL) goto fail;
  if(!LLSD_IS(llsd, LLSD_ARRAY)) goto free_fail;
  llsd_pretty_print(llsd, 0);

  resp = llsd_new_map();

  for(int i = 0; i < llsd->t.arr.count; i++) {
    sl_llsd *item = llsd->t.arr.data[i];
    if(!LLSD_IS(item, LLSD_STRING)) goto free_fail_2;
    named_caps_iter iter = ctx->named_caps.find(item->t.str);
    if(iter != ctx->named_caps.end()) {
      llsd_map_append(resp, item->t.str,
		      llsd_new_string_take(caps_get_uri(iter->second)));
      }
  }

  llsd_free(llsd);
  llsd_soup_set_response(msg, resp);
  llsd_free(resp);
  return;

 free_fail_2:
  llsd_free(resp);
 free_fail:
  llsd_free(llsd);
 fail:
  soup_message_set_status(msg,400);


}

static void event_queue_get_resp(SoupMessage *msg, user_ctx* ctx) {
    sl_llsd *resp = llsd_new_map();

    if(ctx->last_eventqueue != NULL)
      llsd_free(ctx->last_eventqueue);

    llsd_map_append(resp,"events",ctx->queued_events);
    ctx->queued_events = llsd_new_array();
    llsd_map_append(resp,"id",llsd_new_int(++ctx->event_queue_ctr));

    ctx->last_eventqueue = resp;
    llsd_soup_set_response(msg, resp);
}

static void event_queue_do_timeout(user_ctx* ctx) {
  if(ctx->event_queue_msg != NULL) {
    soup_server_unpause_message(ctx->sim->soup, ctx->event_queue_msg);
    soup_message_set_status(ctx->event_queue_msg,502);
    ctx->event_queue_msg = NULL;
  }
}

static void event_queue_get(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  sl_llsd *llsd, *ack;
  if(msg->request_body->length > 4096) goto fail;
  llsd = llsd_parse_xml(msg->request_body->data, msg->request_body->length);
  if(llsd == NULL) {
    printf("DEBUG: EventQueueGet parse failed\n");
    goto fail;
  }
  if(!LLSD_IS(llsd, LLSD_MAP)) {
    printf("DEBUG: EventQueueGet not map\n");
    goto free_fail;
  }
  ack = llsd_map_lookup(llsd,"ack");
  if(ack == NULL || (ack->type_id != LLSD_INT && ack->type_id != LLSD_UNDEF)) {
    printf("DEBUG: EventQueueGet bad ack\n");
    printf("DEBUG: message is {{%s}}\n", msg->request_body->data);
    goto free_fail;
  }
  if(ack->type_id == LLSD_INT && ack->t.i == ctx->event_queue_ctr &&
     ctx->last_eventqueue != NULL) {
    llsd_soup_set_response(msg, ctx->last_eventqueue);
    llsd_free(llsd);
    return;
  }

  event_queue_do_timeout(ctx);

  if(ctx->queued_events->t.arr.count > 0) {
    event_queue_get_resp(msg, ctx);
  } else {
    soup_server_pause_message(ctx->sim->soup, msg);
    ctx->event_queue_timeout = g_timer_elapsed(ctx->sim->timer, NULL) + 10.0;
    ctx->event_queue_msg = msg;
  }

  
  llsd_free(llsd);
  return;
  
 free_fail:
  llsd_free(llsd);
 fail:
  soup_message_set_status(msg,400);
  
}

#define NO_RELEASE_NOTES "Sorry, no release notes available."

static void send_release_notes(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  // TODO
  if (msg->method != SOUP_METHOD_GET) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  soup_message_set_status(msg,200);
  if(ctx->sim->release_notes) {
    soup_message_set_response(msg,"text/html", SOUP_MEMORY_COPY,
			      ctx->sim->release_notes, 
			      ctx->sim->release_notes_len);
  } else {
    soup_message_set_response(msg,"text/plain", SOUP_MEMORY_STATIC,
			      NO_RELEASE_NOTES, strlen(NO_RELEASE_NOTES));
  }
}


// -- END of caps code --

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

void user_get_uuid(struct user_ctx *user, uuid_t u) {
  uuid_copy(u, user->user_id);
}
void user_get_session_id(struct user_ctx *user, uuid_t u) {
  uuid_copy(u, user->session_id);
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


void user_set_texture_entry(struct user_ctx *user, struct sl_string* data) {
  sl_string_free(&user->texture_entry);
  user->texture_entry = *data;
  data->data = NULL;
  user->flags |= AGENT_FLAG_APPEARANCE_UPD; // FIXME - send full update instead?
}

void user_set_visual_params(struct user_ctx *user, struct sl_string* data) {
  sl_string_free(&user->visual_params);
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
  ctx->sock = sim->sock; ctx->counter = 0;
  ctx->draw_dist = 0.0f;
  ctx->circuit_code = uinfo->circuit_code;

  ctx->texture_entry.data = NULL;
  ctx->visual_params.data = NULL;
  ctx->appearance_serial = ctx->wearable_serial = 0;
  memset(ctx->wearables, 0, sizeof(ctx->wearables));

  debug_prepare_new_user(uinfo);

  uuid_copy(ctx->user_id, uinfo->user_id);
  uuid_copy(ctx->session_id, uinfo->session_id);
  uuid_copy(ctx->secure_session_id, uinfo->secure_session_id);

  ctx->addr.sin_family = AF_INET; 
  ctx->addr.sin_port = 0;
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

  // FIXME - where to put this?
  sim->gridh.fetch_user_inventory(sim,ctx,ctx->grid_priv);

  ctx->seed_cap = caps_new_capability_named(sim, seed_caps_callback, 
					    ctx, NULL, uinfo->seed_cap);
  // FIXME - split off the event queue stuff
  ctx->queued_events = llsd_new_array();
  ctx->last_eventqueue = NULL;
  ctx->event_queue_ctr = 0;
  ctx->event_queue_msg = NULL;
  user_add_named_cap(sim,"EventQueueGet",event_queue_get,ctx,NULL);
  user_add_named_cap(sim,"ServerReleaseNotes",send_release_notes,ctx,NULL);

  return ctx;
}


static void simstatus_rest_handler (SoupServer *server,
				SoupMessage *msg,
				const char *path,
				GHashTable *query,
				SoupClientContext *client,
				gpointer user_data) {
  /* struct simulator_ctx* sim = (struct simulator_ctx*) user_data; */
  // For OpenSim grid protocol - FIXME check actual status
  soup_message_set_status(msg,200);
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    "OK",2);
}



static void user_remove_int(user_ctx **user) {
  user_ctx* ctx = *user;
  simulator_ctx *sim = ctx->sim;
  if(ctx->av != NULL) {
    world_remove_obj(ctx->sim, &ctx->av->ob);
    if(!(ctx->flags & AGENT_FLAG_CHILD)) {
      sim->gridh.user_logoff(sim, ctx->user_id,
			     &ctx->av->ob.pos, &ctx->av->ob.pos);
    }
    free(ctx->av); ctx->av = NULL;
  }

  printf("Removing user %s %s\n", ctx->first_name, ctx->last_name);

  // If we're logging out, sending DisableSimulator causes issues
  if(ctx->addr.sin_port != 0 && !(ctx->flags & AGENT_FLAG_IN_LOGOUT)) {
    SL_DECLMSG(DisableSimulator, quit);
    sl_send_udp(ctx, &quit);
  } 

  event_queue_do_timeout(ctx);
  sim->gridh.user_deleted(ctx->sim,ctx,ctx->grid_priv);

  for(named_caps_iter iter = ctx->named_caps.begin(); 
      iter != ctx->named_caps.end(); iter++) {
    caps_remove(iter->second);
  }

  if(ctx->last_eventqueue != NULL)
    llsd_free(ctx->last_eventqueue);
  llsd_free(ctx->queued_events);

  user_int_free_texture_sends(ctx);

  free(ctx->first_name);
  free(ctx->last_name);
  free(ctx->name);
  free(ctx->group_title);
  caps_remove(ctx->seed_cap);
  
  *user = ctx->next;
  delete ctx;
}

void user_session_close(user_ctx* ctx) {
  simulator_ctx *sim = ctx->sim;
  if(ctx->av != NULL) {
    // FIXME - code duplication
    world_remove_obj(ctx->sim, &ctx->av->ob);
    sim->gridh.user_logoff(sim, ctx->user_id,
			   &ctx->av->ob.pos, &ctx->av->ob.pos);
    free(ctx->av); ctx->av = NULL;
  }
  ctx->flags |= AGENT_FLAG_PURGE;
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

static volatile int shutting_down = 0;

static gboolean shutdown_timer(gpointer data) {
  struct simulator_ctx* sim = (struct simulator_ctx*) data;
  if(sim->hold_off_shutdown) {
    printf("Shutting down (%i items pending)\n", sim->hold_off_shutdown);
    return TRUE;
  }
  g_key_file_free(sim->config);
  sim->config = NULL;
  sim->physh.destroy(sim, sim->phys_priv);
  sim->phys_priv = NULL;
  sim->gridh.cleanup(sim);
  sim->grid_priv = NULL;

  free(sim->release_notes);

  for(std::map<obj_uuid_t,texture_desc*>::iterator iter = sim->textures.begin();
      iter != sim->textures.end(); iter++) {
    struct texture_desc *desc = iter->second;
    free(desc->data); delete desc->discard_levels; delete desc;
  }
  
  world_octree_destroy(sim->world_tree);
  g_timer_destroy(sim->timer);
  g_free(sim->name);
  g_free(sim->ip_addr);
  g_free(sim->welcome_message);
  delete sim;
  exit(0); // FIXME
  return FALSE;
}

static gboolean cleanup_timer(gpointer data) {
  struct simulator_ctx* sim = (struct simulator_ctx*) data;
  double time_now = g_timer_elapsed(sim->timer,NULL);
  
  if(shutting_down) {
    printf("Shutting down...\n");
    sim->state_flags |= CAJEPUT_SIM_SHUTTING_DOWN;
    while(sim->ctxts)
      user_remove_int(&sim->ctxts);
    
    g_timeout_add(300, shutdown_timer, sim);
    return FALSE;
  }

  for(user_ctx** user = &sim->ctxts; *user != NULL; ) {
    if(time_now - (*user)->last_activity > USER_CONNECTION_TIMEOUT ||
       (*user)->flags & AGENT_FLAG_PURGE)
      user_remove_int(user);
    else {
      user_ctx *ctx = *user;
      if(ctx->event_queue_msg != NULL && 
	 time_now > ctx->event_queue_timeout) {
	printf("DEBUG: Timing out EventQueueGet\n");
	event_queue_do_timeout(ctx);
      }
      user = &ctx->next;
    }
  }
  return TRUE;
}

static gboolean physics_timer(gpointer data) {
  struct simulator_ctx* sim = (struct simulator_ctx*) data;
  //printf("*STEP*\n");
  sim->physh.step(sim, sim->phys_priv);
  return TRUE;
}

static void sigint_handler(int num) {
  shutting_down = 1;
}

static void set_sigint_handler() {
  struct sigaction act;
  act.sa_handler = sigint_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_RESETHAND; // one-shot handlerx
  if(sigaction(SIGINT, &act, NULL)) {
    perror("sigaction");
  }
}

static char *read_text_file(const char *name, int *lenout) {
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

int main(void) {
  g_thread_init(NULL);
  g_type_init();

  char* sim_uuid, *sim_owner;
  struct simulator_ctx* sim = new simulator_ctx();

  main_loop = g_main_loop_new(NULL, false);

  srandom(time(NULL));

  sim->config = g_key_file_new();
  sim->hold_off_shutdown = 0;
  sim->state_flags = 0;
  sim->xfer_id_ctr = 1;
  // FIXME - make sure to add G_KEY_FILE_KEEP_COMMENTS/TRANSLATIONS 
  // if I ever want to modify the config
  if(!g_key_file_load_from_file(sim->config,  "server.ini", 
				G_KEY_FILE_NONE, NULL)) {
    printf("Config file load failed\n"); 
    g_key_file_free(sim->config); sim->config = NULL;
    return 1;
  }

  sim->release_notes = read_text_file("sim-release-notes.html", 
				      &sim->release_notes_len);
  if(sim->release_notes == NULL) {
    printf("WARNING: Release notes load failed\n"); 
  }

  // FIXME - better error handling needed
  sim->name = g_key_file_get_value(sim->config,"sim","name",NULL);
  sim->http_port = g_key_file_get_integer(sim->config,"sim","http_port",NULL);
  sim->udp_port = g_key_file_get_integer(sim->config,"sim","udp_port",NULL);
  sim->region_x = g_key_file_get_integer(sim->config,"sim","region_x",NULL);
  sim->region_y = g_key_file_get_integer(sim->config,"sim","region_y",NULL);
  sim->region_handle = (uint64_t)(sim->region_x*256)<<32 | (sim->region_y*256);
  sim_uuid = g_key_file_get_value(sim->config,"sim","uuid",NULL);
  sim_owner = g_key_file_get_value(sim->config,"sim","owner",NULL);
  sim->ip_addr = g_key_file_get_value(sim->config,"sim","ip_address",NULL);
  
  // welcome message is optional
  sim->welcome_message = g_key_file_get_value(sim->config,"sim",
					      "welcome_message",NULL);

  if(sim->http_port == 0 || sim->udp_port == 0 || 
     sim->region_x == 0 || sim->region_y == 0 ||
      sim->name == NULL || sim->ip_addr == NULL ||
     sim_uuid == NULL ||
     uuid_parse(sim_uuid,sim->region_id)) {
    printf("Error: bad config\n"); return 1;
  }

  g_free(sim_uuid);

  if(sim_owner == NULL) {
    uuid_clear(sim->owner);
  } else {
    if(uuid_parse(sim_owner,sim->owner)) {
      printf("Error: bad owner UUID\n"); return 1;
    }
    g_free(sim_owner);
  }

  sim->world_tree = world_octree_create();
  sim->ctxts = NULL;
  sim->soup_session = soup_session_async_new();
  //uuid_generate_random(sim->region_secret);
  sim_int_init_udp(sim);

  sim->soup = soup_server_new(SOUP_SERVER_PORT, (int)sim->http_port, NULL);
  if(sim->soup == NULL) {
    printf("HTTP server init failed\n"); return 1;
  }
  soup_server_add_handler(sim->soup, CAPS_PATH, caps_handler, 
			  sim, NULL);
  soup_server_add_handler(sim->soup, "/simstatus", simstatus_rest_handler, 
			  sim, NULL);
  soup_server_run_async(sim->soup);
  
  g_timeout_add(100, av_update_timer, sim);
  g_timeout_add(1000, cleanup_timer, sim);
  g_timeout_add(1000/60, physics_timer, sim);
  sim->timer = g_timer_new();

  memset(&sim->gridh, 0, sizeof(sim->gridh));
  if(!cajeput_grid_glue_init(CAJEPUT_API_VERSION, sim, 
			     &sim->grid_priv, &sim->gridh)) {
    printf("Couldn't init grid glue!\n"); return 1;
  }

  if(!cajeput_physics_init(CAJEPUT_API_VERSION, sim, 
			     &sim->phys_priv, &sim->physh)) {
    printf("Couldn't init physics engine!\n"); return 1;
  }

  sim->gridh.do_grid_login(sim);
  set_sigint_handler();
  g_main_loop_run(main_loop);

  // Cleanup - FIXME, this is missing stuff
  return 0;
}

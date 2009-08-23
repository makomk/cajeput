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
#include "cajeput_int.h"
#include "cajeput_j2k.h"
#include "cajeput_prim.h"
#include "cajeput_anims.h"
#include "terrain_compress.h"
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
float* sim_get_heightfield(struct simulator_ctx *sim) {
  return sim->terrain;
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

static void world_insert_demo_objects(struct simulator_ctx *sim) {
  struct primitive_obj *prim = new primitive_obj();
  memset(prim, 0, sizeof(struct primitive_obj));
  prim->ob.type = OBJ_TYPE_PRIM;
  prim->ob.pos.x = 128.0f; prim->ob.pos.y = 128.0f; prim->ob.pos.z = 25.0f;
  prim->ob.scale.x = prim->ob.scale.y = prim->ob.scale.z = 1.0f;
  prim->profile_curve = PROFILE_SHAPE_SQUARE | PROFILE_HOLLOW_DEFAULT;
  prim->path_scale_x = 100; prim->path_scale_y = 100;
  world_insert_obj(sim, &prim->ob);
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

//  ----------- Texture-related stuff ----------------


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
    printf("WARNING: texture metadata read failed for %s\n", buf);
    desc->num_discard = 1;
    desc->discard_levels = new int[1];
    desc->discard_levels[0] = desc->len;
  }
}

static void save_texture(texture_desc *desc, const char* dirname) {
  char asset_str[40], fname[80]; int fd;
    uuid_unparse(desc->asset_id, asset_str);
    snprintf(fname, 80, "%s/%s.jp2", dirname, asset_str);
    fd = open(fname, O_WRONLY|O_CREAT|O_EXCL, 0644);
    if(fd < 0) {
      printf("Warning: couldn't open %s for temp texture save\n",
	     fname);
    } else {
      int ret = write(fd, desc->data, desc->len);
      if(ret != desc->len) {
	if(ret < 0) perror("save local texture");
	printf("Warning: couldn't write full texure to %s: %i/%i\n",
	       fname, ret, desc->len);
      }
      close(fd);
    }
}

void sim_texture_finished_load(texture_desc *desc) {
  assert(desc->data != NULL);
  sim_texture_read_metadata(desc);
  save_texture(desc,"tex_cache");
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
    save_texture(desc, "temp_assets");
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

static const char* texture_dirs[] = {"temp_assets","tex_cache",NULL};

void sim_request_texture(struct simulator_ctx *sim, struct texture_desc *desc) {
  if(desc->data == NULL && (desc->flags & 
		     (CJP_TEXTURE_PENDING | CJP_TEXTURE_MISSING)) == 0) {
    char asset_str[40], fname[80]; int fd; 
    struct stat st;
    uuid_unparse(desc->asset_id, asset_str);

    // first, let's see if we've got a cached copy locally
    for(int i = 0; texture_dirs[i] != NULL; i++) {
      sprintf(fname, "%s/%s.jp2", texture_dirs[i], asset_str);
      if(stat(fname, &st) != 0 || st.st_size == 0) continue;

      printf("DEBUG: loading texture from %s, len %i\n",fname,st.st_size);

      desc->len = st.st_size;
      fd = open(fname, O_RDONLY);
      if(fd < 0) {
	printf("ERROR: couldn't open texture cache file\n");
	break;
      }
      
      unsigned char *data = (unsigned char*)malloc(desc->len);
      int off;
      for(off = 0; off < desc->len; ) {
	int ret = read(fd, data+off, desc->len-off);
	if(ret <= 0) break;
	off += ret;
      }
      close(fd);

      if(off < desc->len) {
	printf("ERROR: Couldn't read texture from file\n");
	free(data); break;
      }

      desc->data = data;
      sim_texture_read_metadata(desc);
      return;
    }

    // No cached copy, have to do a real fetch
    desc->flags |= CJP_TEXTURE_PENDING;
    sim->gridh.get_texture(sim, desc);
  }
}


// FIXME - remove these!
#define PCODE_PRIM 9
#define PCODE_AV 47
#define PCODE_GRASS 95
#define PCODE_NEWTREE 111
#define PCODE_PARTSYS 143 /* ??? */
#define PCODE_TREE 255

// FIXME - this whole timer, and the associated callbacks, are a huge kludge
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
      if(user2->flags & AGENT_FLAG_AV_FULL_UPD ||
	 user->flags & AGENT_FLAG_NEED_OTHER_AVS) {
	if(user->userh != NULL && user->userh->send_av_full_update != NULL)
	  user->userh->send_av_full_update(user, user2);
      } else {
	if(user->userh != NULL && user->userh->send_av_terse_update != NULL)
	  user->userh->send_av_terse_update(user, user2->av); // FIXME - only send if needed
      }
      if((user2->flags & AGENT_FLAG_APPEARANCE_UPD ||
	 user->flags & AGENT_FLAG_NEED_OTHER_AVS) && user != user2) {
	// shouldn't send AvatarAppearance to self, I think.
	if(user->userh != NULL && user->userh->send_av_appearance != NULL)
	  user->userh->send_av_appearance(user, user2);
      }
      if(user2->flags & AGENT_FLAG_ANIM_UPDATE ||
	 user->flags & AGENT_FLAG_NEED_OTHER_AVS) {
	if(user->userh != NULL && user->userh->send_av_animations != NULL)
	  user->userh->send_av_animations(user, user2);
      }
    }
    user_clear_flag(user, AGENT_FLAG_NEED_OTHER_AVS);
  }
  for(user_ctx* user = sim->ctxts; user != NULL; user = user->next) {
    if(user->av != NULL)
      user_clear_flag(user,  AGENT_FLAG_APPEARANCE_UPD|AGENT_FLAG_ANIM_UPDATE|
		     AGENT_FLAG_AV_FULL_UPD);
  }
  return TRUE;
}


// --- START of part of hacky object update code. FIXME - remove this ---

// FIXME - move this in with rest of the object update code 
static void init_obj_updates_for_user(user_ctx *ctx) {
  struct simulator_ctx* sim = ctx->sim;
  for(std::map<uint32_t,world_obj*>::iterator iter = sim->localid_map.begin();
      iter != sim->localid_map.end(); iter++) {
    world_obj *obj = iter->second;
    if(obj->type == OBJ_TYPE_PRIM) {
      ctx->obj_upd[obj->local_id] = UPDATE_LEVEL_FULL;
    }
  }
}

// -- START of caps code --

// Note - length of this is hardcoded places
// Also note - the OpenSim grid server kinda assumes this path.
#define CAPS_PATH "/CAPS"


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

const sl_string* user_get_texture_entry(struct user_ctx *user) {
  return &user->texture_entry;
}

const sl_string* user_get_visual_params(struct user_ctx *user) {
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
			    const sl_vector3 *pos, const sl_vector3 *look_at) {
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
  sl_llsd *body = llsd_new_map();
  sl_llsd *sims = llsd_new_array();
  sl_llsd *info = llsd_new_map();

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
  init_obj_updates_for_user(ctx);

  for(int i = 0; i < 16; i++) ctx->dirty_terrain[i] = 0xffff;

  // FIXME - delete this!
  // sim->gridh.fetch_user_inventory(sim,ctx,ctx->grid_priv);

  ctx->seed_cap = caps_new_capability_named(sim, seed_caps_callback, 
					    ctx, NULL, uinfo->seed_cap);

  user_int_event_queue_init(ctx);

  user_add_named_cap(sim,"ServerReleaseNotes",send_release_notes,ctx,NULL);

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

    ctx->sim->gridh.user_entered(ctx->sim, ctx, ctx->grid_priv);
  }

  return true;
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

  user_int_event_queue_free(ctx);

  sim->gridh.user_deleted(ctx->sim,ctx,ctx->grid_priv);

  for(named_caps_iter iter = ctx->named_caps.begin(); 
      iter != ctx->named_caps.end(); iter++) {
    caps_remove(iter->second);
  }
  caps_remove(ctx->seed_cap);

  if(ctx->userh != NULL) { // non-optional hook!
    ctx->userh->remove(ctx->user_priv);
  }

  sl_string_free(&ctx->texture_entry);
  sl_string_free(&ctx->visual_params);


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

void user_session_close(user_ctx* ctx) {
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

  soup_session_abort(sim->soup_session);

  g_key_file_free(sim->config);
  sim->config = NULL;
  sim->physh.destroy(sim, sim->phys_priv);
  sim->phys_priv = NULL;
  sim->gridh.cleanup(sim);
  sim->grid_priv = NULL;

  sim_call_shutdown_hook(sim);

  free(sim->release_notes);

  soup_server_quit(sim->soup);
  g_object_unref(sim->soup);

  soup_session_abort(sim->soup_session); // yes, again!
  g_object_unref(sim->soup_session);

  for(std::map<obj_uuid_t,texture_desc*>::iterator iter = sim->textures.begin();
      iter != sim->textures.end(); iter++) {
    struct texture_desc *desc = iter->second;
    free(desc->data); delete[] desc->discard_levels; delete desc;
  }
  
  world_octree_destroy(sim->world_tree);
  g_timer_destroy(sim->timer);
  g_free(sim->name);
  g_free(sim->ip_addr);
  g_free(sim->welcome_message);
  delete[] sim->terrain;
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
      user_int_event_queue_check_timeout(ctx, time_now);
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

static void load_terrain(struct simulator_ctx *sim, const char* file) {
  unsigned char *dat = new unsigned char[13*256*256];
  int fd = open(file,O_RDONLY); int ret;
  if(fd < 0) return;
  ret = read(fd, dat, 13*256*256);
  if(ret != 13*256*256) goto out;
  for(int i = 0; i < 256*256; i++) {
    sim->terrain[i] = (int)dat[i*13] * dat[i*13+1] / 128.0f;
  }
 out:
  close(fd); delete[] dat;
  
}

static void create_cache_dirs(void) {
  mkdir("temp_assets", 0700);
  mkdir("tex_cache", 0700);
}

int main(void) {
  g_thread_init(NULL);
  g_type_init();
  terrain_init_compress(); // FIXME - move to omuser module
  create_cache_dirs();

  char* sim_uuid, *sim_owner;
  struct simulator_ctx* sim = new simulator_ctx();

  main_loop = g_main_loop_new(NULL, false);

  srandom(time(NULL));

  sim->terrain = new float[256*256];
  for(int i = 0; i < 256*256; i++) sim->terrain[i] = 25.0f;
  load_terrain(sim,"terrain.raw");

  sim->config = g_key_file_new();
  sim->hold_off_shutdown = 0;
  sim->state_flags = 0;
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

  world_insert_demo_objects(sim);

  sim->gridh.do_grid_login(sim);
  set_sigint_handler();
  g_main_loop_run(main_loop);

  // Cleanup - FIXME, this is missing stuff
  return 0;
}

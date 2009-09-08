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

#include "caj_llsd.h"
#include "cajeput_core.h"
#include "cajeput_int.h"
#include "cajeput_j2k.h"
#include "caj_script.h"
#include "terrain_compress.h"
#include "caj_parse_nini.h"
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
void* sim_get_script_priv(struct simulator_ctx *sim) {
  return sim->script_priv;
}
void sim_set_script_priv(struct simulator_ctx *sim, void* p) {
  sim->script_priv = p;
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



static GMainLoop *main_loop;

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

      printf("DEBUG: loading texture from %s, len %i\n",fname,(int)st.st_size);

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

// ------------- asset-related fun --------------------------

int user_can_access_asset_direct(user_ctx *user, simple_asset *asset) {
  return asset->type != ASSET_NOTECARD && asset->type != ASSET_LSL_TEXT;
}

int user_can_access_asset_task_inv(user_ctx *user, primitive_obj *prim,
				   inventory_item *inv) {
  return TRUE; // FIXME - need actual permission checks
}

void sim_asset_finished_load(struct simulator_ctx *sim, 
			     struct simple_asset *asset, int success) {
  // FIXME - use something akin to containerof?
  std::map<obj_uuid_t,asset_desc*>::iterator iter =
    sim->assets.find(asset->id);
  assert(iter != sim->assets.end());
  asset_desc *desc = iter->second;
  assert(desc->status == CAJ_ASSET_PENDING);
  assert(asset == &desc->asset);

  if(success) desc->status = CAJ_ASSET_READY;
  else desc->status = CAJ_ASSET_MISSING;

  for(std::set<asset_cb_desc*>::iterator iter = desc->cbs.begin(); 
      iter != desc->cbs.end(); iter++) {
    (*iter)->cb(sim, (*iter)->cb_priv, success ? &desc->asset : NULL);
    delete (*iter);
  }
  desc->cbs.clear();
  // FIXME - TODO
}

void sim_get_asset(struct simulator_ctx *sim, uuid_t asset_id,
		   void(*cb)(struct simulator_ctx *sim, void *priv,
			     struct simple_asset *asset), void *cb_priv) {
  asset_desc *desc;
  std::map<obj_uuid_t,asset_desc*>::iterator iter =
    sim->assets.find(asset_id);
  if(iter != sim->assets.end()) {
    desc = iter->second;
  } else {
    desc = new asset_desc();
    uuid_copy(desc->asset.id, asset_id);
    desc->status = CAJ_ASSET_PENDING;
    desc->asset.name = desc->asset.description = NULL;
    desc->asset.data.data = NULL;
    sim->assets[asset_id] = desc;
    
    // FIXME - need to actually do the fetch!!
    //printf("FIXME: we don't actually know how to fetch assets yet!\n");
    printf("DEBUG: sending asset request via grid\n");
    sim->gridh.get_asset(sim, &desc->asset);
  }
  
  switch(desc->status) {
  case CAJ_ASSET_PENDING:
    // FIXME - move this to the generic hooks code
    desc->cbs.insert(new asset_cb_desc(cb, cb_priv));
    break;
  case CAJ_ASSET_READY:
    cb(sim, cb_priv, &desc->asset); break;
  case CAJ_ASSET_MISSING:
    cb(sim, cb_priv, NULL); break;
  default: assert(0);
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

void llsd_soup_set_response(SoupMessage *msg, caj_llsd *llsd) {
  char *str;
  str = llsd_serialise_xml(llsd);
  if(str == NULL) {
    printf("DEBUG: couldn't serialise LLSD to send response\n");
    soup_message_set_status(msg,400);
  }
  printf("DEBUG: caps response {{%s}}\n", str);
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

  caj_llsd *llsd, *resp; 
  if(msg->request_body->length > 65536) goto fail;
  llsd = llsd_parse_xml(msg->request_body->data, msg->request_body->length);
  if(llsd == NULL) goto fail;
  if(!LLSD_IS(llsd, LLSD_ARRAY)) goto free_fail;
  llsd_pretty_print(llsd, 0);

  resp = llsd_new_map();

  for(int i = 0; i < llsd->t.arr.count; i++) {
    caj_llsd *item = llsd->t.arr.data[i];
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

struct update_script_desc {
  uuid_t task_id, item_id;
  cap_descrip* cap;
  int script_running;
  
  // only used during compile stage
  uuid_t asset_id;
  user_ctx *ctx; simulator_ctx *sim; SoupMessage *msg;
};


// used to free the capability etc if the session ends before the client
// actually sends the script. 
static void free_update_script_desc(user_ctx* ctx, void *priv) {
   update_script_desc *upd = (update_script_desc*)priv;
   caps_remove(upd->cap);
   delete upd;
}

static void update_script_compiled_cb(void *priv, int success, char* output, 
				      int output_len) {
  printf("DEBUG: in update_script_compiled_cb\n");
  update_script_desc *upd = (update_script_desc*)priv;
  soup_server_unpause_message(upd->sim->soup, upd->msg);
  sim_shutdown_release(upd->sim);
  if(upd->ctx != NULL) {
    caj_llsd *resp; caj_llsd *errors;
    printf("DEBUG: sending %s script compile response\n", success ? "successful" : "unsuccessful");
    // FIXME - this probably ain't right; think OpenSim gets this wrong
    resp = llsd_new_map();
    llsd_map_append(resp,"state",llsd_new_string("complete"));
    llsd_map_append(resp,"new_asset",llsd_new_uuid(upd->asset_id));
    llsd_map_append(resp,"compiled",llsd_new_bool(success));

    errors = llsd_new_array();
    char *outp = output;
    for(;;) {
      char *next = strchr(outp,'\n'); if(next == NULL) break;
      int len = next-outp;
      char *line = (char*)malloc(len+1);
      memcpy(line, outp, len); line[len] = 0;
      llsd_array_append(errors, llsd_new_string_take(line));
      outp = next+1;
    }
    if(outp[0] != 0) llsd_array_append(errors, llsd_new_string(outp));
    
    llsd_map_append(resp,"errors",errors);
    
    llsd_soup_set_response(upd->msg, resp);
    llsd_free(resp);
    user_del_self_pointer(&upd->ctx);
  } else {
    soup_message_set_status(upd->msg,500);
  }
  delete upd;

}

static void update_script_stage2(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  update_script_desc *upd = (update_script_desc*)user_data;
  primitive_obj * prim; inventory_item *inv;
  
  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  // now we've got the upload, the capability isn't needed anymore, and we
  // delete the user removal hook because our lifetime rules change!
  caps_remove(upd->cap);
  user_remove_delete_hook(ctx, free_update_script_desc, upd);

  printf("Got UpdateScriptTask data >%s<\n", msg->request_body->data);

  struct world_obj* obj = world_object_by_id(ctx->sim, upd->task_id);
  if(obj == NULL) {
    printf("ERROR: UpdateScriptTask for non-existent object\n");
    goto out_fail;
  } else if(!user_can_modify_object(ctx, obj)) {
    printf("ERROR: UpdateScriptTask with insufficient permissions on object\n");
    goto out_fail;
  } else if(obj->type != OBJ_TYPE_PRIM) {
    printf("ERROR: UpdateScriptTask for non-prim object\n");
    goto out_fail;
  }
  prim = (primitive_obj*) obj;

  // FIXME - need to check permissions on script!
  
  uuid_clear(upd->asset_id);

  upd->ctx = ctx; user_add_self_pointer(&upd->ctx);
  upd->sim = ctx->sim; sim_shutdown_hold(ctx->sim);
  upd->msg = msg; soup_server_pause_message(ctx->sim->soup, msg);

  inv = prim_update_script(ctx->sim, prim, upd->item_id, upd->script_running, 
			   (unsigned char*)msg->request_body->data, 
			   msg->request_body->length, update_script_compiled_cb,
			   upd);
  if(inv != NULL) uuid_copy(upd->asset_id, inv->asset_id);

  return;
  
  // FIXME - TODO

 out_fail:
  delete upd;
  soup_message_set_status(msg,400);
}

static void update_script_task(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  caj_llsd *llsd, *resp; 
  caj_llsd *item_id, *task_id, *script_running;
  update_script_desc *upd;
  if(msg->request_body->length > 65536) goto fail;
  llsd = llsd_parse_xml(msg->request_body->data, msg->request_body->length);
  if(llsd == NULL) goto fail;
  if(!LLSD_IS(llsd, LLSD_MAP)) goto free_fail;

  // FIXME - need to implement this
  printf("Got UpdateScriptTask:\n");
  llsd_pretty_print(llsd, 1);
  item_id = llsd_map_lookup(llsd, "item_id");
  task_id = llsd_map_lookup(llsd, "task_id");
  script_running = llsd_map_lookup(llsd, "is_script_running");
  if(!(LLSD_IS(item_id, LLSD_UUID) && LLSD_IS(task_id, LLSD_UUID) && 
       LLSD_IS(script_running, LLSD_INT))) goto free_fail;
  
  upd = new update_script_desc();
  uuid_copy(upd->task_id, task_id->t.uuid);
  uuid_copy(upd->item_id, item_id->t.uuid);
  upd->script_running = script_running->t.i;
  upd->cap = caps_new_capability(ctx->sim, update_script_stage2, ctx, upd);
  user_add_delete_hook(ctx, free_update_script_desc, upd);
  
  resp = llsd_new_map();
  llsd_map_append(resp,"state",llsd_new_string("upload"));
  llsd_map_append(resp,"uploader",llsd_new_string_take(caps_get_uri(upd->cap)));

  llsd_free(llsd);
  llsd_soup_set_response(msg, resp);
  llsd_free(resp);
  return;

 free_fail:
  llsd_free(llsd);
 fail:
  soup_message_set_status(msg,400);
  
}

void user_int_caps_init(simulator_ctx *sim, user_ctx *ctx, 
			struct sim_new_user *uinfo) {
  ctx->seed_cap = caps_new_capability_named(sim, seed_caps_callback, 
					    ctx, NULL, uinfo->seed_cap);

  user_int_event_queue_init(ctx);

  user_add_named_cap(sim,"ServerReleaseNotes",send_release_notes,ctx,NULL);
  user_add_named_cap(sim,"UpdateScriptTask",update_script_task,ctx,NULL);  
}

void user_int_caps_cleanup(user_ctx *ctx) {
  for(named_caps_iter iter = ctx->named_caps.begin(); 
      iter != ctx->named_caps.end(); iter++) {
    caps_remove(iter->second);
  }
  caps_remove(ctx->seed_cap);
}

// -- END of caps code --


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
  // FIXME - want to shutdown physics here, really.
  sim->gridh.cleanup(sim);
  sim->grid_priv = NULL;

  sim_call_shutdown_hook(sim);

  free(sim->release_notes);

  soup_server_quit(sim->soup);
  g_object_unref(sim->soup);

  soup_session_abort(sim->soup_session); // yes, again!
  g_object_unref(sim->soup_session);

  world_int_dump_prims(sim);

  for(std::map<obj_uuid_t,texture_desc*>::iterator iter = sim->textures.begin();
      iter != sim->textures.end(); iter++) {
    struct texture_desc *desc = iter->second;
    free(desc->data); delete[] desc->discard_levels; delete desc;
  }

  for(std::map<obj_uuid_t,inventory_contents*>::iterator iter = sim->inv_lib.begin();
      iter != sim->inv_lib.end(); iter++) {
    caj_inv_free_contents_desc(iter->second);
  }

  for(std::map<obj_uuid_t,asset_desc*>::iterator iter = sim->assets.begin();
      iter != sim->assets.end(); iter++) {
    asset_desc *desc = iter->second;
    for(std::set<asset_cb_desc*>::iterator iter = desc->cbs.begin(); 
	iter != desc->cbs.end(); iter++) {
      (*iter)->cb(sim, (*iter)->cb_priv, NULL);
      delete (*iter);
    }
    free(desc->asset.name); free(desc->asset.description);
    free(desc->asset.data.data);
    delete desc;
  }

  for(std::map<uint32_t, world_obj*>::iterator iter = sim->localid_map.begin();
      iter != sim->localid_map.end(); /* nowt */) {
    if(iter->second->type == OBJ_TYPE_PRIM) {
      std::map<uint32_t, world_obj*>::iterator iter2 = iter; iter2++;
      primitive_obj *prim = (primitive_obj*)iter->second;
      world_delete_prim(sim, prim);
      iter = iter2;
    } else iter++;
  }

  sim->physh.destroy(sim, sim->phys_priv);
  sim->phys_priv = NULL;
  
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
    if((*user)->flags & AGENT_FLAG_IN_SLOW_REMOVAL) {
      if(--((*user)->shutdown_ctr) <= 0)
	(*user)->flags |= AGENT_FLAG_PURGE;
    }

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

// --------------- INVENTORY LIBRARY STUFF ----------------------------

// In a perfect world, this would be handled by the inventory server.
// Unfortunately, it isn't, so we have to hardcode it here!
// Oh, and our local copy of the inventory library has to be the same as what 
// the rest of the grid has, or things don't work right. Ick.

#define LIBRARY_OWNER "11111111-1111-0000-0000-000100bba000"

#define LIBRARY_ROOT "00000112-000f-0000-0000-000100bba000"


static void load_inv_folders(struct simulator_ctx *sim, const char* filename) {
  char path[256];
  snprintf(path,256,"inventory/%s",filename);

 GKeyFile *config = caj_parse_nini_xml(path);
 if(config == NULL) {
   printf("WARNING: couldn't load inventory file %s\n", filename);
   return;
 }

 gchar** sect_list = g_key_file_get_groups(config, NULL);

 for(int i = 0; sect_list[i] != NULL; i++) {
   gchar *folder_id, *parent_id, *name, *type_str;
   uuid_t folder_uuid, parent_uuid;
   folder_id = g_key_file_get_value(config, sect_list[i], "folderID", NULL);
   parent_id = g_key_file_get_value(config, sect_list[i], "parentFolderID", NULL);
   name = g_key_file_get_value(config, sect_list[i], "name", NULL);
   type_str = g_key_file_get_value(config, sect_list[i], "type", NULL);

   if(folder_id == NULL || parent_id == NULL || name == NULL ||
      type_str == NULL || uuid_parse(folder_id,folder_uuid) || 
      uuid_parse(parent_id, parent_uuid)) {
     printf("ERROR: bad section %s in %s", sect_list[i], filename);
   } else {
     // FIXME - this leaks memory if we have more than one folder with 
     // the same UUID
     sim->inv_lib[folder_uuid] = caj_inv_new_contents_desc(folder_uuid);

     // FIXME - add folders to their parent folders
   }
   
   g_free(folder_id); g_free(parent_id); g_free(name); g_free(type_str);
 }

 g_strfreev(sect_list);
 g_key_file_free(config);
}

static void load_inv_items(struct simulator_ctx *sim, const char* filename) {
  char path[256];
  snprintf(path,256,"inventory/%s",filename);

 GKeyFile *config = caj_parse_nini_xml(path);
 if(config == NULL) {
   printf("WARNING: couldn't load inventory file %s\n", filename);
   return;
 }

 gchar** sect_list = g_key_file_get_groups(config, NULL);

 for(int i = 0; sect_list[i] != NULL; i++) {
   gchar *folder_id, *asset_id, *inv_id, *name, *descr;
   gchar *asset_type, *inv_type;
   uuid_t folder_uuid, asset_uuid, inv_uuid;
   folder_id = g_key_file_get_value(config, sect_list[i], "folderID", NULL);
   asset_id = g_key_file_get_value(config, sect_list[i], "assetID", NULL);
   inv_id = g_key_file_get_value(config, sect_list[i], "inventoryID", NULL);
   name = g_key_file_get_value(config, sect_list[i], "name", NULL);
   descr = g_key_file_get_value(config, sect_list[i], "description", NULL);
   asset_type = g_key_file_get_value(config, sect_list[i], "assetType", NULL);
   inv_type = g_key_file_get_value(config, sect_list[i], "inventoryType", NULL);

   if(folder_id == NULL || asset_id == NULL || inv_id == NULL ||
      name == NULL || descr == NULL || asset_type == NULL ||
      inv_type == NULL || uuid_parse(folder_id,folder_uuid) || 
      uuid_parse(asset_id, asset_uuid) || uuid_parse(inv_id, inv_uuid)) {
     printf("ERROR: bad section %s in %s", sect_list[i], filename);
   } else {
     std::map<obj_uuid_t,inventory_contents*>::iterator iter = 
       sim->inv_lib.find(folder_uuid);
     if(iter == sim->inv_lib.end()) {
       printf("ERROR: library item %s without parent folder %s\n",
	      name, folder_id);
     } else {
       inventory_contents* folder = iter->second;

       inventory_item* item = caj_add_inventory_item(folder, name, descr,
			      LIBRARY_OWNER);
       uuid_copy(item->item_id, inv_uuid);
       uuid_copy(item->asset_id, asset_uuid);
       uuid_parse(LIBRARY_OWNER, item->creator_as_uuid);
       uuid_parse(LIBRARY_OWNER, item->owner_id);
       item->asset_type = atoi(asset_type);
       item->inv_type = atoi(inv_type);
       item->base_perms = item->current_perms  = 0x7fffffff;
       item->next_perms = item->everyone_perms = 0x7fffffff;
       item->group_perms = 0;
       item->sale_type = 0; item->group_owned = 0;
       uuid_clear(item->group_id);
       item->flags = 0; item->sale_price = 0;
       item->creation_date = 0;
       
       // FIXME - have we filled everything in?
     }
   }
   
   g_free(folder_id); g_free(asset_id); g_free(inv_id); g_free(name); 
   g_free(descr); g_free(asset_type); g_free(inv_type);
 }

 g_strfreev(sect_list);
 g_key_file_free(config);
}


static void load_inv_library(struct simulator_ctx *sim) {
  GKeyFile *lib = caj_parse_nini_xml("inventory/Libraries.xml");
  if(lib == NULL) {
    printf("WARNING: couldn't load inventory library\n");
    return;
  }
  
  // add hardcoded library root folder. Ugly.
  uuid_t root_uuid;
  uuid_parse(LIBRARY_ROOT, root_uuid);
  sim->inv_lib[root_uuid] = caj_inv_new_contents_desc(root_uuid);

  gchar** sect_list = g_key_file_get_groups(lib, NULL);

  for(int i = 0; sect_list[i] != NULL; i++) {
    gchar* folders_file = g_key_file_get_value(lib, sect_list[i], "foldersFile", NULL);
    gchar* items_file = g_key_file_get_value(lib, sect_list[i], "itemsFile", NULL);

    if(folders_file == NULL || items_file == NULL) {
      printf("ERROR: bad section %s in inventory/Libraries.xml", sect_list[i]);
    } else {
      load_inv_folders(sim, folders_file);
      load_inv_items(sim, items_file);
    }
    g_free(folders_file); g_free(items_file);
  }

  g_strfreev(sect_list);
  g_key_file_free(lib);
}

// -------------- END INVENTORY LIBRARY STUFF ------------------------------

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

  load_inv_library(sim);
  
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

  if(!caj_scripting_init(CAJEPUT_API_VERSION, sim, &sim->script_priv,
			 &sim->scripth)) {
    printf("Couldn't init script engine!\n"); return 1;
  }

  world_int_load_prims(sim);
  // world_insert_demo_objects(sim);

  sim->gridh.do_grid_login(sim);
  set_sigint_handler();
  g_main_loop_run(main_loop);

  // Cleanup - FIXME, this is missing stuff
  return 0;
}

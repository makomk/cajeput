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

#include "caj_llsd.h"
#include "cajeput_core.h"
#include "cajeput_int.h"
#include <libsoup/soup.h>
#include <stdio.h>
#include <string.h>

// -- START of caps code --

#define CAJ_LOGGER (sim->sgrp->log)

// Note - length of this is hardcoded places
// Also note - the OpenSim grid server kinda assumes this path.
#define CAPS_PATH "/CAPS"

struct cap_descrip {
  struct simgroup_ctx *sgrp;
  char *cap;
  caps_callback callback;
  user_ctx *ctx;
  void *user_data;
  void *udata_2;
};

struct cap_descrip* caps_new_capability_named(struct simulator_ctx *sim,
					      caps_callback callback,
					      user_ctx* user, void *user_data, char* name) {
  int len = strlen(name);
  struct cap_descrip *desc = new cap_descrip();
  desc->sgrp = sim->sgrp; 
  desc->callback = callback;
  desc->ctx = user;
  desc->user_data = user_data;
  desc->cap = (char*)malloc(len+2);
  strcpy(desc->cap, name);
  if(len == 0 || desc->cap[len-1] != '/') {
    desc->cap[len] = '/'; desc->cap[len+1] = 0;
  }
  CAJ_DEBUG("DEBUG: Adding capability %s\n", desc->cap);
  sim->sgrp->caps[desc->cap] = desc;
  
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
  desc->sgrp->caps.erase(desc->cap);
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
  sprintf(uri,"http://%s:%i" CAPS_PATH "/%s", desc->sgrp->ip_addr,
	  (int)desc->sgrp->http_port,desc->cap);
  return uri;
}

static void caps_handler (SoupServer *server,
			  SoupMessage *msg,
			  const char *path,
			  GHashTable *query,
			  SoupClientContext *client,
			  gpointer user_data) {
  struct simgroup_ctx* sgrp = (struct simgroup_ctx*) user_data;
  CAJ_DEBUG_L(sgrp->log, "Got request: %s %s\n",msg->method, path);
  if(strncmp(path,CAPS_PATH "/",6) == 0) {
    path += 6;
    std::map<std::string,cap_descrip*>::iterator it;
    it = sgrp->caps.find(path);
    if(it != sgrp->caps.end()) {
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
  if(ctx->sim->sgrp->release_notes) {
    soup_message_set_response(msg,"text/html", SOUP_MEMORY_COPY,
			      ctx->sim->sgrp->release_notes, 
			      ctx->sim->sgrp->release_notes_len);
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

static void update_script_compiled_cb(void *priv, int success, 
				      const char* output, int output_len) {
  update_script_desc *upd = (update_script_desc*)priv;
  CAJ_DEBUG_L(upd->sim->sgrp->log, "DEBUG: in update_script_compiled_cb\n");
  soup_server_unpause_message(upd->sim->sgrp->soup, upd->msg);
  sim_shutdown_release(upd->sim);
  if(upd->ctx != NULL) {
    caj_llsd *resp; caj_llsd *errors;
    CAJ_DEBUG_L(upd->sim->sgrp->log,  "DEBUG: sending %s script compile response\n", 
		success ? "successful" : "unsuccessful");
    resp = llsd_new_map();
    llsd_map_append(resp,"state",llsd_new_string("complete"));
    llsd_map_append(resp,"new_asset",llsd_new_uuid(upd->asset_id));
    llsd_map_append(resp,"compiled",llsd_new_bool(success));

    errors = llsd_new_array();
    const char *outp = output;
    for(;;) {
      const char *next = strchr(outp,'\n'); if(next == NULL) break;
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

static void update_script_task_stage2(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  update_script_desc *upd = (update_script_desc*)user_data;
  primitive_obj * prim; inventory_item *inv;
  simulator_ctx *sim = ctx->sim;
  
  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  // now we've got the upload, the capability isn't needed anymore, and we
  // delete the user removal hook because our lifetime rules change!
  caps_remove(upd->cap);
  user_remove_delete_hook(ctx, free_update_script_desc, upd);

  // CAJ_DEBUG("Got UpdateScriptTask data >%s<\n", msg->request_body->data);

  struct world_obj* obj = world_object_by_id(ctx->sim, upd->task_id);
  if(obj == NULL) {
    CAJ_WARN("ERROR: UpdateScriptTask for non-existent object\n");
    goto out_fail;
  } else if(!user_can_modify_object(ctx, obj)) {
    CAJ_WARN("ERROR: UpdateScriptTask with insufficient permissions on object\n");
    goto out_fail;
  } else if(obj->type != OBJ_TYPE_PRIM) {
    CAJ_WARN("ERROR: UpdateScriptTask for non-prim object\n");
    goto out_fail;
  }
  prim = (primitive_obj*) obj;

  // FIXME - need to check permissions on script!
  
  uuid_clear(upd->asset_id);

  upd->ctx = ctx; user_add_self_pointer(&upd->ctx);
  upd->sim = ctx->sim; sim_shutdown_hold(ctx->sim);
  upd->msg = msg; soup_server_pause_message(ctx->sgrp->soup, msg);

  inv = prim_update_script(ctx->sim, prim, upd->item_id, upd->script_running, 
			   (unsigned char*)msg->request_body->data, 
			   msg->request_body->length, update_script_compiled_cb,
			   upd);
  if(inv != NULL) uuid_copy(upd->asset_id, inv->asset_id);

  return;
  
 out_fail: // FIXME - not really the right way to fail
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

  printf("Got UpdateScriptTask:\n");
  llsd_pretty_print(llsd, 1);
  item_id = llsd_map_lookup(llsd, "item_id");
  task_id = llsd_map_lookup(llsd, "task_id");
  script_running = llsd_map_lookup(llsd, "is_script_running"); // FIXME - use!
  if(!(LLSD_IS(item_id, LLSD_UUID) && LLSD_IS(task_id, LLSD_UUID) && 
       LLSD_IS(script_running, LLSD_INT))) goto free_fail;
  
  upd = new update_script_desc();
  uuid_copy(upd->task_id, task_id->t.uuid);
  uuid_copy(upd->item_id, item_id->t.uuid);
  upd->script_running = script_running->t.i;
  upd->cap = caps_new_capability(ctx->sim, update_script_task_stage2, ctx, upd);
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


static void update_script_agent_cb(void *priv, int success, uuid_t asset_id) {
  update_script_desc *upd = (update_script_desc*)priv;
  soup_server_unpause_message(upd->sim->sgrp->soup, upd->msg);
  sim_shutdown_release(upd->sim);
  if(upd->ctx != NULL) {
    if(success) {
      caj_llsd *resp; caj_llsd *errors;
      resp = llsd_new_map();
      llsd_map_append(resp,"state",llsd_new_string("complete"));
      llsd_map_append(resp,"new_asset",llsd_new_uuid(asset_id));
      llsd_map_append(resp,"new_inventory_item",llsd_new_uuid(upd->item_id));
      llsd_soup_set_response(upd->msg, resp);
      llsd_free(resp);
    } else {
      printf("ERROR: script upload to agent inventory failed\n");
      soup_message_set_status(upd->msg,500);
    }
    user_del_self_pointer(&upd->ctx);
  } else {
    soup_message_set_status(upd->msg,500);
  }
  delete upd;

}

static void update_script_agent_stage2(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  update_script_desc *upd = (update_script_desc*)user_data;
  primitive_obj * prim; inventory_item *inv;
  simulator_ctx *sim = ctx->sim;
  
  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  // now we've got the upload, the capability isn't needed anymore, and we
  // delete the user removal hook because our lifetime rules change!
  caps_remove(upd->cap);
  user_remove_delete_hook(ctx, free_update_script_desc, upd);

  {
    caj_string data;
    data.data = (unsigned char*)msg->request_body->data;
    data.len = msg->request_body->length;

    upd->ctx = ctx; user_add_self_pointer(&upd->ctx);
    upd->sim = ctx->sim; sim_shutdown_hold(ctx->sim);
    upd->msg = msg; soup_server_pause_message(ctx->sgrp->soup, msg);

    user_update_inventory_asset(ctx, upd->item_id, ASSET_LSL_TEXT, &data,
				update_script_agent_cb, upd);
    return;
  }

 out_fail: // FIXME - not really the right way to fail
  delete upd;
  soup_message_set_status(msg,400);
}

static void update_script_agent(SoupMessage *msg, user_ctx* ctx, void *user_data) {
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

  printf("Got UpdateScriptAgent:\n");
  llsd_pretty_print(llsd, 1);
  item_id = llsd_map_lookup(llsd, "item_id");
  if(!LLSD_IS(item_id, LLSD_UUID)) goto free_fail;
  
  upd = new update_script_desc();
  uuid_clear(upd->task_id);
  uuid_copy(upd->item_id, item_id->t.uuid);
  upd->script_running = FALSE;
  upd->cap = caps_new_capability(ctx->sim, update_script_agent_stage2, ctx, upd);
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

struct file_agent_inv {
  int8_t asset_type, inv_type;
  char *name, *description;
  uuid_t folder_id, asset_id, item_id;
  cap_descrip* cap;
  user_ctx *ctx; simulator_ctx *sim; SoupMessage *msg; // for second stage only
};

// used to free the capability etc if the session ends before the client
// actually sends the script. 
static void free_file_agent_inv_desc(user_ctx* ctx, void *priv) {
   file_agent_inv *st = (file_agent_inv*)priv;
   if(st->cap != NULL) caps_remove(st->cap);
   free(st->name); free(st->description); delete st;
}


static void finish_file_agent_inv_stage2(file_agent_inv *st) {
  if(st->ctx != NULL) user_del_self_pointer(&st->ctx);
  soup_server_unpause_message(st->sim->sgrp->soup, st->msg);
  sim_shutdown_release(st->sim);
  free(st->name); free(st->description); delete st;
}

#if 0
static void new_file_inv_sys_folders_cb(user_ctx *ctx, void *user_data) {
  file_agent_inv *st = (file_agent_inv*)user_data;
  inventory_folder *folder;
  assert(st->ctx != NULL); // guaranteed not to happen!
  
  folder = user_find_system_folder(ctx, st->asset_type);
  if(folder == NULL) {
    caj_llsd *resp = llsd_new_map();
    llsd_map_append(resp,"state",llsd_new_string("error"));
    llsd_map_append(resp,"message",llsd_new_string("Couldn't find suitable category"));
    llsd_soup_set_response(st->msg, resp);
    llsd_free(resp);
    finish_file_agent_inv_stage2(st);
  } else {
    

    caj_llsd *resp = llsd_new_map();
    llsd_map_append(resp,"state",llsd_new_string("error"));
    llsd_map_append(resp,"message",llsd_new_string("FIXME: upload code unfinished"));
    llsd_soup_set_response(st->msg, resp);
    llsd_free(resp);
    finish_file_agent_inv_stage2(st);
  }
}
#endif

static void new_file_inv_add_item_cb(void *cb_priv, int success, 
				     uuid_t item_id) {
  file_agent_inv *st = (file_agent_inv*)cb_priv;
  caj_llsd *resp = llsd_new_map();
  if(success) {
    // FIXME - send new_everyone_mask/new_group_mask/new_next_owner_mask?
    llsd_map_append(resp,"state",llsd_new_string("complete"));
    llsd_map_append(resp,"new_inventory_item",llsd_new_uuid(item_id));
    llsd_map_append(resp,"new_asset",llsd_new_uuid(st->asset_id));
  } else {
    llsd_map_append(resp,"state",llsd_new_string("error"));
    llsd_map_append(resp,"message",llsd_new_string("Couldn't save inventory item"));
  }
  llsd_soup_set_response(st->msg, resp);
  llsd_free(resp);
  finish_file_agent_inv_stage2(st);
}

static void new_file_inv_to_folder(file_agent_inv *st) {
  char creator_id[40]; uuid_unparse(st->ctx->user_id, creator_id);
  inventory_item item;
  item.name = st->name; item.description = st->description;
  uuid_copy(item.folder_id, st->folder_id);
  item.creator_id = creator_id;
  uuid_copy(item.creator_as_uuid, st->ctx->user_id);

  item.perms.base = item.perms.current = PERM_FULL_PERMS;
  item.perms.group = item.perms.everyone = 0;
  item.perms.next = PERM_FULL_PERMS; // FIXME - not right.
  
  item.inv_type = st->inv_type; item.asset_type = st->asset_type;
  item.sale_type = 0; // FIXME - magic constant!
  item.group_owned = FALSE; uuid_clear(item.group_id);
  uuid_copy(item.asset_id, st->asset_id);
  item.flags = 0; // FIXME - ???
  item.sale_price = 0;
  item.creation_date = time(NULL);
  
  user_add_inventory_item(st->ctx, &item, new_file_inv_add_item_cb, st);
  
}

static void new_file_inv_asset_cb(uuid_t asset_id, void *user_data) {
  file_agent_inv *st = (file_agent_inv*)user_data;

  if(st->ctx == NULL) {
    soup_message_set_status(st->msg,500);
    finish_file_agent_inv_stage2(st);
  } else if(uuid_is_null(asset_id)) {
    caj_llsd *resp = llsd_new_map();
    llsd_map_append(resp,"state",llsd_new_string("error"));
    llsd_map_append(resp,"message",llsd_new_string("Couldn't save asset"));
    llsd_soup_set_response(st->msg, resp);
    llsd_free(resp);
    finish_file_agent_inv_stage2(st);
  } else if(uuid_is_null(st->folder_id)) {
    caj_llsd *resp = llsd_new_map();
    llsd_map_append(resp,"state",llsd_new_string("error"));
    llsd_map_append(resp,"message",llsd_new_string("FIXME: was sent null folder ID"));
    llsd_soup_set_response(st->msg, resp);
    llsd_free(resp);
    finish_file_agent_inv_stage2(st);
    //user_fetch_system_folders(st->ctx, new_file_inv_sys_folders_cb, st);
  } else {
    uuid_copy(st->asset_id, asset_id);
    new_file_inv_to_folder(st);
  }
}

static void new_file_inv_stage2(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  file_agent_inv *st = (file_agent_inv*)user_data;
  // now we've got the upload, the capability isn't needed anymore, and we
  // delete the user removal hook because our lifetime rules change!
  caps_remove(st->cap); st->cap = NULL;
  user_remove_delete_hook(ctx, free_file_agent_inv_desc, st);

  // FIXME - TODO
  struct simple_asset asset;
  asset.data.data = (unsigned char*)msg->request_body->data;
  asset.data.len = msg->request_body->length;
  asset.type = st->asset_type;
  asset.name = st->name;
  asset.description = st->description;
  uuid_generate(asset.id);

  st->ctx = ctx; user_add_self_pointer(&st->ctx);
  st->sim = ctx->sim; sim_shutdown_hold(ctx->sim);
  st->msg = msg; soup_server_pause_message(ctx->sgrp->soup, msg);

  ctx->sgrp->gridh.put_asset(ctx->sgrp, &asset, new_file_inv_asset_cb, st);
}

void new_file_agent_inventory(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  int8_t asset_type, inv_type;
  caj_llsd *llsd, *resp; 
  caj_llsd *asset_type_str, *inv_type_str;
  caj_llsd *name, *description, *folder_id;
  file_agent_inv *st;
  
  if(msg->request_body->length > 65536) goto fail;
  llsd = llsd_parse_xml(msg->request_body->data, msg->request_body->length);
  if(llsd == NULL) goto fail;
  if(!LLSD_IS(llsd, LLSD_MAP)) goto free_fail;
  
  printf("Got NewFileAgentInventory:\n");
  llsd_pretty_print(llsd, 1);

  asset_type_str = llsd_map_lookup(llsd, "asset_type");
  inv_type_str = llsd_map_lookup(llsd, "inventory_type");
  name = llsd_map_lookup(llsd, "name");
  description = llsd_map_lookup(llsd, "description");
  folder_id = llsd_map_lookup(llsd, "folder_id");

  // FIXME - client can optionally request permissions via
  // optional everyone_mask/group_mask/next_owner_mask
  
  if(!(LLSD_IS(asset_type_str, LLSD_STRING) && LLSD_IS(inv_type_str, LLSD_STRING) && 
       LLSD_IS(name, LLSD_STRING) &&  LLSD_IS(description, LLSD_STRING) &&
       LLSD_IS(folder_id, LLSD_UUID))) goto free_fail;

  // FIXME - use the list currently in caj_omv_udp.cpp
  if(strcmp(asset_type_str->t.str, "texture") == 0) asset_type = ASSET_TEXTURE;
  else if(strcmp(asset_type_str->t.str, "animation") == 0) asset_type = ASSET_ANIMATION;
  /* else if(strcmp(asset_type_str, "sound") == 0) asset_type = ASSET_TYPE_SOUND; */
  else {
    resp = llsd_new_map();
    llsd_map_append(resp,"state",llsd_new_string("error"));
    llsd_map_append(resp,"message",llsd_new_string("Unsupported asset type."));

    llsd_free(llsd);
    llsd_soup_set_response(msg, resp);
    llsd_free(resp);
    return;
  }

  // FIXME - use the list currently in caj_omv_udp.cpp
  if(strcmp(inv_type_str->t.str, "texture") == 0) inv_type = INV_TYPE_TEXTURE;
  else if(strcmp(inv_type_str->t.str, "animation") == 0) inv_type = INV_TYPE_ANIMATION;
  else {
    resp = llsd_new_map();
    llsd_map_append(resp,"state",llsd_new_string("error"));
    llsd_map_append(resp,"message",llsd_new_string("Unsupported inventory type."));

    llsd_free(llsd);
    llsd_soup_set_response(msg, resp);
    llsd_free(resp);
    return;
  }

  st = new file_agent_inv();
  uuid_copy(st->folder_id, folder_id->t.uuid);
  st->asset_type = asset_type; st->inv_type = inv_type;
  st->cap = caps_new_capability(ctx->sim, new_file_inv_stage2, ctx, st);
  st->name = strdup(name->t.str); 
  st->description = strdup(description->t.str);
  user_add_delete_hook(ctx, free_file_agent_inv_desc, st);
  
  resp = llsd_new_map();
  llsd_map_append(resp,"state",llsd_new_string("upload"));
  llsd_map_append(resp,"uploader",llsd_new_string_take(caps_get_uri(st->cap)));

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
  user_add_named_cap(sim,"UpdateScriptAgent",update_script_agent,ctx,NULL); 
  user_add_named_cap(sim,"NewFileAgentInventory",new_file_agent_inventory,ctx,NULL);  
}

void user_int_caps_cleanup(user_ctx *ctx) {
  for(named_caps_iter iter = ctx->named_caps.begin(); 
      iter != ctx->named_caps.end(); iter++) {
    caps_remove(iter->second);
  }
  caps_remove(ctx->seed_cap);
}

// -- END of caps code --

void caj_int_caps_init(simgroup_ctx *sgrp) {
  soup_server_add_handler(sgrp->soup, CAPS_PATH, caps_handler, 
			  sgrp, NULL);
}

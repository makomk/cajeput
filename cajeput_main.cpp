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
#include "cajeput_int.h"
#include "terrain_compress.h"
#include "caj_parse_nini.h"
#include <sys/types.h> 
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

void caj_sanitise_perms(struct permission_flags *perms) {
  perms->current &= perms->base;
  perms->group &= perms->base;
  perms->everyone &= perms->base;
  perms->next &= perms->base;
}

// --- START sim query code ---

struct simulator_ctx* caj_local_sim_by_region_handle(struct simgroup_ctx *sgrp,
						     uint64_t region_handle) {
  std::map<uint64_t, simulator_ctx*>::iterator iter = 
    sgrp->sims.find(region_handle);
  if(iter == sgrp->sims.end())
    return NULL;
  else return iter->second;
}

struct simgroup_ctx* sim_get_simgroup(struct simulator_ctx* sim) {
  return sim->sgrp;
}

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
  return sim->sgrp->ip_addr;
}
void sim_get_region_uuid(struct simulator_ctx *sim, uuid_t u) {
  uuid_copy(u, sim->region_id);
}
void sim_get_owner_uuid(struct simulator_ctx *sim, uuid_t u) {
  uuid_copy(u, sim->owner);
}
uint16_t sim_get_http_port(struct simulator_ctx *sim) {
  return sim->sgrp->http_port;
}
uint16_t sim_get_udp_port(struct simulator_ctx *sim) {
  return sim->udp_port;
}
void* caj_get_grid_priv(struct simgroup_ctx *sgrp) {
  return sgrp->grid_priv;
}
void caj_set_grid_priv(struct simgroup_ctx *sgrp, void* p) {
  sgrp->grid_priv = p;
}
void* sim_get_grid_priv(struct simulator_ctx *sim) {
  // compatability  - FIXME remove
  return sim->sgrp->grid_priv;
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
float sim_get_terrain_height(struct simulator_ctx *sim, int x, int y) {
  return sim->terrain[x + y*256];
}
double caj_get_timer(struct simgroup_ctx *sgrp) {
  return g_timer_elapsed(sgrp->timer, NULL);
}
// --- END sim query code ---


char *sgrp_config_get_value(struct simgroup_ctx *sgrp, const char* section,
			   const char* key) {
  return g_key_file_get_value(sgrp->config,section,key,NULL);
}

gboolean sgrp_config_get_bool(struct simgroup_ctx *sgrp, const char* section,
			      const char* key, GError **error) {
  return g_key_file_get_boolean(sgrp->config,section,key,error);
}

char *sim_config_get_value(struct simulator_ctx *sim, const char* key,
			   GError **error) {
  return g_key_file_get_value(sim->sgrp->config,sim->cfg_sect,key,error);
}


gint sim_config_get_integer(struct simulator_ctx *sim, const char* key,
			    GError **error) {
  return g_key_file_get_integer(sim->sgrp->config,sim->cfg_sect,key,error);
}

// legacy code - FIXME remove this
void sim_queue_soup_message(struct simulator_ctx *sim, SoupMessage* msg,
			    SoupSessionCallback callback, void* user_data) {
  soup_session_queue_message(sim->sgrp->soup_session, msg, callback, user_data);
}

void caj_queue_soup_message(struct simgroup_ctx *sgrp, SoupMessage* msg,
			    SoupSessionCallback callback, void* user_data) {
  soup_session_queue_message(sgrp->soup_session, msg, callback, user_data);
}


void caj_http_add_handler (struct simgroup_ctx *sgrp,
			   const char            *path,
			   SoupServerCallback     callback,
			   gpointer               user_data,
			   GDestroyNotify         destroy) {
  soup_server_add_handler(sgrp->soup,path,callback,user_data,destroy);
}

void caj_shutdown_hold(struct simgroup_ctx *sgrp) {
  sgrp->hold_off_shutdown++;
}

void caj_shutdown_release(struct simgroup_ctx *sgrp) {
  sgrp->hold_off_shutdown--;
}

void sim_shutdown_hold(struct simulator_ctx *sim) {
  sim->sgrp->hold_off_shutdown++;
}

void sim_shutdown_release(struct simulator_ctx *sim) {
  sim->sgrp->hold_off_shutdown--;
}

static GMainLoop *main_loop;


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




static void simstatus_rest_handler (SoupServer *server,
				SoupMessage *msg,
				const char *path,
				GHashTable *query,
				SoupClientContext *client,
				gpointer user_data) {
  /* struct simgroup_ctx* sgrp = (struct simgroup_ctx*) user_data; */
  // For OpenSim grid protocol - should we check actual status somehow?
  soup_message_set_status(msg,200);
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    "OK",2);
}


static volatile int shutting_down = 0;

static void shutdown_sim(simulator_ctx *sim) {

  // FIXME - want to shutdown physics here, really.
  sim_call_shutdown_hook(sim);


  world_int_dump_prims(sim);


  for(std::map<uint32_t, world_obj*>::iterator iter = sim->localid_map.begin();
      iter != sim->localid_map.end(); /* nowt */) {
    if(iter->second->type == OBJ_TYPE_PRIM) {
      // std::map<uint32_t, world_obj*>::iterator iter2 = iter; iter2++;
      primitive_obj *prim = (primitive_obj*)iter->second;
      world_delete_prim(sim, prim);
      iter = sim->localid_map.begin();
    } else iter++;
  }

  sim->physh.destroy(sim, sim->phys_priv);
  sim->phys_priv = NULL;
  
  free(sim->cfg_sect); free(sim->shortname);
  
  world_octree_destroy(sim->world_tree);
  delete sim->collisions;
  g_free(sim->name);
  g_free(sim->welcome_message);
  delete[] sim->terrain;
  delete sim;

}

static gboolean shutdown_timer(gpointer data) {
  struct simgroup_ctx* sgrp = (struct simgroup_ctx*) data;
  if(sgrp->hold_off_shutdown) {
    printf("Shutting down (%i items pending)\n", sgrp->hold_off_shutdown);
    return TRUE;
  }

  soup_session_abort(sgrp->soup_session);

  for(std::map<uint64_t, simulator_ctx*>::iterator iter = sgrp->sims.begin();
      iter != sgrp->sims.end(); iter++) {
    simulator_ctx *sim = iter->second;
    shutdown_sim(sim);
  }

  soup_server_quit(sgrp->soup);

  sgrp->gridh.cleanup(sgrp);
  sgrp->grid_priv = NULL;

  g_object_unref(sgrp->soup);

  soup_session_abort(sgrp->soup_session); // yes, again!
  g_object_unref(sgrp->soup_session);

  free(sgrp->release_notes);

  for(std::map<obj_uuid_t,texture_desc*>::iterator iter = sgrp->textures.begin();
      iter != sgrp->textures.end(); iter++) {
    struct texture_desc *desc = iter->second;
    free(desc->data); delete[] desc->discard_levels; delete desc;
  }

  for(std::map<obj_uuid_t,inventory_contents*>::iterator iter = sgrp->inv_lib.begin();
      iter != sgrp->inv_lib.end(); iter++) {
    caj_inv_free_contents_desc(iter->second);
  }

  for(std::map<obj_uuid_t,asset_desc*>::iterator iter = sgrp->assets.begin();
      iter != sgrp->assets.end(); iter++) {
    asset_desc *desc = iter->second;
    for(std::set<asset_cb_desc*>::iterator iter = desc->cbs.begin(); 
	iter != desc->cbs.end(); iter++) {
      (*iter)->cb(sgrp, (*iter)->cb_priv, NULL);
      delete (*iter);
    }
    free(desc->asset.name); free(desc->asset.description);
    free(desc->asset.data.data);
    delete desc;
  }
  
  g_free(sgrp->ip_addr);
  g_timer_destroy(sgrp->timer);
  g_key_file_free(sgrp->config);
  sgrp->config = NULL;
  exit(0); // FIXME
  return FALSE;
}

static int user_connection_timeout(user_ctx *ctx) {
  return ctx->flags & AGENT_FLAG_PAUSED ?
    USER_CONNECTION_TIMEOUT_PAUSED : USER_CONNECTION_TIMEOUT;
}

void user_set_paused(user_ctx *ctx) {
  ctx->flags |= AGENT_FLAG_PAUSED;
}

void user_set_unpaused(user_ctx *ctx) {
  ctx->flags &= ~AGENT_FLAG_PAUSED;
}


static gboolean cleanup_timer(gpointer data) {
  struct simgroup_ctx* sgrp = (struct simgroup_ctx*) data;
  double time_now = g_timer_elapsed(sgrp->timer,NULL);
  
  if(shutting_down) {
    printf("Shutting down...\n");
    sgrp->state_flags |= CAJEPUT_SGRP_SHUTTING_DOWN;

    for(std::map<uint64_t, simulator_ctx*>::iterator iter = sgrp->sims.begin();
	iter != sgrp->sims.end(); iter++) {
      simulator_ctx *sim = iter->second;
      while(sim->ctxts)
	user_remove_int(&sim->ctxts);
    }
    
    g_timeout_add(300, shutdown_timer, sgrp);
    return FALSE;
  }

    for(std::map<uint64_t, simulator_ctx*>::iterator iter = sgrp->sims.begin();
	iter != sgrp->sims.end(); iter++) {
      simulator_ctx *sim = iter->second;
      for(user_ctx** user = &sim->ctxts; *user != NULL; ) {
	if((*user)->flags & AGENT_FLAG_IN_SLOW_REMOVAL) {
	  if(--((*user)->shutdown_ctr) <= 0)
	    (*user)->flags |= AGENT_FLAG_PURGE;
	}

	if(time_now - (*user)->last_activity > user_connection_timeout(*user) 
	   || (*user)->flags & AGENT_FLAG_PURGE)
	  user_remove_int(user);
	else {
	  user_ctx *ctx = *user;
	  user_int_event_queue_check_timeout(ctx, time_now);
	  user = &ctx->next;
	}
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


static void load_inv_folders(struct simgroup_ctx *sgrp, const char* filename) {
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
     sgrp->inv_lib[folder_uuid] = caj_inv_new_contents_desc(folder_uuid);

     // FIXME - add folders to their parent folders
   }
   
   g_free(folder_id); g_free(parent_id); g_free(name); g_free(type_str);
 }

 g_strfreev(sect_list);
 g_key_file_free(config);
}

static void load_inv_items(struct simgroup_ctx *sgrp, const char* filename) {
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
       sgrp->inv_lib.find(folder_uuid);
     if(iter == sgrp->inv_lib.end()) {
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
       item->perms.base = item->perms.current = PERM_FULL_PERMS;
       item->perms.next = item->perms.everyone = PERM_FULL_PERMS;
       item->perms.group = 0;
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


static void load_inv_library(struct simgroup_ctx *sgrp) {
  GKeyFile *lib = caj_parse_nini_xml("inventory/Libraries.xml");
  if(lib == NULL) {
    printf("WARNING: couldn't load inventory library\n");
    return;
  }
  
  // add hardcoded library root folder. Ugly.
  uuid_t root_uuid;
  uuid_parse(LIBRARY_ROOT, root_uuid);
  sgrp->inv_lib[root_uuid] = caj_inv_new_contents_desc(root_uuid);

  gchar** sect_list = g_key_file_get_groups(lib, NULL);

  for(int i = 0; sect_list[i] != NULL; i++) {
    gchar* folders_file = g_key_file_get_value(lib, sect_list[i], "foldersFile", NULL);
    gchar* items_file = g_key_file_get_value(lib, sect_list[i], "itemsFile", NULL);

    if(folders_file == NULL || items_file == NULL) {
      printf("ERROR: bad section %s in inventory/Libraries.xml", sect_list[i]);
    } else {
      load_inv_folders(sgrp, folders_file);
      load_inv_items(sgrp, items_file);
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

void load_sim(simgroup_ctx *sgrp, char *shortname) {
  char* sim_uuid, *sim_owner;
  simulator_ctx *sim = new simulator_ctx();
  sim->sgrp = sgrp;
  // sim->hold_off_shutdown = 0;
  sim->state_flags = 0;

  sim->cfg_sect = (char*)malloc(strlen(shortname)+5);
  strcpy(sim->cfg_sect, "sim ");
  strcpy(sim->cfg_sect+4, shortname);
  sim->shortname = strdup(shortname);
  
  sim->terrain = new float[256*256];
  for(int i = 0; i < 256*256; i++) sim->terrain[i] = 25.0f;
  load_terrain(sim,"terrain.raw"); // FIXME!


  // FIXME - better error handling needed
  sim->name = sim_config_get_value(sim,"name",NULL);
  sim->udp_port = sim_config_get_integer(sim,"udp_port",NULL);
  sim->region_x = sim_config_get_integer(sim,"region_x",NULL);
  sim->region_y = sim_config_get_integer(sim,"region_y",NULL);
  sim->region_handle = (uint64_t)(sim->region_x*256)<<32 | (sim->region_y*256);
  sim_uuid = sim_config_get_value(sim,"uuid",NULL);
  sim_owner = sim_config_get_value(sim,"owner",NULL);
  
  // welcome message is optional
  sim->welcome_message = sim_config_get_value(sim,"welcome_message",NULL);

  if(sim->udp_port == 0 || sim->region_x == 0 || sim->region_y == 0 ||
     sim->name == NULL || sim_uuid == NULL ||
     uuid_parse(sim_uuid,sim->region_id)) {
    // FIXME - cleanup;
    printf("Error: bad config\n"); return;
  }

  g_free(sim_uuid);

  if(sim_owner == NULL) {
    uuid_clear(sim->owner);
  } else {
    if(uuid_parse(sim_owner,sim->owner)) {
      printf("Error: bad owner UUID\n"); return;
    }
    g_free(sim_owner);
  }

  sim->world_tree = world_octree_create();
  sim->ctxts = NULL;
  //uuid_generate_random(sim->region_secret);
  sim_int_init_udp(sim);
  
  g_timeout_add(100, av_update_timer, sim);

  sim->collisions = new collision_state();

  if(!cajeput_physics_init(CAJEPUT_API_VERSION, sim, 
			     &sim->phys_priv, &sim->physh)) {
    printf("Couldn't init physics engine!\n"); return;
  }

  memset(&sim->scripth, 0, sizeof(sim->scripth));
  if(!caj_scripting_init(CAJEPUT_API_VERSION, sim, &sim->script_priv,
			 &sim->scripth)) {
    printf("Couldn't init script engine!\n"); return;
  }

  world_int_load_prims(sim);
  // world_insert_demo_objects(sim);

  sgrp->sims[sim->region_handle] = sim;

  sgrp->gridh.do_grid_login(sgrp,sim);
  
}

int main(void) {
  g_thread_init(NULL);
  g_type_init();
  terrain_init_compress(); // FIXME - move to omuser module
  create_cache_dirs();

  main_loop = g_main_loop_new(NULL, false);
  srandom(time(NULL));

  struct simgroup_ctx* sgrp = new simgroup_ctx();
  sgrp->state_flags = 0;

  sgrp->config = g_key_file_new();
  sgrp->hold_off_shutdown = 0;
  // Note - I should make sure to add G_KEY_FILE_KEEP_COMMENTS/TRANSLATIONS
  // if I ever want to modify the config and save it back.
  if(!g_key_file_load_from_file(sgrp->config,  "server.ini", 
				G_KEY_FILE_NONE, NULL)) {
    printf("Config file load failed\n"); 
    g_key_file_free(sgrp->config); delete sgrp;
    return 1;
  }

  sgrp->http_port = g_key_file_get_integer(sgrp->config, "simgroup","http_port",NULL);
  if(sgrp->http_port == 0) {
    printf("ERROR: http port not configured\n"); return 1;
  }

  sgrp->ip_addr = g_key_file_get_value(sgrp->config, "simgroup", "ip_address", NULL);
  if(sgrp->ip_addr == NULL) {
    printf("ERROR: IP address not configured\n"); return 1;
  }

  gsize len;

  char **sims = g_key_file_get_string_list(sgrp->config, "simgroup",
					   "sims", &len, NULL);
  if(sims == NULL || len == 0) {
    printf("ERROR: no sims enabled. Check config file.\n");
    g_key_file_free(sgrp->config); delete sgrp;
    return 1;
  }

  sgrp->timer = g_timer_new();
  sgrp->soup_session = soup_session_async_new();
  sgrp->soup = soup_server_new(SOUP_SERVER_PORT, (int)sgrp->http_port, NULL);
  if(sgrp->soup == NULL) {
    printf("HTTP server init failed\n"); return 1;
  }
  
  
  
  load_inv_library(sgrp);

  sgrp->release_notes = read_text_file("sim-release-notes.html", 
				      &sgrp->release_notes_len);
  if(sgrp->release_notes == NULL) {
    printf("WARNING: Release notes load failed\n"); 
  }

  memset(&sgrp->gridh, 0, sizeof(sgrp->gridh));
  if(!cajeput_grid_glue_init(CAJEPUT_API_VERSION, sgrp, 
			     &sgrp->grid_priv, &sgrp->gridh)) {
    printf("Couldn't init grid glue!\n"); return 1;
  }

  for(unsigned i = 0; i < len; i++) {
    printf("DEBUG: loading sim %s\n", sims[i]);
    load_sim(sgrp, sims[i]);
    
  }
  g_strfreev(sims);

  caj_int_caps_init(sgrp);
  soup_server_add_handler(sgrp->soup, "/simstatus", simstatus_rest_handler, 
			  sgrp, NULL);
  soup_server_run_async(sgrp->soup);

  g_timeout_add(1000, cleanup_timer, sgrp);
  set_sigint_handler();
  g_main_loop_run(main_loop);

  // Cleanup runs elsewhere.
  return 0;
}

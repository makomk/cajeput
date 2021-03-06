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
#include "cajeput_user.h"
#include "cajeput_grid_glue.h"
#include <libsoup/soup.h>
#include "caj_types.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include "opensim_grid_glue.h"
#include <cassert>
#include <netinet/ip.h>

// ------------------ INCOMING AGENTS --------------------------------------

void pretty_print_json(JsonNode *node) {
  gchar* data; gsize length;
  GValue val = { 0, };
  JsonGenerator* gen = json_generator_new();
  json_generator_set_root(gen, node);
  g_value_init (&val, G_TYPE_BOOLEAN);
  g_value_set_boolean(&val, 1);
  g_object_set_property (G_OBJECT (gen), "pretty", &val);
  g_value_unset (&val);
  data = json_generator_to_data(gen, &length);
  g_object_unref(gen);
  printf("%s\n",data);
  g_free(data);
}

static int helper_json_get_boolean(JsonObject *obj, const char* key, int *val) {
  JsonNode *node = json_object_get_member(obj, key);
  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_VALUE)
    return -1;
  *val = json_node_get_boolean(node);
  return 0;
}

static int helper_json_to_uuid(JsonNode *node, uuid_t uuid) {
  const char* str;
  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_VALUE)
    return -1;
  str = json_node_get_string(node);
  if(str == NULL) return -1;
  return uuid_parse(str, uuid);
}

// FIXME - use helper_json_to_uuid
static int helper_json_get_uuid(JsonObject *obj, const char* key, uuid_t uuid) {
  const char* str;
  JsonNode *node = json_object_get_member(obj, key);
  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_VALUE)
    return -1;
  str = json_node_get_string(node);
  if(str == NULL) return -1;
  return uuid_parse(str, uuid);
}

static const char* helper_json_get_string(JsonObject *obj, const char* key) {
  JsonNode *node = json_object_get_member(obj, key);
  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_VALUE)
    return NULL;
  return json_node_get_string(node);
}

static unsigned char* helper_json_get_bin(JsonNode *node, int *len_out) {
  JsonArray *arr; unsigned char* buf; int len;
  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_ARRAY)
    return NULL;
  arr = json_node_get_array(node);
  len = json_array_get_length(arr);
  if(len < 0) return NULL;
  buf = (unsigned char*)malloc(len);
  for(int i = 0; i < len; i++) {
    JsonNode* item = json_array_get_element(arr,i);
    if(item == NULL || JSON_NODE_TYPE(item) != JSON_NODE_VALUE) {
      free(buf); return NULL;
    }
    buf[i] = (unsigned char)json_node_get_int(item);
  }
  *len_out = len;
  return buf;
}

struct agent_POST_state {
  SoupServer *server;
  SoupMessage *msg;
  JsonParser *parser;
  struct simgroup_ctx* sgrp;
};

// FIXME - call this in login
static void add_child_cap(user_ctx *user, uint64_t region_handle, 
			  const char* seed_path) {
  USER_PRIV_DEF2(user);
  user_glue->child_seeds[region_handle] = std::string(seed_path);
}

static char* make_child_cap(user_ctx *user, uint64_t region_handle) {
  USER_PRIV_DEF2(user);
  std::map<uint64_t,std::string>::iterator iter = 
    user_glue->child_seeds.find(region_handle);
  if(iter == user_glue->child_seeds.end()) {
    uuid_t u; char* ret;
    uuid_generate(u);
    ret = (char*)malloc(40);
    uuid_unparse(u, ret);
    user_glue->child_seeds[region_handle] = std::string(ret);
    return ret;
  } else {
    return strdup(iter->second.c_str());
  }
}

static void fill_in_child_caps(user_ctx *user, JsonNode *node, grid_glue_ctx *grid) {
  USER_PRIV_DEF2(user);
  assert(user_glue != NULL);
  if(node == NULL|| JSON_NODE_TYPE(node) != JSON_NODE_ARRAY) 
    return;
  JsonArray *arr = json_node_get_array(node);

  int len = json_array_get_length(arr);
  for(int i = 0; i < len; i++) {
    JsonNode* item = json_array_get_element(arr,i);
    if(item == NULL || JSON_NODE_TYPE(item) != JSON_NODE_OBJECT) {
      CAJ_WARN("ERROR: Child cap item not object\n"); continue;
    }
    JsonObject* obj = json_node_get_object(item);
    const char *handle = helper_json_get_string(obj,"handle");
    const char *seed = helper_json_get_string(obj,"seed");
    if(handle == NULL || seed == NULL) {
      CAJ_WARN("ERROR: Child cap item bad\n"); continue;
    }
    uint64_t region_handle = atoll(handle);
    user_glue->child_seeds[region_handle] = std::string(seed);
    CAJ_DEBUG(" DEBUG:  filled in child cap %s: %s\n", handle, seed);
  }
}

static void set_wearables_from_json(user_ctx *user, JsonNode *node, grid_glue_ctx *grid) {
  JsonArray *arr; int len;
   if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_ARRAY)
     return;
   arr = json_node_get_array(node);
   if(arr == NULL) return;
   len = json_array_get_length(arr);

   for(int i = 0; i+1 < len; i += 2) {
     uuid_t item_id, asset_id;
     if(helper_json_to_uuid(json_array_get_element(arr, i), item_id) ||
	helper_json_to_uuid(json_array_get_element(arr, i+1), asset_id)) {
       CAJ_ERROR("ERROR: failed to extract wearable %i from JSON\n",i/2);
     } else {
       user_set_wearable(user, i/2, item_id, asset_id);
     }
   }

   CAJ_DEBUG("DEBUG: set wearables from JSON\n");
}

static void agent_POST_stage2(void *priv, int is_ok) {
  int is_child = 0; uint64_t region_handle;
  char seed_cap[50]; const char *caps_path, *s;
  agent_POST_state* st = (agent_POST_state*)priv;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  JsonObject *object = json_node_get_object(json_parser_get_root(st->parser));
  struct sim_new_user uinfo; JsonNode *node;
  user_ctx *user; simulator_ctx *sim;

  if(!is_ok) goto out;
 
  soup_server_unpause_message(st->server,st->msg);
  if(helper_json_get_uuid(object, "agent_id", uinfo.user_id)) {
    CAJ_ERROR("ERROR: agent POST: couldn't get agent_id\n");
    is_ok = 0; goto out;
  }
  if(helper_json_get_uuid(object, "session_id", uinfo.session_id)) {
    CAJ_ERROR("ERROR: agent POST: couldn't get session_id\n");
    is_ok = 0; goto out;
  }
  if(helper_json_get_uuid(object, "secure_session_id", uinfo.secure_session_id)) {
    CAJ_ERROR("ERROR: agent POST: couldn't get secure_session_id\n");
    is_ok = 0; goto out;
  }

  s = helper_json_get_string(object, "destination_handle");
  if(s == NULL) {
    // newer versions use destination_x and destination_y

    uint32_t region_x, region_y;
    s = helper_json_get_string(object, "destination_x");
    if(s == NULL) {
      CAJ_ERROR("ERROR: agent POST: couldn't get region X pos\n");
      is_ok = 0; goto out;
    }
    region_x = atol(s);
    s = helper_json_get_string(object, "destination_y");
    if(s == NULL) {
      CAJ_ERROR("ERROR: agent POST: couldn't get region Y pos\n");
      is_ok = 0; goto out;
    }
    region_y = atol(s);
    region_handle = ((uint64_t)region_x << 32) | region_y;
  } else {
    region_handle = atoll(s);
  }
  if(region_handle == 0) {
    CAJ_ERROR("ERROR: agent POST: bogus region handle\n");
    is_ok = 0; goto out;    
  }

  sim = caj_local_sim_by_region_handle(st->sgrp, region_handle);
  if(sim == NULL) {
    CAJ_ERROR("ERROR: agent POST: not one of our regions\n");
    is_ok = 0; goto out;
  }

  // HACK
  uinfo.first_name = (char*)helper_json_get_string(object, "first_name");
  uinfo.last_name = (char*)helper_json_get_string(object, "last_name");
  if(uinfo.first_name == NULL || uinfo.last_name == NULL) {
    CAJ_ERROR("ERROR: agent POST: couldn't get user name\n");
    is_ok = 0; goto out;
  }
  caps_path = (char*)helper_json_get_string(object, "caps_path");
  s = (char*)helper_json_get_string(object, "circuit_code");
  if(caps_path == NULL || s == NULL) {
    CAJ_ERROR("ERROR: agent POST: caps path or circuit_code missing\n");
    is_ok = 0; goto out;
  }
  uinfo.circuit_code = atol(s);
  if(helper_json_get_boolean(object, "child", &is_child)) {
    // HACK - this is now used for initial login, but without the "child"
    // element!
    CAJ_ERROR("ERROR: agent POST: \"child\" attribute missing (non-fatal)\n");
    //is_ok = 0; goto out;    
    is_child = FALSE; 
  }

  // FIXME FIXME: fill in start position if this is the initial login.
  
  // WTF? Why such an odd path?
  snprintf(seed_cap,50,"%s0000/",caps_path);
  uinfo.seed_cap = seed_cap;
  uinfo.is_child = is_child;
  user = sim_prepare_new_user(sim, &uinfo);

  if(user != NULL) {
    fill_in_child_caps(user, json_object_get_member(object,"children_seeds"), grid);
    add_child_cap(user, region_handle, caps_path);

    // used for the initial login
    node = json_object_get_member(object,"wearables");
    if(node != NULL) {
      set_wearables_from_json(user, node, grid);
    }

    caj_vector3 start_pos;

    s = helper_json_get_string(object, "start_pos");
    if(s != NULL && sscanf(s, "<%f, %f, %f>",&start_pos.x,
			   &start_pos.y, &start_pos.z) == 3) {
      // FIXME - what about look_at? Is it not sent?
      user_set_start_pos(user, &start_pos, &start_pos);
    }
  }

 out:
  soup_message_set_status(st->msg,200); // FIXME - application/json?
  // FIXME - generate proper response!
  soup_message_set_response(st->msg,"text/plain",SOUP_MEMORY_STATIC,
			    is_ok?"{\"reason\":\"\",\"success\":true}":
			    "{\"reason\":\"\",\"success\":false}", is_ok?28:29); 
  g_object_unref(st->parser); delete st;
}

static void agent_POST_handler(SoupServer *server,
			       SoupMessage *msg,
			       uuid_t agent_id,
			       JsonParser *parser,
			       struct simgroup_ctx* sgrp) {
  const char *agent_id_st, *session_id_st;
  JsonObject *object; uuid_t u;
  JsonNode * node = json_parser_get_root(parser);
  GRID_PRIV_DEF_SGRP(sgrp);

  if(JSON_NODE_TYPE(node) != JSON_NODE_OBJECT) {
    CAJ_ERROR("ERROR: Root JSON node not object?!\n");
    goto out_fail;
  }
  object = json_node_get_object(node);
  agent_id_st = helper_json_get_string(object, "agent_id");
  session_id_st = helper_json_get_string(object,"session_id");
  if(agent_id_st == NULL || session_id_st == NULL) {
    CAJ_ERROR("ERROR: Missing agent or session id from agent REST POST\n");
    goto out_fail;
  }
  if(uuid_parse(agent_id_st,u) || uuid_compare(u,agent_id) != 0) {
    CAJ_ERROR("ERROR: Bad/mismatched agent id in agent REST POST\n");
    goto out_fail;
  }
  
  soup_server_pause_message(server,msg);

  {
    agent_POST_state *state = new agent_POST_state();
    state->server = server;
    state->msg = msg;
    g_object_ref(parser);  
    state->parser = parser;
    state->sgrp = sgrp;
    osglue_validate_session(sgrp, agent_id_st, session_id_st, grid,
			    agent_POST_stage2, state);
  }

  return;

 out_fail:
  soup_message_set_status(msg,400);
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    "false",5);  
}

static void agent_PUT_handler(SoupServer *server,
			      SoupMessage *msg,
			      uuid_t agent_id,
			      JsonParser *parser,
			      struct simgroup_ctx* sgrp) {
  JsonNode * node = json_parser_get_root(parser); // reused later
  JsonObject *object; user_ctx *user;
  const char *msg_type, *callback_uri, *s, *s2, *s3;
  user_grid_glue *user_glue; simulator_ctx *sim;
  uuid_t user_id, session_id; uint64_t region_handle;
  GRID_PRIV_DEF_SGRP(sgrp);
  int is_ok = 1;

  if(JSON_NODE_TYPE(node) != JSON_NODE_OBJECT) {
    CAJ_WARN("Root JSON node not object?!\n");
    goto out_fail;
  }
  object = json_node_get_object(node);

  // FIXME - need to actually update the agent
  // struct sim_new_user uinfo;
 
  if(helper_json_get_uuid(object, "agent_uuid", user_id)) {
    CAJ_WARN("DEBUG agent PUT: couldn't get agent_id\n");
    is_ok = 0; goto out_fail;
  }

  // FIXME - code duplication with PUT handler
  s = helper_json_get_string(object, "destination_handle");
  if(s == NULL) {
    uint32_t region_x, region_y;
    s = helper_json_get_string(object, "destination_x");
    if(s == NULL) {
      CAJ_ERROR("ERROR: agent PUT: couldn't get region X pos\n");
      is_ok = 0; goto out;
    }
    region_x = atol(s);
    s = helper_json_get_string(object, "destination_y");
    if(s == NULL) {
      CAJ_ERROR("ERROR: agent PUT: couldn't get region Y pos\n");
      is_ok = 0; goto out;
    }
    region_y = atol(s);
    region_handle = ((uint64_t)region_x << 32) | region_y;
  } else {
    region_handle = atoll(s);
  }
  if(region_handle == 0) {
    CAJ_ERROR("ERROR: agent PUT: bogus region handle\n");
    is_ok = 0; goto out;    
  }

  sim = caj_local_sim_by_region_handle(sgrp, region_handle);
  if(sim == NULL) {
    CAJ_ERROR("ERROR: agent PUT: not one of our regions\n");
    is_ok = 0; goto out;
  }


#if 0 /* not meaningful - just get zero UUID */
  if(helper_json_get_uuid(object, "session_uuid", session_id)) {
    CAJ_ERROR("ERROR: agent PUT: couldn't get session_id\n");
    is_ok = 0; goto out_fail;
  }
#endif
  msg_type = helper_json_get_string(object,"message_type");
  if(msg_type == NULL) msg_type = "AgentData";
 
  //user = user_find_session(sim, user_id, session_id);
  user = user_find_ctx(sim, user_id);
  if(user == NULL) {
    char agent_str[40], session_str[40]; // DEBUG
    uuid_unparse(user_id,agent_str);
    uuid_unparse(session_id,session_str);    
    CAJ_ERROR("ERROR: PUT for unknown agent, user_id=%s session_id=%s\n",
	     agent_str, session_str);
    is_ok = 0; goto out;
  }

  user_glue = (user_grid_glue*)user_get_grid_priv(user);
  assert(user_glue != NULL);
  
  if(!(user_get_flags(user) & AGENT_FLAG_CHILD)) {
    CAJ_ERROR("ERROR: PUT for non-child agent\n");
    goto out_fail;
  }

  if(strcmp(msg_type,"AgentData") == 0) {
    caj_vector3 start_pos;
    user_set_flag(user, AGENT_FLAG_INCOMING);

    free(user_glue->enter_callback_uri);
    callback_uri = helper_json_get_string(object,"callback_uri");
    if(callback_uri != NULL && callback_uri[0] != 0) {
      user_glue->enter_callback_uri = strdup(callback_uri);
    } else {
      user_glue->enter_callback_uri = NULL;
    }

    node = json_object_get_member(object,"throttles");
    if(node != NULL) {
      int len; unsigned char* buf;
      buf = helper_json_get_bin(node, &len);
      if(buf == NULL) {
	CAJ_ERROR("ERROR: agent PUT had bad throttle data");
	goto out_fail;
      }
      user_set_throttles_block(user, buf, len);
      free(buf);
    }

    s = helper_json_get_string(object, "position");
    if(s == NULL || sscanf(s, "<%f, %f, %f>",&start_pos.x,
			   &start_pos.y, &start_pos.z) != 3) {
      CAJ_ERROR("ERROR: agent PUT - bad/missing position\n");
      goto out_fail;
    }
    
    // FIXME - what about look_at? Is it not sent?
    user_set_start_pos(user, &start_pos, &start_pos);

    node = json_object_get_member(object,"texture_entry");
    if(node != NULL) {
      struct caj_string buf;
      buf.data = helper_json_get_bin(node, &buf.len);
      if(buf.data == NULL) {
	CAJ_ERROR("ERROR: agent PUT had bad texture_entry data\n");
	goto out_fail;
      }
      // semantics of this are funny
      user_set_texture_entry(user, &buf);
    }

    node = json_object_get_member(object,"visual_params");
    if(node != NULL) {
      struct caj_string buf;
      buf.data = helper_json_get_bin(node, &buf.len);
      if(buf.data == NULL) {
	CAJ_ERROR("ERROR: agent PUT had bad visual_params data\n");
	goto out_fail;
      }
      // semantics of this are funny
      user_set_visual_params(user, &buf);
    }

    // FIXME - need to handle serial

    node = json_object_get_member(object,"wearables");
    if(node != NULL) {
      set_wearables_from_json(user, node, grid);
    }

    {
      int always_run;
      if(helper_json_get_boolean(object,"always_run", &always_run) == 0) {
	if(always_run) user_set_flag(user, AGENT_FLAG_ALWAYS_RUN);
	else user_clear_flag(user, AGENT_FLAG_ALWAYS_RUN);
      }
    }

    // FIXME - TODO: pos, etc...
    
  } else {
    CAJ_WARN("WARNING: agent PUT with unknown type %s\n",msg_type);
    // but we return success anyway.
  }  

  // FIXME - are we actually returning the right thing?
 out:
  soup_message_set_status(msg,200); // FIXME - application/json?
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    is_ok?"true":"false", is_ok?4:5);  
  return;
 out_fail:
  soup_message_set_status(msg,400);
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    "false",5);  
}

#if 0
static void agent_DELETE_handler(SoupServer *server,
			      SoupMessage *msg,
			      uuid_t agent_id,
			      JsonParser *parser,
			      struct simulator_ctx* sim) {
  JsonNode * node = json_parser_get_root(parser);
  JsonObject *object; user_ctx *user;
  user_grid_glue *user_glue;
  GRID_PRIV_DEF(sim);

  user = user_find_ctx(sim, agent_id);
  if(user == NULL) {
    CAJ_WARN("WARNING: DELETE for unknown agent\n");
    soup_message_set_status(msg, 404);
    is_ok = 0; goto out;
  }

  user_glue = (user_grid_glue*)user_get_grid_priv(user);
  assert(user_glue != NULL);

 out:
  soup_message_set_status(msg,200); // FIXME - application/json?
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    is_ok?"true":"false", is_ok?4:5);  
  return;
 out_fail:
  soup_message_set_status(msg,400);
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    "false",5);  

}
#endif

static void agent_DELETE_release_handler(SoupServer *server,
					 SoupMessage *msg,
					 uuid_t agent_id,
					 JsonParser *parser,
					 struct simulator_ctx* sim) {
  GRID_PRIV_DEF(sim);
  JsonNode * node = json_parser_get_root(parser); // FIXME - ???
  JsonObject *object; user_ctx *user;
  user_grid_glue *user_glue;
  int is_ok = 1;

  user = user_find_ctx(sim, agent_id);
  if(user == NULL) {
    CAJ_WARN("WARNING: DELETE release for unknown agent\n");
    soup_message_set_status(msg, 404); // FIXME - wrong?
    is_ok = 0; return;
  }

  if(user_get_flags(user) & AGENT_FLAG_TELEPORT_COMPLETE) {
    CAJ_INFO("DEBUG: releasing teleported user\n");
    user_session_close(user, true); // needs the delay...
  } else {
    CAJ_WARN("WARNING: ignoring DELETE release for non-teleporting agent\n");
    is_ok = 0; goto out;
  }

 out: // FIXME - correct return?
  soup_message_set_status(msg,200); // FIXME - application/json?
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    is_ok?"true":"false", is_ok?4:5);  
  return;
 out_fail:
  soup_message_set_status(msg,400);
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    "false",5);  

}


void osglue_agent_rest_handler(SoupServer *server,
			       SoupMessage *msg,
			       const char *path,
			       GHashTable *query,
			       SoupClientContext *client,
			       gpointer user_data) {
  struct simgroup_ctx* sgrp = (struct simgroup_ctx*) user_data;
  GRID_PRIV_DEF_SGRP(sgrp);
  const char *reqtype = "???";
  uuid_t agent_id; uint64_t region_handle = 0;
  const char *s; char buf[40]; const char* cmd = NULL;
  JsonParser *parser = NULL;
  if(msg->method == SOUP_METHOD_POST)
    reqtype = "POST";
  else if(msg->method == SOUP_METHOD_GET)
    reqtype = "GET";
  else if(msg->method == SOUP_METHOD_PUT)
    reqtype = "PUT";
  else if(msg->method == SOUP_METHOD_DELETE)
    reqtype = "DELETE";
  CAJ_DEBUG("DEBUG: agent_rest_handler %s %s\n",
	 reqtype, path);
  if(msg->method != SOUP_METHOD_GET)
    CAJ_DEBUG("DEBUG: agent_rest_handler data ~%s~\n",
	      msg->request_body->data);

  assert(strncmp(path,"/agent/",7) == 0);
  path += 7;

  // FIXME - do authentication

  s = strchr(path, '/');
  if(s == NULL) {
    if(uuid_parse(path, agent_id)) {
      CAJ_ERROR("ERROR: agent_rest_handler bad agent_id\n");
      goto out_fail;
    }
  } else if(s - path != 36) {
    CAJ_ERROR("ERROR: agent_rest_handler bad agent_id length %i\n",
	      (int)(s - path));
      goto out_fail;    
  } else {
    memcpy(buf,path, 36); buf[36] = 0;
    if(uuid_parse(buf, agent_id)) {
      CAJ_ERROR("ERROR: agent_rest_handler bad agent_id (2)\n");
      goto out_fail;
    }
    path = s + 1;
    s = strchr(path, '/');
    if(path[0] != 0)
      region_handle = atoll(path);
    if(s != NULL && s[1] != 0) 
      cmd = s + 1;
  }

  // DEBUG
  uuid_unparse(agent_id,buf);
  CAJ_DEBUG("DEBUG: agent_rest_handler request split as %s %llu %s\n",
	    buf, (long long unsigned int)region_handle, 
	    cmd != NULL ? cmd : "(none)");

  if(msg->method != SOUP_METHOD_POST ||
     msg->method != SOUP_METHOD_PUT) {

    parser = json_parser_new();
    if(!json_parser_load_from_data(parser, msg->request_body->data,
				   msg->request_body->length, NULL)) {
      CAJ_ERROR("Error in agent_rest_handler: json parse failed\n");
      g_object_unref(parser); goto out_fail;
    }
  }

  if(msg->method == SOUP_METHOD_POST && region_handle == 0
     && cmd == NULL) {
    agent_POST_handler(server, msg, agent_id, parser, sgrp);
  } else if(msg->method == SOUP_METHOD_PUT && region_handle == 0
     && cmd == NULL) {
    agent_PUT_handler(server, msg, agent_id, parser, sgrp);
  } else if(msg->method == SOUP_METHOD_DELETE && cmd != NULL && 
	    region_handle != 0 &&
	    (strcmp(cmd,"release/") == 0 || strcmp(cmd,"release") == 0)) {
    simulator_ctx *sim = caj_local_sim_by_region_handle(sgrp, region_handle);
    if(sim == NULL) {
      CAJ_ERROR("ERROR: agent DELETE for unknown region\n"); goto out_fail;
    } else {
      agent_DELETE_release_handler(server, msg, agent_id, parser, sim);
    }
  } /* else if(msg->method == SOUP_METHOD_DELETE && region_handle == 0
     && cmd == NULL) {
    agent_DELETE_handler(server, msg, agent_id, sim);
    } */ else {
    CAJ_WARN("WARNING: agent_rest_handler unhandled request\n");
    goto out_fail;
  }
  // TODO

  // other code can g_object_ref if it wants to keep it
  if(parser != NULL)
    g_object_unref(parser); 
  return;
  
 out_fail:
  if(parser != NULL)
    g_object_unref(parser); 
  soup_message_set_status(msg,400);
}



// --------------------- OUTGOING AGENTS ------------------------------------

static void helper_json_add_uuid(JsonObject *obj, const char* key, uuid_t u) {
  JsonNode *node = json_node_new(JSON_NODE_VALUE);
  char buf[40];
  uuid_unparse(u, buf);
  json_node_set_string(node, buf);
  json_object_add_member(obj, key, node);
}

static void helper_json_add_string(JsonObject *obj, const char* key, 
				   const char* s) {
  JsonNode *node = json_node_new(JSON_NODE_VALUE);
  json_node_set_string(node, s);
  json_object_add_member(obj, key, node);
}

static void helper_json_add_bool(JsonObject *obj, const char* key, 
				   gboolean b) {
  JsonNode *node = json_node_new(JSON_NODE_VALUE);
  json_node_set_boolean(node, b);
  json_object_add_member(obj, key, node);
}

static void helper_json_add_double(JsonObject *obj, const char* key, 
				   double f) {
  JsonNode *node = json_node_new(JSON_NODE_VALUE);
  json_node_set_double(node, f);
  json_object_add_member(obj, key, node);
}

int helper_parse_json_resp(JsonParser *parser, const char* data, gsize len, const char **reason_out) {
  if(!json_parser_load_from_data(parser, data, len, NULL)) {
    printf("parse json resp: json parse failed\n");
    *reason_out = "[LOCAL] json parse error";
    return false;
  }

  JsonNode *node = json_parser_get_root(parser); // reused later
  JsonObject *object; int success;

  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_OBJECT) {
    printf("parse json resp: Root JSON node not object?!\n");
    *reason_out = "[LOCAL] bad json response";
    return false;
  }
  object = json_node_get_object(node);
  
  if(helper_json_get_boolean(object,"success",&success)) {
    printf("parse json resp: couldn't get success boolean!\n");
    *reason_out = "[LOCAL] bad json response";
    return false;    
  }
  
  *reason_out = helper_json_get_string(object,"reason");
  if(*reason_out == NULL) *reason_out = "[reason missing from response]";
  return success;
}



static void do_teleport_put_agent_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  os_teleport_desc *tp_priv = (os_teleport_desc*)user_data;
  GRID_PRIV_DEF(tp_priv->our_sim);
  sim_shutdown_release(tp_priv->our_sim);
  if(tp_priv->tp->ctx == NULL) {
    // FIXME - delete child agent!!
    osglue_teleport_failed(tp_priv,"cancelled");
  } else if(msg->status_code != 200) {
    CAJ_WARN("WARNING: Agent PUT request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    // FIXME - the OpenSim code seems to think this should send a reason...
    osglue_teleport_failed(tp_priv, "Error. Agent PUT request failed");
  } else {
    CAJ_DEBUG("DEBUG: Got agent PUT response: ~%s~", msg->response_body->data);
    if(strcasecmp(msg->response_body->data, "True") == 0) {
      // FIXME - this is where we should actually send the avatar across
      teleport_desc *tp = tp_priv->tp;
      int seed_len = strlen(tp_priv->sim_ip)+70;
      char *seed_cap = (char*)malloc(seed_len);
      snprintf(seed_cap, seed_len, "http://%s:%i/CAPS/%s0000/", 
	       tp_priv->sim_ip, tp_priv->sim_port, tp_priv->caps_path);
      tp->seed_cap = seed_cap;

      user_complete_teleport(tp);

      free(seed_cap); free(tp_priv->sim_ip); free(tp_priv->caps_path);
      delete tp_priv;
      //osglue_teleport_failed(tp_priv,"It's the hippos, honest");
    } else {
      osglue_teleport_failed(tp_priv,"Agent PUT request returned error");
    }
  }
}

static JsonNode* jsonise_blob(unsigned char* data, int len) {
  JsonArray *arr = json_array_new();
  JsonNode *node;

  for(int i = 0; i < len; i++) {
    node = json_node_new(JSON_NODE_VALUE);
    json_node_set_int(node, data[i]);
    json_array_add_element(arr, node);
  }

  node = json_node_new(JSON_NODE_ARRAY);
  json_node_take_array(node, arr);
  return node;
}

static JsonNode* jsonise_throttles(user_ctx* ctx) {
  unsigned char data[SL_NUM_THROTTLES*4];
  user_get_throttles_block(ctx, data, SL_NUM_THROTTLES*4);
  return jsonise_blob(data, SL_NUM_THROTTLES*4);
}

static JsonNode* jsonise_wearables(user_ctx* ctx) {
  JsonArray *arr = json_array_new();
  JsonNode *node;
  const wearable_desc* wearables = user_get_wearables(ctx);
  char buf[40];

  for(int i = 0; i < SL_NUM_WEARABLES; i++) {
    // item id, then asset id
    node = json_node_new(JSON_NODE_VALUE);
    uuid_unparse(wearables[i].item_id, buf);
    json_node_set_string(node, buf);
    json_array_add_element(arr, node);

    node = json_node_new(JSON_NODE_VALUE);
    uuid_unparse(wearables[i].asset_id, buf);
    json_node_set_string(node, buf);
    json_array_add_element(arr, node);
  }  

  node = json_node_new(JSON_NODE_ARRAY);
  json_node_take_array(node, arr);
  return node;  
}

static void do_teleport_put_agent(simulator_ctx* sim, teleport_desc *tp,
				  os_teleport_desc *tp_priv) {
  GRID_PRIV_DEF(sim);
  JsonGenerator* gen;
  JsonObject *obj = json_object_new();
  JsonNode *node; uuid_t u, agent_id; 
  char buf[40]; gchar *jbuf; gsize len;
  char uri[256]; // FIXME
  uint64_t our_region_handle = sim_get_region_handle(sim);
  const caj_string *pstr;

  //user_teleport_progress(tp,"Upgrading child agent to full agent");
  user_teleport_progress(tp,"sending_dest"); // FIXME - ????
  
  // Now, we upgrade the child agent to a fully-fledged agent

  // TODO (FIXME)!

  helper_json_add_string(obj, "message_type", "AgentData");
  // yes, *our* region handle!
  sprintf(buf, "%llu", (long long unsigned)our_region_handle); 
  helper_json_add_string(obj, "region_handle", buf);

  // OpenSim sets this to 0 itself, but doing that seems to break teleports
  // for us. I have a nasty feeling that's the *real* reason the viewer has
  // to connect as a child agent first, actually...
  //helper_json_add_string(obj, "circuit_code", "0"); // not used

  user_get_uuid(tp->ctx, agent_id);
  helper_json_add_uuid(obj, "agent_uuid", agent_id);

  uuid_clear(u); // also not used
  helper_json_add_uuid(obj, "session_uuid", u);

  // FIXME - actually fill these in
  snprintf(buf, 40, "<%f, %f, %f>", tp->pos.x, tp->pos.y,
	   tp->pos.z);
  helper_json_add_string(obj,"position",buf);
  helper_json_add_string(obj,"velocity","<0, 0, 0>");
  helper_json_add_string(obj,"size","<0, 0, 0>");

  // camera stuff - FIXME fill this in.
  helper_json_add_string(obj,"center","<126.4218, 125.6846, 39.70211>");
  helper_json_add_string(obj,"at_axis","<0.4857194, 0.8467544, -0.2169876>");
  helper_json_add_string(obj,"left_axis","<-0.8674213, 0.4975745, 0>");
  helper_json_add_string(obj,"up_axis","<0.1079675, 0.1882197, 0.9761744>");
  // "position":"<128, 128, 1.5>","velocity":"<0, 0, 0>","center":"<126.4218, 125.6846, 39.70211>","size":"<0, 0, 0>","at_axis":"<0.4857194, 0.8467544, -0.2169876>","left_axis":"<-0.8674213, 0.4975745, 0>","up_axis":"<0.1079675, 0.1882197, 0.9761744>",

  helper_json_add_bool(obj,"changed_grid",false);
  helper_json_add_double(obj,"far",user_get_draw_dist(tp->ctx));
  helper_json_add_double(obj,"aspect",0.0); // FIXME - ???
  json_object_add_member(obj,"throttles",jsonise_throttles(tp->ctx));
  //"changed_grid":false,"far":64.0,"aspect":0.0,"throttles":[0,0,150,70,0,0,170,70,0,0,136,69,0,0,136,69,0,0,95,71,0,0,95,71,0,0,220,70],"locomotion_state":"0","head_rotation":"<0, 0, 0.5012131, 0.8653239>","body_rotation":"<0, 0, 0.5012121, 0.8653245>","control_flags":"0","energy_level":0.0,"god_level":"0","always_run":false,"prey_agent":"00000000-0000-0000-0000-000000000000","agent_access":"0","active_group_id":"00000000-0000-0000-0000-000000000000",
  helper_json_add_string(obj,"locomotion_state","0"); // FIXME - ???
  helper_json_add_string(obj,"head_rotation","<0, 0, 0.5012131, 0.8653239>"); // FIXME
  helper_json_add_string(obj,"body_rotation","<0, 0, 0.5012121, 0.8653245>"); // FIXME
  helper_json_add_string(obj,"control_flags","0"); // FIXME - ???
  helper_json_add_double(obj,"energy_level",0.0); // not really used anymore?
  helper_json_add_string(obj,"god_level","0"); // FIXME - ???
  helper_json_add_bool(obj,"always_run",
		       (user_get_flags(tp->ctx) & AGENT_FLAG_ALWAYS_RUN) != 0);
  helper_json_add_string(obj,"prey_agent","00000000-0000-0000-0000-000000000000"); // FIXME
  helper_json_add_string(obj,"active_group_id","00000000-0000-0000-0000-000000000000"); // FIXME

  // texture_entry, visual_params, wearables
  pstr = user_get_texture_entry(tp->ctx);
  json_object_add_member(obj, "texture_entry",
			 jsonise_blob(pstr->data, pstr->len));
  pstr = user_get_visual_params(tp->ctx);
  json_object_add_member(obj, "visual_params",
			 jsonise_blob(pstr->data, pstr->len));

  // FIXME - get wearables to *actually* work right
  json_object_add_member(obj, "wearables",jsonise_wearables(tp->ctx));

  sprintf(buf,"%llu",(long long unsigned)tp->region_handle);
  helper_json_add_string(obj,"destination_handle",buf);

  // for newer OpenSim versions
  sprintf(buf,"%lu",(unsigned long)((tp->region_handle >> 32) & 0xffffffff));
  helper_json_add_string(obj,"destination_x",buf);
  sprintf(buf,"%lu",(unsigned long)(tp->region_handle & 0xffffffff));
  helper_json_add_string(obj,"destination_y",buf);
  

  //helper_json_add_string(obj,"start_pos","<128, 128, 1.5>"); // FIXME

  
  {
    char *my_ip_addr = sim_get_ip_addr(sim);
    char callback_uri[256]; // FIXME - non-fixed size buffer!
    uuid_unparse(agent_id,buf);
    snprintf(callback_uri, 256, "http://%s:%i/agent/%s/%llu/release/",
	     my_ip_addr, (int)sim_get_http_port(sim), buf,
	     (long long unsigned)our_region_handle);
    CAJ_DEBUG("DEBUG: sending \"%s\" as callback URI\n", callback_uri);
    helper_json_add_string(obj,"callback_uri",callback_uri);
  }

  gen = json_generator_new();
  node = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(node, obj);
  json_generator_set_root(gen, node);
  jbuf = json_generator_to_data(gen, &len);
  json_node_free(node);
  g_object_unref(gen);

  CAJ_DEBUG("DEBUG: sending agent PUT ~%s~\n", jbuf);

  uuid_unparse(agent_id,buf);
  snprintf(uri, 256, "http://%s:%i/agent/%s/",tp_priv->sim_ip,
	   tp_priv->http_port, buf);
  SoupMessage *msg = soup_message_new ("PUT", uri);
  soup_message_set_request (msg, "application/json", // FIXME - check mime type
			    SOUP_MEMORY_TAKE,  jbuf, 
			    len);
  sim_shutdown_hold(sim);
  sim_queue_soup_message(sim, msg,
			 do_teleport_put_agent_resp, tp_priv);
  
}

static void do_teleport_send_agent_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  os_teleport_desc *tp_priv = (os_teleport_desc*)user_data;
  GRID_PRIV_DEF(tp_priv->our_sim);
  sim_shutdown_release(tp_priv->our_sim);
  if(tp_priv->tp->ctx == NULL) {
    // FIXME - delete child agent!!
    osglue_teleport_failed(tp_priv,"cancelled");
  } else if(msg->status_code != 200) {
    CAJ_WARN("WARNING: Agent POST request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    // FIXME - the OpenSim code seems to think this should send a reason...
    osglue_teleport_failed(tp_priv, "Error. Destination not accepting teleports?");
  } else {
    CAJ_DEBUG("DEBUG: Got agent POST response: ~%s~\n", msg->response_body->data);
    JsonParser *parser = json_parser_new();
    const char *reason = "[NO REASON - FIXME!]";
    int success = helper_parse_json_resp(parser, msg->response_body->data,
					 msg->response_body->length, &reason);
    if(success) {
      // FIXME - code duplication with seed cap
      int seed_len = strlen(tp_priv->sim_ip)+70;
      char *seed_cap = (char*)malloc(seed_len);
      snprintf(seed_cap, seed_len, "http://%s:%i/CAPS/%s0000/", 
	       tp_priv->sim_ip, tp_priv->sim_port, tp_priv->caps_path);

      user_teleport_add_temp_child(tp_priv->tp->ctx,tp_priv->tp->region_handle,
				   tp_priv->tp->sim_ip, tp_priv->tp->sim_port,
				   seed_cap);
      free(seed_cap);
      //osglue_teleport_failed(tp_priv,"It's the hippos, honest");
      do_teleport_put_agent(tp_priv->our_sim, tp_priv->tp, tp_priv);
    } else {
      char buf[256]; // FIXME - don't use fixed-size buffer;
      // FIXME - change message to be in line with OpenSim
      snprintf(buf, 256, "Couldn't create child agent: %s",reason);
      osglue_teleport_failed(tp_priv, buf);
    }
    g_object_unref(parser);
  }
}

void osglue_teleport_send_agent(simulator_ctx* sim, teleport_desc *tp,
				os_teleport_desc *tp_priv) {
  JsonGenerator* gen;
  JsonObject *obj = json_object_new();
  JsonNode *node; uuid_t u, agent_id; 
  char buf[40]; gchar *jbuf; gsize len;
  char uri[256]; // FIXME

  //user_teleport_progress(tp,"Adding child agent to destination");
  user_teleport_progress(tp,"sending_dest"); // FIXME - is this right?
  
  // First, we add a new child agent to the destination...

  // FIXME - factor out, reuse existing child agents, etc
  user_get_uuid(tp->ctx, agent_id);
  helper_json_add_uuid(obj, "agent_id", agent_id);
  user_get_session_id(tp->ctx,u);
  helper_json_add_uuid(obj, "session_id", u);
  user_get_secure_session_id(tp->ctx,u);
  helper_json_add_uuid(obj, "secure_session_id", u);
  sprintf(buf,"%lu",(unsigned long)user_get_circuit_code(tp->ctx));
  helper_json_add_string(obj, "circuit_code", buf);

  helper_json_add_string(obj,"first_name",user_get_first_name(tp->ctx));
  helper_json_add_string(obj,"last_name",user_get_last_name(tp->ctx));

  helper_json_add_bool(obj,"child",true);
  sprintf(buf,"%llu",(long long unsigned)tp->region_handle);
  helper_json_add_string(obj,"destination_handle",buf);

  // for newer OpenSim versions
  sprintf(buf,"%lu",(unsigned long)((tp->region_handle >> 32) & 0xffffffff));
  helper_json_add_string(obj,"destination_x",buf);
  sprintf(buf,"%lu",(unsigned long)(tp->region_handle & 0xffffffff));
  helper_json_add_string(obj,"destination_y",buf);

  helper_json_add_string(obj,"start_pos","<128, 128, 1.5>"); // FIXME

  // FIXME - do we really need to reuse existing caps?
  tp_priv->caps_path = make_child_cap(tp->ctx, tp->region_handle);
  helper_json_add_string(obj,"caps_path",tp_priv->caps_path);

  // FIXME - send existing child caps to destination!
  
  // FIXME - fill these out properly? (were zero in the dump I saw)
  uuid_clear(u);
  helper_json_add_uuid(obj,"inventory_folder",u);
  helper_json_add_uuid(obj,"base_folder",u);

  {
    // FIXME - should actually fill this out, I guess
    JsonArray *arr = json_array_new();
    node = json_node_new(JSON_NODE_ARRAY);
    json_node_take_array(node, arr);
    json_object_add_member(obj,"children_seeds",node);
  }

  gen = json_generator_new();
  node = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(node, obj);
  json_generator_set_root(gen, node);
  jbuf = json_generator_to_data(gen, &len);
  json_node_free(node);
  g_object_unref(gen);

  uuid_unparse(agent_id,buf);
  snprintf(uri, 256, "http://%s:%i/agent/%s/",tp_priv->sim_ip,
	   tp_priv->http_port, buf);
  SoupMessage *msg = soup_message_new ("POST", uri);
  soup_message_set_request (msg, "application/json", // FIXME - check mime type
			    SOUP_MEMORY_TAKE,  jbuf, 
			    len);
  sim_shutdown_hold(sim);
  sim_queue_soup_message(sim, msg,
			 do_teleport_send_agent_resp, tp_priv);
}

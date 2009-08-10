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
#include <libsoup/soup.h>
#include "sl_types.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include "opensim_grid_glue.h"
#include <cassert>

static void got_grid_login_response(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)user_data;
  GRID_PRIV_DEF(sim);
  GHashTable *hash = NULL;
  char *s; uuid_t u;
  if(msg->status_code != 200) {
    printf("Grid login failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    exit(1);
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    printf("Grid login failed: couldn't parse response\n");
    exit(1);    
  }

  if(soup_value_hash_lookup(hash,"restricted",G_TYPE_STRING,
			    &s)) {
    // grid actually report it as an error, not as this
    printf("Error - grid not accepting registrations: %s\n",s);
    exit(1);
  } else if(soup_value_hash_lookup(hash,"error",G_TYPE_STRING,
			    &s)) {
    printf("Grid refused registration: %s\n",s);
    exit(1);
  }

  if(!soup_value_hash_lookup(hash,"authkey",G_TYPE_STRING,
			     &s)) goto bad_resp;
  if(uuid_parse(s, u) || uuid_compare(u,grid->region_secret) != 0) {
    printf("Unexpected authkey value!\n");
    goto bad_resp;
  }
  if(!soup_value_hash_lookup(hash,"regionname",G_TYPE_STRING,
			     &s)) goto bad_resp;
  if(strcmp(s,sim_get_name(sim)) != 0) {
    printf("DEBUG: simname mismatch; we're \"%s\", server says \"%s\"\n",
	   sim_get_name(sim),s);
  }
  // FIXME - use user_recvkey, asset_recvkey, user_sendkey, asset_sendkey
  // FIXME - check region_locx/y, sim_ip, sim_port
  if(!soup_value_hash_lookup(hash,"user_url",G_TYPE_STRING,
			     &s)) goto bad_resp;
  grid->userserver = g_strdup(s);
  if(!soup_value_hash_lookup(hash,"asset_url",G_TYPE_STRING,
			     &s)) goto bad_resp;
  grid->assetserver = g_strdup(s);

  //printf("DEBUG: login response ~%s~\n",msg->response_body->data);
  printf("Grid login complete\n");
  g_hash_table_destroy(hash);
  

  return;
 bad_resp:
  printf("Bad grid login response\n");
  printf("DEBUG {{%s}}\n", msg->response_body->data);
  g_hash_table_destroy(hash);
  exit(1);
  
  // TODO
  // Looks like we get the user and asset server URLs back,
  // plus a bunch of info we sent ourselves.
}

static void do_grid_login(struct simulator_ctx* sim) {
  char buf[40];
  uuid_t zero_uuid, u;
  uuid_clear(zero_uuid);
  GHashTable *hash;
  GError *error = NULL;
  SoupMessage *msg;
  char *ip_addr = sim_get_ip_addr(sim);
  GRID_PRIV_DEF(sim);

  printf("Logging into grid...\n");

  // FIXME - do I need to free this?
  hash = soup_value_hash_new();
  soup_value_hash_insert(hash,"authkey",G_TYPE_STRING,grid->grid_sendkey);
  soup_value_hash_insert(hash,"recvkey",G_TYPE_STRING,grid->grid_recvkey);
  soup_value_hash_insert(hash,"major_interface_version",G_TYPE_STRING,"5");
  soup_value_hash_insert(hash,"maturity",G_TYPE_STRING,"1");
  uuid_unparse(zero_uuid, buf);
  soup_value_hash_insert(hash,"map-image-id",G_TYPE_STRING,buf);
  sim_get_owner_uuid(sim,u);
  uuid_unparse(u, buf);
  soup_value_hash_insert(hash,"master_avatar_uuid",G_TYPE_STRING,buf);
  sprintf(buf,"%i",(int)sim_get_http_port(sim));
  soup_value_hash_insert(hash,"http_port",G_TYPE_STRING,buf);
  uuid_unparse(grid->region_secret, buf);
  soup_value_hash_insert(hash,"region_secret",G_TYPE_STRING,buf);
  soup_value_hash_insert(hash,"sim_name",G_TYPE_STRING,sim_get_name(sim));
  sim_get_region_uuid(sim, u);
  uuid_unparse(u, buf);
  soup_value_hash_insert(hash,"UUID",G_TYPE_STRING,buf);
  sprintf(buf,"http://%s:%i",ip_addr,
	  (int)sim_get_http_port(sim)); // FIXME
  soup_value_hash_insert(hash,"server_uri",G_TYPE_STRING,buf);
  sprintf(buf,"%i",(int)sim_get_region_x(sim));
  soup_value_hash_insert(hash,"region_locx",G_TYPE_STRING,buf);
  sprintf(buf,"%i",(int)sim_get_region_y(sim));
  soup_value_hash_insert(hash,"region_locy",G_TYPE_STRING,buf);
  soup_value_hash_insert(hash,"sim_ip",G_TYPE_STRING,ip_addr); // FIXME
  soup_value_hash_insert(hash,"remoting_port",G_TYPE_STRING,"8895"); // ??? FIXME
  sprintf(buf,"%i",(int)sim_get_udp_port(sim));
  soup_value_hash_insert(hash,"sim_port",G_TYPE_STRING,buf);
  uuid_unparse(zero_uuid, buf); // FIXME - ??? what is originUUID??
  soup_value_hash_insert(hash,"originUUID",G_TYPE_STRING,buf);
  

  msg = soup_xmlrpc_request_new(grid->gridserver, "simulator_login",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create xmlrpc login request\n");
    exit(1);
  }

  // FIXME - why SOUP_MESSAGE(foo)?
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			 got_grid_login_response, sim);
  
  // TODO
}

// -------- Login-related glue --------------

struct expect_user_state {
  SoupServer *server;
  SoupMessage *msg;
  GValueArray *params;
  GHashTable *args;
  struct simulator_ctx* sim;
};

static void xmlrpc_expect_user_2(void* priv, int is_ok) {
  struct expect_user_state *state = (struct expect_user_state*)priv;
  GHashTable *args = state->args;
  GHashTable *hash;
  struct sim_new_user uinfo;
  //char *first_name, *last_name;
  char *caps_path, *s;
  //uuid_t session_id, agent_id, secure_session_id;
  char seed_cap[50]; int success = 0;
  soup_server_unpause_message(state->server,state->msg);

  if(!is_ok) goto return_fail;

  if(!soup_value_hash_lookup(args,"firstname",G_TYPE_STRING,
			     &uinfo.first_name)) goto bad_args;
  if(!soup_value_hash_lookup(args,"lastname",G_TYPE_STRING,
			     &uinfo.last_name)) goto bad_args;
  if(!soup_value_hash_lookup(args,"caps_path",G_TYPE_STRING,
			     &caps_path)) goto bad_args;
  if(strlen(caps_path) != 36) goto bad_args;
  if(!soup_value_hash_lookup(args,"agent_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  if(uuid_parse(s, uinfo.user_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"session_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  if(uuid_parse(s, uinfo.session_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"secure_session_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  if(uuid_parse(s, uinfo.secure_session_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"circuit_code",G_TYPE_INT,
			     &uinfo.circuit_code)) goto bad_args;
  // FIXME - use startpos_x/y/z, secure_session_id
  // FIXME - do something with appearance data (struct)
  
  // WTF? Why such an odd path?
  snprintf(seed_cap,50,"%s0000/",caps_path);
  uinfo.seed_cap = seed_cap;
  uinfo.is_child = 0;
  sim_prepare_new_user(state->sim, &uinfo);

 
  success = 1;
 return_fail:
  hash = soup_value_hash_new();
  soup_value_hash_insert(hash, "success", G_TYPE_STRING, 
			 success?"TRUE":"FALSE");
  soup_xmlrpc_set_response(state->msg, G_TYPE_HASH_TABLE, hash);
  g_hash_table_destroy(hash);
  g_value_array_free(state->params);
  delete state;
  return;

 bad_args:
  g_value_array_free(state->params);
  soup_xmlrpc_set_fault(state->msg, 
			SOUP_XMLRPC_FAULT_SERVER_ERROR_INVALID_METHOD_PARAMETERS,
			"Bad arguments");  
  delete state;
}

typedef void(*validate_session_cb)(void* state, int is_ok);

struct validate_session_state {
  validate_session_cb callback;
  void *priv;
  simulator_ctx *sim;
};

static void got_validate_session_resp(SoupSession *session, SoupMessage *msg, 
				   gpointer user_data) {
  GHashTable *hash; char *s;
  int is_ok = 0;
  validate_session_state* vs = (validate_session_state*) user_data;
  printf("DEBUG: got check_auth_session response\n");
  sim_shutdown_release(vs->sim);
  if(soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    if(soup_value_hash_lookup(hash, "auth_session",
			      G_TYPE_STRING, &s)) {
      is_ok = (strcasecmp(s,"TRUE") == 0);
      printf("DEBUG: check_auth_session resp %s (%s)\n",
	     is_ok?"TRUE":"FALSE",s);
    } else printf("DEBUG: couldn't extract value from check_auth_session response\n");
    g_hash_table_destroy(hash);
  } else printf("DEBUG: couldn't extract check_auth_session response\n");
  vs->callback(vs->priv, is_ok);
  delete vs;
}

void validate_session(struct simulator_ctx* sim, const char* agent_id,
		      const char *session_id, grid_glue_ctx* grid,
		      validate_session_cb callback, void *priv)  {
  GHashTable *hash;
  SoupMessage *val_msg;
  
  printf("Validating session for %s...\n", agent_id);
  
  hash = soup_value_hash_new();
  soup_value_hash_insert(hash,"session_id",G_TYPE_STRING,session_id);
  soup_value_hash_insert(hash,"avatar_uuid",G_TYPE_STRING,agent_id);
  
  val_msg = soup_xmlrpc_request_new(grid->userserver, "check_auth_session",
				    G_TYPE_HASH_TABLE, hash,
				    G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!val_msg) {
    fprintf(stderr, "Could not create check_auth_session request\n");
    exit(1);
  }

  validate_session_state *vs_state = new validate_session_state();
  vs_state->callback = callback;
  vs_state->priv = priv;
  vs_state->sim = sim;

  sim_shutdown_hold(sim);
  // FIXME - why SOUP_MESSAGE(foo)?
  sim_queue_soup_message(sim, SOUP_MESSAGE(val_msg),
			 got_validate_session_resp, vs_state);
  				   
}


static void xmlrpc_expect_user(SoupServer *server,
			       SoupMessage *msg,
			       GValueArray *params,
			       struct simulator_ctx* sim) {
  GRID_PRIV_DEF(sim);
  GHashTable *args = NULL;
  char *agent_id, *session_id, *s;
  uint64_t region_handle;
  GHashTable *hash;
  if(params->n_values != 1 || 
     !soup_value_array_get_nth (params, 0, G_TYPE_HASH_TABLE, &args)) 
    goto bad_args;
  printf("DEBUG: Got an expect_user call\n");
  if(grid->userserver == NULL) goto return_fail; // not ready yet
  if(!soup_value_hash_lookup(args,"agent_id",G_TYPE_STRING,
			     &agent_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"session_id",G_TYPE_STRING,
			     &session_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"regionhandle",G_TYPE_STRING,
			     &s)) goto bad_args;
  region_handle = atoll(s);
  if(region_handle == 0) goto bad_args;
  if(region_handle != sim_get_region_handle(sim)) {
    printf("Got expect_user for wrong region\n");
    goto return_fail;
  }

  soup_server_pause_message(server,msg);

  {
    expect_user_state *state = new expect_user_state();
    state->server = server;
    state->msg = msg;
    state->params = params;
    state->args = args;
    state->sim = sim;
    validate_session(sim, agent_id, session_id, grid,
		     xmlrpc_expect_user_2, state);
  }

  //soup_message_set_status(msg,500);  
  return;
 
 return_fail:
  hash = soup_value_hash_new();
  soup_value_hash_insert(hash, "success", G_TYPE_STRING, "FALSE");
  soup_xmlrpc_set_response(msg, G_TYPE_HASH_TABLE, hash);
  g_hash_table_destroy(hash);
  g_value_array_free(params);
  return;

 bad_args:
  soup_xmlrpc_set_fault(msg, SOUP_XMLRPC_FAULT_SERVER_ERROR_INVALID_METHOD_PARAMETERS,
			"Bad arguments");  
  g_value_array_free(params);
  return;
}


static void xmlrpc_logoff_user(SoupServer *server,
			       SoupMessage *msg,
			       GValueArray *params,
			       struct simulator_ctx* sim) {
  GRID_PRIV_DEF(sim);
  GHashTable *args = NULL;
  int secret_ok = 1;
  uuid_t agent_id, region_secret;
  char *s;
  uint64_t region_handle;
  GHashTable *hash;
  GError *error = NULL;
  printf("DEBUG: Got a logoff_user call\n");
  if(params->n_values != 1 || 
     !soup_value_array_get_nth (params, 0, G_TYPE_HASH_TABLE, &args)) 
    goto bad_args;

  if(!soup_value_hash_lookup(args,"regionhandle",G_TYPE_STRING,
			     &s)) goto bad_args;
  region_handle = atoll(s);
  if(region_handle == 0) goto bad_args;
  if(region_handle != sim_get_region_handle(sim)) {
    printf("Got logoff_user for wrong region\n");
    goto return_null;
  }
  if(!soup_value_hash_lookup(args,"agent_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  if(uuid_parse(s, agent_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"region_secret",G_TYPE_STRING,
			     &s)) goto bad_args;
  if(uuid_parse(s, region_secret)) goto bad_args;

  if(uuid_compare(region_secret, grid->region_secret) != 0)
    secret_ok = 0;

  printf("DEBUG: logoff_user call is valid, walking the user tree\n");

  user_logoff_user_osglue(sim, agent_id, 
			  secret_ok?NULL:region_secret);

 return_null: // FIXME - should actually return nothing
  hash = soup_value_hash_new();
  soup_value_hash_insert(hash, "foo", G_TYPE_STRING, "bar");
  soup_xmlrpc_set_response(msg, G_TYPE_HASH_TABLE, hash);
  g_hash_table_destroy(hash);
  g_value_array_free(params);
  return;

 bad_args:
  soup_xmlrpc_set_fault(msg, SOUP_XMLRPC_FAULT_SERVER_ERROR_INVALID_METHOD_PARAMETERS,
			"Bad arguments");  
  g_value_array_free(params);
  return;
}

// ---- End login-related glue -----

// FIXME - move this to core?
static void xmlrpc_handler (SoupServer *server,
				SoupMessage *msg,
				const char *path,
				GHashTable *query,
				SoupClientContext *client,
				gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*) user_data;
  const char *dat = msg->request_body->data;
  char *method_name;
  GValueArray *params;

  if(strcmp(path,"/") != 0) {
    printf("DEBUG: request for unhandled path %s\n",
	   path);
    if (msg->method == SOUP_METHOD_POST) {
      printf("DEBUG: POST data is {{%s}}\n",
	     msg->request_body->data);
    }
    soup_message_set_status(msg,404);
    return;
  }

  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  if(!soup_xmlrpc_parse_method_call(msg->request_body->data,
				    msg->request_body->length,
				    &method_name, &params)) {
    printf("Couldn't parse XMLRPC method call\n");
    printf("DEBUG: ~%s~\n", dat);
    soup_message_set_status(msg,500);
    return;
  }

  // FIXME FIXME FIXME - leaks memory, but hard to fix due to oddball lifetimes
  if(strcmp(method_name, "logoff_user") == 0) {
    xmlrpc_logoff_user(server, msg, params, sim);
    
  } else if(strcmp(method_name, "expect_user") == 0) {
    xmlrpc_expect_user(server, msg, params, sim);
  } else {
    printf("DEBUG: unknown xmlrpc method %s called\n", method_name);
    g_value_array_free(params);
    soup_xmlrpc_set_fault(msg, SOUP_XMLRPC_FAULT_SERVER_ERROR_REQUESTED_METHOD_NOT_FOUND,
			  "Method %s not found", method_name);
  }
  g_free(method_name);
}

static void got_user_logoff_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)user_data;
  // GRID_PRIV_DEF(sim);
  GHashTable *hash = NULL;
  sim_shutdown_release(sim);
  if(msg->status_code != 200) {
    printf("User logoff failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    return;
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    printf("User logoff failed: couldn't parse response\n");
    return;    
  }

  printf("User logoff completed\n");
  g_hash_table_destroy(hash);
}


static void user_logoff(struct simulator_ctx* sim,
			const uuid_t user_id, const sl_vector3 *pos,
			const sl_vector3 *look_at) {
  char buf[40];
  uuid_t u;
  GHashTable *hash;
  GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF(sim);

  // FIXME - do I need to free this?
  hash = soup_value_hash_new();
  uuid_unparse(user_id, buf);
  soup_value_hash_insert(hash,"avatar_uuid",G_TYPE_STRING,buf);
  sim_get_region_uuid(sim, u);
  uuid_unparse(u, buf);
  soup_value_hash_insert(hash,"region_uuid",G_TYPE_STRING,buf);
  sprintf(buf,"%llu",sim_get_region_handle(sim));
  soup_value_hash_insert(hash,"region_handle",G_TYPE_STRING,buf);
  sprintf(buf,"%f",(double)pos->x);
  soup_value_hash_insert(hash,"region_pos_x",G_TYPE_STRING,buf);
  sprintf(buf,"%f",(double)pos->y);
  soup_value_hash_insert(hash,"region_pos_y",G_TYPE_STRING,buf);
  sprintf(buf,"%f",(double)pos->z);
  soup_value_hash_insert(hash,"region_pos_z",G_TYPE_STRING,buf);
  sprintf(buf,"%f",(double)pos->x);
  soup_value_hash_insert(hash,"lookat_x",G_TYPE_STRING,buf);
  sprintf(buf,"%f",(double)pos->y);
  soup_value_hash_insert(hash,"lookat_y",G_TYPE_STRING,buf);
  sprintf(buf,"%f",(double)pos->z);
  soup_value_hash_insert(hash,"lookat_z",G_TYPE_STRING,buf);
 

  msg = soup_xmlrpc_request_new(grid->userserver, "logout_of_simulator",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create xmlrpc login request\n");
    return;
  }

  // FIXME - why SOUP_MESSAGE(foo)?
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			 got_user_logoff_resp, sim);
  sim_shutdown_hold(sim);
}

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

static int helper_json_to_boolean(JsonObject *obj, const char* key, int *val) {
  const char* str;
  JsonNode *node = json_object_get_member(obj, key);
  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_VALUE)
    return -1;
  *val = json_node_get_boolean(node);
  return 0;
}

static int helper_json_to_uuid(JsonObject *obj, const char* key, uuid_t uuid) {
  const char* str;
  JsonNode *node = json_object_get_member(obj, key);
  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_VALUE)
    return -1;
  str = json_node_get_string(node);
  if(str == NULL) return -1;
  return uuid_parse(str, uuid);
}

static const char* helper_json_to_string(JsonObject *obj, const char* key) {
  JsonNode *node = json_object_get_member(obj, key);
  if(node == NULL || JSON_NODE_TYPE(node) != JSON_NODE_VALUE)
    return NULL;
  return json_node_get_string(node);
}

struct agent_POST_state {
  SoupServer *server;
  SoupMessage *msg;
  JsonParser *parser;
  struct simulator_ctx* sim;
};

static void agent_POST_stage2(void *priv, int is_ok) {
  int is_child = 0;
  char seed_cap[50]; const char *caps_path, *s;
  agent_POST_state* st = (agent_POST_state*)priv;
  JsonObject *object = json_node_get_object(json_parser_get_root(st->parser));
  struct sim_new_user uinfo;
 
  soup_server_unpause_message(st->server,st->msg);
  if(helper_json_to_uuid(object, "agent_id", uinfo.user_id)) {
    printf("DEBUG agent POST: couldn't get agent_id\n");
    is_ok = 0; goto out;
  }
  if(helper_json_to_uuid(object, "session_id", uinfo.session_id)) {
    printf("DEBUG agent POST: couldn't get session_id\n");
    is_ok = 0; goto out;
  }
  if(helper_json_to_uuid(object, "secure_session_id", uinfo.secure_session_id)) {
    printf("DEBUG agent POST: couldn't get secure_session_id\n");
    is_ok = 0; goto out;
  }
  // HACK
  uinfo.first_name = (char*)helper_json_to_string(object, "first_name");
  uinfo.last_name = (char*)helper_json_to_string(object, "last_name");
  if(uinfo.first_name == NULL || uinfo.last_name == NULL) {
    printf("DEBUG agent POST: couldn't get user name\n");
    is_ok = 0; goto out;
  }
  caps_path = (char*)helper_json_to_string(object, "caps_path");
  s = (char*)helper_json_to_string(object, "circuit_code");
  if(caps_path == NULL || s == NULL) {
    printf("DEBUG agent POST: caps path or circuit_code missing\n");
    is_ok = 0; goto out;
  }
  uinfo.circuit_code = atol(s);
  if(helper_json_to_boolean(object, "child", &is_child)) {
    printf("DEBUG agent POST: \"child\" attribute missing\n");
    is_ok = 0; goto out;    
  }
  
  // WTF? Why such an odd path?
  snprintf(seed_cap,50,"%s0000/",caps_path);
  uinfo.seed_cap = seed_cap;
  uinfo.is_child = 1;
  sim_prepare_new_user(st->sim, &uinfo);

 out:
  g_object_unref(st->parser); delete st;
  soup_message_set_status(st->msg,200); // FIXME - application/json?
  soup_message_set_response(st->msg,"text/plain",SOUP_MEMORY_STATIC,
			    is_ok?"true":"false", is_ok?4:5); 
}

static void agent_POST_handler(SoupServer *server,
			       SoupMessage *msg,
			       uuid_t agent_id,
			       JsonParser *parser,
			       struct simulator_ctx* sim) {
  const char *agent_id_st, *session_id_st;
  JsonObject *object; uuid_t u;
  JsonNode * node = json_parser_get_root(parser);
  GRID_PRIV_DEF(sim);

  if(JSON_NODE_TYPE(node) != JSON_NODE_OBJECT) {
    printf("Root JSON node not object?!\n");
    goto out_fail;
  }
  object = json_node_get_object(node);
  agent_id_st = helper_json_to_string(object, "agent_id");
  session_id_st = helper_json_to_string(object,"session_id");
  if(agent_id_st == NULL || session_id_st == NULL) {
    printf("Missing agent or session id from agent REST POST\n");
    goto out_fail;
  }
  if(uuid_parse(agent_id_st,u) || uuid_compare(u,agent_id) != 0) {
    printf("Bad/mismatched agent id in agent REST POST\n");
    goto out_fail;
  }
  
  soup_server_pause_message(server,msg);

  {
    agent_POST_state *state = new agent_POST_state();
    state->server = server;
    state->msg = msg;
    g_object_ref(parser);  
    state->parser = parser;
    state->sim = sim;
    validate_session(sim, agent_id_st, session_id_st, grid,
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
			      struct simulator_ctx* sim) {
  JsonNode * node = json_parser_get_root(parser);
  GRID_PRIV_DEF(sim);
  int is_ok = 1;

  if(JSON_NODE_TYPE(node) != JSON_NODE_OBJECT) {
    printf("Root JSON node not object?!\n");
    goto out_fail;
  }

  // FIXME - need to actually update the agent

  soup_message_set_status(msg,200); // FIXME - application/json?
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    is_ok?"true":"false", is_ok?4:5);  
  return;
 out_fail:
  soup_message_set_status(msg,400);
  soup_message_set_response(msg,"text/plain",SOUP_MEMORY_STATIC,
			    "false",5);  
}


static void agent_rest_handler(SoupServer *server,
			       SoupMessage *msg,
			       const char *path,
			       GHashTable *query,
			       SoupClientContext *client,
			       gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*) user_data;
  const char *reqtype = "???";
  uuid_t agent_id; uint64_t region_handle = 0;
  char *s; char buf[40]; char* cmd = NULL;
  JsonParser *parser;
  if(msg->method == SOUP_METHOD_POST)
    reqtype = "POST";
  else if(msg->method == SOUP_METHOD_GET)
    reqtype = "GET";
  else if(msg->method == SOUP_METHOD_PUT)
    reqtype = "PUT";
  printf("DEBUG: agent_rest_handler %s %s\n",
	 reqtype, path);
  if(msg->method != SOUP_METHOD_GET)
    printf("DEBUG: agent_rest_handler data ~%s~\n",
	   msg->request_body->data);

  assert(strncmp(path,"/agent/",7) == 0);
  path += 7;

  // FIXME - do authentication

  s = strchr(path, '/');
  if(s == NULL) {
    if(uuid_parse(path, agent_id)) {
      printf("DEBUG: agent_rest_handler bad agent_id\n");
      goto out_fail;
    }
  } else if(s - path != 36) {
    printf("DEBUG: agent_rest_handler bad agent_id length %i\n",
	   (int)(s - path));
      goto out_fail;    
  } else {
    memcpy(buf,path, 36); buf[36] = 0;
    if(uuid_parse(buf, agent_id)) {
      printf("DEBUG: agent_rest_handler bad agent_id (2)\n");
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
  printf("DEBUG: agent_rest_handler request split as %s %llu %s\n",
	 buf, (long long unsigned int)region_handle, 
	 cmd != NULL ? cmd : "(none)");

  if(msg->method != SOUP_METHOD_POST &&
     msg->method != SOUP_METHOD_PUT) goto out_fail;

  parser = json_parser_new();
  if(!json_parser_load_from_data(parser, msg->request_body->data,
				 msg->request_body->length, NULL)) {
    printf("Error in agent_rest_handler: json parse failed\n");
    g_object_unref(parser); goto out_fail;
  }

  if(msg->method == SOUP_METHOD_POST && region_handle == 0
     && cmd == NULL) {
    agent_POST_handler(server, msg, agent_id, parser, sim);
  } else if(msg->method == SOUP_METHOD_PUT && region_handle == 0
     && cmd == NULL) {
    agent_PUT_handler(server, msg, agent_id, parser, sim);
  } else {
    printf("DEBUG: agent_rest_handler unhandled request");
    goto out_fail;
  }
  // TODO

  // other code can g_object_ref if it wants to keep it
  g_object_unref(parser); 
  return;
  
 out_fail:
  soup_message_set_status(msg,400);
}

static void user_created(struct simulator_ctx* sim,
			 struct user_ctx* user,
			 void **user_priv) {
  // GRID_PRIV_DEF(sim);
  user_grid_glue *user_glue = new user_grid_glue();
  user_glue->ctx = user;
  user_glue->refcnt = 1;
  
  *user_priv = user_glue;
}

static void user_deleted(struct simulator_ctx* sim,
			 struct user_ctx* user,
			 void *user_priv) {
  USER_PRIV_DEF(user_priv);
  user_glue->ctx = NULL;
  user_grid_glue_deref(user_glue);
}

void user_grid_glue_ref(user_grid_glue *user_glue) {
  user_glue->refcnt++;
}

void user_grid_glue_deref(user_grid_glue *user_glue) {
  user_glue->refcnt--;
  if(user_glue->refcnt == 0) {
    assert(user_glue->ctx == NULL);
    delete user_glue;
  }
}

static void cleanup(struct simulator_ctx* sim) {
  GRID_PRIV_DEF(sim);
  g_free(grid->userserver);
  g_free(grid->gridserver);
  g_free(grid->assetserver);
  g_free(grid->grid_recvkey);
  g_free(grid->grid_sendkey);
  delete grid;
}

int cajeput_grid_glue_init(int api_version, struct simulator_ctx *sim, 
			   void **priv, struct cajeput_grid_hooks *hooks) {
  if(api_version != CAJEPUT_API_VERSION) 
    return false;

  struct grid_glue_ctx *grid = new grid_glue_ctx;
  *priv = grid;
  uuid_generate_random(grid->region_secret);

  hooks->do_grid_login = do_grid_login;
  hooks->user_created = user_created;
  hooks->user_logoff = user_logoff;
  hooks->user_deleted = user_deleted;
  hooks->fetch_user_inventory = fetch_user_inventory;
  hooks->cleanup = cleanup;

  grid->gridserver = sim_config_get_value(sim,"grid","gridserver");
  grid->inventoryserver = sim_config_get_value(sim,"grid","inventory_server");
  grid->userserver = grid->assetserver = NULL;
  grid->grid_recvkey = sim_config_get_value(sim,"grid","grid_recvkey");
  grid->grid_sendkey = sim_config_get_value(sim,"grid","grid_sendkey");
  grid->user_recvkey = grid->user_sendkey = NULL;
  grid->asset_recvkey = grid->asset_sendkey = NULL;

  sim_http_add_handler(sim, "/", xmlrpc_handler, 
		       sim, NULL);
  sim_http_add_handler(sim, "/agent/", agent_rest_handler, 
		       sim, NULL);

  return true;
}

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
#include "cajeput_user.h"
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
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <netinet/ip.h>

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
  // GError *error = NULL;
  SoupMessage *msg;
  char *ip_addr = sim_get_ip_addr(sim);
  GRID_PRIV_DEF(sim);

  printf("Logging into grid...\n");

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

static int helper_soup_hash_get_uuid(GHashTable *hash, const char* name,
				     uuid_t u) {
  const char *s;
  if(!soup_value_hash_lookup(hash, name, G_TYPE_STRING, &s)) 
    return -1;
  return uuid_parse(s, u);
}

/* actually passed the "appearance" member of the request */
static void expect_user_set_appearance(user_ctx *user,  GHashTable *hash) {
  GByteArray *data; char *s;
  if(soup_value_hash_lookup(hash,"visual_params",SOUP_TYPE_BYTE_ARRAY,
			    &data)) {
    sl_string str;
    sl_string_set_bin(&str,data->data,data->len);
    user_set_visual_params(user, &str);
  } else {
    printf("WARNING: expect_user is missing visual_params\n");
  }  

  if(soup_value_hash_lookup(hash,"texture",SOUP_TYPE_BYTE_ARRAY,
			    &data)) {
    sl_string str;
    sl_string_set_bin(&str,data->data,data->len);
    user_set_texture_entry(user, &str);
  } else {
    printf("WARNING: expect_user is missing texture data\n");
  }  

  if(soup_value_hash_lookup(hash, "serial", G_TYPE_STRING, &s)) {
    user_set_wearable_serial(user, atoi(s)); // FIXME - type correctness
  } else {
    printf("WARNING: expect_user is missing serial\n");    
  }


  for(int i = 0; i < SL_NUM_WEARABLES; i++) {
    char asset_str[24], item_str[24];
    uuid_t item_id, asset_id;
    sprintf(asset_str,"%s_asset",sl_wearable_names[i]);
    sprintf(item_str,"%s_item",sl_wearable_names[i]);
    if(helper_soup_hash_get_uuid(hash, asset_str, asset_id) || 
       helper_soup_hash_get_uuid(hash, item_str, item_id)) {
      printf("Error: couldn't find wearable %s in expect_user\n",
	     sl_wearable_names[i]);
    } else {
      user_set_wearable(user, i, item_id, asset_id);
    }
  }
}

static void xmlrpc_expect_user_2(void* priv, int is_ok) {
  struct expect_user_state *state = (struct expect_user_state*)priv;
  GHashTable *args = state->args;
  GHashTable *hash; user_ctx *user;
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
  user = sim_prepare_new_user(state->sim, &uinfo);
  if(soup_value_hash_lookup(args,"appearance",G_TYPE_HASH_TABLE,
			    &hash)) {
    expect_user_set_appearance(user, hash);
  } else {
    printf("WARNING: expect_user is missing appearance data\n");
  }
  

 
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

void osglue_validate_session(struct simulator_ctx* sim, const char* agent_id,
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
    osglue_validate_session(sim, agent_id, session_id, grid,
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
  // GError *error = NULL;
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
  char *method_name;
  GValueArray *params;

  if(strcmp(path,"/") != 0) {
    printf("DEBUG: request for unhandled path %s\n",
	   path);
    if (msg->method == SOUP_METHOD_POST) {
      printf("DEBUG: POST data is ~%s~\n",
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
    printf("DEBUG: ~%s~\n", msg->request_body->data);
    soup_message_set_status(msg,500);
    return;
  }

  if(strcmp(method_name, "logoff_user") == 0) {
    xmlrpc_logoff_user(server, msg, params, sim);
    
  } else if(strcmp(method_name, "expect_user") == 0) {
    printf("DEBUG: expect_user ~%s~\n", msg->request_body->data);
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
  // GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF(sim);

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

static void user_created(struct simulator_ctx* sim,
			 struct user_ctx* user,
			 void **user_priv) {
  // GRID_PRIV_DEF(sim);
  user_grid_glue *user_glue = new user_grid_glue();
  user_glue->ctx = user;
  user_glue->refcnt = 1;
  user_glue->enter_callback_uri = NULL;
  *user_priv = user_glue;
}

static void user_deleted(struct simulator_ctx* sim,
			 struct user_ctx* user,
			 void *user_priv) {
  USER_PRIV_DEF(user_priv);
  user_glue->ctx = NULL;
  free(user_glue->enter_callback_uri);
  user_glue->enter_callback_uri = NULL;  
  user_grid_glue_deref(user_glue);
}

static void user_entered_callback_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  simulator_ctx *sim = (simulator_ctx*) user_data;
  sim_shutdown_release(sim);
  // FIXME - should probably pay *some* attention to the response
  if(msg->status_code != 200) {
    printf("User entry callback failed: got %i %s\n",
	   (int)msg->status_code,msg->reason_phrase);
  }

}

void user_entered(simulator_ctx *sim, user_ctx *user,
		  void *user_priv) {
  USER_PRIV_DEF(user_priv);

  if(user_glue->enter_callback_uri != NULL) {
    printf("DEBUG: calling back to %s on avatar entry\n",
	   user_glue->enter_callback_uri);

    // FIXME - shouldn't we have to, y'know, identify ourselves somehow?

    SoupMessage *msg = 
      soup_message_new ("DELETE", user_glue->enter_callback_uri);
    // FIXME - should we send a body at all?
    soup_message_set_request (msg, "text/plain",
			      SOUP_MEMORY_STATIC, "", 0);
    sim_shutdown_hold(sim);
    sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			   user_entered_callback_resp, sim);
    free(user_glue->enter_callback_uri);
    user_glue->enter_callback_uri = NULL;  
    
  }
}

void user_grid_glue_ref(user_grid_glue *user_glue) {
  user_glue->refcnt++;
}

void user_grid_glue_deref(user_grid_glue *user_glue) {
  user_glue->refcnt--;
  if(user_glue->refcnt == 0) {
    assert(user_glue->ctx == NULL);
    free(user_glue->enter_callback_uri);
    delete user_glue;
  }
}

struct asset_req_desc {
  struct simulator_ctx *sim;
  texture_desc *texture;
};

static void get_texture_resp(SoupSession *session, SoupMessage *msg, 
			     gpointer user_data) {
  asset_req_desc* req = (asset_req_desc*)user_data;
  texture_desc *texture = req->texture;
  const char* content_type = 
    soup_message_headers_get_content_type(msg->response_headers, NULL);
  sim_shutdown_release(req->sim);

  printf("Get texture resp: got %i %s (len %i)\n",
	 (int)msg->status_code, msg->reason_phrase, 
	 (int)msg->response_body->length);

  if(msg->status_code >= 400 && msg->status_code < 500) {
    // not transitory, don't bother retrying
    texture->flags |= CJP_TEXTURE_MISSING;
  } else if(msg->status_code == 200) {
    int is_xml_crap = 0;
    char buf[40]; uuid_unparse(texture->asset_id, buf);
    printf("DEBUG: filling in texture entry %p for %s\n", texture, buf);
    if(content_type != NULL && strcmp(content_type, "application/xml") == 0) {
      printf("WARNING: server fobbed us off with XML for %s\n", buf);
      is_xml_crap = 1;
    } else if(msg->response_body->length >= 3 &&
	      (strncmp(msg->response_body->data, "\xef\xbb\xbf",3) == 0 ||
	       strncmp(msg->response_body->data, "<?x",3) == 0)) {
      // real J2K images don't have a UTF-8 BOM, or an XML marker
      printf("WARNING: server fobbed us off with XML for %s and LIED!\n", buf);
      is_xml_crap = 1;      
    }
    if(is_xml_crap) {
      xmlDocPtr doc = xmlReadMemory(msg->response_body->data,
				    msg->response_body->length,"asset.xml",
				    NULL,0);
      if(doc == NULL) {
	printf("ERROR: XML parse failed for texture asset\n");
	goto out_fail;
      }

      xmlNodePtr node = xmlDocGetRootElement(doc)->children;
      while(node != NULL && (node->type == XML_TEXT_NODE || 
			     node->type == XML_COMMENT_NODE))
	node = node->next;
      if(node == NULL || node->type != XML_ELEMENT_NODE || 
	 strcmp((const char*)node->name, "Data") != 0) {
	printf("ERROR: didn't get expected <Data> node parsing texture asset\n");
	xmlFreeDoc(doc);
	goto out_fail;
      }

      char *texstr = (char*)xmlNodeListGetString(doc, node->children, 1);
      gsize sz;
      // HACK - we're overwriting data that's only loosely ours.
      g_base64_decode_inplace(texstr, &sz);

      
      texture->len = sz;
      texture->data = (unsigned char*)malloc(texture->len);
      memcpy(texture->data, texstr, texture->len);     

      xmlFree(texstr);
      xmlFreeDoc(doc);
    } else {
      texture->len = msg->response_body->length;
      texture->data = (unsigned char*)malloc(texture->len);
      memcpy(texture->data, msg->response_body->data, texture->len);
    }
    sim_texture_finished_load(texture);
  } else {
    // HACK!
    texture->flags |= CJP_TEXTURE_MISSING;
  }

  out_fail:
  texture->flags &= ~CJP_TEXTURE_PENDING;
  delete req;
}

// FIXME - move texture stuff elsewhere
static void get_texture(struct simulator_ctx *sim, struct texture_desc *texture) {
  GRID_PRIV_DEF(sim);
  // FIXME - should allocate proper buffer
  char url[255], asset_id[40];
  assert(grid->assetserver != NULL);

  uuid_unparse(texture->asset_id, asset_id);
  snprintf(url, 255, "%sassets/%s/data", grid->assetserver, asset_id);
  printf("DEBUG: requesting asset %s\n", url);

  SoupMessage *msg = soup_message_new ("GET", url);
  asset_req_desc *req = new asset_req_desc;
  req->texture = texture; req->sim = sim;
  sim_shutdown_hold(sim);
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			 get_texture_resp, req);

}

struct map_block_state {
  struct simulator_ctx *sim;
  void(*cb)(void *priv, struct map_block_info *blocks, 
	    int count);
  void *cb_priv;

  map_block_state(struct simulator_ctx *sim_, void(*cb_)(void *priv, struct map_block_info *blocks, 
							 int count), void *cb_priv_) : 
    sim(sim_), cb(cb_), cb_priv(cb_priv_) { };
};

static void got_map_block_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct map_block_state *st = (map_block_state*)user_data;
  //GRID_PRIV_DEF(st->sim);
  struct map_block_info *blocks;
  int num_blocks = 0;
  GHashTable *hash = NULL;
  GValueArray *sims = NULL;

  sim_shutdown_release(st->sim);

  if(msg->status_code != 200) {
    printf("Map block request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    printf("Map block request failed: couldn't parse response\n");
    goto out_fail;
  }

  //printf("DEBUG: map block response ~%s~\n", msg->response_body->data);
  printf("DEBUG: got map block response\n");

  if(!soup_value_hash_lookup(hash,"sim-profiles",G_TYPE_VALUE_ARRAY,&sims)
     || sims == NULL) {
    printf("Map block request failed: no/bad sim-profiles member\n");
    goto out_free_fail;
  }

  blocks = new map_block_info[sims->n_values];
  for(unsigned int i = 0; i < sims->n_values; i++) {
    GHashTable *sim_info = NULL; char *s; int val;
    if(!soup_value_array_get_nth(sims, i, G_TYPE_HASH_TABLE, &sim_info) ||
       sim_info == NULL) {
      printf("Map block request bad: expected hash table\n");
      continue;
    }
    
    if(!soup_value_hash_lookup(sim_info, "x", G_TYPE_STRING, &s))
      goto bad_block;
    blocks[num_blocks].x = atoi(s);
    if(!soup_value_hash_lookup(sim_info, "y", G_TYPE_STRING, &s))
      goto bad_block;
    blocks[num_blocks].y = atoi(s);
    if(!soup_value_hash_lookup(sim_info, "name", G_TYPE_STRING, &s))
      goto bad_block;
    blocks[num_blocks].name = s; // the caller can copy this themselves!
    if(!soup_value_hash_lookup(sim_info, "access", G_TYPE_STRING, &s))
      goto bad_block;
    blocks[num_blocks].access = atoi(s);
    if(!soup_value_hash_lookup(sim_info, "water-height", G_TYPE_INT, &val))
      goto bad_block;
    blocks[num_blocks].water_height = val;
    if(!soup_value_hash_lookup(sim_info, "agents", G_TYPE_INT, &val))
      goto bad_block;
    blocks[num_blocks].num_agents = val;
    if(!soup_value_hash_lookup(sim_info, "region-flags", G_TYPE_INT, &val))
      goto bad_block;
    blocks[num_blocks].flags = val;
    if(!soup_value_hash_lookup(sim_info, "map-image-id", G_TYPE_STRING, &s))
      goto bad_block;
    uuid_parse(s, blocks[num_blocks].map_image); // FIXME - check return value
    if(!soup_value_hash_lookup(sim_info, "sim_ip", G_TYPE_STRING, &s))
      goto bad_block;
    blocks[num_blocks].sim_ip = s;
    if(!soup_value_hash_lookup(sim_info, "sim_port", G_TYPE_STRING, &s))
      goto bad_block;
    blocks[num_blocks].sim_port = atoi(s);
    if(!soup_value_hash_lookup(sim_info, "http_port", G_TYPE_STRING, &s))
      goto bad_block;
    blocks[num_blocks].http_port = atoi(s);
    if(!soup_value_hash_lookup(sim_info, "uuid", G_TYPE_STRING, &s))
      goto bad_block;
    uuid_parse(s, blocks[num_blocks].region_id); // FIXME - check return value
    
    // FIXME - do something with regionhandle, sim_uri?

    printf("DEBUG: map block %i,%i is %s\n",
	   blocks[num_blocks].x, blocks[num_blocks].y, blocks[num_blocks].name);

    num_blocks++;
    
    // FIXME - TODO
      
    continue;    
  bad_block:
    printf("WARNING: Map block response has bad block, skipping\n");
  }


  st->cb(st->cb_priv, blocks, num_blocks);

  delete[] blocks;
  g_hash_table_destroy(hash); // must be after calling the callback
  delete st;
  return;

  // FIXME - TODO

 out_free_fail:
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(st->cb_priv, NULL, 0);
  delete st;
}

static void map_block_request(struct simulator_ctx *sim, int min_x, int max_x, 
			      int min_y, int max_y, 
			      void(*cb)(void *priv, struct map_block_info *blocks, 
					int count),
			      void *cb_priv) {
  
  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF(sim);

  printf("DEBUG: map block request (%i,%i)-(%i,%i)\n",
	 min_x, min_y, max_x, max_y);

  hash = soup_value_hash_new();
  soup_value_hash_insert(hash,"xmin",G_TYPE_INT,min_x);
  soup_value_hash_insert(hash,"xmax",G_TYPE_INT,max_x);
  soup_value_hash_insert(hash,"ymin",G_TYPE_INT,min_y);
  soup_value_hash_insert(hash,"ymax",G_TYPE_INT,max_y);
  

  msg = soup_xmlrpc_request_new(grid->gridserver, "map_block",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create xmlrpc map request\n");
    cb(cb_priv, NULL, 0);
    return;
  }

  sim_shutdown_hold(sim);
  // FIXME - why SOUP_MESSAGE(foo)?
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			 got_map_block_resp, new map_block_state(sim,cb,cb_priv));
}

#if 0
struct os_region_info { // FIXME - merge with other region info desc.
  int x, y;
  char *name;
  char *sim_ip;
  int sim_port, http_port;
  uuid_t region_id;
  // FIXME - TODO
};
#endif

struct region_info_state {
  simulator_ctx* sim;
  void(*cb)(simulator_ctx* sim, void* cb_priv, map_block_info* info);
  void *cb_priv;
};


static void got_region_info(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct region_info_state *st = (region_info_state*)user_data;
  struct map_block_info info;
  //GRID_PRIV_DEF(st->sim);
  GHashTable *hash = NULL;
  char *s; //uuid_t u;

  sim_shutdown_release(st->sim);

  if(msg->status_code != 200) {
    printf("Region info request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    printf("Region info request failed: couldn't parse response\n");
    goto out_fail;
  }

  printf("DEBUG: region info response ~%s~\n", msg->response_body->data);
  //printf("DEBUG: got map block response\n");
   
  if(!soup_value_hash_lookup(hash, "region_locx", G_TYPE_STRING, &s))
    goto bad_block;
  info.x = atoi(s);
  if(!soup_value_hash_lookup(hash, "region_locy", G_TYPE_STRING, &s))
    goto bad_block;
  info.y = atoi(s);
  if(!soup_value_hash_lookup(hash, "region_name", G_TYPE_STRING, &s))
    goto bad_block;
  info.name = s;
  if(!soup_value_hash_lookup(hash, "sim_ip", G_TYPE_STRING, &s))
    goto bad_block;
  info.sim_ip = s;
  if(!soup_value_hash_lookup(hash, "sim_port", G_TYPE_STRING, &s))
    goto bad_block;
  info.sim_port = atoi(s);
   if(!soup_value_hash_lookup(hash, "http_port", G_TYPE_STRING, &s))
    goto bad_block;
  info.http_port = atoi(s);
  if(!soup_value_hash_lookup(hash, "region_UUID", G_TYPE_STRING, &s))
    goto bad_block;
  uuid_parse(s, info.region_id); // FIXME - check return value

  // NOTE! This doesn't fill out a bunch of stuff that it should, because the
  // response doesn't contain the required information!

  // FIXME - use regionHandle, server_uri!
 
  st->cb(st->sim, st->cb_priv, &info);
  g_hash_table_destroy(hash);
  delete st;
  return;

 bad_block:
  printf("ERROR: couldn't lookup expected values in region info reply\n");
  //out_free_fail:
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(st->sim, st->cb_priv, NULL);
  delete st;
}

// WARNING: while superficially, this looks like a map block request-type
// function, it does NOT return enough information for such a purpose, and is
// for INTERNAL USE ONLY.
static void req_region_info(struct simulator_ctx* sim, uint64_t handle,
			    void(*cb)(simulator_ctx* sim, void* cb_priv, map_block_info* info),
			    void *cb_priv) {
  GRID_PRIV_DEF(sim);
  char buf[40];

  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;

  hash = soup_value_hash_new();
  sprintf(buf, "%llu", handle);
  soup_value_hash_insert(hash,"region_handle",G_TYPE_STRING,buf);
  soup_value_hash_insert(hash,"authkey",G_TYPE_STRING,grid->grid_sendkey);

  msg = soup_xmlrpc_request_new(grid->gridserver, "simulator_data_request",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create region_info request\n");
    cb(sim, cb_priv, NULL);
    return;
  }

  region_info_state *st = new region_info_state();
  st->sim = sim; st->cb = cb; st->cb_priv = cb_priv;

  sim_shutdown_hold(sim);
  // FIXME - why SOUP_MESSAGE(foo)?
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			 got_region_info, st);

}


struct region_by_name_state {
  simulator_ctx* sim;
  void(*cb)(void* cb_priv, map_block_info* info, int count);
  void *cb_priv;
};

static void got_region_by_name(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct region_by_name_state *st = (region_by_name_state*)user_data;
  struct map_block_info *info; int count;
  //GRID_PRIV_DEF(st->sim);
  GHashTable *hash = NULL;
  char *s; //uuid_t u;

  sim_shutdown_release(st->sim);

  if(msg->status_code != 200) {
    printf("Region info request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    printf("Region info request failed: couldn't parse response\n");
    goto out_fail;
  }

  printf("DEBUG: region by name response ~%s~\n", msg->response_body->data);

  if(!soup_value_hash_lookup(hash, "numFound", G_TYPE_INT, &count))
    goto bad_resp;
  printf("DEBUG: region by name lookup returned %i items\n", count);
  
  if(count < 0) count = 0; 
  if(count > 100) count = 100;
  info = new map_block_info[count];

  for(int i = 0; i < count && i >= 0; i++) {
      char memb[40];
      snprintf(memb,40,"region%i.region_locx",i);
      if(!soup_value_hash_lookup(hash, memb, G_TYPE_STRING, &s))
	goto bad_block;
      info[i].x = atoi(s);

      snprintf(memb,40,"region%i.region_locy",i);
      if(!soup_value_hash_lookup(hash, memb, G_TYPE_STRING, &s))
	goto bad_block;
      info[i].y = atoi(s);

      snprintf(memb,40,"region%i.region_name",i);
      if(!soup_value_hash_lookup(hash, memb, G_TYPE_STRING, &s))
	goto bad_block;
      info[i].name = s;

      snprintf(memb,40,"region%i.sim_ip",i);
      if(!soup_value_hash_lookup(hash, memb, G_TYPE_STRING, &s))
	goto bad_block;
      info[i].sim_ip = s;

      snprintf(memb,40,"region%i.sim_port",i);
      if(!soup_value_hash_lookup(hash, memb, G_TYPE_STRING, &s))
	goto bad_block;
      info[i].sim_port = atoi(s);

      snprintf(memb,40,"region%i.http_port",i);
      if(!soup_value_hash_lookup(hash, memb, G_TYPE_STRING, &s))
	goto bad_block;
      info[i].http_port = atoi(s);

      snprintf(memb,40,"region%i.region_UUID",i);
      if(!soup_value_hash_lookup(hash, memb, G_TYPE_STRING, &s))
	goto bad_block;
      uuid_parse(s, info[i].region_id); // FIXME - check return value

      // FIXME - the response doesn't have these details. Not needed?
      info[i].num_agents = 0; info[i].water_height = 0;
      info[i].flags = 0;

      // FIXME FIXME - one part of the OpenSim code expects this, but the part
      // that actually parses the response doesn't fill it in, and the response
      // doesn't have it. Bug somewhere?
      info[i].access = 0;
      
      // FIXME - use regionHandle, server_uri!
    }

 
  st->cb(st->cb_priv, info, count);
  delete[] info;
  g_hash_table_destroy(hash);
  delete st;
  return;

 bad_block:
  delete[] info;
 bad_resp:
  printf("ERROR: couldn't lookup expected values in region by name reply\n");
  //out_free_fail:
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(st->cb_priv, NULL, 0);
  delete st;
}

static void map_name_request(struct simulator_ctx* sim, const char* name,
			    void(*cb)(void* cb_priv, map_block_info* info, int count),
			    void *cb_priv) {
  GRID_PRIV_DEF(sim);

  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;

  hash = soup_value_hash_new();
  soup_value_hash_insert(hash,"name",G_TYPE_STRING,name);
  soup_value_hash_insert(hash,"maxNumber",G_TYPE_STRING,"20"); // FIXME

  msg = soup_xmlrpc_request_new(grid->gridserver, "search_for_region_by_name",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create search_for_region_by_name request\n");
    cb(cb_priv, NULL, 0);
    return;
  }

  region_by_name_state *st = new region_by_name_state();
  st->sim = sim; st->cb = cb; st->cb_priv = cb_priv;

  sim_shutdown_hold(sim);
  // FIXME - why SOUP_MESSAGE(foo)?
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			 got_region_by_name, st);

}

void osglue_teleport_failed(os_teleport_desc *tp_priv, const char* reason) {
  user_teleport_failed(tp_priv->tp, reason);
  free(tp_priv->sim_ip);
  free(tp_priv->caps_path);
  delete tp_priv;
}

static void do_teleport_resolve_cb(SoupAddress *addr,
				   guint status,
				   gpointer priv) {
  os_teleport_desc* tp_priv = (os_teleport_desc*)priv;
  sim_shutdown_release(tp_priv->our_sim);

  if(tp_priv->tp->ctx == NULL) {
    osglue_teleport_failed(tp_priv,"cancelled");
  } else if(status != SOUP_STATUS_OK) {
    osglue_teleport_failed(tp_priv,"Couldn't resolve sim IP address");
  } else {
    int len;
    struct sockaddr_in *saddr = (struct sockaddr_in*)
      soup_address_get_sockaddr(addr, &len);
    if(saddr == NULL || saddr->sin_family != AF_INET) {
      // FIXME - need to restrict resolution to IPv4
      osglue_teleport_failed(tp_priv, "FIXME: Got a non-AF_INET address for sim");
    } else {
      teleport_desc* tp = tp_priv->tp;
      tp->sim_port = tp_priv->sim_port;
      tp->sim_ip = saddr->sin_addr.s_addr;
      osglue_teleport_send_agent(tp_priv->our_sim, tp, tp_priv);
    }
  }
  // FIXME - free SoupAddress??
}

static void do_teleport_rinfo_cb(struct simulator_ctx* sim, void *priv, 
				 map_block_info *info) {
  teleport_desc* tp = (teleport_desc*)priv;
  if(tp->ctx == NULL) {
    user_teleport_failed(tp, "cancelled");
  } else if(info == NULL) {
    user_teleport_failed(tp, "Couldn't find destination region");
  } else {
    os_teleport_desc *tp_priv = new os_teleport_desc();
    tp_priv->our_sim = sim;
    tp_priv->sim_ip = strdup(info->sim_ip);
    tp_priv->sim_port = info->sim_port;
    tp_priv->http_port = info->http_port;
    tp_priv->tp = tp;
    tp_priv->caps_path = NULL;

    // FIXME - use provided region handle

    // FIXME - do we really need to hold simulator shutdown here?
    sim_shutdown_hold(sim);
    SoupAddress *addr = soup_address_new(info->sim_ip, 0);
    soup_address_resolve_async(addr, g_main_context_default(),
			       NULL, do_teleport_resolve_cb, tp_priv);
  }
}

static void do_teleport(struct simulator_ctx* sim, struct teleport_desc* tp) {
  //GRID_PRIV_DEF(sim);
 
  // FIXME - handle teleport via region ID
  user_teleport_progress(tp, "resolving");
  req_region_info(sim, tp->region_handle, do_teleport_rinfo_cb, tp);
}

struct user_profile {
  uuid_t uuid;
  char *first, *last;
};

struct user_by_id_state {
  struct simulator_ctx *sim;
  void(*cb)(user_profile* profile, void *priv);
  void *cb_priv;
  
  user_by_id_state(simulator_ctx *sim_, void(*cb_)(user_profile* profile, void *priv),
		   void *cb_priv_) : sim(sim_), cb(cb_), cb_priv(cb_priv_) { };
};

static void got_user_by_id_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct user_by_id_state *st = (user_by_id_state*)user_data;
  //GRID_PRIV_DEF(st->sim);
  user_profile profile;
  GHashTable *hash = NULL;

  sim_shutdown_release(st->sim);

  if(msg->status_code != 200) {
    printf("User by ID req failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }

  printf("DEBUG: user by ID response ~%s~\n", msg->response_body->data);

  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    printf("User by ID req failed: couldn't parse response\n");
    goto out_fail;
  }

  if(helper_soup_hash_get_uuid(hash, "uuid", profile.uuid))
    goto bad_data;

  if(!soup_value_hash_lookup(hash, "firstname", G_TYPE_STRING, &profile.first))
    goto bad_data;
  if(!soup_value_hash_lookup(hash, "lastname", G_TYPE_STRING, &profile.last))
    goto bad_data;
  
  // FIXME - TODO parse rest of response

  // FIXME - cache ID to username mapping!
  
  st->cb(&profile, st->cb_priv);

  g_hash_table_destroy(hash); // must be after calling the callback
  delete st;
  return;

 bad_data:
  printf("ERROR: bad/missing data in user by ID response\n");
 out_free_fail:
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(NULL, st->cb_priv);
  delete st;
}


static void user_profile_by_id(struct simulator_ctx *sim, uuid_t id, 
			       void(*cb)(user_profile* profile, void *priv),
			       void *cb_priv) {
  char buf[40];
  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF(sim);

  hash = soup_value_hash_new();
  uuid_unparse(id, buf);
  soup_value_hash_insert(hash,"avatar_uuid",G_TYPE_STRING,buf);

  msg = soup_xmlrpc_request_new(grid->userserver, "get_user_by_uuid",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create get_user_by_uuid request\n");
    cb(NULL, cb_priv);
    return;
  }

  sim_shutdown_hold(sim);
  // FIXME - why SOUP_MESSAGE(foo)?
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			 got_user_by_id_resp, new user_by_id_state(sim,cb,cb_priv));
}		       

struct uuid_to_name_state {
  uuid_t uuid;
  void(*cb)(uuid_t uuid, const char* first, 
	    const char* last, void *priv);
  void *cb_priv;

  uuid_to_name_state(void(*cb2)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
		     void *cb_priv2, uuid_t uuid2) : cb(cb2), cb_priv(cb_priv2) {
    uuid_copy(uuid, uuid2);
  };
};

static void uuid_to_name_cb(user_profile* profile, void *priv) {
  uuid_to_name_state* st = (uuid_to_name_state*)priv;
  if(profile == NULL) {
    st->cb(st->uuid,NULL,NULL,st->cb_priv);
  } else {
    st->cb(st->uuid,profile->first,profile->last,st->cb_priv);
  }
  delete st;
}

static void uuid_to_name(struct simulator_ctx *sim, uuid_t id, 
			 void(*cb)(uuid_t uuid, const char* first, 
				   const char* last, void *priv),
			 void *cb_priv) {
  // FIXME - use cached UUID->name mappings once we have some
  user_profile_by_id(sim, id, uuid_to_name_cb, 
		     new uuid_to_name_state(cb,cb_priv,id));
  //cb(id, NULL, NULL, cb_priv); // FIXME!!!  
}

static void cleanup(struct simulator_ctx* sim) {
  GRID_PRIV_DEF(sim);
  g_free(grid->userserver);
  g_free(grid->gridserver);
  g_free(grid->assetserver);
  g_free(grid->inventoryserver);
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
  hooks->user_entered = user_entered;
  //hooks->fetch_user_inventory = fetch_user_inventory;
  hooks->map_block_request = map_block_request;
  hooks->map_name_request = map_name_request;
  hooks->do_teleport = do_teleport;
  hooks->fetch_inventory_folder = fetch_inventory_folder;
  hooks->uuid_to_name = uuid_to_name;

  hooks->get_texture = get_texture;
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
  sim_http_add_handler(sim, "/agent/", osglue_agent_rest_handler, 
		       sim, NULL);

  return true;
}

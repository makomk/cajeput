#include "cajeput_core.h"
#include <libsoup/soup.h>
#include "sl_types.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <libsoup/soup.h>
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
  uuid_unparse(zero_uuid, buf);
  soup_value_hash_insert(hash,"master_avatar_uuid",G_TYPE_STRING,buf);
  sprintf(buf,"%i",(int)sim_get_http_port(sim));
  soup_value_hash_insert(hash,"http_port",G_TYPE_STRING,buf);
  uuid_unparse(grid->region_secret, buf);
  soup_value_hash_insert(hash,"region_secret",G_TYPE_STRING,buf);
  soup_value_hash_insert(hash,"sim_name",G_TYPE_STRING,sim_get_name(sim));
  sim_get_region_uuid(sim, u);
  uuid_unparse(u, buf);
  soup_value_hash_insert(hash,"UUID",G_TYPE_STRING,buf);
  sprintf(buf,"http://127.0.0.1:%i",(int)sim_get_http_port(sim)); // FIXME
  soup_value_hash_insert(hash,"server_uri",G_TYPE_STRING,buf);
  sprintf(buf,"%i",(int)sim_get_region_x(sim));
  soup_value_hash_insert(hash,"region_locx",G_TYPE_STRING,buf);
  sprintf(buf,"%i",(int)sim_get_region_y(sim));
  soup_value_hash_insert(hash,"region_locy",G_TYPE_STRING,buf);
  soup_value_hash_insert(hash,"sim_ip",G_TYPE_STRING,"127.0.0.1"); // FIXME
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

static void xmlrpc_expect_user_2(SoupServer *server,
				 SoupMessage *msg,
				 GValueArray *params,
				 GHashTable *args,
				 struct simulator_ctx* sim, int is_ok) {
  GHashTable *hash;
  struct sim_new_user uinfo;
  //char *first_name, *last_name;
  char *caps_path, *s;
  //uuid_t session_id, agent_id, secure_session_id;
  char seed_cap[50];
  soup_server_unpause_message(server,msg);

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
  sim_prepare_new_user(sim, &uinfo);

 
  hash = soup_value_hash_new();
  soup_value_hash_insert(hash, "success", G_TYPE_STRING, "TRUE");
  soup_xmlrpc_set_response(msg, G_TYPE_HASH_TABLE, hash);
  g_hash_table_destroy(hash);
  g_value_array_free(params);
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
  g_value_array_free(params);
  soup_xmlrpc_set_fault(msg, SOUP_XMLRPC_FAULT_SERVER_ERROR_INVALID_METHOD_PARAMETERS,
			"Bad arguments");  
}

static void got_check_session_resp(SoupSession *session, SoupMessage *msg, 
				   gpointer user_data) {
  GHashTable *hash; char *s;
  int is_ok = 0;
  struct expect_user_state* st = (struct expect_user_state*) user_data;
  printf("DEBUG: got check_auth_session response\n");
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
  xmlrpc_expect_user_2(st->server, st->msg, st->params, 
		       st->args, st->sim, is_ok);
  delete st;
}


static void xmlrpc_expect_user(SoupServer *server,
			       SoupMessage *msg,
			       GValueArray *params,
			       struct simulator_ctx* sim) {
  GRID_PRIV_DEF(sim);
  GHashTable *args = NULL;
  char *agent_id, *session_id, *s;
  uint64_t region_handle;
  SoupMessage *val_msg;
  GHashTable *hash;
  GError *error = NULL;
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

  {
    expect_user_state *state = new expect_user_state();
    state->server = server;
    state->msg = msg;
    state->params = params;
    state->args = args;
    state->sim = sim;
  
    soup_server_pause_message(server,msg);

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

    // FIXME - why SOUP_MESSAGE(foo)?
    sim_queue_soup_message(sim, SOUP_MESSAGE(val_msg),
			   got_check_session_resp, state);
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
  GRID_PRIV_DEF(sim);
  GHashTable *hash = NULL;
  char *s; uuid_t u;
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

static void user_created(struct simulator_ctx* sim,
			 struct user_ctx* user,
			 void **user_priv) {
  GRID_PRIV_DEF(sim);
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
  grid->grid_recvkey = grid->grid_sendkey = "null"; // FIXME
  grid->user_recvkey = grid->user_sendkey = NULL;
  grid->asset_recvkey = grid->asset_sendkey = NULL;

  sim_http_add_handler(sim, "/", xmlrpc_handler, 
		       sim, NULL);

  return true;
}

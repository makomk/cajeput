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

// FIXME FIXME TODO - finish support for new user server.

#include "cajeput_core.h"
#include "cajeput_plugin.h"
#include "cajeput_user.h"
#include "cajeput_grid_glue.h"
#include "caj_logging.h"
#include <libsoup/soup.h>
#include "caj_types.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include "opensim_grid_glue.h"
#include "opensim_robust_xml.h"
#include <cassert>
#include <netinet/ip.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#define MIN_V2_PROTO_VERSION "0"
#define MAX_V2_PROTO_VERSION "0"

static void got_grid_login_response_v1(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)user_data;
  GRID_PRIV_DEF(sim);
  GHashTable *hash = NULL;
  char *s; uuid_t u;
  if(msg->status_code != 200) {
    CAJ_ERROR("Grid login failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    exit(1);
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    CAJ_ERROR("Grid login failed: couldn't parse response\n");
    exit(1);    
  }

  if(soup_value_hash_lookup(hash,"restricted",G_TYPE_STRING,
			    &s)) {
    // grid actually report it as an error, not as this
    CAJ_ERROR("Error - grid not accepting registrations: %s\n",s);
    exit(1);
  } else if(soup_value_hash_lookup(hash,"error",G_TYPE_STRING,
			    &s)) {
    CAJ_ERROR("Grid refused registration: %s\n",s);
    exit(1);
  }

  if(!soup_value_hash_lookup(hash,"authkey",G_TYPE_STRING,
			     &s)) goto bad_resp;
  if(uuid_parse(s, u) || uuid_compare(u,grid->region_secret) != 0) {
    CAJ_ERROR("Unexpected authkey value!\n");
    goto bad_resp;
  }
  if(!soup_value_hash_lookup(hash,"regionname",G_TYPE_STRING,
			     &s)) goto bad_resp;
  if(strcmp(s,sim_get_name(sim)) != 0) {
    CAJ_WARN("DEBUG: simname mismatch; we're \"%s\", server says \"%s\"\n",
	      sim_get_name(sim),s);
  }
#if 0
  // FIXME - use user_recvkey, asset_recvkey, user_sendkey, asset_sendkey
  // FIXME - check region_locx/y, sim_ip, sim_port
  if(!soup_value_hash_lookup(hash,"user_url",G_TYPE_STRING,
			     &s)) goto bad_resp;
  if(grid->userserver == NULL) grid->userserver = g_strdup(s);
  if(!soup_value_hash_lookup(hash,"asset_url",G_TYPE_STRING,
			     &s)) goto bad_resp;
  if(grid->assetserver == NULL) grid->assetserver = g_strdup(s);
#endif

  //printf("DEBUG: login response ~%s~\n",msg->response_body->data);
  CAJ_PRINT("Grid login complete\n");
  g_hash_table_destroy(hash);
  

  return;
 bad_resp:
  CAJ_ERROR("Bad grid login response\n");
  CAJ_ERROR("DEBUG {{%s}}\n", msg->response_body->data);
  g_hash_table_destroy(hash);
  exit(1);
  
  // TODO
  // Looks like we get the user and asset server URLs back,
  // plus a bunch of info we sent ourselves.
}

static void do_grid_login_v1(struct simgroup_ctx *sgrp,
			  struct simulator_ctx* sim) {
  char buf[40];
  uuid_t zero_uuid, u;
  uuid_clear(zero_uuid);
  GHashTable *hash;
  // GError *error = NULL;
  SoupMessage *msg;
  char *ip_addr = sim_get_ip_addr(sim);
  GRID_PRIV_DEF(sim);

  CAJ_INFO("Logging into grid...\n");

  hash = soup_value_hash_new();
  soup_value_hash_insert(hash,"authkey",G_TYPE_STRING,grid->grid_sendkey);
  soup_value_hash_insert(hash,"recvkey",G_TYPE_STRING,grid->grid_recvkey);
  soup_value_hash_insert(hash,"major_interface_version",G_TYPE_STRING,"6");
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
  
  // no, this isn't a mistake, we have to put the *UDP* port here and not the 
  // HTTP one. Otherwise, stuff breaks. *glares at OpenSim*
  sprintf(buf,"http://%s:%i",ip_addr, (int)sim_get_udp_port(sim));
  soup_value_hash_insert(hash,"server_uri",G_TYPE_STRING,buf);
  sprintf(buf,"%i",(int)sim_get_region_x(sim));
  soup_value_hash_insert(hash,"region_locx",G_TYPE_STRING,buf);
  sprintf(buf,"%i",(int)sim_get_region_y(sim));
  soup_value_hash_insert(hash,"region_locy",G_TYPE_STRING,buf);
  soup_value_hash_insert(hash,"sim_ip",G_TYPE_STRING,ip_addr);
  soup_value_hash_insert(hash,"remoting_port",G_TYPE_STRING,"8895"); // ??? FIXME
  sprintf(buf,"%i",(int)sim_get_udp_port(sim));
  soup_value_hash_insert(hash,"sim_port",G_TYPE_STRING,buf);
  uuid_unparse(zero_uuid, buf); // FIXME - ??? what is originUUID??
  soup_value_hash_insert(hash,"originUUID",G_TYPE_STRING,buf);
  

  msg = soup_xmlrpc_request_new(grid->grid_server, "simulator_login",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    CAJ_ERROR("Could not create xmlrpc login request\n");
    exit(1);
  }


  caj_queue_soup_message(sgrp, msg,
			 got_grid_login_response_v1, sim);
  
  // TODO
}

// FIXME - merge this to where it's actually used?
static bool check_grid_success_response(grid_glue_ctx *grid, SoupMessage *msg) {
  os_robust_xml *rxml, *node;
  
  if(msg->status_code != 200) return false;

  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    CAJ_ERROR("ERROR: couldn't parse gridserver XML response\n");
    return false;
  }

  bool retval = false;

  node = os_robust_xml_lookup(rxml, "Result");
  if(node == NULL || node->node_type != OS_ROBUST_XML_STR) {
    CAJ_ERROR("ERROR: bad gridserver XML response, no Result\n");
    goto out;
  }
  if(strcasecmp(node->u.s, "Success") == 0) {
    retval = TRUE;
  } else {
    retval = FALSE;
    node = os_robust_xml_lookup(rxml, "Message");
    if(node != NULL && node->node_type == OS_ROBUST_XML_STR) {
      CAJ_ERROR("ERROR from grid: %s\n", node->u.s);
    }
  }

 out:
  os_robust_xml_free(rxml); return retval;
}



static void got_grid_login_response_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)user_data;
  GRID_PRIV_DEF(sim);
  if(msg->status_code != 200) {
    CAJ_ERROR("Grid login failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    exit(1);
  }
  CAJ_DEBUG("DEBUG: grid login response ~%s~\n", msg->response_body->data);
  if(!check_grid_success_response(grid, msg)) {
    CAJ_ERROR("ERROR: grid denied registration\n"); exit(1);
  }
}

static void do_grid_login_v2(struct simgroup_ctx *sgrp,
			     struct simulator_ctx* sim) {
  char grid_uri[256]; char *req_body;
  char region_id[40], map_texture[40], zero_uuid_str[40];
  char loc_x[20], loc_y[20], region_uri[256];
  char http_port[10], udp_port[10]; 
  uuid_t zero_uuid, u;
  uuid_clear(zero_uuid);
  // GError *error = NULL;
  SoupMessage *msg;
  char *ip_addr = sim_get_ip_addr(sim);
  GRID_PRIV_DEF(sim);

  uuid_clear(zero_uuid);
  uuid_unparse(zero_uuid, zero_uuid_str);

  CAJ_INFO("Logging into grid...\n");
  sim_get_region_uuid(sim, u);
  uuid_unparse(u, region_id);
  snprintf(loc_x, 20, "%u", (unsigned)sim_get_region_x(sim)*256);
  snprintf(loc_y, 20, "%u", (unsigned)sim_get_region_y(sim)*256);
  sprintf(http_port,"%i",(int)sim_get_http_port(sim));
  sprintf(udp_port,"%i",(int)sim_get_udp_port(sim));
  uuid_unparse(zero_uuid, map_texture);

  // no, this isn't a mistake, we have to put the *UDP* port here and not the 
  // HTTP one. Otherwise, stuff breaks. *glares at OpenSim*
  snprintf(region_uri,256,"http://%s:%i",ip_addr, (int)sim_get_udp_port(sim));

  // FIXME - don't use fixed-size buffer 
  snprintf(grid_uri,256, "%sgrid", grid->grid_server);
  // I would use soup_message_set_request, but it has a memory leak...
  req_body = soup_form_encode("uuid", region_id,
			      "locX", loc_x, "locY", loc_y,
			      "regionName", sim_get_name(sim),
			      "serverIP", ip_addr,
			      "serverHttpPort", http_port,
			      "serverURI", region_uri,
			      "serverPort", udp_port, // FIXME - is this right?
			      "regionMapTexture", map_texture,
			      "access", "21", // FIXME ???
			      "VERSIONMIN", MIN_V2_PROTO_VERSION,
			      "VERSIONMAX", MAX_V2_PROTO_VERSION,
			      "SCOPEID", zero_uuid_str, // FIXME - what the hell?
			      "METHOD", "register",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, grid_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_queue_soup_message(sgrp, msg,
			 got_grid_login_response_v2, sim);
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
static void expect_user_set_appearance(grid_glue_ctx *grid, user_ctx *user,
				       GHashTable *hash) {
  GByteArray *data; char *s;
  if(soup_value_hash_lookup(hash,"visual_params",SOUP_TYPE_BYTE_ARRAY,
			    &data)) {
    caj_string str;
    caj_string_set_bin(&str,data->data,data->len);
    user_set_visual_params(user, &str);
  } else {
    CAJ_WARN("WARNING: expect_user is missing visual_params\n");
  }  

  if(soup_value_hash_lookup(hash,"texture",SOUP_TYPE_BYTE_ARRAY,
			    &data)) {
    caj_string str;
    caj_string_set_bin(&str,data->data,data->len);
    user_set_texture_entry(user, &str);
  } else {
    CAJ_WARN("WARNING: expect_user is missing texture data\n");
  }  

  if(soup_value_hash_lookup(hash, "serial", G_TYPE_STRING, &s)) {
    user_set_wearable_serial(user, atoi(s)); // FIXME - type correctness
  } else {
    CAJ_WARN("WARNING: expect_user is missing serial\n");    
  }


  for(int i = 0; i < SL_NUM_WEARABLES; i++) {
    char asset_str[24], item_str[24];
    uuid_t item_id, asset_id;
    sprintf(asset_str,"%s_asset",sl_wearable_names[i]);
    sprintf(item_str,"%s_item",sl_wearable_names[i]);
    if(helper_soup_hash_get_uuid(hash, asset_str, asset_id) || 
       helper_soup_hash_get_uuid(hash, item_str, item_id)) {
      CAJ_WARN("Error: couldn't find wearable %s in expect_user\n",
	       sl_wearable_names[i]);
    } else {
      user_set_wearable(user, i, item_id, asset_id);
    }
  }
}

static void xmlrpc_expect_user_2(void* priv, int is_ok) {
  struct expect_user_state *state = (struct expect_user_state*)priv;
  GRID_PRIV_DEF(state->sim);
  GHashTable *args = state->args;
  GHashTable *hash; user_ctx *user;
  struct sim_new_user uinfo;
  //char *first_name, *last_name;
  char *caps_path, *s;
  //uuid_t session_id, agent_id, secure_session_id;
  char seed_cap[50]; int success = 0;
  caj_vector3 start_pos;
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
  if(!soup_value_hash_lookup(args, "startpos_x",G_TYPE_STRING,
			     &s)) goto bad_args;
  start_pos.x = atof(s);
  if(!soup_value_hash_lookup(args, "startpos_y",G_TYPE_STRING,
			     &s)) goto bad_args;
  start_pos.y = atof(s);
  if(!soup_value_hash_lookup(args, "startpos_z",G_TYPE_STRING,
			     &s)) goto bad_args;
  start_pos.z = atof(s);
  
  // WTF? Why such an odd path?
  snprintf(seed_cap,50,"%s0000/",caps_path);
  uinfo.seed_cap = seed_cap;
  uinfo.is_child = 0;

  user = sim_prepare_new_user(state->sim, &uinfo);
  if(user == NULL) goto return_fail;
  if(soup_value_hash_lookup(args,"appearance",G_TYPE_HASH_TABLE,
			    &hash)) {
    expect_user_set_appearance(grid, user, hash);
  } else {
    CAJ_WARN("WARNING: expect_user is missing appearance data\n");
  }
  user_set_start_pos(user, &start_pos, &start_pos); // FIXME - look_at?
 
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
  simgroup_ctx *sgrp;
  grid_glue_ctx *grid;
  char *agent_id;
};

static void got_validate_session_resp_v1(SoupSession *session, SoupMessage *msg, 
				   gpointer user_data) {
  GHashTable *hash; char *s;
  int is_ok = 0;
  validate_session_state* vs = (validate_session_state*) user_data;
  grid_glue_ctx* grid = vs->grid;
  CAJ_DEBUG("DEBUG: got check_auth_session response\n");
  caj_shutdown_release(vs->sgrp);
  if(soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    if(soup_value_hash_lookup(hash, "auth_session",
			      G_TYPE_STRING, &s)) {
      is_ok = (strcasecmp(s,"TRUE") == 0);
      CAJ_DEBUG("DEBUG: check_auth_session resp %s (%s)\n",
		is_ok?"TRUE":"FALSE",s);
    } else CAJ_DEBUG("DEBUG: couldn't extract value from check_auth_session response\n");
    g_hash_table_destroy(hash);
  } else CAJ_ERROR("ERROR: couldn't extract check_auth_session response\n");
  vs->callback(vs->priv, is_ok);
  delete vs;
}

void osglue_validate_session_v1(struct simgroup_ctx* sgrp, const char* agent_id,
			     const char *session_id, grid_glue_ctx* grid,
			     validate_session_cb callback, void *priv)  {
  GHashTable *hash;
  SoupMessage *val_msg;
  
  CAJ_INFO("Validating session for %s...\n", agent_id);
  
  hash = soup_value_hash_new();
  soup_value_hash_insert(hash,"session_id",G_TYPE_STRING,session_id);
  soup_value_hash_insert(hash,"avatar_uuid",G_TYPE_STRING,agent_id);
  
  val_msg = soup_xmlrpc_request_new(grid->user_server, "check_auth_session",
				    G_TYPE_HASH_TABLE, hash,
				    G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!val_msg) {
    CAJ_ERROR("Could not create check_auth_session request\n");
    callback(priv, FALSE);
    return;
  }

  validate_session_state *vs_state = new validate_session_state();
  vs_state->callback = callback;
  vs_state->priv = priv;
  vs_state->sgrp = sgrp;
  vs_state->agent_id = NULL;
  vs_state->grid = grid;

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, val_msg,
			 got_validate_session_resp_v1, vs_state);
  				   
}

static void got_validate_session_resp_v2(SoupSession *session, SoupMessage *msg, 
				   gpointer user_data) {
  int is_ok = 0; int correct = 0;
  validate_session_state* vs = (validate_session_state*) user_data;
  grid_glue_ctx* grid = vs->grid;
  CAJ_DEBUG("DEBUG: got check_auth_session response from presence server\n");
  caj_shutdown_release(vs->sgrp);

  if(msg->status_code != 200) {
    CAJ_ERROR("Presence request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    is_ok = FALSE;
  } else {
    os_robust_xml *rxml, *resnode, *node;

    CAJ_DEBUG("DEBUG: response ~%s~\n", msg->response_body->data);

    rxml = os_robust_xml_deserialise(msg->response_body->data,
				     msg->response_body->length);
    if(rxml == NULL) {
      CAJ_ERROR("ERROR: couldn't parse presence server XML response\n");
      goto out;
    }

    /* OK. This is the fun bit. If the session exists, we get a response like:

       <?xml version="1.0"?><ServerResponse><result type="List"><UserID>2a8353e3-0fb0-4117-b49f-9daad6f777c0</UserID><RegionID>01bd2887-e158-443d-ab1a-08ee45053440</RegionID><online>True</online><login>3/29/2010 10:49:26 PM</login><logout>1/1/1970 12:00:00 AM</logout><position>&lt;119.3307, 127.6954, 25.45109&gt;</position><lookAt>&lt;-0.9945475, 0.1042844, 0&gt;</lookAt><HomeRegionID>01bd2887-e158-443d-ab1a-08ee45053440</HomeRegionID><HomePosition>&lt;139.3684, 130.0514, 23.69808&gt;</HomePosition><HomeLookAt>&lt;0.9991013, 0.04238611, 0&gt;</HomeLookAt></result></ServerResponse>

       However, if it doesn't, we instead get something like

       <?xml version="1.0"?><ServerResponse><result>null</result></ServerResponse>

       This is a pain in the backside to parse with every XML library known to man or woman. So we cheat instead.
    */

    resnode = os_robust_xml_lookup(rxml, "result");

    if(resnode == NULL) {
      CAJ_ERROR("ERROR: bad response from presence server\n");
      goto out2;
    }

    if(resnode->node_type != OS_ROBUST_XML_LIST) {
      CAJ_DEBUG("DEBUG: validate session: session does not exist\n");
      is_ok = FALSE; goto out2;
    }

    node = os_robust_xml_lookup(resnode, "online");
    if(node == NULL || node->node_type != OS_ROBUST_XML_STR) {
      // this is normal now. Sigh.
      CAJ_DEBUG("DEBUG: response from presence server has no <online>\n");
    } else if(strcasecmp(node->u.s, "True") != 0) {
      CAJ_DEBUG("DEBUG: session invalid, user offline\n");
      is_ok = FALSE; goto out2;
    }

    node = os_robust_xml_lookup(resnode, "UserID");
    if(node == NULL || node->node_type != OS_ROBUST_XML_STR) {
      CAJ_DEBUG("ERROR: bad response from presence server (no <UserID>)\n");
      goto out2;
    } else if(strcasecmp(node->u.s, vs->agent_id) != 0) {
      CAJ_DEBUG("DEBUG: session invalid, agent ID mismatch\n");
      is_ok = FALSE; goto out2;
    }

    CAJ_DEBUG("DEBUG: session validated OK\n");
    is_ok = TRUE;

    // FIXME - TODO    
  
  out2:
    os_robust_xml_free(rxml);
  }

 out:
  CAJ_INFO("DEBUG: session validation conclusion: %s\n", is_ok?"OK":"FAILED");
  vs->callback(vs->priv, is_ok);
  free(vs->agent_id);
  delete vs;
}

void osglue_validate_session_v2(struct simgroup_ctx* sgrp, const char* agent_id,
			     const char *session_id, grid_glue_ctx* grid,
			     validate_session_cb callback, void *priv)  {
  char user_uri[256]; char *req_body;
  SoupMessage *msg;

  // FIXME - don't use fixed-size buffer 
  snprintf(user_uri,256, "%spresence", grid->presence_server);

  req_body = soup_form_encode("SessionID", session_id,
			      "VERSIONMIN", MIN_V2_PROTO_VERSION,
			      "VERSIONMAX", MAX_V2_PROTO_VERSION,
			      "METHOD", "getagent",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, user_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  validate_session_state *vs_state = new validate_session_state();
  vs_state->callback = callback;
  vs_state->priv = priv;
  vs_state->sgrp = sgrp;
  vs_state->agent_id = strdup(agent_id);
  vs_state->grid = grid;

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_validate_session_resp_v2, vs_state);
}

void osglue_validate_session(struct simgroup_ctx* sgrp, const char* agent_id,
			     const char *session_id, grid_glue_ctx* grid,
				validate_session_cb callback, void *priv) {
  if(grid->new_userserver)
    osglue_validate_session_v2(sgrp, agent_id, session_id, grid, callback, priv);
  else
    osglue_validate_session_v1(sgrp, agent_id, session_id, grid, callback, priv);
}

static void xmlrpc_expect_user(SoupServer *server,
			       SoupMessage *msg,
			       GValueArray *params,
			       struct simgroup_ctx* sgrp) {
  GRID_PRIV_DEF_SGRP(sgrp);
  GHashTable *args = NULL;
  char *agent_id, *session_id, *s;
  uint64_t region_handle;
  GHashTable *hash;
  simulator_ctx *sim;
  if(params->n_values != 1 || 
     !soup_value_array_get_nth (params, 0, G_TYPE_HASH_TABLE, &args)) 
    goto bad_args;
  CAJ_INFO("DEBUG: Got an expect_user call\n");
  if(grid->user_server == NULL) goto return_fail; // not ready yet
  if(!soup_value_hash_lookup(args,"agent_id",G_TYPE_STRING,
			     &agent_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"session_id",G_TYPE_STRING,
			     &session_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"regionhandle",G_TYPE_STRING,
			     &s)) goto bad_args;
  region_handle = atoll(s);
  if(region_handle == 0) goto bad_args;

  sim = caj_local_sim_by_region_handle(sgrp, region_handle);
  if(sim == NULL) {
    CAJ_WARN("Got expect_user for wrong region\n");
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
    osglue_validate_session(sgrp, agent_id, session_id, grid,
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
			       struct simgroup_ctx* sgrp) {
  GRID_PRIV_DEF_SGRP(sgrp);
  GHashTable *args = NULL;
  int secret_ok = 1;
  uuid_t agent_id, region_secret;
  char *s;
  uint64_t region_handle;
  GHashTable *hash;
  struct simulator_ctx *sim;
  // GError *error = NULL;
  CAJ_INFO("DEBUG: Got a logoff_user call\n");
  if(params->n_values != 1 || 
     !soup_value_array_get_nth (params, 0, G_TYPE_HASH_TABLE, &args)) 
    goto bad_args;

  if(!soup_value_hash_lookup(args,"regionhandle",G_TYPE_STRING,
			     &s)) goto bad_args;
  region_handle = atoll(s);
  if(region_handle == 0) goto bad_args;
  sim = caj_local_sim_by_region_handle(sgrp, region_handle);
  if(sim == NULL) {
    CAJ_WARN("Got logoff_user for wrong region\n");
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

  CAJ_DEBUG("DEBUG: logoff_user call is valid, walking the user tree\n");

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

static void xmlrpc_instant_message(SoupServer *server,
			       SoupMessage *msg,
			       GValueArray *params,
			       struct simgroup_ctx* sgrp) {
  GRID_PRIV_DEF_SGRP(sgrp);
  GHashTable *args = NULL; GHashTable *hash; int success = false;
  char *s; int i; guchar *us; gsize sz; caj_instant_message im;
  im.bucket.data = NULL;
  if(params->n_values != 1 || 
     !soup_value_array_get_nth (params, 0, G_TYPE_HASH_TABLE, &args)) 
    goto bad_args;

  // region_handle isn't used anymore.
  /* if(!soup_value_hash_lookup(args,"region_handle",G_TYPE_INT,
     &i)) goto bad_args; */

  if(!soup_value_hash_lookup(args,"im_session_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  // FIXME - where does im_session_id go?
  if(uuid_parse(s, im.id)) goto bad_args;

  if(!soup_value_hash_lookup(args,"position_x",G_TYPE_STRING,
			     &s)) goto bad_args;
  im.position.x = atof(s);
  if(!soup_value_hash_lookup(args,"position_y",G_TYPE_STRING,
			     &s)) goto bad_args;
  im.position.y = atof(s);
  if(!soup_value_hash_lookup(args,"position_z",G_TYPE_STRING,
			     &s)) goto bad_args;
  im.position.z = atof(s);

  if(!soup_value_hash_lookup(args,"to_agent_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  if(uuid_parse(s, im.to_agent_id)) goto bad_args;
  if(!soup_value_hash_lookup(args,"from_agent_session",G_TYPE_STRING,
			     &s)) goto bad_args;
  // from_agent_session is ignored.

  if(!soup_value_hash_lookup(args,"offline",G_TYPE_STRING,
			     &s)) goto bad_args;
  us = g_base64_decode(s, &sz); // offline is base64 encoded
  if(sz != 1) {
    g_free(us); goto bad_args;
  }
  im.offline = us[0]; g_free(us);

  if(!soup_value_hash_lookup(args,"message",G_TYPE_STRING,
			     &s)) goto bad_args;
  im.message = s;

  if(!soup_value_hash_lookup(args,"binary_bucket",G_TYPE_STRING,
			     &s)) goto bad_args;
  // binary_bucket is base64 encoded
  im.bucket.data = g_base64_decode(s, &sz); im.bucket.len = sz;

  if(!soup_value_hash_lookup(args,"dialog",G_TYPE_STRING,
			     &s)) goto bad_args;
  // for some odd reason, dialog is base64 encoded. Mutter mutter.
  us = g_base64_decode(s, &sz);
  if(sz != 1) {
    g_free(us); goto bad_args;
  }
  im.im_type = us[0]; g_free(us);

  if(!soup_value_hash_lookup(args,"from_agent_name",G_TYPE_STRING,
			     &s)) goto bad_args;
  im.from_agent_name = s;
  if(!soup_value_hash_lookup(args,"from_agent_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  if(uuid_parse(s, im.from_agent_id)) goto bad_args;

  if(!soup_value_hash_lookup(args,"region_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  // region_id is filled out - it's the UUID of the source region
  if(uuid_parse(s, im.region_id)) goto bad_args;

  if(!soup_value_hash_lookup(args,"timestamp",G_TYPE_STRING,
			     &s)) goto bad_args;
  im.timestamp = (unsigned long)atol(s); // Unix format
  if(!soup_value_hash_lookup(args,"parent_estate_id",G_TYPE_STRING,
			     &s)) goto bad_args;
  // parent_estate_id is 1 in the case of the message I saw
  im.parent_estate_id = (unsigned long)atol(s);

  if(!soup_value_hash_lookup(args,"from_group",G_TYPE_STRING,
			     &s)) goto bad_args;
  if(strcasecmp(s, "TRUE") == 0)
    im.from_group = TRUE;
  else if(strcasecmp(s, "FALSE") == 0)
    im.from_group = FALSE;
  else goto bad_args;

  success = cajeput_incoming_im(sgrp, &im);

  hash = soup_value_hash_new();
  soup_value_hash_insert(hash, "success", G_TYPE_STRING, 
			 success?"TRUE":"FALSE");
  soup_xmlrpc_set_response(msg, G_TYPE_HASH_TABLE, hash);
  g_hash_table_destroy(hash);

  g_free(im.bucket.data);
  g_value_array_free(params);
  return;
  

 bad_args:
  g_free(im.bucket.data);
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
  struct simgroup_ctx* sgrp = (struct simgroup_ctx*) user_data;
  GRID_PRIV_DEF_SGRP(sgrp);
  char *method_name;
  GValueArray *params;

  if(strcmp(path,"/") != 0) {
    CAJ_INFO("DEBUG: request for unhandled path %s\n",
	   path);
    if (msg->method == SOUP_METHOD_POST) {
      CAJ_INFO("DEBUG: POST data is ~%s~\n",
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
    CAJ_WARN("Couldn't parse XMLRPC method call\n");
    CAJ_INFO("DEBUG: ~%s~\n", msg->request_body->data);
    soup_message_set_status(msg,500);
    return;
  }

  if(strcmp(method_name, "logoff_user") == 0) {
    xmlrpc_logoff_user(server, msg, params, sgrp);
    
  } else if(strcmp(method_name, "expect_user") == 0) {
    CAJ_DEBUG("DEBUG: expect_user ~%s~\n", msg->request_body->data);
    xmlrpc_expect_user(server, msg, params, sgrp);
  } else if(strcmp(method_name, "grid_instant_message") == 0) {
    CAJ_DEBUG("DEBUG: grid_instant_message ~%s~\n", msg->request_body->data);
    xmlrpc_instant_message(server, msg, params, sgrp);
  } else {
    CAJ_INFO("DEBUG: unknown xmlrpc method %s called\n", method_name);
    g_value_array_free(params);
    soup_xmlrpc_set_fault(msg, SOUP_XMLRPC_FAULT_SERVER_ERROR_REQUESTED_METHOD_NOT_FOUND,
			  "Method %s not found", method_name);
  }
  g_free(method_name);
}

static void got_user_logoff_resp_v1(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)user_data;
  GRID_PRIV_DEF(sim);
  GHashTable *hash = NULL;
  sim_shutdown_release(sim);
  if(msg->status_code != 200) {
    CAJ_WARN("User logoff failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    return;
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    CAJ_WARN("User logoff failed: couldn't parse response\n");
    return;    
  }

  CAJ_INFO("User logoff completed\n");
  g_hash_table_destroy(hash);
}


static void user_logoff_v1(struct simgroup_ctx *sgrp, struct simulator_ctx* sim,
			   const uuid_t user_id, const uuid_t session_id,
			   const caj_vector3 *pos, const caj_vector3 *look_at) {
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
  sprintf(buf,"%llu",(long long unsigned)sim_get_region_handle(sim));
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
 

  msg = soup_xmlrpc_request_new(grid->user_server, "logout_of_simulator",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    CAJ_ERROR("Could not create xmlrpc logout request\n");
    return;
  }

  caj_queue_soup_message(sgrp, msg,
			 got_user_logoff_resp_v1, sim);
  sim_shutdown_hold(sim); // FIXME
}


static void got_user_logoff_resp_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)user_data;
  // GRID_PRIV_DEF(sim);
  sim_shutdown_release(sim);

  // FIXME - TODO!!!
}

// FIXME - need to do presence updates on sim entry too!

static void user_logoff_v2(struct simgroup_ctx *sgrp,
			   struct simulator_ctx* sim,
			   const uuid_t user_id, const uuid_t session_id,
			   const caj_vector3 *pos, const caj_vector3 *look_at) {
  char uri[256]; char *req_body;
  char session_id_str[40], user_id_str[40], region_id_str[40];
  char pos_str[60], look_at_str[60];
  char zero_uuid_str[40];
  uuid_t zero_uuid, u;
  // GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF(sim);

  uuid_clear(zero_uuid);
  uuid_unparse(zero_uuid, zero_uuid_str);

  uuid_unparse(user_id, user_id_str);
  uuid_unparse(session_id, session_id_str);

  snprintf(pos_str, 60, "<%f, %f, %f>", pos->x, pos->y, pos->z);
  snprintf(look_at_str, 60, "<%f, %f, %f>", 
	   look_at->x, look_at->y, look_at->z);

  // FIXME - don't use fixed-size buffer 
  snprintf(uri,256, "%spresence", grid->presence_server);
  // I would use soup_message_set_request, but it has a memory leak...
  req_body = soup_form_encode("VERSIONMIN", MIN_V2_PROTO_VERSION,
			      "VERSIONMAX", MAX_V2_PROTO_VERSION,
			      "METHOD", "logout",
			      "SessionID", session_id_str,
			      "Position", pos_str,
			      "LookAt", look_at_str,
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_queue_soup_message(sgrp, msg,
			 got_user_logoff_resp_v2, sim);
  sim_shutdown_hold(sim); // FIXME

  // This second bit is needed by newer Robust versions
  sim_get_region_uuid(sim, u);
  uuid_unparse(u, region_id_str);

  // FIXME - don't use fixed-size buffer 
  snprintf(uri,256, "%sgriduser", grid->grid_user_server);
  // I would use soup_message_set_request, but it has a memory leak...
  req_body = soup_form_encode("VERSIONMIN", MIN_V2_PROTO_VERSION,
			      "VERSIONMAX", MAX_V2_PROTO_VERSION,
			      "METHOD", "loggedout",
			      "UserID", user_id_str,
			      "RegionID", region_id_str,
			      "Position", pos_str,
			      "LookAt", look_at_str,
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_queue_soup_message(sgrp, msg,
			 got_user_logoff_resp_v2, sim);
  sim_shutdown_hold(sim); // FIXME

}

static void user_created(struct simgroup_ctx *sgrp,
			 struct simulator_ctx* sim,
			 struct user_ctx* user,
			 void **user_priv) {
  // GRID_PRIV_DEF(sim);
  user_grid_glue *user_glue = new user_grid_glue();
  user_glue->ctx = user;
  user_glue->refcnt = 1;
  user_glue->enter_callback_uri = NULL;
  *user_priv = user_glue;
}

static void user_deleted(struct simgroup_ctx *sgrp,
			 struct simulator_ctx* sim,
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
  GRID_PRIV_DEF(sim);
  sim_shutdown_release(sim);
  // FIXME - should probably pay *some* attention to the response
  if(msg->status_code != 200) {
    CAJ_WARN("User entry callback failed: got %i %s\n",
	     (int)msg->status_code,msg->reason_phrase);
  }

}


static void got_presence_upd_resp_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)user_data;
  // GRID_PRIV_DEF(sim);
  sim_shutdown_release(sim);

  // FIXME - TODO!!!
}

static void user_update_presence_v2(struct simgroup_ctx *sgrp,
				    simulator_ctx *sim, user_ctx *user) {
  char presence_uri[256]; char *req_body;
  char session_id_str[40], region_id_str[40];
  char pos_str[60], look_at_str[60];
  uuid_t u; caj_vector3 pos;
  // GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF(sim);

  user_get_session_id(user, u);
  uuid_unparse(u, session_id_str);

  sim_get_region_uuid(sim, u);
  uuid_unparse(u, region_id_str);

  user_get_position(user, &pos);
  snprintf(pos_str, 60, "<%f, %f, %f>", pos.x, pos.y, pos.z);
  snprintf(look_at_str, 60, "<%f, %f, %f>", 
	   0.0, 0.0, 0.0); // FIXME!

  // FIXME - don't use fixed-size buffer 
  snprintf(presence_uri,256, "%spresence", grid->presence_server);
  // I would use soup_message_set_request, but it has a memory leak...
  req_body = soup_form_encode("VERSIONMIN", MIN_V2_PROTO_VERSION,
			      "VERSIONMAX", MAX_V2_PROTO_VERSION,
			      "METHOD", "report",
			      "SessionID", session_id_str,
			      "RegionID", region_id_str,
			      "Position", pos_str,
			      "LookAt", look_at_str,
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, presence_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_queue_soup_message(sgrp, msg,
			 got_presence_upd_resp_v2, sim);
  sim_shutdown_hold(sim); // FIXME
}

static void user_entered(struct simgroup_ctx *sgrp,
			 simulator_ctx *sim, user_ctx *user,
			 void *user_priv) {
  USER_PRIV_DEF(user_priv);
  GRID_PRIV_DEF(sim);

  if(user_glue->enter_callback_uri != NULL) {
    CAJ_DEBUG("DEBUG: calling back to %s on avatar entry\n",
	      user_glue->enter_callback_uri);

    // FIXME - shouldn't we have to, y'know, identify ourselves somehow?

    SoupMessage *msg = 
      soup_message_new ("DELETE", user_glue->enter_callback_uri);
    // FIXME - should we send a body at all?
    soup_message_set_request (msg, "text/plain",
			      SOUP_MEMORY_STATIC, "", 0);
    sim_shutdown_hold(sim);
    caj_queue_soup_message(sgrp, msg,
			   user_entered_callback_resp, sim);
    free(user_glue->enter_callback_uri);
    user_glue->enter_callback_uri = NULL;  
    
  }

  // FIXME - add support for old protocol too? (Less essential.)
  if(grid->new_userserver) {
    user_update_presence_v2(sgrp, sim, user);
  }
}

struct presence_for_im_state {
  simgroup_ctx *sgrp;
  void(*cb)(void *cb_priv, const uuid_t region);
  void *cb_priv;
};

static void got_presence_for_im_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  uuid_t region_id; os_robust_xml *rxml, *node; char *s;
  presence_for_im_state *st = (presence_for_im_state*)user_data;
  GHashTableIter iter;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  caj_shutdown_release(st->sgrp);
  uuid_clear(region_id);

  if(msg->status_code != 200) {
    CAJ_ERROR("ERROR: presence query failed: got %i %s\n", 
	      (int)msg->status_code,msg->reason_phrase);
    goto out;
  }

  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    CAJ_ERROR("ERROR: couldn't parse presence server XML response\n");
    goto out;
  }

  s = os_robust_xml_lookup_str(rxml, "result");
  if(s != NULL && (strcmp(s, "Failure") == 0)) {
    CAJ_WARN("WARNING: failed to find presence for IM\n");
    goto out_xmlfree;
  }

  os_robust_xml_iter_begin(&iter, rxml);
  while(os_robust_xml_iter_next(&iter, NULL, &node)) {
    if(node->node_type != OS_ROBUST_XML_LIST) {
      CAJ_WARN("ERROR: bad presence response, items should be list nodes\n");
      continue;
    }
    char *user_id_str = os_robust_xml_lookup_str(node, "UserID");
    char *region_id_str = os_robust_xml_lookup_str(node, "RegionID");
    uuid_t u;
    if(user_id_str == NULL || region_id_str == NULL || 
       uuid_parse(user_id_str, u)) {
      CAJ_WARN("ERROR: bad presence info in presence response\n");
      continue;
    }
    // FIXME - should check user ID is correct.
    if(uuid_parse(region_id_str, u)) {
      CAJ_WARN("ERROR: bad region ID in presence response\n");
      continue;      
    }
    uuid_copy(region_id, u); break;
  }

 out_xmlfree:
  os_robust_xml_free(rxml);
 out:
  st->cb(st->cb_priv, region_id);
  delete st;
}

static void get_presence_for_im_v2(struct simgroup_ctx *sgrp, uuid_t user_id,
				   void(*cb)(void *cb_priv, const uuid_t region),
				   void *cb_priv) {
  char presence_uri[256]; char *req_body;
  char user_id_str[40];
  // GError *error = NULL;
  SoupMessage *msg; presence_for_im_state *st;
  GRID_PRIV_DEF_SGRP(sgrp);

  uuid_unparse(user_id, user_id_str);

  // FIXME - don't use fixed-size buffer 
  snprintf(presence_uri,256, "%spresence", grid->presence_server);
  // I would use soup_message_set_request, but it has a memory leak...
  req_body = soup_form_encode("VERSIONMIN", MIN_V2_PROTO_VERSION,
			      "VERSIONMAX", MAX_V2_PROTO_VERSION,
			      "METHOD", "getagents",
			      "uuids[]", user_id_str, /* fun... */
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, presence_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  st = new presence_for_im_state();
  st->sgrp = sgrp; st->cb = cb; st->cb_priv = cb_priv;
  caj_queue_soup_message(sgrp, msg,
			 got_presence_for_im_v2, st);
  caj_shutdown_hold(sgrp);  
}

static void get_presence_for_im(struct simgroup_ctx *sgrp, uuid_t user_id,
				void(*cb)(void *cb_priv, const uuid_t region),
				void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  if(grid->new_userserver) {
    get_presence_for_im_v2(sgrp, user_id, cb, cb_priv);
  } else {
    // FIXME - TODO!
    uuid_t u; uuid_clear(u);
    cb(cb_priv, u);
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

struct im_sender_state {
  simgroup_ctx *sgrp;
  struct caj_instant_message *im;
  int tries_remaining;
  uuid_t last_region;
  void(*cb)(void *priv, int success);
  void *cb_priv;
};

static void done_sending_im(struct im_sender_state *st, bool success) {
  caj_shutdown_release(st->sgrp);
  caj_free_instant_message(st->im);
  st->cb(st->cb_priv, success);
  delete st; 
}

static void send_im_via_grid(struct im_sender_state *st);
static void send_im_got_presence(void *priv, const uuid_t region_id);
static void send_im_got_region(void* cb_priv, struct map_block_info* block);
static void send_im_got_response(SoupSession *session, SoupMessage *msg, 
				 gpointer user_data);

// FIXME - the entire IM code needs redesigning to ensure in-order delivery
// and to use proper caching of presence and region info.
static void send_im(simgroup_ctx *sgrp, const struct caj_instant_message *im,
		    void(*cb)(void *priv, int success), void *cb_priv) {
  im_sender_state *st = new im_sender_state();
  st->sgrp = sgrp; caj_shutdown_hold(sgrp);
  st->im = caj_dup_instant_message(im);
  st->tries_remaining = 5; st->cb = cb; st->cb_priv = cb_priv;
  uuid_clear(st->last_region);
  send_im_via_grid(st);
}

static void send_im_via_grid(struct im_sender_state *st) {
  if(st->tries_remaining-- <= 0) {
    printf("WARNING: max tries reached when trying to send IM\n");
    done_sending_im(st, false);
    return;
  }

  get_presence_for_im(st->sgrp, st->im->to_agent_id,
		      send_im_got_presence, st);
}

static void send_im_got_presence(void *priv, const uuid_t region_id) {
  im_sender_state *st = (im_sender_state*)priv;
  if(uuid_is_null(region_id)) {
    printf("WARNING: couldn't send IM - user offline\n");
    done_sending_im(st, false);
    return;    
  } else if(uuid_compare(region_id, st->last_region) == 0) {
    printf("WARNING: couldn't send IM - error sending and no new region\n");
    done_sending_im(st, false);
    return;        
  }
  uuid_copy(st->last_region, region_id);
  caj_map_region_by_uuid(st->sgrp, region_id, send_im_got_region, st);
}

static void send_im_got_region(void* cb_priv, struct map_block_info* block) {
  im_sender_state *st = (im_sender_state*)cb_priv;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  if(block == NULL) {
    printf("WARNING: couldn't find region user is in when sending IM, retrying\n");
    send_im_via_grid(st);
  } else {
    char uri[256]; guchar uc;
    char buf[40]; uuid_t u; gchar *s;
    GHashTable *hash; SoupMessage *msg;
    hash = soup_value_hash_new();    
    soup_value_hash_insert(hash,"region_handle",G_TYPE_INT,0); // unused
    uuid_unparse(st->im->id, buf);
    soup_value_hash_insert(hash,"im_session_id",G_TYPE_STRING,buf);
    snprintf(buf,40,"%f",(double)st->im->position.x);
    soup_value_hash_insert(hash,"position_x",G_TYPE_STRING,buf);
    snprintf(buf,40,"%f",(double)st->im->position.y);
    soup_value_hash_insert(hash,"position_y",G_TYPE_STRING,buf);
    snprintf(buf,40,"%f",(double)st->im->position.z);
    soup_value_hash_insert(hash,"position_z",G_TYPE_STRING,buf);

    uuid_unparse(st->im->to_agent_id, buf);
    soup_value_hash_insert(hash,"to_agent_id",G_TYPE_STRING,buf);
    soup_value_hash_insert(hash,"from_agent_session",G_TYPE_STRING,
			   "00000000-0000-0000-0000-000000000000");
    
    uc = (unsigned)st->im->offline;
    s = g_base64_encode(&uc, 1);
    soup_value_hash_insert(hash,"offline",G_TYPE_STRING,s);
    g_free(s);
    
    soup_value_hash_insert(hash,"message",G_TYPE_STRING,st->im->message);

    s = g_base64_encode(st->im->bucket.data, st->im->bucket.len);
    soup_value_hash_insert(hash,"binary_bucket",G_TYPE_STRING,s);
    g_free(s);

    s = g_base64_encode(&st->im->im_type, 1);
    soup_value_hash_insert(hash,"dialog",G_TYPE_STRING,s);
    g_free(s);

    soup_value_hash_insert(hash,"from_agent_name",G_TYPE_STRING,
			   st->im->from_agent_name);
    uuid_unparse(st->im->from_agent_id, buf);
    soup_value_hash_insert(hash,"from_agent_id",G_TYPE_STRING,buf);
    uuid_unparse(st->im->region_id, buf);
    soup_value_hash_insert(hash,"region_id",G_TYPE_STRING,buf);

    snprintf(buf,40,"%lu",(unsigned long)st->im->timestamp);
    soup_value_hash_insert(hash,"timestamp",G_TYPE_STRING,buf);
    snprintf(buf,40,"%lu",(unsigned long)st->im->parent_estate_id);
    soup_value_hash_insert(hash,"parent_estate_id",G_TYPE_STRING,buf);
    soup_value_hash_insert(hash, "from_group", G_TYPE_STRING,
			   st->im->from_group ? "TRUE" : "FALSE");

    snprintf(uri, 256, "http://%s:%i/", block->sim_ip,
	     block->http_port);

    msg = soup_xmlrpc_request_new(uri, "grid_instant_message",
				  G_TYPE_HASH_TABLE, hash,
				  G_TYPE_INVALID);
    g_hash_table_destroy(hash);
    if (!msg) {
      CAJ_ERROR("Could not create grid_instant_message request\n");
      done_sending_im(st, false);
      return;
    }

    caj_queue_soup_message(st->sgrp, msg, send_im_got_response, st);
  }
}

static void send_im_got_response(SoupSession *session, SoupMessage *msg, 
				 gpointer user_data) {
  im_sender_state *st = (im_sender_state*)user_data;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  GHashTable *hash = NULL; char *s; bool success = false;

  if(msg->status_code != 200) {
    CAJ_WARN("WARNING: sending IM to region failed: %i %s",
	     (int)msg->status_code, msg->reason_phrase);
    send_im_via_grid(st); return;
  }

  if(soup_xmlrpc_extract_method_response(msg->response_body->data,
                                         msg->response_body->length,
                                         NULL,
                                         G_TYPE_HASH_TABLE, &hash)) {
    if(soup_value_hash_lookup(hash, "success",
                              G_TYPE_STRING, &s)) {
      if(strcasecmp(s,"TRUE") == 0)
	success = true;
      else if(strcasecmp(s,"FALSE") == 0)
	success = false;
      else CAJ_WARN("WARNING: bad grid_instant_message response\n");
    } else CAJ_WARN("WARNING: bad grid_instant_message response\n");
    g_hash_table_destroy(hash);
  } else {
    CAJ_WARN("WARNING: couldn't extract grid_instant_message response\n");
  }
  if(success) {
    done_sending_im(st, true);
  } else {
    CAJ_WARN("WARNING: failure sending IM, retrying...\n");
    send_im_via_grid(st); return;
  }
}

struct map_block_state {
  struct simgroup_ctx *sgrp;
  caj_find_regions_cb cb;
  void *cb_priv;

  map_block_state(struct simgroup_ctx *sgrp_, caj_find_regions_cb cb_, void *cb_priv_) : 
    sgrp(sgrp_), cb(cb_), cb_priv(cb_priv_) { };
};

struct map_region_state {
  struct simgroup_ctx *sgrp;
  caj_find_region_cb cb;
  void *cb_priv;

  map_region_state(struct simgroup_ctx *sgrp_, caj_find_region_cb cb_, void *cb_priv_) : 
    sgrp(sgrp_), cb(cb_), cb_priv(cb_priv_) { };
};

static void got_map_block_resp_v1(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct map_block_state *st = (map_block_state*)user_data;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  struct map_block_info *blocks;
  int num_blocks = 0;
  GHashTable *hash = NULL;
  GValueArray *sims = NULL;

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    CAJ_WARN("Map block request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    CAJ_WARN("Map block request failed: couldn't parse response\n");
    goto out_fail;
  }

  //printf("DEBUG: map block response ~%s~\n", msg->response_body->data);
  CAJ_DEBUG("DEBUG: got map block response\n");

  if(!soup_value_hash_lookup(hash,"sim-profiles",G_TYPE_VALUE_ARRAY,&sims)
     || sims == NULL) {
    CAJ_WARN("Map block request failed: no/bad sim-profiles member\n");
    goto out_free_fail;
  }

  blocks = new map_block_info[sims->n_values];
  for(unsigned int i = 0; i < sims->n_values; i++) {
    GHashTable *sim_info = NULL; char *s; int val;
    if(!soup_value_array_get_nth(sims, i, G_TYPE_HASH_TABLE, &sim_info) ||
       sim_info == NULL) {
      CAJ_WARN("Map block request bad: expected hash table\n");
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
    if(uuid_parse(s, blocks[num_blocks].map_image))
      goto bad_block;
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
    if(uuid_parse(s, blocks[num_blocks].region_id))
      goto bad_block;
    
    // FIXME - do something with regionhandle, sim_uri?

    CAJ_DEBUG("DEBUG: map block %i,%i is %s\n",
	   blocks[num_blocks].x, blocks[num_blocks].y, blocks[num_blocks].name);

    num_blocks++;
    
    continue;    
  bad_block:
    CAJ_WARN("WARNING: Map block response has bad block, skipping\n");
  }


  st->cb(st->cb_priv, blocks, num_blocks);

  delete[] blocks;
  g_hash_table_destroy(hash); // must be after calling the callback
  delete st;
  return;

 out_free_fail:
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(st->cb_priv, NULL, 0);
  delete st;
}

static void map_block_request_v1(struct simgroup_ctx *sgrp, int min_x, int max_x, 
				 int min_y, int max_y, 
				 caj_find_regions_cb cb, void *cb_priv) {
  
  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

  CAJ_DEBUG("DEBUG: map block request (%i,%i)-(%i,%i)\n",
	    min_x, min_y, max_x, max_y);

  hash = soup_value_hash_new();
  soup_value_hash_insert(hash,"xmin",G_TYPE_INT,min_x);
  soup_value_hash_insert(hash,"xmax",G_TYPE_INT,max_x);
  soup_value_hash_insert(hash,"ymin",G_TYPE_INT,min_y);
  soup_value_hash_insert(hash,"ymax",G_TYPE_INT,max_y);
  

  msg = soup_xmlrpc_request_new(grid->grid_server, "map_block",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    CAJ_ERROR("Could not create xmlrpc map request\n");
    cb(cb_priv, NULL, 0);
    return;
  }

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_map_block_resp_v1, new map_block_state(sgrp,cb,cb_priv));
}

static bool unpack_v2_region_entry(grid_glue_ctx *grid, os_robust_xml *node,
				   map_block_info *block) {
  os_robust_xml* child;
  block->name = NULL; block->sim_ip = NULL;

  if(node->node_type != OS_ROBUST_XML_LIST)
    return false; // probably a failed lookup

  child = os_robust_xml_lookup(node, "uuid");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR ||
     uuid_parse(child->u.s, block->region_id) != 0) {
    CAJ_WARN("DEBUG: bad/missing uuid in region entry from gridserver\n");
    return false;
  }

  child = os_robust_xml_lookup(node, "locX");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR) {
    CAJ_WARN("DEBUG: missing locX in region entry from gridserver\n");
    return false;
  }
  block->x = atoi(child->u.s)/256;

  child = os_robust_xml_lookup(node, "locY");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR) {
    CAJ_WARN("DEBUG: missing locY in region entry from gridserver\n");
    return false;
  }
  block->y = atoi(child->u.s)/256;

  child = os_robust_xml_lookup(node, "regionName");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR) {
    CAJ_WARN("DEBUG: missing regionName in region entry from gridserver\n");
    return false;
  }
  block->name = child->u.s;

  child = os_robust_xml_lookup(node, "serverIP");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR) {
    CAJ_WARN("DEBUG: missing serverIP in region entry from gridserver\n");
    return false;
  }
  block->sim_ip = child->u.s;

  child = os_robust_xml_lookup(node, "serverHttpPort");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR) {
    CAJ_WARN("DEBUG: missing serverHttpPort in region entry from gridserver\n");
    return false;
  }
  block->http_port = atoi(child->u.s);

  child = os_robust_xml_lookup(node, "serverPort");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR) {
    CAJ_WARN("DEBUG: missing serverPort in region entry from gridserver\n");
    return false;
  }
  block->sim_port = atoi(child->u.s);

  child = os_robust_xml_lookup(node, "regionMapTexture");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR ||
     uuid_parse(child->u.s, block->map_image) != 0) {
    CAJ_WARN("DEBUG: bad/missing regionMapTexture in region entry from gridserver\n");
    return false;
  }
 
  child = os_robust_xml_lookup(node, "access");
  if(child == NULL || child->node_type != OS_ROBUST_XML_STR) {
    CAJ_WARN("DEBUG: missing access in region entry from gridserver\n");
    return false;
  }
  block->access = atoi(child->u.s);

  CAJ_INFO("DEBUG: processed map block data\n");
  // not sent, legacy info
  block->water_height = 20; block->num_agents = 0;
  block->flags = 0; // should this be sent?
  return true;
}

static void got_map_multi_resp_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  os_robust_xml *rxml, *node;
  GHashTableIter iter;
  struct map_block_state *st = (map_block_state*)user_data;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  struct map_block_info *blocks;
  size_t num_blocks = 0, alloc_blocks = 4;

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    CAJ_WARN("Map block request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }

  CAJ_DEBUG("DEBUG: got map block response: ~%s~\n", msg->response_body->data);
  
  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    CAJ_ERROR("ERROR: couldn't parse gridserver XML response\n");
    goto out_fail;
  }

  blocks = (map_block_info*)calloc(alloc_blocks, sizeof(map_block_info));
  os_robust_xml_iter_begin(&iter, rxml);
  while(os_robust_xml_iter_next(&iter, NULL, &node)) {

    CAJ_DEBUG("DEBUG: processing map block data\n");

    if(num_blocks >= alloc_blocks) {
      size_t new_alloc_blocks = alloc_blocks * 2;
      size_t new_size = new_alloc_blocks*sizeof(map_block_info);
      if(new_size / sizeof(map_block_info) < new_alloc_blocks) continue;
      void* new_blocks = realloc(blocks, new_size);
      if(new_blocks == NULL) continue;
      alloc_blocks = new_alloc_blocks; blocks = (map_block_info*)new_blocks;
    }

    CAJ_DEBUG("DEBUG: still processing map block data\n");

    map_block_info *block = &blocks[num_blocks]; 
    if(unpack_v2_region_entry(grid, node, block)) num_blocks++;
     
  }

  CAJ_DEBUG("DEBUG: got %i map blocks\n", (int)num_blocks);
  st->cb(st->cb_priv, blocks, num_blocks);

  os_robust_xml_free(rxml);
  free(blocks); delete st; 
  return;

 out_xmlfree_fail:
  os_robust_xml_free(rxml);
 out_fail:
  st->cb(st->cb_priv, NULL, 0);
  delete st;
}

static void map_block_request_v2(struct simgroup_ctx *sgrp, int min_x, int max_x, 
			      int min_y, int max_y,
				 caj_find_regions_cb cb, void *cb_priv) {
  char grid_uri[256]; char *req_body;
  char xmin_s[20], xmax_s[20], ymin_s[20], ymax_s[20];
  uuid_t zero_uuid; char zero_uuid_str[40];
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

  uuid_clear(zero_uuid); uuid_unparse(zero_uuid, zero_uuid_str);

  CAJ_DEBUG("DEBUG: map block request (%i,%i)-(%i,%i)\n",
	    min_x, min_y, max_x, max_y);

  snprintf(xmin_s, 20, "%i", min_x*256);
  snprintf(xmax_s, 20, "%i", max_x*256);
  snprintf(ymin_s, 20, "%i", min_y*256);
  snprintf(ymax_s, 20, "%i", max_y*256);

  snprintf(grid_uri,256, "%sgrid", grid->grid_server);
  req_body = soup_form_encode(SOUP_METHOD_POST, grid_uri,
			      "SCOPEID", zero_uuid_str,
			      "XMIN", xmin_s, "XMAX", xmax_s,
			      "YMIN", ymin_s, "YMAX", ymax_s,
			      "METHOD", "get_region_range",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, grid_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_map_multi_resp_v2, new map_block_state(sgrp,cb,cb_priv));
}




static void got_region_info_v1(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct map_region_state *st = (map_region_state*)user_data;
  struct map_block_info info;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  GHashTable *hash = NULL;
  char *s; //uuid_t u;

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    CAJ_WARN("Region info request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    CAJ_ERROR("Region info request failed: couldn't parse response\n");
    goto out_fail;
  }

  CAJ_DEBUG("DEBUG: region info response ~%s~\n", msg->response_body->data);
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
  if(uuid_parse(s, info.region_id))
    goto bad_block;

  // NOTE! This doesn't fill out a bunch of stuff that it should, because the
  // response doesn't contain the required information!

  // FIXME - use regionHandle, server_uri!
 
  st->cb(st->cb_priv, &info);
  g_hash_table_destroy(hash);
  delete st;
  return;

 bad_block:
  CAJ_ERROR("ERROR: couldn't lookup expected values in region info reply\n");
  //out_free_fail:
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(st->cb_priv, NULL);
  delete st;
}

// WARNING: while superficially, this looks like a map block request-type
// function, it does NOT return enough information for such a purpose, and is
// for INTERNAL USE ONLY.
static void req_region_info_v1(struct simgroup_ctx* sgrp, uint64_t handle,
			       caj_find_region_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  char buf[40];

  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;

  hash = soup_value_hash_new();
  sprintf(buf, "%llu", (long long unsigned)handle);
  soup_value_hash_insert(hash,"region_handle",G_TYPE_STRING,buf);
  soup_value_hash_insert(hash,"authkey",G_TYPE_STRING,grid->grid_sendkey);

  msg = soup_xmlrpc_request_new(grid->grid_server, "simulator_data_request",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    CAJ_ERROR("Could not create region_info request\n");
    cb(cb_priv, NULL);
    return;
  }

  
  map_region_state *st = new map_region_state(sgrp, cb, cb_priv);

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_region_info_v1, st);

}

static void got_map_single_resp_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  os_robust_xml *rxml, *node;
  struct map_region_state *st = (map_region_state*)user_data;
  struct map_block_info info;
  GRID_PRIV_DEF_SGRP(st->sgrp);

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    CAJ_WARN("Region info request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }

  // FIXME - TODO
  CAJ_DEBUG("DEBUG: got region info response ~%s~\n", msg->response_body->data);

  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    CAJ_ERROR("ERROR: couldn't parse gridserver XML response\n");
    goto out_fail;
  }

  
  node = os_robust_xml_lookup(rxml, "result");
  if(node == NULL || node->node_type != OS_ROBUST_XML_LIST) {
    CAJ_ERROR("ERROR: bad get_region_by_position XML response\n");
    goto out_xmlfree_fail;
  }

  if(unpack_v2_region_entry(grid, node, &info)) {
    st->cb(st->cb_priv, &info);
    os_robust_xml_free(rxml); delete st; return;
  }

 out_xmlfree_fail:
  os_robust_xml_free(rxml);
 out_fail:
  st->cb(st->cb_priv, NULL);
  delete st;
}

static void req_region_info_v2(struct simgroup_ctx* sgrp, uint64_t handle,
			       caj_find_region_cb cb, void *cb_priv) {
  char grid_uri[256]; char *req_body;
  char x_s[20], y_s[20];
  uuid_t zero_uuid; char zero_uuid_str[40];
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

  uuid_clear(zero_uuid); uuid_unparse(zero_uuid, zero_uuid_str);

  snprintf(x_s, 20, "%i", (unsigned)(handle>>32));
  snprintf(y_s, 20, "%i", (unsigned)(handle & 0xffffffff));

  CAJ_INFO("DEBUG: region info request (%s,%s)\n",
	 x_s, y_s);

  snprintf(grid_uri,256, "%sgrid", grid->grid_server);
  req_body = soup_form_encode(SOUP_METHOD_POST, grid_uri,
			      "SCOPEID", zero_uuid_str,
			      "X", x_s, "Y", y_s,
			      "METHOD", "get_region_by_position",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, grid_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  map_region_state *st = new map_region_state(sgrp, cb, cb_priv);

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg, got_map_single_resp_v2, st);
  
}

static void req_region_info(struct simulator_ctx* sim, uint64_t handle,
			    caj_find_region_cb cb, void *cb_priv) {
  simgroup_ctx* sgrp = sim_get_simgroup(sim);
  GRID_PRIV_DEF(sim);
  if(grid->old_xmlrpc_grid_proto)
    req_region_info_v1(sgrp, handle, cb, cb_priv);
  else req_region_info_v2(sgrp, handle, cb, cb_priv);
}

// FIXME - consolidate this?
struct regions_by_name_state {
  simgroup_ctx* sgrp;
  caj_find_regions_cb cb;
  void *cb_priv;
};

static void got_regions_by_name_v1(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct regions_by_name_state *st = (regions_by_name_state*)user_data;
  struct map_block_info *info; int count;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  GHashTable *hash = NULL;
  char *s; //uuid_t u;

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    CAJ_WARN("Region info request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }
  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    CAJ_ERROR("Region info request failed: couldn't parse response\n");
    goto out_fail;
  }

  CAJ_DEBUG("DEBUG: region by name response ~%s~\n", msg->response_body->data);

  if(!soup_value_hash_lookup(hash, "numFound", G_TYPE_INT, &count))
    goto bad_resp;
  CAJ_DEBUG("DEBUG: region by name lookup returned %i items\n", count);
  
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
  CAJ_ERROR("ERROR: couldn't lookup expected values in region by name reply\n");
  //out_free_fail:
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(st->cb_priv, NULL, 0);
  delete st;
}

static void map_name_request_v1(struct simgroup_ctx* sgrp, const char* name,
				caj_find_regions_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);

  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;

  hash = soup_value_hash_new();
  soup_value_hash_insert(hash,"name",G_TYPE_STRING,name);
  soup_value_hash_insert(hash,"maxNumber",G_TYPE_STRING,"20"); // FIXME

  msg = soup_xmlrpc_request_new(grid->grid_server, "search_for_region_by_name",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create search_for_region_by_name request\n");
    cb(cb_priv, NULL, 0);
    return;
  }

  regions_by_name_state *st = new regions_by_name_state();
  st->sgrp = sgrp; st->cb = cb; st->cb_priv = cb_priv;

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_regions_by_name_v1, st);

}

static void map_name_request_v2(struct simgroup_ctx* sgrp, const char* name,
				caj_find_regions_cb cb, void *cb_priv) {

  char grid_uri[256]; char *req_body;
  char max_results[20];
  uuid_t zero_uuid; char zero_uuid_str[40];
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

  uuid_clear(zero_uuid); uuid_unparse(zero_uuid, zero_uuid_str);

  CAJ_DEBUG("DEBUG: map name request %s\n", name);

  snprintf(max_results, 20, "%i", 20); // FIXME!

  snprintf(grid_uri,256, "%sgrid", grid->grid_server);
  req_body = soup_form_encode(SOUP_METHOD_POST, grid_uri,
			      "SCOPEID", zero_uuid_str,
			      "NAME", name, "MAX", max_results,
			      "METHOD", "get_regions_by_name",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, grid_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_map_multi_resp_v2, new map_block_state(sgrp,cb,cb_priv));
}

void map_region_by_name_v1(struct simgroup_ctx* sgrp, const char* name,
			   caj_find_region_cb cb, void *cb_priv) {
  cb(cb_priv, NULL); // FIXME - TODO!!!
}

void map_region_by_name_v2(struct simgroup_ctx* sgrp, const char* name,
			   caj_find_region_cb cb, void *cb_priv) {
  char grid_uri[256]; char *req_body;
  char max_results[20];
  uuid_t zero_uuid; char zero_uuid_str[40];
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

  uuid_clear(zero_uuid); uuid_unparse(zero_uuid, zero_uuid_str);

  CAJ_DEBUG("DEBUG: map name request %s\n", name);

  snprintf(max_results, 20, "%i", 20); // FIXME!

  snprintf(grid_uri,256, "%sgrid", grid->grid_server);
  req_body = soup_form_encode(SOUP_METHOD_POST, grid_uri,
			      "SCOPEID", zero_uuid_str,
			      "NAME", name, "METHOD", "get_region_by_name",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, grid_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg, got_map_single_resp_v2, 
			 new map_region_state(sgrp,cb,cb_priv));
}

void map_region_by_uuid_v2(struct simgroup_ctx* sgrp, const uuid_t id,
			   caj_find_region_cb cb, void *cb_priv) {
  char grid_uri[256]; char *req_body; char id_str[40];
  uuid_t zero_uuid; char zero_uuid_str[40];
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

  uuid_clear(zero_uuid); uuid_unparse(zero_uuid, zero_uuid_str);

  uuid_unparse(id, id_str);
  CAJ_DEBUG("DEBUG: map uuid request %s\n", id_str);

  snprintf(grid_uri,256, "%sgrid", grid->grid_server);
  req_body = soup_form_encode(SOUP_METHOD_POST, grid_uri,
			      "SCOPEID", zero_uuid_str, "REGIONID", id_str,
			      "METHOD", "get_region_by_uuid",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, grid_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg, got_map_single_resp_v2, 
			 new map_region_state(sgrp,cb,cb_priv));
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
  g_object_unref(addr);
}

static void do_teleport_rinfo_cb(void *priv, map_block_info *info) {
  teleport_desc* tp = (teleport_desc*)priv;
  if(tp->ctx == NULL) {
    user_teleport_failed(tp, "cancelled");
  } else if(info == NULL) {
    user_teleport_failed(tp, "Couldn't find destination region");
  } else {
    os_teleport_desc *tp_priv = new os_teleport_desc();
    tp_priv->our_sim = user_get_sim(tp->ctx);
    tp_priv->sim_ip = strdup(info->sim_ip);
    tp_priv->sim_port = info->sim_port;
    tp_priv->http_port = info->http_port;
    tp_priv->tp = tp;
    tp_priv->caps_path = NULL;

    // FIXME - use provided region handle

    // FIXME - do we really need to hold simulator shutdown here?
    sim_shutdown_hold(tp_priv->our_sim);
    SoupAddress *addr = soup_address_new(info->sim_ip, 0);
    soup_address_resolve_async(addr, g_main_context_default(),
			       NULL, do_teleport_resolve_cb, tp_priv);
  }
}

static void do_teleport(struct simgroup_ctx* sgrp,
			struct simulator_ctx* sim, struct teleport_desc* tp) {
  //GRID_PRIV_DEF(sim);
 
  // FIXME - handle teleport via region ID
  user_teleport_progress(tp, "resolving");
  req_region_info(sim, tp->region_handle, do_teleport_rinfo_cb, tp);
}

struct user_by_id_state {
  struct simgroup_ctx *sgrp;
  caj_user_profile_cb cb;
  void *cb_priv;
  
  user_by_id_state(simgroup_ctx *sgrp_, void(*cb_)(caj_user_profile* profile, void *priv),
		   void *cb_priv_) : sgrp(sgrp_), cb(cb_), cb_priv(cb_priv_) { };
};

static void got_user_by_id_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct user_by_id_state *st = (user_by_id_state*)user_data;
  GRID_PRIV_DEF_SGRP(st->sgrp);
  caj_user_profile profile;
  GHashTable *hash = NULL;
  char *s;

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    CAJ_WARN("User by ID req failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }

  CAJ_DEBUG("DEBUG: user by ID response ~%s~\n", msg->response_body->data);

  if(!soup_xmlrpc_extract_method_response(msg->response_body->data,
					 msg->response_body->length,
					 NULL,
					 G_TYPE_HASH_TABLE, &hash)) {
    CAJ_ERROR("User by ID req failed: couldn't parse response\n");
    goto out_fail;
  }

  if(helper_soup_hash_get_uuid(hash, "uuid", profile.uuid))
    goto bad_data;

  if(!soup_value_hash_lookup(hash, "firstname", G_TYPE_STRING, &profile.first))
    goto bad_data;
  if(!soup_value_hash_lookup(hash, "lastname", G_TYPE_STRING, &profile.last))
    goto bad_data;
  if(!soup_value_hash_lookup(hash, "profile_created", G_TYPE_STRING, &s))
    goto bad_data;
  profile.creation_time = atol(s);
  if(!soup_value_hash_lookup(hash, "email", G_TYPE_STRING, &profile.email))
    profile.email = NULL;
  // FIXME - use server_inventory/server_asset
  if(!soup_value_hash_lookup(hash, "profile_about", G_TYPE_STRING,
			     &profile.about_text))
    goto bad_data;
  if(helper_soup_hash_get_uuid(hash, "profile_image",
			       profile.profile_image))
    goto bad_data;
  if(helper_soup_hash_get_uuid(hash, "partner",
			       profile.partner))
    uuid_clear(profile.partner);
  if(!soup_value_hash_lookup(hash, "profile_firstlife_about", G_TYPE_STRING,
			     &profile.first_life_text))
    goto bad_data;
  if(helper_soup_hash_get_uuid(hash, "profile_firstlife_image",
			       profile.first_life_image))
    goto bad_data;
  if(!soup_value_hash_lookup(hash, "home_region", G_TYPE_STRING, &s))
    goto bad_data;
  profile.home_region = atoll(s);
  if(soup_value_hash_lookup(hash, "user_flags", G_TYPE_STRING, &s))
    profile.user_flags = atoi(s) & 0xff; // yes, really!
  else profile.user_flags = 0;
  // FIXME - use home_region_id, home_coordinates_x/y/z, and
  // home_look_x/y/z, and perhaps profile_lastlogin?
  // TODO - use profile_can_do/profile_want_do, god_level, custom_type?

  // It appears custom_type is actually the CharterMember data, if it exists.
  // However, OpenSim also has some horrid alternative method of encoding 
  // CharterMember data in the user_flags value. Ewwwww.
  
  profile.web_url = (char*)""; // OpenSim doesn't support this yet!
  
  // FIXME - cache ID to username mapping!
  
  st->cb(&profile, st->cb_priv);

  g_hash_table_destroy(hash); // must be after calling the callback
  delete st;
  return;

 bad_data:
  CAJ_ERROR("ERROR: bad/missing data in user by ID response\n");
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(NULL, st->cb_priv);
  delete st;
}


static void user_profile_by_id_v1(struct simgroup_ctx *sgrp, uuid_t id, 
			       caj_user_profile_cb cb, void *cb_priv) {
  char buf[40];
  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

  hash = soup_value_hash_new();
  uuid_unparse(id, buf);
  soup_value_hash_insert(hash,"avatar_uuid",G_TYPE_STRING,buf);

  msg = soup_xmlrpc_request_new(grid->user_server, "get_user_by_uuid",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    CAJ_ERROR("Could not create get_user_by_uuid request\n");
    cb(NULL, cb_priv);
    return;
  }

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_user_by_id_resp, new user_by_id_state(sgrp,cb,cb_priv));
}		       

static void user_profile_by_id_v2(struct simgroup_ctx *sgrp, uuid_t id, 
			       caj_user_profile_cb cb, void *cb_priv) {
  // FIXME - TODO (as soon as OpenSim gets it working itself, anyway)
  cb(NULL, cb_priv);
}

struct uuid_to_name_state_v1 {
  uuid_t uuid;
  void(*cb)(uuid_t uuid, const char* first, 
	    const char* last, void *priv);
  void *cb_priv;

  uuid_to_name_state_v1(void(*cb2)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
		     void *cb_priv2, uuid_t uuid2) : cb(cb2), cb_priv(cb_priv2) {
    uuid_copy(uuid, uuid2);
  };
};


static void uuid_to_name_cb_v1(caj_user_profile* profile, void *priv) {
  uuid_to_name_state_v1* st = (uuid_to_name_state_v1*)priv;
  if(profile == NULL) {
    st->cb(st->uuid,NULL,NULL,st->cb_priv);
  } else {
    st->cb(st->uuid,profile->first,profile->last,st->cb_priv);
  }
  delete st;  
}

static void uuid_to_name_v1(struct simgroup_ctx *sgrp, uuid_t id, 
			    void(*cb)(uuid_t uuid, const char* first, 
				      const char* last, void *priv),
			    void *cb_priv) {
  // FIXME - use cached UUID->name mappings once we have some
  user_profile_by_id_v1(sgrp, id, uuid_to_name_cb_v1, 
			new uuid_to_name_state_v1(cb,cb_priv,id));
}

struct uuid_to_name_state_v2 {
  uuid_t uuid;
  void(*cb)(uuid_t uuid, const char* first, 
	    const char* last, void *priv);
  void *cb_priv;
  struct simgroup_ctx *sgrp;

  uuid_to_name_state_v2(void(*cb2)(uuid_t uuid, const char* first, 
				const char* last, void *priv),
			void *cb_priv2, uuid_t uuid2,
			struct simgroup_ctx *sgrp2) : 
    cb(cb2), cb_priv(cb_priv2), sgrp(sgrp2) {
    uuid_copy(uuid, uuid2);
  };
};



static void uuid_to_name_cb_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  char *first_name = NULL, *last_name = NULL;
  os_robust_xml *rxml, *resnode, *node;
  struct uuid_to_name_state_v2 *st = (uuid_to_name_state_v2*)user_data;
  struct map_block_info info;
  GRID_PRIV_DEF_SGRP(st->sgrp);

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    CAJ_WARN("UUID to name failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }

  // FIXME - TODO
  CAJ_DEBUG("DEBUG: got UUID to name response ~%s~\n", msg->response_body->data);

  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    CAJ_ERROR("ERROR: couldn't parse account server XML response\n");
    goto out_fail;
  }

  
  resnode = os_robust_xml_lookup(rxml, "result");
  if(resnode == NULL) {
    CAJ_ERROR("ERROR: bad getaccount XML response\n");
    goto out_xmlfree_fail;
  }

  if(resnode->node_type != OS_ROBUST_XML_LIST) {
    // indicates the UUID doesn't exist
    CAJ_DEBUG("DEBUG: UUID to name lookup failed\n");
    goto out_xmlfree_fail;
  }

  /* <?xml version="1.0"?><ServerResponse><result type="List"><FirstName>Test</FirstName><LastName>User</LastName><Email></Email><PrincipalID>2a8353e3-0fb0-4117-b49f-9daad6f777c0</PrincipalID><ScopeID>00000000-0000-0000-0000-000000000000</ScopeID><Created>1250586006</Created><UserLevel>0</UserLevel><UserFlags>0</UserFlags><UserTitle></UserTitle><ServiceURLs>AssetServerURI*;InventoryServerURI*;GatewayURI*;HomeURI*;</ServiceURLs></result></ServerResponse> */

  node = os_robust_xml_lookup(resnode, "FirstName");
  if(node == NULL || node->node_type != OS_ROBUST_XML_STR) {
    CAJ_DEBUG("DEBUG: UUID to name lookup failed, bad/missing FirstName\n");
    goto out_xmlfree_fail;    
  }
  first_name = node->u.s;

  node = os_robust_xml_lookup(resnode, "LastName");
  if(node == NULL || node->node_type != OS_ROBUST_XML_STR) {
    CAJ_DEBUG("DEBUG: UUID to name lookup failed, bad/missing LastName\n");
    goto out_xmlfree_fail;    
  }
  last_name = node->u.s;

  // we ignore the other stuff


  st->cb(st->uuid, first_name, last_name, st->cb_priv);
  os_robust_xml_free(rxml); delete st; return;
  

 out_xmlfree_fail:
  os_robust_xml_free(rxml);
 out_fail:
  st->cb(st->uuid, NULL, NULL, st->cb_priv);
  delete st;
}

static void uuid_to_name_v2(struct simgroup_ctx *sgrp, uuid_t id, 
			    void(*cb)(uuid_t uuid, const char* first, 
				      const char* last, void *priv),
			    void *cb_priv) {
  // FIXME - TODO!
  //cb(id, NULL, NULL, cb_priv);

  char accounts_uri[256]; char *req_body;
  uuid_t zero_uuid; char zero_uuid_str[40];
  char av_uuid_str[40];
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

  uuid_clear(zero_uuid); uuid_unparse(zero_uuid, zero_uuid_str);

  uuid_unparse(id, av_uuid_str);
  
  snprintf(accounts_uri,256, "%saccounts", grid->user_server);
  req_body = soup_form_encode(SOUP_METHOD_POST, accounts_uri,
			      "VERSIONMIN", MIN_V2_PROTO_VERSION,
			      "VERSIONMAX", MAX_V2_PROTO_VERSION,
			      "ScopeID", zero_uuid_str,
			      "METHOD", "getaccount",
			      "UserID", av_uuid_str,
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, accounts_uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg, uuid_to_name_cb_v2, 
			 new uuid_to_name_state_v2(cb,cb_priv,id,sgrp));

}

static void cleanup(struct simgroup_ctx* sgrp) {
  GRID_PRIV_DEF_SGRP(sgrp);
  g_free(grid->user_server);
  g_free(grid->grid_server);
  g_free(grid->asset_server);
  g_free(grid->inventory_server);
  g_free(grid->avatar_server);
  g_free(grid->presence_server);
  g_free(grid->grid_user_server);

  g_free(grid->grid_recvkey);
  g_free(grid->grid_sendkey);
  delete grid;
}

int cajeput_grid_glue_init(int api_major, int api_minor,
			   struct simgroup_ctx *sgrp, void **priv,
			   struct cajeput_grid_hooks *hooks) {
  if(api_major != CAJEPUT_API_VERSION_MAJOR || 
     api_minor < CAJEPUT_API_VERSION_MINOR) 
    return false;

  struct grid_glue_ctx *grid = new grid_glue_ctx;
  grid->sgrp = sgrp; grid->log = caj_get_logger(sgrp);
  grid->old_xmlrpc_grid_proto = 
    sgrp_config_get_bool(grid->sgrp,"grid","grid_server_is_xmlrpc",NULL);
  grid->new_userserver = 
    sgrp_config_get_bool(grid->sgrp,"grid","new_userserver",NULL);
  grid->use_xinventory = 
    sgrp_config_get_bool(grid->sgrp,"grid","use_xinventory",NULL);
  *priv = grid;
  uuid_generate_random(grid->region_secret);

  if(grid->old_xmlrpc_grid_proto) {
    hooks->do_grid_login = do_grid_login_v1;
    hooks->map_block_request = map_block_request_v1;
    hooks->map_name_request = map_name_request_v1;
    hooks->map_region_by_name = map_region_by_name_v1;
    // hooks->map_region_by_uuid = ???; - TODO!
  } else {
    hooks->do_grid_login = do_grid_login_v2;
    hooks->map_block_request = map_block_request_v2;
    hooks->map_name_request = map_name_request_v2;
    hooks->map_region_by_name = map_region_by_name_v2;
    hooks->map_region_by_uuid = map_region_by_uuid_v2;
  }
  hooks->user_created = user_created;
  hooks->user_deleted = user_deleted;
  hooks->user_entered = user_entered;
  hooks->do_teleport = do_teleport;
  if(grid->use_xinventory) {
    hooks->fetch_inventory_folder = fetch_inventory_folder_x;
    hooks->fetch_inventory_item = fetch_inventory_item_x;
    hooks->fetch_system_folders = fetch_system_folders_x;
    hooks->add_inventory_item = add_inventory_item_x;
    hooks->update_inventory_item = update_inventory_item_x;
  } else {
    hooks->fetch_inventory_folder = fetch_inventory_folder;
    hooks->fetch_inventory_item = fetch_inventory_item;
    hooks->fetch_system_folders = fetch_system_folders;
    hooks->add_inventory_item = add_inventory_item;
    hooks->update_inventory_item = NULL; // FIXME!
  }

  if(grid->new_userserver) {
    hooks->uuid_to_name = uuid_to_name_v2;
    hooks->user_profile_by_id = user_profile_by_id_v2;
    hooks->user_logoff = user_logoff_v2;
  } else {
    hooks->uuid_to_name = uuid_to_name_v1;
    hooks->user_profile_by_id = user_profile_by_id_v1;
    hooks->user_logoff = user_logoff_v1;
  }


  hooks->get_asset = osglue_get_asset;
  hooks->put_asset = osglue_put_asset;
  hooks->send_im = send_im;
  hooks->cleanup = cleanup;

  grid->grid_server = sgrp_config_get_value(grid->sgrp,"grid","grid_server");
  grid->inventory_server = sgrp_config_get_value(grid->sgrp,"grid","inventory_server");
  grid->user_server =  sgrp_config_get_value(grid->sgrp,"grid","user_server");
  grid->asset_server = sgrp_config_get_value(grid->sgrp,"grid","asset_server");
  grid->avatar_server =  sgrp_config_get_value(grid->sgrp,"grid","avatar_server");
  grid->presence_server =  sgrp_config_get_value(grid->sgrp,"grid","presence_server");
  grid->grid_user_server =  sgrp_config_get_value(grid->sgrp,"grid","grid_user_server");
  // FIXME - remove send/recv keys (not used anymore)
  grid->grid_recvkey = sgrp_config_get_value(grid->sgrp,"grid","grid_recvkey");
  grid->grid_sendkey = sgrp_config_get_value(grid->sgrp,"grid","grid_sendkey");

  if(grid->grid_server == NULL || grid->inventory_server == NULL ||
     grid->user_server == NULL || grid->asset_server == NULL) {
    CAJ_ERROR("ERROR: grid not configured properly\n"); exit(1);
  }
  
  if(grid->avatar_server == NULL)
    grid->avatar_server = g_strdup(grid->user_server);
  if(grid->presence_server == NULL)
    grid->presence_server = g_strdup(grid->user_server);
  if(grid->grid_user_server == NULL)
    grid->grid_user_server = g_strdup(grid->user_server);
  
  

  caj_http_add_handler(sgrp, "/", xmlrpc_handler, 
		       sgrp, NULL);
  caj_http_add_handler(sgrp, "/agent/", osglue_agent_rest_handler, 
		       sgrp, NULL);

  return true;
}

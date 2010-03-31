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

// FIXME FIXME TODO - finish support for new user server.

#include "cajeput_core.h"
#include "cajeput_user.h"
#include "cajeput_grid_glue.h"
#include <libsoup/soup.h>
#include "caj_types.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include "opensim_grid_glue.h"
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

  printf("Logging into grid...\n");

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
  

  msg = soup_xmlrpc_request_new(grid->gridserver, "simulator_login",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create xmlrpc login request\n");
    exit(1);
  }


  caj_queue_soup_message(sgrp, msg,
			 got_grid_login_response_v1, sim);
  
  // TODO
}

static bool check_grid_success_response(SoupMessage *msg) {
  xmlDocPtr doc; xmlNodePtr node;
  if(msg->status_code != 200) return false;
  doc = xmlReadMemory(msg->response_body->data,
		      msg->response_body->length,
		      "grid_resp.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: couldn't parse gridserver XML response\n");
    return false;
  }

  bool retval = false; char *status;
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "ServerResponse") != 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto out;
  }
  node = node->children; 
  while(node != NULL && (node->type == XML_TEXT_NODE || 
			 node->type == XML_COMMENT_NODE))
    node = node->next;
  if(node == NULL || strcmp((char*)node->name, "Result") != 0) {
    printf("ERROR: bad success response from grid server\n");
    goto out;
  }
  status = (char*)xmlNodeListGetString(doc, node->children, 1);
  retval = (status != NULL && strcasecmp(status, "Success") == 0);
  xmlFree(status);
  
 out:
  xmlFreeDoc(doc); return retval;
}



static void got_grid_login_response_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)user_data;
  GRID_PRIV_DEF(sim);
  if(msg->status_code != 200) {
    printf("Grid login failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    exit(1);
  }
  printf("DEBUG: grid login response ~%s~\n", msg->response_body->data);
  if(!check_grid_success_response(msg)) {
    printf("ERROR: grid denied registration\n"); exit(1);
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

  printf("Logging into grid...\n");
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
  snprintf(grid_uri,256, "%sgrid", grid->gridserver);
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
static void expect_user_set_appearance(user_ctx *user,  GHashTable *hash) {
  GByteArray *data; char *s;
  if(soup_value_hash_lookup(hash,"visual_params",SOUP_TYPE_BYTE_ARRAY,
			    &data)) {
    caj_string str;
    caj_string_set_bin(&str,data->data,data->len);
    user_set_visual_params(user, &str);
  } else {
    printf("WARNING: expect_user is missing visual_params\n");
  }  

  if(soup_value_hash_lookup(hash,"texture",SOUP_TYPE_BYTE_ARRAY,
			    &data)) {
    caj_string str;
    caj_string_set_bin(&str,data->data,data->len);
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
    expect_user_set_appearance(user, hash);
  } else {
    printf("WARNING: expect_user is missing appearance data\n");
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
  char *agent_id;
};

static void got_validate_session_resp_v1(SoupSession *session, SoupMessage *msg, 
				   gpointer user_data) {
  GHashTable *hash; char *s;
  int is_ok = 0;
  validate_session_state* vs = (validate_session_state*) user_data;
  printf("DEBUG: got check_auth_session response\n");
  caj_shutdown_release(vs->sgrp);
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

void osglue_validate_session_v1(struct simgroup_ctx* sgrp, const char* agent_id,
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
  vs_state->sgrp = sgrp;
  vs_state->agent_id = NULL;

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, val_msg,
			 got_validate_session_resp_v1, vs_state);
  				   
}

static void got_validate_session_resp_v2(SoupSession *session, SoupMessage *msg, 
				   gpointer user_data) {
  int is_ok = 0; int correct = 0;
  validate_session_state* vs = (validate_session_state*) user_data;
  printf("DEBUG: got check_auth_session response from presence server\n");
  caj_shutdown_release(vs->sgrp);

  if(msg->status_code != 200) {
    printf("Presence request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    is_ok = FALSE;
  } else {

    xmlDocPtr doc; xmlNodePtr node;

    doc = xmlReadMemory(msg->response_body->data,
			msg->response_body->length,
			"grid_resp.xml", NULL, 0);
    if(doc == NULL) {
      printf("ERROR: couldn't parse presence server XML response\n");
      goto out;
    }

    bool retval = false; char *status;
    node = xmlDocGetRootElement(doc);
    if(strcmp((char*)node->name, "ServerResponse") != 0) {
      printf("ERROR: unexpected root node %s\n",(char*)node->name);
      goto out2;
    }

    /* OK. This is the fun bit. If the session exists, we get a response like:

       <?xml version="1.0"?><ServerResponse><result type="List"><UserID>2a8353e3-0fb0-4117-b49f-9daad6f777c0</UserID><RegionID>01bd2887-e158-443d-ab1a-08ee45053440</RegionID><online>True</online><login>3/29/2010 10:49:26 PM</login><logout>1/1/1970 12:00:00 AM</logout><position>&lt;119.3307, 127.6954, 25.45109&gt;</position><lookAt>&lt;-0.9945475, 0.1042844, 0&gt;</lookAt><HomeRegionID>01bd2887-e158-443d-ab1a-08ee45053440</HomeRegionID><HomePosition>&lt;139.3684, 130.0514, 23.69808&gt;</HomePosition><HomeLookAt>&lt;0.9991013, 0.04238611, 0&gt;</HomeLookAt></result></ServerResponse>

       However, if it doesn't, we instead get something like

       <?xml version="1.0"?><ServerResponse><result>null</result></ServerResponse>

       This is a pain in the backside to parse with every XML library known to man or woman. So we cheat instead.
    */

    node = node->children; 
    while(node != NULL && (node->type == XML_TEXT_NODE || 
			   node->type == XML_COMMENT_NODE))
      node = node->next;

    if(node == NULL || strcasecmp((char*)node->name, "result") != 0) {
      printf("ERROR: bad success response from presence server\n");
      goto out2;
    }

    node = node->children;

    while(node != NULL) {
      while(node != NULL && (node->type == XML_TEXT_NODE || 
			     node->type == XML_COMMENT_NODE))
	node = node->next;
      if(node == NULL) break;

      if(strcasecmp((char*)node->name, "online") == 0) {
	correct |= 0x1;
	status = (char*)xmlNodeListGetString(doc, node->children, 1);
	if (status != NULL && strcasecmp(status, "True") == 0)
	  correct |= 0x10;
	else printf("DEBUG: session invalid, user offline\n");
	xmlFree(status);
      } else if(strcasecmp((char*)node->name, "UserID") == 0) {
	correct |= 0x2;
	status = (char*)xmlNodeListGetString(doc, node->children, 1);
	if (status != NULL && strcasecmp(status, vs->agent_id) == 0)
	  correct |= 0x20;
	else printf("DEBUG: session invalid, agent ID mismatch\n");
	xmlFree(status);	
      }
      node = node->next;
    }

    if(correct == 0) {
      printf("DEBUG: session invalid, bad session ID or bad response\n");
      is_ok = FALSE;
    } else if(correct == 0x33) {
      printf("DEBUG: session validated OK\n");
      is_ok = TRUE;
    } else {
      is_ok = FALSE;
      if((correct & 0xf0) != 0x30)
	printf("DEBUG: bad presence response?!\n");
    }

    // FIXME - TODO    
  
  out2:
    xmlFreeDoc(doc);
  }

 out:
  printf("DEBUG: session validation conclusion: %s\n", is_ok?"OK":"FAILED");
  vs->callback(vs->priv, is_ok);
  free(vs->agent_id);
  delete vs;
}

void osglue_validate_session_v2(struct simgroup_ctx* sgrp, const char* agent_id,
			     const char *session_id, grid_glue_ctx* grid,
			     validate_session_cb callback, void *priv)  {
  char user_uri[256]; char *req_body; char sess_id;
  SoupMessage *msg;

  // FIXME - don't use fixed-size buffer 
  snprintf(user_uri,256, "%spresence", grid->gridserver);

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

  sim = caj_local_sim_by_region_handle(sgrp, region_handle);
  if(sim == NULL) {
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
  printf("DEBUG: Got a logoff_user call\n");
  if(params->n_values != 1 || 
     !soup_value_array_get_nth (params, 0, G_TYPE_HASH_TABLE, &args)) 
    goto bad_args;

  if(!soup_value_hash_lookup(args,"regionhandle",G_TYPE_STRING,
			     &s)) goto bad_args;
  region_handle = atoll(s);
  if(region_handle == 0) goto bad_args;
  sim = caj_local_sim_by_region_handle(sgrp, region_handle);
  if(sim == NULL) {
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
  struct simgroup_ctx* sgrp = (struct simgroup_ctx*) user_data;
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
    xmlrpc_logoff_user(server, msg, params, sgrp);
    
  } else if(strcmp(method_name, "expect_user") == 0) {
    printf("DEBUG: expect_user ~%s~\n", msg->request_body->data);
    xmlrpc_expect_user(server, msg, params, sgrp);
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


static void user_logoff(struct simgroup_ctx *sgrp,
			struct simulator_ctx* sim,
			const uuid_t user_id, const caj_vector3 *pos,
			const caj_vector3 *look_at) {
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
 

  msg = soup_xmlrpc_request_new(grid->userserver, "logout_of_simulator",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create xmlrpc login request\n");
    return;
  }

  caj_queue_soup_message(sgrp, msg,
			 got_user_logoff_resp, sim);
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
  sim_shutdown_release(sim);
  // FIXME - should probably pay *some* attention to the response
  if(msg->status_code != 200) {
    printf("User entry callback failed: got %i %s\n",
	   (int)msg->status_code,msg->reason_phrase);
  }

}

static void user_entered(struct simgroup_ctx *sgrp,
			 simulator_ctx *sim, user_ctx *user,
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
    caj_queue_soup_message(sgrp, msg,
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
  //GRID_PRIV_DEF(st->sim);
  struct map_block_info *blocks;
  int num_blocks = 0;
  GHashTable *hash = NULL;
  GValueArray *sims = NULL;

  caj_shutdown_release(st->sgrp);

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

    printf("DEBUG: map block %i,%i is %s\n",
	   blocks[num_blocks].x, blocks[num_blocks].y, blocks[num_blocks].name);

    num_blocks++;
    
    continue;    
  bad_block:
    printf("WARNING: Map block response has bad block, skipping\n");
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

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_map_block_resp_v1, new map_block_state(sgrp,cb,cb_priv));
}

static bool unpack_v2_region_entry(xmlDocPtr doc, xmlNodePtr node, 
				map_block_info *block) {
  int got_flags = 0;
  block->name = NULL; block->sim_ip = NULL;
  for(xmlNodePtr child = node->children; child != NULL; child = child->next) {
    if(child->type != XML_ELEMENT_NODE) continue;
    if(strcmp((char*)child->name, "uuid") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(val != NULL && uuid_parse(val, block->region_id) == 0)
	got_flags |= 0x1;
      free(val);
    } else if(strcmp((char*)child->name,"locX") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(val != NULL) {
	block->x = atoi(val)/256; got_flags |= 0x2;
      }
      free(val);
    } else if(strcmp((char*)child->name,"locY") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(val != NULL) {
	block->y = atoi(val)/256; got_flags |= 0x4;
      }
      free(val);
    } else if(strcmp((char*)child->name,"regionName") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(block->name == NULL) block->name = val; 
      else xmlFree(val);
      got_flags |= 0x8;
    } else if(strcmp((char*)child->name,"serverIP") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(block->sim_ip == NULL) block->sim_ip = val;
      else xmlFree(val);
      got_flags |= 0x10;
    } else if(strcmp((char*)child->name,"serverHttpPort") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(val != NULL) {
	block->http_port = atoi(val); got_flags |= 0x20;
      }
      free(val);
    } else if(strcmp((char*)child->name,"serverPort") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(val != NULL) {
	block->sim_port = atoi(val); got_flags |= 0x40;
      }
      free(val);
    } else if(strcmp((char*)child->name, "regionMapTexture") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(val != NULL && uuid_parse(val, block->map_image) == 0)
	got_flags |= 0x80;
      free(val);
    } else if(strcmp((char*)child->name,"access") == 0) {
      char *val = (char*)xmlNodeListGetString(doc, child->children, 1);
      if(val != NULL) {
	block->access = atoi(val); got_flags |= 0x100;
      }
      free(val);
    } 
    // we ignore serverURI, regionSecret
  }
  if(got_flags == 0) {
    // sometimes we get handed a block with nothing in.
    // FIXME - should check block has the type="List" attribute to avoid this.
    xmlFree(block->name); xmlFree(block->sim_ip); return false;
  } else if(got_flags != 0x1ff || block->name == NULL || block->sim_ip == NULL) {
    printf("ERROR: incomplete map block from server, flags 0x%x\n", got_flags);
    xmlFree(block->name); xmlFree(block->sim_ip); return false;
  } else {
    printf("DEBUG: processed map block data\n");
    // not sent, legacy info
    block->water_height = 20; block->num_agents = 0;
    block->flags = 0; // should this be sent?
    return true;
  }
}

static void got_map_multi_resp_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  xmlDocPtr doc; xmlNodePtr node;
  struct map_block_state *st = (map_block_state*)user_data;
  //GRID_PRIV_DEF(st->sim);
  struct map_block_info *blocks;
  size_t num_blocks = 0, alloc_blocks = 4;

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    printf("Map block request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }

  printf("DEBUG: got map block response: ~%s~\n", msg->response_body->data);
  
  doc = xmlReadMemory(msg->response_body->data,
		      msg->response_body->length,
		      "grid_resp.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: couldn't parse gridserver XML response\n");
    goto out_fail;
  }

  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "ServerResponse") != 0) {
    printf("ERROR: bad gridserver XML response\n");
    goto out_xmlfree_fail;
  }

  blocks = (map_block_info*)calloc(alloc_blocks, sizeof(map_block_info));
  for(node = node->children; node != NULL; node = node->next) {
    if(node->type != XML_ELEMENT_NODE) continue;
    // FIXME - should check if it has the type="List" attribute.

    printf("DEBUG: processing map block data\n");

    if(num_blocks >= alloc_blocks) {
      size_t new_alloc_blocks = alloc_blocks * 2;
      size_t new_size = new_alloc_blocks*sizeof(map_block_info);
      if(new_size / sizeof(map_block_info) < new_alloc_blocks) continue;
      void* new_blocks = realloc(blocks, new_size);
      if(new_blocks == NULL) continue;
      alloc_blocks = new_alloc_blocks; blocks = (map_block_info*)new_blocks;
    }

    printf("DEBUG: still processing map block data\n");

    map_block_info *block = &blocks[num_blocks]; 
    if(unpack_v2_region_entry(doc, node, block)) num_blocks++;
     
  }

  xmlFreeDoc(doc);
  printf("DEBUG: got %i map blocks\n", num_blocks);
  st->cb(st->cb_priv, blocks, num_blocks);
  for(size_t i = 0; i < num_blocks; i++) {
    free(blocks[i].name); free(blocks[i].sim_ip);
  }
  free(blocks); delete st; 
  return;

 out_xmlfree_fail:
  xmlFreeDoc(doc);
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

  printf("DEBUG: map block request (%i,%i)-(%i,%i)\n",
	 min_x, min_y, max_x, max_y);

  snprintf(xmin_s, 20, "%i", min_x*256);
  snprintf(xmax_s, 20, "%i", max_x*256);
  snprintf(ymin_s, 20, "%i", min_y*256);
  snprintf(ymax_s, 20, "%i", max_y*256);

  snprintf(grid_uri,256, "%sgrid", grid->gridserver);
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
  //GRID_PRIV_DEF(st->sim);
  GHashTable *hash = NULL;
  char *s; //uuid_t u;

  caj_shutdown_release(st->sgrp);

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
  printf("ERROR: couldn't lookup expected values in region info reply\n");
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

  msg = soup_xmlrpc_request_new(grid->gridserver, "simulator_data_request",
				G_TYPE_HASH_TABLE, hash,
				G_TYPE_INVALID);
  g_hash_table_destroy(hash);
  if (!msg) {
    fprintf(stderr, "Could not create region_info request\n");
    cb(cb_priv, NULL);
    return;
  }

  
  map_region_state *st = new map_region_state(sgrp, cb, cb_priv);

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_region_info_v1, st);

}

static void got_map_single_resp_v2(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  xmlDocPtr doc; xmlNodePtr node;
  struct map_region_state *st = (map_region_state*)user_data;
  struct map_block_info info;
  //GRID_PRIV_DEF(st->sim);
  char *s; //uuid_t u;

  caj_shutdown_release(st->sgrp);

  if(msg->status_code != 200) {
    printf("Region info request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto out_fail;
  }

  // FIXME - TODO
  printf("DEBUG: got region info response ~%s~\n", msg->response_body->data);

  doc = xmlReadMemory(msg->response_body->data,
		      msg->response_body->length,
		      "grid_resp.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: couldn't parse gridserver XML response\n");
    goto out_fail;
  }

  
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "ServerResponse") != 0) {
    printf("ERROR: bad get_region_by_position XML response\n");
    goto out_xmlfree_fail;
  }
  node = node->children;
  if(strcmp((char*)node->name, "result") != 0) {
    printf("ERROR: bad get_region_by_position XML response\n");
    goto out_xmlfree_fail;
  }

  if(unpack_v2_region_entry(doc, node, &info)) {
    st->cb(st->cb_priv, &info);
    xmlFree(info.name); xmlFree(info.sim_ip);
    xmlFreeDoc(doc); delete st; return;
  }

 out_xmlfree_fail:
  xmlFreeDoc(doc);
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

  printf("DEBUG: region info request (%s,%s)\n",
	 x_s, y_s);

  snprintf(grid_uri,256, "%sgrid", grid->gridserver);
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
  //GRID_PRIV_DEF(st->sim);
  GHashTable *hash = NULL;
  char *s; //uuid_t u;

  caj_shutdown_release(st->sgrp);

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

static void map_name_request_v1(struct simgroup_ctx* sgrp, const char* name,
				caj_find_regions_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);

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

  printf("DEBUG: map name request %s\n", name);

  snprintf(max_results, 20, "%i", 20); // FIXME!

  snprintf(grid_uri,256, "%sgrid", grid->gridserver);
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

  printf("DEBUG: map name request %s\n", name);

  snprintf(max_results, 20, "%i", 20); // FIXME!

  snprintf(grid_uri,256, "%sgrid", grid->gridserver);
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
  //GRID_PRIV_DEF(st->sim);
  caj_user_profile profile;
  GHashTable *hash = NULL;
  char *s;

  caj_shutdown_release(st->sgrp);

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
  printf("ERROR: bad/missing data in user by ID response\n");
  g_hash_table_destroy(hash);
 out_fail:
  st->cb(NULL, st->cb_priv);
  delete st;
}


static void user_profile_by_id(struct simgroup_ctx *sgrp, uuid_t id, 
			       caj_user_profile_cb cb, void *cb_priv) {
  char buf[40];
  GHashTable *hash;
  //GError *error = NULL;
  SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);

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

  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg,
			 got_user_by_id_resp, new user_by_id_state(sgrp,cb,cb_priv));
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

static void uuid_to_name_cb(caj_user_profile* profile, void *priv) {
  uuid_to_name_state* st = (uuid_to_name_state*)priv;
  if(profile == NULL) {
    st->cb(st->uuid,NULL,NULL,st->cb_priv);
  } else {
    st->cb(st->uuid,profile->first,profile->last,st->cb_priv);
  }
  delete st;
}

static void uuid_to_name(struct simgroup_ctx *sgrp, uuid_t id, 
			 void(*cb)(uuid_t uuid, const char* first, 
				   const char* last, void *priv),
			 void *cb_priv) {
  // FIXME - use cached UUID->name mappings once we have some
  user_profile_by_id(sgrp, id, uuid_to_name_cb, 
		     new uuid_to_name_state(cb,cb_priv,id));
  //cb(id, NULL, NULL, cb_priv); // FIXME!!!  
}

static void cleanup(struct simgroup_ctx* sgrp) {
  GRID_PRIV_DEF_SGRP(sgrp);
  g_free(grid->userserver);
  g_free(grid->gridserver);
  g_free(grid->assetserver);
  g_free(grid->inventoryserver);
  g_free(grid->grid_recvkey);
  g_free(grid->grid_sendkey);
  delete grid;
}

int cajeput_grid_glue_init(int api_version, struct simgroup_ctx *sgrp, 
			   void **priv, struct cajeput_grid_hooks *hooks) {
  if(api_version != CAJEPUT_API_VERSION) 
    return false;

  struct grid_glue_ctx *grid = new grid_glue_ctx;
  grid->sgrp = sgrp;
  grid->old_xmlrpc_grid_proto = 
    sgrp_config_get_bool(grid->sgrp,"grid","grid_server_is_xmlrpc",NULL);
  grid->new_userserver = 
    sgrp_config_get_bool(grid->sgrp,"grid","new_userserver",NULL);
  *priv = grid;
  uuid_generate_random(grid->region_secret);

  if(grid->old_xmlrpc_grid_proto) {
    hooks->do_grid_login = do_grid_login_v1;
    hooks->map_block_request = map_block_request_v1;
    hooks->map_name_request = map_name_request_v1;
    hooks->map_region_by_name = map_region_by_name_v1;
  } else {
    hooks->do_grid_login = do_grid_login_v2;
    hooks->map_block_request = map_block_request_v2;
    hooks->map_name_request = map_name_request_v2;
    hooks->map_region_by_name = map_region_by_name_v2;
  }
  hooks->user_created = user_created;
  hooks->user_logoff = user_logoff;
  hooks->user_deleted = user_deleted;
  hooks->user_entered = user_entered;
  hooks->do_teleport = do_teleport;
  hooks->fetch_inventory_folder = fetch_inventory_folder;
  hooks->fetch_inventory_item = fetch_inventory_item;
  hooks->fetch_system_folders = fetch_system_folders;
  hooks->uuid_to_name = uuid_to_name;
  hooks->user_profile_by_id = user_profile_by_id;

  hooks->add_inventory_item = add_inventory_item;

  hooks->get_texture = osglue_get_texture;
  hooks->get_asset = osglue_get_asset;
  hooks->put_asset = osglue_put_asset;
  hooks->cleanup = cleanup;

  grid->gridserver = sgrp_config_get_value(grid->sgrp,"grid","grid_server");
  grid->inventoryserver = sgrp_config_get_value(grid->sgrp,"grid","inventory_server");
  grid->userserver =  sgrp_config_get_value(grid->sgrp,"grid","user_server");
  grid->assetserver = sgrp_config_get_value(grid->sgrp,"grid","asset_server");
  // FIXME - remove send/recv keys (not used anymore)
  grid->grid_recvkey = sgrp_config_get_value(grid->sgrp,"grid","grid_recvkey");
  grid->grid_sendkey = sgrp_config_get_value(grid->sgrp,"grid","grid_sendkey");

  if(grid->gridserver == NULL || grid->inventoryserver == NULL ||
     grid->userserver == NULL || grid->assetserver == NULL) {
    printf("ERROR: grid not configured properly\n"); exit(1);
  }

  caj_http_add_handler(sgrp, "/", xmlrpc_handler, 
		       sgrp, NULL);
  caj_http_add_handler(sgrp, "/agent/", osglue_agent_rest_handler, 
		       sgrp, NULL);

  return true;
}

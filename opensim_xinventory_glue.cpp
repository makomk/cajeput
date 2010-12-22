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
#include <libsoup/soup.h>
#include "caj_types.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <libsoup/soup.h>
#include "opensim_grid_glue.h"
#include "opensim_robust_xml.h"
#include <cassert>
#include <stddef.h>


static bool parse_inventory_item_x(os_robust_xml *node, 
				   struct inventory_item &invit) {
    const char *asset_id = os_robust_xml_lookup_str(node, "AssetID");
    const char *asset_type = os_robust_xml_lookup_str(node, "AssetType");
    const char *base_perms = os_robust_xml_lookup_str(node, "BasePermissions");
    const char *creation_date = os_robust_xml_lookup_str(node, "CreationDate");
    const char *creator_id = os_robust_xml_lookup_str(node, "CreatorId");
    const char *cur_perms = os_robust_xml_lookup_str(node, "CurrentPermissions");
    const char *descr = os_robust_xml_lookup_str(node, "Description");
    const char *everyone_perms = os_robust_xml_lookup_str(node, "EveryOnePermissions");
    const char *flags = os_robust_xml_lookup_str(node, "Flags");
    const char *folder = os_robust_xml_lookup_str(node, "Folder");
    const char *group_id = os_robust_xml_lookup_str(node, "GroupID");
    const char *group_owned = os_robust_xml_lookup_str(node, "GroupOwned");
    const char *group_perms = os_robust_xml_lookup_str(node, "GroupPermissions");
    const char *id_s = os_robust_xml_lookup_str(node, "ID");
    const char *inv_type = os_robust_xml_lookup_str(node, "InvType");
    const char *name = os_robust_xml_lookup_str(node, "Name");
    const char *next_perms = os_robust_xml_lookup_str(node, "NextPermissions");
    const char *owner_s = os_robust_xml_lookup_str(node, "Owner");
    const char *sale_price = os_robust_xml_lookup_str(node, "SalePrice");
    const char *sale_type = os_robust_xml_lookup_str(node, "SaleType");

    if(asset_id == NULL || asset_type == NULL || base_perms == NULL || 
       creation_date == NULL || creator_id == NULL || cur_perms == NULL ||
       descr == NULL || everyone_perms == NULL || flags == NULL || 
       folder == NULL || group_id == NULL || group_owned == NULL || 
       group_perms == NULL || id_s == NULL || inv_type == NULL ||
       name == NULL || next_perms == NULL || owner_s == NULL || 
       sale_price == NULL || sale_type == NULL) {
      printf("ERROR: bad item in XInventory response\n");
      return false;
    }

    invit.name = const_cast<char*>(name);
    invit.description = const_cast<char*>(descr);
    invit.creator_id = const_cast<char*>(creator_id);
    if(uuid_parse(id_s, invit.item_id) ||
       uuid_parse(asset_id, invit.asset_id) ||
       uuid_parse(owner_s, invit.owner_id) || // FIXME - do we want this?
       uuid_parse(group_id, invit.group_id) ||
       uuid_parse(folder, invit.folder_id)) {
      printf("ERROR: bad UUID in XInventory response\n");
      return false;
    }
    if(uuid_parse(creator_id, invit.creator_as_uuid))
      uuid_clear(invit.creator_as_uuid); // FIXME - ???
    // FIXME - better error checking?
    invit.perms.base = atoi(base_perms);
    invit.perms.current = atoi(cur_perms);
    invit.perms.group = atoi(group_perms);
    invit.perms.next = atoi(next_perms);
    invit.perms.everyone = atoi(everyone_perms);
    invit.inv_type = atoi(inv_type);
    invit.asset_type = atoi(asset_type);
    invit.sale_type = atoi(sale_type); 
    invit.group_owned = (strcasecmp(group_owned, "True") == 0);
    invit.flags = atoi(flags);
    invit.sale_price = atoi(sale_price);
    invit.creation_date = atoi(creation_date);  
    return true;
}

static void parse_inv_folders_x(os_robust_xml *folders_node,
				struct inventory_contents* inv) {
  GHashTableIter iter; os_robust_xml *node;
  os_robust_xml_iter_begin(&iter, folders_node);
  while(os_robust_xml_iter_next(&iter, NULL, &node)) {
    // TODO - what goes here?
  }
}

static void parse_inv_items_x(os_robust_xml *items_node,
			      struct inventory_contents* inv) {
  GHashTableIter iter; os_robust_xml *node;
  os_robust_xml_iter_begin(&iter, items_node);
  while(os_robust_xml_iter_next(&iter, NULL, &node)) {
    if(node->node_type != OS_ROBUST_XML_LIST) {
      printf("ERROR: bad XInventory response, items should be list nodes");
      continue;
    }
    
    struct inventory_item invit;
    if(!parse_inventory_item_x(node, invit))
      continue;

    caj_add_inventory_item_copy(inv, &invit);
  }
}

struct inv_items_req {
  user_grid_glue* user_glue;
  uuid_t folder_id;
  void(*cb)(struct inventory_contents* inv, 
	    void *cb_priv);
  void *cb_priv;
};

static void got_inventory_items_resp_x(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  //USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  inv_items_req *req = (inv_items_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  os_robust_xml *rxml, *folders_node, *items_node,  *node;
  struct inventory_contents* inv;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("XInventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto fail;
  }

  printf("DEBUG: XInventory folder content resp {{%s}}\n",
	 msg->response_body->data);

  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    printf("ERROR: couldn't parse XInventory folder contents\n");
    goto fail;
  }

  
  folders_node = os_robust_xml_lookup(rxml, "FOLDERS");
  items_node = os_robust_xml_lookup(rxml, "ITEMS");
  if(folders_node == NULL || folders_node->node_type != OS_ROBUST_XML_LIST ||
     items_node == NULL || items_node->node_type != OS_ROBUST_XML_LIST) {
    printf("ERROR: bad XInventory response, missing FOLDERS or ITEMS\n");
    goto free_fail;
  }

  inv = caj_inv_new_contents_desc(req->folder_id);
  parse_inv_folders_x(folders_node, inv);
  parse_inv_items_x(items_node, inv);

  req->cb(inv, req->cb_priv);
  caj_inv_free_contents_desc(inv);
  os_robust_xml_free(rxml);
  delete req;

  return;

 free_inv_fail:
  caj_inv_free_contents_desc(inv);
 free_fail:
  os_robust_xml_free(rxml);
 fail:
  req->cb(NULL, req->cb_priv);
  delete req;
  printf("ERROR: inventory response parse failure\n");
  return;
}


// fetch contents of inventory folder (XInventory)
void fetch_inventory_folder_x(simgroup_ctx *sgrp, user_ctx *user,
			    void *user_priv, const uuid_t folder_id,
			    void(*cb)(struct inventory_contents* inv, 
				      void* priv),
			    void *cb_priv) {
  uuid_t u; char user_id_str[40], folder_id_str[40]; char uri[256];
  inv_items_req *req; char *req_body; SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);
  USER_PRIV_DEF(user_priv);

  user_get_uuid(user, u);
  uuid_unparse(u, user_id_str);
  uuid_unparse(folder_id, folder_id_str);
  
  // FIXME - don't use fixed-size buffer 
  snprintf(uri,256, "%sxinventory", grid->inventory_server);
  // I would use soup_message_set_request, but it has a memory leak...
  req_body = soup_form_encode("PRINCIPAL", user_id_str,
			      "FOLDER", folder_id_str,
			      "METHOD", "GETFOLDERCONTENT",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  req = new inv_items_req();
  req->user_glue = user_glue; req->cb = cb;
  req->cb_priv = cb_priv;
  uuid_copy(req->folder_id, folder_id);
  user_grid_glue_ref(user_glue);
  caj_queue_soup_message(sgrp, msg,
			 got_inventory_items_resp_x, req);

}

struct inv_item_req {
  user_grid_glue* user_glue;
  uuid_t item_id;
  void(*cb)(struct inventory_item* item, 
	    void *cb_priv);
  void *cb_priv;
};

static void got_inventory_item_resp_x(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  //USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  inv_item_req *req = (inv_item_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  os_robust_xml *rxml, *item_node,  *node;
  struct inventory_item invit;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("XInventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto fail;
  }

  printf("DEBUG: XInventory item content resp {{%s}}\n",
	 msg->response_body->data);

  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    printf("ERROR: couldn't parse XInventory item contents\n");
    goto fail;
  }

  node = os_robust_xml_lookup(rxml, "item");
  if(node == NULL || node->node_type != OS_ROBUST_XML_LIST) {
    // this is normal if the item doesn't exist.
    printf("ERROR: bad XInventory response, missing/bad item node\n");
    goto free_fail;
  }

  if(!parse_inventory_item_x(node, invit))
    goto free_fail;
    
  req->cb(&invit, req->cb_priv);
  os_robust_xml_free(rxml);
  delete req;
  return;

 free_fail:
  os_robust_xml_free(rxml);
 fail:
  req->cb(NULL, req->cb_priv);
  delete req;
  printf("ERROR: inventory item response parse failure\n");
  return;
}

void fetch_inventory_item_x(simgroup_ctx *sgrp, user_ctx *user,
			    void *user_priv, const uuid_t item_id,
			    void(*cb)(struct inventory_item* item, 
				      void* priv),
			    void *cb_priv) {
  uuid_t u; char item_id_str[40]; char uri[256];
  inv_item_req *req; char *req_body; SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);
  USER_PRIV_DEF(user_priv);

  // for some reason, this doesn't need the user ID.

  uuid_unparse(item_id, item_id_str);
  
  // FIXME - don't use fixed-size buffer 
  snprintf(uri,256, "%sxinventory", grid->inventory_server);
  // I would use soup_message_set_request, but it has a memory leak...
  req_body = soup_form_encode("ID", item_id_str,
			      "METHOD", "GETITEM",
			      NULL);
  msg = soup_message_new(SOUP_METHOD_POST, uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  req = new inv_item_req();
  req->user_glue = user_glue; req->cb = cb;
  req->cb_priv = cb_priv;
  uuid_copy(req->item_id, item_id);
  user_grid_glue_ref(user_glue);
  caj_queue_soup_message(sgrp, msg,
			 got_inventory_item_resp_x, req);
}

// fetch a list of the user's system folders
void fetch_system_folders_x(simgroup_ctx *sgrp, user_ctx *user,
			  void *user_priv) {
  // FIXME - how the hell can we do this with XInventory?
}

static char *build_xinv_req(user_ctx *user, inventory_item *inv, const char *method) {
  char item_id_str[40], asset_id_str[40], owner_str[40], folder_id_str[40]; 
  char inv_type[16], asset_type[16], sale_type[16], sale_price[16], flags[16];
  char creation_date[16], base_perms[16], cur_perms[16], group_perms[16];
  char next_perms[16], everyone_perms[16], group_id_str[40];
  uuid_t u;

  uuid_unparse(inv->item_id, item_id_str);
  user_get_uuid(user, u);
  uuid_unparse(u, owner_str);
  uuid_unparse(inv->folder_id, folder_id_str);
  uuid_unparse(inv->asset_id, asset_id_str);
  uuid_unparse(inv->group_id, group_id_str);

  snprintf(inv_type, 16, "%i", (int)inv->inv_type);
  snprintf(asset_type, 16, "%i", (int)inv->asset_type);
  snprintf(sale_type, 16, "%i", (int)inv->sale_type);
  snprintf(sale_price, 16, "%i", (int)inv->sale_price);
  snprintf(flags, 16, "%i", (int)inv->flags);
  snprintf(creation_date, 16, "%i", (int)inv->creation_date);

  snprintf(base_perms, 16, "%i", (int)inv->perms.base);
  snprintf(cur_perms, 16, "%i", (int)inv->perms.current);
  snprintf(group_perms, 16, "%i", (int)inv->perms.group);
  snprintf(everyone_perms, 16, "%i", (int)inv->perms.everyone);
  snprintf(next_perms, 16, "%i", (int)inv->perms.next);
  
  // I would use soup_message_set_request, but it has a memory leak...
  return soup_form_encode("METHOD", method,
			  "Owner", owner_str,
			  "ID", item_id_str,
			  "Folder", folder_id_str,
			  "CreatorId", inv->creator_id,
			  "AssetID", asset_id_str,
			  "AssetType", asset_type,
			  "InvType", inv_type,
			  "Name", inv->name,
			  "Description", inv->description,
			  "SaleType", sale_type,
			  "SalePrice", sale_price,
			  "Flags", flags,
			  "CreationDate", creation_date,
			  "BasePermissions", base_perms,
			  "CurrentPermissions", cur_perms,
			  "GroupPermissions", group_perms,
			  "EveryOnePermissions", everyone_perms,
			  "NextPermissions", next_perms,
			  "GroupID", group_id_str,
			  "GroupOwned", inv->group_owned?"True":"False",
			  NULL);
}

struct add_inv_item_req {
  user_grid_glue* user_glue;
  uuid_t item_id;
  void(*cb)(void *cb_priv, int success, uuid_t item_id);
  void *cb_priv;
};

static void got_add_inventory_resp_x(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  //USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  add_inv_item_req *req = (add_inv_item_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  os_robust_xml *rxml, *node; uuid_t u;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("XInventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto fail;
  }

  printf("DEBUG: XInventory add item resp {{%s}}\n",
	 msg->response_body->data);

  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    printf("ERROR: couldn't parse XInventory item contents\n");
    goto fail;
  }

  node = os_robust_xml_lookup(rxml, "RESULT");
  if(node == NULL || node->node_type != OS_ROBUST_XML_STR) {
    // this is normal if the item doesn't exist.
    printf("ERROR: bad XInventory response, missing/bad RESULT node\n");
    goto free_fail;
  }

  if(strcasecmp(node->u.s, "True") != 0) goto free_fail;
    
  os_robust_xml_free(rxml);
  req->cb(req->cb_priv, TRUE, req->item_id);
  delete req;
  return;

 free_fail:
  os_robust_xml_free(rxml);
 fail:
  printf("ERROR: failed to add inventory item\n");
  uuid_clear(u); req->cb(req->cb_priv, FALSE, u);
  delete req;
  return;
}

void add_inventory_item_x(simgroup_ctx *sgrp, user_ctx *user,
			void *user_priv, inventory_item *inv,
			void(*cb)(void* priv, int success, uuid_t item_id),
			void *cb_priv) {
  char uri[256];
  add_inv_item_req *req; char *req_body; SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);
  USER_PRIV_DEF(user_priv);

  // FIXME - don't use fixed-size buffer 
  snprintf(uri,256, "%sxinventory", grid->inventory_server);
  req_body = build_xinv_req(user, inv, "ADDITEM");
  msg = soup_message_new(SOUP_METHOD_POST, uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  req = new add_inv_item_req();
  req->user_glue = user_glue; req->cb = cb;
  req->cb_priv = cb_priv;
  uuid_copy(req->item_id, inv->item_id);
  user_grid_glue_ref(user_glue);
  caj_queue_soup_message(sgrp, msg,
			 got_add_inventory_resp_x, req);
}


struct update_inv_item_req {
  user_grid_glue* user_glue;
  uuid_t item_id;
  void(*cb)(void *cb_priv, int success);
  void *cb_priv;
};

static void got_update_inventory_resp_x(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  //USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  update_inv_item_req *req = (update_inv_item_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  os_robust_xml *rxml, *node;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("XInventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto fail;
  }

  printf("DEBUG: XInventory update item resp {{%s}}\n",
	 msg->response_body->data);

  rxml = os_robust_xml_deserialise(msg->response_body->data,
				   msg->response_body->length);
  if(rxml == NULL) {
    printf("ERROR: couldn't parse XInventory response\n");
    goto fail;
  }

  node = os_robust_xml_lookup(rxml, "RESULT");
  if(node == NULL || node->node_type != OS_ROBUST_XML_STR) {
    // this is normal if the item doesn't exist.
    printf("ERROR: bad XInventory response, missing/bad RESULT node\n");
    goto free_fail;
  }

  if(strcasecmp(node->u.s, "True") != 0) goto free_fail;
    
  os_robust_xml_free(rxml);
  req->cb(req->cb_priv, TRUE);
  delete req;
  return;

 free_fail:
  os_robust_xml_free(rxml);
 fail:
  printf("ERROR: failed to update inventory item\n");
  req->cb(req->cb_priv, FALSE);
  delete req;
  return;
}

void update_inventory_item_x(simgroup_ctx *sgrp, user_ctx *user,
			     void *user_priv, inventory_item *inv,
			     void(*cb)(void* priv, int success),
			     void *cb_priv) {
  char uri[256];
  update_inv_item_req *req; char *req_body; SoupMessage *msg;
  GRID_PRIV_DEF_SGRP(sgrp);
  USER_PRIV_DEF(user_priv);

  // FIXME - don't use fixed-size buffer 
  snprintf(uri,256, "%sxinventory", grid->inventory_server);
  req_body = build_xinv_req(user, inv, "UPDATEITEM");
  msg = soup_message_new(SOUP_METHOD_POST, uri);
  soup_message_set_request(msg, "application/x-www-form-urlencoded",
			   SOUP_MEMORY_TAKE, req_body, strlen(req_body));

  req = new update_inv_item_req();
  req->user_glue = user_glue; req->cb = cb;
  req->cb_priv = cb_priv;
  uuid_copy(req->item_id, inv->item_id);
  user_grid_glue_ref(user_glue);
  caj_queue_soup_message(sgrp, msg,
			 got_update_inventory_resp_x, req);
}

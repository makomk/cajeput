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
#include "opensim_xml_glue.h"
#include <cassert>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <stddef.h>

// FIXME - this code has bucket-loads of code duplication!

static int check_node(xmlNodePtr node, const char* name) {
  if(node == NULL) {
    printf("ERROR: missing %s node\n", name); 
    return 0;
  } else if(strcmp((char*)node->name, name) != 0) {
    printf("ERROR: unexpected node: wanted %s, got %s\n",
	   name, (char*)node->name);
    return 0;
  }
  return 1;
}




struct os_inv_folder {
  char *name;
  uuid_t folder_id, owner_id, parent_id;
  int asset_type;
  int version; // FIXME - use long/int32_t?
};

static xml_serialisation_desc deserialise_inv_folder[] = {
  { "Name", XML_STYPE_STRING, offsetof(os_inv_folder, name) },
  { "ID", XML_STYPE_UUID, offsetof(os_inv_folder, folder_id) },
  { "Owner", XML_STYPE_UUID,offsetof(os_inv_folder, owner_id) },
  { "ParentID", XML_STYPE_UUID,offsetof(os_inv_folder, parent_id) },
  { "Type", XML_STYPE_INT,offsetof(os_inv_folder, asset_type) },
  { "Version", XML_STYPE_INT,offsetof(os_inv_folder, version) },
  { NULL, }
};

// FIXME - deal with whitespace!
static void parse_inv_folders(xmlDocPtr doc, xmlNodePtr node,
			      struct inventory_contents* inv) {
  for( ; node != NULL; node = node->next) {
    if(!check_node(node,"InventoryFolderBase")) continue;

    os_inv_folder folder;
    if(!osglue_deserialise_xml(doc, node->children, deserialise_inv_folder,
			       &folder)) continue;

    // DEBUG
    char buf[40];
    uuid_unparse(folder.folder_id, buf);
    printf("Inventory folder: %s [%s]\n", folder.name, buf);

    // FIXME - need to check parent ID matches expected one
    caj_inv_add_folder(inv, folder.folder_id, folder.owner_id, 
		       folder.name, folder.asset_type);
    xmlFree(folder.name);
  }
}

struct os_inv_item {
  char *name;
  uuid_t item_id, owner_id;
  int inv_type;
  uuid_t folder_id;
  char *creator_id;
  uuid_t creator_as_uuid;
  char *description;
  int next_perms, current_perms, base_perms; // FIXME - should be uint32_t
  int everyone_perms, group_perms; // ditto!
  int asset_type;
  uuid_t asset_id, group_id;
  int group_owned;
  int sale_price; // FIXME - should be int32_t
  int sale_type;
  int flags; // FIXME - should be uint32_t
  int creation_date; // FIXME - should be int32_t (or larger?)
};

static xml_serialisation_desc deserialise_inv_item[] = {
  { "Name", XML_STYPE_STRING, offsetof(os_inv_item, name) },
  { "ID", XML_STYPE_UUID, offsetof(os_inv_item, item_id) },
  { "Owner", XML_STYPE_UUID,offsetof(os_inv_item, owner_id) },
  { "InvType", XML_STYPE_INT,offsetof(os_inv_item, inv_type) },
  { "Folder", XML_STYPE_UUID,offsetof(os_inv_item, folder_id) },
  { "CreatorId", XML_STYPE_STRING, offsetof(os_inv_item, creator_id) },
  { "CreatorIdAsUuid", XML_STYPE_UUID, offsetof(os_inv_item, creator_as_uuid) },
  { "Description", XML_STYPE_STRING, offsetof(os_inv_item, description) },
  { "NextPermissions", XML_STYPE_INT,offsetof(os_inv_item, next_perms) },
  { "CurrentPermissions", XML_STYPE_INT,offsetof(os_inv_item, current_perms) },
  { "BasePermissions", XML_STYPE_INT,offsetof(os_inv_item, base_perms) },
  { "EveryOnePermissions", XML_STYPE_INT,offsetof(os_inv_item, everyone_perms) },
  { "GroupPermissions", XML_STYPE_INT,offsetof(os_inv_item, group_perms) },
  { "AssetType", XML_STYPE_INT,offsetof(os_inv_item, asset_type) },
  { "AssetID", XML_STYPE_UUID,offsetof(os_inv_item, asset_id) },
  { "GroupID", XML_STYPE_UUID,offsetof(os_inv_item, group_id) },
  { "GroupOwned", XML_STYPE_BOOL,offsetof(os_inv_item, group_owned)  },
  { "SalePrice", XML_STYPE_INT,offsetof(os_inv_item, sale_price) },  
  { "SaleType", XML_STYPE_INT,offsetof(os_inv_item, sale_type) },  
  { "Flags", XML_STYPE_INT,offsetof(os_inv_item, flags) },  
  { "CreationDate", XML_STYPE_INT,offsetof(os_inv_item, creation_date) },  
  { NULL, }
};

static void fill_inv_item_from_opensim(inventory_item *citem,
				       os_inv_item &item) {

    uuid_copy(citem->item_id, item.item_id);
    uuid_copy(citem->owner_id, item.owner_id);
    citem->inv_type = item.inv_type;
    uuid_copy(citem->folder_id, item.folder_id);
    uuid_copy(citem->creator_as_uuid, item.creator_as_uuid);
    citem->perms.next = item.next_perms;
    citem->perms.current = item.current_perms;
    citem->perms.base = item.base_perms;
    citem->perms.everyone = item.everyone_perms;
    citem->perms.group = item.group_perms;
    citem->asset_type = item.asset_type;
    uuid_copy(citem->asset_id, item.asset_id);
    uuid_copy(citem->group_id, item.group_id);
    citem->group_owned = item.group_owned;
    citem->sale_price = item.sale_price;
    citem->sale_type = item.sale_type;
    citem->flags = item.flags;
    citem->creation_date = item.creation_date;
    
    xmlFree(item.name); xmlFree(item.description);
    xmlFree(item.creator_id);
}

static void inv_item_to_opensim(const inventory_item *citem,
				os_inv_item &item) {
  item.name = citem->name;
  item.description = citem->description;
  item.creator_id = citem->creator_id;
  uuid_copy(item.item_id, citem->item_id);
  uuid_copy(item.owner_id, citem->owner_id);
  item.inv_type = citem->inv_type;
  uuid_copy(item.folder_id, citem->folder_id);
  uuid_copy(item.creator_as_uuid, citem->creator_as_uuid);
  item.next_perms = citem->perms.next;
  item.current_perms = citem->perms.current;
  item.base_perms = citem->perms.base;
  item.group_perms = citem->perms.group;
  item.everyone_perms = citem->perms.everyone;
  item.asset_type = citem->asset_type;
  uuid_copy(item.asset_id, citem->asset_id);
  uuid_copy(item.group_id, citem->group_id);
  item.group_owned = citem->group_owned;
  item.sale_price = citem->sale_price;
  item.sale_type = citem->sale_type;
  item.flags = citem->flags;
  item.creation_date = citem->creation_date;
}

static void conv_inv_item_from_opensim(inventory_item *citem,
				       os_inv_item &item) {
  citem->name = strdup(item.name);
  citem->description = strdup(item.description);
  citem->creator_id = strdup(item.creator_id);
  fill_inv_item_from_opensim(citem, item);
}

static void free_inv_item_from_opensim(inventory_item *citem) {
  free(citem->name); free(citem->description); free(citem->creator_id);
}

static void parse_inv_items(xmlDocPtr doc, xmlNodePtr node,
			      struct inventory_contents* inv) {
  for( ; node != NULL; node = node->next) {
    if(!check_node(node,"InventoryItemBase")) continue;

    os_inv_item item;
    if(!osglue_deserialise_xml(doc, node->children, deserialise_inv_item,
			       &item)) continue;

    // DEBUG
    char buf[40];
    uuid_unparse(item.item_id, buf);
    printf("Inventory item: %s [%s]\n", item.name, buf);

    inventory_item *citem = caj_add_inventory_item(inv, item.name, 
						   item.description,
						   item.creator_id);
    fill_inv_item_from_opensim(citem, item);
    if(citem != NULL)
      printf("  DEBUG: item flags 0x%x\n",(unsigned)citem->flags);
  }
}

struct inv_items_req {
  user_grid_glue* user_glue;
  uuid_t folder_id;
  void(*cb)(struct inventory_contents* inv, 
	    void *cb_priv);
  void *cb_priv;
};

// FIXME - deal with whitespace!
static void got_inventory_items_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  //USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  inv_items_req *req = (inv_items_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  xmlDocPtr doc;
  xmlNodePtr node;
  struct inventory_contents* inv;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("ERROR: Inventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto fail;
  }

  printf("DEBUG: inventory folder content resp {{%s}}\n",
	 msg->response_body->data);

  doc = xmlReadMemory(msg->response_body->data,
		      msg->response_body->length,
		      "inventory.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: inventory XML parse failed\n");
    goto fail;    
  }
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "InventoryCollection") != 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto free_fail;
  }

  node = node->children;
  if(!check_node(node,"Folders")) goto free_fail;

  inv = caj_inv_new_contents_desc(req->folder_id);
  parse_inv_folders(doc, node->children, inv);

  node = node->next;
  if(!check_node(node,"Items")) goto free_inv_fail;
  parse_inv_items(doc, node->children, inv);

  // there's a UserID node next, but it's just the null UUID anyway.

  req->cb(inv, req->cb_priv);
  caj_inv_free_contents_desc(inv);
  xmlFreeDoc(doc);
  delete req;

  return;

 free_inv_fail:
  caj_inv_free_contents_desc(inv);
 free_fail:
  xmlFreeDoc(doc);
 fail:
  req->cb(NULL, req->cb_priv);
  delete req;
  printf("ERROR: inventory response parse failure\n");
  return;
}

// fetch contents of inventory folder
void fetch_inventory_folder(simgroup_ctx *sgrp, user_ctx *user,
			    void *user_priv, const uuid_t folder_id,
			    void(*cb)(struct inventory_contents* inv, 
				      void* priv),
			    void *cb_priv) {
  uuid_t u; char tmp[40]; char uri[256];
  GRID_PRIV_DEF_SGRP(sgrp);
  USER_PRIV_DEF(user_priv);
  xmlTextWriterPtr writer;
  xmlBufferPtr buf;
  SoupMessage *msg;
  inv_items_req *req;
  
  assert(grid->inventoryserver != NULL);

  buf = xmlBufferCreate();
  if(buf == NULL) goto fail;
  writer = xmlNewTextWriterMemory(buf, 0);
  if(writer == NULL) goto free_fail_1;
  
  if(xmlTextWriterStartDocument(writer,NULL,"UTF-8",NULL) < 0) 
    goto free_fail;
  if(xmlTextWriterStartElement(writer, BAD_CAST "RestSessionObjectOfGuid") < 0) 
    goto free_fail;
  user_get_session_id(user, u);
  uuid_unparse(u, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "SessionID",
				       "%s",tmp) < 0) goto free_fail;


  user_get_uuid(user, u);
  uuid_unparse(u, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "AvatarID",
				       "%s",tmp) < 0) goto free_fail;

  uuid_unparse(folder_id, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "Body",
				       "%s",tmp) < 0) goto free_fail;
  if(xmlTextWriterEndElement(writer) < 0) 
    goto free_fail;

  if(xmlTextWriterEndDocument(writer) < 0) {
    printf("DEBUG: couldn't end XML document\n"); goto fail;
  }

  // FIXME - don't use fixed-length buffer, and handle missing trailing /
  snprintf(uri, 256, "%sGetFolderContent/", grid->inventoryserver);
  printf("DEBUG: sending inventory request to %s\n", uri);
  msg = soup_message_new ("POST", uri);
  // FIXME - avoid unnecessary strlen
  soup_message_set_request (msg, "application/xml",
			    SOUP_MEMORY_COPY, (char*)buf->content, 
			    strlen ((char*)buf->content));
  req = new inv_items_req();
  req->user_glue = user_glue; req->cb = cb;
  req->cb_priv = cb_priv;
  uuid_copy(req->folder_id, folder_id);
  user_grid_glue_ref(user_glue);
  caj_queue_soup_message(sgrp, SOUP_MESSAGE(msg),
			 got_inventory_items_resp, req);
    
  xmlFreeTextWriter(writer);  
  xmlBufferFree(buf);
  return;

 free_fail:
  xmlFreeTextWriter(writer);  
 free_fail_1:
  xmlBufferFree(buf);
 fail:
  printf("DEBUG: ran into issues sending inventory request\n");
  cb(NULL, cb_priv);
  // FIXME - handle this
}

struct inv_item_req {
  user_grid_glue* user_glue;
  uuid_t item_id;
  void(*cb)(struct inventory_item* item, 
	    void *cb_priv);
  void *cb_priv;
};

// FIXME - deal with whitespace!
static void got_inventory_item_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  //USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  inv_item_req *req = (inv_item_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  xmlDocPtr doc;
  xmlNodePtr node;
  struct inventory_item invit;
  os_inv_item item;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("Inventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto fail;
  }

  printf("DEBUG: inventory item query resp {{%s}}\n",
	 msg->response_body->data);

  doc = xmlReadMemory(msg->response_body->data,
		      msg->response_body->length,
		      "inventory.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: inventory XML parse failed\n");
    goto fail;    
  }
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "InventoryItemBase") != 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto free_fail;
  }

  if(!osglue_deserialise_xml(doc, node->children, deserialise_inv_item,
			     &item)) {
    printf("ERROR: couldn't deserialise XML inventory item\n");
    goto free_fail;
  }

  conv_inv_item_from_opensim(&invit, item);
  
  req->cb(&invit, req->cb_priv);
  free_inv_item_from_opensim(&invit);
  xmlFreeDoc(doc);
  delete req;
  return;

 free_fail:
  xmlFreeDoc(doc);
 fail:
  req->cb(NULL, req->cb_priv);
  delete req;
  printf("ERROR: inventory item response parse failure\n");
  return;
}


void fetch_inventory_item(simgroup_ctx *sgrp, user_ctx *user,
			    void *user_priv, const uuid_t item_id,
			    void(*cb)(struct inventory_item* item, 
				      void* priv),
			    void *cb_priv) {
  uuid_t u, user_id; char tmp[40]; char uri[256];
  GRID_PRIV_DEF_SGRP(sgrp);
  USER_PRIV_DEF(user_priv);
  xmlTextWriterPtr writer;
  xmlBufferPtr buf;
  SoupMessage *msg;
  inv_item_req *req;
  struct os_inv_item invitem; // don't ask. Please.
  
  assert(grid->inventoryserver != NULL);

  buf = xmlBufferCreate();
  if(buf == NULL) goto fail;
  writer = xmlNewTextWriterMemory(buf, 0);
  if(writer == NULL) goto free_fail_1;
  
  if(xmlTextWriterStartDocument(writer,NULL,"UTF-8",NULL) < 0) 
    goto free_fail;
  if(xmlTextWriterStartElement(writer, BAD_CAST "RestSessionObjectOfInventoryItemBase") < 0) 
    goto free_fail;
  user_get_session_id(user, u);
  uuid_unparse(u, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "SessionID",
				       "%s",tmp) < 0) goto free_fail;


  user_get_uuid(user, user_id);
  uuid_unparse(user_id, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "AvatarID",
				       "%s",tmp) < 0) goto free_fail;

  // okay, this is just painful... we have to serialise an entire complex
  // object in this cruddy .Net XML serialisation format... and the only bit
  // they actually use or need is a single UUID. Bletch  *vomit*.
  if(xmlTextWriterStartElement(writer,BAD_CAST "Body") < 0) 
    goto free_fail;
  memset(&invitem, 0, sizeof(invitem));
  invitem.name = invitem.description = const_cast<char*>("");
  invitem.creator_id = const_cast<char*>("");
  uuid_copy(invitem.owner_id, user_id);
  uuid_copy(invitem.item_id, item_id);

  osglue_serialise_xml(writer, deserialise_inv_item, &invitem);

  if(xmlTextWriterEndElement(writer) < 0) 
    goto free_fail;

  // now we should be done serialising the XML crud.

  if(xmlTextWriterEndElement(writer) < 0) 
    goto free_fail;

  if(xmlTextWriterEndDocument(writer) < 0) {
    printf("DEBUG: couldn't end XML document\n"); goto fail;
  }

  // FIXME - don't use fixed-length buffer, and handle missing trailing /
  snprintf(uri, 256, "%sQueryItem/", grid->inventoryserver);
  printf("DEBUG: sending inventory request to %s\n", uri);
  msg = soup_message_new ("POST", uri);
  // FIXME - avoid unnecessary strlen
  soup_message_set_request (msg, "text/xml",
			    SOUP_MEMORY_COPY, (char*)buf->content, 
			    strlen ((char*)buf->content));
  req = new inv_item_req();
  req->user_glue = user_glue; req->cb = cb;
  req->cb_priv = cb_priv;
  uuid_copy(req->item_id, item_id);
  user_grid_glue_ref(user_glue);
  caj_queue_soup_message(sgrp, SOUP_MESSAGE(msg),
			 got_inventory_item_resp, req);
    
  xmlFreeTextWriter(writer);  
  xmlBufferFree(buf);
  return;

 free_fail:
  xmlFreeTextWriter(writer);  
 free_fail_1:
  xmlBufferFree(buf);
 fail:
  printf("DEBUG: ran into issues sending inventory QueryItem request\n");
  cb(NULL, cb_priv);
  // FIXME - handle this
  
}

struct add_inv_item_req {
  user_grid_glue* user_glue;
  uuid_t item_id;
  void(*cb)(void *cb_priv, int success, uuid_t item_id);
  void *cb_priv;
};

// FIXME - most of this should be factored out into boilerplate code
static void got_add_inv_item_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  //USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  uuid_t u;  char* s;
  add_inv_item_req *req = (add_inv_item_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  xmlDocPtr doc;
  xmlNodePtr node;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("Inventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto fail;
  }

  printf("DEBUG: inventory item add resp {{%s}}\n",
	 msg->response_body->data);

  doc = xmlReadMemory(msg->response_body->data,
		      msg->response_body->length,
		      "inventory_resp.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: inventory XML response parse failed\n");
    goto fail;    
  }
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "boolean") != 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto free_fail;
  }

  s = (char*)xmlNodeListGetString(doc, node->children, 1);
  req->cb(req->cb_priv, s != NULL && strcmp(s, "true") == 0,
	  req->item_id);
  xmlFree(s);
  xmlFreeDoc(doc);
  delete req;
  return;

 free_fail:
  xmlFreeDoc(doc);
 fail:
  uuid_clear(u); req->cb(req->cb_priv, FALSE, u);
  delete req;
  printf("ERROR: add inventory item response parse failure\n");
  return;
}

void add_inventory_item(simgroup_ctx *sgrp, user_ctx *user,
			void *user_priv, inventory_item *inv,
			void(*cb)(void* priv, int success, uuid_t item_id),
			void *cb_priv) {
  uuid_t u, user_id; char tmp[40]; char uri[256];
  GRID_PRIV_DEF_SGRP(sgrp);
  USER_PRIV_DEF(user_priv);
  xmlTextWriterPtr writer;
  xmlBufferPtr buf;
  SoupMessage *msg;
  add_inv_item_req *req;
  struct os_inv_item invitem;
  
  assert(grid->inventoryserver != NULL);

  buf = xmlBufferCreate();
  if(buf == NULL) goto fail;
  writer = xmlNewTextWriterMemory(buf, 0);
  if(writer == NULL) goto free_fail_1;
  
  if(xmlTextWriterStartDocument(writer,NULL,"UTF-8",NULL) < 0) 
    goto free_fail;
  if(xmlTextWriterStartElement(writer, BAD_CAST "RestSessionObjectOfInventoryItemBase") < 0) 
    goto free_fail;
  user_get_session_id(user, u);
  uuid_unparse(u, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "SessionID",
				       "%s",tmp) < 0) goto free_fail;


  user_get_uuid(user, user_id);
  uuid_unparse(user_id, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "AvatarID",
				       "%s",tmp) < 0) goto free_fail;

  if(xmlTextWriterStartElement(writer,BAD_CAST "Body") < 0) 
    goto free_fail;

  inv_item_to_opensim(inv, invitem);
  osglue_serialise_xml(writer, deserialise_inv_item, &invitem);

  if(xmlTextWriterEndElement(writer) < 0) 
    goto free_fail;

  if(xmlTextWriterEndElement(writer) < 0) 
    goto free_fail;

  if(xmlTextWriterEndDocument(writer) < 0) {
    printf("DEBUG: couldn't end XML document\n"); goto fail;
  }

  // FIXME - don't use fixed-length buffer, and handle missing trailing /
  // (note: not AddNewItem, as that's not meant for sim use!)
  snprintf(uri, 256, "%sNewItem/", grid->inventoryserver);
  printf("DEBUG: sending inventory add request to %s\n", uri);
  msg = soup_message_new ("POST", uri);
  // FIXME - avoid unnecessary strlen
  soup_message_set_request (msg, "text/xml",
			    SOUP_MEMORY_COPY, (char*)buf->content, 
			    strlen ((char*)buf->content));
  req = new add_inv_item_req();
  req->user_glue = user_glue; req->cb = cb;
  req->cb_priv = cb_priv;
  uuid_copy(req->item_id, inv->item_id);
  user_grid_glue_ref(user_glue);
  caj_queue_soup_message(sgrp, SOUP_MESSAGE(msg),
			 got_add_inv_item_resp, req);
    
  xmlFreeTextWriter(writer);  
  xmlBufferFree(buf);
  return;

 free_fail:
  xmlFreeTextWriter(writer);  
 free_fail_1:
  xmlBufferFree(buf);
 fail:
  printf("DEBUG: ran into issues sending inventory NewItem request\n");
  uuid_clear(u); cb(cb_priv, FALSE, u);
}

// FIXME - deal with whitespace!
static void got_system_folders_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  //USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  inv_items_req *req = (inv_items_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  user_ctx* ctx = user_glue->ctx;
  xmlDocPtr doc;
  xmlNodePtr node;
  struct inventory_contents* inv;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("System folders request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    goto fail;
  }

  printf("DEBUG: system folders resp {{%s}}\n",
	 msg->response_body->data);


  doc = xmlReadMemory(msg->response_body->data,
		      msg->response_body->length,
		      "system_folders.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: system folders XML parse failed\n");
    goto fail;    
  }
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "ArrayOfInventoryFolderBase") != 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto free_fail;
  }

  // FIXME - proper error handling!
  if(ctx != NULL) {
    inv = caj_inv_new_contents_desc(req->folder_id);
    parse_inv_folders(doc, node->children, inv);
    user_set_system_folders(ctx, inv);
  }

  xmlFreeDoc(doc); delete req;
  return;

 free_fail:
  xmlFreeDoc(doc);
 fail:
  // FIXME - handle this!
  delete req;
  printf("ERROR: request for system folders failed\n");
  if(ctx != NULL) user_set_system_folders(ctx, NULL);
  return;
}

// fetch contents of inventory folder
void fetch_system_folders(simgroup_ctx *sgrp, user_ctx *user,
			  void *user_priv) {
  uuid_t u; char tmp[40]; char uri[256];
  GRID_PRIV_DEF_SGRP(sgrp);
  USER_PRIV_DEF(user_priv);
  xmlTextWriterPtr writer;
  xmlBufferPtr buf;
  SoupMessage *msg;
  inv_items_req *req;
  
  assert(grid->inventoryserver != NULL);

  buf = xmlBufferCreate();
  if(buf == NULL) goto fail;
  writer = xmlNewTextWriterMemory(buf, 0);
  if(writer == NULL) goto free_fail_1;
  
  if(xmlTextWriterStartDocument(writer,NULL,"UTF-8",NULL) < 0) 
    goto free_fail;
  if(xmlTextWriterStartElement(writer, BAD_CAST "RestSessionObjectOfGuid") < 0) 
    goto free_fail;
  user_get_session_id(user, u);
  uuid_unparse(u, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "SessionID",
				       "%s",tmp) < 0) goto free_fail;


  user_get_uuid(user, u);
  uuid_unparse(u, tmp);
  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "AvatarID",
				       "%s",tmp) < 0) goto free_fail;

  if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "Body",
				       "%s",tmp) < 0) goto free_fail;
  if(xmlTextWriterEndElement(writer) < 0) 
    goto free_fail;

  if(xmlTextWriterEndDocument(writer) < 0) {
    printf("DEBUG: couldn't end XML document\n"); goto fail;
  }

  // FIXME - don't use fixed-length buffer, and handle missing trailing /
  snprintf(uri, 256, "%sSystemFolders/", grid->inventoryserver);
  printf("DEBUG: sending inventory request to %s\n", uri);
  msg = soup_message_new ("POST", uri);
  // FIXME - avoid unnecessary strlen
  soup_message_set_request (msg, "application/xml",
			    SOUP_MEMORY_COPY, (char*)buf->content, 
			    strlen ((char*)buf->content));
  req = new inv_items_req();
  req->user_glue = user_glue; 
  user_grid_glue_ref(user_glue);
  caj_queue_soup_message(sgrp, SOUP_MESSAGE(msg),
			 got_system_folders_resp, req);
    
  xmlFreeTextWriter(writer);  
  xmlBufferFree(buf);
  return;

 free_fail:
  xmlFreeTextWriter(writer);  
 free_fail_1:
  xmlBufferFree(buf);
 fail:
  printf("DEBUG: ran into issues sending inventory request\n");
  // FIXME - handle this
}

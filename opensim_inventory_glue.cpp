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
#include "opensim_grid_glue.h"
#include <cassert>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <stddef.h>

#if 0 // FIXME - add support for falling back to this method?
void fetch_user_inventory(simulator_ctx *sim, user_ctx *user,
			  void *user_priv) {
  uuid_t u; char tmp[40];
  GRID_PRIV_DEF(sim);
  USER_PRIV_DEF(user_priv);
  xmlTextWriterPtr writer;
  xmlBufferPtr buf;
  SoupMessage *msg;
  
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

  /* Yes, we really do send the avatar ID twice */
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

  // FIXME - don't hardcode this
  msg = soup_message_new ("POST", "http://127.0.0.1:8003/GetInventory/");
  // FIXME - avoid unnecessary strlen
  soup_message_set_request (msg, "application/xml",
			    SOUP_MEMORY_COPY, (char*)buf->content, 
			    strlen ((char*)buf->content));
  user_grid_glue_ref(user_glue);
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
			 got_user_inventory_resp, user_glue);
    
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
#endif

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

static int node_get_uuid(xmlDocPtr doc, xmlNodePtr node, const char* name,
			 uuid_t u) {
  char *s;
  if(!check_node(node, name)) return 0;
  node = node->children;
  if(!check_node(node, "Guid")) return 0;
  s = (char*)xmlNodeListGetString(doc, node->children, 1);
  if(uuid_parse(s, u)) {
    printf("ERROR: couldn't parse UUID from serialised XML\n");
    xmlFree(s); return 0;
  }
  xmlFree(s);
  return 1;
}

#define XML_STYPE_SKIP 0
#define XML_STYPE_STRING 1
#define XML_STYPE_UUID 2
#define XML_STYPE_INT 3

struct xml_serialisation_desc {
  const char* name;
  int type;
  size_t offset;
};

// for internal use of deserialise_xml only
static void free_partial_deserial_xml(xml_serialisation_desc* serial,
				      void* out, int cnt) {
  unsigned char* outbuf = (unsigned char*)out;
  for(int i = 0; i < cnt; i++) {
    switch(serial[i].type) {
    case XML_STYPE_STRING:
      xmlFree(*(char**)(outbuf+serial[i].offset));
      break;
    default:
      break;
    }
  }
}

static int deserialise_xml(xmlDocPtr doc, xmlNodePtr node,
			   xml_serialisation_desc* serial,
			   void* out) {
  unsigned char* outbuf = (unsigned char*)out;
  for(int i = 0; serial[i].name != NULL; i++) {
    if(!check_node(node,serial[i].name)) {
      free_partial_deserial_xml(serial, out, i);
      return 0;
    }
    switch(serial[i].type) {
    case XML_STYPE_SKIP:
      break; // do nothing.
    case XML_STYPE_STRING:
      *(char**)(outbuf+serial[i].offset) = 
	(char*)xmlNodeListGetString(doc, node->children, 1);
      break;
    case XML_STYPE_UUID:
      {
	xmlNodePtr guid = node->children;
	if(!check_node(guid, "Guid")) {
	  free_partial_deserial_xml(serial, out, i);
	  return 0;
	}
	char *s = (char*)xmlNodeListGetString(doc, guid->children, 1);
	// FIXME - handle whitespace!
	if(s == NULL || uuid_parse(s, (unsigned char*)(outbuf+serial[i].offset))) {
	  printf("ERROR: couldn't parse UUID for %s", serial[i].name);
	  xmlFree(s); free_partial_deserial_xml(serial, out, i);
	  return 0;
	}
	xmlFree(s);
      }
      break;
    case XML_STYPE_INT:
      {
	char *s = (char*)xmlNodeListGetString(doc, node->children, 1);
	*(int*)(outbuf+serial[i].offset) = atoi(s);
	xmlFree(s);
      }
      break;
    default:
      printf("ERROR: bad type passed to deserialise_xml");
      free_partial_deserial_xml(serial, out, i);
      return 0;
    }

    node = node->next;
  }
  return 1;
}

static int serialise_xml(xmlTextWriterPtr writer,
			 xml_serialisation_desc* serial,
			 void* in) {
  unsigned char* inbuf = (unsigned char*)in;
  for(int i = 0; serial[i].name != NULL; i++) {
    /* if(!check_node(node,serial[i].name)) {
      free_partial_deserial_xml(serial, out, i);
      return 0;
      } */
    switch(serial[i].type) {
    case XML_STYPE_STRING:
      if(xmlTextWriterWriteFormatElement(writer,BAD_CAST serial[i].name,
				       "%s",*(char**)(inbuf+serial[i].offset)) < 0) 
	return 0;
      break;
    case XML_STYPE_UUID:
      {
	char buf[40]; uuid_unparse((unsigned char*)(inbuf+serial[i].offset), buf);
	 if(xmlTextWriterStartElement(writer, BAD_CAST serial[i].name) < 0) 
	   return 0;
	if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "Guid",
					   "%s",buf) < 0) 
	  return 0;
	if(xmlTextWriterEndElement(writer) < 0) 
	  return 0;
      }
      break;
    case XML_STYPE_INT:
      {
	 if(xmlTextWriterWriteFormatElement(writer,BAD_CAST serial[i].name,
					    "%i",*(int*)(inbuf+serial[i].offset)) < 0) 
	   return 0;
      }
      break;
    case XML_STYPE_SKIP:
    default:
      printf("ERROR: bad type passed to deserialise_xml");
      return 0;
    }
  }
  return 1;
}

struct os_inv_folder {
  char *name;
  uuid_t folder_id, owner_id, parent_id;
  int inv_type;
  int version; // FIXME - use long/int32_t?
};

xml_serialisation_desc deserialise_inv_folder[] = {
  { "Name", XML_STYPE_STRING, offsetof(os_inv_folder, name) },
  { "ID", XML_STYPE_UUID, offsetof(os_inv_folder, folder_id) },
  { "Owner", XML_STYPE_UUID,offsetof(os_inv_folder, owner_id) },
  { "ParentID", XML_STYPE_UUID,offsetof(os_inv_folder, parent_id) },
  { "Type", XML_STYPE_INT,offsetof(os_inv_folder, inv_type) },
  { "Version", XML_STYPE_INT,offsetof(os_inv_folder, version) },
  { NULL, }
};

// FIXME - deal with whitespace!
static void parse_inv_folders(xmlDocPtr doc, xmlNodePtr node,
			      struct inventory_contents* inv) {
  for( ; node != NULL; node = node->next) {
    if(!check_node(node,"InventoryFolderBase")) continue;

    os_inv_folder folder;
    if(!deserialise_xml(doc, node->children, deserialise_inv_folder,
			&folder)) continue;

    // DEBUG
    char buf[40];
    uuid_unparse(folder.folder_id, buf);
    printf("Inventory folder: %s [%s]\n", folder.name, buf);

    // FIXME - need to check parent ID matches expected one
    caj_inv_add_folder(inv, folder.folder_id, folder.owner_id, 
		       folder.name, folder.inv_type);
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
  // int group_owned; // TODO
  int sale_price; // FIXME - should be int32_t
  int sale_type;
  int flags; // FIXME - should be uint32_t
  int creation_date; // FIXME - should be int32_t (or larger?)
};

xml_serialisation_desc deserialise_inv_item[] = {
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
  { "GroupOwned", XML_STYPE_SKIP, 0 }, // FIXME
  { "SalePrice", XML_STYPE_INT,offsetof(os_inv_item, sale_price) },  
  { "SaleType", XML_STYPE_INT,offsetof(os_inv_item, sale_type) },  
  { "Flags", XML_STYPE_INT,offsetof(os_inv_item, flags) },  
  { "CreationDate", XML_STYPE_INT,offsetof(os_inv_item, creation_date) },  
  { NULL, }
};


static void parse_inv_items(xmlDocPtr doc, xmlNodePtr node,
			      struct inventory_contents* inv) {
  for( ; node != NULL; node = node->next) {
    if(!check_node(node,"InventoryItemBase")) continue;

    os_inv_item item;
    if(!deserialise_xml(doc, node->children, deserialise_inv_item,
			&item)) continue;

    // DEBUG
    char buf[40];
    uuid_unparse(item.item_id, buf);
    printf("Inventory item: %s [%s]\n", item.name, buf);

    inventory_item *citem = caj_add_inventory_item(inv, item.name, 
						   item.description,
						   item.creator_id);
 
    uuid_copy(citem->item_id, item.item_id);
    uuid_copy(citem->owner_id, item.owner_id);
    citem->inv_type = item.inv_type;
    uuid_copy(citem->folder_id, item.folder_id);
    uuid_copy(citem->creator_as_uuid, item.creator_as_uuid);
    citem->next_perms = item.next_perms;
    citem->current_perms = item.current_perms;
    citem->base_perms = item.base_perms;
    citem->everyone_perms = item.everyone_perms;
    citem->group_perms = item.group_perms;
    citem->asset_type = item.asset_type;
    uuid_copy(citem->asset_id, item.asset_id);
    uuid_copy(citem->group_id, item.group_id);
    citem->group_owned = 0; // FIXME;
    citem->sale_price = item.sale_price;
    citem->sale_type = item.sale_type;
    citem->flags = item.flags;
    citem->creation_date = item.creation_date;
    
    xmlFree(item.name); xmlFree(item.description);
    xmlFree(item.creator_id);
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
    printf("Inventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
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
void fetch_inventory_folder(simulator_ctx *sim, user_ctx *user,
			    void *user_priv, uuid_t folder_id,
			    void(*cb)(struct inventory_contents* inv, 
				      void* priv),
			    void *cb_priv) {
  uuid_t u; char tmp[40]; char uri[256];
  GRID_PRIV_DEF(sim);
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
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
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
  inv_items_req *req = (inv_items_req*)user_data;
  user_grid_glue* user_glue = req->user_glue;
  xmlDocPtr doc;
  xmlNodePtr node;
  struct inventory_item invit;
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
  if(strcmp((char*)node->name, "???") != 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto free_fail;
  }

  node = node->children;
  if(!check_node(node,"Folders")) goto free_fail;

#if 0
  req->cb(invit, req->cb_priv);
  xmlFreeDoc(doc);
  delete req;
  return;
#endif

 free_fail:
  xmlFreeDoc(doc);
 fail:
  req->cb(NULL, req->cb_priv);
  delete req;
  printf("ERROR: inventory item response parse failure\n");
  return;
}


void fetch_inventory_item(simulator_ctx *sim, user_ctx *user,
			    void *user_priv, uuid_t item_id,
			    void(*cb)(struct inventory_item* item, 
				      void* priv),
			    void *cb_priv) {
  uuid_t u; char tmp[40]; char uri[256];
  GRID_PRIV_DEF(sim);
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

  // okay, this is just painful... we have to serialise an entire complex
  // object in this cruddy .Net XML serialisation format... and the only bit
  // they actually use or need is a single UUID. Bletch  *vomit*.
  if(xmlTextWriterStartElement(writer,BAD_CAST "InventoryItemBase") < 0) 
    goto free_fail;
  memset(&invitem, 0, sizeof(invitem));
  invitem.name = ""; invitem.description = ""; invitem.creator_id = "";
  uuid_copy(invitem.item_id, item_id);

  serialise_xml(writer, deserialise_inv_item, &invitem);

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
  soup_message_set_request (msg, "application/xml",
			    SOUP_MEMORY_COPY, (char*)buf->content, 
			    strlen ((char*)buf->content));
  req = new inv_item_req();
  req->user_glue = user_glue; req->cb = cb;
  req->cb_priv = cb_priv;
  uuid_copy(req->item_id, item_id);
  user_grid_glue_ref(user_glue);
  sim_queue_soup_message(sim, SOUP_MESSAGE(msg),
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

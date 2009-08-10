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
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>

static void got_user_inventory_resp(SoupSession *session, SoupMessage *msg, gpointer user_data) {
  USER_PRIV_DEF(user_data);
  //GRID_PRIV_DEF(sim);
  xmlDocPtr doc;
  xmlNodePtr node;
  user_grid_glue_deref(user_glue);
  if(msg->status_code != 200) {
    printf("Inventory request failed: got %i %s\n",(int)msg->status_code,msg->reason_phrase);
    return;
  }

  printf("DEBUG: inventory response {{%s}}\n",
	 msg->response_body->data);

  doc = xmlReadMemory(msg->response_body->data,
		      msg->response_body->length,
		      "inventory.xml", NULL, 0);
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "InventoryCollection") == 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto fail;
  }

  fail:
    printf("ERROR: inventory response parse failure\n");
    return;
}


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

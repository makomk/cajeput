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
#include "opensim_grid_glue.h"
#include "opensim_xml_glue.h" // for asset parsing
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <cassert>
#include "caj_llsd.h" // for a dubious hack

struct asset_req_desc {
  struct simgroup_ctx *sgrp;
  simple_asset *asset;
};

struct os_asset {
  caj_string data;
  uuid_t full_id;
  char *id, *name, *description;
  int type;
  int local, temporary; // bool
};

static xml_serialisation_desc deserialise_asset[] = {
  { "Data", XML_STYPE_BASE64, offsetof(os_asset, data) },
  { "FullID", XML_STYPE_UUID, offsetof(os_asset, full_id) },
  { "ID", XML_STYPE_STRING, offsetof(os_asset, id) },
  { "Name", XML_STYPE_STRING, offsetof(os_asset, name) },
  { "Description", XML_STYPE_STRING, offsetof(os_asset, description) },
  { "Type", XML_STYPE_INT, offsetof(os_asset, type) },
  { "Local", XML_STYPE_BOOL, offsetof(os_asset, local) },
  { "Temporary", XML_STYPE_BOOL, offsetof(os_asset, temporary) },
  { NULL, }
};

static void get_asset_resp(SoupSession *session, SoupMessage *msg, 
			   gpointer user_data) {
  asset_req_desc* req = (asset_req_desc*)user_data;
  simple_asset *asset = req->asset;
  xmlDocPtr doc; os_asset oasset;
  oasset.data.data = NULL;
  /* const char* content_type = 
     soup_message_headers_get_content_type(msg->response_headers, NULL); */
  caj_shutdown_release(req->sgrp);

  printf("Get asset resp: got %i %s (len %i)\n",
	 (int)msg->status_code, msg->reason_phrase, 
	 (int)msg->response_body->length);
  // printf("{%s}\n", msg->response_body->data);

  if(msg->status_code == 200) {
    doc = xmlReadMemory(msg->response_body->data,
				  msg->response_body->length,"asset.xml",
				  NULL,0);
    if(doc == NULL) {
      printf("ERROR: XML parse failed for asset\n");
      goto out_fail;
    }

    xmlNodePtr node = xmlDocGetRootElement(doc)->children;
    while(node != NULL && (node->type == XML_TEXT_NODE || 
			   node->type == XML_COMMENT_NODE))
      node = node->next;

    if(!osglue_deserialise_xml(doc, node, deserialise_asset,
			       &oasset)) {
      printf("ERROR: couldn't deserialise asset XML\n");
      goto out_fail_free;
    }

    if(uuid_compare(oasset.full_id, asset->id) != 0) {
      printf("WARNING: returned asset from asset server has unexpected ID\n");
    }
      
    caj_string_steal(&asset->data, &oasset.data);
    asset->name = strdup(oasset.name);
    asset->description = strdup(oasset.description);
    asset->type = oasset.type;
    // FIXME - do something with local & temporary flags?
    xmlFree(oasset.name); xmlFree(oasset.description);
    xmlFree(oasset.id); // FIXME - do something with this?

    xmlFreeDoc(doc);
    caj_asset_finished_load(req->sgrp, asset, TRUE);
    delete req;
    return;
  } else {
    goto out_fail;
  }

 out_fail_free:
   xmlFreeDoc(doc);
 out_fail:
  caj_asset_finished_load(req->sgrp, asset, FALSE);
  delete req;
}

void osglue_get_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset) {
  GRID_PRIV_DEF_SGRP(sgrp);
  // FIXME - should allocate proper buffer
  char url[255], asset_id[40];
  assert(grid->assetserver != NULL);

  uuid_unparse(asset->id, asset_id);
  snprintf(url, 255, "%sassets/%s/", grid->assetserver, asset_id);
  printf("DEBUG: requesting asset %s\n", url);

  SoupMessage *msg = soup_message_new ("GET", url);
  asset_req_desc *req = new asset_req_desc;
  req->asset = asset; req->sgrp = sgrp;
  caj_shutdown_hold(sgrp); // not sure we want to do this
  caj_queue_soup_message(sgrp, msg, get_asset_resp, req);
  
}

struct put_asset_req_desc {
  struct simgroup_ctx *sgrp;
  uuid_t asset_id;
  caj_put_asset_cb cb; 
  void *cb_priv;
};


static void put_asset_resp(SoupSession *session, SoupMessage *msg, 
			   gpointer user_data) {
  uuid_t asset_id;
  put_asset_req_desc* req = (put_asset_req_desc*)user_data;
  /* const char* content_type = 
     soup_message_headers_get_content_type(msg->response_headers, NULL); */
  caj_shutdown_release(req->sgrp);

  printf("Upload asset resp: got %i %s (len %i)\n",
	 (int)msg->status_code, msg->reason_phrase, 
	 (int)msg->response_body->length);
  printf("{%s}\n", msg->response_body->data);

  if(msg->status_code == 200) {
    uuid_copy(asset_id, req->asset_id);
    xmlDocPtr doc = xmlReadMemory(msg->response_body->data,
				  msg->response_body->length,"asset_resp.xml",
				  NULL,0);
    if(doc != NULL) {
      xmlNodePtr node = xmlDocGetRootElement(doc);
      if(strcmp((char*)node->name, "string") != 0) {
	printf("ERROR: unexpected XML response from asset upload\n");
	uuid_clear(asset_id);
      } else {
	xmlChar *text = xmlNodeListGetString(doc, node->children, 1);
	if(text == NULL || uuid_parse((char*)text, asset_id)) {
	  printf("ERROR: bad XML response from asset upload\n");
	  uuid_clear(asset_id);
	}
	xmlFree(text);
      }
      xmlFreeDoc(doc);
    }
    req->cb(asset_id, req->cb_priv);
  } else {
    printf("ERROR: asset upload failed\n");
    uuid_clear(asset_id);
    req->cb(asset_id, req->cb_priv);
  }
  delete req;
}

void osglue_put_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset,
		      caj_put_asset_cb cb, void *cb_priv) {
  GRID_PRIV_DEF_SGRP(sgrp);
  // FIXME - should allocate proper buffer
  xmlTextWriterPtr writer;
  xmlBufferPtr buf;
  SoupMessage *msg; put_asset_req_desc *req;
  os_asset oasset;
  char url[255], asset_id[40];
  assert(grid->assetserver != NULL);
  assert(asset != NULL && asset->data.data != NULL);

  buf = xmlBufferCreate();
  if(buf == NULL) return;
  writer = xmlNewTextWriterMemory(buf, 0);
  if(writer == NULL) {
    xmlBufferFree(buf);
    return;
  }

  uuid_unparse(asset->id, asset_id);
  snprintf(url, 255, "%sassets/", grid->assetserver);
  printf("DEBUG: uploading asset %s\n", asset_id);
  
  if(xmlTextWriterStartDocument(writer,NULL,"UTF-8",NULL) < 0) {
    printf("DEBUG: couldn't start XML document\n"); goto fail;
  }
  
  if(xmlTextWriterStartElement(writer, BAD_CAST "AssetBase") < 0) goto fail;  

  oasset.data = asset->data;
  uuid_copy(oasset.full_id, asset->id);
  oasset.id = asset_id;
  oasset.name = asset->name; oasset.description = asset->description;
  oasset.type = asset->type;
  oasset.local = FALSE; oasset.temporary = FALSE;
  if(!osglue_serialise_xml(writer, deserialise_asset, &oasset)) {

    printf("DEBUG: error serialising asset\n"); goto fail;
  }
    
  if(xmlTextWriterEndElement(writer) < 0) {
    printf("DEBUG: error writing end element\n"); goto fail;
  }
  
  if(xmlTextWriterEndDocument(writer) < 0) {
    printf("DEBUG: couldn't end XML document\n"); goto fail;
    
  }

  msg = soup_message_new ("POST", url);
  req = new put_asset_req_desc;
  req->sgrp = sgrp;
  uuid_copy(req->asset_id, asset->id);
  req->cb = cb; req->cb_priv = cb_priv;
  caj_shutdown_hold(sgrp);
  caj_queue_soup_message(sgrp, msg, put_asset_resp, req);
  
  soup_message_set_request (msg, "text/xml",
			    SOUP_MEMORY_COPY, (char*)buf->content, 
			    buf->use);

  xmlFreeTextWriter(writer);
  xmlBufferFree(buf);
  return;


 fail:
  xmlFreeTextWriter(writer);
  xmlBufferFree(buf);
  printf("ERROR: couldn't format asset store request\n");
  return;
}

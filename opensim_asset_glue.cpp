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
#include "cajeput_grid_glue.h"
#include <libsoup/soup.h>
#include "caj_types.h"
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include "opensim_grid_glue.h"
#include "opensim_xml_glue.h" // for asset parsing
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <cassert>

struct asset_req_desc {
  struct simgroup_ctx *sgrp;
  texture_desc *texture;
  simple_asset *asset;
};

static void get_texture_resp(SoupSession *session, SoupMessage *msg, 
			     gpointer user_data) {
  asset_req_desc* req = (asset_req_desc*)user_data;
  texture_desc *texture = req->texture;
  const char* content_type = 
    soup_message_headers_get_content_type(msg->response_headers, NULL);
  caj_shutdown_release(req->sgrp);

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
    caj_texture_finished_load(texture);
  } else {
    // HACK!
    texture->flags |= CJP_TEXTURE_MISSING;
  }

  out_fail:
  texture->flags &= ~CJP_TEXTURE_PENDING;
  delete req;
}

void osglue_get_texture(struct simgroup_ctx *sgrp, struct texture_desc *texture) {
  GRID_PRIV_DEF_SGRP(sgrp);
  // FIXME - should allocate proper buffer
  char url[255], asset_id[40];
  assert(grid->assetserver != NULL);

  uuid_unparse(texture->asset_id, asset_id);
  snprintf(url, 255, "%sassets/%s/data", grid->assetserver, asset_id);
  printf("DEBUG: requesting texture asset %s\n", url);

  SoupMessage *msg = soup_message_new ("GET", url);
  asset_req_desc *req = new asset_req_desc;
  req->texture = texture; req->sgrp = sgrp;
  caj_shutdown_hold(sgrp); // FIXME - probably don't want to do this
  caj_queue_soup_message(sgrp, msg,
			 get_texture_resp, req);

}

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

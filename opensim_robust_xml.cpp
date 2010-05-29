/* Copyright (c) 2010 Aidan Thornton, all rights reserved.
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

/* Deserialisation of Melanie's odd XML format used by the new ROBUST
   services for OpenSim. Fun. */

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <string.h>
#include <strings.h>
#include <glib.h>
#include "opensim_robust_xml.h"
#include <cassert>

void os_robust_xml_free(os_robust_xml *rxml) {
  if(rxml == NULL) return;
  switch(rxml->node_type) {
  case OS_ROBUST_XML_STR:
    xmlFree(rxml->u.s); break;
  case OS_ROBUST_XML_LIST:
    g_hash_table_destroy(rxml->u.hashtbl); break;
  }
  delete rxml;
}

os_robust_xml *os_robust_xml_lookup(os_robust_xml *rxml, const char* key) {
  assert(rxml != NULL); assert(rxml->node_type == OS_ROBUST_XML_LIST);
  return (os_robust_xml*)g_hash_table_lookup(rxml->u.hashtbl, (const xmlChar*)key);
}

char* os_robust_xml_lookup_str(os_robust_xml *rxml, const char* key) {
  assert(rxml != NULL); assert(rxml->node_type == OS_ROBUST_XML_LIST);
  os_robust_xml *node = (os_robust_xml*)g_hash_table_lookup(rxml->u.hashtbl, (const xmlChar*)key);
  if(node == NULL || node->node_type != OS_ROBUST_XML_STR) return NULL;
  else return node->u.s;
}

void os_robust_xml_iter_begin(GHashTableIter *iter, os_robust_xml *rxml) {
  assert(rxml != NULL); assert(rxml->node_type == OS_ROBUST_XML_LIST);
  g_hash_table_iter_init(iter, rxml->u.hashtbl);
}

int os_robust_xml_iter_next(GHashTableIter *iter, const char** key,
			    os_robust_xml **value) {
  // FIXME - dubious casting...
  return g_hash_table_iter_next(iter, (gpointer*)key, (gpointer*)value);
}

static void rxml_destroy_key(gpointer data) {
  xmlFree(data);
}

static void rxml_destroy_value(gpointer data) {
  os_robust_xml_free((os_robust_xml*)data);
}

// FIXME - TODO - recursion limit
static os_robust_xml *deserialise_dict(xmlDocPtr doc, xmlNodePtr node) {
  os_robust_xml *ret = new os_robust_xml();
  ret->node_type = OS_ROBUST_XML_LIST;

  // FIXME - TODO - memory management
  ret->u.hashtbl = g_hash_table_new_full(g_str_hash, g_str_equal,
					 rxml_destroy_key, rxml_destroy_value);

  for(;;) {
    while(node != NULL && (node->type == XML_TEXT_NODE || 
			 node->type == XML_COMMENT_NODE))
      node = node->next;
    if(node == NULL) break;

    os_robust_xml *rnode = NULL;
    xmlChar *nodetype = xmlGetProp(node,(const xmlChar*)"type");
    if(nodetype != NULL && strcasecmp((char*)nodetype, "List") == 0) {
      rnode = deserialise_dict(doc, node->children);
    } else {
      rnode = new os_robust_xml();
      rnode->node_type = OS_ROBUST_XML_STR;
      rnode->u.s = (char*)xmlNodeListGetString(doc, node->children, 1);
      if(rnode->u.s == NULL) 
	rnode->u.s = (char*)xmlStrdup((const xmlChar*)"");
    }
    xmlFree(nodetype);

    // FIXME - handle failure (rnode == NULL)?

    g_hash_table_insert(ret->u.hashtbl, xmlStrdup(node->name),
			rnode);

    node = node->next;
  }
  return ret;
}

os_robust_xml *os_robust_xml_deserialise(const char *data, int len) {
  xmlDocPtr doc; xmlNodePtr node;
  struct os_robust_xml *ret = NULL;

  doc = xmlReadMemory(data, len, "robust.xml", NULL, 0);
  if(doc == NULL) {
    printf("ERROR: couldn't parse gridserver XML response\n");
    return NULL;
  }
  
  node = xmlDocGetRootElement(doc);
  if(strcmp((char*)node->name, "ServerResponse") != 0) {
    printf("ERROR: unexpected root node %s\n",(char*)node->name);
    goto out;
  }
  
  ret = deserialise_dict(doc, node->children);
  
 out:
  xmlFreeDoc(doc); return ret;
}

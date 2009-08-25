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

/* Parser for Nini XML files */

#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include "caj_parse_nini.h"

GKeyFile* caj_parse_nini_xml(const char* filename) {
  GKeyFile *config;
  xmlNodePtr root, sect, key;
  xmlDocPtr doc = xmlReadFile(filename,NULL,0);
  if(doc == NULL) return NULL;

  config = g_key_file_new();

  root = xmlDocGetRootElement(doc);
  if(strcmp((char*)root->name, "Nini") != 0) {
    printf("ERROR: not a valid Nini xml file\n");
    goto out_fail;
  }

  for(sect = root->children; sect != NULL; sect = sect->next) {
    char *sect_name;
    if(sect->type != XML_ELEMENT_NODE) continue;

    if(strcmp((char*)sect->name, "Section") != 0) {
      printf("ERROR: expected Section, got %s\n", sect->name);
      goto out_fail;
    }

    sect_name = (char*)xmlGetProp(sect, (const xmlChar*)"Name");
    if(sect_name == NULL) {
      printf("ERROR: missing section name\n");
      goto out_fail;
    }

    for(key = sect->children; key != NULL; key = key->next) {
      char *key_name, *value;

      if(key->type != XML_ELEMENT_NODE) continue;

      if(strcmp((char*)key->name, "Key") != 0) {
	printf("ERROR: expected Key, got %s\n", key->name);
	xmlFree(sect_name);
	goto out_fail;
      }
      
      key_name = (char*)xmlGetProp(key, (const xmlChar*)"Name");
      if(key_name == NULL) {
	printf("ERROR: missing key's name\n");
	xmlFree(sect_name);
	goto out_fail;
      }

      value = (char*)xmlGetProp(key, (const xmlChar*)"Value");
      if(value == NULL) {
	printf("ERROR: missing key's value\n");
	xmlFree(sect_name); xmlFree(key_name);
	goto out_fail;
      }
      
      // FIXME - is this right?
      g_key_file_set_value(config, sect_name, key_name, value);

      xmlFree(key_name); xmlFree(value);
    }

    xmlFree(sect_name);
  }

  xmlFreeDoc(doc);
  return config;

 out_fail:
  g_key_file_free(config);
  xmlFreeDoc(doc);
  return NULL;
}

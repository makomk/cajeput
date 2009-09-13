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

#include "caj_llsd.h"
#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <ctype.h>
#include <assert.h>
#include <glib.h>

#define LLSD_MAX_DEPTH 16

void llsd_free(caj_llsd *llsd) {
  int i;
  switch(llsd->type_id) {
  case LLSD_ARRAY:
    for(i = 0; i < llsd->t.arr.count; i++) {
      llsd_free(llsd->t.arr.data[i]);
    }
    free(llsd->t.arr.data);
    break;
  case LLSD_MAP:
    for(i = 0; i < llsd->t.map.count; i++) {
      free(llsd->t.map.data[i].key);
      llsd_free(llsd->t.map.data[i].val);
    }
    free(llsd->t.map.data);
    break;
  case LLSD_STRING:
  case LLSD_URI:
    free(llsd->t.str);
    break;
  case LLSD_BINARY:
    caj_string_free(&llsd->t.bin);
    break;
  }
  free(llsd);
}

void llsd_pretty_print(caj_llsd *llsd, int depth) {
  int i,n;
  for(n = 0; n < depth; n++) printf("    ");
  switch(llsd->type_id) {
  case LLSD_UNDEF:
    printf("undef\n"); break;
  case LLSD_INT:
    printf("int %i\n", llsd->t.i); break;
  case LLSD_REAL:
    printf("real %f\n", llsd->t.r); break;
  case LLSD_STRING:
    printf("string %s\n", llsd->t.str); break;
  case LLSD_ARRAY:
    printf("array %i:\n", llsd->t.arr.count);
    for(i = 0; i < llsd->t.arr.count; i++) {
      llsd_pretty_print(llsd->t.arr.data[i], depth+1);
    }
    break;
  case LLSD_MAP:
    printf("map %i:\n", llsd->t.map.count);
    for(i = 0; i < llsd->t.map.count; i++) {
      for(n = 0; n < depth; n++) printf("  ");
      printf("  key %s\n", llsd->t.map.data[i].key);
      llsd_pretty_print(llsd->t.map.data[i].val, depth+1);
    }
    break;
  case LLSD_UUID:
    {
      char buf[40];
      uuid_unparse(llsd->t.uuid, buf);
      printf("uuid %s\n", buf);
      break;
    }
  case LLSD_BINARY:
    printf("binary %i: ", llsd->t.bin.len);
    for(i = 0; i < llsd->t.bin.len; i++) {
      printf("%02x ", (int)((unsigned char*)llsd->t.bin.data)[i]);
    }
    printf("\n");
    break;
   
  default:
    printf("???\n"); break;
  }
}

void llsd_array_append(caj_llsd *arr, caj_llsd *it) {
  assert(arr->type_id == LLSD_ARRAY);
  if(arr->t.arr.count >= arr->t.arr.max) {
    arr->t.arr.max *= 2;
    arr->t.arr.data = realloc(arr->t.arr.data, arr->t.arr.max*sizeof(caj_llsd*));
  }
  arr->t.arr.data[arr->t.arr.count++] = it;
}

void llsd_map_append(caj_llsd *arr, const char* key, caj_llsd *it) {
  assert(arr->type_id == LLSD_MAP);
  if(arr->t.map.count >= arr->t.map.max) {
    arr->t.map.max *= 2;
    arr->t.map.data = realloc(arr->t.map.data, arr->t.map.max*sizeof(caj_llsd_pair));
  }
  arr->t.map.data[arr->t.map.count].key = strdup(key);
  arr->t.map.data[arr->t.map.count++].val = it;
}

static caj_llsd* parse_llsd_xml(xmlDocPtr doc, xmlNode * a_node, int depth) {
  xmlNode *cur_node;
  if(depth >= LLSD_MAX_DEPTH) return NULL; // DoS protection
  for(;;) {
    if(a_node == NULL) { 
      printf("%i no node found\n", depth);
      return NULL;
    }
    if(a_node->type != XML_TEXT_NODE && a_node->type != XML_COMMENT_NODE)
      break;
    // FIXME - check this is just whitespace
    a_node = a_node->next;
  }
  if (a_node->type == XML_ELEMENT_NODE) {
    caj_llsd* llsd = malloc(sizeof(caj_llsd));
    if(strcmp((char*)a_node->name, "undef") == 0) {
      llsd->type_id = LLSD_UNDEF;
    } else if(strcmp((char*)a_node->name, "integer") == 0) {
      char *str = (char*)xmlNodeListGetString(doc, a_node->children, 1);
      llsd->type_id = LLSD_INT;
      llsd->t.i = atoi(str); xmlFree(str);
    } else if(strcmp((char*)a_node->name, "real") == 0) {
      char *str = (char*)xmlNodeListGetString(doc, a_node->children, 1);
      llsd->type_id = LLSD_REAL;
      llsd->t.r = atof(str); xmlFree(str);
    } else if(strcmp((char*)a_node->name, "uuid") == 0) {
      char *str = (char*)xmlNodeListGetString(doc, a_node->children, 1);
      char *str2;
      llsd->type_id = LLSD_UUID;
      for(str2 = str; isspace(*str2); str2++);
      if(uuid_parse(str2, llsd->t.uuid)) {
	printf("%i UUID parse failed for ^%s^\n", depth, str2);
	xmlFree(str); free(llsd); return NULL;
      }
      xmlFree(str);
    } else if(strcmp((char*)a_node->name, "string") == 0) {
      char *str = (char*)xmlNodeListGetString(doc, a_node->children, 1);
      llsd->type_id = LLSD_STRING;
      llsd->t.str = strdup(str); 
      xmlFree(str);
    } else if(strcmp((char*)a_node->name, "binary") == 0) {
      // FIXME - shouldn't assume base64 encoding
      char *str = (char*)xmlNodeListGetString(doc, a_node->children, 1);
      gsize sz;
      llsd->type_id = LLSD_BINARY;
      // HACK - we're overwriting data that's only loosely ours.
      g_base64_decode_inplace(str, &sz); // FIXME - error handling?
      llsd->t.bin.len = sz;
      llsd->t.bin.data = malloc(sz);
      memcpy(llsd->t.bin.data, str, sz);
      xmlFree(str);
    } else if(strcmp((char*)a_node->name, "array") == 0) {
      llsd->type_id = LLSD_ARRAY;
      llsd->t.arr.count = 0;
      llsd->t.arr.max = 8;
      llsd->t.arr.data = calloc(sizeof(caj_llsd*), llsd->t.arr.max);
      cur_node = a_node->children;
      for(;;) {
	caj_llsd *child;
	while(cur_node != NULL && (cur_node->type == XML_TEXT_NODE || 
				   cur_node->type == XML_COMMENT_NODE)) 
	  cur_node = cur_node->next;
	if(cur_node == NULL) break;
	child = parse_llsd_xml(doc,cur_node,depth+1);
	if(child == NULL) {
	  llsd_free(llsd); return NULL;
	}
	llsd_array_append(llsd, child);
	cur_node = cur_node->next;
      }
    } else if(strcmp((char*)a_node->name, "map") == 0) {
      llsd->type_id = LLSD_MAP;
      llsd->t.map.count = 0;
      llsd->t.map.max = 8;
      llsd->t.map.data = calloc(sizeof(caj_llsd_pair), llsd->t.arr.max);
      cur_node = a_node->children;
      for(;;) {
	caj_llsd *child; char* key;
	while(cur_node != NULL && (cur_node->type == XML_TEXT_NODE || 
				   cur_node->type == XML_COMMENT_NODE)) 
	  cur_node = cur_node->next;
	if(cur_node == NULL) break;
	if(cur_node->type != XML_ELEMENT_NODE || 
	   strcmp((char*)cur_node->name,"key") != 0) {
	  printf("%i unexpected node %s while looking for key\n", depth, cur_node->name);
	}
	key = (char*)xmlNodeListGetString(doc, cur_node->children, 1);
	cur_node = cur_node->next;
	child = parse_llsd_xml(doc,cur_node,depth+1);
	if(child == NULL) {
	  llsd_free(llsd); xmlFree(key); return NULL;
	}
	llsd_map_append(llsd, key, child);
	xmlFree(key);
	cur_node = cur_node->next;
      }
    } else if(strcmp((char*)a_node->name, "boolean") == 0) {
      char *str = (char*)xmlNodeListGetString(doc, a_node->children, 1);
      llsd->type_id = LLSD_BOOLEAN;
      if(strcasecmp(str,"true") == 0 || strcmp(str,"0") == 0) {
	llsd->t.i = 1;
      } else if(str[0] == 0 || strcasecmp(str,"false") == 0 || 
		strcmp(str,"1") == 0) {
	llsd->t.i = 0;
      } else {
	printf("%i bad bool val: %s\n", depth, str);
	xmlFree(str); return NULL;
      }
      xmlFree(str);      
    } else {
      printf("%i unhandled node name: %s\n", depth, a_node->name);
      free(llsd); return NULL;
    }
    return llsd;
  } else {
    printf("%i unexpected node type: %i\n", depth, a_node->type);
    return NULL;
  }
  assert(0);
};

caj_llsd* llsd_parse_xml(const char* data, int len) {
  caj_llsd *ret;
  xmlDocPtr doc = xmlReadMemory(data,len,"llsd.xml",NULL,0);
  if(doc == NULL) return NULL;

  ret = parse_llsd_xml(doc, xmlDocGetRootElement(doc)->children, 0);

  xmlFreeDoc(doc);
  return ret;
}

caj_llsd* llsd_map_lookup(caj_llsd *map, const char *key) {
  int i;
  assert(map->type_id == LLSD_MAP);
  for(i = 0; i < map->t.map.count; i++) {
    if(strcmp(key,map->t.map.data[i].key) == 0) {
      return map->t.map.data[i].val;
    }
  }
  return NULL;
  
}

static int serialise_xml(caj_llsd *llsd, xmlTextWriterPtr writer) {
  char buf[40]; int i;
  switch(llsd->type_id) {
  case LLSD_UNDEF:
    if(xmlTextWriterStartElement(writer, BAD_CAST "undef") < 0) return 0;
    if(xmlTextWriterEndElement(writer) < 0) return 0;
    break;
  case LLSD_BOOLEAN:
    if(xmlTextWriterStartElement(writer, BAD_CAST "boolean") < 0) return 0;
    if(xmlTextWriterWriteString(writer,BAD_CAST (llsd->t.i?"true":"false")) < 0) return 0;
    if(xmlTextWriterEndElement(writer) < 0) return 0;
    break;
  case LLSD_INT:
    if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "integer",
				       "%i",llsd->t.i) < 0) return 0;
    break;
  case LLSD_REAL:
    if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "real",
				       "%f",llsd->t.r) < 0) return 0;
    break;
  case LLSD_UUID:
    uuid_unparse(llsd->t.uuid,buf);
    if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "uuid",
				       "%s",buf) < 0) return 0;
    break;    
  case LLSD_STRING:
    if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "string",
				       "%s",llsd->t.str) < 0) return 0;
    break;    
  case LLSD_BINARY:
    if(xmlTextWriterStartElement(writer, BAD_CAST "binary") < 0) return 0;
    if(xmlTextWriterWriteBase64(writer, (char*)llsd->t.bin.data, 0, 
				llsd->t.bin.len) < 0) return 0;
    if(xmlTextWriterEndElement(writer) < 0) return 0;
    break;
    printf("Unhandled LLSD type %i in serialisation\n",llsd->type_id);
    return 0;
  case LLSD_MAP:
    if(xmlTextWriterStartElement(writer, BAD_CAST "map") < 0) return 0;
    for(i = 0; i < llsd->t.map.count; i++) {
      if(xmlTextWriterWriteFormatElement(writer,BAD_CAST "key","%s",
					 llsd->t.map.data[i].key) < 0) return 0;
    
      if(!serialise_xml(llsd->t.map.data[i].val,writer)) return 0;
    }
    if(xmlTextWriterEndElement(writer) < 0) return 0;    
    break;
  case LLSD_ARRAY:
    if(xmlTextWriterStartElement(writer, BAD_CAST "array") < 0) return 0;
    for(i = 0; i < llsd->t.arr.count; i++) {
      if(!serialise_xml(llsd->t.arr.data[i],writer)) return 0;
    }
    if(xmlTextWriterEndElement(writer) < 0) return 0;    
    break;
  default:
    printf("Unhandled LLSD type %i in serialisation\n",llsd->type_id);
    return 0;
  }
  return 1;
}

char* llsd_serialise_xml(caj_llsd *llsd) {
  char *out = NULL;
  xmlTextWriterPtr writer;
  xmlBufferPtr buf;
  buf = xmlBufferCreate();
  if(buf == NULL) return NULL;
  writer = xmlNewTextWriterMemory(buf, 0);
  if(writer == NULL) {
    xmlBufferFree(buf);
    return NULL;
  }
  
  if(xmlTextWriterStartDocument(writer,NULL,"UTF-8",NULL) < 0) {
    printf("DEBUG: couldn't start XML document\n"); goto fail;
  }
  
  if(xmlTextWriterStartElement(writer, BAD_CAST "llsd") < 0) goto fail;

  if(!serialise_xml(llsd, writer)) goto fail;
  
  if(xmlTextWriterEndElement(writer) < 0) goto fail;
  
  if(xmlTextWriterEndDocument(writer) < 0) {
    printf("DEBUG: couldn't end XML document\n"); goto fail;
    
  }

  out = strdup((char*)buf->content);

 fail:
  xmlFreeTextWriter(writer);
  xmlBufferFree(buf);
  return out;
}

caj_llsd* llsd_new_array(void) {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_ARRAY;
  llsd->t.arr.count = 0;
  llsd->t.arr.max = 8;
  llsd->t.arr.data = calloc(sizeof(caj_llsd*), llsd->t.arr.max);
  return llsd;
}

caj_llsd* llsd_new_map(void) {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_MAP;
  llsd->t.map.count = 0;
  llsd->t.map.max = 8;
  llsd->t.map.data = calloc(sizeof(caj_llsd_pair), llsd->t.arr.max);
  return llsd;
}

caj_llsd* llsd_new_string(const char *str) {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_STRING;
  llsd->t.str = strdup(str);
  return llsd;
}

caj_llsd* llsd_new_string_take(char *str) {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_STRING;
  llsd->t.str = str;
  return llsd;
}

caj_llsd* llsd_new_binary(void* data, int len) {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_BINARY;
  caj_string_set_bin(&llsd->t.bin, (unsigned char*)data, len);
  return llsd;
  
}

caj_llsd* llsd_new_uuid(uuid_t u) {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_UUID;
  uuid_copy(llsd->t.uuid, u);
  return llsd;
}

caj_llsd* llsd_new_int( int i) {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_INT;
  llsd->t.i = i;
  return llsd;
}

caj_llsd* llsd_new_bool( int i) {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_BOOLEAN;
  llsd->t.i = (i != 0);
  return llsd;
}

caj_llsd* llsd_new() {
  caj_llsd* llsd = malloc(sizeof(caj_llsd));
  llsd->type_id = LLSD_UNDEF;
  return llsd;
}

// ah, SL and its random endianness
caj_llsd* llsd_new_from_u64(uint64_t val) {
  unsigned char rawmsg[8];
  rawmsg[7] = val&0xff; rawmsg[6] = val >> 8; 
  rawmsg[5] = val >> 16; rawmsg[4] = val >> 24;
  rawmsg[3] = val >> 32; rawmsg[2] = val >> 40;
  rawmsg[1] = val >> 48; rawmsg[0] = val >> 56;
  return llsd_new_binary(rawmsg, 8);
}

caj_llsd* llsd_new_from_u32(uint32_t val) {
  unsigned char rawmsg[4];
  rawmsg[3] = val&0xff; rawmsg[2] = val >> 8; 
  rawmsg[1] = val >> 16; rawmsg[0] = val >> 24;
  return llsd_new_binary(rawmsg, 4);
}

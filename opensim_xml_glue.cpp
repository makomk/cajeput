#include "opensim_xml_glue.h"
#include "string.h"
#include "uuid/uuid.h"
#include "caj_types.h"
#include <glib.h>

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


// for internal use of deserialise_xml only
static void free_partial_deserial_xml(xml_serialisation_desc* serial,
				      void* out, int cnt) {
  unsigned char* outbuf = (unsigned char*)out;
  for(int i = 0; i < cnt && serial[i].name != NULL; i++) {
    switch(serial[i].type) {
    case XML_STYPE_STRING:
      xmlFree(*(char**)(outbuf+serial[i].offset));
      break;
    case XML_STYPE_STRUCT:
      free_partial_deserial_xml((xml_serialisation_desc*)serial[i].extra,
				outbuf+serial[i].offset, 1000000 /* FIXME - use INT_MAX */);
      break;
    case XML_STYPE_BASE64:
      caj_string_free((caj_string*)(outbuf+serial[i].offset));
      break;
    default:
      break;
    }
  }
}

int osglue_deserialise_xml(xmlDocPtr doc, xmlNodePtr node,
			   xml_serialisation_desc* serial,
			   void* out) {
  unsigned char* outbuf = (unsigned char*)out;
  for(int i = 0; serial[i].name != NULL; i++) {
    while(node != NULL && (node->type == XML_TEXT_NODE || 
			   node->type == XML_COMMENT_NODE))
      node = node->next;
    if(!check_node(node,serial[i].name)) {
      free_partial_deserial_xml(serial, out, i);
      return 0;
    }
    switch(serial[i].type) {
    case XML_STYPE_SKIP:
      break; // do nothing.
    case XML_STYPE_STRING:
      {
	char* str = (char*)xmlNodeListGetString(doc, node->children, 1);
	if(str == NULL) { str = (char*)xmlMalloc(1); str[0] = 0; }
	*(char**)(outbuf+serial[i].offset) = str;
      }
      break;
    case XML_STYPE_UUID:
      {
	xmlNodePtr guid = node->children;
	while(guid != NULL && (guid->type == XML_TEXT_NODE || 
			       guid->type == XML_COMMENT_NODE))
	  guid = guid->next;
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
	if(s == NULL) {
	  printf("ERROR: bad integer XML node %s", serial[i].name);
	  free_partial_deserial_xml(serial, out, i);
	  return 0;
	}
	*(int*)(outbuf+serial[i].offset) = atoi(s);
	xmlFree(s);
      }
      break;
    case XML_STYPE_BOOL:
      {
	char *s = (char*)xmlNodeListGetString(doc, node->children, 1);
	if(strcmp(s,"true") == 0) {
	  *(int*)(outbuf+serial[i].offset) = 1;
	} else if(strcmp(s,"false") == 0) {
	  *(int*)(outbuf+serial[i].offset) = 0;
	} else {
	  printf("ERROR: bad boolean value in deserialise_xml\n");
	  free_partial_deserial_xml(serial, out, i);
	  xmlFree(s);
	  return 0;
	}
	xmlFree(s);
      }
      break;
    case XML_STYPE_FLOAT:
      {
	char *s = (char*)xmlNodeListGetString(doc, node->children, 1);
	if(s == NULL) {
	  printf("ERROR: bad float XML node %s\n", serial[i].name);
	  free_partial_deserial_xml(serial, out, i);
	  return 0;
	}
	*(float*)(outbuf+serial[i].offset) = atof(s);
	xmlFree(s);
      }
      break;
    case XML_STYPE_STRUCT:
      if(!osglue_deserialise_xml(doc, node->children, 
				 (xml_serialisation_desc*)serial[i].extra,
				 outbuf+serial[i].offset)) {
	printf("ERROR: couldn't deserialise child struct %s\n",
	       serial[i].name);
	free_partial_deserial_xml(serial, out, i);
	return 0;
      }
      break;
    case XML_STYPE_BASE64:
      {
	caj_string *str = (caj_string*)(outbuf+serial[i].offset);
	char* s = (char*)xmlNodeListGetString(doc, node->children, 1);
	if(s == NULL) {
	  str->data = (unsigned char*)malloc(0); str->len = 0;
	} else {
	  gsize sz;	  
	  g_base64_decode_inplace(s, &sz); // FIXME - error handling?
	  caj_string_set_bin(str, (unsigned char*)s, sz);
	}
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

int osglue_serialise_xml(xmlTextWriterPtr writer,
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
    case XML_STYPE_BOOL:
       if(xmlTextWriterWriteFormatElement(writer,BAD_CAST serial[i].name,
					  "%s",(*(int*)(inbuf+serial[i].offset)?"true":"false")) < 0) 
	return 0;
      break;
    case XML_STYPE_FLOAT:
      if(xmlTextWriterWriteFormatElement(writer,BAD_CAST serial[i].name,
					    "%f",*(float*)(inbuf+serial[i].offset)) < 0) 
	   return 0;
      break;
    case XML_STYPE_BASE64:
      {
	caj_string *str = (caj_string*)(inbuf+serial[i].offset);
	gchar *b64 = g_base64_encode(str->data, str->len);
	if(xmlTextWriterWriteFormatElement(writer,BAD_CAST serial[i].name,
					   "%s", b64) < 0) {
	  g_free(b64); return 0;
	}
	g_free(b64);
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

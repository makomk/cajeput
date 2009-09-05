#ifndef OPENSIM_XML_GLUE_H
#define OPENSIM_XML_GLUE_H

#include <libxml/tree.h>
#include <libxml/xmlwriter.h>

#define XML_STYPE_SKIP 0
#define XML_STYPE_STRING 1
#define XML_STYPE_UUID 2
#define XML_STYPE_INT 3

struct xml_serialisation_desc {
  const char* name;
  int type;
  size_t offset;
};

int osglue_deserialise_xml(xmlDocPtr doc, xmlNodePtr node,
			   xml_serialisation_desc* serial,
			   void* out);
int osglue_serialise_xml(xmlTextWriterPtr writer,
			 xml_serialisation_desc* serial,
			 void* in);

#endif

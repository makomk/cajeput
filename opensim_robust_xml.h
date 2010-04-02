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

#ifndef OPENSIM_ROBUST_XML_H
#define OPENSIM_ROBUST_XML_H

typedef struct os_robust_xml os_robust_xml;

#define OS_ROBUST_XML_STR 0
#define OS_ROBUST_XML_LIST 1

struct os_robust_xml {
  int node_type;
  union {
    char *s;
    GHashTable *hashtbl;
  } u;
};

os_robust_xml *os_robust_xml_deserialise(const char *data, int len);
void os_robust_xml_free(os_robust_xml *rxml);
os_robust_xml *os_robust_xml_lookup(os_robust_xml *rxml,  const char* key);

#endif /* !defined(OPENSIM_ROBUST_XML_H) */

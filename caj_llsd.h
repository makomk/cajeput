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

#ifndef CAJ_LLSD_H
#define CAJ_LLSD_H
#include "caj_types.h"
#include <uuid/uuid.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LLSD_UNDEF 0
#define LLSD_BOOLEAN 1
#define LLSD_INT 2
#define LLSD_REAL 3
#define LLSD_UUID 4
#define LLSD_STRING 5
#define LLSD_DATE 6 // TODO
#define LLSD_URI 7 // is this used?
#define LLSD_BINARY 8
#define LLSD_MAP 9
#define LLSD_ARRAY 10

typedef struct caj_llsd caj_llsd;

typedef struct caj_llsd_array {
  int count, max;
  caj_llsd **data;
} caj_llsd_array;

typedef struct caj_llsd_pair {
  char* key;
  caj_llsd *val;
} caj_llsd_pair;

typedef struct caj_llsd_map {
  int count, max;
  caj_llsd_pair *data;
} caj_llsd_map;


struct caj_llsd {
  int type_id;
  union {
    int32_t i; // int
    double r; // real
    char *str; // string/uri
    uuid_t uuid; // uuid
    caj_string bin; // binary
    caj_llsd_array arr;
    caj_llsd_map map;
  } t;
};

caj_llsd* llsd_parse_xml(const char* data, int len);
char* llsd_serialise_xml(caj_llsd *llsd);
void llsd_pretty_print(caj_llsd *llsd, int depth);
void llsd_free(caj_llsd *llsd);
caj_llsd* llsd_map_lookup(caj_llsd *map, const char *key);
  
caj_llsd* llsd_new();
caj_llsd* llsd_new_array(void);
caj_llsd* llsd_new_map(void);
caj_llsd* llsd_new_string(const char *str);
caj_llsd* llsd_new_string_take(char *str);
  caj_llsd* llsd_new_binary(void* data, int len);
caj_llsd* llsd_new_uuid(uuid_t u);
caj_llsd* llsd_new_int( int i);
caj_llsd* llsd_new_bool( int i);
void llsd_array_append(caj_llsd *arr, caj_llsd *it);
void llsd_map_append(caj_llsd *arr, const char* key, caj_llsd *it);

  caj_llsd* llsd_new_from_u64(uint64_t val);
  caj_llsd* llsd_new_from_u32(uint32_t val);

#define LLSD_IS(llsd, typ) ((llsd) != NULL && (llsd)->type_id == typ)

#ifdef __cplusplus
}
#endif

#endif

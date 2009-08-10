#ifndef SL_LLSD_H
#define SL_LLSD_H
#include "sl_types.h"
#include <uuid/uuid.h>

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

typedef struct sl_llsd sl_llsd;

typedef struct sl_llsd_array {
  int count, max;
  sl_llsd **data;
} sl_llsd_array;

typedef struct sl_llsd_pair {
  char* key;
  sl_llsd *val;
} sl_llsd_pair;

typedef struct sl_llsd_map {
  int count, max;
  sl_llsd_pair *data;
} sl_llsd_map;


struct sl_llsd {
  int type_id;
  union {
    int32_t i; // int
    double r; // real
    char *str; // string/uri
    uuid_t uuid; // uuid
    sl_string bin; // binary
    sl_llsd_array arr;
    sl_llsd_map map;
  } t;
};

sl_llsd* llsd_parse_xml(const char* data, int len);
char* llsd_serialise_xml(sl_llsd *llsd);
void llsd_pretty_print(sl_llsd *llsd, int depth);
void llsd_free(sl_llsd *llsd);
sl_llsd* llsd_map_lookup(sl_llsd *map, const char *key);
  
sl_llsd* llsd_new();
sl_llsd* llsd_new_array(void);
sl_llsd* llsd_new_map(void);
sl_llsd* llsd_new_string(char *str);
sl_llsd* llsd_new_string_take(char *str);
  sl_llsd* llsd_new_int( int i);
void llsd_array_append(sl_llsd *arr, sl_llsd *it);
void llsd_map_append(sl_llsd *arr, char* key, sl_llsd *it);


#define LLSD_IS(llsd, typ) ((llsd) != NULL && (llsd)->type_id == typ)

#ifdef __cplusplus
}
#endif

#endif

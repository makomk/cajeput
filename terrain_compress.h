#ifndef TERRAIN_COMPRESS_H
#define TERRAIN_COMPRESS_H

#include "caj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAYER_TYPE_LAND 0x4C

void terrain_create_patches(float *heightmap, int  *patches, int num_patches, struct caj_string *out);
void terrain_init_compress();

#ifdef __cplusplus
}
#endif

#endif

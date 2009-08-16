#ifndef TERRAIN_COMPRESS_H
#define TERRAIN_COMPRESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sl_types.h"

#define LAYER_TYPE_LAND 0x4C

void terrain_create_patches(float *heightmap, int  *patches, int num_patches, struct sl_string *out);
void terrain_init_compress();

#ifdef __cplusplus
}
#endif

#endif

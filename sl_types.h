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

#ifndef SL_TYPES_H
#define SL_TYPES_H
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct sl_string {
  unsigned char* data;
  int len;
} sl_string;

static inline void sl_string_set(struct sl_string* str, char* val) {
  str->len = strlen(val)+1;
  str->data = (unsigned char*)malloc(str->len);
  memcpy(str->data, val, str->len);
}

static inline void sl_string_set_bin(struct sl_string* str, unsigned char* val, int len) {
  str->len = len;
  str->data = (unsigned char*)malloc(len);
  memcpy(str->data, val, len);
}

static inline void sl_string_free(struct sl_string* str) {
  free(str->data); str->data = NULL;
}

typedef struct sl_vector3 {
  float x, y, z;
} sl_vector3;

typedef struct sl_vector3_dbl {
  double x, y, z;
} sl_vector3_dbl;

typedef struct sl_quat {
  float x, y, z, w;
} sl_quat;

typedef struct sl_vector4 {
  float x, y, z, w;
} sl_vector4;


/* Expands a normalised quaternion for which only the first 3 elements have been set */
static inline void sl_expand_quat(struct sl_quat* quat) {
  float w_squared = 1.0f - (quat->x*quat->x) - (quat->y*quat->y) - (quat->z*quat->z);
  quat->w = w_squared > 0.0f ? sqrtf(w_squared) : 0.0f;
}

static inline float sl_vect3_dist(const struct sl_vector3 *v1, const struct sl_vector3 *v2) {
  float x = v1->x - v2->x, y = v1->y - v2->y, z = v1->z - v2->z;
  return sqrtf(x*x + y*y + z*z);
}

static void sl_mult_vect3_quat(struct sl_vector3 *out, const struct sl_quat* rot,
			       const struct sl_vector3 *vec) {
  // FIXME - blind copy and paste from OpenSim code
  sl_vector3 ret;
  ret.x =
    rot->w * rot->w * vec->x +
    2.f * rot->y * rot->w * vec->z -
    2.f * rot->z * rot->w * vec->y +
    rot->x * rot->x * vec->x +
    2.f * rot->y * rot->x * vec->y +
    2.f * rot->z * rot->x * vec->z -
    rot->z * rot->z * vec->x -
    rot->y * rot->y * vec->x;

  ret.y =
    2.f * rot->x * rot->y * vec->x +
    rot->y * rot->y * vec->y +
    2.f * rot->z * rot->y * vec->z +
    2.f * rot->w * rot->z * vec->x -
    rot->z * rot->z * vec->y +
    rot->w * rot->w * vec->y -
    2.f * rot->x * rot->w * vec->z -
    rot->x * rot->x * vec->y;
  
  ret.z =
    2.f * rot->x * rot->z * vec->x +
    2.f * rot->y * rot->z * vec->y +
    rot->z * rot->z * vec->z -
    2.f * rot->w * rot->y * vec->x -
    rot->y * rot->y * vec->z +
    2.f * rot->w * rot->x * vec->y -
    rot->x * rot->x * vec->z +
    rot->w * rot->w * vec->z;
  
  *out = ret;
}

#endif

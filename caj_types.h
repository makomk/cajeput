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

#ifndef CAJ_TYPES_H
#define CAJ_TYPES_H
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct caj_string {
  unsigned char* data;
  int len;
} caj_string;

static inline void caj_string_set(struct caj_string* str, const char* val) {
  str->len = strlen(val)+1;
  str->data = (unsigned char*)malloc(str->len);
  memcpy(str->data, val, str->len);
}

static inline void caj_string_set_bin(struct caj_string* str, 
				     const unsigned char* val, int len) {
  str->len = len;
  str->data = (unsigned char*)malloc(len);
  memcpy(str->data, val, len);
}

static inline void caj_string_copy(struct caj_string *dest, 
				  const struct caj_string *src) {
  if(src->data == NULL) {
    dest->data = NULL; dest->len = 0;
  } else {
    dest->data = (unsigned char*)malloc(src->len);
    dest->len = src->len;
    memcpy(dest->data, src->data, dest->len);
  }
}

static inline void caj_string_steal(struct caj_string *dest, 
				   struct caj_string *src) {
  if(src->data == NULL) {
    dest->data = NULL; dest->len = 0;
  } else {
    dest->data = src->data; dest->len = src->len;
    src->data = NULL; src->len = 0;
  }
}

static inline void caj_string_free(struct caj_string* str) {
  free(str->data); str->data = NULL;
}

typedef struct caj_vector3 {
  float x, y, z;
} caj_vector3;

typedef struct caj_vector3_dbl {
  double x, y, z;
} caj_vector3_dbl;

typedef struct caj_quat {
  float x, y, z, w;
} caj_quat;

typedef struct caj_vector4 {
  float x, y, z, w;
} caj_vector4;


/* Expands a normalised quaternion for which only the first 3 elements have been set */
static inline void caj_expand_quat(struct caj_quat* quat) {
  float w_squared = 1.0f - (quat->x*quat->x) - (quat->y*quat->y) - (quat->z*quat->z);
  quat->w = w_squared > 0.0f ? sqrtf(w_squared) : 0.0f;
}

static inline float caj_vect3_dist(const struct caj_vector3 *v1, const struct caj_vector3 *v2) {
  float x = v1->x - v2->x, y = v1->y - v2->y, z = v1->z - v2->z;
  return sqrtf(x*x + y*y + z*z);
}

void caj_mult_quat_quat(struct caj_quat *out, const struct caj_quat* q1,
			const struct caj_quat *q2);
void caj_mult_vect3_quat(struct caj_vector3 *out, const struct caj_quat* rot,
			 const struct caj_vector3 *vec);
void caj_cross_vect3(struct caj_vector3 *out, const struct caj_vector3* v1,
		     const struct caj_vector3 *v2);

// should be small enough not to be affected by sane quantisation
#define CAJ_QUAT_EPS 0.0001

static inline int caj_quat_equal(struct caj_quat *q1, struct caj_quat *q2) {
  return fabs(q1->x - q2->x) < CAJ_QUAT_EPS && 
    fabs(q1->y - q2->y) < CAJ_QUAT_EPS &&
    fabs(q1->z - q2->z) < CAJ_QUAT_EPS &&
    fabs(q1->w - q2->w) < CAJ_QUAT_EPS;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

static inline caj_vector3 operator+(const caj_vector3 &v1, const caj_vector3 &v2) {
  caj_vector3 out; 
  out.x = v1.x + v2.x; out.y = v1.y + v2.y; out.z = v1.z + v2.z;
  return out;
}

static inline caj_vector3 operator-(const caj_vector3 &v1, const caj_vector3 &v2) {
  caj_vector3 out; 
  out.x = v1.x - v2.x; out.y = v1.y - v2.y; out.z = v1.z - v2.z;
  return out;
}

#endif // __cplusplus

#endif

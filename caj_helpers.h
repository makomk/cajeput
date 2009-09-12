#ifndef CAJ_HELPERS_H
#define CAJ_HELPERS_H

#include "caj_types.h"

// WARNING: this assumes float is 32-bit and same endianness as int, etc.

static inline void caj_uint32_to_bin_le(unsigned char *buf, uint32_t val) {
  buf[0] = val & 0xff;
  buf[1] = (val >> 8) & 0xff;
  buf[2] = (val >> 16) & 0xff;
  buf[3] = (val >> 24) & 0xff;
}

static inline uint32_t caj_bin_to_uint32_le(unsigned char *buf) {
  return buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
    ((uint32_t)buf[3] << 24);
}

static inline void caj_float_to_bin_le(unsigned char *buf, float val) {
  union { uint32_t i; float f; } u;
  u.f = val; caj_uint32_to_bin_le(buf, u.i);
}

static inline float caj_bin_to_float_le(unsigned char *buf) {
  union { uint32_t i; float f; } u;
  u.i = caj_bin_to_uint32_le(buf); return u.f;
}

static inline void caj_vect3_to_bin_le(unsigned char *buf, const caj_vector3 *v) {
  caj_float_to_bin_le(buf+0, v->x);
  caj_float_to_bin_le(buf+4, v->y);
  caj_float_to_bin_le(buf+8, v->z);
}

static inline void caj_quat_to_bin4_le(unsigned char *buf, const caj_quat *v) {
  caj_float_to_bin_le(buf+0, v->x);
  caj_float_to_bin_le(buf+4, v->y);
  caj_float_to_bin_le(buf+8, v->z);
  caj_float_to_bin_le(buf+12, v->w);
}

static inline void caj_quat_to_bin3_le(unsigned char *buf, const caj_quat *v) {
  caj_float_to_bin_le(buf+0, v->x);
  caj_float_to_bin_le(buf+4, v->y);
  caj_float_to_bin_le(buf+8, v->z);
}

static inline void caj_vect4_to_bin_le(unsigned char *buf, const caj_vector4 *v) {
  caj_float_to_bin_le(buf+0, v->x);
  caj_float_to_bin_le(buf+4, v->y);
  caj_float_to_bin_le(buf+8, v->z);
  caj_float_to_bin_le(buf+12, v->w);
}

static inline void caj_bin_to_vect3_le(caj_vector3 *v, unsigned char *buf) {
  v->x = caj_bin_to_float_le(buf+0);
  v->y = caj_bin_to_float_le(buf+4);
  v->z = caj_bin_to_float_le(buf+8);
}

static inline void caj_bin3_to_quat_le(caj_quat *v, unsigned char *buf) {
  v->x = caj_bin_to_float_le(buf+0);
  v->y = caj_bin_to_float_le(buf+4);
  v->z = caj_bin_to_float_le(buf+8);
  caj_expand_quat(v);
}

#endif

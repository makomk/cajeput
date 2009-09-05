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

/* Hacky (and incomplete) sim state save/restore code */

#include "cajeput_core.h"
#include "cajeput_world.h"
#include "cajeput_int.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>

static int dump_write_u32(int fd, uint32_t val) {
  unsigned char buf[4];
  buf[0] = val >> 24; buf[1] = val >> 16; buf[2] = val >> 8; buf[3] = val;
  return (write(fd, buf, 4) != 4);
}

static int dump_write_u16(int fd, uint16_t val) {
  unsigned char buf[2];
  buf[0] = val >> 8; buf[1] = val;
  return (write(fd, buf, 2) != 2);
}

static int dump_write_u8(int fd, uint8_t val) {
  unsigned char buf[1]; buf[0] = val;
  return (write(fd, buf, 1) != 1);
}

// of dubious portability, but never mind
static int dump_write_float(int fd, float val) {
  union { float f; uint32_t i; } u;
  u.f = val; return dump_write_u32(fd, u.i);
}

static int dump_read_u32(int fd, uint32_t *val) {
  unsigned char buf[4];
  if(read(fd, buf, 4) != 4) return 1;
  *val = (uint32_t)buf[0] << 24 | (uint32_t)buf[1] << 16 |
    (uint32_t)buf[2] << 8 | (uint32_t)buf[3];
  return 0;
}

static int dump_read_u16(int fd, uint16_t *val) {
  unsigned char buf[2];
  if(read(fd, buf, 2) != 2) return 1;
  *val = (uint16_t)buf[0] << 8 | (uint16_t)buf[1];
  return 0;
}

static int dump_read_u8(int fd, uint8_t *val) {
  return (read(fd, val, 1) != 1);
}

static int dump_read_float(int fd, float *val) {
  union { float f; uint32_t i; } u;
  if(dump_read_u32(fd, &u.i)) return 1;
  *val = u.f; return 0;
}


#define PRIM_MAGIC_V1 0x7385ad01

// 0 is reserved as an end marker;
#define DUMP_TYPE_INT 1
#define DUMP_TYPE_FLOAT 2
#define DUMP_TYPE_VECT3 3
#define DUMP_TYPE_QUAT 4
#define DUMP_TYPE_U8 5
#define DUMP_TYPE_U16 6
#define DUMP_TYPE_U32 7
#define DUMP_TYPE_UUID 8
#define DUMP_TYPE_STR 9
#define DUMP_TYPE_CAJ_STR 10

struct dump_desc {
  int type, offset;
};

dump_desc prim_dump_v1[] = {
  // skipping ob.type
  { DUMP_TYPE_VECT3, offsetof(primitive_obj, ob.pos) },
  { DUMP_TYPE_VECT3, offsetof(primitive_obj, ob.scale) },
  { DUMP_TYPE_VECT3, offsetof(primitive_obj, ob.velocity) },
  { DUMP_TYPE_QUAT, offsetof(primitive_obj, ob.rot) },
  { DUMP_TYPE_UUID, offsetof(primitive_obj, ob.id) },
  // skipping ob.local_id, ob.phys, ob.chat, crc_counter
  { DUMP_TYPE_U8, offsetof(primitive_obj, sale_type) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, material) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_curve) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, profile_curve) },

  { DUMP_TYPE_U16, offsetof(primitive_obj, path_begin) },
  { DUMP_TYPE_U16, offsetof(primitive_obj, path_end) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_scale_x) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_scale_y) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_shear_x) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_shear_y) },
  
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_twist) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_twist_begin) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_radius_offset) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_taper_x) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_taper_y) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_revolutions) },
  { DUMP_TYPE_U8, offsetof(primitive_obj, path_skew) },
  { DUMP_TYPE_U16, offsetof(primitive_obj, profile_begin) },
  { DUMP_TYPE_U16, offsetof(primitive_obj, profile_end) },
  { DUMP_TYPE_U16, offsetof(primitive_obj, profile_hollow) },
  { DUMP_TYPE_UUID, offsetof(primitive_obj, creator) },
  { DUMP_TYPE_UUID, offsetof(primitive_obj, owner) },
  { DUMP_TYPE_U32, offsetof(primitive_obj, base_perms) },
  { DUMP_TYPE_U32, offsetof(primitive_obj, owner_perms) },
  { DUMP_TYPE_U32, offsetof(primitive_obj, group_perms) },
  { DUMP_TYPE_U32, offsetof(primitive_obj, everyone_perms) },
  { DUMP_TYPE_U32, offsetof(primitive_obj, next_perms) },
  { DUMP_TYPE_U32, offsetof(primitive_obj, sale_price) },
  { DUMP_TYPE_STR, offsetof(primitive_obj, name) },
  { DUMP_TYPE_STR, offsetof(primitive_obj, description) },
  { DUMP_TYPE_CAJ_STR, offsetof(primitive_obj, tex_entry) },
  // not included - inv. (FIXME)
  { 0, 0 }
};

static int dump_object(int fd, dump_desc *dump, void *obj) {
  char *data = (char*)obj;
  for(int i = 0; dump[i].type != 0; i++) {
    switch(dump[i].type) {
    case DUMP_TYPE_INT:
      {
	int32_t val = *(int*)(data+dump[i].offset);
	if(dump_write_u32(fd, val)) return 1;
	break;
      }
    case DUMP_TYPE_FLOAT:
      if(dump_write_float(fd, *(float*)(data+dump[i].offset))) return 1;
      break;
    case DUMP_TYPE_VECT3:
      {
	caj_vector3 *vect = (caj_vector3*)(data+dump[i].offset);
	if(dump_write_float(fd, vect->x) || dump_write_float(fd, vect->y) ||
	   dump_write_float(fd, vect->z)) return 1;
	break;
      }
    case DUMP_TYPE_QUAT:
      {
	caj_quat *quat = (caj_quat*)(data+dump[i].offset);
	if(dump_write_float(fd, quat->x) || dump_write_float(fd, quat->y) ||
	   dump_write_float(fd, quat->z) || dump_write_float(fd, quat->w)) 
	  return 1;
	break;
      }
    case DUMP_TYPE_U8:
      if(dump_write_u8(fd, *(uint8_t*)(data+dump[i].offset))) return 1;
      break;
    case DUMP_TYPE_U16:
      if(dump_write_u16(fd, *(uint16_t*)(data+dump[i].offset))) return 1;
      break;
    case DUMP_TYPE_U32:
      if(dump_write_u32(fd, *(uint32_t*)(data+dump[i].offset))) return 1;
      break;
    case DUMP_TYPE_UUID:
      if(write(fd,data+dump[i].offset,16) != 16) return 1;
      break;
    case DUMP_TYPE_STR:
      {
	char *str = *(char**)(data+dump[i].offset);
	int len = strlen(str);
	if(dump_write_u32(fd, len)) return 1;
	if(write(fd,str,len) != len) return 1;
	break;
      }
    case DUMP_TYPE_CAJ_STR:
      {
	caj_string *str = (caj_string*)(data+dump[i].offset);
	if(dump_write_u32(fd, str->len)) return 1;
	if(write(fd,str->data,str->len) != str->len) return 1;
	break;
      }
    default:
      printf("FIXME: bad type %i in dump_object\n", dump[i].type);
      return 1;
    }
  }
  return 0;
}

static int undump_object(int fd, dump_desc *dump, void *obj) {
  char *data = (char*)obj;
  for(int i = 0; dump[i].type != 0; i++) {
    switch(dump[i].type) {
    case DUMP_TYPE_INT:
      {
	int32_t val;
	if(dump_read_u32(fd, (uint32_t*)&val)) return 1;
	*(int*)(data+dump[i].offset) = val;
	break;
      }
    case DUMP_TYPE_FLOAT:
      if(dump_read_float(fd, (float*)(data+dump[i].offset))) return 1;
      break;
    case DUMP_TYPE_VECT3:
      {
	caj_vector3 *vect = (caj_vector3*)(data+dump[i].offset);
	if(dump_read_float(fd, &vect->x) || dump_read_float(fd, &vect->y) ||
	   dump_read_float(fd, &vect->z)) return 1;
	break;
      }
    case DUMP_TYPE_QUAT:
      {
	caj_quat *quat = (caj_quat*)(data+dump[i].offset);
	if(dump_read_float(fd, &quat->x) || dump_read_float(fd, &quat->y) ||
	   dump_read_float(fd, &quat->z) || dump_read_float(fd, &quat->w)) 
	  return 1;
	break;
      }
    case DUMP_TYPE_U8:
      if(dump_read_u8(fd, (uint8_t*)(data+dump[i].offset))) return 1;
      break;
    case DUMP_TYPE_U16:
      if(dump_read_u16(fd, (uint16_t*)(data+dump[i].offset))) return 1;
      break;
    case DUMP_TYPE_U32:
      if(dump_read_u32(fd, (uint32_t*)(data+dump[i].offset))) return 1;
      break;
    case DUMP_TYPE_UUID:
      if(read(fd,data+dump[i].offset,16) != 16) return 1;
      break;
    case DUMP_TYPE_STR:
      {
	uint32_t len;
	if(dump_read_u32(fd, &len)) return 1;
	char *str = (char*)malloc(len+1);
	if(read(fd,str,len) != len) return 1;
	str[len] = 0; *(char**)(data+dump[i].offset) = str;
	break;
      }
    case DUMP_TYPE_CAJ_STR:
      {
	uint32_t len;
	if(dump_read_u32(fd, &len)) return 1;
	unsigned char *buf = (unsigned char*)malloc(len+1);
	if(read(fd,buf,len) != len) return 1;
	buf[len] = 0;

	caj_string *str = (caj_string*)(data+dump[i].offset);
	str->len = len; str->data = buf;
	break;
      }
    default:
      printf("FIXME: bad type %i in undump_object\n", dump[i].type);
      return 1;
    }
  }
  return 0;
}

static int dump_prim(int fd, primitive_obj *prim) {
  if(dump_write_u32(fd, PRIM_MAGIC_V1)) return 1;
  return dump_object(fd, prim_dump_v1, prim);
}

static int load_prim_v1(int fd, simulator_ctx *sim) {
  primitive_obj *prim = new primitive_obj();
  prim->ob.type = OBJ_TYPE_PRIM;
  prim->name = prim->description = NULL;
  prim->tex_entry.data = NULL;
  if(undump_object(fd, prim_dump_v1, prim)) {
    free(prim->name); free(prim->description);
    free(prim->tex_entry.data); delete prim;
    return 1;
  }
  prim->ob.phys = NULL; prim->ob.chat = NULL;
  prim->crc_counter = 0; 
  prim->inv.num_items = prim->inv.alloc_items = 0;
  prim->inv.items = NULL; prim->inv.serial = 0;
  prim->inv.filename = NULL;
  world_insert_obj(sim, &prim->ob);
  printf("DEBUG: loaded a prim from saved state\n");
  // FIXME - TODO
  return 0;
}

void world_int_dump_prims(simulator_ctx *sim) {
  int fd = open("simstate.dat.new", O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if(fd < 0) {
    printf("ERROR: couldn't open file to save simstate\n"); return;
  }
  for(std::map<uint32_t,world_obj*>::iterator iter = sim->localid_map.begin();
      iter != sim->localid_map.end(); iter++) {
    if(iter->second->type == OBJ_TYPE_PRIM) {
      primitive_obj *prim = (primitive_obj*) iter->second;
      if(dump_prim(fd, prim)) {
	printf("ERROR: dump_prims failed saving %lu\n", (unsigned long)prim->ob.local_id);
	close(fd); return;
      }
    }
  }
  if(close(fd) < 0) { printf("ERROR: dump_prims write error?\n"); return; }
  if(rename("simstate.dat.new","simstate.dat")) {
    printf("ERROR: dump_prims failed to rename simstate\n"); return;
  }
}

void world_int_load_prims(simulator_ctx *sim) {
  int fd = open("simstate.dat", O_RDONLY, 0644);
  if(fd < 0) {
    printf("ERROR: couldn't open saved simstate\n"); return;
  }
  for(;;) {
    uint32_t val;
    if(dump_read_u32(fd, &val)) break;
    switch(val) {
    case PRIM_MAGIC_V1:
      if(load_prim_v1(fd, sim)) {
	printf("ERROR: failure loading v1 prim from saved simstate\n"); 
	close(fd); return;
      }
      break;
    default:
      printf("ERROR: unexpected magic in simstate\n"); close(fd); return;
    }
  }
  close(fd); 
}

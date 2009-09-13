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

#include "sl_messages.h"
#include "sl_udp_proto.h"
#include "caj_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 2048

void sl_dump_packet(struct sl_message* msg) {
  int i,j,k;
  printf("Packet %s, flags 0x%x, sequence %u:\n", msg->tmpl->name,
	 (int)msg->flags, (unsigned int)msg->seqno);

  for(i = 0; i < msg->tmpl->num_blocks; i++) {
    struct sl_block_tmpl* bt = msg->tmpl->blocks+i;
    int blkcnt = msg->blocks[i].count;
    printf("  Block %s, %i instances\n", bt->name, blkcnt);
    for(j = 0; j < blkcnt; j++) {
      unsigned char *blk = (unsigned char*)msg->blocks[i].data[j];
      for(k = 0; k < bt->num_vals; k++) {
	switch(bt->vals[k].type) {
	case SL_MSG_U8:
	  printf("    %s = %i\n", bt->vals[k].name, (int)*(uint8_t*)(blk+bt->vals[k].offset));
	  break;
	case SL_MSG_U16:
	  printf("    %s = %i\n", bt->vals[k].name, (int)*(uint16_t*)(blk+bt->vals[k].offset));
	  break;
	case SL_MSG_U32:
	  printf("    %s = %u\n", bt->vals[k].name, 
		 (unsigned int)*(uint32_t*)(blk+bt->vals[k].offset));
	  break;
	case SL_MSG_S8:
	  printf("    %s = %i\n", bt->vals[k].name, (int)*(int8_t*)(blk+bt->vals[k].offset));
	  break;
	case SL_MSG_S16:
	  printf("    %s = %i\n", bt->vals[k].name, (int)*(int16_t*)(blk+bt->vals[k].offset));
	  break;
	case SL_MSG_S32:
	  printf("    %s = %i\n", bt->vals[k].name, 
		 (int)*(int32_t*)(blk+bt->vals[k].offset));
	  break;
	case SL_MSG_F32:
	  printf("    %s = %f\n", bt->vals[k].name, 
		 (double)*(float*)(blk+bt->vals[k].offset));
	  break;
	case SL_MSG_LLVECTOR3:
	  {
	    struct caj_vector3 *vect = (struct caj_vector3*)(blk+bt->vals[k].offset);
	    printf("    %s = (%f, %f, %f)\n", bt->vals[k].name, 
		   (double)vect->x, (double)vect->y, (double)vect->z);
	    break;
	  }
	case SL_MSG_LLQUATERNION:
	  {
	    struct caj_quat *quat = (struct caj_quat*)(blk+bt->vals[k].offset);
	    printf("    %s = quat(%f, %f, %f, %f)\n", bt->vals[k].name, 
		   (double)quat->x, (double)quat->y, 
		   (double)quat->z, (double)quat->w);
	    break;
	  }
	case SL_MSG_LLUUID:
	  {
	    unsigned char* u = blk+bt->vals[k].offset;
	    printf("    %s = %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
		   bt->vals[k].name, (int)u[0], (int)u[1], (int)u[2], (int)u[3], (int)u[4], (int)u[5], (int)u[6], (int)u[7], (int)u[8], (int)u[9], (int)u[10], (int)u[11], (int)u[12], (int)u[13], (int)u[14], (int)u[15]);
	    break;
	  }
	case SL_MSG_BOOL:
	  printf("    %s = %s\n", bt->vals[k].name, 
		 (*(int*)(blk+bt->vals[k].offset))?"true":"false");
	  break;
	case SL_MSG_VARIABLE1:
	case SL_MSG_VARIABLE2:
	  {
	    caj_string *str = (caj_string*)(blk+bt->vals[k].offset);
	    printf("    %s = (variable, length %i)\n",
		   bt->vals[k].name, str->data != NULL ? str->len : -1);
	    break;
	  }
	default:
	  printf("    %s (unhandled type)\n",  bt->vals[k].name);
	  break;
	}
      }
    }
  }

}

static int sl_zerodecode(const unsigned char* datin, int len, unsigned char* datout, int outlen) {
  int cnt = 0;
  while(len > 0) {
    if(*datin == 0) {
      if(cnt + *(++datin) >= outlen) return 0;
      memset(datout+cnt,0,*datin); 
      cnt += *datin; datin++; len -= 2;
    } else {
      if(cnt >= outlen) return 0;
      datout[cnt++] = *(datin++); len--;
    }
  }
  return cnt;
}

int sl_parse_message(unsigned char* data, int len, struct sl_message* msgout) {
  int i,j,k; unsigned char buf[BUFFER_SIZE];

  msgout->acks = NULL; msgout->blocks = NULL; msgout->tmpl = NULL;
  if(len < 10) return 1; 
  msgout->flags = data[0];
  msgout->seqno = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4];
  msgout->num_appended_acks = 0;
  if(msgout->flags & MSG_APPENDED_ACKS) {
    msgout->num_appended_acks = data[--len];
    msgout->acks = (uint32_t*)calloc(sizeof(uint32_t), msgout->num_appended_acks);
    for(i = 0; i < msgout->num_appended_acks; i++) {
      len -= 4; msgout->acks[i] = (data[len] << 24) | 
		  (data[len+1] << 16) | (data[len+2] << 8) | data[len+3];
      if(len < 10) { printf("Acks ate our packet!\n"); return 1; }
    }
  }
  len -= 6;

  if(data[5] != 0) {
    printf("Packet with data[5] != 0, unhandled case, aborting!\n"); return 1;
  }

  if(msgout->flags & MSG_ZEROCODED) {
    //memcpy(buf,data,6);
    len = sl_zerodecode(data+6,len,buf+6,BUFFER_SIZE-6);
    if(len <= 0) {
      printf("Zerodecode failed, aborting!\n"); return 1;
    }
    data = buf;
  }
  
  if(data[6] == 0xff) {
    if(data[7] == 0xff) {
      int packetno = data[8] << 8 | data[9];
      if(packetno >= SL_FIXED_MSG_MASK) {
	msgout->tmpl = sl_fixed_msg_map[packetno - SL_FIXED_MSG_MASK];
      } else if(packetno >= SL_NUM_LOW_MSGS) {
	printf("Bogus LOW packetno %i\n", packetno); return 1;
      } else {
	msgout->tmpl = sl_low_msg_map[packetno];
      }
      if(msgout->tmpl == NULL) printf("Packet LOW %i not found\n", packetno);
      len -= 4; data += 10;
    } else {
      int packetno = data[7];
      if(packetno >= SL_NUM_MED_MSGS) {
	printf("Bogus MED packetno %i\n", packetno); return 1;      
      }
      msgout->tmpl = sl_med_msg_map[packetno];
      if(msgout->tmpl == NULL) printf("Packet MED %i not found\n", packetno);
      len -= 2; data += 8;
    }
  } else {
    int packetno = data[6];
    if(packetno >= SL_NUM_HIGH_MSGS) {
	printf("Bogus HIGH packetno %i\n", packetno); return 1;      
      return 1;
    }
    msgout->tmpl = sl_high_msg_map[packetno];
    if(msgout->tmpl == NULL) printf("Packet HIGH %i not found\n", packetno);
    len -= 1; data += 7;
  }
  if(msgout->tmpl == NULL) return 1;
  //printf("Got a %s packet\n", msgout->tmpl->name);

  msgout->blocks = (struct sl_msg_block*)calloc(sizeof(struct sl_msg_block), msgout->tmpl->num_blocks);
  for(i = 0; i < msgout->tmpl->num_blocks; i++) {
    struct sl_block_tmpl* bt = msgout->tmpl->blocks+i;
    int blkcnt = bt->num_inst;
    unsigned char  *blk, **blks;
    if(len == 0) { printf("Premature end of packet\n"); return 0; }
    if(blkcnt == 0) {
      blkcnt = data[0]; len--; data++;
    }
    msgout->blocks[i].count = blkcnt;
    msgout->blocks[i].data = blks = (unsigned char**)calloc(sizeof(void*), blkcnt);
    for(j = 0; j < blkcnt; j++) {
      blks[j] = blk = (unsigned char*)calloc(bt->struct_len, 1);
      for(k = 0; k < bt->num_vals; k++) {
#define COPY_NUM_FIELD(t,c) if(len < sizeof(t)) { printf("Unexpected end of packet\n"); return 1;} \
	*(t*)(blk+bt->vals[k].offset) = c; len -= sizeof(t); data += sizeof(t);
	switch(bt->vals[k].type) {
	case SL_MSG_U8:
	case SL_MSG_S8:
	  COPY_NUM_FIELD(uint8_t, data[0]); break;
	case SL_MSG_U16:
	case SL_MSG_S16:
	  COPY_NUM_FIELD(uint16_t, data[0] | ((uint16_t)data[1] << 8)); break;
	case SL_MSG_U32:
	case SL_MSG_S32:
	  COPY_NUM_FIELD(uint32_t, data[0] | ((uint32_t)data[1] << 8) | 
			((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24)); break;
	case SL_MSG_U64:
	case SL_MSG_S64:
	  COPY_NUM_FIELD(uint64_t, data[0] | ((uint64_t)data[1] << 8) | 
			((uint64_t)data[2] << 16) | ((uint64_t)data[3] << 24) |
			((uint64_t)data[4] << 32) | ((uint64_t)data[5] << 40) |
			 ((uint64_t)data[6] << 48) | ((uint64_t)data[7] << 56)); 
	  break;
	case SL_MSG_LLUUID:
	  if(len < 16) { printf("Unexpected end of packet\n"); return 1;}
	  memcpy(blk+bt->vals[k].offset, data, 16);
	  len -= 16; data += 16;
	  break;
	case SL_MSG_VARIABLE1:
	  {
	    struct caj_string *str = (struct caj_string*)(blk+bt->vals[k].offset);
	    if(len < 1) { printf("Unexpected end of packet\n"); return 1;}
	    int tmp = data[0];
	    if(len < tmp+1) { 
	      printf("Unexpected end of packet %s in VARIABLE1 of len %i; only %i remaining\n",
		     msgout->tmpl->name,tmp,len); 
	      return 1;
	    }
	    str->data = malloc(tmp+1); str->len = tmp;
	    memcpy(str->data, data+1, tmp);
	    str->data[tmp] = 0; // ensure null termination, just in case
	    len -= tmp+1; data += tmp+1;
	    break;
	  }
	case SL_MSG_VARIABLE2:
	  {
	    struct caj_string *str = (struct caj_string*)(blk+bt->vals[k].offset);
	    if(len < 2) { printf("Unexpected end of packet\n"); return 1;}
	    int tmp = data[0] | (data[1] << 8);
	    if(len < tmp+2) { 
	      printf("Unexpected end of packet %s in VARIABLE2 of len %i; only %i remaining\n",
		     msgout->tmpl->name,tmp,len); 
	      return 1;
	    }
	    str->data = malloc(tmp+1); str->len = tmp;
	    memcpy(str->data, data+2, tmp);
	    str->data[tmp] = 0; // ensure null termination, just in case
	    len -= tmp+2; data += tmp+2;
	    break;
	  }
	case SL_MSG_BOOL:
	  if(len < 1) { printf("Unexpected end of packet\n"); return 1;}
	  *(int*)(blk+bt->vals[k].offset) = *(data++); len--;
	  break;
	case SL_MSG_F32:
	  if(len < 4) { printf("Unexpected end of packet\n"); return 1;}
	  *(float*)(blk+bt->vals[k].offset) = caj_bin_to_float_le(data);
	  len -= 4; data += 4;
	  break;
	/* case SL_MSG_F64: */
	case SL_MSG_FIXED:
	  {
	    char *outbuf = (char*)(blk+bt->vals[k].offset);
	    int tmp = bt->vals[k].size;
	    if(len < tmp) { printf("Unexpected end of packet\n"); return 1;}
	    memcpy(outbuf, data,tmp);
	    len -= tmp; data += tmp;
	    break;
	  }
	case SL_MSG_LLQUATERNION:
	  if(len < 12) { printf("Unexpected end of packet\n"); return 1;}
	  caj_bin3_to_quat_le((struct caj_quat*)(blk+bt->vals[k].offset),
			      data);
	  len -= 12; data += 12;
	  break;
	case SL_MSG_LLVECTOR3:
	  if(len < 12) { printf("Unexpected end of packet\n"); return 1;}
	  caj_bin_to_vect3_le((struct caj_vector3*)(blk+bt->vals[k].offset),
			      data);
	  len -= 12; data += 12;
	  break;
	/* case SL_MSG_LLVECTOR4 */
	/* case SL_MSG_LLVECTOR3D */
	/* case SL_MSG_IPADDR */
	/* case SL_MSG_IPPORT */
	default:
	  printf("Error: unhandled type %i parsing message\n", bt->vals[k].type);
	  return 1;
	}
      }
    }
  }
  //sl_dump_packet(msgout);
  return 0;
}

int sl_pack_message(struct sl_message* msg, unsigned char* data, int buflen) {
  int len = 0; unsigned char *rawmsg = data+6; int i,j,k, tmp;
  if(buflen < 10) return 0; buflen -= 6;
  msg->flags &= ~MSG_ZEROCODED; // FIXME - handle zerocoding!
  data[0] = msg->flags;
  *(uint32_t*)(data+1) = htonl(msg->seqno);
  data[5] = 0;
  if(msg->tmpl->number & 0xffff0000) {
    *(uint32_t*)(rawmsg) = htonl(msg->tmpl->number);
    len += 4;
  } else if(msg->tmpl->number & 0xff00) {
    *(uint16_t*)(rawmsg) = htons(msg->tmpl->number);
    len += 2;
  } else {
    *rawmsg = msg->tmpl->number; len++;
  }
  for(i = 0; i < msg->tmpl->num_blocks; i++) {
    struct sl_block_tmpl* bt = msg->tmpl->blocks+i;
    int blkcnt = msg->blocks[i].count;
    if(bt->num_inst == 0) {
      if(len >= buflen) { printf("Packet %s overran buffer at start of %s\n",
				 msg->tmpl->name, bt->name); return 0;}
      rawmsg[len++] = blkcnt;
    } else {
      if(bt->num_inst != blkcnt) {
	printf("Bad block count for %s.%s: %i\n", msg->tmpl->name, bt->name, blkcnt); 
	return 0;
      }
    }
    for(j = 0; j < blkcnt; j++) {
      unsigned char *blk = msg->blocks[i].data[j];
      for(k = 0; k < bt->num_vals; k++) {
#define OUT_NUM_FIELD(t,cmd) { if(len+sizeof(t) > buflen) { printf("Packet %s overran buffer packing %s.%s\n", msg->tmpl->name, bt->name, bt->vals[k].name); return 0;}; \
	  t val = *(t*)(blk+bt->vals[k].offset); cmd; /*len += sizeof(t); - oops */ }
	switch(bt->vals[k].type) {
	case SL_MSG_U8:
	case SL_MSG_S8: // assuming 2's-complement math
	  OUT_NUM_FIELD(uint8_t, rawmsg[len++] = val); break;
	case SL_MSG_U16:
	case SL_MSG_S16:
	  OUT_NUM_FIELD(uint16_t, ((rawmsg[len++] = val&0xff), (rawmsg[len++] = val >> 8))); break;
	case SL_MSG_U32:
	case SL_MSG_S32:
	  OUT_NUM_FIELD(uint32_t, ((rawmsg[len++] = val&0xff), (rawmsg[len++] = val >> 8), (rawmsg[len++] = val >> 16), (rawmsg[len++] = val >> 24))); break;
	case SL_MSG_U64:
	  OUT_NUM_FIELD(uint64_t, ((rawmsg[len++] = val&0xff), (rawmsg[len++] = val >> 8), (rawmsg[len++] = val >> 16), (rawmsg[len++] = val >> 24), (rawmsg[len++] = val >> 32), (rawmsg[len++] = val >> 40), (rawmsg[len++] = val >> 48), (rawmsg[len++] = val >> 56))); break;
	case SL_MSG_LLUUID:
	  if(len+16 > buflen) { printf("Packet %s overran buffer packing %s.%s\n", msg->tmpl->name, bt->name, bt->vals[k].name); return 0;}
	  memcpy(rawmsg+len,blk+bt->vals[k].offset, 16);
	  len += 16;
	  break;
	case SL_MSG_VARIABLE1:
	  {
	    struct caj_string *str = (struct caj_string*)(blk+bt->vals[k].offset);
	    tmp = str->len;
	    if(tmp > 0xff) tmp = 0xff;
	    if(len+tmp+1 > buflen) { printf("Packet %s overran buffer packing %s.%s of len %i\n", msg->tmpl->name, bt->name, bt->vals[k].name, tmp); return 0;}
	    rawmsg[len++] = tmp;
	    memcpy(rawmsg+len,str->data,tmp);
	    len += tmp;
	    break;
	  }
	case SL_MSG_VARIABLE2:
	  {
	    struct caj_string *str = (struct caj_string*)(blk+bt->vals[k].offset);
	    tmp = str->len;
	    if(tmp > 0xffff) tmp = 0xffff;
	    if(len+tmp+2 > buflen) { printf("Packet %s overran buffer packing %s.%s of len %i\n", msg->tmpl->name, bt->name, bt->vals[k].name, tmp); return 0;}
	    rawmsg[len++] = tmp&0xff; rawmsg[len++] = tmp >> 8;
	    memcpy(rawmsg+len,str->data,tmp);
	    len += tmp;
	    break;
	  }
	case SL_MSG_BOOL:
	  if(len+4 > buflen) { printf("Packet %s overran buffer packing %s.%s\n", msg->tmpl->name, bt->name, bt->vals[k].name); return 0;}
	  rawmsg[len++] =  (*(int*)(blk+bt->vals[k].offset) ? 1 : 0);
	  break;
	case SL_MSG_F32:
	  if(len+4 > buflen) { printf("Packet %s overran buffer packing %s.%s\n", msg->tmpl->name, bt->name, bt->vals[k].name); return 0;}
	  caj_float_to_bin_le(rawmsg+len, *(float*)(blk+bt->vals[k].offset));
	  len += 4;
	  break;
	case SL_MSG_F64: // TODO
	  printf("Error: unhandled type F64 packing message %s\n", 
		 msg->tmpl->name);
	  return 0;	  
	case SL_MSG_FIXED:
	  tmp = bt->vals[k].size;
	  if(len+tmp > buflen) { printf("Packet %s overran buffer packing %s.%s\n", msg->tmpl->name, bt->name, bt->vals[k].name); return 0;}
	  memcpy(rawmsg+len, (char*)(blk+bt->vals[k].offset),tmp);
	  len += tmp;
	  break;
	case SL_MSG_LLVECTOR3:
	  if(len+12 > buflen) { printf("Packet %s overran buffer packing %s.%s\n", msg->tmpl->name, bt->name, bt->vals[k].name); return 0;}
	  caj_vect3_to_bin_le(rawmsg+len, (caj_vector3*)(blk+bt->vals[k].offset));
	  len += 12;
	  break;
	case SL_MSG_LLQUATERNION: // TODO
	  printf("Error: unhandled type LLQUATERNION packing message %s\n", 
		 msg->tmpl->name);
	  return 0;	  
	case SL_MSG_LLVECTOR4: // TODO
	  printf("Error: unhandled type LLVECTOR4 packing message %s\n", 
		 msg->tmpl->name);
	  return 0;	  
	case SL_MSG_LLVECTOR3D: // TODO
	  printf("Error: unhandled type LLVECTOR3D packing message %s\n", 
		 msg->tmpl->name);
	  return 0;	  
	case SL_MSG_IPADDR: // TODO
	  printf("Error: unhandled type IPADDR packing message %s\n", 
		 msg->tmpl->name);
	  return 0;	  
	case SL_MSG_IPPORT: // TODO
	  printf("Error: unhandled type IPPORT packing message %s\n", 
		 msg->tmpl->name);
	  return 0;	  
	default:
	  printf("Error: unhandled type %i packing message %s\n", 
		 bt->vals[k].type, msg->tmpl->name);
	  return 0;
	}
      }
    }
  }
  
  return len+6;
}

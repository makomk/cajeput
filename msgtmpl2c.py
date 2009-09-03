# msgtmpl2c.py - generates a C description of the Second Life protocol

# Copyright (c) 2009 Aidan Thornton, all rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AIDAN THORNTON ''AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL AIDAN THORNTON BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#!/usr/bin/python
from msgtmpl_core import MessageTemplate

_tmpl2c_typemap = {"U8":"uint8_t","U16":"uint16_t","U32":"uint32_t",
                   "U64":"uint64_t",
                   "S8":"int8_t","S16":"int16_t","S32":"int32_t",
                   "S64":"int64_t", "LLUUID":"uuid_t","Variable":"caj_string",
                   "BOOL":"int","F32":"float","F64":"double",
                   "Fixed":"char*","LLQuaternion":"caj_quat",
                   "LLVector3":"caj_vector3","LLVector4":"caj_vector4",
                   "LLVector3d":"caj_vector3_dbl",
                   "IPADDR":"uint32_t","IPPORT":"uint16_t",
                   }

FIXED_MASK = 0xfff0

if __name__ == '__main__':
    tmpl = MessageTemplate(file('message_template.msg','r'))
    num_low = 0; num_med = 0; num_high = 0;
    outh = file('sl_messages.h','w')    
    outh.write("""/* Generated file - do not edit! */
#ifndef SL_MESSAGES_H
#define SL_MESSAGES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "caj_types.h"
#include <stdint.h>
#include <uuid/uuid.h>

struct sl_message_tmpl {
   int zerocoded;
   uint32_t number;
   const char* name;
   int num_blocks;
   struct sl_block_tmpl* blocks;
};

struct sl_block_tmpl {
   int num_inst;
   int num_vals;
   int struct_len;
   const char* name;
   struct sl_val_tmpl* vals;
};

struct sl_val_tmpl {
   int type;
   int offset;
   const char* name;
   int size;
};

struct sl_msg_block {
    int count;
    unsigned char** data;
};

struct sl_message {
    uint8_t flags;
    uint32_t seqno;
    uint32_t msgno;
    int num_appended_acks;
    struct sl_message_tmpl* tmpl;
    struct sl_msg_block* blocks;
    uint32_t *acks;
};

#define MSG_APPENDED_ACKS 0x10 
#define MSG_RESENT 0x20 
#define MSG_RELIABLE 0x40 
#define MSG_ZEROCODED 0x80

#define SL_MSG_U8 1
#define SL_MSG_U16 2
#define SL_MSG_U32 3
#define SL_MSG_U64 4
#define SL_MSG_S8 5
#define SL_MSG_S16 6
#define SL_MSG_S32 7
#define SL_MSG_S64 8
#define SL_MSG_LLUUID 9
#define SL_MSG_VARIABLE1 10
#define SL_MSG_VARIABLE2 11
#define SL_MSG_BOOL 12
#define SL_MSG_F32 13
#define SL_MSG_F64 14
#define SL_MSG_FIXED 15
#define SL_MSG_LLQUATERNION 16
#define SL_MSG_LLVECTOR3 17
#define SL_MSG_LLVECTOR4 18
#define SL_MSG_LLVECTOR3D 19
#define SL_MSG_IPADDR 20
#define SL_MSG_IPPORT 21

#define SL_GETBLK(msgid,blk,msg) ((msg)->blocks[SL_BLKIDX_##msgid##_##blk])
#define SL_GETBLKI(msgid,blk,msg,idx) ((struct sl_blk_##msgid##_##blk *)(msg)->blocks[SL_BLKIDX_##msgid##_##blk].data[idx])
#define SL_GETBLK1(msgid,blk,msg) SL_GETBLKI(msgid,blk,msg,0)
#define SL_DECLBLK_GET1(msgid, blk, name, msg) struct sl_blk_##msgid##_##blk *name = SL_GETBLK1(msgid,blk,msg)
#define SL_MKBLK(msg, blk) ((struct sl_blk_##msg##_##blk *)calloc(sizeof(struct sl_blk_##msg##_##blk), 1))

#define SL_ADDBLK(msgid, blk, msg) ((struct sl_blk_##msgid##_##blk*) sl_bind_block(SL_MKBLK(msgid,blk),&SL_GETBLK(msgid,blk,msg)));
#define SL_INITMSG(msgid,msg) sl_new_message(&sl_msgt_##msgid,msg);
#define SL_DECLMSG(msgid,name) struct sl_message name; sl_new_message(&sl_msgt_##msgid,&name)
#define SL_DECLBLK_ONLY(msgid, blk, name) struct sl_blk_##msgid##_##blk *name
#define SL_DECLBLK(msgid, blk, name, msg) struct sl_blk_##msgid##_##blk *name = SL_ADDBLK(msgid, blk, msg)


extern void sl_new_message(struct sl_message_tmpl* tmpl, struct sl_message* msgout);
extern void* sl_bind_block(void* block, struct sl_msg_block *desc);
extern void sl_free_msg(struct sl_message* msg);

""");
    outc = file('sl_messages.c','w')    
    outc.write("""/* Generated file - do not edit! */\n
#include "sl_messages.h"
#include <stddef.h>
#include <stdlib.h>

void sl_new_message(struct sl_message_tmpl* tmpl, struct sl_message* msgout) {
    msgout->flags = tmpl->zerocoded ? MSG_ZEROCODED : 0;
    msgout->seqno = 0; msgout->msgno = tmpl->number;
    msgout->tmpl = tmpl; msgout->num_appended_acks = 0;
    msgout->blocks = (struct sl_msg_block*)calloc(tmpl->num_blocks, sizeof(struct sl_msg_block));
    msgout->acks = NULL;
}

void* sl_bind_block(void* block, struct sl_msg_block *desc) {
  if(desc->data != NULL) {
    desc->data = (unsigned char**)realloc(desc->data, (desc->count+1)*sizeof(void*));
    desc->data[desc->count++] = (unsigned char*)block;
  } else {
    desc->data = (unsigned char**)malloc(sizeof(void*));
    desc->data[0] = (unsigned char*)block; desc->count = 1;
  }
  return block;
}

void sl_free_msg(struct sl_message* msg) {
  int i,j,k;
  if(msg->tmpl == NULL || msg->blocks == NULL) return;
  for(i = 0; i < msg->tmpl->num_blocks; i++) {
    struct sl_msg_block *blk = msg->blocks+i;
    struct sl_block_tmpl* bt = msg->tmpl->blocks+i;
    if(blk->data != NULL) {
      for(j = 0; j < blk->count; j++) {
        char *data = blk->data[j];
        if(data != NULL) {
          for(k = 0; k < bt->num_vals; k++) {
            struct caj_string *str;
            switch(bt->vals[k].type) {
            case SL_MSG_VARIABLE1:
            case SL_MSG_VARIABLE2:
               str = (struct caj_string*)(data+bt->vals[k].offset);
               caj_string_free(str);
               break;
            }
          }
	  free(data);
        }
      }
      free(blk->data);
    }
  }
  free(msg->blocks);
  msg->blocks = NULL;
  free(msg->acks);
  msg->acks = NULL;
}

""")
    for msgnum in tmpl.low_msgs.iterkeys():
        if msgnum < FIXED_MASK:
            num_low = max(num_low, msgnum+1)
    outh.write("#define SL_NUM_LOW_MSGS %i\n" % num_low);
    for msgnum in tmpl.med_msgs.iterkeys():
        num_med = max(num_med, msgnum+1)
    outh.write("#define SL_NUM_MED_MSGS %i\n" % num_med);
    for msgnum in tmpl.high_msgs.iterkeys():
        num_high = max(num_high, msgnum+1)
    outh.write("#define SL_NUM_HIGH_MSGS %i\n" % num_high);
    outh.write("#define SL_FIXED_MSG_MASK %i\n\n" % FIXED_MASK);
    for msg in tmpl.msgs.itervalues():
        for block in msg.blocks:
            outh.write("struct sl_blk_%s_%s {\n" % (msg.name, block.name))
            outh.write("    struct sl_block_tmpl* tmpl;\n")
            for field in block.fields:
                if field.type == "Fixed":
                    outh.write("    char %s[%i];\n" % (field.name,field.size));
                else: 
                    outh.write("    %s %s;\n" % (_tmpl2c_typemap[field.type],
                                                 field.name));
            outh.write("};\n\n");

            outc.write("struct sl_val_tmpl sl_vt_%s_%s[] = {\n" %
                       (msg.name, block.name));
            for field in block.fields:
                sz = 0;
                if field.type == 'Fixed':
                    sz = field.size
                outc.write("    { SL_MSG_%s, offsetof(struct sl_blk_%s_%s, %s), \"%s\", %i }, \n" %
                           (field.fulltype.replace(' ','').upper(), msg.name,
                            block.name, field.name, field.name, sz));
            outc.write("};\n\n");
                           
            
        outh.write("extern struct sl_block_tmpl sl_bt_%s[];\n" %
                   msg.name)
        outc.write("struct sl_block_tmpl sl_bt_%s[] = {\n" %
                   msg.name)
        for block in msg.blocks:
            if block.count == None:
                num_inst = 0
            else: num_inst = block.count
            outc.write("    { %i, %i, sizeof(struct sl_blk_%s_%s), \"%s\", sl_vt_%s_%s },\n" %
                       (num_inst, len(block.fields), msg.name, block.name,
                        block.name, msg.name, block.name))
        for i in range(0, len(msg.blocks)):
            outh.write("#define SL_BLKIDX_%s_%s %i\n" % (msg.name, msg.blocks[i].name, i));
#            outh.write("#define SL_MKBLK_%(msg)s_%(blk)s ((struct sl_blk_%(msg)s_%(blk)s*)calloc(sizeof(struct sl_blk_%(msg)s_%(blk)s), 1))\n" %
#                       {'msg':msg.name, 'blk':msg.blocks[i].name});
            #outh.write("#define SL_GETBLK_%(msg)s_%(blk)s(msg) ((msg)->blocks[%(i)i])\n" %
            #           {'msg':msg.name, 'blk':msg.blocks[i].name, 'i':i});
        outc.write("};\n");
        outh.write("extern struct sl_message_tmpl sl_msgt_%s;\n" % msg.name);
        outc.write("struct sl_message_tmpl sl_msgt_%s = {\n" % msg.name);
        if len(msg.packedid) == 4:
            msgid = 0xffff0000 |msg.number
        elif len(msg.packedid) == 2:
            msgid = 0xff00 |msg.number
        elif len(msg.packedid) == 1:
            msgid = msg.number
        else: assert False
        outc.write("    %i, %s, \"%s\", %i, sl_bt_%s\n" %
                   (msg.zerocoded, hex(msgid), msg.name, len(msg.blocks), msg.name));
        outc.write("};\n");
            #outc.write
        pass
    def write_msg_map(name, msglist, cnt,offset=0):
        outh.write("extern struct sl_message_tmpl *sl_%s_msg_map[];\n" % name)
        outc.write("struct sl_message_tmpl *sl_%s_msg_map[] = {\n" % name)
        for i in range(offset, cnt):
            if msglist.has_key(i):
                outc.write("    &sl_msgt_%s,\n" % msglist[i].name)
            else: outc.write("    NULL,\n")
        outc.write("};\n\n");
    write_msg_map("low", tmpl.low_msgs, num_low);
    write_msg_map("med", tmpl.med_msgs, num_med);
    write_msg_map("high", tmpl.high_msgs, num_high);
    write_msg_map("fixed", tmpl.low_msgs, 0x10000, FIXED_MASK);
    
    outh.write("""
#ifdef __cplusplus
}
#endif

#endif /* !SL_MESSAGES_H*/
""")
    outh.close();
    outc.close();

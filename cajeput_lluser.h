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

#ifndef CAJEPUT_LLUSER_H
#define CAJEPUT_LLUSER_H

// for clients speaking the Linden Labs UDP protocol

struct lluser_sim_ctx;
struct lluser_ctx;

// FIXME - rename these to something saner
typedef void(*sl_msg_handler)(lluser_ctx*,sl_message*);
void register_msg_handler(struct lluser_sim_ctx *sim, sl_message_tmpl* tmpl, 
			  sl_msg_handler handler);

/* Note - this calls sl_free_msg for you, but this does *not* free
   the actual struct sl_message itself. In normal usage, that's a 
   local variable (i.e. stack allocated) */
void sl_send_udp(struct lluser_ctx* ctx, struct sl_message* msg);
void sl_send_udp_throt(struct lluser_ctx* ctx, struct sl_message* msg, int throt_id);

typedef std::multimap<sl_message_tmpl*,sl_msg_handler> msg_handler_map;
typedef std::multimap<sl_message_tmpl*,sl_msg_handler>::iterator msg_handler_map_iter;

struct lluser_ctx {
  struct user_ctx *u;
  struct lluser_sim_ctx *lsim;
  struct lluser_ctx *next;

  struct sockaddr_in addr;
  int sock;
  uint32_t counter;

  std::vector<uint32_t> pending_acks;
  std::set<uint32_t> seen_packets; // FIXME - clean this up

  // icky Linden stuff
  std::map<uint64_t,asset_xfer*> xfers;

  // Image transfers
  std::map<obj_uuid_t,image_request*> image_reqs;

};

struct lluser_sim_ctx {
  struct simulator_ctx *sim;
  int sock;
  struct lluser_ctx *ctxts;

  msg_handler_map msg_handlers;
  uint64_t xfer_id_ctr; 
};

#endif

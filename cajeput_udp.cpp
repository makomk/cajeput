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
//#include "sl_llsd.h"
#include "cajeput_core.h"
#include "cajeput_udp.h"
#include "cajeput_int.h"
#include <stdlib.h>
#include <cassert>

#define BUF_SIZE 2048

// FIXME - resend lost packets
void sl_send_udp(struct user_ctx* ctx, struct sl_message* msg) {
  unsigned char buf[BUF_SIZE]; int len, ret;
  msg->seqno = ctx->counter++;
  len = sl_pack_message(msg,buf,BUF_SIZE);
  if(len > 0) {
    ret = sendto(ctx->sock,buf,len,0,
		 (struct sockaddr*)&ctx->addr,sizeof(ctx->addr));
    if(ret <= 0) {
      //int err = errno;
      //printf("DEBUG: couldn't send message\n");
      perror("sending UDP message");
    }
  } else {
    printf("DEBUG: couldn't pack message, not sending\n");
  }
  sl_free_msg(msg);
}

// FIXME - split acks across multiple packets if necessary
static void send_pending_acks(struct simulator_ctx *sim) {
  struct user_ctx* ctx;
  for(ctx = sim->ctxts; ctx != NULL; ctx = ctx->next) {
    if(ctx->pending_acks.begin() == ctx->pending_acks.end()) 
      continue;
    SL_DECLMSG(PacketAck, acks);
    for(std::vector<uint32_t>::iterator iter = ctx->pending_acks.begin(); 
	iter != ctx->pending_acks.end(); iter++) {
      SL_DECLBLK(PacketAck, Packets, ack, &acks);
      ack->ID = *iter;
    }
    sl_send_udp(ctx,&acks);
    ctx->pending_acks.empty();
  }
}

#define VALIDATE_SESSION(ad) (uuid_compare(ctx->user_id, ad->AgentID) != 0 || uuid_compare(ctx->session_id, ad->SessionID) != 0)

#define AGENT_CONTROL_AT_POS (1<<0)
#define AGENT_CONTROL_AT_NEG (1<<1)
#define AGENT_CONTROL_LEFT_POS (1<<2)
#define AGENT_CONTROL_LEFT_NEG (1<<3)
#define AGENT_CONTROL_UP_POS (1<<4)
#define AGENT_CONTROL_UP_NEG (1<<5)


static void handle_AgentUpdate_msg(struct user_ctx* ctx, struct sl_message* msg) {
  struct sl_blk_AgentUpdate_AgentData *ad;
  if(SL_GETBLK(AgentUpdate,AgentData,msg).count != 1) return;
  ad = SL_GETBLKI(AgentUpdate,AgentData,msg,0);
  if(VALIDATE_SESSION(ad)) return;
  ctx->draw_dist = ad->Far;
  if(ctx->av != NULL) {
    if(!sl_quat_equal(&ctx->av->ob.rot, &ad->BodyRotation)) {
      ctx->av->ob.rot = ad->BodyRotation;
    }
    // FIXME - this is a horrid hack
    uint32_t control_flags = ad->ControlFlags;
    sl_vector3 force; 
    force.x = 0.0f; force.y = 0.0f; force.z = 0.0f;
    if(control_flags & AGENT_CONTROL_AT_POS)
      force.x = 200.0;
    if(control_flags & AGENT_CONTROL_AT_NEG)
      force.x = -200.0;
    if(control_flags & AGENT_CONTROL_LEFT_POS)
      force.y = -200.0;
    if(control_flags & AGENT_CONTROL_LEFT_NEG)
      force.y = 200.0;
    sl_mult_vect3_quat(&force,&ctx->av->ob.rot,&force);
    ctx->sim->physh.set_force(ctx->sim,ctx->sim->phys_priv,&ctx->av->ob,force);
  }
}

static void handle_StartPingCheck_msg(struct user_ctx* ctx, struct sl_message* msg) {
  struct sl_blk_StartPingCheck_PingID *ping;
  if(SL_GETBLK(StartPingCheck,PingID,msg).count != 1) return;
  ping = SL_GETBLKI(StartPingCheck,PingID,msg,0);
  SL_DECLMSG(CompletePingCheck, pong);
  SL_DECLBLK(CompletePingCheck, PingID, pongid, &pong);
  pongid->PingID = ping->PingID;
  sl_send_udp(ctx,&pong);
}

void av_chat_callback(struct simulator_ctx *sim, struct world_obj *obj,
		       const struct chat_message *msg, void *user_data) {
  struct user_ctx* ctx = (user_ctx*)user_data;
  SL_DECLMSG(ChatFromSimulator, chat);
  SL_DECLBLK(ChatFromSimulator, ChatData, cdata, &chat);
  sl_string_set(&cdata->FromName, msg->name);
  uuid_copy(cdata->SourceID, msg->source);
  uuid_copy(cdata->OwnerID, msg->owner);
  cdata->SourceType = msg->source_type;
  cdata->ChatType = msg->chat_type;
  cdata->Audible = CHAT_AUDIBLE_FULLY;
  sl_string_set(&cdata->Message,msg->msg);
  sl_send_udp(ctx, &chat);
}

static void handle_ChatFromViewer_msg(struct user_ctx* ctx, struct sl_message* msg) {
    struct sl_blk_ChatFromViewer_AgentData *ad;
    struct sl_blk_ChatFromViewer_ChatData *cdata;
    struct chat_message chat;
    //struct sl_message amc;
    if(SL_GETBLK(ChatFromViewer,AgentData,msg).count != 1) return;
    ad = SL_GETBLKI(ChatFromViewer,AgentData,msg,0);
    if(VALIDATE_SESSION(ad)) return;
    if(SL_GETBLK(ChatFromViewer,ChatData,msg).count != 1) return;
    cdata = SL_GETBLKI(ChatFromViewer,ChatData,msg,0);
    printf("DEBUG: got ChatFromViewer\n");
    if(cdata->Channel < 0) return; // can't send on negative channels
    if(ctx->av == NULL) return; // I have no mouth and I must scream...
    if(cdata->Type == CHAT_TYPE_WHISPER || cdata->Type == CHAT_TYPE_NORMAL ||
       cdata->Type == CHAT_TYPE_SHOUT) {
      chat.channel = cdata->Channel;
      chat.pos = ctx->av->ob.pos;
      chat.name = ctx->name;
      uuid_copy(chat.source,ctx->user_id);
      uuid_clear(chat.owner); // FIXME - ???
      chat.source_type = CHAT_SOURCE_AVATAR;
      chat.chat_type = cdata->Type;
      chat.msg = (char*)cdata->Message.data;
      world_send_chat(ctx->sim, &chat);
    }
}

static void handle_CompleteAgentMovement_msg(struct user_ctx* ctx, struct sl_message* msg) {
    struct sl_blk_CompleteAgentMovement_AgentData *ad;
    //struct sl_message amc;
    if(SL_GETBLK(CompleteAgentMovement,AgentData,msg).count != 1) return;
    ad = SL_GETBLKI(CompleteAgentMovement,AgentData,msg,0);
    if(ad->CircuitCode != ctx->circuit_code || VALIDATE_SESSION(ad)) 
      return;
    if(!(ctx->flags & AGENT_FLAG_INCOMING)) {
      // return; // FIXME - how to figure this out?
    }
    ctx->flags &= ~AGENT_FLAG_CHILD;
    ctx->flags |= AGENT_FLAG_ENTERED;
    if(ctx->av == NULL) {
      ctx->av = (struct avatar_obj*)calloc(sizeof(struct avatar_obj),1);
      ctx->av->ob.type = OBJ_TYPE_AVATAR;
      ctx->av->ob.pos.x = 128.0f;
      ctx->av->ob.pos.y = 128.0f;
      ctx->av->ob.pos.z = 60.0f;
      ctx->av->ob.rot.x = ctx->av->ob.rot.y = ctx->av->ob.rot.z = 0.0f;
      ctx->av->ob.rot.w = 1.0f;
      uuid_copy(ctx->av->ob.id, ctx->user_id);
      world_insert_obj(ctx->sim, &ctx->av->ob);
      world_obj_listen_chat(ctx->sim,&ctx->av->ob,av_chat_callback,ctx);
      world_obj_add_channel(ctx->sim,&ctx->av->ob,0);

      ctx->sim->gridh.user_entered(ctx->sim, ctx, ctx->grid_priv);
    }
    printf("Got CompleteAgentMovement; sending AgentMovementComplete\n");
    SL_DECLMSG(AgentMovementComplete, amc);
    amc.flags |= MSG_RELIABLE;
    SL_DECLBLK(AgentMovementComplete, AgentData, ad2, &amc);
    SL_DECLBLK(AgentMovementComplete, Data, dat, &amc);
    SL_DECLBLK(AgentMovementComplete, SimData, simdat, &amc);
    uuid_copy(ad2->AgentID, ctx->user_id);
    uuid_copy(ad2->SessionID, ctx->session_id);
    dat->Position = ctx->av->ob.pos;
    dat->LookAt = dat->Position;
    dat->RegionHandle = ctx->sim->region_handle;
    dat->Timestamp = time(NULL);
    sl_string_set(&simdat->ChannelVersion, "OtherSim 0.001");
    sl_send_udp(ctx, &amc);
}

static void handle_LogoutRequest_msg(struct user_ctx* ctx, struct sl_message* msg) {
  struct sl_blk_LogoutRequest_AgentData *ad;
  if(SL_GETBLK(LogoutRequest,AgentData,msg).count != 1) return;
  ad = SL_GETBLKI(LogoutRequest,AgentData,msg,0);
  if(VALIDATE_SESSION(ad)) 
    return;
  ctx->flags |= AGENT_FLAG_IN_LOGOUT;
  SL_DECLMSG(LogoutReply, reply);
  SL_DECLBLK(LogoutReply, AgentData, ad2, &reply);
  uuid_copy(ad2->AgentID, ctx->user_id);
  uuid_copy(ad2->SessionID, ctx->session_id);
  sl_send_udp(ctx, &reply);
  user_session_close(ctx);
}

static float sl_unpack_float(unsigned char *buf) {
  float f;
  // FIXME - need to swap byte order if necessary.
  memcpy(&f,buf,sizeof(float));
  return f;
}

static void handle_AgentThrottle_msg(struct user_ctx* ctx, struct sl_message* msg) {
  struct sl_blk_AgentThrottle_AgentData *ad;
  struct sl_blk_AgentThrottle_Throttle *throt;
  if(SL_GETBLK(AgentThrottle,AgentData,msg).count != 1) return;
  ad = SL_GETBLKI(AgentThrottle,AgentData,msg,0);
  if(VALIDATE_SESSION(ad) || ad->CircuitCode != ctx->circuit_code) return;

  if(SL_GETBLK(AgentThrottle,Throttle,msg).count != 1) return;
  throt = SL_GETBLKI(AgentThrottle,Throttle,msg,0);

  // FIXME - need to check generation counter

  if(throt->Throttles.len < SL_NUM_THROTTLES*4) {
    printf("Error: AgentThrottle with not enough data\n");
  }

  printf("DEBUG: got new throttles:\n");
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    ctx->throttles[i] =  sl_unpack_float(throt->Throttles.data + 4*i);
    printf("  throttle %s: %f\n", sl_throttle_names[i], ctx->throttles[i]);
  }
}


void register_msg_handler(struct simulator_ctx *sim, sl_message_tmpl* tmpl, 
			  sl_msg_handler handler){
  sim->msg_handlers.insert(std::pair<sl_message_tmpl*,sl_msg_handler>(tmpl,handler));
}

// Handles incoming messages
static void dispatch_msg(struct user_ctx* ctx, struct sl_message* msg) {
  std::pair<msg_handler_map_iter, msg_handler_map_iter> iters =
    ctx->sim->msg_handlers.equal_range(msg->tmpl);
  for(msg_handler_map_iter iter = iters.first; iter != iters.second; iter++) {
    assert(iter->first == msg->tmpl);
    iter->second(ctx, msg);
  }
  user_reset_timeout(ctx);
}

static gboolean got_packet(GIOChannel *source,
			   GIOCondition condition,
			   gpointer data) {
  struct simulator_ctx* sim = (struct simulator_ctx*)data;
  struct sockaddr_in addr;
  int ret; socklen_t addrlen;
  unsigned char buf[BUF_SIZE];
  struct sl_message msg;
    send_pending_acks(sim); // FIXME - more suitable timing
    addrlen = sizeof(addr);
    ret = recvfrom(sim->sock, buf, BUF_SIZE, 0, 
		   (struct sockaddr*)&addr, &addrlen);
    //printf("Packet! %i\n", ret);
    if(sl_parse_message(buf, ret, &msg)) {
      printf("DEBUG: packet parse failed\n");
      goto out;
    };
    if(msg.tmpl == &sl_msgt_UseCircuitCode) {
       printf("DEBUG: handling UseCircuitCode\n");
     // FIXME - need to handle dupes
      struct sl_blk_UseCircuitCode_CircuitCode *cc;
      struct user_ctx* ctx; struct sl_message ack;
      struct sl_blk_PacketAck_Packets *ackack;
      if(SL_GETBLK(UseCircuitCode,CircuitCode,&msg).count != 1) goto out;
      //cc = SL_GETBLK_UseCircuitCode_CircuitCode(&msg).data[0];
      cc = SL_GETBLKI(UseCircuitCode,CircuitCode,&msg,0);
      if(cc == NULL) goto out;
      // FIXME: handle same port/addr, different agent case
      for(ctx = sim->ctxts; ctx != NULL; ctx = ctx->next) {
	if(ctx->circuit_code == cc->Code &&
	   uuid_compare(ctx->user_id, cc->ID) == 0 &&
	   uuid_compare(ctx->session_id, cc->SessionID) == 0) 
	  break;
      }
      if(ctx == NULL) {
	printf("DEBUG: got unexpected UseCircuitCode\n");
	sl_dump_packet(&msg);
	goto out;
      }
      if(ctx->addr.sin_port != 0) {
	// FIXME - check port/addr matches, resend ack
	printf("DEBUG: got dupe UseCircuitCode?\n");
	goto out;
      }
      ctx->addr = addr;

      sl_new_message(&sl_msgt_PacketAck,&ack);
      ackack = SL_ADDBLK(PacketAck, Packets,&ack);
      ackack->ID = msg.seqno;
      sl_send_udp(ctx,&ack);

      SL_DECLMSG(RegionHandshake, rh);
      SL_DECLBLK(RegionHandshake, RegionInfo, ri, &rh);
      SL_DECLBLK(RegionHandshake, RegionInfo2, ri2, &rh);
      SL_DECLBLK(RegionHandshake, RegionInfo3, ri3, &rh);
      rh.flags |= MSG_RELIABLE;
      ri->RegionFlags = 0; // FIXME
      ri->SimAccess = 0; // FIXME
      sl_string_set(&ri->SimName, sim->name);
      memset(ri->SimOwner, 0, 16);
      ri->IsEstateManager = 1; // for now; FIXME
      ri->WaterHeight = 20.0f;
      uuid_generate_random(ri->CacheID); // FIXME FIXME
      memset(ri->TerrainBase0,0,16); // should be OK, OpenSim gets away with it
      memset(ri->TerrainBase1,0,16);
      memset(ri->TerrainBase2,0,16);
      memset(ri->TerrainBase3,0,16);
      memset(ri->TerrainDetail0,0,16); // FIXME
      memset(ri->TerrainDetail1,0,16);
      memset(ri->TerrainDetail2,0,16);
      memset(ri->TerrainDetail3,0,16);
      ri->TerrainStartHeight00 = ri->TerrainStartHeight01 = 30.0f;
      ri->TerrainStartHeight10 = ri->TerrainStartHeight11 = 30.0f;
      ri->TerrainHeightRange00 = ri->TerrainHeightRange01 = 60.0f;
      ri->TerrainHeightRange10 = ri->TerrainHeightRange11 = 60.0f;
      uuid_copy(ri2->RegionID, sim->region_id);
      ri3->CPUClassID = 1; ri3->CPURatio = 1;
      sl_string_set(&ri3->ColoName, "Nowhere");
      sl_string_set(&ri3->ProductName, "Cajeput Server Demo");
      sl_string_set(&ri3->ProductSKU, "0");
      sl_send_udp(ctx,&rh);
      
    } else {
      struct user_ctx* ctx;
      for(ctx = sim->ctxts; ctx != NULL; ctx = ctx->next) {
	if(ctx->addr.sin_addr.s_addr == addr.sin_addr.s_addr &&
	   ctx->addr.sin_port == addr.sin_port) {
	  if(msg.flags & MSG_RELIABLE) {
	    // FIXME - need to do this for unknown messages too
	    ctx->pending_acks.push_back(msg.seqno);
	  }
	  std::set<uint32_t>::iterator iter = ctx->seen_packets.find(msg.seqno);
	  if(iter == ctx->seen_packets.end()) {
	    ctx->seen_packets.insert(msg.seqno);
	    dispatch_msg(ctx, &msg); break;
	  }
	}
      }
    }
 out:
    sl_free_msg(&msg);

    // FIXME - what should I return?
}

void sim_int_init_udp(struct simulator_ctx *sim)  {
  int sock; struct sockaddr_in addr;

  register_msg_handler(sim, &sl_msgt_AgentUpdate, handle_AgentUpdate_msg);
  register_msg_handler(sim, &sl_msgt_StartPingCheck, handle_StartPingCheck_msg);
  register_msg_handler(sim, &sl_msgt_CompleteAgentMovement, handle_CompleteAgentMovement_msg);
  register_msg_handler(sim, &sl_msgt_LogoutRequest, handle_LogoutRequest_msg);
  register_msg_handler(sim, &sl_msgt_ChatFromViewer, handle_ChatFromViewer_msg);

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  addr.sin_family= AF_INET;
  addr.sin_port = htons(sim->udp_port);
  addr.sin_addr.s_addr=INADDR_ANY;
  bind(sock, (struct sockaddr*)&addr, sizeof(addr));
  GIOChannel* gio_sock = g_io_channel_unix_new(sock);
  g_io_add_watch(gio_sock, G_IO_IN, got_packet, sim);
  sim->sock = sock;  
}

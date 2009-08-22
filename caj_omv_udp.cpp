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
#include "cajeput_int.h"
#include "cajeput_anims.h"
#include "caj_omv.h"
#include "terrain_compress.h"
#include <stdlib.h>
#include <cassert>

#define BUF_SIZE 2048

#define RESEND_INTERVAL 1.0
#define MAX_RESENDS 3

// FIXME - resend lost packets
void sl_send_udp_throt(struct omuser_ctx* lctx, struct sl_message* msg, int throt_id) {
  // sends packet and updates throttle, but doesn't queue
  unsigned char buf[BUF_SIZE]; int len, ret;
  msg->seqno = lctx->counter++;
  len = sl_pack_message(msg,buf,BUF_SIZE);
  if(len > 0) {
    ret = sendto(lctx->sock,buf,len,0,
		 (struct sockaddr*)&lctx->addr,sizeof(lctx->addr));
    if(ret <= 0) {
      //int err = errno;
      //printf("DEBUG: couldn't send message\n");
      perror("sending UDP message");
    }
    if(throt_id >= 0) lctx->u->throttles[throt_id].level -= len;
  } else {
    printf("DEBUG: couldn't pack message, not sending\n");
  }
  if(len > 0 && (msg->flags & MSG_RELIABLE)) {
    // FIXME - remove acks on resend?
    udp_resend_desc *resend = new udp_resend_desc();
    resend->time = g_timer_elapsed(lctx->u->sim->timer, NULL) + RESEND_INTERVAL;
    resend->ctr = 0;
    resend->msg = *msg;
    lctx->resends[msg->seqno] = resend;
    lctx->resend_sched.insert(std::pair<double,udp_resend_desc*>(resend->time,resend));
    msg->tmpl = NULL; msg->blocks = NULL; msg->acks = NULL;
  } else {
    sl_free_msg(msg);
  }
}

void sl_send_udp(struct omuser_ctx* lctx, struct sl_message* msg) {
  sl_send_udp_throt(lctx, msg, -1);
}

static void free_resend_int(udp_resend_desc* resend) {
  sl_free_msg(&resend->msg); delete resend;
}

static void handle_packet_ack(omuser_ctx* lctx, uint32_t seqno) {
  std::map<uint32_t, udp_resend_desc*>::iterator iter
    = lctx->resends.find(seqno);
  if(iter == lctx->resends.end()) return;

  // printf("DEBUG: got ack for %u\n", seqno);

  udp_resend_desc *resend = iter->second;
  
  std::pair<std::multimap<double,udp_resend_desc*>::iterator,
    std::multimap<double,udp_resend_desc*>::iterator> iters =
    lctx->resend_sched.equal_range(resend->time);
  for(std::multimap<double,udp_resend_desc*>::iterator iter2 = iters.first; 
      iter2 != iters.second; iter2++) {
    if(iter2->second == resend) {
      lctx->resend_sched.erase(iter2); break;
    }
  }

  free_resend_int(resend);
}

// part of the user deletion code
static void free_resend_data(omuser_ctx* lctx) {
  for(std::multimap<double,udp_resend_desc*>::iterator iter =
	lctx->resend_sched.begin(); iter != lctx->resend_sched.end(); iter++) {
    free_resend_int(iter->second);
  }
}

static gboolean resend_timer(gpointer data) {
  struct omuser_sim_ctx* lsim = (omuser_sim_ctx*)data;
  double time_now = g_timer_elapsed(lsim->sim->timer, NULL);

  for(omuser_ctx* lctx = lsim->ctxts; lctx != NULL; lctx = lctx->next) {    
    
    user_update_throttles(lctx->u);

    while(lctx->u->throttles[SL_THROTTLE_RESEND].level > 0.0f) {
      std::multimap<double,udp_resend_desc*>::iterator iter =
	lctx->resend_sched.begin();
      if(iter == lctx->resend_sched.end() || iter->first > time_now) {
	break;
      }
      
      udp_resend_desc* resend = iter->second;
      lctx->resend_sched.erase(iter);

      if(resend->ctr >= MAX_RESENDS) {
	printf("!!! WARNING: max resends for packet %u exceeded\n",
	       resend->msg.seqno);
	lctx->resends.erase(resend->msg.seqno);
	free_resend_int(resend);
	continue;
      }

      printf("DEBUG: resending packet %i\n",resend->msg.seqno);

      resend->ctr++;
      resend->time = time_now + RESEND_INTERVAL;
      lctx->resend_sched.insert(std::pair<double,udp_resend_desc*>
				(resend->time,resend));

      unsigned char buf[BUF_SIZE]; int ret, len;
      resend->msg.flags |= MSG_RESENT;
      len = sl_pack_message(&resend->msg,buf,BUF_SIZE);
      if(len > 0) {
	ret = sendto(lctx->sock,buf,len,0,
		     (struct sockaddr*)&lctx->addr,sizeof(lctx->addr));
	if(ret <= 0) {
	  //int err = errno;
	  //printf("DEBUG: couldn't send message\n");
	  perror("sending UDP message");
	}
	lctx->u->throttles[SL_THROTTLE_RESEND].level -= len;
      } else {
	printf("DEBUG: couldn't pack resent message, not sending\n");
      }
    }
  }

  return TRUE;
}

// FIXME - split acks across multiple packets if necessary
static void send_pending_acks(struct omuser_sim_ctx *sim) {
  struct omuser_ctx* lctx;
  for(lctx = sim->ctxts; lctx != NULL; lctx = lctx->next) {
    if(lctx->pending_acks.begin() == lctx->pending_acks.end()) 
      continue;
    SL_DECLMSG(PacketAck, acks);
    for(std::vector<uint32_t>::iterator iter = lctx->pending_acks.begin(); 
	iter != lctx->pending_acks.end(); iter++) {
      SL_DECLBLK(PacketAck, Packets, ack, &acks);
      ack->ID = *iter;
    }
    sl_send_udp(lctx,&acks);
    lctx->pending_acks.clear();
  }
}

#define VALIDATE_SESSION(ad) (uuid_compare(lctx->u->user_id, ad->AgentID) != 0 || uuid_compare(lctx->u->session_id, ad->SessionID) != 0)

// FIXME - move this somewhere saner!
static void set_default_anim(struct user_ctx* ctx, uuid_t anim) {
  if(uuid_compare(ctx->default_anim.anim, anim) != 0) {
    uuid_copy(ctx->default_anim.anim, anim);
    ctx->default_anim.sequence = ctx->anim_seq++; // FIXME
    ctx->default_anim.caj_type = CAJ_ANIM_TYPE_DEFAULT;
    uuid_copy(ctx->default_anim.obj, ctx->user_id);
    ctx->flags |= AGENT_FLAG_ANIM_UPDATE;
  }
 }

#define AGENT_CONTROL_AT_POS (1<<0)
#define AGENT_CONTROL_AT_NEG (1<<1)
#define AGENT_CONTROL_LEFT_POS (1<<2)
#define AGENT_CONTROL_LEFT_NEG (1<<3)
#define AGENT_CONTROL_UP_POS (1<<4)
#define AGENT_CONTROL_UP_NEG (1<<5)
#define AGENT_CONTROL_FLY (1<<13)

static void handle_AgentUpdate_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentUpdate, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  user_ctx *ctx = lctx->u;
  // FIXME - factor this stuff out in the next pass
  ctx->draw_dist = ad->Far;
  if(ctx->av != NULL) {
    if(!sl_quat_equal(&ctx->av->ob.rot, &ad->BodyRotation)) {
      ctx->av->ob.rot = ad->BodyRotation;
    }
    // FIXME - this is a horrid hack
    uint32_t control_flags = ad->ControlFlags;
    int is_flying = (control_flags & AGENT_CONTROL_FLY) != 0;
    sl_vector3 force; 
    force.x = 0.0f; force.y = 0.0f; force.z = 0.0f;
    if(control_flags & AGENT_CONTROL_AT_POS)
      force.x = 4.0;
    if(control_flags & AGENT_CONTROL_AT_NEG)
      force.x = -4.0;
    if(control_flags & AGENT_CONTROL_LEFT_POS)
      force.y = -4.0;
    if(control_flags & AGENT_CONTROL_LEFT_NEG)
      force.y = 4.0;
    if(control_flags & AGENT_CONTROL_UP_POS)
      force.z = 4.0;
    if(control_flags & AGENT_CONTROL_UP_NEG)
      force.z = -4.0;
    sl_mult_vect3_quat(&force,&ctx->av->ob.rot,&force);
    ctx->sim->physh.set_avatar_flying(ctx->sim,ctx->sim->phys_priv,&ctx->av->ob, is_flying);
    ctx->sim->physh.set_target_velocity(ctx->sim,ctx->sim->phys_priv,&ctx->av->ob,force);

    // FIXME - iffy
    if(is_flying) {
      if(control_flags & (AGENT_CONTROL_AT_POS|AGENT_CONTROL_AT_NEG)) {
	// FIXME - send proper animations for flying backwards & turns
	set_default_anim(ctx, fly_anim);
      } else if(control_flags & AGENT_CONTROL_UP_POS) {
	set_default_anim(ctx, hover_up_anim);
      } else if(control_flags & AGENT_CONTROL_UP_NEG) {
	set_default_anim(ctx, hover_down_anim);
      } else {
	set_default_anim(ctx, hover_anim);
      }
    } else {
      if(control_flags & (AGENT_CONTROL_AT_POS|AGENT_CONTROL_AT_NEG)) {
	// FIXME - should walking backwards be different?
	set_default_anim(ctx, walk_anim);
      } else {
	set_default_anim(ctx, stand_anim);
      }
    }
  }
}

static void handle_AgentAnimation_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentAnimation, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  // sl_dump_packet(msg); // FIXME - TODO

  int count = SL_GETBLK(AgentAnimation, AnimationList, msg).count;
  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(AgentAnimation, AnimationList, aitem) =
      SL_GETBLKI(AgentAnimation, AnimationList, msg, i);
    if(aitem->StartAnim) {
      animation_desc anim;
      uuid_copy(anim.anim, aitem->AnimID);
      anim.sequence = lctx->u->anim_seq++; // FIXME
      anim.caj_type = CAJ_ANIM_TYPE_NORMAL;
      uuid_copy(anim.obj, lctx->u->user_id);
      user_add_animation(lctx->u, &anim, false);
      // TODO - (FIXME implement this!)
    } else {
      // FIXME - handle clearing default animation
      user_clear_animation_by_id(lctx->u, aitem->AnimID);
    }
  }

  lctx->u->flags |= AGENT_FLAG_ANIM_UPDATE;
}

static void handle_StartPingCheck_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(StartPingCheck, PingID, ping, msg);
  if(ping == NULL) return;
  SL_DECLMSG(CompletePingCheck, pong);
  SL_DECLBLK(CompletePingCheck, PingID, pongid, &pong);
  pongid->PingID = ping->PingID;
  sl_send_udp(lctx,&pong);
}

static void chat_callback(void *user_priv, const struct chat_message *msg) {
  omuser_ctx* lctx = (omuser_ctx*)user_priv;
  SL_DECLMSG(ChatFromSimulator, chat);
  SL_DECLBLK(ChatFromSimulator, ChatData, cdata, &chat);
  sl_string_set(&cdata->FromName, msg->name);
  uuid_copy(cdata->SourceID, msg->source);
  uuid_copy(cdata->OwnerID, msg->owner);
  cdata->SourceType = msg->source_type;
  cdata->ChatType = msg->chat_type;
  cdata->Audible = CHAT_AUDIBLE_FULLY;
  sl_string_set(&cdata->Message,msg->msg);
  sl_send_udp(lctx, &chat);
}

static void handle_ChatFromViewer_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ChatFromViewer, AgentData, ad, msg);
  SL_DECLBLK_GET1(ChatFromViewer, ChatData, cdata, msg);
  struct chat_message chat;
  if(ad == NULL || cdata == NULL || VALIDATE_SESSION(ad)) return;

  printf("DEBUG: got ChatFromViewer\n");
  // FIXME - factor this out?
  if(cdata->Channel < 0) return; // can't send on negative channels
  if(lctx->u->av == NULL) return; // I have no mouth and I must scream...
  if(cdata->Type == CHAT_TYPE_WHISPER || cdata->Type == CHAT_TYPE_NORMAL ||
     cdata->Type == CHAT_TYPE_SHOUT) { 
    chat.channel = cdata->Channel;
    chat.pos = lctx->u->av->ob.pos;
    chat.name = lctx->u->name;
    uuid_copy(chat.source,lctx->u->user_id);
    uuid_clear(chat.owner); // FIXME - ???
    chat.source_type = CHAT_SOURCE_AVATAR;
    chat.chat_type = cdata->Type;
    chat.msg = (char*)cdata->Message.data;
    world_send_chat(lctx->u->sim, &chat);
  }
}

static void handle_MapLayerRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(MapLayerRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  
  // FIXME - this is some evil hack copied from OpenSim
  SL_DECLMSG(MapLayerReply, reply);
  SL_DECLBLK(MapLayerReply, AgentData, ad2, &reply);
  uuid_copy(ad2->AgentID, lctx->u->user_id);
  ad2->Flags = 0;
  SL_DECLBLK(MapLayerReply, LayerData, ld, &reply);
  ld->Bottom = ld->Left = 0;
  ld->Top = ld->Right = 30000; // is this even valid?
  uuid_parse("00000000-0000-1111-9999-000000000006", ld->ImageID);
  sl_send_udp(lctx, &reply);
}

// FIXME - we assume omuser_ctx lifetime tied to user_ctx lifetime
struct map_block_req_priv {
  omuser_ctx *lctx;
  user_ctx *ctx;
  uint32_t flags;
};

// we'll start running into trouble if a bunch of sims have names longer than
// 100 bytes, but that's unlikely, I think.
#define MAX_MAP_BLOCKS_PER_MSG 10

static void map_block_req_cb(void *priv, struct map_block_info *blocks, int count) {
  map_block_req_priv *req = (map_block_req_priv*)priv;
  if(req->ctx != NULL) {
    // FIXME - should we send an empty message if we have nothing?
    // For now, I'm guessing not

    // FIXME - OpenSim has some special case re. clicking a map tile!
 
    for(int i = 0; i < count; /*nothing*/) {
      SL_DECLMSG(MapBlockReply, reply);
      SL_DECLBLK(MapBlockReply, AgentData, ad, &reply);
      uuid_copy(ad->AgentID, req->ctx->user_id);
      ad->Flags = req->flags;
      
      for(int j = 0; j < MAX_MAP_BLOCKS_PER_MSG && i < count; i++, j++) {
	SL_DECLBLK(MapBlockReply, Data, dat, &reply);
	dat->X = blocks[i].x;
	dat->Y = blocks[i].y;
	sl_string_set(&dat->Name, blocks[i].name);
	dat->RegionFlags = blocks[i].flags;
	dat->WaterHeight = blocks[i].water_height;
	dat->Agents = blocks[i].num_agents;
	uuid_copy(dat->MapImageID, blocks[i].map_image);
      }
      
      reply.flags |= MSG_RELIABLE;
      sl_send_udp(req->lctx, &reply);
    }
    
    user_del_self_pointer(&req->ctx);
  }
  delete req;
}

static void handle_MapBlockRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(MapBlockRequest, AgentData, ad, msg);
  SL_DECLBLK_GET1(MapBlockRequest, PositionData, pos, msg);
  if(ad == NULL || pos == NULL || VALIDATE_SESSION(ad)) return;

  user_ctx *ctx = lctx->u;
  map_block_req_priv *priv = new map_block_req_priv();
  priv->ctx = ctx; priv->flags = ad->Flags; priv->lctx = lctx;
  user_add_self_pointer(&priv->ctx);
  ctx->sim->gridh.map_block_request(ctx->sim, pos->MinX, pos->MaxX, pos->MinY, 
				    pos->MaxY, map_block_req_cb, priv);
  
  // FIXME - TODO
}

static void send_agent_wearables(struct omuser_ctx* lctx) {
  user_ctx *ctx = lctx->u;
  SL_DECLMSG(AgentWearablesUpdate, upd);
  SL_DECLBLK(AgentWearablesUpdate, AgentData, ad, &upd);
  uuid_copy(ad->AgentID, ctx->user_id);
  uuid_copy(ad->SessionID, ctx->session_id);
  ad->SerialNum = ctx->wearable_serial; // FIXME: don't think we increment here
  
  for(int i = 0; i < SL_NUM_WEARABLES; i++) {
    // FIXME - avoid sending empty wearables?

     SL_DECLBLK(AgentWearablesUpdate, WearableData, wd, &upd);
     uuid_copy(wd->ItemID, ctx->wearables[i].item_id);
     uuid_copy(wd->AssetID, ctx->wearables[i].asset_id);
     wd->WearableType = i;
  }

  upd.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &upd);
}

static void handle_AgentWearablesRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentWearablesRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  send_agent_wearables(lctx);
}

static void handle_AgentSetAppearance_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  sl_string str; user_ctx* ctx = lctx->u;
  SL_DECLBLK_GET1(AgentSetAppearance, AgentData, ad, msg);
  SL_DECLBLK_GET1(AgentSetAppearance, ObjectData, objd, msg);
  if(ad == NULL || objd == NULL ||  VALIDATE_SESSION(ad)) 
    return;
  if(ad->SerialNum < ctx->appearance_serial) {
    printf("WARNING: Got outdated AgentSetAppearance\n");
    return;
  }

  ctx->appearance_serial = ad->SerialNum;
  // FIXME - do something with size
  printf("DEBUG: agent size (%f,%f,%f)\n",(double)ad->Size.x,(double)ad->Size.y,
	 (double)ad->Size.z);

  // FIXME - what to do with WearableData blocks?

  // we could steal the message's buffer, but that would be evil
  sl_string_copy(&str, &objd->TextureEntry);
  user_set_texture_entry(ctx, &str);

  str.len = SL_GETBLK(AgentSetAppearance, VisualParam, msg).count;
  str.data = (unsigned char*)malloc(str.len);
  for(int i = 0; i < str.len; i++) {
    str.data[i] = SL_GETBLKI(AgentSetAppearance, VisualParam, msg, i)->ParamValue;
  }
  user_set_visual_params(ctx, &str);

  printf("DEBUG: Completed AgentSetAppearance\n");
}

static void send_agent_data_update(struct omuser_ctx* lctx) {
  user_ctx* ctx = lctx->u;
  SL_DECLMSG(AgentDataUpdate,upd);
  SL_DECLBLK(AgentDataUpdate, AgentData, ad, &upd);
  uuid_copy(ad->AgentID, ctx->user_id);
  sl_string_set(&ad->FirstName, ctx->first_name);
  sl_string_set(&ad->LastName, ctx->last_name);
  sl_string_set(&ad->GroupTitle, ctx->group_title);
  uuid_clear(ad->ActiveGroupID); // TODO
  ad->GroupPowers = 0;
  sl_string_set(&ad->GroupName, ""); // TODO
  upd.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &upd);
}

static void handle_AgentDataUpdateRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentDataUpdateRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  send_agent_data_update(lctx);
}

static void handle_RegionHandshakeReply_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RegionHandshakeReply, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  lctx->u->flags |= AGENT_FLAG_RHR | AGENT_FLAG_NEED_OTHER_AVS;
  // FIXME - should we do something with RegionInfo.Flags?
}

static void handle_PacketAck_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  int cnt = SL_GETBLK(PacketAck, Packets, msg).count;
  for(int i = 0; i < cnt; i++) {
    SL_DECLBLK_ONLY(PacketAck, Packets, ack) =
      SL_GETBLKI(PacketAck, Packets, msg, i);
    handle_packet_ack(lctx, ack->ID);
  }
}

static void handle_CompleteAgentMovement_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  user_ctx *ctx = lctx->u;
  SL_DECLBLK_GET1(CompleteAgentMovement, AgentData, ad, msg);
  if(ad == NULL || ad->CircuitCode != ctx->circuit_code || VALIDATE_SESSION(ad)) 
    return;
  if(user_complete_movement(ctx)) {
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
    sl_string_set(&simdat->ChannelVersion, CAJ_VERSION_STRING);
    sl_send_udp(lctx, &amc);
  }

  // FIXME - move this somewhere saner?
  if(ctx->sim->welcome_message != NULL)
    user_send_message(ctx, ctx->sim->welcome_message);
}

static void handle_LogoutRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(LogoutRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  user_ctx *ctx = lctx->u;
  // FIXME - hacky!
  ctx->flags |= AGENT_FLAG_IN_LOGOUT;
  SL_DECLMSG(LogoutReply, reply);
  SL_DECLBLK(LogoutReply, AgentData, ad2, &reply);
  uuid_copy(ad2->AgentID, ctx->user_id);
  uuid_copy(ad2->SessionID, ctx->session_id);
  sl_send_udp(lctx, &reply);
  user_session_close(ctx);
}

static void teleport_failed(struct user_ctx* ctx, const char* reason) {
  omuser_ctx* lctx = (omuser_ctx*)ctx->user_priv;
  SL_DECLMSG(TeleportFailed, fail);
  SL_DECLBLK(TeleportFailed, Info, info, &fail);
  uuid_copy(info->AgentID, ctx->user_id);
  sl_string_set(&info->Reason, reason);
  fail.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &fail);
}

static void teleport_progress(struct user_ctx* ctx, const char* msg, uint32_t flags) {
  omuser_ctx* lctx = (omuser_ctx*)ctx->user_priv;
  SL_DECLMSG(TeleportProgress, prog);
  SL_DECLBLK(TeleportProgress, AgentData, ad, &prog);
  uuid_copy(ad->AgentID, ctx->user_id);
   SL_DECLBLK(TeleportProgress, Info, info, &prog);
  sl_string_set(&info->Message, msg);
  info->TeleportFlags = flags;
  // prog.flags |= MSG_RELIABLE; // not really needed, I think. FIXME?
  sl_send_udp(lctx, &prog);
}


static void teleport_complete(struct user_ctx* ctx, struct teleport_desc *tp) {
  //omuser_ctx* lctx = (omuser_ctx*)ctx->user_priv;
  sl_llsd *msg = llsd_new_map();
  sl_llsd *info = llsd_new_map();

  llsd_map_append(info, "AgentID", llsd_new_uuid(ctx->user_id));
  llsd_map_append(info, "LocationID", llsd_new_int(4)); // ???!! FIXME ???
  llsd_map_append(info, "SimIP", llsd_new_binary(&tp->sim_ip,4)); // big-endian?
  llsd_map_append(info, "SimPort", llsd_new_int(tp->sim_port));
  llsd_map_append(info, "RegionHandle", llsd_new_from_u64(tp->region_handle));
  llsd_map_append(info, "SeedCapability", llsd_new_string(tp->seed_cap));
  llsd_map_append(info, "SimAccess", llsd_new_int(13)); // ????!! FIXME!
  llsd_map_append(info, "TeleportFlags", llsd_new_from_u32(tp->flags));

  sl_llsd *array = llsd_new_array();
  llsd_array_append(array, info);
  llsd_map_append(msg, "Info", array);

  user_event_queue_send(ctx,"TeleportFinish",msg);
}

// FIXME - handle TeleportRequest message (by UUID)?

static void handle_TeleportLocationRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(TeleportLocationRequest, AgentData, ad, msg);
  SL_DECLBLK_GET1(TeleportLocationRequest, Info, info, msg);
  if(ad == NULL || info == NULL || VALIDATE_SESSION(ad)) 
    return;
  user_teleport_location(lctx->u, info->RegionHandle, &info->Position, &info->LookAt);
}

static void handle_TeleportLandmarkRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(TeleportLandmarkRequest, Info, info, msg);
  if(info == NULL || VALIDATE_SESSION(info)) 
    return;
  user_teleport_landmark(lctx->u, info->LandmarkID);
}



static void handle_AgentThrottle_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentThrottle, AgentData, ad, msg);
  SL_DECLBLK_GET1(AgentThrottle, Throttle, throt, msg);
  if(ad == NULL || throt == NULL || VALIDATE_SESSION(ad) || 
     ad->CircuitCode != lctx->u->circuit_code) return;

  // FIXME - need to check generation counter

  user_set_throttles_block(lctx->u, throt->Throttles.data,
			   throt->Throttles.len);
}

struct asset_xfer {
  uint64_t id;
  uint32_t ctr;
  uint8_t local;
  int8_t asset_type;
  int len, total_len;
  unsigned char* data;
  uuid_t transaction, asset_id;
};

static void helper_combine_uuids(uuid_t out, const uuid_t u1, const uuid_t u2) {
  gsize len = 16;
  GChecksum * csum = g_checksum_new(G_CHECKSUM_MD5);
  g_checksum_update(csum, u1, 16);
  g_checksum_update(csum, u2, 16);
  g_checksum_get_digest(csum, out, &len);
  g_checksum_free(csum);
}

// FIXME - need to free old transfers properly

static asset_xfer* init_asset_upload(struct omuser_ctx* lctx, int is_local,
				     int asset_type, uuid_t transaction) {
  asset_xfer *xfer = new asset_xfer();
  xfer->local = is_local;
  xfer->asset_type = asset_type;
  uuid_copy(xfer->transaction, transaction);
  helper_combine_uuids(xfer->asset_id, transaction,
		       lctx->u->secure_session_id);
  xfer->id = ++(lctx->lsim->xfer_id_ctr);
  xfer->ctr = 0;
  xfer->len = xfer->total_len = 0;
  xfer->data = NULL;
  lctx->xfers[xfer->id] = xfer;
  return xfer;
}

static void complete_asset_upload(omuser_ctx* lctx, asset_xfer *xfer,
				  int success) {
  lctx->xfers.erase(xfer->id);
  SL_DECLMSG(AssetUploadComplete, complete);
  SL_DECLBLK(AssetUploadComplete, AssetBlock, blk, &complete);
  blk->Type = xfer->asset_type;
  blk->Success = !!success;
  uuid_copy(blk->UUID, xfer->asset_id);
  complete.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &complete);

  if(success) {
    sim_add_local_texture(lctx->u->sim, xfer->asset_id, xfer->data,
			  xfer->len, true);
  } else {
    free(xfer->data); // note: sim_add_local_texture take ownership of buffer
  }

  delete xfer;
}

static void handle_SendXferPacket_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  asset_xfer *xfer;
  SL_DECLBLK_GET1(SendXferPacket,XferID,xferid,msg);
  SL_DECLBLK_GET1(SendXferPacket,DataPacket,data,msg);

  std::map<uint64_t,asset_xfer*>::iterator iter =
    lctx->xfers.find(xferid->ID);
  if(iter == lctx->xfers.end()) {
    printf("ERROR: SendXfer for unknown ID\n");
    return;
  }
  xfer = iter->second;

  /* printf("DEBUG SendXfer packet=%i len=%i\n", xferid->Packet,
     data->Data.len); */

  if(xfer->data == NULL) {
    if(data->Data.len < 4 || (xferid->Packet & 0x7fffffff) != 0) {
      printf("ERROR: bad first SendXfer len=%i packet=%u\n",
	     data->Data.len, xferid->Packet);
      return;
    }
    xfer->total_len = data->Data.data[0] | (data->Data.data[1] << 8) |
      (data->Data.data[2] << 16) | (data->Data.data[3] << 24);
    if(xfer->total_len > 1000000 || 
       (data->Data.len - 4) > xfer->total_len) {
      printf("ERROR: bad first SendXfer length %u\n", xfer->total_len);
      return;      
    }
    xfer->data = (unsigned char*)malloc(xfer->total_len);
    xfer->len = data->Data.len - 4;
    memcpy(xfer->data, data->Data.data+4, xfer->len);
    printf("DEBUG: initial xfer %i long\n", xfer->len);
  } else if((xferid->Packet & 0x7fffffff) != xfer->ctr) {
    printf("ERROR: bad xfer packet ctr: expected %i, got %i\n",
	   xfer->ctr, xferid->Packet);
    return;
  } else {
    if(xfer->len + data->Data.len > xfer->total_len) {
      printf("ERROR: xfer too long\n");
      return;
    }
    memcpy(xfer->data + xfer->len, data->Data.data, data->Data.len);
    xfer->len += data->Data.len;
    /* printf("DEBUG: xfer %i long, got %i bytes so far\n",
       data->Data.len, xfer->len); */
  }

  xfer->ctr++;
  
  {
    SL_DECLMSG(ConfirmXferPacket,confirm);
    SL_DECLBLK(ConfirmXferPacket, XferID, xferid2, &confirm);
    xferid2->ID = xfer->id; xferid2->Packet = xferid->Packet;
    confirm.flags |= MSG_RELIABLE;
    sl_send_udp(lctx, &confirm);
  }

  if(xferid->Packet & 0x80000000) {
    if(xfer->len != xfer->total_len) {
      printf("Bad xfer length at end of data: got %i, want %i\n",
	     xfer->len, xfer->total_len);
      return;
    }
    complete_asset_upload(lctx, xfer, 1);
  }
}

static void handle_AssetUploadRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  asset_xfer *xfer;
  SL_DECLBLK_GET1(AssetUploadRequest,AssetBlock,asset,msg);
  if(asset == NULL) return;
  sl_dump_packet(msg);
  switch(asset->Type) {
  case 0: // texture
    if(!asset->StoreLocal) {
      user_send_message(lctx->u, "Sorry, AssetUploadRequest is now only for local texture uploads.\n");
      return;
    }
    printf("DEBUG: got local AssetUploadRequest for texture\n");
    break;
  default:
    user_send_message(lctx->u, "ERROR: AssetUploadRequest for unsupported type\n");
    return;
  }

  // FIXME - handle "all data in initial packet" case

  xfer = init_asset_upload(lctx, asset->StoreLocal,
			   asset->Type, asset->TransactionID);

  {
    SL_DECLMSG(RequestXfer, xferreq);
    SL_DECLBLK(RequestXfer, XferID, xferid, &xferreq);
    xferid->ID = xfer->id;
    xferid->Filename.len = 0; // apparently long dead
    xferid->Filename.data = NULL;
    xferid->FilePath = 0; // not used?
    xferid->DeleteOnCompletion = 0; // ? think so
    xferid->UseBigPackets = 0; // no way
    xferid->VFileType = asset->Type;
    uuid_copy(xferid->VFileID, xfer->asset_id);
    xferreq.flags |= MSG_RELIABLE;
    sl_send_udp(lctx, &xferreq);
  }
}

// called as part of the user removal process
static void free_texture_sends(struct omuser_ctx *lctx) {
  for(std::map<obj_uuid_t,image_request*>::iterator iter = 
	lctx->image_reqs.begin();  iter != lctx->image_reqs.end(); iter++) {
    image_request *req = iter->second;
    req->texture->refcnt--; delete req;
  }
}

#define TEXTURE_FIRST_LEN 600
#define TEXTURE_PACKET_LEN 1000

int send_err_throt = 0; // HACK

static gboolean texture_send_timer(gpointer data) {
  struct omuser_sim_ctx* lsim = (omuser_sim_ctx*)data;
  send_err_throt++;
  for(omuser_ctx* lctx = lsim->ctxts; lctx != NULL; lctx = lctx->next) {
    std::map<obj_uuid_t,image_request*>::iterator req_iter =
      lctx->image_reqs.begin(); // FIXME - do by priority
    user_update_throttles(lctx->u);
    while(lctx->u->throttles[SL_THROTTLE_TEXTURE].level > 0.0f) {
      if(req_iter == lctx->image_reqs.end()) break;
      image_request *req = req_iter->second;
      if(req->texture->flags & CJP_TEXTURE_MISSING) {
	char uuid_buf[40]; uuid_unparse(req->texture->asset_id, uuid_buf);
	printf("Image %s not found!\n", uuid_buf);
	  
	SL_DECLMSG(ImageNotInDatabase, noimg);
	SL_DECLBLK(ImageNotInDatabase, ImageID, iid, &noimg);
	uuid_copy(iid->ID, req->texture->asset_id);
	noimg.flags |= MSG_RELIABLE;
	sl_send_udp_throt(lctx, &noimg, SL_THROTTLE_TEXTURE);

	std::map<obj_uuid_t,image_request*>::iterator next = req_iter;
	next++;
	req->texture->refcnt--; delete req;
	lctx->image_reqs.erase(req_iter);
	req_iter = next;
      } else if(req->texture->data == NULL) {
	  char uuid_buf[40]; uuid_unparse(req->texture->asset_id, uuid_buf);
	  if((send_err_throt % 10) == 0) 
	    printf("Image %s still pending, skipping\n", uuid_buf);
	req_iter++;
      } else {
	int sent = (req->packet_no > 1) ? 
	  (TEXTURE_FIRST_LEN + (req->packet_no-1)*TEXTURE_PACKET_LEN) :
	  (TEXTURE_FIRST_LEN * req->packet_no);
	int tex_len = req->texture->len;
	assert(req->texture->num_discard > 0);
	if(req->discard >= req->texture->num_discard)
	  req->discard = req->texture->num_discard - 1;
	
	int wanna_send = req->texture->discard_levels[req->discard];
	if(sent >= wanna_send || sent < 0) {
	  char uuid_buf[40]; uuid_unparse(req->texture->asset_id, uuid_buf);
	  if(sent < 0) {
	    printf("INTERNAL ERROR: Bad sent value %i for packet_no %i\n",
		   sent, req->packet_no);
	  } else {
	    printf("Image %s send complete (%i bytes, %i packets), removing\n", 
		   uuid_buf, sent, req->packet_no);
	  }

	  std::map<obj_uuid_t,image_request*>::iterator next = req_iter;
	  next++;
	  req->texture->refcnt--; delete req;
	  lctx->image_reqs.erase(req_iter);
	  req_iter = next;
	} else if(sent == 0) {
	  // DEBUG
	  char uuid_buf[40]; uuid_unparse(req->texture->asset_id, uuid_buf);
	  printf("Sending first packet for %s image\n", uuid_buf);

	  SL_DECLMSG(ImageData, imgdat);
	  SL_DECLBLK(ImageData, ImageID, iid, &imgdat);
	  uuid_copy(iid->ID, req->texture->asset_id);
	  iid->Codec = 2; // FIXME - magic numbers are bad
	  iid->Size = tex_len;
	  // this next line is right, since TEXTURE_FIRST_LEN < TEXTURE_PACKET_LEN
	  iid->Packets = 1 + ( tex_len - TEXTURE_FIRST_LEN + (TEXTURE_PACKET_LEN-1))/TEXTURE_PACKET_LEN;

	  SL_DECLBLK(ImageData, ImageData, idat, &imgdat);
	  idat->Data.len = tex_len > TEXTURE_FIRST_LEN ? 
	                        TEXTURE_FIRST_LEN : tex_len;
	  idat->Data.data = (unsigned char*)malloc(idat->Data.len);
	  memcpy(idat->Data.data, req->texture->data, idat->Data.len);

	  imgdat.flags |= MSG_RELIABLE;
	  sl_send_udp_throt(lctx, &imgdat, SL_THROTTLE_TEXTURE);
	  req->packet_no++;
	} else {
	  char uuid_buf[40]; uuid_unparse(req->texture->asset_id, uuid_buf);
	  //printf("Sending packet %i for %s image\n", req->packet_no, uuid_buf);

	  int send_len = tex_len - sent;
	  if(send_len > TEXTURE_PACKET_LEN) send_len = TEXTURE_PACKET_LEN;
	  
	  SL_DECLMSG(ImagePacket, imgpkt);
	  SL_DECLBLK(ImagePacket, ImageID, iid, &imgpkt);
	  uuid_copy(iid->ID, req->texture->asset_id);
	  iid->Packet = req->packet_no;

	  SL_DECLBLK(ImagePacket, ImageData, idat, &imgpkt);
	  idat->Data.len = send_len;
	  idat->Data.data = (unsigned char*)malloc(send_len);
	  memcpy(idat->Data.data, req->texture->data + sent, send_len);

	  imgpkt.flags |= MSG_RELIABLE;
	  sl_send_udp_throt(lctx, &imgpkt, SL_THROTTLE_TEXTURE);
	  req->packet_no++;
	}
      }
    }

    //printf("DEBUG: texture throttle at %f\n", (double)ctx->throttles[SL_THROTTLE_TEXTURE].level);
  }
  
  return TRUE;
}


static void handle_RequestImage_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RequestImage, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad))
    return;
  int cnt = SL_GETBLK(RequestImage, RequestImage, msg).count;
  for(int i = 0; i < cnt; i++) {
    SL_DECLBLK_ONLY(RequestImage, RequestImage, ri) =
      SL_GETBLKI(RequestImage, RequestImage, msg, i);

    // DEBUG
    char buf[40]; uuid_unparse(ri->Image, buf);
    printf("RequestImage %s discard %i prio %f pkt %i type %i\n",
	   buf, (int)ri->DiscardLevel, (double)ri->DownloadPriority,
	   (int)ri->Packet, (int)ri->Type);

    std::map<obj_uuid_t,image_request*>::iterator iter =
      lctx->image_reqs.find(ri->Image);
    
    if(ri->DiscardLevel >= 0) {
      if(iter == lctx->image_reqs.end()) {
	texture_desc *texture = sim_get_texture(lctx->u->sim, ri->Image);
	image_request* ireq = new image_request;
	ireq->texture = texture;
	ireq->priority = ri->DownloadPriority;
	ireq->discard = ri->DiscardLevel;
	ireq->packet_no = ri->Packet;
	sim_request_texture(lctx->u->sim, texture);
	lctx->image_reqs[ri->Image] = ireq;
      } else {
	image_request* ireq = iter->second;
	ireq->priority = ri->DownloadPriority;
	ireq->discard = ri->DiscardLevel;
	if(ireq->packet_no != ri->Packet) {
	  printf("DEBUG: mismatching packet no for %s: us %i, them %i\n",
		 buf, ireq->packet_no, ri->Packet);
	}
	// FIXME - don't think we want to update packet_no, but...
      }
    } else if(iter != lctx->image_reqs.end()) {
      printf("Deleting RequestImage for %s\n", buf);
      image_request* ireq = iter->second;
      ireq->texture->refcnt--;
      delete ireq;
      lctx->image_reqs.erase(iter);
    }
  }
}

#define PCODE_PRIM 9
#define PCODE_AV 47
#define PCODE_GRASS 95
#define PCODE_NEWTREE 111
#define PCODE_PARTSYS 143 /* ??? */
#define PCODE_TREE 255


// FIXME - incomplete
static void send_av_full_update(user_ctx* ctx, user_ctx* av_user) {
  omuser_ctx *lctx = (omuser_ctx*)ctx->user_priv;
  avatar_obj* av = av_user->av;
  char name[0x100]; unsigned char obj_data[60];
  SL_DECLMSG(ObjectUpdate,upd);
  SL_DECLBLK(ObjectUpdate,RegionData,rd,&upd);
  rd->RegionHandle = ctx->sim->region_handle;
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation

  SL_DECLBLK(ObjectUpdate,ObjectData,objd,&upd);
  objd->ID = av->ob.local_id;
  objd->State = 0;
  uuid_copy(objd->FullID, av->ob.id);
  objd->PCode = PCODE_AV;
  objd->Scale.x = 1.0f; objd->Scale.y = 1.0f; objd->Scale.z = 1.0f;

  // FIXME - endianness issues
  memcpy(obj_data, &av->ob.pos, 12); 
  memcpy(obj_data+12, &av->ob.velocity, 12); // velocity
  memset(obj_data+24, 0, 12); // accel
  memcpy(obj_data+36, &av->ob.rot, 12); 
  memset(obj_data+48, 0, 12);
  sl_string_set_bin(&objd->ObjectData, obj_data, 60);

  objd->ParentID = 0;
  objd->UpdateFlags = 0; // TODO

  sl_string_copy(&objd->TextureEntry, &ctx->texture_entry);
  //objd->TextureEntry.len = 0;
  objd->TextureAnim.len = 0;
  objd->Data.len = 0;
  objd->Text.len = 0;
  memset(objd->TextColor, 0, 4);
  objd->MediaURL.len = 0;
  objd->PSBlock.len = 0;
  objd->ExtraParams.len = 0;

  memset(objd->OwnerID,0,16);
  memset(objd->Sound,0,16);

  // FIXME - copied from OpenSim
  objd->UpdateFlags = 61 + (9 << 8) + (130 << 16) + (16 << 24);
  objd->PathCurve = 16;
  objd->ProfileCurve = 1;
  objd->PathScaleX = 100;
  objd->PathScaleY = 100;
  objd->ParentID = 0;
  objd->Material = 4;
  // END FIXME
  
  name[0] = 0;
  snprintf(name,0xff,"FirstName STRING RW SV %s\nLastName STRING RW SV %s\nTitle STRING RW SV %s",
	   av_user->first_name,av_user->last_name,av_user->group_title); // FIXME
  sl_string_set(&objd->NameValue,name);

  upd.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &upd);
}

static void sl_float_to_int16(unsigned char* out, float val, float range) {
  uint16_t ival = (uint16_t)((val+range)*32768/range);
  out[0] = ival & 0xff;
  out[1] = (ival >> 8) & 0xff;
}

static void send_av_terse_update(user_ctx* ctx, avatar_obj* av) {
  omuser_ctx *lctx = (omuser_ctx*)ctx->user_priv;
  unsigned char dat[0x3C];
  SL_DECLMSG(ImprovedTerseObjectUpdate,terse);
  SL_DECLBLK(ImprovedTerseObjectUpdate,RegionData,rd,&terse);
  rd->RegionHandle = ctx->sim->region_handle;
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation
  SL_DECLBLK(ImprovedTerseObjectUpdate,ObjectData,objd,&terse);
  objd->TextureEntry.data = NULL;
  objd->TextureEntry.len = 0;

  dat[0] = av->ob.local_id & 0xff;
  dat[1] = (av->ob.local_id >> 8) & 0xff;
  dat[2] = (av->ob.local_id >> 16) & 0xff;
  dat[3] = (av->ob.local_id >> 24) & 0xff;
  dat[4] = 0; // state - ???
  dat[5] = 1; // object is an avatar
  
  // FIXME - copied from OpenSim
  memset(dat+6,0,16);
  dat[0x14] = 128; dat[0x15] = 63;
  
  // FIXME - correct endianness
  memcpy(dat+0x16, &av->ob.pos, 12); 

  // Velocity
  sl_float_to_int16(dat+0x22, av->ob.velocity.x, 128.0f);
  sl_float_to_int16(dat+0x24, av->ob.velocity.y, 128.0f);
  sl_float_to_int16(dat+0x26, av->ob.velocity.z, 128.0f);

  // Acceleration
  sl_float_to_int16(dat+0x28, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x2A, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x2C, 0.0f, 64.0f);
 
  // Rotation
  sl_float_to_int16(dat+0x2E, av->ob.rot.x, 1.0f);
  sl_float_to_int16(dat+0x30, av->ob.rot.y, 1.0f);
  sl_float_to_int16(dat+0x32, av->ob.rot.z, 1.0f);
  sl_float_to_int16(dat+0x34, av->ob.rot.w, 1.0f);

  // Rotational velocity
  sl_float_to_int16(dat+0x36, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x38, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x3A, 0.0f, 64.0f);

 
  sl_string_set_bin(&objd->Data, dat, 0x3C);
  terse.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &terse);
}

static void send_av_appearance(user_ctx* ctx, user_ctx* av_user) {
  omuser_ctx *lctx = (omuser_ctx*)ctx->user_priv;
  SL_DECLMSG(AvatarAppearance,aa);
  SL_DECLBLK(AvatarAppearance,Sender,sender,&aa);
  uuid_copy(sender->ID, av_user->user_id);
  sender->IsTrial = 0;
  SL_DECLBLK(AvatarAppearance,ObjectData,objd,&aa);
  sl_string_copy(&objd->TextureEntry, &av_user->texture_entry);

  // FIXME - this is horribly, horribly inefficient
  if(av_user->visual_params.data != NULL) {
      for(int i = 0; i < av_user->visual_params.len; i++) {
	SL_DECLBLK(AvatarAppearance,VisualParam,param,&aa);
	param->ParamValue = av_user->visual_params.data[i];
      }
  }
  
  aa.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &aa);
}

static void send_av_animations(user_ctx* ctx, user_ctx* av_user) {
  omuser_ctx *lctx = (omuser_ctx*)ctx->user_priv;
  SL_DECLMSG(AvatarAnimation,aa);
  SL_DECLBLK(AvatarAnimation,Sender,sender,&aa);
  uuid_copy(sender->ID, av_user->user_id);
  
  {
    SL_DECLBLK(AvatarAnimation,AnimationList,anim,&aa);
    uuid_copy(anim->AnimID, av_user->default_anim.anim);
    anim->AnimSequenceID = av_user->default_anim.sequence; // FIXME - ???
    SL_DECLBLK(AvatarAnimation,AnimationSourceList,source,&aa);
    uuid_copy(source->ObjectID,av_user->default_anim.obj); // FIXME!!!
  }
  
  // FIXME - copy non-default animations too
  
  // aa.flags |= MSG_RELIABLE; // FIXME - not reliable?
  sl_send_udp(lctx, &aa);
}

// --- START of hacky object update code pt 2. FIXME - remove this ---

static void obj_send_full_upd(omuser_ctx* lctx, world_obj* obj) {
  if(obj->type != OBJ_TYPE_PRIM) return;
  primitive_obj *prim = (primitive_obj*)obj;
  unsigned char obj_data[60];
  SL_DECLMSG(ObjectUpdate,upd);
  SL_DECLBLK(ObjectUpdate,RegionData,rd,&upd);
  rd->RegionHandle = lctx->u->sim->region_handle;
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation

  SL_DECLBLK(ObjectUpdate,ObjectData,objd,&upd);
  objd->ID = prim->ob.local_id;
  objd->State = 0;

  uuid_copy(objd->FullID, prim->ob.id);
  objd->CRC = 0; // FIXME - need this for caching
  objd->PCode = PCODE_PRIM;
  objd->Material = prim->material;
  objd->ClickAction = 0; // FIXME.
  objd->Scale = prim->ob.scale;

  // FIXME - endianness issues
  memcpy(obj_data, &prim->ob.pos, 12); 
  memset(obj_data+12, 0, 12); // velocity
  memset(obj_data+24, 0, 12); // accel
  memcpy(obj_data+36, &prim->ob.rot, 12); 
  memset(obj_data+48, 0, 12);
  sl_string_set_bin(&objd->ObjectData, obj_data, 60);

  objd->ParentID = 0; // FIXME - todo
  objd->UpdateFlags = 0; // TODO - FIXME

  objd->PathCurve = prim->path_curve;
  objd->ProfileCurve = prim->profile_curve;
  objd->PathBegin = prim->path_begin;
  objd->PathEnd = prim->path_end;
  objd->PathScaleX = prim->path_scale_x;
  objd->PathScaleY = prim->path_scale_y;
  objd->PathShearX = prim->path_shear_x;
  objd->PathShearY = prim->path_shear_y;
  objd->PathTwist = prim->path_twist;
  objd->PathTwistBegin = prim->path_twist_begin;
  objd->PathRadiusOffset = prim->path_radius_offset;
  objd->PathTaperX = prim->path_taper_x;
  objd->PathTaperY = prim->path_taper_y;
  objd->ProfileBegin = prim->profile_begin;
  objd->ProfileEnd = prim->profile_end;
  objd->ProfileHollow = prim->profile_hollow;

  objd->TextureEntry.len = 0;
  objd->TextureAnim.len = 0;
  objd->Data.len = 0;
  objd->Text.len = 0;
  memset(objd->TextColor, 0, 4);
  objd->MediaURL.len = 0;
  objd->PSBlock.len = 0;
  objd->ExtraParams.len = 0;

  uuid_copy(objd->OwnerID, prim->owner);
  memset(objd->Sound,0,16);

  objd->NameValue.len = 0;

  upd.flags |= MSG_RELIABLE;
  sl_send_udp_throt(lctx, &upd, SL_THROTTLE_TASK);
}

static gboolean obj_update_timer(gpointer data) {
  omuser_sim_ctx* lsim = (omuser_sim_ctx*)data;
  for(omuser_ctx* lctx = lsim->ctxts; lctx != NULL; lctx = lctx->next) {
    user_ctx *ctx = lctx->u;
    if((ctx->flags & AGENT_FLAG_RHR) == 0) continue;
    user_update_throttles(ctx);
    std::map<uint32_t, int>::iterator iter = ctx->obj_upd.begin();
    while(ctx->throttles[SL_THROTTLE_TASK].level > 0.0f && 
	  iter != ctx->obj_upd.end()) {
      if(iter->second == UPDATE_LEVEL_NONE) {
	iter++; continue;
      } else /* if(iter->second == UPDATE_LEVEL_FULL) - FIXME handle other cases */ {
	printf("DEBUG: sending full update for %u\n", iter->first);
	obj_send_full_upd(lctx, lsim->sim->localid_map[iter->first]);
	iter->second = UPDATE_LEVEL_NONE;
      }
    }

    // FIXME - should probably move this elsewhere
    int i = 0;
    while(ctx->throttles[SL_THROTTLE_LAND].level > 0.0f && 
	  i < 16) {
      if(ctx->dirty_terrain[i] == 0) { i++; continue; }
      int patch_cnt = 0; int patches[4]; uint16_t dirty = ctx->dirty_terrain[i];

      // in the worst case, we could only fit 2 texture patches per packet
      // (this is probably unlikely, but best to be safe anyway, especially as
      // getting it wrong can crash the viewer!)
      // should really do this dynamically - normally we'll fit at least 4
      for(int j = 0; j < 16 && patch_cnt < 2; j++) {
	if(dirty & (1<<j)) {
	  patches[patch_cnt++] = i * 16 + j;
	  dirty &= ~(1<<j);
	}
      }
      SL_DECLMSG(LayerData, layer);
      SL_DECLBLK(LayerData, LayerID, lid, &layer);
      lid->Type = LAYER_TYPE_LAND;
      SL_DECLBLK(LayerData, LayerData, ldat, &layer);
      terrain_create_patches(lsim->sim->terrain, patches, 
			     patch_cnt, &ldat->Data);
      layer.flags |= MSG_RELIABLE;
      sl_send_udp_throt(lctx, &layer, SL_THROTTLE_LAND);
      ctx->dirty_terrain[i] = dirty;
    }
  }

  return TRUE;
}

// -------------------------------------------------------

void register_msg_handler(struct omuser_sim_ctx *lsim, sl_message_tmpl* tmpl, 
			  sl_msg_handler handler){
  lsim->msg_handlers.insert(std::pair<sl_message_tmpl*,sl_msg_handler>(tmpl,handler));
}

// Handles incoming messages
static void dispatch_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  std::pair<msg_handler_map_iter, msg_handler_map_iter> iters =
    lctx->lsim->msg_handlers.equal_range(msg->tmpl);
  for(msg_handler_map_iter iter = iters.first; iter != iters.second; iter++) {
    assert(iter->first == msg->tmpl);
    iter->second(lctx, msg);
  }
  if(iters.first == iters.second) {
    printf("DEBUG: no handler for message %s\n",
	   msg->tmpl->name);
  }
  user_reset_timeout(lctx->u);
}

static void disable_sim(void *user_priv) {
  omuser_ctx* lctx = (omuser_ctx*)user_priv;
  SL_DECLMSG(DisableSimulator, quit);
  sl_send_udp(lctx, &quit);
}

static void remove_user(void *user_priv) {
  omuser_ctx* lctx = (omuser_ctx*)user_priv;

  free_texture_sends(lctx);

  free_resend_data(lctx);

  for(omuser_ctx** luser = &lctx->lsim->ctxts; *luser != NULL; ) {
    if(*luser == lctx) {
      *luser = lctx->next; break;
    } else luser = &(*luser)->next;
  }


  delete lctx;
}

// FIXME - use GCC-specific extensions for robustness?
static user_hooks our_hooks = {
  teleport_failed,
  teleport_progress,
  teleport_complete,
  remove_user,
  disable_sim,
  chat_callback,
  send_av_full_update,
  send_av_terse_update,
  send_av_appearance,
  send_av_animations,
};

static gboolean got_packet(GIOChannel *source,
			   GIOCondition condition,
			   gpointer data) {
  struct omuser_sim_ctx* lsim = (struct omuser_sim_ctx*)data;
  struct sockaddr_in addr;
  int ret; socklen_t addrlen;
  unsigned char buf[BUF_SIZE];
  struct sl_message msg;
    send_pending_acks(lsim); // FIXME - more suitable timing
    addrlen = sizeof(addr);
    ret = recvfrom(lsim->sock, buf, BUF_SIZE, 0, 
		   (struct sockaddr*)&addr, &addrlen);
    //printf("Packet! %i\n", ret);
    if(sl_parse_message(buf, ret, &msg)) {
      printf("DEBUG: packet parse failed\n");
      goto out;
    };
    if(msg.tmpl == &sl_msgt_UseCircuitCode) {
      // FIXME - needs a full rewrite

      printf("DEBUG: handling UseCircuitCode\n");
      // FIXME - need to handle dupes
      SL_DECLBLK_GET1(UseCircuitCode, CircuitCode, cc, &msg);
      struct user_ctx* ctx; struct sl_message ack;
      // FIXME - also need to clean this up to use new macros fully
      struct sl_blk_PacketAck_Packets *ackack;

      if(cc == NULL) goto out;
      // FIXME: handle same port/addr, different agent case
      ctx = sim_bind_user(lsim->sim, cc->ID, cc->SessionID,
			  cc->Code, &our_hooks);
      if(ctx == NULL) {
	printf("DEBUG: got unexpected UseCircuitCode\n");
	sl_dump_packet(&msg);
	goto out;
      }
      if(ctx->user_priv != NULL) {
	// FIXME - check port/addr matches, resend ack
	printf("DEBUG: got dupe UseCircuitCode?\n");
	goto out;
      }

      omuser_ctx *lctx = new omuser_ctx();
      lctx->u = ctx; lctx->lsim = lsim; 
      ctx->user_priv = lctx;
      lctx->addr = addr;
      lctx->sock = lsim->sock; lctx->counter = 0;

      lctx->next = lsim->ctxts; lsim->ctxts = lctx;

      sl_new_message(&sl_msgt_PacketAck,&ack);
      ackack = SL_ADDBLK(PacketAck, Packets,&ack);
      ackack->ID = msg.seqno;
      sl_send_udp(lctx,&ack);

      SL_DECLMSG(RegionHandshake, rh);
      SL_DECLBLK(RegionHandshake, RegionInfo, ri, &rh);
      SL_DECLBLK(RegionHandshake, RegionInfo2, ri2, &rh);
      SL_DECLBLK(RegionHandshake, RegionInfo3, ri3, &rh);
      rh.flags |= MSG_RELIABLE;
      ri->RegionFlags = 0; // FIXME
      ri->SimAccess = 0; // FIXME
      sl_string_set(&ri->SimName, lsim->sim->name);
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
      uuid_copy(ri2->RegionID, lsim->sim->region_id);
      ri3->CPUClassID = 1; ri3->CPURatio = 1;
      sl_string_set(&ri3->ColoName, "Nowhere");
      sl_string_set(&ri3->ProductName, "Cajeput Server Demo");
      sl_string_set(&ri3->ProductSKU, "0");
      sl_send_udp(lctx,&rh);
      
    } else {
      struct omuser_ctx* lctx;
      for(lctx = lsim->ctxts; lctx != NULL; lctx = lctx->next) {
	if(lctx->addr.sin_addr.s_addr == addr.sin_addr.s_addr &&
	   lctx->addr.sin_port == addr.sin_port) {
	  if(msg.flags & MSG_RELIABLE) {
	    // FIXME - need to do this for unknown messages too
	    lctx->pending_acks.push_back(msg.seqno);
	  }
	  std::set<uint32_t>::iterator iter = lctx->seen_packets.find(msg.seqno);
	  if(iter == lctx->seen_packets.end()) {
	    lctx->seen_packets.insert(msg.seqno);
	    dispatch_msg(lctx, &msg);
	  }

	  for(int i = 0; i < msg.num_appended_acks; i++) {
	    handle_packet_ack(lctx, msg.acks[i]);
	  }
	  
	  break;
	}
      }
    }
 out:
    sl_free_msg(&msg);

    // FIXME - what should I return?
}

static void shutdown_handler(simulator_ctx *sim, void *priv) {
  printf("DEBUG: running caj_omv shutdown hook\n");
  omuser_sim_ctx *lsim = (omuser_sim_ctx*) priv;
  g_io_channel_shutdown(lsim->gio_sock, FALSE, NULL);
  g_io_channel_unref(lsim->gio_sock);
  delete lsim;
}

#define ADD_HANDLER(name) register_msg_handler(lsim, &sl_msgt_##name, handle_##name##_msg)

// FIXME - need to detect ABI mismatches before we can modularise!
void sim_int_init_udp(struct simulator_ctx *sim)  {
  omuser_sim_ctx *lsim = new omuser_sim_ctx();
  lsim->sim = sim;
  lsim->ctxts = NULL;
  lsim->xfer_id_ctr = 1;
  int sock; struct sockaddr_in addr;

  sim_add_shutdown_hook(sim, shutdown_handler, lsim);

  ADD_HANDLER(AgentUpdate);
  ADD_HANDLER(StartPingCheck);
  ADD_HANDLER(CompleteAgentMovement);
  ADD_HANDLER(LogoutRequest);
  ADD_HANDLER(ChatFromViewer);
  ADD_HANDLER(AgentThrottle);
  ADD_HANDLER(RegionHandshakeReply);
  ADD_HANDLER(AgentWearablesRequest);
  ADD_HANDLER(AssetUploadRequest);
  ADD_HANDLER(SendXferPacket);
  // FIXME - handle AbortXfer
  ADD_HANDLER(RequestImage);
  ADD_HANDLER(AgentDataUpdateRequest);
  ADD_HANDLER(AgentSetAppearance);
  ADD_HANDLER(AgentAnimation);
  ADD_HANDLER(PacketAck);
  ADD_HANDLER(MapLayerRequest);
  ADD_HANDLER(MapBlockRequest);
  ADD_HANDLER(TeleportLocationRequest);
  ADD_HANDLER(TeleportLandmarkRequest);

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  addr.sin_family= AF_INET;
  addr.sin_port = htons(sim->udp_port);
  addr.sin_addr.s_addr=INADDR_ANY;
  bind(sock, (struct sockaddr*)&addr, sizeof(addr));
  lsim->gio_sock = g_io_channel_unix_new(sock);
  g_io_add_watch(lsim->gio_sock, G_IO_IN, got_packet, lsim);
  lsim->sock = sock;  

  g_timeout_add(100, texture_send_timer, lsim); // FIXME - check the timing on this
  g_timeout_add(100, obj_update_timer, lsim);  
  g_timeout_add(200, resend_timer, lsim);
}

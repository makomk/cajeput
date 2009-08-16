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
#include "cajeput_anims.h"
#include <stdlib.h>
#include <cassert>

#define BUF_SIZE 2048

// FIXME - resend lost packets
void sl_send_udp_throt(struct user_ctx* ctx, struct sl_message* msg, int throt_id) {
  // sends packet and updates throttle, but doesn't queue
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
    if(throt_id >= 0) ctx->throttles[throt_id].level -= len;
  } else {
    printf("DEBUG: couldn't pack message, not sending\n");
  }
  sl_free_msg(msg);
}

void sl_send_udp(struct user_ctx* ctx, struct sl_message* msg) {
  sl_send_udp_throt(ctx, msg, -1);
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
    ctx->pending_acks.clear();
  }
}

void user_set_throttles(struct user_ctx *ctx, float rates[]) {
  double time_now = g_timer_elapsed(ctx->sim->timer, NULL);
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    ctx->throttles[i].time = time_now;
    ctx->throttles[i].level = 0.0f;
    ctx->throttles[i].rate = rates[i];
    
  }
}

void user_reset_throttles(struct user_ctx *ctx) {
  double time_now = g_timer_elapsed(ctx->sim->timer, NULL);
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    ctx->throttles[i].time = time_now;
    ctx->throttles[i].level = 0.0f;
  }
}

void user_update_throttles(struct user_ctx *ctx) {
  double time_now = g_timer_elapsed(ctx->sim->timer, NULL);
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    assert(time_now >=  ctx->throttles[i].time); // need monotonic time
    ctx->throttles[i].level += ctx->throttles[i].rate * 
      (time_now - ctx->throttles[i].time);

    if(ctx->throttles[i].level > ctx->throttles[i].rate * 0.3f) {
      // limit maximum reservoir level to 0.3 sec of data
      ctx->throttles[i].level = ctx->throttles[i].rate * 0.3f;
    }
    ctx->throttles[i].time = time_now;
  }
  
}


#define VALIDATE_SESSION(ad) (uuid_compare(ctx->user_id, ad->AgentID) != 0 || uuid_compare(ctx->session_id, ad->SessionID) != 0)

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

static void handle_AgentAnimation_msg(struct user_ctx* ctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentAnimation, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  sl_dump_packet(msg); // FIXME - TODO

  int count = SL_GETBLK(AgentAnimation, AnimationList, msg).count;
  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(AgentAnimation, AnimationList, aitem) =
      SL_GETBLKI(AgentAnimation, AnimationList, msg, i);
    if(aitem->StartAnim) {
      animation_desc anim;
      uuid_copy(anim.anim, aitem->AnimID);
      anim.sequence = ctx->anim_seq++; // FIXME
      anim.caj_type = CAJ_ANIM_TYPE_NORMAL;
      uuid_copy(anim.obj, ctx->user_id);
      user_add_animation(ctx, &anim, false);
      // TODO - (FIXME implement this!)
    } else {
      // FIXME - handle clearing default animation
      user_clear_animation_by_id(ctx, aitem->AnimID);
    }
  }

  ctx->flags |= AGENT_FLAG_ANIM_UPDATE;
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

void user_send_message(struct user_ctx *ctx, const char* msg) {
  struct chat_message chat;
  chat.source_type = CHAT_SOURCE_SYSTEM;
  chat.chat_type = CHAT_TYPE_NORMAL;
  uuid_clear(chat.source); // FIXME - set this?
  uuid_clear(chat.owner);
  chat.name = "Cajeput";
  chat.msg = (char*)msg;

  // FIXME - evil hack
  av_chat_callback(ctx->sim, NULL, &chat, ctx);
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

static void send_agent_wearables(struct user_ctx* ctx) {
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
  sl_send_udp(ctx, &upd);
}

static void handle_AgentWearablesRequest_msg(struct user_ctx* ctx, struct sl_message* msg) {
    struct sl_blk_AgentWearablesRequest_AgentData *ad;
    if(SL_GETBLK(AgentWearablesRequest,AgentData,msg).count != 1) return;
    ad = SL_GETBLKI(AgentWearablesRequest,AgentData,msg,0);
    if(VALIDATE_SESSION(ad)) 
      return;
    send_agent_wearables(ctx);
}

static void handle_AgentSetAppearance_msg(struct user_ctx* ctx, struct sl_message* msg) {
  sl_string str;
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

static void send_agent_data_update(struct user_ctx* ctx) {
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
  sl_send_udp(ctx, &upd);
}

static void handle_AgentDataUpdateRequest_msg(struct user_ctx* ctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentDataUpdateRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  send_agent_data_update(ctx);
}

static void handle_RegionHandshakeReply_msg(struct user_ctx* ctx, struct sl_message* msg) {
    struct sl_blk_RegionHandshakeReply_AgentData *ad;
    if(SL_GETBLK(RegionHandshakeReply,AgentData,msg).count != 1) return;
    ad = SL_GETBLKI(RegionHandshakeReply,AgentData,msg,0);
    if(VALIDATE_SESSION(ad)) 
      return;
    ctx->flags |= AGENT_FLAG_RHR | AGENT_FLAG_NEED_OTHER_AVS;
    // FIXME - should we do something with RegionInfo.Flags?
}

static void handle_PacketAck_msg(struct user_ctx* ctx, struct sl_message* msg) {
  // FIXME FIXME FIXME
}

static void handle_CompleteAgentMovement_msg(struct user_ctx* ctx, struct sl_message* msg) {
    struct sl_blk_CompleteAgentMovement_AgentData *ad;
    //struct sl_message amc;
    if(SL_GETBLK(CompleteAgentMovement,AgentData,msg).count != 1) return;
    ad = SL_GETBLKI(CompleteAgentMovement,AgentData,msg,0);
    if(ad->CircuitCode != ctx->circuit_code || VALIDATE_SESSION(ad)) 
      return;
    if(!(ctx->flags & AGENT_FLAG_INCOMING)) {
      printf("ERROR: unexpected CompleteAgentMovement for %s %s\n",
	     ctx->first_name, ctx->last_name);
      return;
    }
    ctx->flags &= ~AGENT_FLAG_CHILD;
    ctx->flags |= AGENT_FLAG_ENTERED | AGENT_FLAG_APPEARANCE_UPD |  
      AGENT_FLAG_ANIM_UPDATE | AGENT_FLAG_AV_FULL_UPD;
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

    // FIXME - move this somewhere saner?
    if(ctx->sim->welcome_message != NULL)
      user_send_message(ctx, ctx->sim->welcome_message);
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

void user_set_throttles_block(struct user_ctx* ctx, unsigned char* data,
			      int len) {
  float throttles[SL_NUM_THROTTLES];

  if(len < SL_NUM_THROTTLES*4) {
    printf("Error: AgentThrottle with not enough data\n");
    return;
  }

  printf("DEBUG: got new throttles:\n");
  for(int i = 0; i < SL_NUM_THROTTLES; i++) {
    throttles[i] =  sl_unpack_float(data + 4*i);
    printf("  throttle %s: %f\n", sl_throttle_names[i], throttles[i]);
    user_set_throttles(ctx, throttles);
  }
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

  user_set_throttles_block(ctx, throt->Throttles.data,
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

static asset_xfer* init_asset_upload(struct user_ctx* ctx, int is_local,
				     int asset_type, uuid_t transaction) {
  asset_xfer *xfer = new asset_xfer();
  xfer->local = is_local;
  xfer->asset_type = asset_type;
  uuid_copy(xfer->transaction, transaction);
  helper_combine_uuids(xfer->asset_id, transaction,
		       ctx->secure_session_id);
  xfer->id = ++(ctx->sim->xfer_id_ctr);
  xfer->ctr = 0;
  xfer->len = xfer->total_len = 0;
  xfer->data = NULL;
  ctx->xfers[xfer->id] = xfer;
  return xfer;
}

static void complete_asset_upload(user_ctx* ctx, asset_xfer *xfer,
				  int success) {
  ctx->xfers.erase(xfer->id);
  SL_DECLMSG(AssetUploadComplete, complete);
  SL_DECLBLK(AssetUploadComplete, AssetBlock, blk, &complete);
  blk->Type = xfer->asset_type;
  blk->Success = !!success;
  uuid_copy(blk->UUID, xfer->asset_id);
  complete.flags |= MSG_RELIABLE;
  sl_send_udp(ctx, &complete);

  if(success) {
    sim_add_local_texture(ctx->sim, xfer->asset_id, xfer->data,
			  xfer->len, true);
  } else {
    free(xfer->data); // note: sim_add_local_texture take ownership of buffer
  }

  delete xfer;
}

static void handle_SendXferPacket_msg(struct user_ctx* ctx, struct sl_message* msg) {
  asset_xfer *xfer;
  SL_DECLBLK_GET1(SendXferPacket,XferID,xferid,msg);
  SL_DECLBLK_GET1(SendXferPacket,DataPacket,data,msg);

  std::map<uint64_t,asset_xfer*>::iterator iter =
    ctx->xfers.find(xferid->ID);
  if(iter == ctx->xfers.end()) {
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
    sl_send_udp(ctx, &confirm);
  }

  if(xferid->Packet & 0x80000000) {
    if(xfer->len != xfer->total_len) {
      printf("Bad xfer length at end of data: got %i, want %i\n",
	     xfer->len, xfer->total_len);
      return;
    }
    complete_asset_upload(ctx, xfer, 1);
  }
}

static void handle_AssetUploadRequest_msg(struct user_ctx* ctx, struct sl_message* msg) {
  asset_xfer *xfer;
  SL_DECLBLK_GET1(AssetUploadRequest,AssetBlock,asset,msg);
  if(asset == NULL) return;
  sl_dump_packet(msg);
  switch(asset->Type) {
  case 0: // texture
    if(!asset->StoreLocal) {
      user_send_message(ctx, "Sorry, AssetUploadRequest is now only for local texture uploads.\n");
      return;
    }
    printf("DEBUG: got local AssetUploadRequest for texture\n");
    break;
  default:
    user_send_message(ctx, "ERROR: AssetUploadRequest for unsupported type\n");
    return;
  }

  // FIXME - handle "all data in initial packet" case

  xfer = init_asset_upload(ctx, asset->StoreLocal,
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
    sl_send_udp(ctx, &xferreq);
  }
}

// called as part of the user removal process
void user_int_free_texture_sends(struct user_ctx *ctx) {
  for(std::map<obj_uuid_t,image_request*>::iterator iter = 
	ctx->image_reqs.begin();  iter != ctx->image_reqs.end(); iter++) {
    image_request *req = iter->second;
    req->texture->refcnt--; delete req;
  }
}

#define TEXTURE_FIRST_LEN 600
#define TEXTURE_PACKET_LEN 1000

int send_err_throt = 0; // HACK

static gboolean texture_send_timer(gpointer data) {
  struct simulator_ctx* sim = (simulator_ctx*)data;
  send_err_throt++;
  for(user_ctx* ctx = sim->ctxts; ctx != NULL; ctx = ctx->next) {
    std::map<obj_uuid_t,image_request*>::iterator req_iter =
      ctx->image_reqs.begin(); // FIXME - do by priority
    user_update_throttles(ctx);
    while(ctx->throttles[SL_THROTTLE_TEXTURE].level > 0.0f) {
      if(req_iter == ctx->image_reqs.end()) break;
      image_request *req = req_iter->second;
      if(req->texture->flags & CJP_TEXTURE_MISSING) {
	char uuid_buf[40]; uuid_unparse(req->texture->asset_id, uuid_buf);
	printf("Image %s not found!\n", uuid_buf);
	  
	SL_DECLMSG(ImageNotInDatabase, noimg);
	SL_DECLBLK(ImageNotInDatabase, ImageID, iid, &noimg);
	uuid_copy(iid->ID, req->texture->asset_id);
	noimg.flags |= MSG_RELIABLE;
	sl_send_udp_throt(ctx, &noimg, SL_THROTTLE_TEXTURE);

	std::map<obj_uuid_t,image_request*>::iterator next = req_iter;
	next++;
	req->texture->refcnt--; delete req;
	ctx->image_reqs.erase(req_iter);
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
	  ctx->image_reqs.erase(req_iter);
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
	  sl_send_udp_throt(ctx, &imgdat, SL_THROTTLE_TEXTURE);
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
	  sl_send_udp_throt(ctx, &imgpkt, SL_THROTTLE_TEXTURE);
	  req->packet_no++;
	}
      }
    }

    //printf("DEBUG: texture throttle at %f\n", (double)ctx->throttles[SL_THROTTLE_TEXTURE].level);
  }
  
  return TRUE;
}


static void handle_RequestImage_msg(struct user_ctx* ctx, struct sl_message* msg) {
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
      ctx->image_reqs.find(ri->Image);
    
    if(ri->DiscardLevel >= 0) {
      if(iter == ctx->image_reqs.end()) {
	texture_desc *texture = sim_get_texture(ctx->sim, ri->Image);
	image_request* ireq = new image_request;
	ireq->texture = texture;
	ireq->priority = ri->DownloadPriority;
	ireq->discard = ri->DiscardLevel;
	ireq->packet_no = ri->Packet;
	sim_request_texture(ctx->sim, texture);
	ctx->image_reqs[ri->Image] = ireq;
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
    } else if(iter != ctx->image_reqs.end()) {
      printf("Deleting RequestImage for %s\n", buf);
      image_request* ireq = iter->second;
      ireq->texture->refcnt--;
      delete ireq;
      ctx->image_reqs.erase(iter);
    }
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
  if(iters.first == iters.second) {
    printf("DEBUG: no handler for message %s\n",
	   msg->tmpl->name);
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

#define ADD_HANDLER(name) register_msg_handler(sim, &sl_msgt_##name, handle_##name##_msg)

void sim_int_init_udp(struct simulator_ctx *sim)  {
  int sock; struct sockaddr_in addr;

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

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  addr.sin_family= AF_INET;
  addr.sin_port = htons(sim->udp_port);
  addr.sin_addr.s_addr=INADDR_ANY;
  bind(sock, (struct sockaddr*)&addr, sizeof(addr));
  GIOChannel* gio_sock = g_io_channel_unix_new(sock);
  g_io_add_watch(gio_sock, G_IO_IN, got_packet, sim);
  sim->sock = sock;  

  g_timeout_add(100, texture_send_timer, sim); // FIXME - check the timing on this
  
}

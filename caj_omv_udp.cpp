/* Copyright (c) 2009-2010 Aidan Thornton, all rights reserved.
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
//#include "caj_llsd.h"
#include "cajeput_core.h"
#include "cajeput_plugin.h"
#include "cajeput_user.h"
#include "cajeput_world.h"
#include "cajeput_user_glue.h"
#include "cajeput_anims.h"
#include "cajeput_prim.h"
#include "caj_helpers.h"
#include "caj_omv.h"
#include "caj_version.h"
#include "terrain_compress.h"
#include <stdlib.h>
#include <cassert>
#include <stdio.h>


#define BUF_SIZE 2048

#define DEBUG_CHANNEL 2147483647

#define RESEND_INTERVAL 1.0
#define MAX_RESENDS 3

// fun stuff for talking to the SL viewer
#define PCODE_PRIM 9
#define PCODE_AV 47
#define PCODE_GRASS 95
#define PCODE_NEWTREE 111
#define PCODE_PARTSYS 143 /* ??? */
#define PCODE_TREE 255

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
    if(throt_id >= 0) user_throttle_expend(lctx->u, throt_id, len);
  } else {
    printf("DEBUG: couldn't pack message, not sending\n");
  }
  if(len > 0 && (msg->flags & MSG_RELIABLE)) {
    // FIXME - remove acks on resend?
    udp_resend_desc *resend = new udp_resend_desc();
    resend->time = caj_get_timer(user_get_sgrp(lctx->u)) + RESEND_INTERVAL;
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

  lctx->resends.erase(iter);
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
  double time_now = caj_get_timer(sim_get_simgroup(lsim->sim));

  for(omuser_ctx* lctx = lsim->ctxts; lctx != NULL; lctx = lctx->next) {    
    if(user_get_flags(lctx->u) & (AGENT_FLAG_IN_SLOW_REMOVAL|AGENT_FLAG_PURGE))
      continue;
    
    user_update_throttles(lctx->u);

    while(user_throttle_level(lctx->u, SL_THROTTLE_RESEND) > 0.0f) {
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
	user_throttle_expend(lctx->u, SL_THROTTLE_RESEND, len);
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

#define VALIDATE_SESSION(ad) user_check_session(lctx->u, ad->AgentID, ad->SessionID)
#define VALIDATE_SESSION_2(aid,sid) user_check_session(lctx->u, (aid), (sid))

static void handle_AgentUpdate_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentUpdate, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  user_ctx *ctx = lctx->u;
  user_set_draw_dist(ctx, ad->Far);
  user_set_control_flags(ctx, ad->ControlFlags, &ad->BodyRotation);
}

static void handle_SetAlwaysRun_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(SetAlwaysRun, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  if(ad->AlwaysRun)
    user_set_flag(lctx->u, AGENT_FLAG_ALWAYS_RUN);
  else user_clear_flag(lctx->u, AGENT_FLAG_ALWAYS_RUN);
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
      anim.sequence = user_get_an_anim_seq(lctx->u); // FIXME
      anim.caj_type = CAJ_ANIM_TYPE_NORMAL;
      user_get_uuid(lctx->u, anim.obj);
      user_add_animation(lctx->u, &anim, false);
    } else {
      // FIXME - handle clearing default animation
      user_clear_animation_by_id(lctx->u, aitem->AnimID);
    }
  }

  user_set_flag(lctx->u, AGENT_FLAG_ANIM_UPDATE);
}

static void handle_AgentRequestSit_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentRequestSit, AgentData, ad, msg);
  SL_DECLBLK_GET1(AgentRequestSit, TargetObject, targ, msg);
  if(ad == NULL || targ == NULL || VALIDATE_SESSION(ad)) return;

  sl_dump_packet(msg); // FIXME

  uuid_clear(lctx->sit_info.target);
  lctx->sit_info.offset = targ->Offset;

  struct world_obj *ob = world_object_by_id(lctx->lsim->sim, targ->TargetID);
  if(ob == NULL) {
    user_send_alert_message(lctx->u, "Cannot sit on non-existant object", FALSE);
    return;
  } else if(ob->type != OBJ_TYPE_PRIM) {
    user_send_alert_message(lctx->u, "You can only sit on prims.", FALSE);
    return;
  }

  if(user_begin_sit(lctx->u, (primitive_obj*)ob, &lctx->sit_info)) {
    SL_DECLMSG(AvatarSitResponse, resp);
    SL_DECLBLK(AvatarSitResponse, SitObject, sitobj, &resp);
    uuid_copy(sitobj->ID, lctx->sit_info.target);
    SL_DECLBLK(AvatarSitResponse, SitTransform, sittrans, &resp);
    sittrans->AutoPilot = FALSE; // ???
    sittrans->SitPosition = lctx->sit_info.offset;
    sittrans->SitRotation = lctx->sit_info.rot;
    // FIXME - TODO - CameraEyeOffset / CameraAtOffset
    sittrans->ForceMouselook = FALSE;
    resp.flags |= MSG_RELIABLE;
    sl_dump_packet(&resp); // FIXME
    sl_send_udp(lctx, &resp);

    // if we're not doing autopilot, we have to sit immediately.
    if(!user_complete_sit(lctx->u, &lctx->sit_info)) {
      user_send_alert_message(lctx->u, "Couldn't complete sit!", FALSE);
    }
    uuid_clear(lctx->sit_info.target);
  } else {
    user_send_alert_message(lctx->u, "You can't sit here.", FALSE);
  }
}

static void handle_AgentSit_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  sl_dump_packet(msg); // FIXME

  SL_DECLBLK_GET1(AgentSit, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;

  if(uuid_is_null(lctx->sit_info.target)) {
    user_send_alert_message(lctx->u, "Tried to sit with no target selected", FALSE);
    return;
  }

  if(!user_complete_sit(lctx->u, &lctx->sit_info)) {
    user_send_alert_message(lctx->u, "Couldn't complete sit!", FALSE);
  }
  
  uuid_clear(lctx->sit_info.target);
}

static void handle_StartPingCheck_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(StartPingCheck, PingID, ping, msg);
  if(ping == NULL) return;
  SL_DECLMSG(CompletePingCheck, pong);
  SL_DECLBLK(CompletePingCheck, PingID, pongid, &pong);
  pongid->PingID = ping->PingID;
  sl_send_udp(lctx,&pong);
}

static void handle_EconomyDataRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLMSG(EconomyData, resp);
  SL_DECLBLK(EconomyData, Info, info, &resp);
  info->ObjectCapacity = 0; // FIXME?
  info->ObjectCount = 0; // FIXME?
  info->PriceEnergyUnit = 0;
  info->PriceObjectClaim = 0;
  info->PricePublicObjectDecay = 0;
  info->PricePublicObjectDelete = 0;
  info->PriceParcelClaim = 0;
  info->PriceParcelClaimFactor = 0.0f;
  info->PriceUpload = 0; // FIXME.
  info->PriceRentLight = 0;
  info->TeleportMinPrice = 0;
  info->TeleportPriceExponent = 0.0f; // should be legal, touch wood.
  info->EnergyEfficiency = 1.0f; // FIXME - use 0.0f for better zerocoding?
  info->PriceObjectRent = 0.0f;
  info->PriceObjectScaleFactor = 0.0f;
  info->PriceParcelRent = 0;
  info->PriceGroupCreate = 0; // FIXME
  resp.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &resp);
}

static void handle_MoneyBalanceRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(MoneyBalanceRequest, AgentData, ad, msg);
  SL_DECLBLK_GET1(MoneyBalanceRequest, MoneyData, md, msg);
  if(ad == NULL || md == NULL || VALIDATE_SESSION(ad)) return;

  SL_DECLMSG(MoneyBalanceReply, reply);
  SL_DECLBLK(MoneyBalanceReply, MoneyData, rmd, &reply);
  user_get_uuid(lctx->u, rmd->AgentID);
  uuid_copy(rmd->TransactionID, md->TransactionID);
  rmd->TransactionSuccess = TRUE;
  rmd->MoneyBalance = 0;
  rmd->SquareMetersCredit = 0;
  rmd->SquareMetersCommitted = 0;
  caj_string_set(&rmd->Description, "");

  reply.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &reply);
}

static void send_script_dialog(void *user_priv, primitive_obj* prim,
			       char *msg, int num_buttons, char** buttons,
			       int32_t channel) {
  omuser_ctx* lctx = (omuser_ctx*)user_priv;
  SL_DECLMSG(ScriptDialog, dialog);
  SL_DECLBLK(ScriptDialog, Data, ddata, &dialog);
  uuid_copy(ddata->ObjectID, prim->ob.id);
  caj_string_set(&ddata->FirstName, "???"); // FIXME!
  caj_string_set(&ddata->LastName, "???"); // FIXME
  caj_string_set(&ddata->ObjectName, prim->name);
  caj_string_set(&ddata->Message, msg);
  ddata->ChatChannel = channel;
  uuid_clear(ddata->ImageID); // FIXME - ???
  
  for(int i = 0; i < num_buttons; i++) {
    SL_DECLBLK(ScriptDialog, Buttons, button, &dialog);
    caj_string_set(&button->ButtonLabel, buttons[i]);
  }
  dialog.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &dialog);
}

static void chat_callback(void *user_priv, const struct chat_message *msg) {
  omuser_ctx* lctx = (omuser_ctx*)user_priv;
  SL_DECLMSG(ChatFromSimulator, chat);
  SL_DECLBLK(ChatFromSimulator, ChatData, cdata, &chat);
  caj_string_set(&cdata->FromName, msg->name);
  uuid_copy(cdata->SourceID, msg->source);
  uuid_copy(cdata->OwnerID, msg->owner);
  cdata->SourceType = msg->source_type;
  cdata->ChatType = (msg->channel == DEBUG_CHANNEL ? CHAT_TYPE_DEBUG : 
		     msg->chat_type);
  cdata->Audible = CHAT_AUDIBLE_FULLY;
  caj_string_set(&cdata->Message,msg->msg);
  chat.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &chat);
}

static void send_alert_message(void *user_priv, const char* msg, int is_modal) {
  omuser_ctx* lctx = (omuser_ctx*)user_priv;
  SL_DECLMSG(AgentAlertMessage, alert);
  SL_DECLBLK(AgentAlertMessage, AgentData, ad, &alert);
  user_get_uuid(lctx->u, ad->AgentID);

  SL_DECLBLK(AgentAlertMessage, AlertData, alertdat, &alert);
  alertdat->Modal = !!is_modal;
  caj_string_set(&alertdat->Message, msg);
  alert.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &alert);
}

static void handle_ChatFromViewer_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ChatFromViewer, AgentData, ad, msg);
  SL_DECLBLK_GET1(ChatFromViewer, ChatData, cdata, msg);
  if(ad == NULL || cdata == NULL || VALIDATE_SESSION(ad)) return;

  printf("DEBUG: got ChatFromViewer\n");
  if(cdata->Channel < 0) return; // can't send on negative channels
  world_chat_from_user(lctx->u, cdata->Channel,
		       (char*)cdata->Message.data, cdata->Type);
}

static void handle_ScriptDialogReply_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ScriptDialogReply, AgentData, ad, msg);
  SL_DECLBLK_GET1(ScriptDialogReply, Data, cdata, msg);
  if(ad == NULL || cdata == NULL || VALIDATE_SESSION(ad)) return;
  
  printf("DEBUG: got ScriptDialogReply\n");

  if(cdata->ChatChannel == 0) {
    printf("DEBUG: ScriptDialogReply on channel 0, discarding\n");
    return;
  }

  // FIXME - chat from root prim?
  world_obj* src = world_object_by_id(lctx->lsim->sim, cdata->ObjectID);
  if(src == NULL) {
    printf("DEBUG: ScriptDialogReply for unknown obj, discarding\n");
    return;
  }
  
  struct chat_message chat;
  chat.channel = cdata->ChatChannel;
  chat.pos = src->world_pos;
  chat.name = (char*)user_get_name(lctx->u);
  user_get_uuid(lctx->u, chat.source);
  uuid_clear(chat.owner); // FIXME - ???
  chat.source_type = CHAT_SOURCE_AVATAR;
  chat.chat_type = CHAT_TYPE_NORMAL;
  chat.msg = (char*)cdata->ButtonLabel.data;
  world_send_chat(lctx->lsim->sim, &chat);
}

static void handle_RequestGodlikePowers_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RequestGodlikePowers, AgentData, ad, msg);
  SL_DECLBLK_GET1(RequestGodlikePowers, RequestBlock, req, msg);
  if(ad == NULL || req == NULL || VALIDATE_SESSION(ad)) return;
  if(req->Godlike) {
    int level = user_request_god_powers(lctx->u);
    if(level > 0) {
      SL_DECLMSG(GrantGodlikePowers, grant);

      SL_DECLBLK(GrantGodlikePowers, AgentData, ad2, &grant);
      user_get_uuid(lctx->u, ad2->AgentID);
      user_get_session_id(lctx->u, ad2->SessionID); // required!
      SL_DECLBLK(GrantGodlikePowers, GrantData, gdat, &grant);
      gdat->GodLevel = level; uuid_clear(gdat->Token);

      grant.flags |= MSG_RELIABLE;
      sl_send_udp(lctx, &grant);
    } else {
      user_send_alert_message(lctx->u, "Your request for god-like powers is denied, mortal", FALSE);
    }
  } else {
    user_relinquish_god_powers(lctx->u);
  }
}

static void handle_MapLayerRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(MapLayerRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  
  // FIXME - this is some evil hack copied from OpenSim
  SL_DECLMSG(MapLayerReply, reply);
  SL_DECLBLK(MapLayerReply, AgentData, ad2, &reply);
  user_get_uuid(lctx->u, ad2->AgentID);
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
  char *name;
  uint32_t x, y;
};

// we'll start running into trouble if a bunch of sims have names longer than
// 100 bytes, but that's unlikely, I think.
#define MAX_MAP_BLOCKS_PER_MSG 10

static void map_block_req_cb(void *priv, struct map_block_info *blocks, int count) {
  map_block_req_priv *req = (map_block_req_priv*)priv;
  if(req->ctx != NULL) {
    // FIXME - should we send an empty message if we have nothing?
    // For now, I'm guessing not

    int send_count = count;
    if(req->name != NULL) send_count++;
    else if((req->flags & 0x10000) != 0 && count == 0) send_count = 1;

    for(int i = 0; i < send_count; /*nothing*/) {
      SL_DECLMSG(MapBlockReply, reply);
      SL_DECLBLK(MapBlockReply, AgentData, ad, &reply);
      user_get_uuid(req->ctx, ad->AgentID);
      ad->Flags = req->flags & ~0x10000; // just hardcode to 2?
      
      for(int j = 0; j < MAX_MAP_BLOCKS_PER_MSG && i < send_count; i++, j++) {
	SL_DECLBLK(MapBlockReply, Data, dat, &reply);
	if(i < count) {
	  dat->X = blocks[i].x;
	  dat->Y = blocks[i].y;
	  caj_string_set(&dat->Name, blocks[i].name);
	  dat->Access = blocks[i].access;
	  dat->RegionFlags = blocks[i].flags;
	  dat->WaterHeight = blocks[i].water_height;
	  dat->Agents = blocks[i].num_agents;
	  uuid_copy(dat->MapImageID, blocks[i].map_image);
	} else if(req->name != NULL) {
	  dat->X = dat->Y = 0;
	  caj_string_set(&dat->Name, req->name);
	  dat->Access = 255;
	  dat->RegionFlags = 0;
	  dat->WaterHeight = 0;
	  dat->Agents = 0;
	  uuid_clear(dat->MapImageID);
	} else if(req->flags & 0x10000) {
	  // special magic to do with clicking map tiles
	  dat->X = req->x; dat->Y = req->y;
	  dat->Name.len = 0;
	  dat->Access = 255; // NOT 254 - that's "region offline"!
	  dat->RegionFlags = 0;
	  dat->WaterHeight = 0;
	  dat->Agents = 0;
	  uuid_clear(dat->MapImageID);
	}
      }
      
      reply.flags |= MSG_RELIABLE;
      printf("DEBUG: sending MapBlockReply\n");
      sl_send_udp(req->lctx, &reply);
    }
    
    user_del_self_pointer(&req->ctx);
  }
  free(req->name);
  delete req;
}

static void handle_MapBlockRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(MapBlockRequest, AgentData, ad, msg);
  SL_DECLBLK_GET1(MapBlockRequest, PositionData, pos, msg);
  if(ad == NULL || pos == NULL || VALIDATE_SESSION(ad)) return;

  user_ctx *ctx = lctx->u;
  map_block_req_priv *priv = new map_block_req_priv();
  priv->ctx = ctx; priv->flags = ad->Flags; priv->lctx = lctx;
  priv->name = NULL; priv->x = pos->MinX; priv->y = pos->MinY;
  user_add_self_pointer(&priv->ctx);
  sl_dump_packet(msg);
  caj_map_block_request(user_get_sgrp(ctx), pos->MinX, pos->MaxX, 
			pos->MinY, pos->MaxY, map_block_req_cb, priv);
}

static void handle_MapNameRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(MapNameRequest, AgentData, ad, msg);
  SL_DECLBLK_GET1(MapNameRequest, NameData, name, msg);
  if(ad == NULL || name == NULL || VALIDATE_SESSION(ad)) return;

  user_ctx *ctx = lctx->u;
  map_block_req_priv *priv = new map_block_req_priv();
  priv->ctx = ctx; priv->flags = ad->Flags; priv->lctx = lctx;
  priv->name = strdup((char*)name->Name.data);
  user_add_self_pointer(&priv->ctx);
  caj_map_name_request(user_get_sgrp(ctx), priv->name, 
		       map_block_req_cb, priv);
}


static void send_agent_wearables(struct omuser_ctx* lctx) {
  user_ctx *ctx = lctx->u;
  SL_DECLMSG(AgentWearablesUpdate, upd);
  SL_DECLBLK(AgentWearablesUpdate, AgentData, ad, &upd);
  user_get_uuid(ctx, ad->AgentID);
  user_get_session_id(ctx, ad->SessionID);
  ad->SerialNum = user_get_wearable_serial(ctx); // FIXME: don't think we increment here

  const wearable_desc *wearables = user_get_wearables(ctx);
  for(int i = 0; i < SL_NUM_WEARABLES; i++) {
    // FIXME - avoid sending empty wearables?

     SL_DECLBLK(AgentWearablesUpdate, WearableData, wd, &upd);
     uuid_copy(wd->ItemID, wearables[i].item_id);
     uuid_copy(wd->AssetID, wearables[i].asset_id);
     wd->WearableType = i;
  }

  upd.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &upd);
}

static void handle_wearables_changed(void *user_priv) {
  omuser_ctx* lctx = (omuser_ctx*)user_priv;
  // FIXME - should only send wearables update when all asset IDS have been
  // found!
  send_agent_wearables(lctx);
}

static void handle_AgentWearablesRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentWearablesRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  send_agent_wearables(lctx);
}

static void handle_AgentIsNowWearing_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentIsNowWearing, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;

  int count = SL_GETBLK(AgentIsNowWearing, WearableData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(AgentIsNowWearing, WearableData, wd) =
      SL_GETBLKI(AgentIsNowWearing, WearableData, msg, i);
    user_set_wearable_item_id(lctx->u, wd->WearableType, wd->ItemID);
  }
}

static void handle_AgentSetAppearance_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  caj_string str; user_ctx* ctx = lctx->u;
  SL_DECLBLK_GET1(AgentSetAppearance, AgentData, ad, msg);
  SL_DECLBLK_GET1(AgentSetAppearance, ObjectData, objd, msg);
  if(ad == NULL || objd == NULL ||  VALIDATE_SESSION(ad)) 
    return;
  if(ad->SerialNum < lctx->appearance_serial) {
    printf("WARNING: Got outdated AgentSetAppearance\n");
    return;
  }

  lctx->appearance_serial = ad->SerialNum;
  // FIXME - do something with size
  printf("DEBUG: agent size (%f,%f,%f)\n",(double)ad->Size.x,(double)ad->Size.y,
	 (double)ad->Size.z);

  // FIXME - what to do with WearableData blocks?

  // we could steal the message's buffer, but that would be evil
  caj_string_copy(&str, &objd->TextureEntry);
  user_set_texture_entry(ctx, &str);

  str.len = SL_GETBLK(AgentSetAppearance, VisualParam, msg).count;
  str.data = (unsigned char*)malloc(str.len);
  for(int i = 0; i < str.len; i++) {
    str.data[i] = SL_GETBLKI(AgentSetAppearance, VisualParam, msg, i)->ParamValue;
  }
  user_set_visual_params(ctx, &str);

  printf("DEBUG: Completed AgentSetAppearance\n");
}

// FIXME - need to actually handle ViewerEffect message.
// (this is just a stub to prevent warning spam)
static void handle_ViewerEffect_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  static int done_warning = 0;
  SL_DECLBLK_GET1(ViewerEffect, AgentData, ad, msg);
  if(ad == NULL ||  VALIDATE_SESSION(ad)) 
    return;
  if(!done_warning) {
    printf("\nFIXME: need to handle ViewerEffect message\n\n");
    done_warning = 1;
  }
}

static void handle_AgentPause_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  sl_dump_packet(msg);
  SL_DECLBLK_GET1(AgentPause, AgentData, ad, msg);
  if(ad == NULL ||  VALIDATE_SESSION(ad)) 
    return;
  if(ad->SerialNum < lctx->pause_serial) {
    printf("WARNING: ignoring AgentPause with outdated serial\n");
    return;
  }
  
  printf("\nDEBUG: pausing agent %s\n\n", user_get_name(lctx->u));
  lctx->pause_serial = ad->SerialNum;
  user_set_paused(lctx->u);
}

static void handle_AgentResume_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentResume, AgentData, ad, msg);
  if(ad == NULL ||  VALIDATE_SESSION(ad)) 
    return;
  if(ad->SerialNum < lctx->pause_serial) {
    printf("WARNING: ignoring AgentResume with outdated serial\n");
    return;
  }

  printf("\nDEBUG: resuming agent %s\n\n", user_get_name(lctx->u));
  lctx->pause_serial = ad->SerialNum;
  user_set_unpaused(lctx->u);
}

static void send_agent_data_update(struct omuser_ctx* lctx) {
  user_ctx* ctx = lctx->u;
  SL_DECLMSG(AgentDataUpdate,upd);
  SL_DECLBLK(AgentDataUpdate, AgentData, ad, &upd);
  user_get_uuid(ctx, ad->AgentID);
  caj_string_set(&ad->FirstName, user_get_first_name(ctx));
  caj_string_set(&ad->LastName, user_get_last_name(ctx));
  caj_string_set(&ad->GroupTitle, user_get_group_title(ctx));
  user_get_active_group(ctx, ad->ActiveGroupID);
  ad->GroupPowers = 0; // FIXME - TODO
  caj_string_set(&ad->GroupName, user_get_group_name(ctx));
  upd.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &upd);
}

static void handle_AgentDataUpdateRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AgentDataUpdateRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  send_agent_data_update(lctx);
}

struct fetch_inv_desc_req {
  user_ctx *ctx;
  int fetch_folders, fetch_items;
};


// FIXME - should choose num of blocks per packet dynamically!
#define MAX_INV_PER_MSG 4

static void inventory_descendents_cb(struct inventory_contents* inv, void* priv) {
  fetch_inv_desc_req *req = (fetch_inv_desc_req*)priv;
  if(req->ctx != NULL) {
    omuser_ctx* lctx = (omuser_ctx*)user_get_priv(req->ctx);
    user_del_self_pointer(&req->ctx);

    if(inv == NULL) {
      delete req; return;
    }

    // FIXME - TODO send folders!

    if(req->fetch_items) {
      // FIXME - OpenSim always sends a null terminating entry, should we too?
      if(inv->num_items == 0) {
	SL_DECLMSG(InventoryDescendents, invdesc);
	SL_DECLBLK(InventoryDescendents, AgentData, ad, &invdesc);
	user_get_uuid(lctx->u, ad->AgentID);
	uuid_copy(ad->FolderID, inv->folder_id);
	user_get_uuid(lctx->u, ad->OwnerID); // FIXME - is this always so?
	ad->Version = 1; // FIXME FIXME FIXME!!!
	ad->Descendents = inv->num_items + inv->num_subfolder;
	
	SL_DECLBLK(InventoryDescendents, ItemData, idata, &invdesc);
	uuid_clear(idata->ItemID);
	uuid_clear(idata->FolderID);
	uuid_clear(idata->AssetID);
	uuid_clear(idata->OwnerID);
	idata->Type = -1;
	sl_send_udp(lctx, &invdesc); // FIXME - throttle this!
      }

      for(unsigned int i = 0; i < inv->num_items; /**/) {
	SL_DECLMSG(InventoryDescendents, invdesc);
	SL_DECLBLK(InventoryDescendents, AgentData, ad, &invdesc);
	user_get_uuid(lctx->u, ad->AgentID);
	uuid_copy(ad->FolderID, inv->folder_id);
	user_get_uuid(lctx->u, ad->OwnerID); // FIXME - is this always so?
	ad->Version = 1; // FIXME FIXME FIXME!!!
	ad->Descendents = inv->num_items + inv->num_subfolder;
     
	for(int j = 0; j < MAX_INV_PER_MSG && i < inv->num_items; i++, j++) {
	  SL_DECLBLK(InventoryDescendents, ItemData, idata, &invdesc);
	  uuid_copy(idata->ItemID, inv->items[i].item_id);
	  uuid_copy(idata->FolderID, inv->items[i].folder_id);
	  uuid_copy(idata->CreatorID, inv->items[i].creator_as_uuid);
	  uuid_copy(idata->OwnerID, inv->items[i].owner_id);
	  uuid_copy(idata->GroupID, inv->items[i].group_id);
	  idata->BaseMask = inv->items[i].perms.base;
	  idata->OwnerMask = inv->items[i].perms.current;
	  idata->GroupMask = inv->items[i].perms.group;
	  idata->EveryoneMask = inv->items[i].perms.everyone;
	  idata->NextOwnerMask = inv->items[i].perms.next;
	  idata->GroupOwned = inv->items[i].group_owned;
	  uuid_copy(idata->AssetID, inv->items[i].asset_id);
	  idata->Type = inv->items[i].asset_type;
	  idata->InvType = inv->items[i].inv_type;
	  idata->Flags = inv->items[i].flags;
	  idata->SaleType = inv->items[i].sale_type;
	  idata->SalePrice = inv->items[i].sale_price;
	  caj_string_set(&idata->Name, inv->items[i].name);
	  caj_string_set(&idata->Description, inv->items[i].description);
	  idata->CreationDate = inv->items[i].creation_date;
	  idata->CRC = caj_calc_inventory_crc(&inv->items[i]);
	}
	invdesc.flags |= MSG_RELIABLE;
	sl_send_udp(lctx, &invdesc); // FIXME - throttle this!
      }
    }
  }
  delete req;
}

static void handle_FetchInventoryDescendents_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(FetchInventoryDescendents, AgentData, ad, msg);
  SL_DECLBLK_GET1(FetchInventoryDescendents, InventoryData, inv, msg);
  if(ad == NULL || inv == NULL || VALIDATE_SESSION(ad)) 
    return;

  printf("DEBUG: Got FetchInventoryDescendents, sending to inventory server!\n");

  fetch_inv_desc_req *req = new fetch_inv_desc_req();
  req->ctx = lctx->u;  user_add_self_pointer(&req->ctx);
  req->fetch_folders = inv->FetchFolders;
  req->fetch_items = inv->FetchItems;
  
  user_fetch_inventory_folder(lctx->u, inv->FolderID, inv->OwnerID, 
			      inventory_descendents_cb, req);
}

static void inventory_item_cb(struct inventory_item* inv, void* priv) {
  user_ctx **pctx = (user_ctx**)priv;
  if(*pctx != NULL) {
    omuser_ctx* lctx = (omuser_ctx*)user_get_priv(*pctx);
    user_del_self_pointer(pctx);

    if(inv != NULL) {
      SL_DECLMSG(FetchInventoryReply, invresp);
      SL_DECLBLK(FetchInventoryReply, AgentData, ad, &invresp);
      user_get_uuid(lctx->u, ad->AgentID);
      
      SL_DECLBLK(FetchInventoryReply, InventoryData, idata, &invresp);
      uuid_copy(idata->ItemID, inv->item_id);
      uuid_copy(idata->FolderID, inv->folder_id);
      uuid_copy(idata->CreatorID, inv->creator_as_uuid);
      uuid_copy(idata->OwnerID, inv->owner_id);
      uuid_copy(idata->GroupID, inv->group_id);
      idata->BaseMask = inv->perms.base;
      idata->OwnerMask = inv->perms.current;
      idata->GroupMask = inv->perms.group;
      idata->EveryoneMask = inv->perms.everyone;
      idata->NextOwnerMask = inv->perms.next;
      idata->GroupOwned = inv->group_owned;
      uuid_copy(idata->AssetID, inv->asset_id);
      idata->Type = inv->asset_type;
      idata->InvType = inv->inv_type;
      idata->Flags = inv->flags;
      idata->SaleType = inv->sale_type;
      idata->SalePrice = inv->sale_price;
      caj_string_set(&idata->Name, inv->name);
      caj_string_set(&idata->Description, inv->description);
      idata->CreationDate = inv->creation_date;
      idata->CRC = caj_calc_inventory_crc(inv);
      invresp.flags |= MSG_RELIABLE;
      sl_send_udp(lctx, &invresp); // FIXME - throttle this!
    }
  }
  delete pctx;
}

static void handle_FetchInventory_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(FetchInventory, AgentData, ad, msg);
  ;
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  
  int count = SL_GETBLK(FetchInventory, InventoryData, msg).count;

  for(int i = 0; i < count; i++) {
     SL_DECLBLK_ONLY(FetchInventory, InventoryData, inv) =
      SL_GETBLKI(FetchInventory, InventoryData, msg, i);

     char buf[40]; uuid_unparse(inv->ItemID, buf);
     printf("DEBUG: Got FetchInventory for %s, sending to inventory server!\n",
	    buf);
  
    user_ctx **pctx = new user_ctx*();
    *pctx = lctx->u;  user_add_self_pointer(pctx);
    
    user_fetch_inventory_item(lctx->u, inv->ItemID, inv->OwnerID,
			      inventory_item_cb, pctx);
  }
}

// for debugging purposes ontly
static void print_uuid_with_msg(const char* msg, uuid_t u) {
  char uuid_str[40];
  uuid_unparse(u,uuid_str);
  printf("%s: %s\n",msg, uuid_str);
}


struct asset_request {
  user_ctx *ctx;
  omuser_ctx* lctx;
  uuid_t asset_id, transfer_id;
  int32_t channel_type;
  int is_direct;
  caj_string params; // FIXME - not right. For a start, don't want to send SessionID
};

// Note: as well as being a callback, this is called directly in one situation
static void do_send_asset_cb(struct simgroup_ctx *sgrp, void *priv,
			     struct simple_asset *asset) {
  asset_request *req = (asset_request*)priv;
  if(req->ctx != NULL) {
    if(asset == NULL) {
      printf("ERROR: couldn't get asset\n"); // FIXME - how to handle
    } else if(req->is_direct && !user_can_access_asset_direct(req->ctx, asset)) {
      printf("ERROR: not permitted to request this asset direct\n");
    } else {
      {
	SL_DECLMSG(TransferInfo, transinfo);
	SL_DECLBLK(TransferInfo, TransferInfo, tinfo, &transinfo);
	uuid_copy(tinfo->TransferID, req->transfer_id);
	tinfo->ChannelType = 2; tinfo->Status = 0;
	tinfo->TargetType = 0; // think this is the only possible type...
	tinfo->Size = asset->data.len;
	caj_string_steal(&tinfo->Params, &req->params);
	sl_send_udp(req->lctx, &transinfo);
      }

      int offset = 0, packet_no = 0, last = 0;
      while(!last) {
	int len = asset->data.len - offset;
	if(len > 1000) len = 1000;
	last = (offset + len >= asset->data.len);
	SL_DECLMSG(TransferPacket, trans);
	SL_DECLBLK(TransferPacket, TransferData, tdata, &trans);
	uuid_copy(tdata->TransferID, req->transfer_id);
	tdata->ChannelType = 2; 
	tdata->Status = last ? 1 : 0;
	tdata->Packet = packet_no++;
	caj_string_set_bin(&tdata->Data, asset->data.data+offset, len);
	sl_send_udp(req->lctx, &trans); // FIXME - this *really* needs throttling
	offset += len;
      }
    }

    user_del_self_pointer(&req->ctx);
  }
  caj_string_free(&req->params);
  delete req;
}

static void do_send_asset(asset_request *req) {
  caj_get_asset(user_get_sgrp(req->lctx->u), req->asset_id, 
		do_send_asset_cb, req);
}

static void handle_TransferRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(TransferRequest, TransferInfo, tinfo, msg);
  if(tinfo == NULL) return;

  if(tinfo->ChannelType != 2) {
    // shouldn't ever happen, I think
    user_send_message(lctx->u, "FIXME: TransferRequest with unexpected ChannelType");
    return;
  }
  // FIXME - we want to do something with tinfo->Priority

  uuid_t asset_id;
  if(tinfo->SourceType == 2) {
    if(tinfo->Params.len < 16) {
      user_send_message(lctx->u, "ERROR: TransferRequest with too-short params");
      printf("ERROR: TransferRequest with too-short params\n");
      return;
    }
    
    asset_request *req = new asset_request();
    req->ctx = lctx->u; user_add_self_pointer(&req->ctx);
    req->lctx = lctx; req->is_direct = 1;
    memcpy(req->asset_id, tinfo->Params.data+0, 16);
    print_uuid_with_msg("DEBUG: AssetID",req->asset_id);
    
    uuid_copy(req->transfer_id, tinfo->TransferID);
    req->channel_type = tinfo->ChannelType;
    caj_string_copy(&req->params, &tinfo->Params);
    
    do_send_asset(req);
    
  } else if(tinfo->SourceType == 3) {
    if(tinfo->Params.len < 100) {
      user_send_message(lctx->u, "ERROR: TransferRequest with too-short params");
      printf("ERROR: TransferRequest with too-short params\n");
      return;
    }

    uuid_t agent_id, session_id, owner_id, task_id, item_id;
    memcpy(agent_id, tinfo->Params.data+0, 16);
    memcpy(session_id, tinfo->Params.data+16, 16);
    memcpy(owner_id, tinfo->Params.data+32, 16);
    memcpy(task_id, tinfo->Params.data+48, 16);
    memcpy(item_id, tinfo->Params.data+64, 16);
    memcpy(asset_id, tinfo->Params.data+80, 16);
    // last 4 bytes are asset type
    
    print_uuid_with_msg("DEBUG: AgentID",agent_id);
    print_uuid_with_msg("DEBUG: SessionID",session_id);
    print_uuid_with_msg("DEBUG: OwnerID",owner_id);
    print_uuid_with_msg("DEBUG: TaskID",task_id);
    print_uuid_with_msg("DEBUG: ItemID",item_id);
    print_uuid_with_msg("DEBUG: AssetID",asset_id);
    
    if(VALIDATE_SESSION_2(agent_id,session_id)) {
      printf("ERROR: session validation failure for TransferRequest\n"); return;
    }
    
    if(uuid_is_null(task_id)) {
      printf("FIXME: TransferRequest for item in user's inventory\n");
      // non-prim inventory items - TODO
    } else {
      struct world_obj* obj = world_object_by_id(user_get_sim(lctx->u), task_id);
      if(obj == NULL) {
	printf("ERROR: wanted inventory item from non-existent object\n");
	return; 
      } else if(obj->type != OBJ_TYPE_PRIM) {
	printf("ERROR: wanted inventory item from non-prim object\n");
	return;
      }
      struct primitive_obj* prim = (primitive_obj*)obj;

      inventory_item *inv = NULL;
      for(unsigned i = 0; i < prim->inv.num_items; i++) {
	if(uuid_compare(prim->inv.items[i]->item_id, item_id) == 0) {
	  printf("DEBUG: found prim inventory item\n");
	  inv = prim->inv.items[i];
	}
      }
      
      if(inv == NULL) {
	printf("ERROR: TransferRequest for non-existent prim inv item\n"); return; 
      }
      if(uuid_compare(inv->asset_id, asset_id)) {
	printf("ERROR: TransferRequest for prim inv item has mismatching asset_id\n"); 
	return; 
      }
      if(!user_can_access_asset_task_inv(lctx->u, prim, inv)) {
	// FIXME - send the right sort of error
	printf("ERROR: insufficient perms accessing asset from prim inventory.\n");
	return;
      }
      
      asset_request *req = new asset_request();
      req->ctx = lctx->u; user_add_self_pointer(&req->ctx);
      req->lctx = lctx; req->is_direct = 0;
      uuid_copy(req->asset_id, asset_id);
      uuid_copy(req->transfer_id, tinfo->TransferID);
      req->channel_type = tinfo->ChannelType;
      caj_string_copy(&req->params, &tinfo->Params);
    
      if(inv->asset_hack != NULL) {
	// HACK - FIXME!
	do_send_asset_cb(user_get_sgrp(lctx->u), req, inv->asset_hack);
      } else {
	do_send_asset(req);
      }
    }
  } else {
    // I suspect this could happen, though.
    user_send_message(lctx->u, "FIXME: TransferRequest with unexpected SourceType");
    return;
  }

}

static void handle_RegionHandshakeReply_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RegionHandshakeReply, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  user_set_flag(lctx->u, AGENT_FLAG_RHR | AGENT_FLAG_NEED_OTHER_AVS);
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
  if(ad == NULL || ad->CircuitCode != user_get_circuit_code(ctx) || VALIDATE_SESSION(ad)) 
    return;
  if(user_complete_movement(ctx)) {
    printf("Got CompleteAgentMovement; sending AgentMovementComplete\n");
    SL_DECLMSG(AgentMovementComplete, amc);
    amc.flags |= MSG_RELIABLE;
    SL_DECLBLK(AgentMovementComplete, AgentData, ad2, &amc);
    SL_DECLBLK(AgentMovementComplete, Data, dat, &amc);
    SL_DECLBLK(AgentMovementComplete, SimData, simdat, &amc);
    user_get_uuid(ctx, ad2->AgentID);
    user_get_session_id(ctx, ad2->SessionID);
    user_get_position(ctx, &dat->Position);
    user_get_initial_look_at(ctx, &dat->LookAt);
    dat->RegionHandle = sim_get_region_handle(lctx->lsim->sim);
    dat->Timestamp = time(NULL);
    caj_string_set(&simdat->ChannelVersion, caj_version_name_str);
    sl_send_udp(lctx, &amc);
  }
}

static void handle_LogoutRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(LogoutRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) 
    return;
  user_ctx *ctx = lctx->u;
  // FIXME - hacky!
  user_set_flag(ctx, AGENT_FLAG_IN_LOGOUT);
  SL_DECLMSG(LogoutReply, reply);
  SL_DECLBLK(LogoutReply, AgentData, ad2, &reply);
  user_get_uuid(ctx, ad2->AgentID);
  user_get_session_id(ctx, ad2->SessionID);
  sl_send_udp(lctx, &reply);
  user_session_close(ctx, false);
}

static void teleport_begin(struct user_ctx* ctx, struct teleport_desc *tp) {
  omuser_ctx* lctx = (omuser_ctx*)user_get_priv(ctx);
  SL_DECLMSG(TeleportStart, start);
  SL_DECLBLK(TeleportStart, Info, info, &start);
  info->TeleportFlags = tp->flags;
  start.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &start);
}


static void teleport_failed(struct user_ctx* ctx, const char* reason) {
  omuser_ctx* lctx = (omuser_ctx*)user_get_priv(ctx);
  SL_DECLMSG(TeleportFailed, fail);
  SL_DECLBLK(TeleportFailed, Info, info, &fail);
  user_get_uuid(ctx, info->AgentID);
  caj_string_set(&info->Reason, reason);
  fail.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &fail);
}

static void teleport_progress(struct user_ctx* ctx, const char* msg, uint32_t flags) {
  omuser_ctx* lctx = (omuser_ctx*)user_get_priv(ctx);
  SL_DECLMSG(TeleportProgress, prog);
  SL_DECLBLK(TeleportProgress, AgentData, ad, &prog);
  user_get_uuid(ctx, ad->AgentID);
   SL_DECLBLK(TeleportProgress, Info, info, &prog);
  caj_string_set(&info->Message, msg);
  info->TeleportFlags = flags;
  // prog.flags |= MSG_RELIABLE; // not really needed, I think. FIXME?
  sl_send_udp(lctx, &prog);
}


static void teleport_complete(struct user_ctx* ctx, struct teleport_desc *tp) {
  //omuser_ctx* lctx = (omuser_ctx*)user_get_priv(ctx);
  caj_llsd *msg = llsd_new_map();
  caj_llsd *info = llsd_new_map();
  uuid_t u;
  
  user_get_uuid(ctx, u);
  llsd_map_append(info, "AgentID", llsd_new_uuid(u));
  llsd_map_append(info, "LocationID", llsd_new_int(4)); // ???!! FIXME ???
  llsd_map_append(info, "SimIP", llsd_new_binary(&tp->sim_ip,4)); // big-endian?
  llsd_map_append(info, "SimPort", llsd_new_int(tp->sim_port));
  llsd_map_append(info, "RegionHandle", llsd_new_from_u64(tp->region_handle));
  llsd_map_append(info, "SeedCapability", llsd_new_string(tp->seed_cap));
  llsd_map_append(info, "SimAccess", llsd_new_int(13)); // ????!! FIXME!
  llsd_map_append(info, "TeleportFlags", llsd_new_from_u32(tp->flags));

  caj_llsd *array = llsd_new_array();
  llsd_array_append(array, info);
  llsd_map_append(msg, "Info", array);

  user_event_queue_send(ctx,"TeleportFinish",msg);
}

static void teleport_local(struct user_ctx* ctx, struct teleport_desc *tp) {
  omuser_ctx* lctx = (omuser_ctx*)user_get_priv(ctx);
  SL_DECLMSG(TeleportLocal, msg);
  SL_DECLBLK(TeleportLocal, Info, info, &msg);
  user_get_uuid(ctx, info->AgentID);
  info->LocationID = 0; // FIXME!!!!!
  info->Position = tp->pos;
  info->LookAt = tp->look_at;
  info->TeleportFlags = tp->flags;
  sl_send_udp(lctx, &msg);
}

// FIXME - handle TeleportRequest message (by UUID)?

static void handle_TeleportLocationRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(TeleportLocationRequest, AgentData, ad, msg);
  SL_DECLBLK_GET1(TeleportLocationRequest, Info, info, msg);
  if(ad == NULL || info == NULL || VALIDATE_SESSION(ad)) 
    return;
  user_teleport_location(lctx->u, info->RegionHandle, &info->Position, 
			 &info->LookAt, TRUE);
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
     ad->CircuitCode != user_get_circuit_code(lctx->u)) return;

  // FIXME - need to check generation counter

  user_set_throttles_block(lctx->u, throt->Throttles.data,
			   throt->Throttles.len);
}


static unsigned char* build_dummy_texture_entry(uuid_t texture, int *len) {
  unsigned char* data = (unsigned char*)malloc(46);
  float repeat_uv = 1.0f;
  *len = 46;
  memcpy(data,texture,16);
  data[16] = 0;
  memset(data+17,0,4); // colour
  data[21] = 0;
  caj_float_to_bin_le(data+22,repeat_uv); // repeat U.
  data[26] = 0;
  caj_float_to_bin_le(data+27,repeat_uv); // repeat V.
  data[31] = 0;
  memset(data+32,0,2); // offset U
  data[34] = 0;
  memset(data+35,0,2); // offset V
  data[37] = 0;
  memset(data+38,0,2); // rotation
  data[39] = 0;
  data[40] = 0; // material
  data[41] = 0;
  data[42] = 0; // media
  data[43] = 0;
  data[44] = 0; // glow
  data[45] = 0; // FIXME - do we need this last terminating 0?
  return data;
}

static void handle_ObjectAdd_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectAdd, AgentData, ad, msg);
  SL_DECLBLK_GET1(ObjectAdd, ObjectData, objd, msg);
  if(ad == NULL || objd == NULL || VALIDATE_SESSION(ad)) return;
  
  if(objd->PCode != PCODE_PRIM) {
    // FIXME - support this!
    user_send_alert_message(lctx->u,"Sorry, creating non-prim objects not supported.", FALSE);
    return;
  }

  primitive_obj* prim = world_begin_new_prim(user_get_sim(lctx->u));
  user_get_uuid(lctx->u, prim->owner);
  uuid_copy(prim->creator, prim->owner);
  // FIXME - set group of object

  prim->ob.local_pos = objd->RayEnd; // FIXME - do proper raycast

  prim->material = objd->Material;
  // FIXME - handle AddFlags
  prim->path_curve = objd->PathCurve;
  prim->profile_curve = objd->ProfileCurve;
  prim->path_begin = objd->PathBegin;
  prim->path_end = objd->PathEnd;
  prim->path_scale_x = objd->PathScaleX;
  prim->path_scale_y = objd->PathScaleY;
  prim->path_shear_x = objd->PathShearX;
  prim->path_shear_y = objd->PathShearY;
  prim->path_twist = objd->PathTwist;
  prim->path_twist_begin = objd->PathTwistBegin;
  prim->path_radius_offset = objd->PathRadiusOffset;
  prim->path_taper_x = objd->PathTaperX;
  prim->path_taper_y = objd->PathTaperY;
  prim->path_revolutions = objd->PathRevolutions;
  prim->path_skew = objd->PathSkew;
  prim->profile_begin = objd->ProfileBegin;
  prim->profile_end = objd->ProfileEnd;
  prim->profile_hollow = objd->ProfileHollow;
  
  prim->ob.scale = objd->Scale;
  prim->ob.rot = objd->Rotation;

  // FIXME - handle objd->State somehow

  uuid_t tex; 
  assert(uuid_parse("89556747-24cb-43ed-920b-47caed15465f",tex) == 0);
  prim->tex_entry.data = build_dummy_texture_entry(tex, &prim->tex_entry.len);
  

  // FIXME - TODO

  world_insert_obj(user_get_sim(lctx->u), &prim->ob);
}

static world_obj* get_obj_for_update(struct omuser_ctx* lctx, uint32_t localid) {
  struct world_obj* obj = world_object_by_localid(user_get_sim(lctx->u), localid);
  if(obj == NULL) {
    printf("ERROR: attempt to modify non-existent object\n");
    return NULL;
  } else if(!user_can_modify_object(lctx->u, obj)) {
    printf("ERROR: attempt to modify non-permitted object\n");
    return NULL; // FIXME - send update to originating client.
  }

  return obj;
}

static primitive_obj* get_prim_for_update(struct omuser_ctx* lctx, uint32_t localid) {
  struct world_obj* obj = get_obj_for_update(lctx, localid);
  if(obj == NULL) return NULL;

  if(obj->type != OBJ_TYPE_PRIM) {
      printf("ERROR: attempt to modify non-prim object\n");
      return NULL;
  }
  return (primitive_obj*)obj;
}

#define MULTI_UPDATE_POS 1
#define MULTI_UPDATE_ROT 2
#define MULTI_UPDATE_SCALE 4
#define MULTI_UPDATE_LINKSET 8 
#define MULTI_UPDATE_16 16 

static void handle_MultipleObjectUpdate_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(MultipleObjectUpdate, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  int count = SL_GETBLK(MultipleObjectUpdate, ObjectData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(MultipleObjectUpdate, ObjectData, objd) =
      SL_GETBLKI(MultipleObjectUpdate, ObjectData, msg, i);

    struct world_obj* obj = get_obj_for_update(lctx, 
					       objd->ObjectLocalID);
    if(obj == NULL)
      continue;

    if(objd->Type & 0xe0) {
      printf("ERROR: MultipleObjectUpdate with unrecognised flags %i\n",
	     (int)objd->Type);
      continue;
    } 

    unsigned char *dat = objd->Data.data;
    int len = objd->Data.len;
    struct caj_multi_upd upd;
    upd.flags = 0;

    if(objd->Type & MULTI_UPDATE_LINKSET) 
      upd.flags |= CAJ_MULTI_UPD_LINKSET;
    
    if(objd->Type & MULTI_UPDATE_16) {
      printf("FIXME: unhandled flag 16 in MultipleObjectUpdate\n");
    }

    // FIXME - linkset resizing totally broken!

    // FIXME - handle LINSKET flag!

    if(objd->Type & MULTI_UPDATE_POS) {
      if(len < 12) {
	printf("ERROR: MultipleObjectUpdate too short for pos\n"); 
	continue;
      }
      upd.flags |= CAJ_MULTI_UPD_POS;
      caj_bin_to_vect3_le(&upd.pos, dat);
      dat += 12; len -= 12;
    }
    if(objd->Type & MULTI_UPDATE_ROT) {
      if(len < 12) {
	printf("ERROR: MultipleObjectUpdate too short for rot\n"); 
	continue;
      }
      upd.flags |= CAJ_MULTI_UPD_ROT;
      caj_bin3_to_quat_le(&upd.rot, dat);
      dat += 12; len -= 12;
    }
    if(objd->Type & MULTI_UPDATE_SCALE) {
      if(len < 12) {
	printf("ERROR: MultipleObjectUpdate too short for scale\n"); 
	continue;
      }
      upd.flags |= CAJ_MULTI_UPD_SCALE;
      caj_bin_to_vect3_le(&upd.scale, dat);
      dat += 12; len -= 12;
    }

    // FIXME - TODO

    world_multi_update_obj(user_get_sim(lctx->u), obj, &upd);
  }
}

// TODO - ObjectRotation?

static void handle_ObjectFlagUpdate_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectFlagUpdate, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  
  struct primitive_obj* prim = get_prim_for_update(lctx, ad->ObjectLocalID);
  if(prim == NULL) return;
  
  // FIXME - difference between PRIM_FLAG_TEMP_ON_REZ and PRIM_FLAG_TEMPORARY?
  prim->flags &= ~(uint32_t)(PRIM_FLAG_PHANTOM|PRIM_FLAG_PHYSICAL|
			     PRIM_FLAG_TEMP_ON_REZ);
  if(ad->UsePhysics) prim->flags |= PRIM_FLAG_PHYSICAL;
  else if(ad->IsPhantom) prim->flags |= PRIM_FLAG_PHANTOM;
  if(ad->IsTemporary) prim->flags |= PRIM_FLAG_TEMP_ON_REZ; 
  /* if(ad->CastsShadows) ???; - FIXME */
  
  world_mark_object_updated(user_get_sim(lctx->u), &prim->ob, CAJ_OBJUPD_FLAGS);
}

// TODO - ObjectFlagUpdate

// TODO - ObjectClickAction?


static void handle_ObjectImage_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectImage, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  int count = SL_GETBLK(ObjectImage, ObjectData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectImage, ObjectData, objd) =
      SL_GETBLKI(ObjectImage, ObjectData, msg, i);

    struct primitive_obj* prim = get_prim_for_update(lctx, 
						     objd->ObjectLocalID);
    if(prim == NULL)
      continue;

    caj_string_free(&prim->tex_entry);
    caj_string_copy(&prim->tex_entry, &objd->TextureEntry);

    world_mark_object_updated(user_get_sim(lctx->u), &prim->ob, CAJ_OBJUPD_TEXTURE);
  }
}

static void handle_ObjectMaterial_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectMaterial, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  int count = SL_GETBLK(ObjectMaterial, ObjectData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectMaterial, ObjectData, objd) =
      SL_GETBLKI(ObjectMaterial, ObjectData, msg, i);

    struct primitive_obj* prim = get_prim_for_update(lctx, 
						     objd->ObjectLocalID);
    if(prim == NULL)
      continue;

    prim->material = objd->Material;

    // seems a tad wasteful...
    world_mark_object_updated(user_get_sim(lctx->u), &prim->ob, CAJ_OBJUPD_MATERIAL);
  }
}


static void handle_ObjectShape_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectShape, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  int count = SL_GETBLK(ObjectShape, ObjectData, msg).count;

  sl_dump_packet(msg); // DEBUG!

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectShape, ObjectData, objd) =
      SL_GETBLKI(ObjectShape, ObjectData, msg, i);

    struct primitive_obj* prim = get_prim_for_update(lctx, 
						     objd->ObjectLocalID);
    if(prim == NULL)
      continue;

    // FIXME - validate/constrain these!
    prim->path_curve = objd->PathCurve;
    prim->profile_curve = objd->ProfileCurve;
    prim->path_begin = objd->PathBegin;
    prim->path_end = objd->PathEnd;
    prim->path_scale_x = objd->PathScaleX;
    prim->path_scale_y = objd->PathScaleY;
    prim->path_shear_x = objd->PathShearX;
    prim->path_shear_y = objd->PathShearY;
    prim->path_twist = objd->PathTwist;
    prim->path_twist_begin = objd->PathTwistBegin;
    prim->path_radius_offset = objd->PathRadiusOffset;
    prim->path_taper_x = objd->PathTaperX;
    prim->path_taper_y = objd->PathTaperY;
    prim->path_revolutions = objd->PathRevolutions;
    prim->path_skew = objd->PathSkew;
    prim->profile_begin = objd->ProfileBegin;
    prim->profile_end = objd->ProfileEnd;
    prim->profile_hollow = objd->ProfileHollow;   

    world_mark_object_updated(user_get_sim(lctx->u), &prim->ob, CAJ_OBJUPD_SHAPE);
  }
}

static void handle_ObjectExtraParams_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectExtraParams, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  int count = SL_GETBLK(ObjectExtraParams, ObjectData, msg).count;

  sl_dump_packet(msg); // DEBUG!

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectExtraParams, ObjectData, objd) =
      SL_GETBLKI(ObjectExtraParams, ObjectData, msg, i);

    struct primitive_obj* prim = get_prim_for_update(lctx, 
						     objd->ObjectLocalID);
    if(prim == NULL)
      continue;

    if(objd->ParamInUse) {
      prim_set_extra_param(prim, objd->ParamType, &
objd->ParamData);
    } else {
      prim_delete_extra_param(prim, objd->ParamType);
    }

    // FIXME - add specific flag for sculpt updates!

    world_mark_object_updated(user_get_sim(lctx->u), &prim->ob, CAJ_OBJUPD_EXTRA_PARAMS);
  }
}

// TODO - ObjectGroup

// TODO - ObjectBuy

// TODO - BuyObjectInventory

// TODO - ObjectPermissions

// TODO - ObjectSaleInfo

static void handle_ObjectName_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectName, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  int count = SL_GETBLK(ObjectName, ObjectData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectName, ObjectData, objd) =
      SL_GETBLKI(ObjectName, ObjectData, msg, i);

    struct primitive_obj* prim = get_prim_for_update(lctx, 
						     objd->LocalID);
    if(prim == NULL)
      continue;

    free(prim->name);
    prim->name = strdup((char*)objd->Name.data);
  }
}


static void handle_ObjectDescription_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectDescription, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;
  int count = SL_GETBLK(ObjectDescription, ObjectData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectDescription, ObjectData, objd) =
      SL_GETBLKI(ObjectDescription, ObjectData, msg, i);

    struct primitive_obj* prim = get_prim_for_update(lctx, 
						     objd->LocalID);
    if(prim == NULL)
      continue;

    free(prim->description);
    prim->description = strdup((char*)objd->Description.data);
  }
}

static void handle_ObjectLink_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectLink, AgentData, ad, msg);
  int count = SL_GETBLK(ObjectLink, ObjectData, msg).count;
  if(ad == NULL || VALIDATE_SESSION(ad) || count < 1) return;

  struct primitive_obj *main;
  {
    SL_DECLBLK_ONLY(ObjectLink, ObjectData, objd) =
      SL_GETBLKI(ObjectLink, ObjectData, msg, 0);

    main = get_prim_for_update(lctx, objd->ObjectLocalID);
    if(main == NULL) {
      user_send_alert_message(lctx->u, "Tried to link invalid prim", FALSE); 
      return;
    }
  }

  for(int i = 1; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectLink, ObjectData, objd) =
      SL_GETBLKI(ObjectLink, ObjectData, msg, i);

    struct primitive_obj *child = get_prim_for_update(lctx, objd->ObjectLocalID);
    if(child == NULL) {
      user_send_alert_message(lctx->u, "Tried to link invalid child prim", FALSE);
      continue;
    }

    world_prim_link(user_get_sim(lctx->u), main, child);
  }
}

// TODO: ObjectCategory message

#define DEREZ_SAVE_TO_EXISTING 0 // save to existing inventory item
#define DEREZ_TAKE_COPY 1 
#define DEREZ_TAKE 4
#define DEREZ_GOD_TAKE_COPY 5
#define DEREZ_DELETE 6
#define DEREZ_RETURN 9

// FIXME - horribly incomplete
static void  handle_DeRezObject_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(DeRezObject, AgentData, ad, msg);
  SL_DECLBLK_GET1(DeRezObject, AgentBlock, ablk, msg);
  if(ad == NULL || ablk == NULL || VALIDATE_SESSION(ad)) return;
  int count = SL_GETBLK(DeRezObject, ObjectData, msg).count;

  if(ablk->Destination != DEREZ_DELETE) {
    printf("FIXME: can't handle DeRezObject unless it's a deletion\n");
    return;
  }

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(DeRezObject, ObjectData, objd) =
      SL_GETBLKI(DeRezObject, ObjectData, msg, i);

    struct world_obj* obj = world_object_by_localid(user_get_sim(lctx->u), 
						    objd->ObjectLocalID);
    if(obj == NULL) {
      printf("ERROR: attempt to delete non-existent object\n");
      continue;
    } else if(!user_can_delete_object(lctx->u, obj)) {
      printf("ERROR: attempt to delete non-permitted object\n");
      continue; // FIXME - send error to originating client.
    } else if(obj->type != OBJ_TYPE_PRIM) {
      printf("ERROR: attempt to delete non-prim object\n");
      continue;
    }

    // once we have trees, this will have to change
    struct primitive_obj* prim = (primitive_obj*)obj;

    world_delete_prim(user_get_sim(lctx->u), prim);
  }
}

static void handle_RequestObjectPropertiesFamily_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RequestObjectPropertiesFamily, AgentData, ad, msg);
  SL_DECLBLK_GET1(RequestObjectPropertiesFamily, ObjectData, objd, msg);
  if(ad == NULL || objd == NULL || VALIDATE_SESSION(ad)) return;

  struct world_obj* obj = world_object_by_id(user_get_sim(lctx->u), objd->ObjectID);
  if(obj == NULL) {
    printf("ERROR: RequestObjectPropertiesFamily for non-existent object\n");
    return;
  } else if(obj->type != OBJ_TYPE_PRIM) {
    printf("ERROR: RequestObjectPropertiesFamily for non-prim object\n");
    return;
  }
  struct primitive_obj* prim = (primitive_obj*)obj;

  SL_DECLMSG(ObjectPropertiesFamily, objprop);
  SL_DECLBLK(ObjectPropertiesFamily, ObjectData, propdat, &objprop);
  propdat->RequestFlags = objd->RequestFlags;
  uuid_copy(propdat->ObjectID, obj->id);
  uuid_copy(propdat->OwnerID, prim->owner);
  uuid_clear(propdat->GroupID); // FIXME - send group ID once we have it!
  propdat->BaseMask = prim->perms.base;
  propdat->OwnerMask = prim->perms.current;
  propdat->GroupMask = prim->perms.group;
  propdat->EveryoneMask = prim->perms.everyone;
  propdat->NextOwnerMask = prim->perms.next;
  propdat->OwnershipCost = 0; // ?? is this still used?
  propdat->SaleType = prim->sale_type;
  propdat->SalePrice = prim->sale_price;
  propdat->Category = 0; // FIXME - what is this?
  uuid_clear(propdat->LastOwnerID); // FIXME - what?
  caj_string_set(&propdat->Name, prim->name);
  caj_string_set(&propdat->Description, prim->description);
  objprop.flags |= MSG_RELIABLE; // ???
  sl_send_udp(lctx, &objprop);
}

// FIXME - (a) throttle responses, and (b) send multiple items per message
static void send_object_properties(struct omuser_ctx* lctx, uint32_t localid) {
  struct world_obj* obj = world_object_by_localid(user_get_sim(lctx->u), localid);
  if(obj == NULL) {
    printf("ERROR: wanted object properties for non-existent object\n");
    return;
  } else if(obj->type != OBJ_TYPE_PRIM) {
    printf("ERROR: wanted object properties for non-prim object\n");
    return;
  }
  struct primitive_obj* prim = (primitive_obj*)obj;

  SL_DECLMSG(ObjectProperties, objprop);
  SL_DECLBLK(ObjectProperties, ObjectData, propdat, &objprop);
  uuid_copy(propdat->ObjectID, obj->id);
  uuid_copy(propdat->OwnerID, prim->owner);
  uuid_copy(propdat->CreatorID, prim->creator);
  uuid_clear(propdat->GroupID); // FIXME - send group ID once we have it!
  propdat->CreationDate = 0; // FIXME!!!
  propdat->BaseMask = prim->perms.base;
  propdat->OwnerMask = prim->perms.current;
  propdat->GroupMask = prim->perms.group;
  propdat->EveryoneMask = prim->perms.everyone;
  propdat->NextOwnerMask = prim->perms.next;
  propdat->OwnershipCost = 0; // ?? is this still used?
  propdat->SaleType = prim->sale_type;
  propdat->SalePrice = prim->sale_price;
  propdat->AggregatePerms = 0; // FIXME FIXME FIXME
  propdat->AggregatePermTextures = 0; // FIXME FIXME FIXME
  propdat->AggregatePermTexturesOwner = 0; // FIXME FIXME FIXME
  propdat->Category = 0; // FIXME - what is this?
  uuid_clear(propdat->LastOwnerID); // FIXME - what?


  propdat->InventorySerial = prim->inv.serial;
  uuid_clear(propdat->ItemID); // FIXME - what exactly is this?
  uuid_clear(propdat->FolderID);
  uuid_clear(propdat->FromTaskID);

  caj_string_set(&propdat->Name, prim->name);
  caj_string_set(&propdat->Description, prim->description);
  caj_string_set(&propdat->TouchName, prim->touch_name);
  caj_string_set(&propdat->SitName, prim->sit_name);
  propdat->TextureID.len = 0; // FIXME - ?
  objprop.flags |= MSG_RELIABLE; // ???
  sl_send_udp(lctx, &objprop);
}

static void handle_ObjectSelect_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectSelect, AgentData, ad, msg);
  
  if(ad == NULL || VALIDATE_SESSION(ad)) return;

  int count = SL_GETBLK(ObjectSelect, ObjectData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectSelect, ObjectData, objd) =
      SL_GETBLKI(ObjectSelect, ObjectData, msg, i);
    send_object_properties(lctx, objd->ObjectLocalID);
    // FIXME - actually track object selection state!
  }  
}

static void task_inv_str(std::string &task_inv, const char* key, const char* val) {
  task_inv.append("\t\t"); task_inv.append(key);
  task_inv.append("\t"); task_inv.append(val); task_inv.append("\n");
}
static void task_inv_strlit(std::string &task_inv, const char* key, const char* val) {
  task_inv.append("\t\t"); task_inv.append(key);
  task_inv.append("\t"); task_inv.append(val); task_inv.append("|\n");
}
static void task_inv_uuid(std::string &task_inv, const char* key, const uuid_t u) {
  char buf[40]; uuid_unparse(u, buf);
  task_inv_str(task_inv, key, buf);
}

static void task_inv_hex(std::string &task_inv, const char* key, uint32_t v) {
  char buf[40]; sprintf(buf, "%lx",(long unsigned)v);
  task_inv_str(task_inv, key, buf);
}

#define NUM_LL_ASSET_TYPES 22
static const char* ll_asset_typenames[NUM_LL_ASSET_TYPES] = {
  "texture", "sound", "callcard", "landmark",
  "script", // allegedly, not used anymore
  "clothing", "object", "notecard", "category",
  "root", "lsltext", "lslbyte", "txtr_tga", "bodypart",
  "trash", "snapshot", "lstndfnd", "snd_wav", "img_tga",
  "jpeg", "animatn", "gesture",
};

static const char* ll_asset_type_name(int8_t asset_type) {
  if(asset_type < 0 || asset_type >= NUM_LL_ASSET_TYPES) return "?";
  return ll_asset_typenames[asset_type];
}

#define NUM_LL_INV_TYPES 21
static const char* ll_inv_typenames[NUM_LL_INV_TYPES] = {
  "texture", "sound", "callcard", "landmark", "?",
  "?", "object", "notecard", "category", "root",
  "script", "?", "?", "?", "?",
  "snapshot", "?", "attach", "wearable", "animation",
        "gesture", 
};

static const char* ll_inv_type_name(int8_t inv_type) {
  if(inv_type < 0 || inv_type >= NUM_LL_ASSET_TYPES) return "?";
  return ll_inv_typenames[inv_type];
}

// FIXME - need to actually do something with the available xfers
static void add_xfer_file(struct omuser_ctx* lctx, const char *fname, 
			  const char* data, int len) {
  std::map<std::string, om_xfer_file>::iterator iter = 
    lctx->xfer_files.find(fname);
  if(iter == lctx->xfer_files.end()) {
    om_xfer_file file; 
    caj_string_set_bin(&file.data, (const unsigned char*)data, len);
    lctx->xfer_files[fname] = file;
  } else {
    caj_string_free(&iter->second.data);
    caj_string_set_bin(&iter->second.data, (const unsigned char*)data, len);
  }
}

static void handle_RequestTaskInventory_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RequestTaskInventory, AgentData, ad, msg);
  SL_DECLBLK_GET1(RequestTaskInventory, InventoryData, idata, msg);
  if(ad == NULL || idata == NULL || VALIDATE_SESSION(ad))
    return;
  
  struct world_obj* obj = world_object_by_localid(user_get_sim(lctx->u), idata->LocalID);
  if(obj == NULL) {
    printf("ERROR: wanted inventory for non-existent object\n");
    return; // FIXME - think OpenSim sends same thing as for empty inv in this case
  } else if(obj->type != OBJ_TYPE_PRIM) {
    printf("ERROR: wanted inventory for non-prim object\n");
    return;
  }
  struct primitive_obj* prim = (primitive_obj*)obj;

  {
    SL_DECLMSG(ReplyTaskInventory, tinv);
    SL_DECLBLK(ReplyTaskInventory, InventoryData, idresp, &tinv);
    uuid_copy(idresp->TaskID, obj->id);
    if(prim->inv.num_items == 0) {
      idresp->Serial = 0; 
      idresp->Filename.len = 0;
    } else {
      uuid_t zero_id; uuid_clear(zero_id);
      std::string task_inv = "\tinv_object\t0\n\t{\n";
      task_inv_uuid(task_inv, "obj_id", prim->ob.id);
      task_inv_uuid(task_inv, "parent_id", zero_id);
      task_inv_str(task_inv, "type", "category");
      task_inv_strlit(task_inv, "name","Contents");
      task_inv.append("\t}\n");
      
      for(unsigned i = 0; i < prim->inv.num_items; i++) {
	inventory_item *item = prim->inv.items[i];
	task_inv.append("\tinv_item\t0\n\t{\n");
	task_inv_uuid(task_inv, "item_id", item->item_id);
	task_inv_uuid(task_inv, "parent_id", prim->ob.id);

	task_inv.append("\tpermissions 0\n\t{\n"); // yes, a space. Really.
	task_inv_hex(task_inv, "base_mask", prim->perms.base);
	task_inv_hex(task_inv, "owner_mask", prim->perms.current);
	task_inv_hex(task_inv, "group_mask", prim->perms.group);
	task_inv_hex(task_inv, "everyone_mask", prim->perms.everyone);
	task_inv_hex(task_inv, "next_owner_mask", prim->perms.next);
	
	task_inv_uuid(task_inv, "creator_id", item->creator_as_uuid);
	task_inv_uuid(task_inv, "owner_id", item->owner_id);
	task_inv_uuid(task_inv, "last_owner_id", zero_id); // FIXME!!!
	task_inv_uuid(task_inv, "group_id", zero_id); // FIXME!!!
	task_inv.append("\t}\n");
	
	task_inv_uuid(task_inv, "asset_id", item->asset_id);
	task_inv_str(task_inv, "type", ll_asset_type_name(item->asset_type));
	task_inv_str(task_inv, "inv_type", ll_inv_type_name(item->inv_type));
	
	task_inv.append("\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n"
			"\t\tsale_price\t0\n\t}\n");

	task_inv_strlit(task_inv, "name", item->name);
	task_inv_strlit(task_inv, "desc", item->description);
	task_inv_str(task_inv, "creation_date","0"); // FIXME!!
	task_inv.append("\t}\n");
      }

      char *fname = world_prim_upd_inv_filename(prim);

      add_xfer_file(lctx, fname, task_inv.c_str(), task_inv.length());

      idresp->Serial = prim->inv.serial; 
      caj_string_set(&idresp->Filename, fname);      
    }
    sl_send_udp(lctx, &tinv);
  }
}

static void handle_RemoveTaskInventory_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RemoveTaskInventory, AgentData, ad, msg);
  SL_DECLBLK_GET1(RemoveTaskInventory, InventoryData, invd, msg);
  if(ad == NULL || invd == NULL || VALIDATE_SESSION(ad))
    return;

  struct primitive_obj* prim = get_prim_for_update(lctx, 
						   invd->LocalID);
  if(prim == NULL)
    return;
  if(world_prim_delete_inv(user_get_sim(lctx->u), prim, invd->ItemID)) {
    // updates the inventory serial, hopefully forcing an update
    send_object_properties(lctx, invd->LocalID);
  } else {
    user_send_message(lctx->u, "ERROR: inventory deletion failed");
  }
}

static void handle_RezScript_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RezScript, AgentData, ad, msg);
  SL_DECLBLK_GET1(RezScript, UpdateBlock, upd, msg);
  SL_DECLBLK_GET1(RezScript, InventoryBlock, invd, msg);
  if(ad == NULL || upd == NULL || invd == NULL || VALIDATE_SESSION(ad))
    return;

  struct primitive_obj* prim = get_prim_for_update(lctx, 
						   upd->ObjectLocalID);
  if(prim == NULL)
    return;

  struct permission_flags perms;
  perms.base = invd->BaseMask;
  perms.current = invd->OwnerMask;
  perms.group = invd->GroupMask;
  perms.everyone = invd->EveryoneMask;
  perms.next = invd->NextOwnerMask;

  if(uuid_is_null(invd->ItemID)) {
    user_rez_script(lctx->u, prim, (char*)invd->Name.data, (char*)invd->Description.data,
		    invd->Flags, &perms);

    // updates the inventory serial
    send_object_properties(lctx, upd->ObjectLocalID);
  } else {
    printf("FIXME: need to handle RezScript from inventory\n");
    user_send_message(lctx->u, "FIXME: need to handle RezScript from inventory\n");
  }
  sl_dump_packet(msg);

}

static void handle_ObjectGrab_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectGrab, AgentData, ad, msg);
  SL_DECLBLK_GET1(ObjectGrab, ObjectData, objd, msg);
  SL_DECLBLK_GET1(ObjectGrab, SurfaceInfo, surf, msg); // optional
  if(ad == NULL || objd == NULL || VALIDATE_SESSION(ad))
    return;
  
  struct world_obj* obj = world_object_by_localid(user_get_sim(lctx->u), objd->LocalID);
  if(obj == NULL) {
    printf("ERROR: grabbed non-existent object\n");
    return;
  } else if(obj->type != OBJ_TYPE_PRIM) {
    printf("ERROR: grabbed non-prim object\n");
    return;
  }
  struct primitive_obj* prim = (primitive_obj*)obj;

  // FIXME - do something with GrabOffset?

  struct caj_touch_info tinfo; 
  struct caj_touch_info *tinfop;
  if(surf == NULL) {
    tinfop = NULL;
  } else {
    tinfo.uv = surf->UVCoord;
    tinfo.st = surf->STCoord;
    tinfo.face_index = surf->FaceIndex;
    tinfo.pos = surf->Position;
    tinfo.normal = surf->Normal;
    tinfo.binormal = surf->Binormal;
    tinfop = &tinfo;
  }

  user_prim_touch(lctx->u, prim, CAJ_TOUCH_START, tinfop);
}

static void handle_ObjectGrabUpdate_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectGrabUpdate, AgentData, ad, msg);
  SL_DECLBLK_GET1(ObjectGrabUpdate, ObjectData, objd, msg);
  SL_DECLBLK_GET1(ObjectGrabUpdate, SurfaceInfo, surf, msg); // optional
  if(ad == NULL || objd == NULL || VALIDATE_SESSION(ad))
    return;
  
  // for some reason, unlike ObjectGrab/ObjectDeGrab, this uses the full UUID
  // of the object. No idea why - especially as this is probably far *more* 
  // frequent than either of the other two. That's LL for you...
  struct world_obj* obj = world_object_by_id(user_get_sim(lctx->u), objd->ObjectID);
  if(obj == NULL || obj->type != OBJ_TYPE_PRIM) {
    printf("WARNING: bogus ObjectGrabUpdate\n");
    return;
  }
  struct primitive_obj* prim = (primitive_obj*)obj;

  // FIXME - do something with GrabOffset and SurfaceInfo!

  struct caj_touch_info tinfo; 
  struct caj_touch_info *tinfop;
  if(surf == NULL) {
    tinfop = NULL;
  } else {
    tinfo.uv = surf->UVCoord;
    tinfo.st = surf->STCoord;
    tinfo.face_index = surf->FaceIndex;
    tinfo.pos = surf->Position;
    tinfo.normal = surf->Normal;
    tinfo.binormal = surf->Binormal;
    tinfop = &tinfo;
  }

  user_prim_touch(lctx->u, prim, CAJ_TOUCH_CONT, tinfop);
}

static void handle_ObjectDeGrab_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectDeGrab, AgentData, ad, msg);
  SL_DECLBLK_GET1(ObjectDeGrab, ObjectData, objd, msg);
  SL_DECLBLK_GET1(ObjectDeGrab, SurfaceInfo, surf, msg); // optional
  if(ad == NULL || objd == NULL || VALIDATE_SESSION(ad))
    return;
  
  struct world_obj* obj = world_object_by_localid(user_get_sim(lctx->u), objd->LocalID);
  if(obj == NULL || obj->type != OBJ_TYPE_PRIM)  {
    printf("WARNING: bogus ObjectDeGrab\n");
    return;
  }
  struct primitive_obj* prim = (primitive_obj*)obj;

  struct caj_touch_info tinfo; 
  struct caj_touch_info *tinfop;
  if(surf == NULL) {
    tinfop = NULL;
  } else {
    tinfo.uv = surf->UVCoord;
    tinfo.st = surf->STCoord;
    tinfo.face_index = surf->FaceIndex;
    tinfo.pos = surf->Position;
    tinfo.normal = surf->Normal;
    tinfo.binormal = surf->Binormal;
    tinfop = &tinfo;
  }

  user_prim_touch(lctx->u, prim, CAJ_TOUCH_END, tinfop);
}

static void handle_ObjectDuplicate_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectDuplicate, AgentData, ad, msg);
  SL_DECLBLK_GET1(ObjectDuplicate, SharedData, sdat, msg);
  if(ad == NULL || sdat == NULL || VALIDATE_SESSION(ad))
    return;

  int count = SL_GETBLK(ObjectDuplicate, ObjectData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectDuplicate, ObjectData, objd) =
      SL_GETBLKI(ObjectDuplicate, ObjectData, msg, i);

    struct world_obj* obj = world_object_by_localid(user_get_sim(lctx->u), objd->ObjectLocalID);
    if(obj == NULL || obj->type != OBJ_TYPE_PRIM)  {
      printf("WARNING: bogus ObjectDuplicate\n");
      return;
    }
    struct primitive_obj* prim = (primitive_obj*)obj;
    
    // FIXME - do permissions popup.
    if(!user_can_copy_prim(lctx->u, prim)) continue;
    
    // FIXME - use the DuplicateFlags provided
    caj_vector3 newpos; 
    newpos.x = prim->ob.world_pos.x + sdat->Offset.x;
    newpos.y = prim->ob.world_pos.y + sdat->Offset.y;
    newpos.z = prim->ob.world_pos.z + sdat->Offset.z;
    user_duplicate_prim(lctx->u, prim, newpos);
  }  
}


static void handle_RezObject_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RezObject, AgentData, ad, msg);
  SL_DECLBLK_GET1(RezObject, RezData, rezd, msg);
  SL_DECLBLK_GET1(RezObject, InventoryData, invd, msg);
  if(ad == NULL || rezd == NULL || invd == NULL || VALIDATE_SESSION(ad))
    return;

  // FIXME - do something with GroupMask/EveryoneMask/NextOwnerMask
  // FIXME - do what with ItemFlags and RemoveItem?
  // FIXME - bunch of other stuff

  user_rez_object(lctx->u, rezd->FromTaskID, invd->ItemID, invd->OwnerID,
		  rezd->RayEnd);

  sl_dump_packet(msg);
}

static void handle_RezSingleAttachmentFromInv_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RezSingleAttachmentFromInv, AgentData, ad, msg);
  SL_DECLBLK_GET1(RezSingleAttachmentFromInv, ObjectData, objd, msg);
  if(ad == NULL || objd == NULL || VALIDATE_SESSION(ad))
    return;

  sl_dump_packet(msg);

  // FIXME - do something with GroupMask/EveryoneMask/NextOwnerMask
  // FIXME - do what with Name and Description?

  user_rez_attachment(lctx->u, objd->ItemID, objd->AttachmentPt);
}

static void handle_ObjectDetach_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ObjectDetach, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad))
    return;

  int count = SL_GETBLK(ObjectDetach, ObjectData, msg).count;

  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(ObjectDetach, ObjectData, objd) =
      SL_GETBLKI(ObjectDetach, ObjectData, msg, i);

    struct world_obj* obj = world_object_by_localid(user_get_sim(lctx->u), 
						    objd->ObjectLocalID);
    if(obj == NULL || obj->type != OBJ_TYPE_PRIM)  {
      printf("WARNING: bogus ObjectDetach\n");
      return;
    }
    user_remove_attachment(lctx->u, (primitive_obj*)obj);
  }
  
}

static void uuid_name_resp(uuid_t uuid, const char* first, const char* last,
			   void *priv) {
  user_ctx **pctx = (user_ctx**)priv;
  if(*pctx) {
    user_ctx *ctx = *pctx; omuser_ctx *lctx = (omuser_ctx*)user_get_priv(ctx);
    user_del_self_pointer(pctx);
    
    if(first != NULL && last != NULL) {
      printf("DEBUG: Sending UUIDNameReply for %s %s\n", first, last);
      SL_DECLMSG(UUIDNameReply, reply);
      SL_DECLBLK(UUIDNameReply, UUIDNameBlock, name, &reply);
      uuid_copy(name->ID, uuid);
      caj_string_set(&name->FirstName, first);
      caj_string_set(&name->LastName, last);
      reply.flags |= MSG_RELIABLE;
      sl_send_udp(lctx, &reply);
    } else {
      printf("DEBUG: UUID name request FAILED!\n");
    }
  }
  delete pctx;
}

static void handle_UUIDNameRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  
  int count = SL_GETBLK(UUIDNameRequest,UUIDNameBlock,msg).count;
  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(UUIDNameRequest,UUIDNameBlock,req) =
      SL_GETBLKI(UUIDNameRequest,UUIDNameBlock,msg,i);
    user_ctx **pctx = new user_ctx*(); *pctx = lctx->u;
    user_add_self_pointer(pctx);
    caj_uuid_to_name(user_get_sgrp(lctx->u), req->ID, uuid_name_resp, pctx);
  }
}

static void avatar_prop_resp(caj_user_profile* profile, void *priv) {
  user_ctx **pctx = (user_ctx**)priv;
  if(*pctx) {
    user_ctx *ctx = *pctx; omuser_ctx *lctx = (omuser_ctx*)user_get_priv(ctx);
    user_del_self_pointer(pctx);
    
    if(profile != NULL) {
      char buf[40]; struct tm tm; time_t birth_time;

      printf("DEBUG: Sending AvatarPropertiesReply for %s %s\n", 
	     profile->first, profile->last);
      SL_DECLMSG(AvatarPropertiesReply, reply);
      SL_DECLBLK(AvatarPropertiesReply, AgentData, ad, &reply);
      user_get_uuid(ctx, ad->AgentID);
      uuid_copy(ad->AvatarID, profile->uuid);

      SL_DECLBLK(AvatarPropertiesReply, PropertiesData, props, &reply);
      uuid_copy(props->ImageID, profile->profile_image);
      uuid_copy(props->FLImageID, profile->first_life_image);
      caj_string_set(&props->AboutText, profile->about_text);
      caj_string_set(&props->FLAboutText, profile->first_life_text);
      birth_time = profile->creation_time;
      gmtime_r(&birth_time, &tm); // FIXME - time zone?
      strftime(buf, 40, "%Y-%m-%d", &tm);
      caj_string_set(&props->BornOn, buf);
      caj_string_set(&props->ProfileURL, profile->web_url);
      caj_string_set(&props->CharterMember, ""); // FIXME!!!
      props->Flags = profile->user_flags;
      reply.flags |= MSG_RELIABLE;
      sl_send_udp(lctx, &reply);
    } else {
      printf("DEBUG: avatar profile request FAILED!\n");
    }
  }
  delete pctx;
  
}

static void handle_AvatarPropertiesRequest_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(AvatarPropertiesRequest, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;

   user_ctx **pctx = new user_ctx*(); *pctx = lctx->u;
   user_add_self_pointer(pctx);
   caj_get_user_profile(user_get_sgrp(lctx->u), ad->AvatarID, avatar_prop_resp, pctx);
}

static void handle_UpdateInventoryItem_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(UpdateInventoryItem, AgentData, ad, msg);
  if(ad == NULL || VALIDATE_SESSION(ad)) return;

  // FIXME - TODO!
  sl_dump_packet(msg);

  int count = SL_GETBLK(UpdateInventoryItem,InventoryData,msg).count;
  for(int i = 0; i < count; i++) {
    SL_DECLBLK_ONLY(UpdateInventoryItem,InventoryData,invd) =
      SL_GETBLKI(UpdateInventoryItem,InventoryData,msg,i);

    inventory_item inv;
    uuid_copy(inv.item_id, invd->ItemID);
    uuid_copy(inv.folder_id, invd->FolderID);
    // FIXME - do something with CallbackID
    // we don't bother using CreatorID/OwnerID - FIXME?
    uuid_copy(inv.group_id, invd->GroupID);
    inv.perms.base = invd->BaseMask;
    inv.perms.current = invd->OwnerMask;
    inv.perms.group = invd->GroupMask;
    inv.perms.everyone = invd->EveryoneMask;
    inv.perms.next = invd->NextOwnerMask;
    inv.group_owned = invd->GroupOwned;
    // we don't use Type/InvType either.
    inv.flags = invd->Flags;
    inv.sale_type = invd->SaleType;
    inv.sale_price = invd->SalePrice;
    inv.name = (char*)invd->Name.data;
    inv.description = (char*)invd->Description.data;
    // the creation date and CRC also aren't used.
  }
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
  uuid_t secure_session_id;
  asset_xfer *xfer = new asset_xfer();
  xfer->local = is_local;
  xfer->asset_type = asset_type;
  uuid_copy(xfer->transaction, transaction);
  user_get_secure_session_id(lctx->u, secure_session_id);
  helper_combine_uuids(xfer->asset_id, transaction,
		       secure_session_id);
  xfer->id = ++(lctx->lsim->xfer_id_ctr);
  xfer->ctr = 0;
  xfer->len = xfer->total_len = 0;
  xfer->data = NULL;
  lctx->xfers[xfer->id] = xfer;
  return xfer;
}

static void asset_upload_complete(omuser_ctx* lctx, uuid_t asset_id, 
				  int8_t asset_type, int success) {
  SL_DECLMSG(AssetUploadComplete, complete);
  SL_DECLBLK(AssetUploadComplete, AssetBlock, blk, &complete);
  blk->Type = asset_type;
  blk->Success = !!success;
  uuid_copy(blk->UUID, asset_id);
  complete.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &complete);  
}

struct asset_save_via_xfer_st {
  user_ctx *ctx;
  uuid_t asset_id;
  int8_t asset_type;

  asset_save_via_xfer_st(user_ctx *ctx, uuid_t asset_id, int8_t asset_type) : 
    ctx(ctx), asset_type(asset_type) {
    uuid_copy(this->asset_id, asset_id);
    user_add_self_pointer(&this->ctx);
  }

  ~asset_save_via_xfer_st() {
    if(ctx != NULL) user_del_self_pointer(&ctx);
  }
};

static void asset_save_cb(uuid_t asset_id, void *user_data) {
  bool success = true;
  asset_save_via_xfer_st *st = (asset_save_via_xfer_st*)user_data;

  // FIXME - handle this right, etc. Horribly broken.
  if(uuid_is_null(asset_id)) {
    printf("ERROR: asset uploaded failed\n");
    success = false;
  } else if(uuid_compare(asset_id, st->asset_id) != 0) {
    printf("ERROR: asset uploaded didn't give wanted asset_id!\n");
    success = false;
  }

  if(st->ctx) {
    omuser_ctx *lctx = (omuser_ctx*)user_get_priv(st->ctx);
    asset_upload_complete(lctx, st->asset_id, st->asset_type, success);
  }

  delete st;
}


static void complete_asset_upload(omuser_ctx* lctx, asset_xfer *xfer,
				  int success) {
  lctx->xfers.erase(xfer->id);
  
  if(success) {
    if(xfer->asset_type == ASSET_TEXTURE) {
      sim_add_local_texture(user_get_sim(lctx->u), xfer->asset_id, xfer->data,
			    xfer->len, true);
      asset_upload_complete(lctx, xfer->asset_id, xfer->asset_type, TRUE);
    } else {
      struct simple_asset asset;
      asset.data.data = xfer->data;
      asset.data.len = xfer->len;
      asset.type = xfer->asset_type;
      uuid_copy(asset.id, xfer->asset_id);
      asset.name = const_cast<char*>("no name"); // can't see *any* way to fill these out
      asset.description = const_cast<char*>("no description");

      asset_save_via_xfer_st *st = 
	new asset_save_via_xfer_st(lctx->u, xfer->asset_id, xfer->asset_type);
      caj_put_asset(user_get_sgrp(lctx->u), &asset, 
		    asset_save_cb, st);
      free(xfer->data);
    }
  } else {
    asset_upload_complete(lctx, xfer->asset_id, xfer->asset_type, FALSE);
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
  case ASSET_TEXTURE:
    if(!asset->StoreLocal) {
      user_send_message(lctx->u, "Sorry, AssetUploadRequest is now only for local texture uploads.\n");
      return;
    }
    printf("DEBUG: got local AssetUploadRequest for texture\n");
    break;
  case ASSET_SOUND:
  case ASSET_ANIMATION:
    user_send_message(lctx->u, "Sorry, AssetUploadRequest doesn't allow sound/animation uploads anymore.\n");
    return;
  case ASSET_BODY_PART:
  case ASSET_CLOTHING:
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

static void handle_CreateInventoryItem_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  asset_xfer *xfer;
  SL_DECLBLK_GET1(CreateInventoryItem,AgentData,ad,msg);
  SL_DECLBLK_GET1(CreateInventoryItem,InventoryBlock,invinfo,msg);
  if(ad == NULL || invinfo == NULL || VALIDATE_SESSION(ad)) return;

  // FIXME - TODO
  user_send_message(lctx->u, "FIXME: CreateInventoryItem not implemented.");
}

struct xfer_send {
  uint64_t xfer_id;
  uint32_t packet_no;
  caj_string data;
};

static void send_xfer_packet(struct omuser_ctx* lctx, xfer_send *send) {
  int offset = send->packet_no*1000;
  int len = send->data.len - offset, last = 1;
  if(len > 1000) { len = 1000; last = 0; }

  SL_DECLMSG(SendXferPacket, xferpkt);
  SL_DECLBLK(SendXferPacket, XferID, pktid, &xferpkt);
  SL_DECLBLK(SendXferPacket, DataPacket, datapkt, &xferpkt);
  pktid->ID = send->xfer_id;

  if(send->packet_no == 0) {
    unsigned char* buf = (unsigned char*)malloc(len+4);
    buf[0] = send->data.len & 0xff;
    buf[1] = (send->data.len >> 8) & 0xff;
    buf[2] = (send->data.len >> 16) & 0xff;
    buf[3] = (send->data.len >> 24) & 0xff;
    memcpy(buf+4, send->data.data, len);
    datapkt->Data.len = len+4; datapkt->Data.data = buf;
  } else {
    caj_string_set_bin(&datapkt->Data, send->data.data + offset, len);
  }
  pktid->Packet = send->packet_no | (last ? 0x80000000 : 0);
  sl_send_udp(lctx, &xferpkt);
}

// warning - this steals the SL string you give it!
static void send_by_xfer(struct omuser_ctx* lctx, uint64_t xfer_id, caj_string *data) {
  if(lctx->xfer_sends.count(xfer_id)) { 
    printf("ERROR: xfer request ID collision\n");
    caj_string_free(data); return;
  }

  xfer_send *send = new xfer_send();
  send->xfer_id = xfer_id;
  send->packet_no = 0; 
  caj_string_steal(&send->data, data);
  lctx->xfer_sends[xfer_id] = send;
  send_xfer_packet(lctx, send);
}

// doesn't remove from list of sends
static void free_xfer_send(struct omuser_ctx* lctx, xfer_send *send) {
  caj_string_free(&send->data); delete send;
}

static void handle_ConfirmXferPacket_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(ConfirmXferPacket, XferID, xfer_id, msg);
  if(xfer_id == NULL) return;
  std::map<uint64_t,xfer_send*>::iterator iter = lctx->xfer_sends.find(xfer_id->ID);
  if(iter == lctx->xfer_sends.end()) {
    printf("WARNING: ConfirmXferPacket for non-existent xfer\n");
  } else if(xfer_id->Packet != iter->second->packet_no) {
    printf("WARNING: ConfirmXferPacket with wrong xfer packet number: got %u, expect %u\n",
	   (unsigned)xfer_id->Packet, (unsigned)iter->second->packet_no);
  } else {
    xfer_send *send = iter->second;
    send->packet_no++;
    if(send->packet_no * 1000 >= send->data.len) {
      free_xfer_send(lctx, send); lctx->xfer_sends.erase(iter);
    } else {
      send_xfer_packet(lctx, send);
    }
  }
}

static void handle_RequestXfer_msg(struct omuser_ctx* lctx, struct sl_message* msg) {
  SL_DECLBLK_GET1(RequestXfer, XferID, xfer_id, msg);
  if(xfer_id == NULL) return;
  
  if(xfer_id->Filename.len != 0 && xfer_id->Filename.data[0] != 0) {
    std::map<std::string, om_xfer_file>::iterator iter = 
      lctx->xfer_files.find((char*)xfer_id->Filename.data);
    if(iter != lctx->xfer_files.end()) {
      send_by_xfer(lctx, xfer_id->ID, &iter->second.data);
      lctx->xfer_files.erase(iter);
    } else {
      printf("ERROR: RequestXfer for unknown file %s\n", (char*)xfer_id->Filename.data);
    }
  } else {
    printf("FIXME: non-file based RequestXfer not supported\n");
    // TODO - FIXME!
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
    if(user_get_flags(lctx->u) & (AGENT_FLAG_IN_SLOW_REMOVAL|AGENT_FLAG_PURGE))
      continue;

    std::map<obj_uuid_t,image_request*>::iterator req_iter =
      lctx->image_reqs.begin(); // FIXME - do by priority
    user_update_throttles(lctx->u);
    while(user_throttle_level(lctx->u, SL_THROTTLE_TEXTURE) > 0.0f) {
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
	texture_desc *texture = caj_get_texture(user_get_sgrp(lctx->u), ri->Image);
	image_request* ireq = new image_request;
	ireq->texture = texture;
	ireq->priority = ri->DownloadPriority;
	ireq->discard = ri->DiscardLevel;
	ireq->packet_no = ri->Packet;
	caj_request_texture(user_get_sgrp(lctx->u), texture);
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


// FIXME - incomplete
static void send_av_full_update(user_ctx* ctx, user_ctx* av_user) {
  omuser_ctx *lctx = (omuser_ctx*)user_get_priv(ctx);
  world_obj *av = user_get_avatar(av_user);
  char name[0x100]; unsigned char obj_data[76];
  SL_DECLMSG(ObjectUpdate,upd);
  SL_DECLBLK(ObjectUpdate,RegionData,rd,&upd);
  rd->RegionHandle = sim_get_region_handle(lctx->lsim->sim);
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation

  SL_DECLBLK(ObjectUpdate,ObjectData,objd,&upd);
  objd->ID = av->local_id;
  objd->State = 0;
  uuid_copy(objd->FullID, av->id);
  objd->PCode = PCODE_AV;
  objd->Material = MATERIAL_FLESH; // discrimination against robots?
  objd->Scale.x = 1.0f; objd->Scale.y = 1.0f; objd->Scale.z = 1.0f;

  caj_vector4 footfall; avatar_get_footfall(av, &footfall);
  caj_vect4_to_bin_le(obj_data, &footfall);
  caj_vect3_to_bin_le(obj_data+16, &av->local_pos);
  caj_vect3_to_bin_le(obj_data+16+12, &av->velocity);
  memset(obj_data+16+24, 0, 12); // accel
  caj_quat_to_bin3_le(obj_data+16+36, &av->rot);
  memset(obj_data+16+48, 0, 12); // not sure? rotational velocity?
  caj_string_set_bin(&objd->ObjectData, obj_data, 76);

  objd->ParentID = (av->parent == NULL ? 0 : av->parent->local_id);
  objd->UpdateFlags = PRIM_FLAG_PHYSICAL|PRIM_FLAG_ANY_OWNER|
    PRIM_FLAG_INVENTORY_EMPTY|PRIM_FLAG_CAST_SHADOWS;

  caj_string_copy(&objd->TextureEntry, user_get_texture_entry(ctx));
  objd->TextureAnim.len = 0;
  objd->Data.len = 0;
  objd->Text.len = 0;
  memset(objd->TextColor, 0, 4);
  objd->MediaURL.len = 0;
  objd->PSBlock.len = 0;
  objd->ExtraParams.len = 0;

  memset(objd->OwnerID,0,16);
  memset(objd->Sound,0,16);

  // not convinced this is really needed, but...
  objd->PathCurve = PATH_CURVE_STRAIGHT;
  objd->ProfileCurve = PROFILE_SHAPE_SQUARE;
  objd->PathScaleX = 100; objd->PathScaleY = 100;

  name[0] = 0;
  snprintf(name,0xff,"FirstName STRING RW SV %s\nLastName STRING RW SV %s\nTitle STRING RW SV %s",
	   user_get_first_name(av_user), user_get_last_name(av_user),
	   user_get_group_title(av_user));
  caj_string_set(&objd->NameValue,name);

  upd.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &upd);
}

static void sl_float_to_int16(unsigned char* out, float val, float range) {
  uint16_t ival = (uint16_t)((val+range)*32768/range);
  out[0] = ival & 0xff;
  out[1] = (ival >> 8) & 0xff;
}

static void send_av_terse_update(user_ctx* ctx, world_obj* av) {
  omuser_ctx *lctx = (omuser_ctx*)user_get_priv(ctx);
  unsigned char dat[0x3C];
  SL_DECLMSG(ImprovedTerseObjectUpdate,terse);
  SL_DECLBLK(ImprovedTerseObjectUpdate,RegionData,rd,&terse);
  rd->RegionHandle = sim_get_region_handle(lctx->lsim->sim);
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation
  SL_DECLBLK(ImprovedTerseObjectUpdate,ObjectData,objd,&terse);
  objd->TextureEntry.data = NULL;
  objd->TextureEntry.len = 0;

  dat[0] = av->local_id & 0xff;
  dat[1] = (av->local_id >> 8) & 0xff;
  dat[2] = (av->local_id >> 16) & 0xff;
  dat[3] = (av->local_id >> 24) & 0xff;
  dat[4] = 0; // state - ???
  dat[5] = 1; // object is an avatar
  
  caj_vector4 footfall; avatar_get_footfall(av, &footfall);
  caj_vect4_to_bin_le(dat+6,&footfall);
  caj_vect3_to_bin_le(dat+0x16, &av->local_pos);

  // Velocity
  sl_float_to_int16(dat+0x22, av->velocity.x, 128.0f);
  sl_float_to_int16(dat+0x24, av->velocity.y, 128.0f);
  sl_float_to_int16(dat+0x26, av->velocity.z, 128.0f);

  // Acceleration
  sl_float_to_int16(dat+0x28, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x2A, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x2C, 0.0f, 64.0f);
 
  // Rotation
  sl_float_to_int16(dat+0x2E, av->rot.x, 1.0f);
  sl_float_to_int16(dat+0x30, av->rot.y, 1.0f);
  sl_float_to_int16(dat+0x32, av->rot.z, 1.0f);
  sl_float_to_int16(dat+0x34, av->rot.w, 1.0f);

  // Rotational velocity
  sl_float_to_int16(dat+0x36, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x38, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x3A, 0.0f, 64.0f);

 
  caj_string_set_bin(&objd->Data, dat, 0x3C);
  terse.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &terse);
}

static void send_av_appearance(user_ctx* ctx, user_ctx* av_user) {
  omuser_ctx *lctx = (omuser_ctx*)user_get_priv(ctx);
  SL_DECLMSG(AvatarAppearance,aa);
  SL_DECLBLK(AvatarAppearance,Sender,sender,&aa);
  user_get_uuid(av_user, sender->ID);
  sender->IsTrial = 0;
  SL_DECLBLK(AvatarAppearance,ObjectData,objd,&aa);
  caj_string_copy(&objd->TextureEntry, user_get_texture_entry(av_user));

  // FIXME - this is horribly, horribly inefficient
  const caj_string *visual_params = user_get_visual_params(av_user);
  if(visual_params->data != NULL) {
      for(int i = 0; i < visual_params->len; i++) {
	SL_DECLBLK(AvatarAppearance,VisualParam,param,&aa);
	param->ParamValue = visual_params->data[i];
      }
  }
  
  aa.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &aa);
}

static void send_av_animations(user_ctx* ctx, user_ctx* av_user) {
  omuser_ctx *lctx = (omuser_ctx*)user_get_priv(ctx);
  SL_DECLMSG(AvatarAnimation,aa);
  SL_DECLBLK(AvatarAnimation,Sender,sender,&aa);
  user_get_uuid(av_user, sender->ID);
  
  {
    const animation_desc *default_anim = user_get_default_anim(av_user);
    SL_DECLBLK(AvatarAnimation,AnimationList,anim,&aa);
    uuid_copy(anim->AnimID, default_anim->anim);
    anim->AnimSequenceID = default_anim->sequence; // FIXME - ???
    SL_DECLBLK(AvatarAnimation,AnimationSourceList,source,&aa);
    uuid_copy(source->ObjectID, default_anim->obj); // FIXME!!!
  }
  
  // FIXME - copy non-default animations too
  
  // aa.flags |= MSG_RELIABLE; // FIXME - not reliable?
  sl_send_udp(lctx, &aa);
}

// --- START of hacky object update code pt 2. FIXME - remove this ---

static void obj_send_full_upd(omuser_ctx* lctx, world_obj* obj) {
  if(obj->type != OBJ_TYPE_PRIM) return;
  primitive_obj *prim = (primitive_obj*)obj;

  if(prim->attach_point >= FIRST_HUD_ATTACH_POINT &&
     !user_owns_prim(lctx->u, prim)) {
    printf("DEBUG: skipping %i, someone else's HUD attachment\n",
	   obj->local_id);
    return;
  }

  unsigned char obj_data[60];
  SL_DECLMSG(ObjectUpdate,upd);
  SL_DECLBLK(ObjectUpdate,RegionData,rd,&upd);
  rd->RegionHandle = sim_get_region_handle(lctx->lsim->sim);
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation

  SL_DECLBLK(ObjectUpdate,ObjectData,objd,&upd);
  objd->ID = prim->ob.local_id;
  if(prim->attach_point == 0) 
    objd->State = 0;
  else // welcome to the Second Life protocol(!)
    objd->State = ((prim->attach_point & 0xf) << 4) | ((prim->attach_point >> 4) & 0xf);

  uuid_copy(objd->FullID, prim->ob.id);
  objd->CRC = 0; // FIXME - need this for caching
  objd->PCode = PCODE_PRIM;
  objd->Material = prim->material;
  objd->ClickAction = 0; // FIXME.
  objd->Scale = prim->ob.scale;

  caj_vect3_to_bin_le(obj_data, &prim->ob.local_pos);
  memcpy(obj_data, &prim->ob.local_pos, 12); 
  memset(obj_data+12, 0, 12); // velocity - FIXME send this
  memset(obj_data+24, 0, 12); // accel
  caj_quat_to_bin3_le(obj_data+36, &prim->ob.rot);
  memset(obj_data+48, 0, 12);
  caj_string_set_bin(&objd->ObjectData, obj_data, 60);

  objd->ParentID = (prim->ob.parent == NULL ? 0 : prim->ob.parent->local_id);
  objd->UpdateFlags = PRIM_FLAG_ANY_OWNER | prim->flags | user_calc_prim_flags(lctx->u, prim);
  
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
  objd->PathRevolutions = prim->path_revolutions;
  objd->PathSkew = prim->path_skew;
  objd->ProfileBegin = prim->profile_begin;
  objd->ProfileEnd = prim->profile_end;
  objd->ProfileHollow = prim->profile_hollow;

  caj_string_copy(&objd->TextureEntry, &prim->tex_entry);
  objd->TextureAnim.len = 0;
  objd->Data.len = 0;
  caj_string_set(&objd->Text, prim->hover_text);
  memcpy(objd->TextColor, prim->text_color, 4);
  objd->MediaURL.len = 0;
  objd->PSBlock.len = 0;
  caj_string_copy(&objd->ExtraParams, &prim->extra_params);

  uuid_copy(objd->OwnerID, prim->owner);
  memset(objd->Sound,0,16);
  objd->Gain = 0.0f; objd->Flags = 0; objd->Radius = 0.0f; // sound-related

  if(prim->attach_point != 0 && user_owns_prim(lctx->u, prim)) { 
    char s[26+36+1];
    strcpy(s, "AttachItemID STRING RW SV ");
    uuid_unparse(prim->inv_item_id, s+26);
    caj_string_set(&objd->NameValue, s);
  } else objd->NameValue.len = 0;

  upd.flags |= MSG_RELIABLE;
  sl_send_udp_throt(lctx, &upd, SL_THROTTLE_TASK);
}

static void obj_send_terse_upd(omuser_ctx* lctx, world_obj* obj) {
  if(obj->type == OBJ_TYPE_PRIM) {
    primitive_obj* prim = (primitive_obj*)obj;
    if(prim->attach_point >= FIRST_HUD_ATTACH_POINT &&
       !user_owns_prim(lctx->u, prim)) {
      printf("DEBUG: skipping %i, someone else's HUD attachment\n",
	     obj->local_id);
      return;
    }
  }

  unsigned char dat[0x2C];
  SL_DECLMSG(ImprovedTerseObjectUpdate,terse);
  SL_DECLBLK(ImprovedTerseObjectUpdate,RegionData,rd,&terse);
  rd->RegionHandle = sim_get_region_handle(lctx->lsim->sim);
  rd->TimeDilation = 0xffff; // FIXME - report real time dilation
  SL_DECLBLK(ImprovedTerseObjectUpdate,ObjectData,objd,&terse);
  objd->TextureEntry.data = NULL;
  objd->TextureEntry.len = 0;

  dat[0] = obj->local_id & 0xff;
  dat[1] = (obj->local_id >> 8) & 0xff;
  dat[2] = (obj->local_id >> 16) & 0xff;
  dat[3] = (obj->local_id >> 24) & 0xff;
  dat[4] = 0; // state - ???
  dat[5] = 0; // object is not an avatar
  
  caj_vect3_to_bin_le(dat+0x6, &obj->local_pos);

  // Velocity
#if 0 // FIXME !!!
  sl_float_to_int16(dat+0x12, obj->velocity.x, 128.0f);
  sl_float_to_int16(dat+0x14, obj->velocity.y, 128.0f);
  sl_float_to_int16(dat+0x16, obj->velocity.z, 128.0f);
#else
  sl_float_to_int16(dat+0x12, 0.0f, 128.0f);
  sl_float_to_int16(dat+0x14, 0.0f, 128.0f);
  sl_float_to_int16(dat+0x16, 0.0f, 128.0f);
#endif

  // Acceleration
  sl_float_to_int16(dat+0x18, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x1A, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x1C, 0.0f, 64.0f);
 
  // Rotation
  sl_float_to_int16(dat+0x1E, obj->rot.x, 1.0f);
  sl_float_to_int16(dat+0x20, obj->rot.y, 1.0f);
  sl_float_to_int16(dat+0x22, obj->rot.z, 1.0f);
  sl_float_to_int16(dat+0x24, obj->rot.w, 1.0f);

  // Rotational velocity
  sl_float_to_int16(dat+0x26, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x28, 0.0f, 64.0f);
  sl_float_to_int16(dat+0x2A, 0.0f, 64.0f);
 
  caj_string_set_bin(&objd->Data, dat, 0x2C);
  terse.flags |= MSG_RELIABLE;
  sl_send_udp(lctx, &terse);
}


static gboolean obj_update_timer(gpointer data) {
  omuser_sim_ctx* lsim = (omuser_sim_ctx*)data;
  for(omuser_ctx* lctx = lsim->ctxts; lctx != NULL; lctx = lctx->next) {
    user_ctx *ctx = lctx->u;
    int user_flags = user_get_flags(ctx);
    if((user_flags & AGENT_FLAG_RHR) == 0 || (user_flags & AGENT_FLAG_PAUSED)) 
      continue;
    user_update_throttles(ctx);
    while(user_throttle_level(ctx, SL_THROTTLE_TASK) > 0.0f && 
	  user_has_pending_deleted_objs(ctx)) {
      SL_DECLMSG(KillObject, kill);
      for(int i = 0; i < 256 && user_has_pending_deleted_objs(ctx); i++) {
	SL_DECLBLK(KillObject, ObjectData, objd, &kill);
	objd->ID = user_get_next_deleted_obj(ctx);
      }
      sl_send_udp_throt(lctx, &kill, SL_THROTTLE_TASK);
    }
    while(user_throttle_level(ctx, SL_THROTTLE_TASK) > 0.0f) {
      uint32_t localid; int flags;
      if(!user_get_next_updated_obj(ctx, &localid, &flags))
	break;

      /* FIXME - we should support compressed updates too */
      if(flags & (CAJ_OBJUPD_CREATED|CAJ_OBJUPD_SCALE|CAJ_OBJUPD_SHAPE|
			 CAJ_OBJUPD_TEXTURE|CAJ_OBJUPD_FLAGS|CAJ_OBJUPD_MATERIAL|
			 CAJ_OBJUPD_TEXT|CAJ_OBJUPD_PARENT|
			 CAJ_OBJUPD_EXTRA_PARAMS)) {
	printf("DEBUG: sending full update for %u\n", localid);
	obj_send_full_upd(lctx, world_object_by_localid(lsim->sim, localid));
      } else if(flags & CAJ_OBJUPD_POSROT) {
	obj_send_terse_upd(lctx, world_object_by_localid(lsim->sim, localid));
      }
    }

    // FIXME - should probably move this elsewhere
    int i = 0;
    uint16_t *dirty_terrain = user_get_dirty_terrain_array(ctx);
    while(user_throttle_level(ctx, SL_THROTTLE_LAND) > 0.0f && 
	  i < 16) {
      if(dirty_terrain[i] == 0) { i++; continue; }
      int patch_cnt = 0; int patches[4]; uint16_t dirty = dirty_terrain[i];

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
      terrain_create_patches(sim_get_heightfield(lsim->sim), patches, 
			     patch_cnt, &ldat->Data);
      layer.flags |= MSG_RELIABLE;
      sl_send_udp_throt(lctx, &layer, SL_THROTTLE_LAND);
      dirty_terrain[i] = dirty;
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

  // FIXME - move to own func?
  for(std::map<std::string, om_xfer_file>::iterator iter = lctx->xfer_files.begin();
      iter != lctx->xfer_files.end(); iter++) {
    caj_string_free(&iter->second.data);
  }

  for(std::map<uint64_t,xfer_send*>::iterator iter = lctx->xfer_sends.begin();
      iter != lctx->xfer_sends.end(); iter++) {
    free_xfer_send(lctx, iter->second);
  }

  for(omuser_ctx** luser = &lctx->lsim->ctxts; *luser != NULL; ) {
    if(*luser == lctx) {
      *luser = lctx->next; break;
    } else luser = &(*luser)->next;
  }

  delete lctx;
}


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

      printf("DEBUG: handling UseCircuitCode on %s\n", sim_get_name(lsim->sim));
      // FIXME - need to handle dupes
      SL_DECLBLK_GET1(UseCircuitCode, CircuitCode, cc, &msg);
      struct user_ctx* ctx; struct sl_message ack;
      // FIXME - also need to clean this up to use new macros fully
      struct sl_blk_PacketAck_Packets *ackack;

      if(cc == NULL) goto out;
      // FIXME: handle same port/addr, different agent case
      ctx = sim_bind_user(lsim->sim, cc->ID, cc->SessionID,
			  cc->Code, lsim->hooks);
      if(ctx == NULL) {
	printf("DEBUG: got unexpected UseCircuitCode\n");
	sl_dump_packet(&msg);
	goto out;
      }
      if(user_get_priv(ctx) != NULL) {
	// FIXME - check port/addr matches, resend ack
	printf("DEBUG: got dupe UseCircuitCode?\n");
	goto out;
      }

      omuser_ctx *lctx = new omuser_ctx();
      lctx->u = ctx; lctx->lsim = lsim; 
      user_set_priv(ctx, lctx);
      lctx->addr = addr;
      lctx->sock = lsim->sock; lctx->counter = 0;
      lctx->appearance_serial = lctx->pause_serial = 0;
      uuid_clear(lctx->sit_info.target);

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
      caj_string_set(&ri->SimName, sim_get_name(lsim->sim));
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
      sim_get_region_uuid(lsim->sim, ri2->RegionID);
      ri3->CPUClassID = 1; ri3->CPURatio = 1;
      caj_string_set(&ri3->ColoName, "Nowhere");
      caj_string_set(&ri3->ProductName, "Cajeput Server Demo");
      caj_string_set(&ri3->ProductSKU, "0");
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

    return TRUE;
}

static void shutdown_handler(simulator_ctx *sim, void *priv) {
  printf("DEBUG: running caj_omv shutdown hook\n");
  omuser_sim_ctx *lsim = (omuser_sim_ctx*) priv;
  g_io_channel_shutdown(lsim->gio_sock, FALSE, NULL);
  g_io_channel_unref(lsim->gio_sock);
  delete lsim;
}


#define ADD_HANDLER(name) register_msg_handler(lsim, &sl_msgt_##name, handle_##name##_msg)

static void sim_int_init_udp(struct simulator_ctx *sim, void *priv)  {
  struct user_hooks *hooks = (user_hooks*)priv;
  omuser_sim_ctx *lsim = new omuser_sim_ctx();
  lsim->sim = sim; 
  lsim->hooks = hooks;
  lsim->ctxts = NULL;
  lsim->xfer_id_ctr = 1;
  int sock; struct sockaddr_in addr;

  sim_add_shutdown_hook(sim, shutdown_handler, lsim);

  ADD_HANDLER(AgentUpdate);
  ADD_HANDLER(SetAlwaysRun);
  ADD_HANDLER(AgentRequestSit);
  ADD_HANDLER(AgentSit);
  ADD_HANDLER(StartPingCheck);
  ADD_HANDLER(CompleteAgentMovement);
  ADD_HANDLER(EconomyDataRequest);
  ADD_HANDLER(MoneyBalanceRequest);
  ADD_HANDLER(LogoutRequest);
  ADD_HANDLER(ChatFromViewer);
  ADD_HANDLER(ScriptDialogReply);
  ADD_HANDLER(RequestGodlikePowers);
  ADD_HANDLER(AgentThrottle);
  ADD_HANDLER(RegionHandshakeReply);
  ADD_HANDLER(AgentWearablesRequest);
  ADD_HANDLER(AgentIsNowWearing);
  ADD_HANDLER(AssetUploadRequest);
  ADD_HANDLER(CreateInventoryItem);
  ADD_HANDLER(SendXferPacket);
  ADD_HANDLER(RequestXfer);
  ADD_HANDLER(ConfirmXferPacket);
  // FIXME - handle AbortXfer
  ADD_HANDLER(RequestImage);
  ADD_HANDLER(AgentDataUpdateRequest);
  ADD_HANDLER(AgentSetAppearance);
  ADD_HANDLER(AgentAnimation);
  ADD_HANDLER(PacketAck);
  ADD_HANDLER(MapLayerRequest);
  ADD_HANDLER(MapBlockRequest);
  ADD_HANDLER(MapNameRequest);
  ADD_HANDLER(TeleportLocationRequest);
  ADD_HANDLER(TeleportLandmarkRequest);
  ADD_HANDLER(FetchInventoryDescendents);
  ADD_HANDLER(ObjectAdd);
  ADD_HANDLER(RequestObjectPropertiesFamily);
  ADD_HANDLER(UUIDNameRequest);
  ADD_HANDLER(AvatarPropertiesRequest);
  ADD_HANDLER(ObjectSelect);
  ADD_HANDLER(MultipleObjectUpdate);
  ADD_HANDLER(ObjectImage);
  ADD_HANDLER(ObjectName);
  ADD_HANDLER(ObjectDescription);
  ADD_HANDLER(ObjectMaterial);
  ADD_HANDLER(ObjectShape);
  ADD_HANDLER(ObjectExtraParams);
  ADD_HANDLER(RezScript);
  ADD_HANDLER(RequestTaskInventory);
  ADD_HANDLER(RemoveTaskInventory);
  ADD_HANDLER(TransferRequest);
  ADD_HANDLER(ObjectFlagUpdate);
  ADD_HANDLER(DeRezObject);
  ADD_HANDLER(ViewerEffect);
  ADD_HANDLER(ObjectGrab);
  ADD_HANDLER(ObjectGrabUpdate);
  ADD_HANDLER(ObjectDeGrab);
  ADD_HANDLER(ObjectDuplicate);
  ADD_HANDLER(RezObject);
  ADD_HANDLER(FetchInventory);
  ADD_HANDLER(ObjectLink);
  ADD_HANDLER(RezSingleAttachmentFromInv);
  ADD_HANDLER(ObjectDetach);
  ADD_HANDLER(AgentPause);
  ADD_HANDLER(AgentResume);
  ADD_HANDLER(UpdateInventoryItem);

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  addr.sin_family= AF_INET;
  addr.sin_port = htons(sim_get_udp_port(sim));
  addr.sin_addr.s_addr=INADDR_ANY;
  bind(sock, (struct sockaddr*)&addr, sizeof(addr));
  lsim->gio_sock = g_io_channel_unix_new(sock);
  g_io_add_watch(lsim->gio_sock, G_IO_IN, got_packet, lsim);
  lsim->sock = sock;  

  g_timeout_add(100, texture_send_timer, lsim); // FIXME - check the timing on this
  g_timeout_add(100, obj_update_timer, lsim);  
  g_timeout_add(200, resend_timer, lsim);
}

int cajeput_plugin_init(int api_major, int api_minor,
			struct simgroup_ctx *sgrp) {
  if(api_major != CAJEPUT_API_VERSION_MAJOR || 
     api_minor < CAJEPUT_API_VERSION_MINOR) 
    return false;

  struct user_hooks *hooks = cajeput_alloc_user_hooks();
  
  hooks->teleport_begin = teleport_begin;
  hooks->teleport_failed = teleport_failed;
  hooks->teleport_progress = teleport_progress;
  hooks->teleport_complete = teleport_complete;
  hooks->teleport_local = teleport_local;
  hooks->remove = remove_user;
  hooks->disable_sim = disable_sim;
  hooks->chat_callback = chat_callback;
  hooks->alert_message = send_alert_message;
  hooks->send_av_full_update = send_av_full_update; 
  hooks->send_av_terse_update = send_av_terse_update;
  hooks->send_av_appearance = send_av_appearance; 
  hooks->send_av_animations = send_av_animations;
  hooks->script_dialog = send_script_dialog;
  hooks->wearables_changed = handle_wearables_changed;

  cajeput_add_sim_added_hook(sgrp, sim_int_init_udp, hooks);

  return true;
}

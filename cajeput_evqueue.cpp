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

#include "caj_types.h"
#include "caj_llsd.h"
#include "cajeput_core.h"
#include "cajeput_int.h"

static void event_queue_get_resp(SoupMessage *msg, user_ctx* ctx) {
    caj_llsd *resp = llsd_new_map();

    if(ctx->evqueue.last != NULL)
      llsd_free(ctx->evqueue.last);

    llsd_map_append(resp,"events",ctx->evqueue.queued);
    ctx->evqueue.queued = llsd_new_array();
    llsd_map_append(resp,"id",llsd_new_int(++ctx->evqueue.ctr));

    ctx->evqueue.last = resp;
    llsd_soup_set_response(msg, resp);
}

static void event_queue_do_timeout(user_ctx* ctx) {
  if(ctx->evqueue.msg != NULL) {
    soup_server_unpause_message(ctx->sim->soup, ctx->evqueue.msg);
    soup_message_set_status(ctx->evqueue.msg,502); // FIXME - ????
    ctx->evqueue.msg = NULL;
  }
}

void user_event_queue_send(user_ctx* ctx, const char* name, caj_llsd *body) {
  caj_llsd *event = llsd_new_map();
  llsd_map_append(event, "message", llsd_new_string(name));
  llsd_map_append(event, "body", body);
  llsd_array_append(ctx->evqueue.queued, event);
  if(ctx->evqueue.msg != NULL) {
    soup_server_unpause_message(ctx->sim->soup, ctx->evqueue.msg);
    event_queue_get_resp(ctx->evqueue.msg, ctx);
    ctx->evqueue.msg = NULL;
  }
}

static void event_queue_get(SoupMessage *msg, user_ctx* ctx, void *user_data) {
  if (msg->method != SOUP_METHOD_POST) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
    return;
  }

  caj_llsd *llsd, *ack;
  if(msg->request_body->length > 4096) goto fail;
  llsd = llsd_parse_xml(msg->request_body->data, msg->request_body->length);
  if(llsd == NULL) {
    printf("DEBUG: EventQueueGet parse failed\n");
    goto fail;
  }
  if(!LLSD_IS(llsd, LLSD_MAP)) {
    printf("DEBUG: EventQueueGet not map\n");
    goto free_fail;
  }
  ack = llsd_map_lookup(llsd,"ack");
  if(ack == NULL || (ack->type_id != LLSD_INT && ack->type_id != LLSD_UNDEF)) {
    printf("DEBUG: EventQueueGet bad ack\n");
    printf("DEBUG: message is {{%s}}\n", msg->request_body->data);
    goto free_fail;
  }
  if(ack->type_id == LLSD_INT && ack->t.i < ctx->evqueue.ctr &&
     ctx->evqueue.last != NULL) {
    llsd_soup_set_response(msg, ctx->evqueue.last);
    llsd_free(llsd);
    return;
  }

  event_queue_do_timeout(ctx);

  if(ctx->evqueue.queued->t.arr.count > 0) {
    event_queue_get_resp(msg, ctx);
  } else {
    soup_server_pause_message(ctx->sim->soup, msg);
    ctx->evqueue.timeout = g_timer_elapsed(ctx->sim->timer, NULL) + 10.0;
    ctx->evqueue.msg = msg;
  }

  
  llsd_free(llsd);
  return;
  
 free_fail:
  llsd_free(llsd);
 fail:
  soup_message_set_status(msg,400);
  
}

void user_int_event_queue_init(user_ctx *ctx) {
  // FIXME - split off the event queue stuff
  ctx->evqueue.queued = llsd_new_array();
  ctx->evqueue.last = NULL;
  ctx->evqueue.ctr = 0;
  ctx->evqueue.msg = NULL;
  user_add_named_cap(ctx->sim,"EventQueueGet",event_queue_get,ctx,NULL);
}

void user_int_event_queue_free(user_ctx *ctx) {
  event_queue_do_timeout(ctx);

  if(ctx->evqueue.last != NULL)
    llsd_free(ctx->evqueue.last);
  llsd_free(ctx->evqueue.queued);
  
}

// FIXME - add our own timer for this? Or some way of hooking cleanup timer?
void user_int_event_queue_check_timeout(user_ctx *ctx, double time_now) {
  if(ctx->evqueue.msg != NULL && 
     time_now > ctx->evqueue.timeout) {
    printf("DEBUG: Timing out EventQueueGet\n");
    event_queue_do_timeout(ctx);
  }
}

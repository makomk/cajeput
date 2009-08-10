#ifndef CAJEPUT_UDP_H
#define CAJEPUT_UDP_H
#include "cajeput_core.h"

// FIXME - rename these to something saner
typedef void(*sl_msg_handler)(user_ctx*,sl_message*);
void register_msg_handler(struct simulator_ctx *sim, sl_message_tmpl* tmpl, 
			  sl_msg_handler handler);

/* Note - this calls sl_free_msg for you, but this does *not* free
   the actual struct sl_message itself. In normal usage, that's a 
   local variable (i.e. stack allocated) */
void sl_send_udp(struct user_ctx* ctx, struct sl_message* msg);

#endif

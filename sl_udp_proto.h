#ifndef SL_UDP_PROTO_H
#define SL_UDP_PROTO_H
#include "sl_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

int sl_parse_message(unsigned char* data, int len, struct sl_message* msgout);
int sl_pack_message(struct sl_message* msg, unsigned char* data, int len);
void sl_dump_packet(struct sl_message* msg);

#ifdef __cplusplus
}
#endif


#endif

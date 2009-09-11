#include <uuid/uuid.h>

/* ', '.join([ "0x%s" % "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".replace("-","")[i:i+2] for i in range(0,32,2) ]) */

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!! BIG FAT WARNING: do not change these from what the viewer thinks they    !!
!! should be. Seriously, don't do it. I'm pretty sure the viewer code has   !!
!! these hardcoded in. If you want to override these, apply additional      !!
!! animations with higher priorities on top of them. (See also: AOs)        !! 
!! Suggested hook point for this is set_default_anim in cajeput_udp.cpp     !!
!! !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
*/

// default animations shipped with the viewer
uuid_t stand_anim = { 0x24, 0x08, 0xfe, 0x9e, 0xdf, 0x1d, 0x1d, 0x7d, 0xf4, 0xff, 0x13, 0x84, 0xfa, 0x7b, 0x35, 0x0f };
uuid_t walk_anim = { 0x6e, 0xd2, 0x4b, 0xd8, 0x91, 0xaa, 0x4b, 0x12, 0xcc, 0xc7, 0xc9, 0x7c, 0x85, 0x7a, 0xb4, 0xe0 };
uuid_t hover_anim = { 0x4a, 0xe8, 0x01, 0x6b, 0x31, 0xb9, 0x03, 0xbb, 0xc4, 0x01, 0xb1, 0xea, 0x94, 0x1d, 0xb4, 0x1d };
uuid_t fly_anim = { 0xae, 0xc4, 0x61, 0x0c, 0x75, 0x7f, 0xbc, 0x4e, 0xc0, 0x92, 0xc6, 0xe9, 0xca, 0xf1, 0x8d, 0xaf };
uuid_t hover_down_anim = { 0x20, 0xf0, 0x63, 0xea, 0x83, 0x06, 0x25, 0x62, 0x0b, 0x07, 0x5c, 0x85, 0x3b, 0x37, 0xb3, 0x1e };
uuid_t hover_up_anim = { 0x62, 0xc5, 0xde, 0x58, 0xcb, 0x33, 0x57, 0x43, 0x3d, 0x07, 0x9e, 0x4c, 0xd4, 0x35, 0x28, 0x64 };
uuid_t land_anim = { 0x7a, 0x17, 0xb0, 0x59, 0x12, 0xb2, 0x41, 0xb1, 0x57, 0x0a, 0x18, 0x63, 0x68, 0xb6, 0xaa, 0x6f };

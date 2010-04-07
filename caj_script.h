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

#ifndef CAJ_SCRIPT_H
#define CAJ_SCRIPT_H

// internal event mask used by Cajeput script engine
// mostly not implemented yet
#define CAJ_EVMASK_TOUCH 0x1 // touch_start/end
#define CAJ_EVMASK_TOUCH_CONT 0x2 // continuous touch
#define CAJ_EVMASK_COLLISION 0x4 // collision start/end
#define CAJ_EVMASK_COLLISION_CONT 0x8 // continuous collision events
#define CAJ_EVMASK_LINK_MESSAGE 0x10 // FIXME - potential race condition on rez?

// more internal stuff
#define CAJ_COLLISION_START 0
#define CAJ_COLLISION_CONT 1
#define CAJ_COLLISION_END 2

// flags used by the changed event
#define CHANGED_INVENTORY 0x1
#define CHANGED_COLOR 0x2
#define CHANGED_SHAPE 0x4
#define CHANGED_SCALE 0x8
#define CHANGED_TEXTURE 0x10
#define CHANGED_LINK 0x20
#define CHANGED_ALLOWED_DROP 0x40
#define CHANGED_OWNER 0x80
/* The following appear to differ between OpenSim and Second Life. We use the
   Second Life definitions here, as far as I can tell. This seems likely to 
   cause pain in the future. */
#define CHANGED_REGION 0x100
#define CHANGED_TELEPORT 0x200
#define CHANGED_REGION_START 0x400

#define LINK_ROOT 1
#define LINK_FIRST_CHILD 2
#define LINK_SET -1
#define LINK_ALL_OTHERS -2
#define LINK_ALL_CHILDREN -3
#define LINK_THIS -4

#define DET_TYPE_AGENT 1
#define DET_TYPE_ACTIVE 2
#define DET_TYPE_PASSIVE 4
#define DET_TYPE_SCRIPTED 8

#endif

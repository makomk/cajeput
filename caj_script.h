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
#define CHANGED_INVENTORY 0x1 // lsl:int
#define CHANGED_COLOR 0x2 // lsl:int
#define CHANGED_SHAPE 0x4 // lsl:int
#define CHANGED_SCALE 0x8 // lsl:int
#define CHANGED_TEXTURE 0x10 // lsl:int
#define CHANGED_LINK 0x20 // lsl:int
#define CHANGED_ALLOWED_DROP 0x40 // lsl:int
#define CHANGED_OWNER 0x80 // lsl:int
/* The following appear to differ between OpenSim and Second Life. We use the
   Second Life definitions here, as far as I can tell. This seems likely to 
   cause pain in the future. */
#define CHANGED_REGION 0x100 // lsl:int
#define CHANGED_TELEPORT 0x200 // lsl:int
#define CHANGED_REGION_START 0x400 // lsl:int

#define LINK_ROOT 1 // lsl:int
#define LINK_FIRST_CHILD 2 // lsl:int
#define LINK_SET -1 // lsl:int
#define LINK_ALL_OTHERS -2 // lsl:int
#define LINK_ALL_CHILDREN -3 // lsl:int
#define LINK_THIS -4 // lsl:int

#define DET_TYPE_AGENT 1
#define DET_TYPE_ACTIVE 2
#define DET_TYPE_PASSIVE 4
#define DET_TYPE_SCRIPTED 8

// constants for llSetPrimitiveParams
#define PRIM_TYPE 9 // lsl:int
#define   PRIM_TYPE_BOX 0 // lsl:int
#define   PRIM_TYPE_CYLINDER 1 // lsl:int
#define   PRIM_TYPE_PRISM 2 // lsl:int
#define   PRIM_TYPE_SPHERE 3 // lsl:int
#define   PRIM_TYPE_TORUS 4 // lsl:int
#define   PRIM_TYPE_TUBE 5 // lsl:int
#define   PRIM_TYPE_RING 6 // lsl:int
#define   PRIM_TYPE_SCULPT 7 // lsl:int
/* #define   PRIM_HOLE_DEFAULT PROFILE_HOLLOW_DEFAULT
   #define   PRIM_HOLE_CIRCLE PROFILE_HOLLOW_CIRC 
   etc...
*/
#define PRIM_MATERIAL 2 // lsl:int
/* TODO: reuse constants from cajeput_world.h */
#define   PRIM_MATERIAL_STONE   0 // lsl:int
#define   PRIM_MATERIAL_METAL   1 // lsl:int
#define   PRIM_MATERIAL_GLASS   2 // lsl:int
#define   PRIM_MATERIAL_WOOD    3 // lsl:int
#define   PRIM_MATERIAL_FLESH   4 // lsl:int
#define   PRIM_MATERIAL_PLASTIC 5 // lsl:int
#define   PRIM_MATERIAL_RUBBER  6 // lsl:int
#define   PRIM_MATERIAL_LIGHT   7 // lsl:int

#define PRIM_POINT_LIGHT 23 // lsl:int
#define PRIM_TEXT 26 // lsl:int

//permission flags
#define PERMISSION_DEBIT 0x2 // lsl:int
#define PERMISSION_TAKE_CONTROLS 0x4 // lsl:int
#define PERMISSION_REMAP_CONTROLS 0x8 // lsl:int
#define PERMISSION_TRIGGER_ANIMATION 0x10 // lsl:int
#define PERMISSION_ATTACH 0x20 // lsl:int

// for llHTTPRequest
#define HTTP_METHOD 0 // lsl:int

// for texture-related stuff
#define ALL_SIDES -1 // lsl:int

#endif

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

void caj_mult_quat_quat(struct caj_quat *out, const struct caj_quat* q1,
			       const struct caj_quat *q2) {
  caj_quat ret;
  ret.w = - q1->x*q2->x - q1->y*q2->y - q1->z*q2->z + q1->w*q2->w;
  ret.x =   q1->x*q2->w + q1->y*q2->z - q1->z*q2->y + q1->w*q2->x;
  ret.y = - q1->x*q2->z + q1->y*q2->w + q1->z*q2->x + q1->w*q2->y;
  ret.z =   q1->x*q2->y - q1->y*q2->x + q1->z*q2->w + q1->w*q2->z;
  *out = ret;
}

void caj_mult_vect3_quat(struct caj_vector3 *out, const struct caj_quat* rot,
			 const struct caj_vector3 *vec) {
  // FIXME - blind copy and paste from OpenSim code
  caj_vector3 ret;
  ret.x =
    rot->w * rot->w * vec->x +
    2.f * rot->y * rot->w * vec->z -
    2.f * rot->z * rot->w * vec->y +
    rot->x * rot->x * vec->x +
    2.f * rot->y * rot->x * vec->y +
    2.f * rot->z * rot->x * vec->z -
    rot->z * rot->z * vec->x -
    rot->y * rot->y * vec->x;

  ret.y =
    2.f * rot->x * rot->y * vec->x +
    rot->y * rot->y * vec->y +
    2.f * rot->z * rot->y * vec->z +
    2.f * rot->w * rot->z * vec->x -
    rot->z * rot->z * vec->y +
    rot->w * rot->w * vec->y -
    2.f * rot->x * rot->w * vec->z -
    rot->x * rot->x * vec->y;
  
  ret.z =
    2.f * rot->x * rot->z * vec->x +
    2.f * rot->y * rot->z * vec->y +
    rot->z * rot->z * vec->z -
    2.f * rot->w * rot->y * vec->x -
    rot->y * rot->y * vec->z +
    2.f * rot->w * rot->x * vec->y -
    rot->x * rot->x * vec->z +
    rot->w * rot->w * vec->z;
  
  *out = ret;
}

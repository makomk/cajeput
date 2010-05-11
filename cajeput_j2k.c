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

#include <stdio.h>
#include <stdint.h>
#include "cajeput_j2k.h"
#include "libopenjpeg/openjpeg.h"

int cajeput_j2k_info(unsigned char* data, int len, struct cajeput_j2k *info) {
  opj_dparameters_t params;
  opj_codestream_info_t imginfo;
  opj_dinfo_t* dinfo = opj_create_decompress(CODEC_J2K);
  opj_set_default_decoder_parameters(&params);
  params.cp_limit_decoding = LIMIT_TO_TIER2;
  opj_setup_decoder(dinfo, &params);
  opj_set_event_mgr((opj_common_ptr)dinfo, NULL, NULL); // FIXME - HACK!
  opj_cio_t* cio = opj_cio_open((opj_common_ptr)dinfo, data, len);
  opj_image_t* image = opj_decode_with_info(dinfo, cio, &imginfo);
  int i, numprec = 0;

  if(image == NULL) {
    printf("J2K ERROR: decode failed\n");
    opj_destroy_decompress(dinfo);
    opj_cio_close(cio);
    return 0;
  }
  
  printf("Decode done: %ix%i, %i components, %i layers, %i tiles\n",
	 image->x1, image->y1, imginfo.numcomps, imginfo.numlayers, imginfo.tw*imginfo.th);
  for(i = 0; i < imginfo.numcomps; i++) {
    // number of resolutions = numdecompos+1

    // FIXME - we're assuming one precinct per (layer,res,component) triplet
    numprec += imginfo.numdecompos[i]+1;
    //for(j = 0; j < imginfo.numdecompos[i]+1; j++) {
    //}
  }

  // FIXME - just set a single discard level in this case
  if(imginfo.prog != LRCP) {
    printf("J2K ERROR: image progression not LRCP\n");
    goto out_fail;
  }

  if(numprec * imginfo.numlayers != imginfo.packno) {
    printf("J2K ERROR: unexpected number of packets: got %i, expected %i*%i=%i\n",
	   imginfo.packno, numprec, imginfo.numlayers, numprec * imginfo.numlayers);
    goto out_fail;
  }

  if(imginfo.numlayers > MAX_DISCARD_LEVELS || imginfo.numlayers <= 0) {
    printf("J2K ERROR: bad number of layers\n");
    goto out_fail;
  }

  info->width = image->x1;
  info->height = image->y1;
  info->num_comps = imginfo.numcomps;
  info->num_discard = imginfo.numlayers;

  for(i = 0; i < imginfo.numlayers; i++) {
    int layer_start = imginfo.tile[0].packet[i*numprec].start_pos;
    int layer_end = imginfo.tile[0].packet[i*numprec+numprec-1].end_pos;
    printf("Layer %i: start %i, end %i\n", i, layer_start, layer_end);

    info->discard_levels[imginfo.numlayers - 1 - i] = layer_end;
  }

  info->discard_levels[0] = len;

  opj_destroy_cstr_info(&imginfo);
  opj_image_destroy(image);
  opj_destroy_decompress(dinfo);
  opj_cio_close(cio);
  return 1;  

 out_fail:
  opj_destroy_cstr_info(&imginfo);
  opj_image_destroy(image);
  opj_destroy_decompress(dinfo);
  opj_cio_close(cio);
  return 0;
}

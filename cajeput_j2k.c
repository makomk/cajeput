#include <stdio.h>
#include <stdint.h>
#include "cajeput_j2k.h"
#include "libopenjpeg/openjpeg.h"

int cajeput_j2k_parse(unsigned char* data, int len, struct cajeput_j2k *info) {
  opj_dparameters_t params;
  opj_codestream_info_t imginfo;
  opj_dinfo_t* dinfo = opj_create_decompress(CODEC_J2K);
  opj_set_default_decoder_parameters(&params);
  params.cp_limit_decoding = LIMIT_TO_TIER2;
  opj_setup_decoder(dinfo, &params);
  opj_cio_t* cio = opj_cio_open(dinfo, data, len);
  opj_image_t* image = opj_decode_with_info(dinfo, cio, &imginfo);
  int i, j, numprec = 0;

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

  if(numprec * imginfo.numlayers != imginfo.packno) {
    printf("J2K ERROR: unexpected number of packets: got %i, expected %i*%i=%i\n",
	   imginfo.packno, numprec, imginfo.numlayers, numprec * imginfo.numlayers);
    goto out_fail;
  }

  for(i = 0; i < imginfo.numlayers; i++) {
    int layer_start = imginfo.tile[0].packet[i*numprec].start_pos;
    int layer_end = imginfo.tile[0].packet[i*numprec+numprec-1].end_pos;
    printf("Layer %i: start %i, end %i\n", i, layer_start, layer_end);
  }

  

 out_fail:
  opj_destroy_cstr_info(&imginfo);
  opj_image_destroy(image);
  opj_destroy_decompress(dinfo);
  opj_cio_close(cio);
  return 0;
}
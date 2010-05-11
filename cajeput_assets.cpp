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

#include "cajeput_core.h"
#include "cajeput_int.h" 
#include "cajeput_j2k.h"
#include <stdio.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <unistd.h>
#include <cassert>
#include <fcntl.h>

//  ----------- Texture-related stuff ----------------


// may want to move this to a thread, but it's reasonably fast since it 
// doesn't have to do a full decode
static void sim_texture_read_metadata(struct texture_desc *desc) {
  struct cajeput_j2k info;
  if(cajeput_j2k_info(desc->data, desc->len, &info)) {
    assert(info.num_discard > 0);
    desc->width = info.width; desc->height = info.height;
    desc->num_discard = info.num_discard;
    desc->discard_levels = new int[info.num_discard];
    memcpy(desc->discard_levels, info.discard_levels, 
	   info.num_discard*sizeof(int));
  } else {
    char buf[40]; uuid_unparse(desc->asset_id, buf);
    printf("WARNING: texture metadata read failed for %s\n", buf);
    desc->num_discard = 1;
    desc->discard_levels = new int[1];
    desc->discard_levels[0] = desc->len;
  }
}

static void save_texture(texture_desc *desc, const char* dirname) {
  char asset_str[40], fname[80]; int fd;
    uuid_unparse(desc->asset_id, asset_str);
    snprintf(fname, 80, "%s/%s.jp2", dirname, asset_str);
    fd = open(fname, O_WRONLY|O_CREAT|O_EXCL, 0644);
    if(fd < 0) {
      printf("Warning: couldn't open %s for temp texture save\n",
	     fname);
    } else {
      int ret = write(fd, desc->data, desc->len);
      if(ret != desc->len) {
	if(ret < 0) perror("save local texture");
	printf("Warning: couldn't write full texure to %s: %i/%i\n",
	       fname, ret, desc->len);
      }
      close(fd);
    }
}

void caj_texture_finished_load(texture_desc *desc) {
  assert(desc->data != NULL);
  sim_texture_read_metadata(desc);
  save_texture(desc,"tex_cache");
}

// FIXME - actually clean up the textures we allocate
void sim_add_local_texture(struct simulator_ctx *sim, uuid_t asset_id, 
			   unsigned char *data, int len, int is_local) {
  texture_desc *desc = new texture_desc();
  uuid_copy(desc->asset_id, asset_id);
  desc->flags = is_local ?  CJP_TEXTURE_LOCAL : 0; // FIXME - code duplication
  desc->data = data; desc->len = len;
  desc->refcnt = 0; 
  desc->width = desc->height = desc->num_discard = 0;
  desc->discard_levels = NULL;
  sim_texture_read_metadata(desc);
  sim->sgrp->textures[asset_id] = desc;

  if(is_local) {
    save_texture(desc, "temp_assets");
  }
}

struct texture_desc *caj_get_texture(struct simgroup_ctx *sgrp, uuid_t asset_id) {
  texture_desc *desc;
  std::map<obj_uuid_t,texture_desc*>::iterator iter =
    sgrp->textures.find(asset_id);
  if(iter != sgrp->textures.end()) {
    desc = iter->second;
  } else {
    desc = new texture_desc();
    uuid_copy(desc->asset_id, asset_id);
    desc->flags = 0;
    desc->data = NULL; desc->len = 0;
    desc->refcnt = 0;
    desc->width = desc->height = desc->num_discard = 0;
    desc->discard_levels = NULL;
    sgrp->textures[asset_id] = desc;
  }
  desc->refcnt++; return desc;
}

static const char* texture_dirs[] = {"temp_assets","tex_cache",NULL};

void caj_request_texture(struct simgroup_ctx *sgrp, struct texture_desc *desc) {
  if(desc->data == NULL && (desc->flags & 
		     (CJP_TEXTURE_PENDING | CJP_TEXTURE_MISSING)) == 0) {
    char asset_str[40], fname[80]; int fd; 
    struct stat st;
    uuid_unparse(desc->asset_id, asset_str);

    // first, let's see if we've got a cached copy locally
    for(int i = 0; texture_dirs[i] != NULL; i++) {
      sprintf(fname, "%s/%s.jp2", texture_dirs[i], asset_str);
      if(stat(fname, &st) != 0 || st.st_size == 0) continue;

      printf("DEBUG: loading texture from %s, len %i\n",fname,(int)st.st_size);

      desc->len = st.st_size;
      fd = open(fname, O_RDONLY);
      if(fd < 0) {
	printf("ERROR: couldn't open texture cache file\n");
	break;
      }
      
      unsigned char *data = (unsigned char*)malloc(desc->len);
      int off;
      for(off = 0; off < desc->len; ) {
	int ret = read(fd, data+off, desc->len-off);
	if(ret <= 0) break;
	off += ret;
      }
      close(fd);

      if(off < desc->len) {
	printf("ERROR: Couldn't read texture from file\n");
	free(data); break;
      }

      desc->data = data;
      sim_texture_read_metadata(desc);
      return;
    }

    // No cached copy, have to do a real fetch
    desc->flags |= CJP_TEXTURE_PENDING;
    sgrp->gridh.get_texture(sgrp, desc);
  }
}

// ------------- asset-related fun --------------------------

int user_can_access_asset_direct(user_ctx *user, simple_asset *asset) {
  return asset->type != ASSET_NOTECARD && asset->type != ASSET_LSL_TEXT;
}

int user_can_access_asset_task_inv(user_ctx *user, primitive_obj *prim,
				   inventory_item *inv) {
  return TRUE; // FIXME - need actual permission checks
}

void caj_asset_finished_load(struct simgroup_ctx *sgrp, 
			     struct simple_asset *asset, int success) {
  // FIXME - use something akin to containerof?
  std::map<obj_uuid_t,asset_desc*>::iterator iter =
    sgrp->assets.find(asset->id);
  assert(iter != sgrp->assets.end());
  asset_desc *desc = iter->second;
  assert(desc->status == CAJ_ASSET_PENDING);
  assert(asset == &desc->asset);

  if(success) desc->status = CAJ_ASSET_READY;
  else desc->status = CAJ_ASSET_MISSING;

  for(std::set<asset_cb_desc*>::iterator iter = desc->cbs.begin(); 
      iter != desc->cbs.end(); iter++) {
    (*iter)->cb(sgrp, (*iter)->cb_priv, success ? &desc->asset : NULL);
    delete (*iter);
  }
  desc->cbs.clear();
}

void caj_get_asset(struct simgroup_ctx *sgrp, uuid_t asset_id,
		   void(*cb)(struct simgroup_ctx *sgrp, void *priv,
			     struct simple_asset *asset), void *cb_priv) {
  asset_desc *desc;
  std::map<obj_uuid_t,asset_desc*>::iterator iter =
    sgrp->assets.find(asset_id);
  if(iter != sgrp->assets.end()) {
    desc = iter->second;
  } else {
    desc = new asset_desc();
    uuid_copy(desc->asset.id, asset_id);
    desc->status = CAJ_ASSET_PENDING;
    desc->asset.name = desc->asset.description = NULL;
    desc->asset.data.data = NULL;
    sgrp->assets[asset_id] = desc;
    
    printf("DEBUG: sending asset request via grid\n");
    sgrp->gridh.get_asset(sgrp, &desc->asset);
  }
  
  switch(desc->status) {
  case CAJ_ASSET_PENDING:
    // FIXME - move this to the generic hooks code
    desc->cbs.insert(new asset_cb_desc(cb, cb_priv));
    break;
  case CAJ_ASSET_READY:
    cb(sgrp, cb_priv, &desc->asset); break;
  case CAJ_ASSET_MISSING:
    cb(sgrp, cb_priv, NULL); break;
  default: assert(0);
  }
}

void caj_put_asset(struct simgroup_ctx *sgrp, struct simple_asset *asset,
		   caj_put_asset_cb cb, void *cb_priv) {
  sgrp->gridh.put_asset(sgrp, asset, cb, cb_priv);
}

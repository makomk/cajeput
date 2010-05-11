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

//#include "sl_messages.h"
//#include "caj_llsd.h"
#include "cajeput_core.h"
#include "cajeput_user.h"
#include <stdlib.h>
#include <cassert>
#include <set>
#include <stdio.h>

// FIXME - need to check for integer overflow?
template<class T>
static void grow_array(unsigned int *num_items, unsigned int* alloc_items,
		       T** items) {
  if(*alloc_items == 0) {
    *alloc_items = 4; *items = (T*)malloc(4*sizeof(T));
  } else if(*num_items >= *alloc_items) {
    *alloc_items *= 2;
    *items = (T*)realloc(*items, (size_t)*alloc_items * sizeof(T));
  }
  if(*items == NULL) abort();
}

struct inventory_contents* caj_inv_new_contents_desc(uuid_t folder_id) {
  struct inventory_contents* inv = new inventory_contents();
  uuid_copy(inv->folder_id, folder_id);
  inv->num_subfolder = inv->num_items = 0;
  inv->alloc_subfolder = inv->alloc_items = 0;
  inv->subfolders = NULL;
  inv->items = NULL;
  return inv;
}

void caj_inv_free_contents_desc(struct inventory_contents* inv) {
  for(unsigned i = 0; i < inv->num_subfolder; i++) {
    free(inv->subfolders[i].name);
  }
  free(inv->subfolders);

  for(unsigned i = 0; i < inv->num_items; i++) {
    free(inv->items[i].name);
    free(inv->items[i].description);
    free(inv->items[i].creator_id);
  }
  free(inv->items);

  delete inv;
}

// arbitrary limit to prevent certain potential attacks
// in theory, we can do this in a streaming fashion that doesn't require us to
// have all the inventory in memory at once, but that's a pain.
#define MAX_INVENTORY_DESC 65535

struct inventory_folder* caj_inv_add_folder(struct inventory_contents* inv,
					    uuid_t folder_id, uuid_t owner_id,
					    const char* name, 
					    int8_t asset_type) {
  if(inv->num_subfolder >= MAX_INVENTORY_DESC) {
    printf("!!! ERROR: too many subfolders in one inventory folder\n");
    return NULL;
  }
  grow_array<inventory_folder>(&inv->num_subfolder, &inv->alloc_subfolder, 
			       &inv->subfolders);
  struct inventory_folder* folder = &inv->subfolders[inv->num_subfolder++];
  folder->name = strdup(name);
  uuid_copy(folder->folder_id, folder_id);
  uuid_copy(folder->owner_id, owner_id); 
  uuid_copy(folder->parent_id, inv->folder_id); // FIXME - remove this?
  folder->asset_type = asset_type;
  return folder;
}

struct inventory_folder* caj_inv_make_folder(uuid_t parent_id, uuid_t folder_id,
					     uuid_t owner_id, const char* name,
					     int8_t asset_type) {
  inventory_folder* folder = new inventory_folder();
  folder->name = strdup(name);
  uuid_copy(folder->folder_id, folder_id);
  uuid_copy(folder->owner_id, owner_id); 
  uuid_copy(folder->parent_id, parent_id);
  folder->asset_type = asset_type;
  return folder;
}

void caj_inv_free_folder(struct inventory_folder* folder) {
  free(folder->name); delete folder;
}
						 
struct inventory_item* caj_add_inventory_item(struct inventory_contents* inv, 
					      const char* name, const char* desc,
					      const char* creator) {
  if(inv->num_items >= MAX_INVENTORY_DESC) {
    printf("!!! ERROR: too many items in one inventory folder\n");
    return NULL;
  }
  grow_array<inventory_item>(&inv->num_items, &inv->alloc_items, 
			     &inv->items);
  struct inventory_item* item = &inv->items[inv->num_items++];
  item->name = strdup(name);
  item->description = strdup(desc);
  item->creator_id = strdup(creator);
  uuid_copy(item->folder_id, inv->folder_id);// FIXME - remove item->folder_id?

  return item;
}

void caj_inv_copy_item(struct inventory_item *dest,
		       const struct inventory_item *src) {
  *dest = *src;
  dest->name = strdup(src->name);
  dest->description = strdup(src->description);
  dest->creator_id = strdup(src->creator_id);
}


uint32_t caj_calc_inventory_crc(struct inventory_item* item) {
  return 0; // FIXME
  // The gory details of how this should work are already implemented in 
  // libopenmv, but we seem to be getting away with it for now.
}

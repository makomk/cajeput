/*
  Copyright (c) 2007-2009, openmetaverse.org
  All rights reserved.

  - Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

  - Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
  - Neither the name of the openmetaverse.org nor the names
    of its contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "caj_types.h"
#include "caj_helpers.h"
#include "terrain_compress.h"

#define END_OF_PATCHES 97
#define STRIDE 264
#define OO_SQRT2 0.7071067811865475244008443621049f
#define ZERO_CODE 0x0
#define ZERO_EOB 0x2
#define POSITIVE_VALUE 0x6
#define NEGATIVE_VALUE 0x7

// TODO - initialise these statically
static float QuantizeTable16[16*16];
static float CosineTable16[16*16];
static int CopyMatrix16[16*16];

struct bit_packer {
  unsigned char* buf;
  int buflen;
  int bytePos, bitPos;
};

struct patch_header {
  float DCOffset;
  int Range;
  int QuantWBits;
  int PatchIDs;
  unsigned int WordBits;
};

static struct bit_packer *new_bit_packer(char* buf, int len) {
  struct bit_packer *bitpack = malloc(sizeof(struct bit_packer));
  bitpack->buf = buf; bitpack->buflen = len;
  memset(buf, 0, len);
  bitpack->bytePos = bitpack->bitPos = 0;
  return bitpack;
}

static int bit_packer_num_bytes(struct bit_packer *pack) {
  int ret = pack->bitPos ? pack->bytePos + 1 : pack->bytePos;
  if(ret > pack->buflen) ret = pack->buflen;
  return ret;
}

#define MAX_BITS 8

static void pack_bit_array(struct bit_packer *pack, unsigned char *data, int totalCount)
{
  int count = 0;
  int curBytePos = 0;
  int curBitPos = 0;

  if(pack->bytePos >= pack->buflen) return;

  while (totalCount > 0)
    {
      if (totalCount > MAX_BITS)
	{
	  count = MAX_BITS;
	  totalCount -= MAX_BITS;
	}
      else
	{
	  count = totalCount;
	  totalCount = 0;
	}

      while (count > 0)
	{
	  // FIXME - optimise this
	  if ((data[curBytePos] & (0x01 << (count - 1))) != 0)
	    pack->buf[pack->bytePos] |= (unsigned char)(0x80 >> pack->bitPos);

	  --count;
	  ++pack->bitPos;
	  ++curBitPos;

	  if (pack->bitPos >= MAX_BITS)
	    {
	      if(pack->bytePos >= pack->buflen) {
		printf("ERROR: bitpacker buffer overflowed!\n");
		return;
	      }
	      pack->bitPos = 0;
	      ++pack->bytePos;
	    }
	  if (curBitPos >= MAX_BITS)
	    {
	      curBitPos = 0;
	      ++curBytePos;
	    }
	}
    }
}

static void pack_bits(struct bit_packer *pack, uint32_t data, int totalCount) {
  unsigned char arr[4];
  caj_uint32_to_bin_le(arr, data);
  pack_bit_array(pack, arr, totalCount);
}

static void pack_float(struct bit_packer *pack, float data) {
  unsigned char arr[4];
  caj_float_to_bin_le(arr, data);
  pack_bit_array(pack, arr, 32);
}

static void prescan_patch(float *heightmap, int patchX, int patchY, struct patch_header *header)
{
  memset(header, 0, sizeof(*header));
  float zmax = -99999999.0f;
  float zmin = 99999999.0f;
  int i, j;
  
  for (j = patchY * 16; j < (patchY + 1) * 16; j++)
    {
      for (i = patchX * 16; i < (patchX + 1) * 16; i++)
	{
	  float val = heightmap[j * 256 + i];
	  if (val > zmax) zmax = val;
	  if (val < zmin) zmin = val;
	}
    }
  
  header->DCOffset = zmin;
  header->Range = (int)((zmax - zmin) + 1.0f);
}

static void DCTLine16(float *linein, float *lineout, int line)
{
  float total = 0.0f;
  int lineSize = line * 16;
  int n, u;

  for (n = 0; n < 16; n++)
    {
      total += linein[lineSize + n];
    }

  lineout[lineSize] = OO_SQRT2 * total;

  for (u = 1; u < 16; u++)
    {
      total = 0.0f;

      for (n = 0; n < 16; n++)
	{
	  total += linein[lineSize + n] * CosineTable16[u * 16 + n];
	}

      lineout[lineSize + u] = total;
    }
}

static void DCTColumn16(float *linein, int *lineout, int column)
{
  float total = 0.0f;
  const float oosob = 2.0f / 16.0f;
  int n, u;

  for (n = 0; n < 16; n++)
    {
      total += linein[16 * n + column];
    }

  lineout[CopyMatrix16[column]] = (int)(OO_SQRT2 * total * oosob * QuantizeTable16[column]);

  for (u = 1; u < 16; u++)
    {
      total = 0.0f;

      for (n = 0; n < 16; n++)
	{
	  total += linein[16 * n + column] * CosineTable16[u * 16 + n];
	}

      lineout[CopyMatrix16[16 * u + column]] = (int)(total * oosob * QuantizeTable16[16 * u + column]);
    }
}


static void CompressPatch(float* heightmap, int patchX, int patchY, struct patch_header *header, 
			  int prequant, int *patch_out)
{
  float block[16 * 16];
  int wordsize = prequant;
  float oozrange = 1.0f / (float)header->Range;
  float range = (float)(1 << prequant);
  float premult = oozrange * range;
  float sub = (float)(1 << (prequant - 1)) + header->DCOffset * premult;
  int i, j, k;
  float ftemp[16 * 16];
  
  header->QuantWBits = wordsize - 2;
  header->QuantWBits |= (prequant - 2) << 4;
  
  k = 0;
  for (j = patchY * 16; j < (patchY + 1) * 16; j++)
    {
      for (i = patchX * 16; i < (patchX + 1) * 16; i++)
	block[k++] = heightmap[j * 256 + i] * premult - sub;
    }
  

  for (i = 0; i < 16; i++)
    DCTLine16(block, ftemp, i);
  for (i = 0; i < 16; i++)
    DCTColumn16(ftemp, patch_out, i);
}

static int EncodePatchHeader(struct bit_packer *output, struct patch_header *header, int *patch)
{
  int temp;
  int wbits = (header->QuantWBits & 0x0f) + 2;
  unsigned int maxWbits = (uint)wbits + 5;
  unsigned int minWbits = ((uint)wbits >> 1);
  int i, j;

  wbits = (int)minWbits;

  for (i = 0; i < (16*16); i++)
    {
      temp = patch[i];

      if (temp != 0)
	{
	  // Get the absolute value
	  if (temp < 0) temp *= -1;

	  for (j = (int)maxWbits; j > (int)minWbits; j--)
	    {
	      if ((temp & (1 << j)) != 0)
		{
		  if (j > wbits) wbits = j;
		  break;
		}
	    }
	}
    }

  wbits += 1;

  header->QuantWBits &= 0xf0;

  if (wbits > 17 || wbits < 2)
    {
      printf("Bits needed per word in EncodePatchHeader() are outside the allowed range\n");
    }

  header->QuantWBits |= (wbits - 2);

  pack_bits(output, header->QuantWBits, 8);
  pack_float(output, header->DCOffset);
  pack_bits(output, header->Range, 16);
  pack_bits(output, header->PatchIDs, 10);

  return wbits;
}

static void EncodePatch(struct bit_packer *output, int *patch, int postquant, int wbits)
{
  int temp, i, j;
  int eob;
  
  if (postquant > 16 * 16 || postquant < 0)
    {
      printf("Postquant is outside the range of allowed values in EncodePatch()\n");
      return;
    }

  if (postquant != 0) patch[16 * 16 - postquant] = 0;

  for (i = 0; i < 16 * 16; i++)
    {
      eob = 0;
      temp = patch[i];

      if (temp == 0)
	{
	  eob = 1;

	  for (j = i; j < 16 * 16 - postquant; j++)
	    {
	      if (patch[j] != 0)
		{
		  eob = 0;
		  break;
		}
	    }

	  if (eob)
	    {
	      pack_bits(output, ZERO_EOB, 2);
	      return;
	    }
	  else
	    {
	      pack_bits(output, ZERO_CODE, 1);
	    }
	}
      else
	{
	  if (temp < 0)
	    {
	      temp *= -1;

	      if (temp > (1 << wbits)) temp = (1 << wbits);

	      pack_bits(output, NEGATIVE_VALUE, 3);
	      pack_bits(output, temp, wbits);
	    }
	  else
	    {
	      if (temp > (1 << wbits)) temp = (1 << wbits);

	      pack_bits(output, POSITIVE_VALUE, 3);
	      pack_bits(output, temp, wbits);
	    }
	}
    }
}


static void CreatePatchFromHeightmap(struct bit_packer *output, float *heightmap, int x, int y)
{
  struct patch_header header;
  int patch[16*16];
  
  prescan_patch(heightmap, x, y, &header);
  header.QuantWBits = 136;
  header.PatchIDs = (y & 0x1F);
  header.PatchIDs += (x << 5);
  
  // NOTE: No idea what prequant and postquant should be or what they do
  CompressPatch(heightmap, x, y, &header, 10, patch);
  int wbits = EncodePatchHeader(output, &header, patch);
  EncodePatch(output, patch, 0, wbits);
}

void terrain_create_patches(float *heightmap, int  *patches, int num_patches, struct caj_string *out)
{
  int i;
#if 0
  LayerDataPacket layer = new LayerDataPacket();
  layer.LayerID.Type = (byte)TerrainPatch.LayerType.Land;
  
  TerrainPatch.GroupHeader header = new TerrainPatch.GroupHeader();
  header.Stride = STRIDE;
  header.PatchSize = 16;
  header.Type = TerrainPatch.LayerType.Land;
#endif
  
  char *data = malloc(1536);
  struct bit_packer *bitpack = new_bit_packer(data, 1536);
  pack_bits(bitpack, STRIDE, 16);
  pack_bits(bitpack, 16, 8);
  pack_bits(bitpack, LAYER_TYPE_LAND /* land */, 8);
  
  for (i = 0; i < num_patches; i++) {
    CreatePatchFromHeightmap(bitpack, heightmap, patches[i] % 16, (patches[i] - (patches[i] % 16)) / 16);
  }
  
  pack_bits(bitpack, END_OF_PATCHES, 8);

  out->data = bitpack->buf; out->len = bit_packer_num_bytes(bitpack);
  free(bitpack);
}


static void BuildQuantizeTable16()
{
  int i, j;
  for (j = 0; j < 16; j++)
    {
      for (i = 0; i < 16; i++)
	{
	  QuantizeTable16[j * 16 + i] = 1.0f / (1.0f + 2.0f * ((float)i + (float)j));
	}
    }
}

static void SetupCosines16()
{
  const float hposz = (float)M_PI * 0.5f / 16.0f;
  int u, n;

  for (u = 0; u < 16; u++)
    {
      for (n = 0; n < 16; n++)
	{
	  CosineTable16[u * 16 + n] = cosf((2.0f * (float)n + 1.0f) * (float)u * hposz);
	}
    }
}

static void BuildCopyMatrix16()
{
  int diag = 0;
  int right = 1;
  int i = 0;
  int j = 0;
  int count = 0;

  while (i < 16 && j < 16)
    {
      CopyMatrix16[j * 16 + i] = count++;

      if (!diag)
	{
	  if (right)
	    {
	      if (i < 16 - 1) i++;
	      else j++;

	      right = 0;
	      diag = 1;
	    }
	  else
	    {
	      if (j < 16 - 1) j++;
	      else i++;

	      right = 1;
	      diag = 1;
	    }
	}
      else
	{
	  if (right)
	    {
	      i++;
	      j--;
	      if (i == 16 - 1 || j == 0) diag = 0;
	    }
	  else
	    {
	      i--;
	      j++;
	      if (j == 16 - 1 || i == 0) diag = 0;
	    }
	}
    }
}

void terrain_init_compress() {
  SetupCosines16();
  BuildCopyMatrix16();
  BuildQuantizeTable16();
}

#include "cajeput_j2k.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static char *read_full_file(const char *name, int *lenout) {
  int len = 0, maxlen = 512, ret;
  char *data = (char*)malloc(maxlen);
  //FILE *f = fopen(name,"r");
  int fd = open(name,O_RDONLY);
  if(fd < 0) return NULL;
  for(;;) {
    //ret = fread(data+len, maxlen-len, 1, f);
    ret = read(fd, data+len, maxlen-len);
    if(ret <= 0) break;
    len += ret;
    if(maxlen-len < 256) {
      maxlen += 512;
      data = (char*)realloc(data, maxlen);
    }
  }
  close(fd); *lenout = len; return data;
}

int main( int argc, const char** argv ) {
  unsigned char* data; int len, i;
  struct cajeput_j2k info;
  if(argc != 2) {
    printf("Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  data = (unsigned char*)read_full_file(argv[1], &len);
  if(data == NULL) {
    printf("ERROR: couldn't read file\n"); return 1;
  }

  if(cajeput_j2k_info(data, len, &info)) {
    printf("Success: image %ix%i, %i components, %i layers:\n",
	   info.width, info.height, info.num_comps, info.num_discard);
    for(i = 0; i < info.num_discard; i++) {
      printf("   Discard level %i: %i\n", i, info.discard_levels[i]);
    }
  } else {
    printf("ERROR: decode failed\n");
  }
  
  free(data);
  return 0;
}

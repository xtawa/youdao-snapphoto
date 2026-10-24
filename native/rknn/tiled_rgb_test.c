#include <assert.h>
#include <stdlib.h>
#include "tiled_rgb.h"
int main(void){
 const int sizes[][2]={{1,1},{17,13},{208,208},{209,417},{624,416},{3264,979}};
 for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++){
  int w=sizes[i][0],h=sizes[i][1];size_t bytes=(size_t)w*h*3;
  unsigned char *src=malloc(bytes),*out=calloc(bytes,1),*tile=malloc(256*256*3);
  assert(src&&out&&tile);for(size_t j=0;j<bytes;j++)src[j]=(unsigned char)(j*71+j/13);
  for(int y=0;y<h;y+=SNAP_CORE)for(int x=0;x<w;x+=SNAP_CORE){
   snap_extract_tile(src,w,h,x,y,tile);snap_commit_tile(src,out,w,h,x,y,tile,192);
  }
  assert(!memcmp(src,out,bytes));free(src);free(out);free(tile);
 }
 return 0;
}

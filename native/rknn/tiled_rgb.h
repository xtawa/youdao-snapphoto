#ifndef SNAP_TILED_RGB_H
#define SNAP_TILED_RGB_H
#include <string.h>
/* 24 source pixels cover the twelve 3x3 convolutions at half resolution.
 * Only the center 208px is committed, so adjacent tiles share real context. */
#define SNAP_HALO 24
#define SNAP_CORE (256-2*SNAP_HALO)
static int snap_bound(int x,int size){return x<0?0:x>=size?size-1:x;}
static void snap_extract_tile(const unsigned char *src,int w,int h,int ox,int oy,unsigned char *tile){
 for(int y=0;y<256;y++)for(int x=0;x<256;x++){
  int sx=snap_bound(ox+x-SNAP_HALO,w),sy=snap_bound(oy+y-SNAP_HALO,h);
  memcpy(tile+(y*256+x)*3,src+((size_t)sy*w+sx)*3,3);
 }
}
static void snap_commit_tile(const unsigned char *src,unsigned char *dst,int w,int h,int ox,int oy,const unsigned char *tile,int blend){
 for(int y=0;y<SNAP_CORE&&oy+y<h;y++)for(int x=0;x<SNAP_CORE&&ox+x<w;x++)for(int c=0;c<3;c++){
  size_t at=((size_t)(oy+y)*w+ox+x)*3+c;
  int value=tile[((y+SNAP_HALO)*256+x+SNAP_HALO)*3+c];
  dst[at]=(unsigned char)((src[at]*(256-blend)+value*blend+128)/256);
 }
}
#endif

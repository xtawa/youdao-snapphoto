#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <dlfcn.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "experiments/rga-accel.h"
static double ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
int main(int argc,char **argv){
 (void)argv;const int sw=3264,sh=1836;void *source=NULL;struct dma_image src={0};int fd=-1,pitch=sw;
 if(argc>1){if(!dma_image_create_mode(&src,sw,sh,argc<3)){perror("DMA buffer");return 2;}source=src.data;fd=src.fd;pitch=src.pitch;assert(dma_image_sync(&src,1,1));}
 else if(posix_memalign(&source,4096,(size_t)sw*sh*3/2))return 2;
 unsigned char *nv=source;memset(nv,72,(size_t)pitch*sh);for(int i=pitch*sh;i<pitch*sh*3/2;i+=2){nv[i]=101;nv[i+1]=163;}
 if(fd>=0)assert(dma_image_sync(&src,1,0));
 const int sizes[][2]={{640,192},{216,720},{1280,720},{720,1280},{1920,1080},{2448,1836}};
 for(int i=0;i<6;i++){
  int w=sizes[i][0],h=sizes[i][1],angle=w<h?270:0;unsigned char *out=malloc((size_t)w*h*3/2);struct rga_context ctx={0};
  double began=ms();int good=rga_scale_yuv(&ctx,nv,sw,sh,pitch,out,w,h,100,angle,fd);
  if(!good){printf("{\"width\":%d,\"height\":%d,\"success\":false}\n",w,h);rga_context_destroy(&ctx);free(out);continue;}
  assert(abs(out[w*h/2]-72)<=2&&abs(out[w*h+w*h/8]-101)<=2&&abs(out[w*h+w*h/4+w*h/8]-163)<=2);
  double cold=ms()-began;began=ms();for(int n=0;n<24;n++)assert(rga_scale_yuv(&ctx,nv,sw,sh,pitch,out,w,h,100,angle,fd));
  double average=(ms()-began)/24;
  if(fd>=0)assert(dma_image_sync(&src,1,1));memset(nv,148,(size_t)pitch*sh);if(fd>=0)assert(dma_image_sync(&src,1,0));
  assert(rga_scale_yuv(&ctx,nv,sw,sh,pitch,out,w,h,100,angle,fd));assert(abs(out[w*h/2]-148)<=2);
  if(fd>=0)assert(dma_image_sync(&src,1,1));memset(nv,72,(size_t)pitch*sh);if(fd>=0)assert(dma_image_sync(&src,1,0));
  printf("{\"width\":%d,\"height\":%d,\"success\":true,\"coldMs\":%.3f,\"averageMs\":%.3f,\"cachedSourceUpdatesVisible\":true}\n",w,h,cold,average);
  rga_context_destroy(&ctx);free(out);
 }
 if(fd>=0)dma_image_destroy(&src);else free(source);return rga_successes?0:1;
}

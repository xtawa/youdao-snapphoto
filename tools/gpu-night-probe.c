#define _GNU_SOURCE
#define main processing_correctness_main
#include "../native/processing-test.c"
#undef main
#include <dlfcn.h>
#include <time.h>
static double now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
#include "experiments/gpu-night.h"
int main(void){
 gpu_start();double until=now_ms()+15000;
 while(now_ms()<until){pthread_mutex_lock(&gpu_phase_mutex);int phase=gpu_phase;pthread_mutex_unlock(&gpu_phase_mutex);if(phase!=1)break;usleep(20000);}
 if(gpu_phase!=2){printf("GPU not ready: %s\n",gpu_error);gpu_shutdown();return 1;}
 unsigned char lut[256];night_enabled=1;night_quality=1;night_strength=.45;night_snapshot(lut);
 const int sizes[][2]={{1280,720},{1920,1080},{2448,1836},{721,1281}};
 for(int s=0;s<4;s++){
  int w=sizes[s][0],h=sizes[s][1];size_t size=(size_t)w*h+2*(size_t)((w+1)/2)*((h+1)/2);
  unsigned char *source=malloc(size),*a=malloc(size),*b=malloc(size);assert(source&&a&&b);
  for(size_t i=0;i<size;i++)source[i]=(i<(size_t)w*h?32:128)+noise();
  memcpy(a,source,size);double cpu=now_ms();night_process_yuv(a,w,h,0,lut,1);cpu=now_ms()-cpu;double total=0;
  memcpy(b,source,size);assert(gpu_night(b,w,h,0,lut));
  for(int n=0;n<6;n++){
   memcpy(b,source,size);double began=now_ms();int good=gpu_night(b,w,h,0,lut);assert(good);total+=now_ms()-began;
   if(memcmp(a,b,size)){size_t different=0;for(size_t k=0;k<size;k++)different+=a[k]!=b[k];printf("GPU mismatch %zu/%zu\n",different,size);return 2;}
  }
  printf("{\"width\":%d,\"height\":%d,\"gpuMs\":%.3f,\"cpuMs\":%.3f,\"identical\":true}\n",w,h,total/6,cpu);fflush(stdout);
  free(source);free(a);free(b);
 }
 gpu_shutdown();return 0;
}

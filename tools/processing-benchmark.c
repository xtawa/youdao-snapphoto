/* Own synthetic buffers only, with active assertions even in optimized builds. */
#define main processing_correctness_main
#include "../native/processing-test.c"
#undef main
#include <time.h>
static double clock_ms(clockid_t id){struct timespec t;clock_gettime(id,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
int main(void){
 if(processing_correctness_main())return 1;
 const int sizes[][2]={{1280,720},{1920,1080},{2448,1836}};
 for(int s=0;s<3;s++){
  int w=sizes[s][0],h=sizes[s][1];size_t count=(size_t)w*h;unsigned char *source=malloc(count),*a=malloc(count),*b=malloc(count);assert(source&&a&&b);
  for(size_t i=0;i<count;i++)source[i]=32+noise();
  double wall[2]={0},cpu[2]={0};
  for(int n=0;n<6;n++)for(int vector=0;vector<2;vector++){
   unsigned char *output=vector?b:a;memcpy(output,source,count);
   double began=clock_ms(CLOCK_MONOTONIC),thread=clock_ms(CLOCK_THREAD_CPUTIME_ID);
   night_spatial_impl(output,w,h,0,vector);
   cpu[vector]+=clock_ms(CLOCK_THREAD_CPUTIME_ID)-thread;wall[vector]+=clock_ms(CLOCK_MONOTONIC)-began;
   if(vector)assert(!memcmp(a,b,count));
  }
  printf("{\"width\":%d,\"height\":%d,\"scalarMs\":%.3f,\"neonMs\":%.3f,\"scalarCpuMs\":%.3f,\"neonCpuMs\":%.3f,\"identical\":true}\n",w,h,wall[0]/6,wall[1]/6,cpu[0]/6,cpu[1]/6);
  free(source);free(a);free(b);
 }
 return 0;
}

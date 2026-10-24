#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
// Single-threaded local algorithm harness; production uses real pthreads.
static int lock;
static void pthread_mutex_lock(int *unused){(void)unused;}
static void pthread_mutex_unlock(int *unused){(void)unused;}
#else
#include <pthread.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
#endif
static int clamp8(int x){return x<0?0:x>255?255:x;}
#include "night.h"
#include "pixel-rotation.h"
#include "capture-quality.h"
#include "luma-enhance.h"
#include "chroma-copy.h"
#ifdef _WIN32
#define CACHE "native/cache/processing-fixtures"
#else
#define CACHE "/tmp/snapphoto-processing-test"
#endif
static unsigned playback_generation,playback_presented;
#include "playback-frames.h"
static void verify_playback(void){
#ifdef _WIN32
 _mkdir(CACHE);
#else
 mkdir(CACHE,0700);
#endif
 const char *source=CACHE "/source.jpg";FILE*f=fopen(source,"wb");assert(f);fwrite("\xff\xd8\0\xff\xd9",1,5,f);fclose(f);
 playback_generation=(unsigned)getpid();playback_presented=0;char path[256],first[256],latest[256];
 assert(playback_present(source,path));snprintf(first,sizeof(first),"%s",path);
 // Deleting decoder input cannot remove the independently owned UI frame.
 unlink(source);assert(complete_jpeg(first));
 f=fopen(source,"wb");assert(f);fwrite("\xff\xd8",1,2,f);fclose(f);
 assert(!playback_present(source,path));assert(playback_presented==1&&complete_jpeg(first));
 f=fopen(source,"wb");assert(f);fwrite("\xff\xd8\0\xff\xd9",1,5,f);fclose(f);
 for(int i=1;i<65;i++){assert(playback_present(source,latest));assert(complete_jpeg(latest));assert(strcmp(first,latest));}
 assert(!complete_jpeg(first));assert(playback_presented==65);
 for(unsigned i=0;i<65;i++){
  snprintf(path,sizeof(path),CACHE "/play-view-%u-%08u.jpg",playback_generation,i);
  assert(complete_jpeg(path)==(i>=33));unlink(path);
 }
 unlink(source);puts("playback: complete snapshots, input deletion, partial frames, unique URIs and 32-frame bound passed");
}
static unsigned noise_seed=109;
static int noise(void){noise_seed=noise_seed*1664525U+1013904223U;return (int)((noise_seed>>24)%33)-16;}
static double variance(const unsigned char *p,int w,int h){
 double sum=0,squares=0,n=0;for(int y=4;y<h-4;y++)for(int x=4;x<w-4;x++){double a=p[y*w+x];sum+=a;squares+=a*a;n++;}
 return squares/n-(sum/n)*(sum/n);
}
static void verify_noise(void){
 enum{w=96,h=64,yn=w*h,un=w*h/4};unsigned char source[yn+2*un],quality[sizeof(source)],speed[sizeof(source)],lut[256];
 for(int i=0;i<yn;i++)source[i]=32+noise();
 for(int i=yn;i<yn+2*un;i++)source[i]=128+noise();
 memcpy(quality,source,sizeof(source));memcpy(speed,source,sizeof(source));
 night_enabled=1;night_quality=1;night_strength=.45;assert(night_snapshot(lut));
 for(int i=1;i<256;i++)assert(lut[i]>=lut[i-1]&&lut[i]-lut[i-1]<=2);
 night_process_yuv(quality,w,h,0,lut,1);night_process_yuv(speed,w,h,0,lut,0);
 double y_ratio=variance(quality,w,h)/variance(speed,w,h),uv_ratio=variance(quality+yn,w/2,h/2)/variance(speed+yn,w/2,h/2);
 assert(y_ratio<.40&&uv_ratio<.30);
 printf("quality/speed noise variance: luma=%.4f chroma=%.4f\n",y_ratio,uv_ratio);
 // High-contrast text edge: smoothing must not bleed across it.
 for(int y=0;y<h;y++)for(int x=0;x<w;x++)source[y*w+x]=x<w/2?24:200;
 night_spatial(source,w,h,0);
 for(int y=1;y<h-1;y++){assert(source[y*w+w/2-1]==24);assert(source[y*w+w/2]==200);}
 // Small/odd planes and guards, including off mode, must not overrun.
 for(int x=1;x<12;x++)for(int y=1;y<12;y++){
  size_t size=(size_t)x*y+2*(size_t)((x+1)/2)*((y+1)/2);unsigned char *p=malloc(size+16);memset(p,0xa5,size+16);
  night_process_yuv(p+8,x,y,0,lut,1);
  for(int k=0;k<8;k++)assert(p[k]==0xa5&&p[size+8+k]==0xa5);free(p);
 }
 night_enabled=0;night_snapshot(lut);memcpy(quality,speed,sizeof(speed));night_process_yuv(quality,w,h,0,lut,0);assert(!memcmp(quality,speed,sizeof(speed)));
 // Optimized ARM NEON must be byte-identical to the scalar reference,
 // including unaligned spans, odd widths, threshold boundaries and tails.
 for(int w=3;w<69;w++)for(int h=3;h<19;h++)for(int chroma=0;chroma<2;chroma++){
  unsigned char *a=malloc(w*h),*b=malloc(w*h);assert(a&&b);
  for(int i=0;i<w*h;i++){noise_seed=noise_seed*1664525U+1013904223U;a[i]=(noise_seed>>24)&255;}memcpy(b,a,w*h);
  night_spatial_impl(a,w,h,chroma,0);night_spatial_impl(b,w,h,chroma,1);assert(!memcmp(a,b,w*h));free(a);free(b);
 }
 for(int w=3;w<69;w++)for(int h=3;h<19;h++){
  unsigned char a[69*19],b[69*19];for(int i=0;i<w*h;i++){noise_seed=noise_seed*1664525U+1013904223U;a[i]=(noise_seed>>24)&255;}memcpy(b,a,w*h);
  enhance_luma_impl(a,w,h,0);enhance_luma_impl(b,w,h,1);assert(!memcmp(a,b,w*h));
 }
 unsigned char uv[80*32];for(size_t i=0;i<sizeof(uv);i++)uv[i]=(i*37+13)&255;
 for(int w=3;w<69;w++)for(int h=3;h<19;h++)for(int top=0;top<2;top++){
  size_t un=(size_t)((w+1)/2)*((h+1)/2);unsigned char a[69*19],b[69*19];memset(a,0xa5,sizeof(a));memset(b,0xa5,sizeof(b));
  copy_photo_chroma(uv,80,64,2,top,w,h,a,a+un,0);copy_photo_chroma(uv,80,64,2,top,w,h,b,b+un,1);assert(!memcmp(a,b,sizeof(a)));
 }
 for(int length=1;length<300;length++){
  unsigned char a[316],b[316],curve[256];for(int i=0;i<256;i++)curve[i]=(i*37+13)&255;
  memset(a,0xa5,sizeof(a));memset(b,0xa5,sizeof(b));for(int i=0;i<length;i++){a[i+8]=i&255;b[i+8]=curve[i&255];}
  night_apply_curve(a+8,length,curve);assert(!memcmp(a,b,sizeof(a)));
 }
}
int main(void){
 double scores[]={42,43,42.5,42};assert(focus_scores_stable(scores,4,.01));assert(!focus_scores_stable(scores,3,.01));
 scores[3]=12;assert(!focus_scores_stable(scores,4,.01));scores[3]=42;assert(!focus_scores_stable(scores,4,.4));
 assert(focus_cache_usable(100,42,43,80,81,.01,1));assert(!focus_cache_usable(1500,42,43,80,81,.01,1));
 assert(!focus_cache_usable(100,42,43,80,81,.01,0));assert(!focus_cache_usable(100,12,43,80,81,.01,1));
 assert(!focus_cache_usable(100,42,43,30,81,.01,1));assert(!focus_cache_usable(100,42,43,80,81,.4,1));
 assert(capture_replace(-1,10,0));assert(capture_replace(10,12,0));assert(!capture_replace(10,9,0));assert(!capture_replace(10,5,.3));
 assert(capture_frames(1,1,-1,1)==1);assert(capture_frames(0,0,100,.001)==1);assert(capture_frames(0,1,100,.001)==3);
 assert(capture_frames(0,0,-1,.001)==3);assert(capture_frames(0,0,100,.1)==3);assert(capture_frames(0,0,800,.001)==3);
 const unsigned char src[]={1,2,3,4,5,6},expected90[]={5,3,1,6,4,2},expected270[]={2,4,6,1,3,5},expected180[]={6,5,4,3,2,1};unsigned char out[6],back[6];
 rotate_pixels(src,out,2,3,1,90);assert(!memcmp(out,expected90,6));rotate_pixels(out,back,3,2,1,270);assert(!memcmp(src,back,6));
 rotate_pixels(src,out,2,3,1,270);assert(!memcmp(out,expected270,6));rotate_pixels(src,out,2,3,1,180);assert(!memcmp(out,expected180,6));
 unsigned char rgb[18],rotated[18];for(int i=0;i<6;i++)memset(rgb+i*3,src[i],3);rotate_pixels(rgb,rotated,2,3,3,90);for(int i=0;i<6;i++)for(int k=0;k<3;k++)assert(rotated[i*3+k]==expected90[i]);
 for(int w=7;w<34;w++)for(int h=7;h<34;h++)for(int angle=90;angle<=270;angle+=90){
  unsigned char a[34*34],b[34*34];for(int i=0;i<w*h;i++)a[i]=(i*37+13)%251;
  rotate_pixels(a,b,w,h,1,angle);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   size_t at=angle==90?(size_t)x*h+h-1-y:angle==180?(size_t)(h-1-y)*w+w-1-x:(size_t)(w-1-x)*h+y;
   assert(b[at]==a[y*w+x]);
  }
 }
 unsigned char input[64*32],lut[256];memset(input,24,sizeof(input));
 for(int n=1;n<120;n++)night_observe(input,64,64,32,n*33.3);
 assert(night_active&&night_strength>.1);night_snapshot(lut);assert(lut[24]>24&&lut[0]==0&&lut[255]==255);
 for(int i=1;i<256;i++)assert(lut[i]>=lut[i-1]);
 memset(input,180,sizeof(input));for(int n=120;n<300;n++)night_observe(input,64,64,32,n*33.3);
 assert(!night_active&&night_strength<.001);
 night_enabled=0;night_snapshot(lut);for(int i=0;i<256;i++)assert(lut[i]==i);
 verify_noise();verify_playback();puts("pixel rotation, night hysteresis, noise, edge and plane guards passed");return 0;
}

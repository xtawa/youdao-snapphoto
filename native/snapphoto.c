#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <poll.h>
#include <dirent.h>
#include <spawn.h>
#include <dlfcn.h>
#include <linux/videodev2.h>
#include <linux/input.h>
#include "cJSON.h"
#include "turbojpeg.h"
#ifndef SNAP_CLI
#include "quickjs.h"
#endif
#define GALLERY "/userdisk/Pictures"
#define STATE "/userdisk/snapphoto"
#define CACHE "/tmp/snapphoto"
#define MAX_FILES 256
#ifndef SNAP_SENSOR_HEIGHT
#define SNAP_SENSOR_HEIGHT 2448
#endif
extern char **environ;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_t camera_thread,share_thread;
static pthread_cond_t frame_cond=PTHREAD_COND_INITIALIZER;
static int want_photo;static unsigned long raw_sequence;static double frame_age_ms;
static int photo_fast,photo_candidates;static double photo_best_rank,photo_best_score,photo_selection_started;
static void focus_observe(const unsigned char *,int);
static void focus_shutdown(void);
static int camera_started,quit_camera,ready,frame_id,photo_id,video_fd=-1,video_stop,share_fd=-1,share_client=-1,share_running;
static pid_t video_pid;
static int width,height,stride,video_mode=0,zoom=100,filter,photo_profile=0,portrait_mode=0,preview_width=640,preview_height=192,photo_width=3264,photo_height=979;
static int camera_rotation;
static int record_profile=1,record_width=1280,record_height=720,record_target_fps=30;
static unsigned char *latest,*zsl_frame;
static struct timespec zsl_time;static double zsl_at,zsl_rank,zsl_score;
static struct timespec latest_time,video_time;
static pthread_t photo_thread;static int photo_phase;static cJSON *photo_result;
static cJSON *fail(const char *);static cJSON *ok(void);
static cJSON *command(cJSON *);static void *photo_main(void *);static void await_photo(void);
#include "photo-time.h"
struct photo_profile { const char *id; int width,height; };
static const struct photo_profile photo_profiles[]={{"wide",3264,979},{"cinema",3264,1362},{"two-one",3264,1632},{"sixteen-nine",3264,1836},{"four-three",3264,2448},{"square",2448,2448}};
#define PHOTO_PROFILE_COUNT ((int)(sizeof(photo_profiles)/sizeof(photo_profiles[0])))
static unsigned long frames_seen,frames_encoded;static double convert_ms,encode_ms,stream_started;static char latest_preview[256];
static char camera_error[180],video_path[256],share_ip[64],share_token[49];
static char shared[MAX_FILES][4096];static int shared_count;
static time_t share_until;
static void *jpeg_lib;
static tjhandle (*jpeg_init)(void);
static int (*jpeg_compress)(tjhandle,const unsigned char*,int,int,int,int,unsigned char**,unsigned long*,int,int,int);
static int (*jpeg_yuv)(tjhandle,const unsigned char**,int,const int*,int,int,unsigned char**,unsigned long*,int,int);
static int (*jpeg_destroy)(tjhandle);static void (*jpeg_free)(unsigned char*);
static int load_jpeg(void){if(jpeg_lib)return 1;jpeg_lib=dlopen("libturbojpeg.so.0",RTLD_NOW|RTLD_LOCAL);if(!jpeg_lib)return 0;
#define LOAD(v,n) *(void**)(&v)=dlsym(jpeg_lib,n);if(!v)return 0
 LOAD(jpeg_init,"tjInitCompress");LOAD(jpeg_compress,"tjCompress2");LOAD(jpeg_destroy,"tjDestroy");LOAD(jpeg_free,"tjFree");*(void**)(&jpeg_yuv)=dlsym(jpeg_lib,"tjCompressFromYUVPlanes");return 1;}
static int xio(int fd,unsigned long cmd,void*p){int r;do{r=ioctl(fd,cmd,p);}while(r<0&&errno==EINTR);return r;}
static double now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
static int clamp8(int x){return x<0?0:x>255?255:x;}
#include "night.h"
#include "pixel-rotation.h"
#include "capture-quality.h"
#include "luma-enhance.h"
#include "chroma-copy.h"
static void filter_rgb(int effect,int *r,int *g,int *b,int yy){
 int rr=*r,gg=*g,bb=*b;
 if(effect==1){*r=*g=*b=yy;return;}
 if(effect==2){*r=clamp8((rr*101+gg*197+bb*48)>>8);*g=clamp8((rr*89+gg*176+bb*43)>>8);*b=clamp8((rr*70+gg*137+bb*34)>>8);return;}
 if(effect==3){*r=clamp8(((rr*276+gg*12)>>8)+8);*g=clamp8((gg*256+rr*8)>>8);*b=clamp8(((bb*220+gg*10)>>8)-4);return;}
 if(effect==4){*r=clamp8(((rr*220+gg*8)>>8)-4);*g=clamp8((gg*245+bb*8)>>8);*b=clamp8(((bb*276+gg*12)>>8)+8);return;}
 if(effect==5){int y=(rr*77+gg*150+bb*29)>>8;*r=clamp8(((y*210+rr*30)>>8)+12);*g=clamp8(((y*190+gg*40)>>8)+4);*b=clamp8(((y*160+bb*60)>>8)-8);return;}
 if(effect==6){int y=(rr*77+gg*150+bb*29)>>8;*r=clamp8(y+((rr-y)*3>>1)+8);*g=clamp8(y+((gg-y)*3>>1));*b=clamp8(y+((bb-y)*3>>1));return;}
 if(effect==7){*r=clamp8(255-rr);*g=clamp8(255-gg);*b=clamp8(255-bb);}
}
/* ISP supplies full-range NV12. Crop and scale before color conversion, then
 * use the system's SIMD TurboJPEG; no model or NPU inference is required. */
static int bilinear(const unsigned char *p,int pitch,int step,int x,int y,int fx,int fy){
 int a=p[y*pitch+x*step],b=p[y*pitch+(x+1)*step];
 int c=p[(y+1)*pitch+x*step],d=p[(y+1)*pitch+(x+1)*step];
 return ((a*(256-fx)+b*fx)*(256-fy)+(c*(256-fx)+d*fx)*fy+32768)>>16;
}
/* Shared raw YUV photo sampling lets every filter denoise before tone/color
 * processing, including chroma. The V4L2 source is never modified. */
static unsigned char *photo_planes(const unsigned char *nv,int w,int h,int z,int mono,int *denoise){
 int uw=(w+1)/2,uh=(h+1)/2;size_t yn=(size_t)w*h,un=(size_t)uw*uh;
 unsigned char *buf=malloc(yn+un*2);if(!buf)return NULL;unsigned char *u=buf+yn,*v=u+un;
 int cw=width*100/z,ch=height*100/z;if(cw*h>ch*w)cw=ch*w/h;else ch=cw*h/w;
 int x0=(width-cw)/2,y0=(height-ch)/2;
 for(int y=0;y<h;y++){
  if(cw==w&&ch==h)memcpy(buf+(size_t)y*w,nv+(size_t)(y0+y)*width+x0,w);
  else{int yq=y0*256+(int)((int64_t)y*(ch-1)*256/(h>1?h-1:1)),sy=yq>>8,fy=yq&255;if(sy>=height-1){sy=height-2;fy=256;}
   for(int x=0;x<w;x++){int xq=x0*256+(int)((int64_t)x*(cw-1)*256/(w>1?w-1:1)),sx=xq>>8,fx=xq&255;if(sx>=width-1){sx=width-2;fx=256;}buf[y*w+x]=bilinear(nv,width,1,sx,sy,fx,fy);}}
 }
 if(mono){memset(u,128,un);memset(v,128,un);}
 else if(cw==w&&ch==h&&!(x0&1))copy_photo_chroma(nv+(size_t)width*height,width,height,x0,y0,w,h,u,v,1);
 else for(int y=0;y<uh;y++)for(int x=0;x<uw;x++){
  int xq=x0*128+(int)((int64_t)(x*2)*(cw-1)*128/(w>1?w-1:1));
  int yq=y0*128+(int)((int64_t)(y*2)*(ch-1)*128/(h>1?h-1:1));
  int sx=xq>>8,sy=yq>>8,fx=xq&255,fy=yq&255;
  if(sx>=width/2-1){sx=width/2-2;fx=256;}if(sy>=height/2-1){sy=height/2-2;fy=256;}
  const unsigned char *uv=nv+(size_t)width*height;
  u[y*uw+x]=bilinear(uv,width,2,sx,sy,fx,fy);v[y*uw+x]=bilinear(uv+1,width,2,sx,sy,fx,fy);
 }
 unsigned char lut[256];*denoise=night_snapshot(lut);night_process_yuv(buf,w,h,mono,lut,*denoise);return buf;
}
static unsigned char* rgb_frame(const unsigned char*nv,int ow,int oh,int z,int effect,int enhance){
 int denoise=0;unsigned char *planes=photo_planes(nv,ow,oh,z,effect==1,&denoise);if(!planes)return NULL;
 unsigned char*out=malloc((size_t)ow*oh*3);if(!out){free(planes);return NULL;}
 size_t yn=(size_t)ow*oh,un=(size_t)((ow+1)/2)*((oh+1)/2);int uw=(ow+1)/2;
 for(int y=0;y<oh;y++){
  for(int x=0;x<ow;x++){
   int yy=planes[(size_t)y*ow+x],u=planes[yn+(size_t)(y/2)*uw+x/2]-128,v=planes[yn+un+(size_t)(y/2)*uw+x/2]-128;
   int r=clamp8(yy+((359*v)>>8)),g=clamp8(yy-((88*u+183*v)>>8)),b=clamp8(yy+((454*u)>>8));
   if(effect)filter_rgb(effect,&r,&g,&b,yy);
   size_t k=((size_t)y*ow+x)*3;out[k]=r;out[k+1]=g;out[k+2]=b;
  }
 }
 free(planes);
 if(enhance&&!denoise&&ow>2&&oh>2){
  unsigned char*luma=malloc((size_t)ow*oh);
  if(luma){
   for(int i=0;i<ow*oh;i++)luma[i]=(out[i*3]*77+out[i*3+1]*150+out[i*3+2]*29)>>8;
   for(int y=1;y<oh-1;y++)for(int x=1;x<ow-1;x++){
    int at=y*ow+x,c=luma[at],sum=c*4,n=4;
    int neighbors[]={luma[at-ow],luma[at+ow],luma[at-1],luma[at+1]};
    for(int k=0;k<4;k++)if(abs(neighbors[k]-c)<=8){sum+=neighbors[k];n++;}
    int smooth=(sum+n/2)/n,edge=c-(neighbors[0]+neighbors[1]+neighbors[2]+neighbors[3]+2)/4;
    /* Suppress small flat-region noise; thresholded, bounded luma sharpening. */
    int sharp=abs(edge)>=3&&abs(edge)<=32?edge/4:0;
    if(sharp>6)sharp=6;if(sharp< -6)sharp=-6;
    int add=smooth-c+sharp;
    for(int k=0;k<3;k++)out[at*3+k]=clamp8(out[at*3+k]+add);
   }
   free(luma);
  }
 }
 return out;
}
struct video_profile {const char *id;int width,height,portrait_width,portrait_height,fps;};
static const struct video_profile video_profiles[]={{"compact",640,192,216,720,30},{"hd",1280,720,720,1280,30},{"fhd",1920,1080,1080,1920,24}};
#define VIDEO_PROFILE_COUNT 3
static void output_geometry(void){
 photo_width=portrait_mode?photo_profiles[photo_profile].height:photo_profiles[photo_profile].width;
 photo_height=portrait_mode?photo_profiles[photo_profile].width:photo_profiles[photo_profile].height;
 preview_height=portrait_mode?720:192;
 record_target_fps=video_profiles[record_profile].fps;
 record_width=portrait_mode?video_profiles[record_profile].portrait_width:video_profiles[record_profile].width;
 record_height=portrait_mode?video_profiles[record_profile].portrait_height:video_profiles[record_profile].height;
 preview_width=portrait_mode?((720*photo_width/photo_height+1)&~1):video_mode?640:((192*photo_width/photo_height+1)&~1);
 if(video_mode&&record_profile){preview_width=portrait_mode?360:320;preview_height=portrait_mode?640:180;}
}
static void avi_frame(const unsigned char*,unsigned long);
static int record_encoder_start(int,int);static void record_encoder_stop(void);static void record_submit(const unsigned char*,int,int,int);
static int write_jpeg_bytes(const char *path,const unsigned char *out,unsigned long n,const struct timespec *taken){
 FILE*f=fopen(path,"wb");if(!f)return 0;int ok;
 if(taken&&n>=2&&out[0]==0xff&&out[1]==0xd8){unsigned char exif[256];size_t len=photo_exif(exif,taken);ok=fwrite(out,1,2,f)==2&&fwrite(exif,1,len,f)==len&&fwrite(out+2,1,n-2,f)==n-2;}else ok=fwrite(out,1,n,f)==n;
 if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;return ok;
}
static int jpeg_write(const char*path,const unsigned char*rgb,int w,int h,int quality,const struct timespec *taken){
 if(!load_jpeg())return 0;tjhandle c=jpeg_init();if(!c)return 0;unsigned char*out=NULL;unsigned long n=0;
 int ok=jpeg_compress(c,rgb,w,0,h,TJPF_RGB,&out,&n,TJSAMP_420,quality,TJFLAG_FASTDCT)==0;
 if(ok)ok=write_jpeg_bytes(path,out,n,taken);
 if(out)jpeg_free(out);jpeg_destroy(c);return ok;
}
/* Keep natural/mono captures in YUV. No RGB round trip, no sensor changes. */
static int jpeg_nv12_photo(const char *path,const unsigned char *nv,int w,int h,int z,int mono,const struct timespec *taken,double *processing,int rotation){
 int final_w=w,final_h=h;if(rotation%180){int tmp=w;w=h;h=tmp;}
 int uw=(w+1)/2,uh=(h+1)/2;size_t yn=(size_t)w*h,un=(size_t)uw*uh;
 int denoise=0;unsigned char *buf=photo_planes(nv,w,h,z,mono,&denoise);if(!buf)return 0;
 if(!denoise)enhance_luma(buf,w,h);*processing=now_ms();
 unsigned char *rotated=NULL;
 if(rotation){rotated=malloc(yn+un*2);if(!rotated){free(buf);return 0;}rotate_yuv(buf,rotated,w,h,rotation);}
 const unsigned char *pixels=rotated?rotated:buf;w=final_w;h=final_h;uw=(w+1)/2;
 const unsigned char *planes[]={pixels,pixels+yn,pixels+yn+un};int strides[]={w,uw,uw};tjhandle c=jpeg_init();unsigned char *out=NULL;unsigned long n=0;
 int good=c&&jpeg_yuv(c,planes,w,strides,h,TJSAMP_420,&out,&n,93,TJFLAG_FASTDCT)==0;
 if(good)good=write_jpeg_bytes(path,out,n,taken);if(out)jpeg_free(out);if(c)jpeg_destroy(c);free(rotated);free(buf);return good;
}
static void set_error(const char*s){pthread_mutex_lock(&lock);snprintf(camera_error,sizeof(camera_error),"%s: %s",s,strerror(errno));pthread_mutex_unlock(&lock);}
static void le32(FILE*f,uint32_t n){unsigned char b[]={n,n>>8,n>>16,n>>24};fwrite(b,1,4,f);}
static void four(FILE*f,const char*n){fwrite(n,1,4,f);}
static void patch32(FILE*f,long pos,uint32_t n){long at=ftell(f);fseek(f,pos,SEEK_SET);le32(f,n);fseek(f,at,SEEK_SET);}
#include "recording.h"
#include "autofocus.h"
#include "preview-pipeline.h"
#include "record-encoder.h"
static void *camera_main(void*unused){
 (void)unused;int fd=-1,streaming=0;void*buffers[4]={0};size_t lengths[4]={0};int count=0;struct preview_encoder preview={0};struct v4l2_format oldfmt={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};struct v4l2_selection oldcrop={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.target=V4L2_SEL_TGT_CROP};int havefmt=0,havecrop=0;
 fd=open("/dev/video30",O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd<0){set_error("Open camera");goto end;}
 havefmt=xio(fd,VIDIOC_G_FMT,&oldfmt)==0;havecrop=xio(fd,VIDIOC_G_SELECTION,&oldcrop)==0;
 struct v4l2_selection crop={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.target=V4L2_SEL_TGT_CROP,.r={0,(2448-SNAP_SENSOR_HEIGHT)/2,3264,SNAP_SENSOR_HEIGHT}};
 if(xio(fd,VIDIOC_S_SELECTION,&crop)<0){set_error("Camera crop");goto end;}
 struct v4l2_format fmt={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};fmt.fmt.pix_mp.width=3264;fmt.fmt.pix_mp.height=SNAP_SENSOR_HEIGHT;fmt.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;fmt.fmt.pix_mp.field=V4L2_FIELD_NONE;
 if(xio(fd,VIDIOC_S_FMT,&fmt)<0){set_error("Camera format");goto end;}width=fmt.fmt.pix_mp.width;height=fmt.fmt.pix_mp.height;stride=fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
 if(width!=3264||height!=SNAP_SENSOR_HEIGHT||stride<width||fmt.fmt.pix_mp.num_planes!=1){errno=EINVAL;set_error("Unsupported NV12 format");goto end;}
 struct v4l2_requestbuffers req={.count=4,.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,.memory=V4L2_MEMORY_MMAP};
 if(xio(fd,VIDIOC_REQBUFS,&req)<0){set_error("Camera buffers");goto end;}count=req.count>4?4:req.count;
 for(int i=0;i<count;i++){struct v4l2_plane p[VIDEO_MAX_PLANES]={0};struct v4l2_buffer b={.type=req.type,.memory=req.memory,.index=i,.length=VIDEO_MAX_PLANES,.m.planes=p};
 if(xio(fd,VIDIOC_QUERYBUF,&b)<0){set_error("Query camera buffer");goto end;}lengths[i]=p[0].length;buffers[i]=mmap(NULL,lengths[i],PROT_READ|PROT_WRITE,MAP_SHARED,fd,p[0].m.mem_offset);if(buffers[i]==MAP_FAILED){buffers[i]=NULL;set_error("Map camera buffer");goto end;}if(xio(fd,VIDIOC_QBUF,&b)<0){set_error("Queue camera buffer");goto end;}}
 enum v4l2_buf_type type=req.type;if(xio(fd,VIDIOC_STREAMON,&type)<0){set_error("Start camera");goto end;}streaming=1;pthread_mutex_lock(&lock);stream_started=now_ms();frames_seen=frames_encoded=0;raw_sequence=0;pthread_mutex_unlock(&lock);
 double next=0;int preview_slot=0;unsigned long generation=(unsigned long)now_ms();preview_generation=generation;
 while(1){
  pthread_mutex_lock(&lock);int quit=quit_camera;pthread_mutex_unlock(&lock);if(quit)break;
  struct pollfd po={fd,POLLIN,0};if(poll(&po,1,200)<=0)continue;
  struct v4l2_plane p[VIDEO_MAX_PLANES]={0};struct v4l2_buffer b={.type=req.type,.memory=req.memory,.length=VIDEO_MAX_PLANES,.m.planes=p};
  if(xio(fd,VIDIOC_DQBUF,&b)<0){if(errno==EAGAIN)continue;set_error("Read camera");break;}
  pthread_mutex_lock(&lock);frames_seen++;pthread_mutex_unlock(&lock);double t=now_ms();
  if(b.index<(unsigned)count&&p[0].bytesused>=(size_t)stride*height*3/2&&p[0].data_offset+(size_t)stride*height*3/2<=lengths[b.index]){
   unsigned char *src=(unsigned char*)buffers[b.index]+p[0].data_offset;
   pthread_mutex_lock(&lock);
   raw_sequence++;ready=1;
   int z=zoom,f=filter,pw=preview_width,ph=preview_height;
   pthread_mutex_unlock(&lock);
   night_observe(src,stride,width,height,t);
   focus_observe(src,stride);
   pthread_mutex_lock(&lock);
   /* Keep one recent good full-resolution frame. Transfer ownership at shutter,
    * so a tap does not wait for AF or copy a large image on the UI thread. */
   double rank=capture_rank(af_score,af_motion);
   if(!zsl_frame||t-zsl_at>150||(t-zsl_at>65&&(af_motion>.12||rank>zsl_rank*1.05))){
    if(!zsl_frame)zsl_frame=malloc((size_t)width*height*3/2);
    if(zsl_frame){for(int y=0;y<height*3/2;y++)memcpy(zsl_frame+(size_t)y*width,src+(size_t)y*stride,width);clock_gettime(CLOCK_REALTIME,&zsl_time);zsl_at=t;zsl_rank=rank;zsl_score=af_score;}
   }
   if(want_photo){
    photo_candidates++;
    if(!latest||capture_replace(photo_best_rank,af_score,af_motion)){
     if(!latest)latest=malloc((size_t)width*height*3/2);
     if(latest){
      for(int y=0;y<height*3/2;y++)memcpy(latest+(size_t)y*width,src+(size_t)y*stride,width);
      clock_gettime(CLOCK_REALTIME,&latest_time);photo_best_rank=capture_rank(af_score,af_motion);photo_best_score=af_score;
     }
    }
    want_photo--;
   }
   pthread_cond_broadcast(&frame_cond);pthread_mutex_unlock(&lock);
   record_submit(src,stride,z,f);
   if(t>=next){
    next=next?next+1000.0/30.0:t+1000.0/30.0;if(next<t-1000.0/30.0)next=t+1000.0/30.0;double began=now_ms();unsigned char *jpeg=NULL;unsigned long n=0;
    if(preview_encode(&preview,src,stride,pw,ph,z,f,&jpeg,&n)){
     double conversion=now_ms()-began,writing=now_ms();
     char tmp[256],dest[256],oldpath[256];int seq=++preview_slot;
     snprintf(tmp,sizeof(tmp),CACHE "/preview-%lu-%d.tmp",generation,seq);snprintf(dest,sizeof(dest),CACHE "/preview-%lu-%d.jpg",generation,seq);
     FILE *file=fopen(tmp,"wb");int good=file&&fwrite(jpeg,1,n,file)==n;if(file&&fclose(file))good=0;

     if(good&&!rename(tmp,dest)){
      pthread_mutex_lock(&lock);snprintf(latest_preview,sizeof(latest_preview),"%s",dest);frame_id=seq;frames_encoded++;frame_age_ms=now_ms()-t;convert_ms=conversion;encode_ms=now_ms()-writing;pthread_mutex_unlock(&lock);
     }else unlink(tmp);
     if(seq>32){snprintf(oldpath,sizeof(oldpath),CACHE "/preview-%lu-%d.jpg",generation,seq-32);unlink(oldpath);}

    }
    if(jpeg)jpeg_free(jpeg);
   }
  }
  if(xio(fd,VIDIOC_QBUF,&b)<0){set_error("Return camera buffer");break;}
 }
end:
 if(streaming){enum v4l2_buf_type type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;xio(fd,VIDIOC_STREAMOFF,&type);}for(int i=0;i<count;i++)if(buffers[i])munmap(buffers[i],lengths[i]);
 if(fd>=0){struct v4l2_requestbuffers r={.count=0,.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,.memory=V4L2_MEMORY_MMAP};xio(fd,VIDIOC_REQBUFS,&r);if(havecrop)xio(fd,VIDIOC_S_SELECTION,&oldcrop);if(havefmt)xio(fd,VIDIOC_S_FMT,&oldfmt);close(fd);}preview_destroy(&preview);focus_preview_end();return NULL;}
static cJSON*fail(const char*s){cJSON*j=cJSON_CreateObject();cJSON_AddStringToObject(j,"error",s);return j;}
static cJSON*ok(void){return cJSON_CreateObject();}
static const char*str(cJSON*j,const char*k){cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsString(v)?v->valuestring:"";}
static int media_kind(const char*n){const char*e=strrchr(n,'.');if(!e)return 0;if(!strcasecmp(e,".jpg")||!strcasecmp(e,".jpeg")||!strcasecmp(e,".png"))return 1;if(!strcasecmp(e,".mp4")||!strcasecmp(e,".avi"))return 2;return 0;}
static int valid_media(const char*path,char*resolved){struct stat st;return realpath(path,resolved)&&!strncmp(resolved,GALLERY "/",strlen(GALLERY "/"))&&!strchr(resolved+strlen(GALLERY "/"),'/')&&media_kind(resolved)&&!stat(resolved,&st)&&S_ISREG(st.st_mode);}
static cJSON*saved(const char*path){struct stat st;if(stat(path,&st)||st.st_size<100)return fail("Camera output missing or incomplete");cJSON*j=ok();cJSON_AddStringToObject(j,"path",path);cJSON_AddBoolToObject(j,"written",1);cJSON_AddBoolToObject(j,"indexed",0);cJSON_AddNumberToObject(j,"size",st.st_size);return j;}
static void filename(char*out,const char*prefix,const char*ext,const struct timespec *taken){struct timespec ts=*taken;struct tm tm;localtime_r(&ts.tv_sec,&tm);char date[40];strftime(date,sizeof(date),"%Y%m%d_%H%M%S",&tm);snprintf(out,256,GALLERY "/%s_%s_%03ld_%d.%s",prefix,date,ts.tv_nsec/1000000,++photo_id,ext);}
#include "media-browser.h"
#include "npu-enhancement.h"
#include "background-queue.h"
static cJSON*stop_video(void){
 if(!video_pid)return fail("Not recording");
 double duration=0;int good=finish_avi(&duration);video_pid=0;
 cJSON *j=good?saved(video_path):fail("Recording incomplete; file kept");
 cJSON_AddStringToObject(j,"path",video_path);cJSON_AddBoolToObject(j,"audio",avi_samples>0);
 cJSON_AddNumberToObject(j,"audioSamples",avi_samples);cJSON_AddNumberToObject(j,"audioDurationMs",avi_samples*1000.0/16000);cJSON_AddNumberToObject(j,"durationMs",duration);
 cJSON_AddNumberToObject(j,"width",record_width);cJSON_AddNumberToObject(j,"height",record_height);cJSON_AddNumberToObject(j,"droppedFrames",record_dropped);cJSON_AddNumberToObject(j,"videoFrames",avi_frames);cJSON_AddNumberToObject(j,"audioXruns",audio_xruns);cJSON_AddNumberToObject(j,"audioLastError",audio_last_error);cJSON_AddNumberToObject(j,"audioZeroReads",audio_zero_reads);if(record_limit)cJSON_AddStringToObject(j,"stopReason",record_limit==2?"low-space":"file-limit");
 return j;
}
static void close_camera(void){queue_foreground(0);enhance_shutdown();stop_playback();thumbnail_join();pthread_mutex_lock(&lock);quit_camera=1;pthread_cond_broadcast(&frame_cond);int started=camera_started,rec=video_pid!=0;pthread_mutex_unlock(&lock);if(rec){cJSON*j=stop_video();cJSON_Delete(j);}focus_shutdown();await_photo();if(started)pthread_join(camera_thread,NULL);pthread_mutex_lock(&lock);camera_started=ready=frame_id=0;free(latest);latest=NULL;free(zsl_frame);zsl_frame=NULL;zsl_at=0;want_photo=0;pthread_mutex_unlock(&lock);preview_cleanup();}
static void http_reply(int fd,int status,const char*type,const void*body,size_t n){char h[512];int k=snprintf(h,sizeof(h),"HTTP/1.1 %d OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",status,type,n);send(fd,h,k,MSG_NOSIGNAL);const char*p=body;while(n){ssize_t w=send(fd,p,n,MSG_NOSIGNAL);if(w<=0)break;p+=w;n-=w;}}
#include "avi-preview.h"
#include "share-page.h"
static void* share_main(void*unused){
 (void)unused;
 while(1){
  pthread_mutex_lock(&lock);int srv=share_fd;time_t until=share_until;pthread_mutex_unlock(&lock);if(srv<0||time(NULL)>=until)break;
  struct pollfd po={srv,POLLIN,0};if(poll(&po,1,250)<=0)continue;int fd=accept4(srv,NULL,NULL,SOCK_CLOEXEC);if(fd<0)continue;
  struct timeval tv={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
  pthread_mutex_lock(&lock);share_client=fd;pthread_mutex_unlock(&lock);
  char req[4096]={0},method[12],url[2048];int n=recv(fd,req,sizeof(req)-1,0);
  if(n>0&&sscanf(req,"%11s %2047s",method,url)==2&&!strcmp(method,"GET")){
   char base[80];snprintf(base,sizeof(base),"/%s",share_token);int bl=strlen(base);
   if(strncmp(url,base,bl)||(url[bl]&&url[bl]!='/'&&url[bl]!='?'))http_reply(fd,403,"text/plain","Forbidden",9);
   else if(!strcmp(url+bl,"")||!strcmp(url+bl,"/")){
    size_t length=0;char *body=share_html(&length);if(body){http_reply(fd,200,"text/html; charset=utf-8",body,length);free(body);}else http_reply(fd,500,"text/plain","Memory unavailable",18);
   }else if(!strcmp(url+bl,"/metadata.json")){
    char *body=export_manifest();if(body){http_reply(fd,200,"application/json; charset=utf-8",body,strlen(body));free(body);}else http_reply(fd,500,"text/plain","Memory unavailable",18);
   }else{
    int vi=-1,usedv=0;unsigned vf=0;int kind=-1;
    if(sscanf(url+bl,"/v/%d%n",&vi,&usedv)==1&&!url[bl+usedv])kind=0;
    else if(sscanf(url+bl,"/j/%d/%u%n",&vi,&vf,&usedv)==2&&!url[bl+usedv])kind=1;
    else if(sscanf(url+bl,"/a/%d%n",&vi,&usedv)==1&&!url[bl+usedv])kind=2;
    if(kind>=0&&vi>=0&&vi<shared_count){char real[4096];if(valid_media(shared[vi],real)&&!strcmp(real,shared[vi]))send_avi_preview(fd,real,kind,vf,req);else http_reply(fd,404,"text/plain","Missing",7);goto request_done;}
    int ix=-1,used=0,inline_photo=0;
    int matched=sscanf(url+bl,"/f/%d%n",&ix,&used)==1;
    if(!matched){used=0;matched=sscanf(url+bl,"/p/%d%n",&ix,&used)==1;inline_photo=1;}
    if(matched&&!url[bl+used]&&ix>=0&&ix<shared_count){
     char real[4096];if(valid_media(shared[ix],real)&&!strcmp(real,shared[ix]))send_media(fd,real,inline_photo,req);else http_reply(fd,404,"text/plain","Missing",7);
    }else http_reply(fd,404,"text/plain","Missing",7);
   }
  }else if(n>0)http_reply(fd,405,"text/plain","GET required",12);
request_done:
  pthread_mutex_lock(&lock);share_client=-1;pthread_mutex_unlock(&lock);close(fd);
 }
 pthread_mutex_lock(&lock);if(share_fd>=0){close(share_fd);share_fd=-1;}pthread_mutex_unlock(&lock);return NULL;
}
static void stop_share(void){pthread_mutex_lock(&lock);share_until=0;if(share_client>=0)shutdown(share_client,SHUT_RDWR);int run=share_running;pthread_mutex_unlock(&lock);if(run)pthread_join(share_thread,NULL);share_running=0;shared_count=0;preview_avi_forget();memset(share_token,0,sizeof(share_token));}
static int lan_ip(char*out){struct ifaddrs*a;if(getifaddrs(&a))return 0;int found=0;for(struct ifaddrs*p=a;p;p=p->ifa_next){if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_INET||strcmp(p->ifa_name,"wlan0"))continue;struct sockaddr_in*s=(void*)p->ifa_addr;uint32_t n=ntohl(s->sin_addr.s_addr);if((n>>24)==10||(n>>16)==0xc0a8||(n>>20)==0xac1){inet_ntop(AF_INET,&s->sin_addr,out,64);found=1;break;}}freeifaddrs(a);return found;}
static cJSON*command(cJSON*o){const char*op=str(o,"op");mkdir(STATE,0700);mkdir(CACHE,0700);mkdir(GALLERY,0755);
 cJSON *queued=queue_command(o,op);if(queued)return queued;
 cJSON *enhanced=enhance_command(o,op);if(enhanced)return enhanced;
 cJSON *media=media_command(o,op);if(media)return media;
 if(!strcmp(op,"night")){const char*m=str(o,"mode"),*p=str(o,"priority");if((strcmp(m,"auto")&&strcmp(m,"off"))||(strcmp(p,"speed")&&strcmp(p,"quality")))return fail("Invalid night policy");pthread_mutex_lock(&lock);night_enabled=!strcmp(m,"auto");night_quality=!strcmp(p,"quality");if(!night_enabled){night_active=0;night_strength=0;}pthread_mutex_unlock(&lock);return ok();}
 if(!strcmp(op,"focusStart"))return focus_start(o);
 if(!strcmp(op,"focusResult"))return focus_result();
 if(!strcmp(op,"sensor")){cJSON*j=ok();int fd=open("/dev/input/event7",O_RDONLY|O_NONBLOCK|O_CLOEXEC),good=fd>=0;char name[80]={0};if(good){good=ioctl(fd,EVIOCGNAME(sizeof(name)),name)>=0&&!strcmp(name,"gsensor_orient");}for(int i=0;i<3&&good;i++){struct input_absinfo a={0};if(ioctl(fd,EVIOCGABS(ABS_RX+i),&a)<0){good=0;break;}cJSON_AddNumberToObject(j,i==0?"x":i==1?"y":"z",a.value);}if(fd>=0)close(fd);cJSON_AddBoolToObject(j,"available",good);return j;}
 if(!strcmp(op,"caps")){cJSON*j=ok();int have=load_jpeg()&&access("/dev/video30",R_OK|W_OK)==0;cJSON_AddBoolToObject(j,"camera",have);cJSON_AddBoolToObject(j,"preview",have);cJSON_AddBoolToObject(j,"photo",have);cJSON_AddBoolToObject(j,"video",have&&load_audio());cJSON_AddBoolToObject(j,"audio",load_audio());cJSON *vp=cJSON_CreateArray();for(int i=0;i<VIDEO_PROFILE_COUNT;i++){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"id",video_profiles[i].id);cJSON_AddNumberToObject(p,"width",video_profiles[i].width);cJSON_AddNumberToObject(p,"height",video_profiles[i].height);cJSON_AddNumberToObject(p,"portraitWidth",video_profiles[i].portrait_width);cJSON_AddNumberToObject(p,"portraitHeight",video_profiles[i].portrait_height);cJSON_AddNumberToObject(p,"targetFps",video_profiles[i].fps);cJSON_AddItemToArray(vp,p);}cJSON_AddItemToObject(j,"videoProfiles",vp);cJSON_AddBoolToObject(j,"gallery",1);cJSON_AddBoolToObject(j,"zoom",1);cJSON_AddBoolToObject(j,"focus",0);cJSON_AddBoolToObject(j,"continuousFocus",lens_available());cJSON_AddBoolToObject(j,"zeroShutterLag",1);cJSON_AddBoolToObject(j,"share",1);cJSON_AddNumberToObject(j,"minZoom",1);cJSON_AddNumberToObject(j,"maxZoom",3);cJSON*a=cJSON_CreateArray();const char*filter_ids[]={"none","mono","sepia","warm","cool","vintage","vivid","negative"};for(int i=0;i<8;i++)cJSON_AddItemToArray(a,cJSON_CreateString(filter_ids[i]));cJSON_AddItemToObject(j,"filters",a);cJSON*pp=cJSON_CreateArray();for(int i=0;i<PHOTO_PROFILE_COUNT;i++){cJSON*p=cJSON_CreateObject();cJSON_AddStringToObject(p,"id",photo_profiles[i].id);cJSON_AddNumberToObject(p,"width",photo_profiles[i].width);cJSON_AddNumberToObject(p,"height",photo_profiles[i].height);cJSON_AddItemToArray(pp,p);}cJSON_AddItemToObject(j,"photoProfiles",pp);cJSON_AddBoolToObject(j,"hdr",0);int np=npu_check();cJSON_AddBoolToObject(j,"npu",np);cJSON_AddBoolToObject(j,"npuReady",np);cJSON_AddBoolToObject(j,"enhancement",np);char worker[4096];cJSON_AddBoolToObject(j,"backgroundEnhancement",np&&queue_worker_path(worker)&&access(worker,R_OK)==0);cJSON_AddStringToObject(j,"npuBackend",np?"rknn-ffdnet-int8":"none");return j;}
 if(!strcmp(op,"open")){queue_foreground(1);if(camera_started)return ok();preview_cleanup();pthread_mutex_lock(&lock);quit_camera=0;ready=frame_id=0;camera_error[0]=0;pthread_mutex_unlock(&lock);if(pthread_create(&camera_thread,NULL,camera_main,NULL))return fail("Cannot create camera thread");camera_started=1;return ok();}
 if(!strcmp(op,"frame")){
  pthread_mutex_lock(&lock);cJSON*j=camera_error[0]?fail(camera_error):ok();
  if(!camera_error[0]&&frame_id){
   cJSON_AddStringToObject(j,"image",latest_preview);cJSON_AddNumberToObject(j,"sequence",frame_id);cJSON_AddBoolToObject(j,"nightActive",night_strength>.01);cJSON_AddNumberToObject(j,"sceneLuma",night_mean);cJSON_AddNumberToObject(j,"nightStrength",night_strength);
   cJSON_AddNumberToObject(j,"sourceWidth",width);cJSON_AddNumberToObject(j,"sourceHeight",height);cJSON_AddNumberToObject(j,"sourceStride",stride);
   double elapsed=now_ms()-stream_started;if(elapsed<1)elapsed=1;
   cJSON_AddNumberToObject(j,"captureFps",frames_seen*1000.0/elapsed);cJSON_AddNumberToObject(j,"previewFps",frames_encoded*1000.0/elapsed);
   cJSON_AddNumberToObject(j,"frameAgeMs",frame_age_ms);cJSON_AddNumberToObject(j,"rawSequence",raw_sequence);
   cJSON_AddNumberToObject(j,"convertMs",convert_ms);cJSON_AddNumberToObject(j,"encodeMs",encode_ms);
   pthread_mutex_lock(&record_lock);cJSON_AddBoolToObject(j,"recording",video_pid!=0);cJSON_AddBoolToObject(j,"recordError",video_pid&&video_stop);cJSON_AddBoolToObject(j,"recordLimit",record_limit!=0);pthread_mutex_unlock(&record_lock);
   pthread_mutex_lock(&jobs_lock);cJSON_AddNumberToObject(j,"recordDropped",record_dropped);cJSON_AddNumberToObject(j,"recordEncodeMs",record_encode_ms);cJSON_AddNumberToObject(j,"recordSampleMs",record_sample_ms);pthread_mutex_unlock(&jobs_lock);
  }
  pthread_mutex_unlock(&lock);return j;
 }
 if(!strcmp(op,"close")){close_camera();return ok();}
 if(!strcmp(op,"orientation")){if(avi)return fail("Stop recording before rotating");int portrait=!strcmp(str(o,"value"),"portrait");int rotation=(int)cJSON_GetNumberValue(cJSON_GetObjectItem(o,"rotation"));if(portrait)rotation=rotation==270?270:90;else rotation=rotation==180?180:0;pthread_mutex_lock(&lock);portrait_mode=portrait;camera_rotation=rotation;output_geometry();pthread_mutex_unlock(&lock);return ok();}
 if(!strcmp(op,"videoProfile")){
  int ix=-1;for(int i=0;i<VIDEO_PROFILE_COUNT;i++)if(!strcmp(str(o,"value"),video_profiles[i].id))ix=i;
  if(ix<0)return fail("Unsupported video profile");if(avi)return fail("Stop recording before changing video quality");
  pthread_mutex_lock(&lock);record_profile=ix;output_geometry();int w=record_width,h=record_height;pthread_mutex_unlock(&lock);
  cJSON *j=ok();cJSON_AddNumberToObject(j,"width",w);cJSON_AddNumberToObject(j,"height",h);return j;
 }
 if(!strcmp(op,"photoProfile")){const char*n=str(o,"value");int ix=-1;for(int i=0;i<PHOTO_PROFILE_COUNT;i++)if(!strcmp(n,photo_profiles[i].id))ix=i;if(ix<0)return fail("Unsupported photo profile");if(avi)return fail("Stop recording before changing ratio");pthread_mutex_lock(&lock);photo_profile=ix;output_geometry();pthread_mutex_unlock(&lock);cJSON*j=ok();cJSON_AddStringToObject(j,"id",photo_profiles[ix].id);cJSON_AddNumberToObject(j,"width",photo_profiles[ix].width);cJSON_AddNumberToObject(j,"height",photo_profiles[ix].height);return j;}
 if(!strcmp(op,"zoom")){double n=cJSON_GetNumberValue(cJSON_GetObjectItem(o,"value"));if(n<1||n>3)return fail("Zoom must be between 1 and 3");pthread_mutex_lock(&lock);zoom=(int)(n*100);pthread_mutex_unlock(&lock);return ok();}
 if(!strcmp(op,"filter")){const char*n=str(o,"value");int f=!strcmp(n,"none")?0:!strcmp(n,"mono")?1:!strcmp(n,"sepia")?2:!strcmp(n,"warm")?3:!strcmp(n,"cool")?4:!strcmp(n,"vintage")?5:!strcmp(n,"vivid")?6:!strcmp(n,"negative")?7:-1;if(f<0)return fail("Unsupported filter");pthread_mutex_lock(&lock);filter=f;pthread_mutex_unlock(&lock);return ok();}
 if(!strcmp(op,"captureMode")){const char *m=str(o,"value");if(strcmp(m,"photo")&&strcmp(m,"video"))return fail("Unknown capture mode");if(avi)return fail("Stop recording before changing mode");pthread_mutex_lock(&lock);video_mode=!strcmp(m,"video");output_geometry();pthread_mutex_unlock(&lock);return ok();}
 if(!strcmp(op,"photoStart")){
  pthread_mutex_lock(&lock);if(photo_phase||!ready||avi||enhance_busy()){pthread_mutex_unlock(&lock);return fail("Camera busy or frame not ready");}photo_phase=1;photo_fast=cJSON_IsTrue(cJSON_GetObjectItem(o,"fast"));pthread_mutex_unlock(&lock);
  if(pthread_create(&photo_thread,NULL,photo_main,NULL)){pthread_mutex_lock(&lock);photo_phase=0;pthread_mutex_unlock(&lock);return fail("Cannot start photo processing");}return ok();
 }
 if(!strcmp(op,"photoResult")){
  pthread_mutex_lock(&lock);int phase=photo_phase;cJSON *result=photo_result;
  if(phase==2){photo_result=NULL;photo_phase=0;}pthread_mutex_unlock(&lock);
  if(phase==1){cJSON *j=ok();cJSON_AddBoolToObject(j,"pending",1);return j;}
  if(phase==2){pthread_join(photo_thread,NULL);return result?result:fail("Photo processing failed");}
  return fail("No pending photo");
 }
 if(!strcmp(op,"photo")){
  pthread_mutex_lock(&lock);
  if(!camera_started||quit_camera){pthread_mutex_unlock(&lock);return fail("Camera is closed");}
  int cached=zsl_frame&&now_ms()-zsl_at>=0&&now_ms()-zsl_at<=200;
  if(cached){free(latest);latest=zsl_frame;zsl_frame=NULL;latest_time=zsl_time;}
  want_photo=cached?0:1;
  photo_candidates=cached?1:0;photo_best_rank=cached?zsl_rank:-1;photo_best_score=cached?zsl_score:0;photo_selection_started=now_ms();
  struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=1;
  while(want_photo&&!quit_camera)if(pthread_cond_timedwait(&frame_cond,&lock,&deadline)==ETIMEDOUT)break;
  unsigned char *copy=latest;latest=NULL;if(want_photo||quit_camera){free(copy);copy=NULL;}want_photo=0;
  int z=zoom,effect=filter,ow=photo_width,oh=photo_height,rotation=camera_rotation,candidates=photo_candidates;struct timespec taken=latest_time;
  double selection_ms=now_ms()-photo_selection_started,selection_score=photo_best_score;
  pthread_mutex_unlock(&lock);
  if(!copy)return fail("No recent camera frame available; retry");double started=now_ms(),processing_done=started;char p[256];filename(p,"SNAP","JPG",&taken);int good=0;
  if((effect==0||effect==1)&&load_jpeg()&&jpeg_yuv)good=jpeg_nv12_photo(p,copy,ow,oh,z,effect==1,&taken,&processing_done,rotation);
  else{int sw=rotation%180?oh:ow,sh=rotation%180?ow:oh;unsigned char*rgb=rgb_frame(copy,sw,sh,z,effect,1);if(rgb){unsigned char *rotated=NULL;if(rotation){rotated=malloc((size_t)ow*oh*3);if(rotated)rotate_pixels(rgb,rotated,sw,sh,3,rotation);}processing_done=now_ms();if(!rotation||rotated)good=jpeg_write(p,rotated?rotated:rgb,ow,oh,93,&taken);free(rotated);free(rgb);}}
  free(copy);
  if(!good){unlink(p);return fail("JPEG save failed");}cJSON *j=saved(p);char date[20],iso[48],zone[7];time_strings(&taken,date,iso,zone);
  cJSON_AddBoolToObject(j,"zeroShutterLag",cached);cJSON_AddNumberToObject(j,"candidateFrames",candidates);cJSON_AddNumberToObject(j,"selectionMs",selection_ms);cJSON_AddNumberToObject(j,"sharpnessScore",selection_score);
  cJSON_AddNumberToObject(j,"processingMs",processing_done-started);cJSON_AddNumberToObject(j,"saveMs",now_ms()-processing_done);cJSON_AddStringToObject(j,"capturedAt",iso);cJSON_AddNumberToObject(j,"capturedAtEpochMs",taken.tv_sec*1000.0+taken.tv_nsec/1e6);cJSON_AddStringToObject(j,"timeSource","device-frame-received");return j;
 }
 if(!strcmp(op,"record")){if(enhance_busy())return fail("NPU photo processing busy");
  if(!ready)return fail("Waiting for a camera frame");
  if(avi||video_pid||photo_phase)return fail("Camera busy or already recording");
  clock_gettime(CLOCK_REALTIME,&video_time);filename(video_path,"SNAP","avi",&video_time);
#ifdef SNAP_CLI
  cJSON *limit=cJSON_GetObjectItem(o,"maxBytes");record_limit_bytes=cJSON_IsNumber(limit)&&limit->valuedouble>=1048576&&limit->valuedouble<0x70000000L?(long)limit->valuedouble:0x70000000L;
#endif
  if(!start_avi(video_path))return fail(audio_error[0]?audio_error:"Cannot open video file");
  video_pid=1;cJSON *j=ok();cJSON_AddBoolToObject(j,"audio",1);cJSON_AddNumberToObject(j,"audioRate",16000);return j;
 }
 if(!strcmp(op,"stopRecord"))return stop_video();
 if(!strcmp(op,"list"))return media_listing();
 if(!strcmp(op,"share")){stop_share();cJSON*a=cJSON_GetObjectItem(o,"paths");int n=cJSON_GetArraySize(a);if(n<1||n>MAX_FILES)return fail("Select between 1 and 256 files");for(int i=0;i<n;i++){cJSON*v=cJSON_GetArrayItem(a,i);if(!cJSON_IsString(v)||!valid_media(v->valuestring,shared[i]))return fail("Only existing gallery media can be shared");}shared_count=n;int have_lan=lan_ip(share_ip);
#ifdef SNAP_CLI
 if(cJSON_IsTrue(cJSON_GetObjectItem(o,"loopback"))){snprintf(share_ip,sizeof(share_ip),"127.0.0.1");have_lan=1;}
#endif
 if(!have_lan)return fail("Connect to a private Wi-Fi network");unsigned char rnd[24];int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);int nr=fd<0?-1:read(fd,rnd,sizeof(rnd));if(fd>=0)close(fd);if(nr!=sizeof(rnd))return fail("Secure random source unavailable");for(int i=0;i<24;i++)sprintf(share_token+i*2,"%02x",rnd[i]);int srv=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);struct sockaddr_in addr={.sin_family=AF_INET};inet_pton(AF_INET,share_ip,&addr.sin_addr);if(srv<0||bind(srv,(void*)&addr,sizeof(addr))||listen(srv,4)){if(srv>=0)close(srv);return fail("Cannot open LAN share");}socklen_t len=sizeof(addr);getsockname(srv,(void*)&addr,&len);share_fd=srv;share_until=time(NULL)+600;if(pthread_create(&share_thread,NULL,share_main,NULL)){close(srv);share_fd=-1;return fail("Cannot create share thread");}share_running=1;cJSON*j=ok();char url[200];snprintf(url,sizeof(url),"http://%s:%d/%s/",share_ip,ntohs(addr.sin_port),share_token);cJSON_AddStringToObject(j,"url",url);cJSON_AddNumberToObject(j,"expiresIn",600);return j;}
 if(!strcmp(op,"stopShare")){stop_share();return ok();}
 if(!strcmp(op,"qr")){const char*v=str(o,"value"),*prefix="data:image/png;base64,";if(strncmp(v,prefix,strlen(prefix))||strlen(v)>300000)return fail("Invalid QR image");v+=strlen(prefix);size_t len=strlen(v),n=0;unsigned char*b=malloc(len);if(!b)return fail("QR memory unavailable");unsigned int acc=0;int bits=0;for(size_t i=0;i<len;i++){if(v[i]=='=')break;const char*alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";const char*t=strchr(alphabet,v[i]);if(!t){free(b);return fail("Invalid QR encoding");}acc=(acc<<6)|(t-alphabet);bits+=6;if(bits>=8){bits-=8;b[n++]=(acc>>bits)&255;}}if(n<8||memcmp(b,"\x89PNG\r\n\x1a\n",8)){free(b);return fail("Invalid PNG");}char qp[256];snprintf(qp,sizeof(qp),CACHE "/share-qr-%lu.png",(unsigned long)now_ms());FILE*f=fopen(qp,"wb");int good=f&&fwrite(b,1,n,f)==n;if(f)fclose(f);free(b);if(!good)return fail("Cannot save QR");cJSON*j=ok();cJSON_AddStringToObject(j,"path",qp);return j;}
 if(!strcmp(op,"readPrefs")){FILE*f=fopen(STATE "/preferences.json","rb");if(!f)return ok();char b[4096]={0};fread(b,1,sizeof(b)-1,f);fclose(f);cJSON*j=cJSON_Parse(b);return j?j:ok();}
 if(!strcmp(op,"writePrefs")){cJSON*j=cJSON_GetObjectItem(o,"value");char*s=cJSON_PrintUnformatted(j);if(!s)return fail("Invalid preferences");FILE*f=fopen(STATE "/preferences.tmp","wb");int good=f&&strlen(s)<4096&&fwrite(s,1,strlen(s),f)==strlen(s);free(s);if(f){fflush(f);fsync(fileno(f));fclose(f);}if(!good||rename(STATE "/preferences.tmp",STATE "/preferences.json"))return fail("Cannot save preferences");return ok();}
 return fail("Unknown operation");}
static void *photo_main(void *unused){
 (void)unused;cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"op","photo");cJSON_AddBoolToObject(o,"fast",photo_fast);cJSON *result=command(o);cJSON_Delete(o);
 pthread_mutex_lock(&lock);photo_result=result;photo_phase=2;pthread_mutex_unlock(&lock);return NULL;
}
static void await_photo(void){
 pthread_mutex_lock(&lock);int phase=photo_phase;pthread_mutex_unlock(&lock);
 if(phase){pthread_join(photo_thread,NULL);pthread_mutex_lock(&lock);cJSON_Delete(photo_result);photo_result=NULL;photo_phase=0;pthread_mutex_unlock(&lock);}
}
#ifdef SNAP_CLI
int main(int argc,char **argv){if(argc==3&&!strcmp(argv[1],"--queue-worker"))return queue_worker_main(argv[2]);char*line=NULL;size_t cap=0;while(getline(&line,&cap,stdin)>0){cJSON*o=cJSON_Parse(line);if(o&&!strcmp(str(o,"op"),"quit")){cJSON_Delete(o);break;}cJSON*r=o?command(o):fail("Invalid JSON");char*s=cJSON_PrintUnformatted(r);puts(s?s:"{}");fflush(stdout);free(s);cJSON_Delete(r);cJSON_Delete(o);}free(line);stop_share();close_camera();return 0;}
#else
static JSValue js_command(JSContext*c,JSValueConst th,int argc,JSValueConst*argv){(void)th;const char*s=argc?JS_ToCString(c,argv[0]):NULL;if(!s)return JS_EXCEPTION;cJSON*o=cJSON_Parse(s);JS_FreeCString(c,s);cJSON*r=o?command(o):fail("Invalid JSON");char*out=cJSON_PrintUnformatted(r);JSValue v=JS_NewString(c,out?out:"{}");free(out);cJSON_Delete(o);cJSON_Delete(r);return v;}
static const JSCFunctionListEntry funcs[]={JS_CFUNC_DEF("command",1,js_command)};
static int module_init(JSContext*c,JSModuleDef*m){JSValue o=JS_NewObject(c);JS_SetPropertyFunctionList(c,o,funcs,1);return JS_SetModuleExport(c,m,"default",o);}
static JSModuleDef*loader(JSContext*c,const char*n){JSModuleDef*m=JS_NewCModule(c,n,module_init);if(m)JS_AddModuleExport(c,m,"default");return m;}
extern void registerCModuleLoader(const char*,JSModuleDef*(*)(JSContext*,const char*));
static JSValue stock_unavailable(JSContext*c,JSValueConst self,int argc,JSValueConst*argv){(void)self;(void)argc;(void)argv;return JS_ThrowInternalError(c,"Stock album module unavailable; photo retained");}
static JSValue stock_event_noop(JSContext*c,JSValueConst self,int argc,JSValueConst*argv){(void)c;(void)self;(void)argc;(void)argv;return JS_UNDEFINED;}
static JSValue stock_fallback_constructor(JSContext*c,JSValueConst target,int argc,JSValueConst*argv){
 (void)target;(void)argc;(void)argv;JSValue o=JS_NewObject(c),event=JS_NewObject(c);
 JS_SetPropertyStr(c,event,"on",JS_NewCFunction(c,stock_event_noop,"on",1));JS_SetPropertyStr(c,event,"off",JS_NewCFunction(c,stock_event_noop,"off",1));
 JS_SetPropertyStr(c,o,"onRequestDataResult",event);JS_SetPropertyStr(c,o,"requestData",JS_NewCFunction(c,stock_unavailable,"requestData",2));return o;
}
static int stock_fallback_init(JSContext*c,JSModuleDef*m){JSValue o=JS_NewObject(c);JS_SetPropertyStr(c,o,"PhotoAlbum",JS_NewCFunction2(c,stock_fallback_constructor,"PhotoAlbum",0,JS_CFUNC_constructor,0));return JS_SetModuleExport(c,m,"default",o);}
static JSModuleDef *stock_fallback_loader(JSContext*c,const char*n){JSModuleDef*m=JS_NewCModule(c,n,stock_fallback_init);if(m)JS_AddModuleExport(c,m,"default");return m;}
/* The album module is supplied by the installed stock album, not firmware
 * globals. Reuse its own loader without redistributing or editing its files.
 * Keep handles pinned: its C-module callbacks outlive the current page. */
static void load_stock_album(void){
 static void *business,*api;
 /* Falcon clears module registrations when this app is destroyed, while
  * dlopen handles may remain pinned. Register again on EVERY app launch. */
 const char *root="/userdisk/secondary/miniapp/data/mini_app/pkg/8001718963156066";
 for(int slot=0;slot<2&&!api;slot++){
  char dir[256];snprintf(dir,sizeof(dir),"%s/%c/libs",root,'a'+slot);DIR*d=opendir(dir);if(!d)continue;
  char business_path[512]={0},api_path[512]={0};struct dirent*e;
  while((e=readdir(d))){
   if(!strncmp(e->d_name,"libbusiness_photo_album_",24)&&strstr(e->d_name,".so"))snprintf(business_path,sizeof(business_path),"%s/%s",dir,e->d_name);
   if(!strncmp(e->d_name,"libjsapi_photo_album_",21)&&strstr(e->d_name,".so"))snprintf(api_path,sizeof(api_path),"%s/%s",dir,e->d_name);
  }closedir(d);
  if(business_path[0])business=dlopen(business_path,RTLD_NOW|RTLD_GLOBAL);
  if(business&&api_path[0])api=dlopen(api_path,RTLD_NOW|RTLD_LOCAL);
 }
 if(api){void(*init)(void)=dlsym(api,"custom_init_jsapis");if(init){init();return;}}
 registerCModuleLoader("photoAlbum",stock_fallback_loader);
}
__attribute__((visibility("default"))) void custom_init_jsapis(void){load_stock_album();registerCModuleLoader("snapphoto",loader);}
__attribute__((visibility("default"))) void custom_init_jsmodules(void){}
__attribute__((visibility("default"))) int jsapi_check_unload(void){stop_share();close_camera();return 1;}
#endif

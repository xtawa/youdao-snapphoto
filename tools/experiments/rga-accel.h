/* Optional Rockchip RGA2 acceleration. Vendor C ABI, owned memory only.
 * Missing/unsupported hardware always falls back to the existing CPU path. */
#include "../../native/vendor/rga/im2d_type.h"
#include "../../native/vendor/rga/im2d_buffer.h"
#include "../../native/vendor/rga/im2d_common.h"
#include "../../native/vendor/rga/im2d_single.h"
#include "dma-buffer.h"
static pthread_once_t rga_once=PTHREAD_ONCE_INIT;
static void *rga_lib;
static rga_buffer_handle_t (*rga_import)(void*,im_handle_param_t*);
static rga_buffer_handle_t (*rga_import_fd)(int,im_handle_param_t*);
static IM_STATUS (*rga_release)(rga_buffer_handle_t);
static rga_buffer_t (*rga_wrap)(rga_buffer_handle_t,int,int,int,int,int);
static IM_STATUS (*rga_check)(rga_buffer_t,rga_buffer_t,rga_buffer_t,im_rect,im_rect,im_rect,int);
static IM_STATUS (*rga_process)(rga_buffer_t,rga_buffer_t,rga_buffer_t,im_rect,im_rect,im_rect,int);
static int rga_available,rga_enabled=1;
static unsigned long rga_successes,rga_fallbacks;
static void rga_load(void){
 rga_lib=dlopen("librga.so.2",RTLD_NOW|RTLD_LOCAL);if(!rga_lib)return;
#define RGA_SYMBOL(target,name) *(void**)(&target)=dlsym(rga_lib,name);if(!target)return
 RGA_SYMBOL(rga_import,"importbuffer_virtualaddr");RGA_SYMBOL(rga_release,"releasebuffer_handle");
 RGA_SYMBOL(rga_import_fd,"importbuffer_fd");
 RGA_SYMBOL(rga_wrap,"wrapbuffer_handle_t");RGA_SYMBOL(rga_check,"imcheck_t");RGA_SYMBOL(rga_process,"improcess");
#undef RGA_SYMBOL
 rga_available=1;
}
struct rga_source {const void *address;rga_buffer_handle_t handle;int pitch,height,fd;};
struct rga_context {
 struct rga_source sources[4];unsigned source_next;
 void *pixels;size_t bytes;rga_buffer_handle_t output;
 int stride,height,w,h,z,rotation,disabled,allocation_width;
 struct dma_image dma;
};
static void rga_context_destroy(struct rga_context *ctx){
 for(int i=0;i<4;i++)if(ctx->sources[i].handle)rga_release(ctx->sources[i].handle);
 if(ctx->output)rga_release(ctx->output);
 if(ctx->dma.handle)dma_image_destroy(&ctx->dma);else free(ctx->pixels);memset(ctx,0,sizeof(*ctx));
}
static int rga_scale_yuv(struct rga_context *ctx,const unsigned char *source,int source_width,int source_height,int pitch,
 unsigned char *destination,int w,int h,int zoom_percent,int rotation,int source_fd){
 if(!rga_enabled||(w&1)||(h&1)||w<2||h<2||w>4096||h>4096||pitch<source_width)return 0;
 pthread_once(&rga_once,rga_load);if(!rga_available)return 0;
 int geometry=ctx->w!=w||ctx->h!=h||ctx->z!=zoom_percent||ctx->rotation!=rotation;
 if(geometry){ctx->disabled=0;ctx->w=w;ctx->h=h;ctx->z=zoom_percent;ctx->rotation=rotation;}
 if(ctx->disabled)return 0;
 int stride_out=(w+15)&~15;
 if(ctx->allocation_width!=w||ctx->height!=h){
  if(ctx->output)rga_release(ctx->output);ctx->output=0;
  if(ctx->dma.handle)dma_image_destroy(&ctx->dma);else free(ctx->pixels);ctx->pixels=NULL;
  ctx->allocation_width=w;ctx->stride=stride_out;ctx->height=h;ctx->bytes=((size_t)stride_out*h*3/2+4095)&~(size_t)4095;
  if(dma_image_create(&ctx->dma,stride_out,h)){
   ctx->stride=stride_out=ctx->dma.pitch;ctx->bytes=ctx->dma.bytes;ctx->pixels=ctx->dma.data;
  }else{
   if(posix_memalign(&ctx->pixels,4096,ctx->bytes)){ctx->pixels=NULL;ctx->disabled=1;return 0;}
   memset(ctx->pixels,0,ctx->bytes);
  }
  im_handle_param_t param={.width=stride_out,.height=h,.format=RK_FORMAT_YCbCr_420_P};
  ctx->output=ctx->dma.handle?rga_import_fd(ctx->dma.fd,&param):rga_import(ctx->pixels,&param);
  if(!ctx->output){ctx->disabled=1;return 0;}
 }
 stride_out=ctx->stride;
 struct rga_source *input=NULL;
 for(int i=0;i<4;i++)if(ctx->sources[i].address==source&&ctx->sources[i].pitch==pitch&&ctx->sources[i].height==source_height&&ctx->sources[i].fd==source_fd){input=&ctx->sources[i];break;}
 if(!input){
  input=&ctx->sources[ctx->source_next++%4];if(input->handle)rga_release(input->handle);
  *input=(struct rga_source){.address=source,.pitch=pitch,.height=source_height,.fd=source_fd};
  im_handle_param_t param={.width=pitch,.height=source_height,.format=RK_FORMAT_YCbCr_420_SP};
  input->handle=source_fd>=0?rga_import_fd(source_fd,&param):rga_import((void*)source,&param);
  if(!input->handle){ctx->disabled=1;return 0;}
 }
 rga_buffer_t src=rga_wrap(input->handle,source_width,source_height,pitch,source_height,RK_FORMAT_YCbCr_420_SP);
 rga_buffer_t dst=rga_wrap(ctx->output,w,h,stride_out,h,RK_FORMAT_YCbCr_420_P),empty={0};
 int sw=rotation%180?h:w,sh=rotation%180?w:h,cw=source_width*100/zoom_percent,ch=source_height*100/zoom_percent;
 if((int64_t)cw*sh>(int64_t)ch*sw)cw=ch*sw/sh;else ch=cw*sh/sw;
 cw&=~1;ch&=~1;if(cw<2||ch<2)return 0;
 im_rect crop={((source_width-cw)/2)&~1,((source_height-ch)/2)&~1,cw,ch},target={0,0,w,h},none={0};
 int flags=IM_SYNC|(rotation==90?IM_HAL_TRANSFORM_ROT_90:rotation==180?IM_HAL_TRANSFORM_ROT_180:rotation==270?IM_HAL_TRANSFORM_ROT_270:0);
 if(geometry&&rga_check(src,dst,empty,crop,target,none,flags)<0){ctx->disabled=1;rga_fallbacks++;return 0;}
 if(rga_process(src,dst,empty,crop,target,none,flags)!=IM_STATUS_SUCCESS){ctx->disabled=1;rga_fallbacks++;return 0;}
 const unsigned char *raw=ctx->pixels;size_t pixels=(size_t)w*h;
 if(!dma_image_sync(&ctx->dma,0,1)){ctx->disabled=1;return 0;}
 for(int y=0;y<h;y++)memcpy(destination+(size_t)y*w,raw+(size_t)y*stride_out,w);
 for(int plane=0;plane<2;plane++)for(int y=0;y<h/2;y++)
  memcpy(destination+pixels+(size_t)plane*pixels/4+(size_t)y*w/2,
    raw+(size_t)stride_out*h+(size_t)plane*stride_out*h/4+(size_t)y*stride_out/2,w/2);
 if(!dma_image_sync(&ctx->dma,0,0)){ctx->disabled=1;return 0;}
 rga_successes++;return 1;
}

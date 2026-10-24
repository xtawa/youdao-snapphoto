/* Application-owned DRM dumb memory; never touches display modes/planes. */
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-buf.h>
#ifndef __user
#define __user
#endif
#include "../../native/vendor/drm/drm.h"
#include "../../native/vendor/drm/drm_mode.h"
#include "../../native/vendor/drm/rockchip_drm.h"
struct dma_image {int drm,fd,cached;uint32_t handle,pitch;void *data;size_t bytes;};
static void dma_image_destroy(struct dma_image *b){
 if(b->data)munmap(b->data,b->bytes);
 if(b->handle){if(b->fd>=0)close(b->fd);struct drm_mode_destroy_dumb destroy={.handle=b->handle};ioctl(b->drm,DRM_IOCTL_MODE_DESTROY_DUMB,&destroy);}
 if(b->drm>=0)close(b->drm);*b=(struct dma_image){.drm=-1,.fd=-1};
}
static int dma_image_create_mode(struct dma_image *b,int width,int height,int cache){
 *b=(struct dma_image){.drm=-1,.fd=-1};b->drm=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);if(b->drm<0)return 0;
 int pitch=(width+15)&~15;size_t size=((size_t)pitch*height*3/2+4095)&~(size_t)4095;
 struct drm_rockchip_gem_create cached={.size=size,.flags=ROCKCHIP_BO_CACHABLE};
 if(cache&&!ioctl(b->drm,DRM_IOCTL_ROCKCHIP_GEM_CREATE,&cached)){
  b->handle=cached.handle;b->pitch=pitch;b->bytes=size;b->cached=1;
 }else{
  struct drm_mode_create_dumb create={.width=width,.height=height*3/2,.bpp=8};
  if(ioctl(b->drm,DRM_IOCTL_MODE_CREATE_DUMB,&create))goto fail;
  b->handle=create.handle;b->pitch=create.pitch;b->bytes=create.size;
 }
 struct drm_prime_handle prime={.handle=b->handle,.flags=DRM_CLOEXEC|DRM_RDWR,.fd=-1};
 if(ioctl(b->drm,DRM_IOCTL_PRIME_HANDLE_TO_FD,&prime))goto fail;b->fd=prime.fd;
 struct drm_mode_map_dumb map={.handle=b->handle};if(ioctl(b->drm,DRM_IOCTL_MODE_MAP_DUMB,&map))goto fail;
 b->data=mmap(NULL,b->bytes,PROT_READ|PROT_WRITE,MAP_SHARED,b->drm,map.offset);
 if(b->data==MAP_FAILED){b->data=NULL;goto fail;}return 1;
fail:dma_image_destroy(b);return 0;
}
static int dma_image_create(struct dma_image *b,int width,int height){return dma_image_create_mode(b,width,height,1);}
static int dma_image_sync(struct dma_image *b,int writing,int start){
 if(!b->handle)return 1;
 struct dma_buf_sync sync={.flags=(writing?DMA_BUF_SYNC_WRITE:DMA_BUF_SYNC_READ)|(start?DMA_BUF_SYNC_START:DMA_BUF_SYNC_END)};
 return ioctl(b->fd,DMA_BUF_IOCTL_SYNC,&sync)==0;
}

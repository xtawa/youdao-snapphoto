/* Camera-thread-owned buffers and JPEG context. Never copy the full sensor
 * frame for preview; sample its crop directly from the V4L2 mapping. */
struct preview_encoder {
 tjhandle codec;
 unsigned char *yuv,*rgb,*scanline,*rotation_scratch;
 size_t capacity,rgb_capacity;
 int xs[1920],ys[1920],w,h,z,sw,sh,mw,mh;
 size_t rotation_capacity;
};
static int sample_prepare_unrotated(struct preview_encoder *e,const unsigned char *nv,int pitch,
 int w,int h,int z,int effect){
 if(w<2||h<2||w>1920||h>1920||!load_jpeg())return 0;
 if(!e->scanline)e->scanline=malloc((size_t)width*2);if(!e->scanline)return 0;
 size_t pixels=(size_t)w*h;
 if(e->capacity<pixels){
  unsigned char *y=malloc(pixels*2);if(!y)return 0;
  free(e->yuv);e->yuv=y;e->capacity=pixels;
 }
 if(!((effect==0||effect==1)&&jpeg_yuv)&&e->rgb_capacity<pixels){
  unsigned char *rgb=malloc(pixels*3);if(!rgb)return 0;free(e->rgb);e->rgb=rgb;e->rgb_capacity=pixels;
 }
 if(e->mw!=w||e->mh!=h||e->z!=z||e->sw!=width||e->sh!=height){
  int cw=width*100/z,ch=height*100/z;
  if(cw*h>ch*w)cw=ch*w/h;else ch=cw*h/w;
  int x0=(width-cw)/2,y0=(height-ch)/2;
  for(int x=0;x<w;x++)e->xs[x]=x0+x*cw/w;
  for(int y=0;y<h;y++)e->ys[y]=y0+y*ch/h;
  e->mw=w;e->mh=h;e->z=z;e->sw=width;e->sh=height;
 }
 e->w=w;e->h=h;
 unsigned char lut[256];int denoise=night_snapshot(lut);
 {
  int uw=(w+1)/2,uh=(h+1)/2;unsigned char *u=e->yuv+pixels,*v=u+(size_t)uw*uh;
  for(int y=0;y<h;y++){memcpy(e->scanline,nv+(size_t)e->ys[y]*pitch,width);const unsigned char *row=e->scanline;unsigned char *out=e->yuv+(size_t)y*w;
   for(int x=0;x<w;x++)out[x]=row[e->xs[x]];}
  for(int y=0;y<uh;y++){memcpy(e->scanline,nv+(size_t)pitch*height+(size_t)(e->ys[y*2]/2)*pitch,width);const unsigned char *row=e->scanline;
   for(int x=0;x<uw;x++){int at=e->xs[x*2]&~1;u[y*uw+x]=effect==1?128:row[at];v[y*uw+x]=effect==1?128:row[at+1];}}
  night_process_yuv(e->yuv,w,h,effect==1,lut,denoise);
  if((effect==0||effect==1)&&jpeg_yuv)return 1;
 }
 for(int y=0;y<h;y++){
  size_t yn=(size_t)w*h,un=(size_t)((w+1)/2)*((h+1)/2);int uw=(w+1)/2;
  for(int x=0;x<w;x++){int yy=e->yuv[(size_t)y*w+x],u=e->yuv[yn+(size_t)(y/2)*uw+x/2]-128,v=e->yuv[yn+un+(size_t)(y/2)*uw+x/2]-128;
   int r=clamp8(yy+((359*v)>>8)),g=clamp8(yy-((88*u+183*v)>>8)),b=clamp8(yy+((454*u)>>8));
   filter_rgb(effect,&r,&g,&b,yy);size_t k=((size_t)y*w+x)*3;e->rgb[k]=r;e->rgb[k+1]=g;e->rgb[k+2]=b;
  }
 }
 return 1;
}
static int sample_prepare(struct preview_encoder *e,const unsigned char *nv,int pitch,int w,int h,int z,int effect){
 pthread_mutex_lock(&lock);int rotation=camera_rotation;pthread_mutex_unlock(&lock);
 int sw=rotation%180?h:w,sh=rotation%180?w:h;
 if(!sample_prepare_unrotated(e,nv,pitch,sw,sh,z,effect))return 0;
 if(rotation){
  size_t size=(effect==0||effect==1)&&jpeg_yuv?(size_t)w*h+2*(size_t)((w+1)/2)*((h+1)/2):(size_t)w*h*3;
  if(e->rotation_capacity<size){unsigned char*p=realloc(e->rotation_scratch,size);if(!p)return 0;e->rotation_scratch=p;e->rotation_capacity=size;}
  if((effect==0||effect==1)&&jpeg_yuv){rotate_yuv(e->yuv,e->rotation_scratch,sw,sh,rotation);size=(size_t)w*h+2*(size_t)((w+1)/2)*((h+1)/2);memcpy(e->yuv,e->rotation_scratch,size);}
  else{rotate_pixels(e->rgb,e->rotation_scratch,sw,sh,3,rotation);memcpy(e->rgb,e->rotation_scratch,size);}
 }
 e->w=w;e->h=h;return 1;
}
static int sample_compress(struct preview_encoder *e,int effect,int quality,unsigned char **jpeg,unsigned long *length){
 if(!e->codec)e->codec=jpeg_init();if(!e->codec)return 0;
 int w=e->w,h=e->h;
 if((effect==0||effect==1)&&jpeg_yuv){
  int uw=(w+1)/2,uh=(h+1)/2;const unsigned char *planes[]={e->yuv,e->yuv+(size_t)w*h,e->yuv+(size_t)w*h+(size_t)uw*uh};int strides[]={w,uw,uw};
  return jpeg_yuv(e->codec,planes,w,strides,h,TJSAMP_420,jpeg,length,quality,TJFLAG_FASTDCT)==0;
 }
 return jpeg_compress(e->codec,e->rgb,w,0,h,TJPF_RGB,jpeg,length,TJSAMP_420,quality,TJFLAG_FASTDCT)==0;
}
static int preview_encode(struct preview_encoder *e,const unsigned char *nv,int pitch,
 int w,int h,int z,int effect,unsigned char **jpeg,unsigned long *length){
 return sample_prepare(e,nv,pitch,w,h,z,effect)&&sample_compress(e,effect,76,jpeg,length);
}
static void preview_destroy(struct preview_encoder *e){if(e->codec)jpeg_destroy(e->codec);free(e->yuv);free(e->rgb);free(e->scanline);free(e->rotation_scratch);}
static unsigned long preview_generation;
static void preview_cleanup(void){
 if(!preview_generation)return;
 char prefix[80];snprintf(prefix,sizeof(prefix),"preview-%lu-",preview_generation);
 DIR *d=opendir(CACHE);if(!d)return;struct dirent *item;
 while((item=readdir(d)))if(!strncmp(item->d_name,prefix,strlen(prefix))){
  char p[512];snprintf(p,sizeof(p),CACHE "/%s",item->d_name);unlink(p);
 }
 closedir(d);
}

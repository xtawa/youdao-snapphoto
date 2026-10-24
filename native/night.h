/* Software-only shadow enhancement. No ISP/AE, sensor, or system settings. */
#ifdef __aarch64__
#include <arm_neon.h>
#endif
static int night_enabled=1,night_quality,night_active;
static double night_mean=128,night_since,night_strength,night_last;
static void night_observe(const unsigned char *nv,int pitch,int w,int h,double now){
 unsigned long sum=0,count=0;
 for(int y=h/16;y<h;y+=h/32?h/32:1)for(int x=w/32;x<w;x+=w/64?w/64:1){sum+=nv[(size_t)y*pitch+x];count++;}
 pthread_mutex_lock(&lock);
 double mean=count?(double)sum/count:128;
 night_mean=night_last?night_mean*.85+mean*.15:mean;
 int change=night_enabled?(night_active?night_mean>88:night_mean<64):night_active;
 if(!change)night_since=0;
 else if(!night_since)night_since=now;
 else if(!night_enabled||now-night_since>(night_active?1500:800)){night_active=night_enabled&&!night_active;night_since=0;}
 double target=night_enabled&&night_active?fmin(night_quality?.50:.36,fmax(0,(90-night_mean)/130)):0;
 double step=fmin(.025,fmax(0,(now-night_last)/1000)*.20);
 if(!night_enabled)night_strength=0;
 else if(night_strength<target)night_strength=fmin(target,night_strength+step);
 else night_strength=fmax(target,night_strength-step);
 night_last=now;pthread_mutex_unlock(&lock);
}
static int night_snapshot(unsigned char lut[256]){
 pthread_mutex_lock(&lock);double strength=night_enabled?night_strength:0;int quality=night_quality;pthread_mutex_unlock(&lock);
 // Bounded shadow lift: unlike gamma < 1, its near-black slope is finite
 // (at most 2x), so sensor noise close to zero is not boosted without limit.
 for(int i=0;i<256;i++){
  double t=i/255.0;
  lut[i]=strength>.001?(unsigned char)clamp8((int)(i+255*strength*2*t*(1-t)*(1-t)+.5)):(unsigned char)i;
 }
 return strength>.01&&quality;
}
static void night_spatial_impl(unsigned char *plane,int w,int h,int chroma,int vector){
 if(w<3||h<3)return;
 // Filter original neighbours before applying the tone curve. Three rows of
 // scratch keep the result independent of traversal and resolution bounded.
 unsigned char *rows=malloc((size_t)w*3);if(!rows)return;
 memcpy(rows,plane,(size_t)w*2);
 for(int j=1;j<h-1;j++){
  memcpy(rows+w*2,plane+(size_t)(j+1)*w,w);
  int x=1;
#ifdef __aarch64__
  if(vector)for(;x<=w-9;x+=8){
   uint8x8_t c=vld1_u8(rows+w+x),threshold=vdup_n_u8(44);
   if(!chroma){
    uint8x8_t shadow=vqsub_u8(vdup_n_u8(96),c);
    threshold=vadd_u8(vdup_n_u8(12),vmovn_u16(vshrq_n_u16(vmull_u8(shadow,vdup_n_u8(171)),9)));
   }
   uint16x8_t sum=vshll_n_u8(c,1),n=vdupq_n_u16(2);
   for(int dy=0;dy<3;dy++)for(int dx=-1;dx<=1;dx++){
    if(dy==1&&dx==0)continue;
    uint8x8_t a=vld1_u8(rows+dy*w+x+dx),difference=vabd_u8(a,c);
    uint8x8_t weight=vand_u8(vclt_u8(difference,threshold),vadd_u8(vdup_n_u8(1),vand_u8(vclt_u8(difference,vshr_n_u8(threshold,1)),vdup_n_u8(1))));
    sum=vmlal_u8(sum,a,weight);n=vaddw_u8(n,weight);
   }
   sum=vaddq_u16(sum,vshrq_n_u16(n,1));
   uint32x4_t lo=vcvtq_u32_f32(vdivq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(sum))),vcvtq_f32_u32(vmovl_u16(vget_low_u16(n)))));
   uint32x4_t hi=vcvtq_u32_f32(vdivq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(sum))),vcvtq_f32_u32(vmovl_u16(vget_high_u16(n)))));
   vst1_u8(plane+(size_t)j*w+x,vmovn_u16(vcombine_u16(vmovn_u32(lo),vmovn_u32(hi))));
  }
#else
  (void)vector;
#endif
  for(;x<w-1;x++){
   int c=rows[w+x],threshold=chroma?44:12+(c<96?(96-c)/3:0),sum=c*2,n=2;
   for(int dy=0;dy<3;dy++)for(int dx=-1;dx<=1;dx++){
    if(dy==1&&dx==0)continue;
    int a=rows[dy*w+x+dx],difference=abs(a-c);
    if(difference<threshold){int weight=difference<threshold/2?2:1;sum+=a*weight;n+=weight;}
   }
   plane[(size_t)j*w+x]=(sum+n/2)/n;
  }
  memmove(rows,rows+w,(size_t)w*2);
 }
 free(rows);
}
static void night_spatial(unsigned char *plane,int w,int h,int chroma){night_spatial_impl(plane,w,h,chroma,1);}
static void night_apply_curve(unsigned char *plane,size_t count,const unsigned char lut[256]){
 int identity=1;for(int i=0;i<256;i++)if(lut[i]!=i){identity=0;break;}if(identity)return;
 size_t i=0;
#ifdef __aarch64__
 uint8x16x4_t tables[4];
 for(int group=0;group<4;group++)for(int row=0;row<4;row++)tables[group].val[row]=vld1q_u8(lut+group*64+row*16);
 for(;i+16<=count;i+=16){
  uint8x16_t value=vld1q_u8(plane+i),out=vqtbl4q_u8(tables[0],value);
  for(int group=1;group<4;group++)out=vorrq_u8(out,vqtbl4q_u8(tables[group],vsubq_u8(value,vdupq_n_u8(group*64))));
  vst1q_u8(plane+i,out);
 }
#endif
 for(;i<count;i++)plane[i]=lut[plane[i]];
}
static void night_process_yuv(unsigned char *planes,int w,int h,int mono,const unsigned char lut[256],int denoise){
 size_t yn=(size_t)w*h;int uw=(w+1)/2,uh=(h+1)/2;size_t un=(size_t)uw*uh;
 unsigned char *u=planes+yn,*v=u+un;
 if(denoise){
  night_spatial(planes,w,h,0);
  if(!mono){
   night_spatial(u,uw,uh,1);night_spatial(v,uw,uh,1);
   // Very dark chroma has poor SNR; gently reduce coloured speckles without
   // desaturating highlights. Work against raw, not artificially lifted, Y.
   for(int y=0;y<uh;y++){
    int x=0;
#ifdef __aarch64__
    for(;x+8<=uw&&x*2+16<=w;x+=8){
     uint8x8_t luma=vld2_u8(planes+(size_t)(y*2)*w+x*2).val[0];
     uint8x8_t mix=vshrn_n_u16(vmull_u8(vqsub_u8(vdup_n_u8(80),luma),vdup_n_u8(3)),2);
     int16x8_t scale=vreinterpretq_s16_u16(vsubq_u16(vdupq_n_u16(256),vmovl_u8(mix)));
     for(int channel=0;channel<2;channel++){
      unsigned char *p=(channel?v:u)+(size_t)y*uw+x;
      int16x8_t delta=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(p))),vdupq_n_s16(128));
      int16x8_t product=vmulq_s16(delta,scale);
      product=vaddq_s16(product,vandq_s16(vshrq_n_s16(product,15),vdupq_n_s16(255)));
      vst1_u8(p,vmovn_u16(vreinterpretq_u16_s16(vaddq_s16(vshrq_n_s16(product,8),vdupq_n_s16(128)))));
     }
    }
#endif
    for(;x<uw;x++){
    int luma=planes[(size_t)(y*2)*w+x*2],mix=luma<80?(80-luma)*3/4:0;
    size_t at=(size_t)y*uw+x;
    u[at]=128+((int)u[at]-128)*(256-mix)/256;
    v[at]=128+((int)v[at]-128)*(256-mix)/256;
    }
   }
  }
 }
 night_apply_curve(planes,yn,lut);
}

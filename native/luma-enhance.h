/* Identical edge-preserving still-photo enhancement, eight pixels at once. */
static void enhance_luma_impl(unsigned char *plane,int w,int h,int vector){
 if(w<3||h<3)return;unsigned char *original=malloc((size_t)w*h);if(!original)return;memcpy(original,plane,(size_t)w*h);
 for(int y=1;y<h-1;y++){
  int x=1;
#ifdef __aarch64__
  if(vector)for(;x<=w-9;x+=8){
   size_t at=(size_t)y*w+x;uint8x8_t c=vld1_u8(original+at),neighbors[]={vld1_u8(original+at-w),vld1_u8(original+at+w),vld1_u8(original+at-1),vld1_u8(original+at+1)};
   uint16x8_t sum=vshll_n_u8(c,2),n=vdupq_n_u16(4),total=vdupq_n_u16(2);
   for(int k=0;k<4;k++){
    uint8x8_t weight=vand_u8(vcle_u8(vabd_u8(neighbors[k],c),vdup_n_u8(8)),vdup_n_u8(1));
    sum=vmlal_u8(sum,neighbors[k],weight);n=vaddw_u8(n,weight);total=vaddw_u8(total,neighbors[k]);
   }
   sum=vaddq_u16(sum,vshrq_n_u16(n,1));
   uint32x4_t lo=vcvtq_u32_f32(vdivq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(sum))),vcvtq_f32_u32(vmovl_u16(vget_low_u16(n)))));
   uint32x4_t hi=vcvtq_u32_f32(vdivq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(sum))),vcvtq_f32_u32(vmovl_u16(vget_high_u16(n)))));
   int16x8_t smooth=vreinterpretq_s16_u16(vcombine_u16(vmovn_u32(lo),vmovn_u32(hi)));
   int16x8_t edge=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(c)),vreinterpretq_s16_u16(vshrq_n_u16(total,2)));
   uint16x8_t absolute=vreinterpretq_u16_s16(vabsq_s16(edge)),mask=vandq_u16(vcgeq_u16(absolute,vdupq_n_u16(3)),vcleq_u16(absolute,vdupq_n_u16(32)));
   int16x8_t sharp=vshrq_n_s16(vaddq_s16(edge,vandq_s16(vshrq_n_s16(edge,15),vdupq_n_s16(3))),2);
   sharp=vandq_s16(vreinterpretq_s16_u16(mask),vmaxq_s16(vdupq_n_s16(-6),vminq_s16(vdupq_n_s16(6),sharp)));
   vst1_u8(plane+at,vqmovun_s16(vaddq_s16(smooth,sharp)));
  }
#else
  (void)vector;
#endif
  for(;x<w-1;x++){
   size_t at=(size_t)y*w+x;int c=original[at],a=original[at-w],b=original[at+w],l=original[at-1],r=original[at+1],sum=c*4,n=4;
   if(abs(a-c)<=8){sum+=a;n++;}if(abs(b-c)<=8){sum+=b;n++;}if(abs(l-c)<=8){sum+=l;n++;}if(abs(r-c)<=8){sum+=r;n++;}
   int edge=c-(a+b+l+r+2)/4,sharp=abs(edge)>=3&&abs(edge)<=32?edge/4:0;if(sharp>6)sharp=6;if(sharp< -6)sharp=-6;
   plane[at]=clamp8((sum+n/2)/n+sharp);
  }
 }
 free(original);
}
static void enhance_luma(unsigned char *plane,int w,int h){enhance_luma_impl(plane,w,h,1);}

/* Identity-sized crops need only NV12 deinterleaving, not per-pixel division.
 * An odd top offset preserves the original half-chroma-row interpolation. */
static void copy_photo_chroma(const unsigned char *uv,int pitch,int source_h,int x0,int y0,int w,int h,unsigned char *u,unsigned char *v,int vector){
 int uw=(w+1)/2,uh=(h+1)/2;
 for(int y=0;y<uh;y++){
  int sy=y0/2+y;const unsigned char *row=uv+(size_t)sy*pitch+x0;
  const unsigned char *next=(y0&1)&&sy+1<source_h/2?row+pitch:row;int x=0;
#ifdef __aarch64__
  if(vector)for(;x+16<=uw&&x*2+32<=w;x+=16){
   uint8x16x2_t a=vld2q_u8(row+x*2);if(next!=row){uint8x16x2_t b=vld2q_u8(next+x*2);a.val[0]=vrhaddq_u8(a.val[0],b.val[0]);a.val[1]=vrhaddq_u8(a.val[1],b.val[1]);}
   vst1q_u8(u+(size_t)y*uw+x,a.val[0]);vst1q_u8(v+(size_t)y*uw+x,a.val[1]);
  }
#else
  (void)vector;
#endif
  for(;x<uw;x++){u[(size_t)y*uw+x]=(row[x*2]+next[x*2]+1)/2;v[(size_t)y*uw+x]=(row[x*2+1]+next[x*2+1]+1)/2;}
 }
}

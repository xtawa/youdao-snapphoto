#ifdef __aarch64__
#include <arm_neon.h>
/* Intrinsic contracts: https://arm-software.github.io/acle/main/acle.html */
static void transpose8(const unsigned char *src,int stride,uint8x8_t out[8]){
 uint8x8x2_t a[4];uint16x4x2_t b[4];uint32x2x2_t c[4];
 for(int i=0;i<4;i++)a[i]=vtrn_u8(vld1_u8(src+(size_t)i*2*stride),vld1_u8(src+(size_t)(i*2+1)*stride));
 b[0]=vtrn_u16(vreinterpret_u16_u8(a[0].val[0]),vreinterpret_u16_u8(a[1].val[0]));
 b[1]=vtrn_u16(vreinterpret_u16_u8(a[0].val[1]),vreinterpret_u16_u8(a[1].val[1]));
 b[2]=vtrn_u16(vreinterpret_u16_u8(a[2].val[0]),vreinterpret_u16_u8(a[3].val[0]));
 b[3]=vtrn_u16(vreinterpret_u16_u8(a[2].val[1]),vreinterpret_u16_u8(a[3].val[1]));
 c[0]=vtrn_u32(vreinterpret_u32_u16(b[0].val[0]),vreinterpret_u32_u16(b[2].val[0]));
 c[1]=vtrn_u32(vreinterpret_u32_u16(b[1].val[0]),vreinterpret_u32_u16(b[3].val[0]));
 c[2]=vtrn_u32(vreinterpret_u32_u16(b[0].val[1]),vreinterpret_u32_u16(b[2].val[1]));
 c[3]=vtrn_u32(vreinterpret_u32_u16(b[1].val[1]),vreinterpret_u32_u16(b[3].val[1]));
 for(int i=0;i<4;i++){out[i]=vreinterpret_u8_u32(c[i].val[0]);out[i+4]=vreinterpret_u8_u32(c[i].val[1]);}
}
static void rotate_plane_neon(const unsigned char *src,unsigned char *dst,int w,int h,int angle){
 int bw=w&~7,bh=h&~7;
 if(angle==180){
  for(int y=0;y<h;y++){
   for(int x=0;x<bw;x+=8)vst1_u8(dst+(size_t)(h-1-y)*w+w-x-8,vrev64_u8(vld1_u8(src+(size_t)y*w+x)));
   for(int x=bw;x<w;x++)dst[(size_t)(h-1-y)*w+w-1-x]=src[(size_t)y*w+x];
  }return;
 }
 for(int y=0;y<bh;y+=8)for(int x=0;x<bw;x+=8){
  uint8x8_t columns[8];transpose8(src+(size_t)y*w+x,w,columns);
  for(int k=0;k<8;k++){
   if(angle==90)vst1_u8(dst+(size_t)(x+k)*h+h-y-8,vrev64_u8(columns[k]));
   else vst1_u8(dst+(size_t)(w-1-x-k)*h+y,columns[k]);
  }
 }
 for(int y=0;y<h;y++)for(int x=y<bh?bw:0;x<w;x++){
  size_t to=angle==90?(size_t)x*h+h-1-y:(size_t)(w-1-x)*h+y;dst[to]=src[(size_t)y*w+x];
 }
}
#endif
/* Out-of-place 90-degree tile transpose, or a linear 180-degree reversal.
 * 'channels' is 1 for a Y/U/V plane and 3 for RGB. */
static void rotate_pixels(const unsigned char *src,unsigned char *dst,int w,int h,int channels,int angle){
 if(!angle){memcpy(dst,src,(size_t)w*h*channels);return;}
#ifdef __aarch64__
 if(channels==1){rotate_plane_neon(src,dst,w,h,angle);return;}
#endif
 // Specialize the channel count outside the pixel loop: calling a variable-
 // length memcpy once per pixel costs more than the transpose on this CPU.
#define ROTATE_LOOP(BODY) for(int ty=0;ty<h;ty+=16)for(int tx=0;tx<w;tx+=16) \
 for(int y=ty;y<h&&y<ty+16;y++)for(int x=tx;x<w&&x<tx+16;x++){ \
  int dx,dy,dw=w;if(angle==90){dx=h-1-y;dy=x;dw=h;} \
  else if(angle==180){dx=w-1-x;dy=h-1-y;}else{dx=y;dy=w-1-x;dw=h;} \
  size_t from=(size_t)y*w+x,to=(size_t)dy*dw+dx;BODY;}
 if(channels==1){ROTATE_LOOP(dst[to]=src[from])}
 else{ROTATE_LOOP(dst[to*3]=src[from*3];dst[to*3+1]=src[from*3+1];dst[to*3+2]=src[from*3+2])}
#undef ROTATE_LOOP
}
static void rotate_yuv(const unsigned char *src,unsigned char *dst,int w,int h,int angle){
 size_t pixels=(size_t)w*h,uv=(size_t)((w+1)/2)*((h+1)/2);
 rotate_pixels(src,dst,w,h,1,angle);
 rotate_pixels(src+pixels,dst+pixels,(w+1)/2,(h+1)/2,1,angle);
 rotate_pixels(src+pixels+uv,dst+pixels+uv,(w+1)/2,(h+1)/2,1,angle);
}

/* JPEG APP1/Exif: little-endian TIFF, with local capture time and UTC offset.
 * Only newly captured JPEGs are annotated; existing media are never rewritten. */
static void put16(unsigned char *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void put32(unsigned char *p,unsigned n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static void time_strings(const struct timespec *ts,char *date,char *iso,char *offset){
 struct tm tm;localtime_r(&ts->tv_sec,&tm);char zone[8]={0};
 strftime(date,20,"%Y:%m:%d %H:%M:%S",&tm);strftime(zone,sizeof(zone),"%z",&tm);
 snprintf(offset,7,"%.3s:%.2s",zone,zone+3);
 strftime(iso,32,"%Y-%m-%dT%H:%M:%S",&tm);size_t at=strlen(iso);
 snprintf(iso+at,48-at,".%03ld%s",ts->tv_nsec/1000000,offset);
}
static void exif_entry(unsigned char *p,unsigned tag,unsigned type,unsigned count,unsigned value){
 put16(p,tag);put16(p+2,type);put32(p+4,count);put32(p+8,value);
}
static size_t photo_exif(unsigned char *out,const struct timespec *ts){
 unsigned char *t=out+10;memset(out,0,256);out[0]=0xff;out[1]=0xe1;
 memcpy(out+4,"Exif\0\0",6);memcpy(t,"II",2);put16(t+2,42);put32(t+4,8);
 /* IFD0: DateTime and ExifIFD pointer. Exif IFD starts at TIFF offset 38. */
 put16(t+8,2);exif_entry(t+10,0x0132,2,20,104);exif_entry(t+22,0x8769,4,1,38);
 put16(t+38,5);
 exif_entry(t+40,0x9003,2,20,104); /* DateTimeOriginal */
 exif_entry(t+52,0x9004,2,20,104); /* DateTimeDigitized */
 exif_entry(t+64,0x9011,2,7,124);  /* OffsetTimeOriginal */
 exif_entry(t+76,0x9012,2,7,124);  /* OffsetTimeDigitized */
 char date[20],iso[48],offset[7],subsec[4];time_strings(ts,date,iso,offset);
 snprintf(subsec,sizeof(subsec),"%03ld",ts->tv_nsec/1000000);
 unsigned packed=(unsigned char)subsec[0]|((unsigned char)subsec[1]<<8)|((unsigned char)subsec[2]<<16);
 exif_entry(t+88,0x9291,2,4,packed); /* SubSecTimeOriginal fits inline. */
 memcpy(t+104,date,20);memcpy(t+124,offset,7);
 size_t size=10+131;out[2]=(size-2)>>8;out[3]=(size-2)&255;return size;
}
static int captured_name(const char *path,char *display){
 const char *name=strrchr(path,'/');name=name?name+1:path;
 if(strncmp(name,"SNAP_",5)||strlen(name)<22)return 0;
 for(int i=5;i<20;i++)if(i==13){if(name[i]!='_')return 0;}else if(name[i]<'0'||name[i]>'9')return 0;
 snprintf(display,32,"%.4s-%.2s-%.2s %.2s:%.2s:%.2s",name+5,name+9,name+11,name+14,name+16,name+18);return 1;
}

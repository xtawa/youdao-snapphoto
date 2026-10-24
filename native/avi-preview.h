/* Read only finalized MJPEG/16k mono PCM AVIs produced by SnapPhoto.
 * Index offsets, lengths and file boundaries are validated before use. */
struct preview_avi_entry {uint32_t id,offset,size;};
static struct preview_avi_entry *preview_avi_index;
static size_t preview_avi_count;static uint64_t preview_pcm_bytes;
static char preview_avi_path[4096];static off_t preview_avi_size;
static double preview_avi_step;
static uint32_t read32(const unsigned char*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int preview_avi_load(const char *path){
 struct stat st;if(stat(path,&st))return 0;
 if(preview_avi_index&&!strcmp(path,preview_avi_path)&&st.st_size==preview_avi_size)return 1;
 FILE*f=fopen(path,"rb");if(!f)return 0;unsigned char header[4096];size_t got=fread(header,1,sizeof(header),f);int movi=-1,avih=-1;
 if(got<40||memcmp(header,"RIFF",4)||memcmp(header+8,"AVI ",4))goto bad;
 int audio_valid=0,video_valid=0;
 for(size_t i=12;i+16<got;i++){
  if(!memcmp(header+i,"vidsMJPG",8))video_valid=1;
  if(!memcmp(header+i,"auds",4))for(size_t j=i+4;j+24<got&&j<i+180;j++)if(!memcmp(header+j,"strf",4)){
   const unsigned char *format=header+j+8;
   audio_valid=format[0]==1&&format[1]==0&&format[2]==1&&format[3]==0&&read32(format+4)==16000&&format[14]==16&&format[15]==0;break;
  }
 }
 if(!video_valid||!audio_valid)goto bad;
 for(size_t i=12;i+12<got;i++){if(!memcmp(header+i,"movi",4)&&i>=8&&!memcmp(header+i-8,"LIST",4))movi=i;if(!memcmp(header+i,"avih",4))avih=i;}
 if(movi<0||avih<0)goto bad;
 uint32_t length=read32(header+movi-4);uint64_t index_at=(uint64_t)movi+length;
 if(length<4||index_at+8>(uint64_t)st.st_size)goto bad;
 if(fseek(f,(long)index_at,SEEK_SET)||fread(header,1,8,f)!=8||memcmp(header,"idx1",4))goto bad;
 uint32_t bytes=read32(header+4);size_t count=bytes/16;
 if(bytes%16||count<1||count>300000||index_at+8+bytes>(uint64_t)st.st_size)goto bad;
 struct preview_avi_entry *entries=calloc(count,sizeof(*entries));if(!entries)goto bad;
 uint64_t pcm=0;size_t frames=0;
 for(size_t i=0;i<count;i++){
  unsigned char entry[16];if(fread(entry,1,16,f)!=16){free(entries);goto bad;}
  uint64_t offset=(uint64_t)read32(entry+8)+movi+8;
  entries[i].id=read32(entry);entries[i].size=read32(entry+12);
  if(offset<(uint64_t)movi+12||offset>UINT32_MAX||entries[i].size>32*1024*1024||offset+entries[i].size>index_at){free(entries);goto bad;}
  entries[i].offset=offset;
  if(entries[i].id==0x63643030)frames++;
  if(entries[i].id==0x62773130)pcm+=entries[i].size;
 }
 if(!frames||!pcm||pcm>0xffff0000ULL){free(entries);goto bad;}
 // AVI avih timestamp is overwritten in finish_avi with the measured rate.
 fseek(f,avih+8,SEEK_SET);if(fread(header,1,4,f)!=4){free(entries);goto bad;}
 double step=read32(header)/1000000.0;if(step<.001||step>10){free(entries);goto bad;}
 fclose(f);free(preview_avi_index);preview_avi_index=entries;preview_avi_count=count;preview_pcm_bytes=pcm;preview_avi_step=step;
 snprintf(preview_avi_path,sizeof(preview_avi_path),"%s",path);preview_avi_size=st.st_size;return 1;
bad:fclose(f);return 0;
}
static void preview_avi_forget(void){free(preview_avi_index);preview_avi_index=NULL;preview_avi_count=0;preview_avi_path[0]=0;}
static int send_all(int fd,const void*data,size_t size){const char*p=data;while(size){ssize_t n=send(fd,p,size,MSG_NOSIGNAL);if(n<=0)return 0;p+=n;size-=n;}return 1;}
static int request_range(const char *request,uint64_t total,uint64_t *first,uint64_t *last){
 *first=0;*last=total?total-1:0;const char*p=strcasestr(request,"\r\nRange:");if(!p)return total?200:416;
 p+=8;while(*p==' ')p++;unsigned long long a,b;int used=0;
 if(sscanf(p,"bytes=%llu-%llu%n",&a,&b,&used)<2){if(sscanf(p,"bytes=%llu-%n",&a,&used)<1||!used)return 416;b=total?total-1:0;}
 if(!used||p[used]!='\r'||a>b||a>=total)return 416;
 *first=a;*last=b>=total?total-1:b;return 206;
}
static int range_header(int fd,int status,const char *mime,uint64_t first,uint64_t last,uint64_t total){
 char h[700],range[160]={0};if(status==206)snprintf(range,sizeof(range),"Content-Range: bytes %llu-%llu/%llu\r\n",(unsigned long long)first,(unsigned long long)last,(unsigned long long)total);
 int n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\n%sAccept-Ranges: bytes\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",status,status==206?"Partial Content":"OK",mime,(unsigned long long)(last-first+1),range);
 return send_all(fd,h,n);
}
static void send_avi_preview(int fd,const char *path,int kind,unsigned frame,const char *request){
 if(!preview_avi_load(path)){http_reply(fd,415,"text/plain","AVI preview unavailable",23);return;}
 if(kind==0){char json[160];int frames=0;for(size_t i=0;i<preview_avi_count;i++)if(preview_avi_index[i].id==0x63643030)frames++;
  snprintf(json,sizeof(json),"{\"frames\":%d,\"step\":%.8f,\"duration\":%.5f}",frames,preview_avi_step,frames*preview_avi_step);http_reply(fd,200,"application/json",json,strlen(json));return;}
 FILE*f=fopen(path,"rb");if(!f){http_reply(fd,404,"text/plain","Missing",7);return;}
 if(kind==1){
  size_t i;for(i=0;i<preview_avi_count;i++)if(preview_avi_index[i].id==0x63643030&&frame--==0)break;
  if(i==preview_avi_count){fclose(f);http_reply(fd,404,"text/plain","No frame",8);return;}
  size_t bytes=preview_avi_index[i].size;unsigned char *data=malloc(bytes);
  int good=data&&fseek(f,preview_avi_index[i].offset,SEEK_SET)==0&&fread(data,1,bytes,f)==bytes;
  if(good)http_reply(fd,200,"image/jpeg",data,bytes);else http_reply(fd,500,"text/plain","Read failed",11);
  free(data);fclose(f);return;
 }
 uint64_t total=44+preview_pcm_bytes,first,last;int status=request_range(request,total,&first,&last);
 if(status==416){fclose(f);http_reply(fd,416,"text/plain","Invalid range",13);return;}
 unsigned char wav[44]={0};memcpy(wav,"RIFF",4);put32(wav+4,total-8);memcpy(wav+8,"WAVEfmt ",8);put32(wav+16,16);put16(wav+20,1);put16(wav+22,1);put32(wav+24,16000);put32(wav+28,32000);put16(wav+32,2);put16(wav+34,16);memcpy(wav+36,"data",4);put32(wav+40,preview_pcm_bytes);
 if(!range_header(fd,status,"audio/wav",first,last,total)){fclose(f);return;}
 if(first<44){size_t end=last<43?last:43;if(!send_all(fd,wav+first,end-first+1)){fclose(f);return;}first=end+1;}
 uint64_t at=44;unsigned char block[8192];
 for(size_t i=0;i<preview_avi_count&&first<=last;i++)if(preview_avi_index[i].id==0x62773130){
  uint64_t end=at+preview_avi_index[i].size;if(first<end&&last>=at){uint64_t offset=first>at?first-at:0,n=end-(at+offset);if(n>last-first+1)n=last-first+1;
   if(fseek(f,preview_avi_index[i].offset+offset,SEEK_SET))break;
   while(n){size_t take=n>sizeof(block)?sizeof(block):n;if(fread(block,1,take,f)!=take||!send_all(fd,block,take)){fclose(f);return;}first+=take;n-=take;}
  }at=end;
 }fclose(f);
}

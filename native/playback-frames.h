/* Complete, independently owned JPEG snapshots for asynchronous UI reads. */
static int complete_jpeg(const char *path){
 FILE *f=fopen(path,"rb");if(!f)return 0;
 int good=fseek(f,-2,SEEK_END)==0&&fgetc(f)==255&&fgetc(f)==217;fclose(f);return good;
}
static int playback_present(const char *source,char destination[256]){
 // Open the decoder frame first: its pathname may be removed by max-files
 // while Falcon asynchronously opens an image. Keep independent presentation
 // snapshots for ~2 seconds instead of handing UI a decoder-owned pathname.
 FILE *in=fopen(source,"rb");if(!in)return 0;
 char tmp[280];snprintf(tmp,sizeof(tmp),CACHE "/play-view-%u-%08u.jpg.tmp",playback_generation,playback_presented);
 snprintf(destination,256,CACHE "/play-view-%u-%08u.jpg",playback_generation,playback_presented);
 FILE *out=fopen(tmp,"wb");if(!out){fclose(in);return 0;}
 unsigned char buf[32768];size_t count,total=0;int good=1;
 while((count=fread(buf,1,sizeof(buf),in))){total+=count;if(total>8*1024*1024||fwrite(buf,1,count,out)!=count){good=0;break;}}
 if(ferror(in))good=0;fclose(in);if(fclose(out))good=0;
 good=good&&complete_jpeg(tmp)&&rename(tmp,destination)==0;
 if(!good)unlink(tmp);else{
  if(playback_presented>=32){char old[256];snprintf(old,sizeof(old),CACHE "/play-view-%u-%08u.jpg",playback_generation,playback_presented-32);unlink(old);}
  playback_presented++;
 }return good;
}

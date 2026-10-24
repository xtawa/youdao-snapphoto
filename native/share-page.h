static const char *media_name(const char *p){const char *n=strrchr(p,'/');return n?n+1:p;}
static void html_text(FILE *f,const char *s){
 for(;*s;s++)switch(*s){case '&':fputs("&amp;",f);break;case '<':fputs("&lt;",f);break;case '>':fputs("&gt;",f);break;case '"':fputs("&quot;",f);break;case '\'':fputs("&#39;",f);break;default:fputc((unsigned char)*s,f);}
}
static char *export_manifest(void){
 cJSON *j=cJSON_CreateObject(),*a=cJSON_CreateArray();cJSON_AddStringToObject(j,"app","SnapPhoto");
 cJSON_AddStringToObject(j,"timeNote","Capture times use the pen clock. Existing files without a SNAP timestamp only have modifiedAtEpoch.");
 for(int i=0;i<shared_count;i++){
  struct stat st;if(stat(shared[i],&st))continue;cJSON *m=cJSON_CreateObject();
  cJSON_AddStringToObject(m,"name",media_name(shared[i]));cJSON_AddNumberToObject(m,"bytes",st.st_size);
  cJSON_AddNumberToObject(m,"modifiedAtEpoch",st.st_mtime);char date[32];
  if(captured_name(shared[i],date))cJSON_AddStringToObject(m,"capturedAtLocal",date);
  cJSON_AddItemToArray(a,m);
 }
 cJSON_AddItemToObject(j,"files",a);char *out=cJSON_Print(j);cJSON_Delete(j);return out;
}
static char *share_html(size_t *length){
 char *body=NULL;FILE *f=open_memstream(&body,length);if(!f)return NULL;
 long long total=0;for(int i=0;i<shared_count;i++){struct stat st;if(!stat(shared[i],&st))total+=st.st_size;}
 fputs("<!doctype html><html lang=zh-CN><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>SnapPhoto · 照片导出</title><style>"
 "*{box-sizing:border-box}body{margin:0;background:#f3f6f5;color:#20332e;font:16px/1.55 system-ui,sans-serif}header,main{max-width:1080px;margin:auto;padding:24px}header{padding-top:42px}.brand{letter-spacing:.12em;color:#406c5b;font-weight:700;font-size:13px}h1{font-size:clamp(28px,5vw,44px);margin:12px 0 8px}p{color:#61756d;margin:6px 0}.status{display:flex;flex-wrap:wrap;gap:12px;align-items:center;margin-top:20px}.pill{background:#e2ebe7;padding:8px 14px;border-radius:24px}a{color:#236349}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,270px),1fr));gap:20px}article{background:#fff;border:1px solid #dce7e1;border-radius:20px;overflow:hidden;min-width:0}img,video,.video{width:100%;height:190px;object-fit:contain;background:#192d27;display:block}.video{display:grid;place-items:center;color:#d6ece0}.info{padding:18px}.name{font-size:14px;overflow-wrap:anywhere;font-weight:650}.meta{font-size:13px;margin:10px 0 16px}.download{display:block;border-radius:10px;text-align:center;padding:11px;background:#255d47;color:white;text-decoration:none}audio{width:100%;margin-top:8px}button{border:0;border-radius:8px;padding:10px 18px;background:#d7e9df;color:#20332e;cursor:pointer}footer{padding:28px 0;color:#61756d;font-size:13px}#ended{display:none;color:#943d35}body.expired #ended{display:block}body.expired .download{pointer-events:none;opacity:.45}</style></head><body><header><div class=brand>SNAPPHOTO / LOCAL TRANSFER</div><h1>把这一刻，带走。</h1><p>原始文件直接从词典笔下载，照片内保留拍摄时间。</p><div class=status>",f);
 fprintf(f,"<span class=pill>%d 个文件 · %.1f MB</span><span class=pill>剩余 <b id=timer>--:--</b></span><a href='metadata.json' download>下载时间清单 JSON</a></div><p id=ended>链接已过期，请在词典笔上重新开启分享。</p></header><main><div class=grid>",shared_count,total/1048576.0);
 for(int i=0;i<shared_count;i++){
  struct stat st;if(stat(shared[i],&st))continue;int kind=media_kind(shared[i]);char date[32];
  fputs("<article>",f);if(kind==1)fprintf(f,"<img loading=lazy src='p/%d' alt='照片预览'>",i);else if(!strcasecmp(strrchr(shared[i],'.'),".avi"))fprintf(f,"<img id=c%d loading=lazy src='j/%d/0' alt='录像封面'><div style='padding:12px'><button onclick='playAvi(%d,this)'>▶ 预览</button><audio id=a%d controls preload=none src='a/%d'></audio></div>",i,i,i,i,i);else fprintf(f,"<video controls playsinline preload=metadata src='p/%d'></video>",i);
  fputs("<div class=info><div class=name>",f);html_text(f,media_name(shared[i]));fputs("</div><div class=meta>",f);
  if(captured_name(shared[i],date)){fputs("拍摄于 ",f);html_text(f,date);}else fputs("原文件未记录拍摄时间",f);
  fprintf(f,"<br>%.1f KB · %s</div><a class=download href='f/%d' download>下载原文件</a></div></article>",st.st_size/1024.0,kind==1?"照片":"录像",i);
 }
 int remaining=(int)(share_until-time(NULL));if(remaining<0)remaining=0;
 fprintf(f,"</div><footer>拍摄时间以词典笔时钟为准。关闭分享或退出应用后，链接立即失效。</footer></main><script>let n=%d;function tick(){document.getElementById('timer').textContent=String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%%60).padStart(2,'0');if(!n)document.body.classList.add('expired');else n--}tick();setInterval(tick,1000);const players={};async function playAvi(id,b){let a=document.getElementById('a'+id),img=document.getElementById('c'+id);if(!players[id]){players[id]={};a.onpause=()=>{clearInterval(players[id].timer);b.textContent='▶ 预览'};a.onended=a.onpause;a.onplay=async()=>{try{const meta=players[id].meta||(players[id].meta=await fetch('v/'+id).then(r=>{if(!r.ok)throw Error('无法读取录像');return r.json()}));let busy=false,last=-1;const paint=()=>{const frame=Math.min(meta.frames-1,Math.floor(a.currentTime/meta.step));if(busy||frame===last)return;last=frame;busy=true;img.onload=img.onerror=()=>{busy=false};img.src='j/'+id+'/'+frame};clearInterval(players[id].timer);players[id].timer=setInterval(paint,125);paint();b.textContent='暂停'}catch(e){a.pause();b.textContent='此录像请下载后播放'}}}if(a.paused){for(const el of document.querySelectorAll('audio,video'))if(el!==a)el.pause();a.play().catch(()=>{b.textContent='请点击音频播放按钮'})}else a.pause()}</script></body></html>",remaining);
 fclose(f);return body;
}
static void send_media(int fd,const char *real,int inline_photo,const char *request){
 int file=open(real,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
 if(file<0||fstat(file,&st)){if(file>=0)close(file);http_reply(fd,404,"text/plain","Missing",7);return;}
 if(inline_photo&&media_kind(real)==2){
  uint64_t first,last;int status=request_range(request,st.st_size,&first,&last);
  if(status==416){close(file);http_reply(fd,416,"text/plain","Invalid range",13);return;}
  if(!range_header(fd,status,"video/mp4",first,last,st.st_size)){close(file);return;}
  lseek(file,first,SEEK_SET);unsigned char block[32768];uint64_t remaining=last-first+1;
  while(remaining){size_t take=remaining>sizeof(block)?sizeof(block):remaining;ssize_t got=read(file,block,take);if(got<=0||!send_all(fd,block,got))break;remaining-=got;}close(file);return;
 }
 const char *name=media_name(real);char encoded[12288],fallback[256];size_t n=0,k=0;
 for(const unsigned char *p=(const unsigned char*)name;*p&&n+4<sizeof(encoded);p++){
  if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'||*p=='.')encoded[n++]=*p;
  else{snprintf(encoded+n,4,"%%%02X",*p);n+=3;}
  if(k+1<sizeof(fallback))fallback[k++]=*p>=32&&*p<127&&*p!='"'&&*p!='\\'?*p:'_';
 }
 encoded[n]=0;fallback[k]=0;const char *ext=strrchr(real,'.');
 const char *mime=inline_photo&&ext&&!strcasecmp(ext,".png")?"image/png":inline_photo?"image/jpeg":"application/octet-stream";
 char header[14000];int size=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Disposition: %s; filename=\"%s\"; filename*=UTF-8''%s\r\nContent-Length: %lld\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",mime,inline_photo?"inline":"attachment",fallback,encoded,(long long)st.st_size);
 if(size<=0||size>=(int)sizeof(header)){close(file);return;}send(fd,header,size,MSG_NOSIGNAL);
 char buffer[32768];ssize_t got;while((got=read(file,buffer,sizeof(buffer)))>0){ssize_t at=0;while(at<got){ssize_t sent=send(fd,buffer+at,got-at,MSG_NOSIGNAL);if(sent<=0)goto done;at+=sent;}}
done:close(file);
}

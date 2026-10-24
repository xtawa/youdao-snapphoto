/* Read-only accelerator discovery; does not create an ISP context or change clocks. */
#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include "cJSON.h"
int main(void){
 cJSON *root=cJSON_CreateObject();void *rga=dlopen("librga.so.2",RTLD_NOW|RTLD_LOCAL);
 const char *(*query)(int)=rga?dlsym(rga,"querystring"):NULL;
 if(query){const char *text=query(10);cJSON_AddStringToObject(root,"rga",text?text:"");}
 void *cl=dlopen("libOpenCL.so",RTLD_NOW|RTLD_LOCAL);
 int (*platforms)(unsigned,void**,unsigned*)=cl?dlsym(cl,"clGetPlatformIDs"):NULL;
 int (*devices)(void*,uint64_t,unsigned,void**,unsigned*)=cl?dlsym(cl,"clGetDeviceIDs"):NULL;
 int (*device_info)(void*,unsigned,size_t,void*,size_t*)=cl?dlsym(cl,"clGetDeviceInfo"):NULL;
 cJSON *list=cJSON_AddArrayToObject(root,"opencl");
 if(platforms&&devices&&device_info){
  void *ps[8]={0};unsigned pn=0;int status=platforms(8,ps,&pn);cJSON_AddNumberToObject(root,"openclStatus",status);
  for(unsigned p=0;!status&&p<pn&&p<8;p++){
   void *ds[8]={0};unsigned dn=0;int error=devices(ps[p],0xffffffffULL,8,ds,&dn);
   for(unsigned d=0;!error&&d<dn&&d<8;d++){
    char name[256]={0},version[256]={0};unsigned units=0,mhz=0;uint64_t memory=0;
    device_info(ds[d],0x102b,sizeof(name),name,NULL);device_info(ds[d],0x102f,sizeof(version),version,NULL);
    device_info(ds[d],0x1002,sizeof(units),&units,NULL);device_info(ds[d],0x100c,sizeof(mhz),&mhz,NULL);device_info(ds[d],0x101f,sizeof(memory),&memory,NULL);
    cJSON *item=cJSON_CreateObject();cJSON_AddStringToObject(item,"name",name);cJSON_AddStringToObject(item,"version",version);
    cJSON_AddNumberToObject(item,"units",units);cJSON_AddNumberToObject(item,"clockMHz",mhz);cJSON_AddNumberToObject(item,"globalMemory",memory);cJSON_AddItemToArray(list,item);
   }
  }
 }
 char *json=cJSON_PrintUnformatted(root);puts(json);return 0;
}

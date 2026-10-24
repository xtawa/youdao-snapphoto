#include "rknn_enhancer.hpp"
#include "npu_bridge.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>
#if defined(SNAPPHOTO_HAVE_RKNN)
#include <dlfcn.h>
#include <rknn_api.h>
namespace {
struct Api {
 void *library=nullptr;
 decltype(&rknn_init) init=nullptr;
 decltype(&rknn_destroy) destroy=nullptr;
 decltype(&rknn_query) query=nullptr;
 decltype(&rknn_inputs_set) inputs=nullptr;
 decltype(&rknn_run) run=nullptr;
 decltype(&rknn_outputs_get) outputs=nullptr;
 decltype(&rknn_outputs_release) release=nullptr;
} api;
std::mutex mutex;
rknn_context context=0;
rknn_tensor_attr input_attr{},output_attr{};
SnapNpuInfo stats{};
std::string opened_model,performance;
bool collecting=false;
std::vector<unsigned char> packed;
void copy_info(SnapNpuInfo *out){if(out)*out=stats;}
void failure(const char *error){std::snprintf(stats.error,sizeof(stats.error),"%s",error);}
void close_locked(){
 if(context){api.destroy(context);context=0;}
 stats.ready=0;opened_model.clear();packed.clear();performance.clear();
}
bool dimensions(const rknn_tensor_attr& attr,unsigned channels){
 if(attr.n_dims!=4||attr.dims[0]!=1)return false;
 if(attr.fmt==RKNN_TENSOR_NCHW)return attr.dims[1]==channels&&attr.dims[2]==128&&attr.dims[3]==128;
 if(attr.fmt==RKNN_TENSOR_NHWC)return attr.dims[1]==128&&attr.dims[2]==128&&attr.dims[3]==channels;
 return false;
}
bool load_api(){
 if(api.library)return true;
 // Load the firmware OEM runtime locally; leave the old system copy intact.
 api.library=dlopen("/oem/YoudaoDictPen/output/libs/librknnrt.so",RTLD_NOW|RTLD_LOCAL);
 if(!api.library){failure("OEM RKNN runtime unavailable");return false;}
#define SNAP_LOAD(member,symbol) api.member=reinterpret_cast<decltype(api.member)>(dlsym(api.library,symbol));if(!api.member){failure("Missing RKNN API: " symbol);dlclose(api.library);api.library=nullptr;return false;}
 SNAP_LOAD(init,"rknn_init");SNAP_LOAD(destroy,"rknn_destroy");SNAP_LOAD(query,"rknn_query");
 SNAP_LOAD(inputs,"rknn_inputs_set");SNAP_LOAD(run,"rknn_run");SNAP_LOAD(outputs,"rknn_outputs_get");SNAP_LOAD(release,"rknn_outputs_release");
#undef SNAP_LOAD
 return true;
}
}
#endif

extern "C" int snap_npu_open(const char *path,int collect,SnapNpuInfo *info){
#if !defined(SNAPPHOTO_HAVE_RKNN)
 (void)path;(void)collect;if(info){std::memset(info,0,sizeof(*info));std::snprintf(info->error,sizeof(info->error),"RKNN backend not built");}return 0;
#else
 std::lock_guard<std::mutex> guard(mutex);
 try{
  if(!path||std::strlen(path)>1024)throw std::runtime_error("Invalid model path");
  if(context&&opened_model==path&&collecting==(collect!=0)){copy_info(info);return 1;}
  close_locked();stats={};if(!load_api()){copy_info(info);return 0;}
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  if(!file.good())throw std::runtime_error("Bundled FFDNet model unavailable");
  auto length=file.tellg();if(length<256||length>8*1024*1024)throw std::runtime_error("Invalid bundled model size");
  std::vector<unsigned char> model(static_cast<std::size_t>(length));file.seekg(0);
  if(!file.read(reinterpret_cast<char*>(model.data()),length)||std::memcmp(model.data(),"RKNN",4))throw std::runtime_error("Invalid RKNN file");
  int result=api.init(&context,model.data(),static_cast<uint32_t>(model.size()),RKNN_FLAG_PRIOR_LOW|(collect?RKNN_FLAG_COLLECT_PERF_MASK:0),nullptr);
  if(result){context=0;throw std::runtime_error("RKNN model initialization failed: "+std::to_string(result));}
  rknn_sdk_version version{};if(api.query(context,RKNN_QUERY_SDK_VERSION,&version,sizeof(version)))throw std::runtime_error("Cannot query actual RKNN runtime");
  std::snprintf(stats.sdk,sizeof(stats.sdk),"%s",version.api_version);std::snprintf(stats.driver,sizeof(stats.driver),"%s",version.drv_version);
  rknn_input_output_num io{};if(api.query(context,RKNN_QUERY_IN_OUT_NUM,&io,sizeof(io))||io.n_input!=1||io.n_output!=1)throw std::runtime_error("Expected one FFDNet input/output");
  input_attr={};output_attr={};
  if(api.query(context,RKNN_QUERY_INPUT_ATTR,&input_attr,sizeof(input_attr))||api.query(context,RKNN_QUERY_OUTPUT_ATTR,&output_attr,sizeof(output_attr)))throw std::runtime_error("Cannot query tensor contract");
  if(!dimensions(input_attr,13)||!dimensions(output_attr,12)||input_attr.n_elems!=13*128*128||output_attr.n_elems!=12*128*128)throw std::runtime_error("Model is not the bundled 256px FFDNet core");
  stats.input_format=input_attr.fmt;stats.output_format=output_attr.fmt;stats.input_type=input_attr.type;stats.output_type=output_attr.type;
  rknn_mem_size memory{};if(!api.query(context,RKNN_QUERY_MEM_SIZE,&memory,sizeof(memory))){stats.weight_bytes=memory.total_weight_size;stats.internal_bytes=memory.total_internal_size;stats.dma_bytes=memory.total_dma_allocated_size;}
  packed.resize(13*128*128);opened_model=path;collecting=collect!=0;stats.ready=1;stats.error[0]=0;copy_info(info);return 1;
 }catch(const std::exception& error){failure(error.what());close_locked();copy_info(info);return 0;}
#endif
}

extern "C" int snap_npu_tile(const unsigned char *rgb,int pitch,int sigma,unsigned char *out,SnapNpuInfo *info){
#if !defined(SNAPPHOTO_HAVE_RKNN)
 (void)rgb;(void)pitch;(void)sigma;(void)out;return snap_npu_open(nullptr,0,info);
#else
 std::lock_guard<std::mutex> guard(mutex);
 try{
 if(!context||!rgb||!out||pitch<256*3||sigma<1||sigma>50){failure("Invalid FFDNet tile or closed session");copy_info(info);return 0;}
 for(int y=0;y<128;y++)for(int x=0;x<128;x++){
  unsigned char *pixel=packed.data()+(y*128+x)*13;
  for(int c=0;c<3;c++)for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++)pixel[c*4+dy*2+dx]=rgb[(y*2+dy)*pitch+(x*2+dx)*3+c];
  pixel[12]=static_cast<unsigned char>(sigma);
 }
 rknn_input input{};input.buf=packed.data();input.size=static_cast<uint32_t>(packed.size());input.type=RKNN_TENSOR_UINT8;input.fmt=RKNN_TENSOR_NHWC;
 rknn_run_extend run{};run.timeout_ms=3000;run.fence_fd=-1;
 int status=api.inputs(context,1,&input);if(!status)status=api.run(context,&run);
 if(status){failure(("NPU inference failed: "+std::to_string(status)).c_str());copy_info(info);return 0;}
 rknn_output output{};output.want_float=1;
 status=api.outputs(context,1,&output,nullptr);
 if(status){failure("NPU output unavailable");copy_info(info);return 0;}
 bool valid=output.buf&&output.size==12*128*128*sizeof(float);
 if(valid){
  const float *values=static_cast<const float*>(output.buf);
  for(int y=0;y<256&&valid;y++)for(int x=0;x<256&&valid;x++)for(int c=0;c<3;c++){
   int channel=c*4+(y%2)*2+x%2;
   std::size_t at=output_attr.fmt==RKNN_TENSOR_NCHW?(channel*128+y/2)*128+x/2:((y/2)*128+x/2)*12+channel;
   float value=values[at];if(!std::isfinite(value)||value< -1||value>2){valid=false;break;}
   out[(y*256+x)*3+c]=static_cast<unsigned char>(std::lround(std::max(0.f,std::min(1.f,value))*255));
  }
 }
 api.release(context,1,&output);
 if(!valid){failure("Invalid FFDNet output values/size");copy_info(info);return 0;}
 rknn_perf_run timing{};if(!api.query(context,RKNN_QUERY_PERF_RUN,&timing,sizeof(timing)))stats.last_run_us=static_cast<double>(timing.run_duration);
 if(collecting){rknn_perf_detail detail{};if(!api.query(context,RKNN_QUERY_PERF_DETAIL,&detail,sizeof(detail))&&detail.perf_data&&detail.data_len<1024*1024)performance.assign(detail.perf_data,detail.data_len);}
 stats.inferences++;stats.error[0]=0;copy_info(info);return 1;
 }catch(const std::exception& error){failure(error.what());copy_info(info);return 0;}
#endif
}
extern "C" const char *snap_npu_performance(void){
#if defined(SNAPPHOTO_HAVE_RKNN)
 return performance.c_str();
#else
 return "";
#endif
}
extern "C" void snap_npu_close(void){
#if defined(SNAPPHOTO_HAVE_RKNN)
 std::lock_guard<std::mutex> guard(mutex);close_locked();
#endif
}

namespace snapphoto {
bool rknn_supported_by_build(){
#if defined(SNAPPHOTO_HAVE_RKNN)
 return true;
#else
 return false;
#endif
}
bool rknn_denoise_tile(const std::string& path,const std::uint8_t *rgb,int w,int h,std::vector<std::uint8_t>& output,std::string& error){
 output.clear();SnapNpuInfo info{};
 if(!rknn_supported_by_build()){error="RKNN backend not built";return false;}
 if(w!=256||h!=256||!rgb){error="The bundled FFDNet core requires a 256x256 RGB tile";return false;}
 if(!snap_npu_open(path.c_str(),0,&info)){error=info.error;return false;}
 output.resize(256*256*3);
 if(!snap_npu_tile(rgb,w*3,12,output.data(),&info)){output.clear();error=info.error;return false;}
 error.clear();return true;
}
}

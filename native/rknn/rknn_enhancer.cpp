#include "rknn_enhancer.hpp"
#include <cstdint>
#include <fstream>
#include <vector>
#include <string>
#if defined(SNAPPHOTO_HAVE_RKNN)
#include <rknn_api.h>
#endif
namespace snapphoto {
bool rknn_supported_by_build(){
#if defined(SNAPPHOTO_HAVE_RKNN)
  return true; // Not proof the model, driver, or runtime can execute.
#else
  return false;
#endif
}
bool rknn_denoise_tile(const std::string& model_path,const std::uint8_t* rgb,
                      int width,int height,std::vector<std::uint8_t>& output,
                      std::string& error){
  output.clear();
#if !defined(SNAPPHOTO_HAVE_RKNN)
  (void)model_path;(void)rgb;(void)width;(void)height;
  error="RKNN backend not built";return false;
#else
  if(!rgb||width<1||height<1||width>1024||height>1024||model_path.size()<5||
      model_path.size()>1024||model_path.substr(model_path.size()-5)!=".rknn"){
    error="invalid model path or input RGB tile";return false;
  }
  std::ifstream file(model_path,std::ios::binary|std::ios::ate);
  if(!file.good()){error="model unavailable";return false;}
  const auto length=file.tellg();
  if(length<=0||length>32*1024*1024){error="model size invalid";return false;}
  file.seekg(0,std::ios::beg);
  std::vector<char> model(static_cast<std::size_t>(length));
  if(!file.read(model.data(),length)){error="model read failed";return false;}
  rknn_context ctx=0;
  if(rknn_init(&ctx,model.data(),static_cast<std::uint32_t>(model.size()),0,nullptr)!=0){
    error="RKNN init failed (driver/runtime/model)";return false;
  }
  struct Guard{rknn_context ctx;~Guard(){rknn_destroy(ctx);}} guard{ctx};
  rknn_input_output_num io={};
  if(rknn_query(ctx,RKNN_QUERY_IN_OUT_NUM,&io,sizeof(io))!=0||io.n_input!=1||io.n_output!=1){
    error="expected 1 input and 1 output";return false;
  }
  rknn_tensor_attr in={},out={};in.index=0;out.index=0;
  if(rknn_query(ctx,RKNN_QUERY_INPUT_ATTR,&in,sizeof(in))!=0||
     rknn_query(ctx,RKNN_QUERY_OUTPUT_ATTR,&out,sizeof(out))!=0){
    error="tensor attribute query failed";return false;
  }
  const std::size_t size=static_cast<std::size_t>(width)*height*3;
  const bool valid_in=in.type==RKNN_TENSOR_UINT8&&in.fmt==RKNN_TENSOR_NHWC&&
    in.n_dims==4&&in.dims[0]==1&&in.dims[1]==static_cast<std::uint32_t>(height)&&
    in.dims[2]==static_cast<std::uint32_t>(width)&&in.dims[3]==3&&in.size==size;
  const bool valid_out=out.type==RKNN_TENSOR_UINT8&&out.fmt==RKNN_TENSOR_NHWC&&
    out.n_dims==4&&out.dims[0]==1&&out.dims[1]==static_cast<std::uint32_t>(height)&&
    out.dims[2]==static_cast<std::uint32_t>(width)&&out.dims[3]==3&&out.size==size;
  if(!valid_in||!valid_out){error="model must be UINT8 RGB NHWC same-shape denoiser";return false;}
  rknn_input input={};input.index=0;input.buf=const_cast<std::uint8_t*>(rgb);
  input.size=static_cast<std::uint32_t>(size);input.type=RKNN_TENSOR_UINT8;
  input.fmt=RKNN_TENSOR_NHWC;input.pass_through=0;
  if(rknn_inputs_set(ctx,1,&input)!=0||rknn_run(ctx,nullptr)!=0){
    error="RKNN inference failed";return false;
  }
  rknn_output result={};result.index=0;result.want_float=0;result.is_prealloc=0;
  if(rknn_outputs_get(ctx,1,&result,nullptr)!=0){
    error="RKNN output failed";return false;
  }
  const bool ok=result.buf&&result.size==size;
  if(ok){
    const auto* start=static_cast<const std::uint8_t*>(result.buf);
    output.assign(start,start+size);
  }
  rknn_outputs_release(ctx,1,&result);
  if(!ok){error="unexpected output tensor size";return false;}
  error.clear();return true;
#endif
}
} // namespace snapphoto

/* Optional OpenCL 1.2 compute on application-owned YUV copies, never the ISP.
 * Compilation happens off the camera thread; ready/busy/failure => CPU fallback. */
#define CL_TARGET_OPENCL_VERSION 120
#include "../../native/vendor/CL/cl.h"
static void *gpu_library;static pthread_t gpu_init_thread;
static pthread_mutex_t gpu_mutex=PTHREAD_MUTEX_INITIALIZER,gpu_phase_mutex=PTHREAD_MUTEX_INITIALIZER;
static int gpu_phase,gpu_thread_started,gpu_enabled=1;static char gpu_error[300];
static cl_context gpu_context;static cl_command_queue gpu_queue;static cl_program gpu_program;
static cl_kernel gpu_spatial,gpu_finish;static cl_device_id gpu_device;
static cl_mem gpu_input,gpu_temp,gpu_output,gpu_lut;static size_t gpu_capacity;
static unsigned long gpu_frames,gpu_failures;static double gpu_total_ms;
#define GPU_API_LIST(X) \
 X(clGetPlatformIDs) X(clGetDeviceIDs) X(clCreateContext) X(clCreateCommandQueue) \
 X(clCreateProgramWithSource) X(clBuildProgram) X(clGetProgramBuildInfo) X(clCreateKernel) \
 X(clCreateBuffer) X(clSetKernelArg) X(clEnqueueWriteBuffer) X(clEnqueueReadBuffer) \
 X(clEnqueueMapBuffer) X(clEnqueueUnmapMemObject) \
 X(clEnqueueNDRangeKernel) X(clFinish) X(clReleaseMemObject) X(clReleaseKernel) \
 X(clReleaseProgram) X(clReleaseCommandQueue) X(clReleaseContext)
#define GPU_DECLARE(name) static __typeof__(&name) gpu_##name;
GPU_API_LIST(GPU_DECLARE)
#undef GPU_DECLARE
static const char gpu_source[]=
"__constant uint reciprocal[19]={0,0,2147483648U,1431655766U,1073741824U,858993460U,715827883U,613566757U,536870912U,477218589U,429496730U,390451573U,357913942U,330382100U,306783379U,286331154U,268435456U,252645136U,238609295U};\n"
"__kernel void spatial(__global const uchar *src,__global uchar *dst,int w,int h,int mono){\n"
" int x=get_global_id(0),y=get_global_id(1),yn=w*h,uw=(w+1)/2,uh=(h+1)/2,color=y>=h,pw=color?uw:w,ph=color?uh:h;\n"
" if(y>=h+uh||x>=(color?uw*2:w))return;int base=color?(x>=uw?yn+uw*uh:yn):0;if(color){y-=h;if(x>=uw)x-=uw;}int gid=base+y*pw+x,c=src[gid];\n"
" if(color&&mono){dst[gid]=128;return;}if(x==0||y==0||x==pw-1||y==ph-1){dst[gid]=c;return;}\n"
" int threshold=color?44:12+max(96-c,0)/3,sum=c*2,n=2;\n"
" for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){if(dx==0&&dy==0)continue;int a=src[gid+dy*pw+dx],d=abs(a-c);if(d<threshold){int k=d<threshold/2?2:1;sum+=a*k;n+=k;}}\n"
" dst[gid]=mul_hi((uint)(sum+n/2),reciprocal[n]);\n}\n"
"__kernel void finish_image(__global const uchar *src,__global uchar *dst,__constant const uchar *lut,int w,int h,int mono){\n"
" int x=get_global_id(0),y=get_global_id(1),yn=w*h,uw=(w+1)/2,uh=(h+1)/2;if(y>=h+uh||x>=(y<h?w:uw*2))return;\n"
" if(y<h){int gid=y*w+x;dst[gid]=lut[src[gid]];return;}y-=h;int base=x>=uw?yn+uw*uh:yn;if(x>=uw)x-=uw;int gid=base+y*uw+x;if(mono){dst[gid]=128;return;}\n"
" int luma=src[(y*2)*w+x*2],mix=luma<80?(80-luma)*3/4:0;\n"
" dst[gid]=128+((int)src[gid]-128)*(256-mix)/256;\n}\n";
static void gpu_buffers_release(void){
 if(gpu_input)gpu_clReleaseMemObject(gpu_input);if(gpu_temp)gpu_clReleaseMemObject(gpu_temp);if(gpu_output)gpu_clReleaseMemObject(gpu_output);if(gpu_lut)gpu_clReleaseMemObject(gpu_lut);
 gpu_input=gpu_temp=gpu_output=gpu_lut=NULL;gpu_capacity=0;
}
static void *gpu_initialize(void *unused){
 (void)unused;cl_int error=-1;gpu_library=dlopen("libOpenCL.so",RTLD_NOW|RTLD_LOCAL);if(!gpu_library)goto failed;
#define GPU_LOAD(name) *(void**)(&gpu_##name)=dlsym(gpu_library,#name);if(!gpu_##name)goto failed;
 GPU_API_LIST(GPU_LOAD)
#undef GPU_LOAD
 cl_platform_id platform=NULL;cl_uint count=0;
 if(gpu_clGetPlatformIDs(1,&platform,&count)||!count)goto failed;
 if(gpu_clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,1,&gpu_device,&count)||!count)goto failed;
 gpu_context=gpu_clCreateContext(NULL,1,&gpu_device,NULL,NULL,&error);if(!gpu_context||error)goto failed;
 gpu_queue=gpu_clCreateCommandQueue(gpu_context,gpu_device,0,&error);if(!gpu_queue||error)goto failed;
 const char *source=gpu_source;gpu_program=gpu_clCreateProgramWithSource(gpu_context,1,&source,NULL,&error);if(!gpu_program||error)goto failed;
 error=gpu_clBuildProgram(gpu_program,1,&gpu_device,"-cl-std=CL1.2",NULL,NULL);
 if(error){gpu_clGetProgramBuildInfo(gpu_program,gpu_device,CL_PROGRAM_BUILD_LOG,sizeof(gpu_error)-1,gpu_error,NULL);goto failed;}
 gpu_spatial=gpu_clCreateKernel(gpu_program,"spatial",&error);if(!gpu_spatial||error)goto failed;
 gpu_finish=gpu_clCreateKernel(gpu_program,"finish_image",&error);if(!gpu_finish||error)goto failed;
 pthread_mutex_lock(&gpu_phase_mutex);gpu_phase=2;pthread_mutex_unlock(&gpu_phase_mutex);return NULL;
failed:
 if(!gpu_error[0])snprintf(gpu_error,sizeof(gpu_error),"OpenCL initialization error %d; using CPU",error);
 pthread_mutex_lock(&gpu_phase_mutex);gpu_phase=-1;pthread_mutex_unlock(&gpu_phase_mutex);return NULL;
}
static void gpu_start(void){
 if(!gpu_enabled)return;
 pthread_mutex_lock(&gpu_phase_mutex);
 if(!gpu_phase){gpu_phase=1;if(pthread_create(&gpu_init_thread,NULL,gpu_initialize,NULL)){gpu_phase=-1;}else gpu_thread_started=1;}
 pthread_mutex_unlock(&gpu_phase_mutex);
}
static int gpu_night(unsigned char *planes,int w,int h,int mono,const unsigned char lut[256]){
 if(!gpu_enabled||(size_t)w*h<640*360||(size_t)w*h>3264*1836)return 0;
 gpu_start();pthread_mutex_lock(&gpu_phase_mutex);int ready=gpu_phase==2;pthread_mutex_unlock(&gpu_phase_mutex);
 if(!ready||pthread_mutex_trylock(&gpu_mutex))return 0;
 double began=now_ms();size_t size=(size_t)w*h+2*(size_t)((w+1)/2)*((h+1)/2);cl_int error=CL_SUCCESS;
 if(gpu_capacity<size){
  gpu_buffers_release();gpu_input=gpu_clCreateBuffer(gpu_context,CL_MEM_READ_ONLY|CL_MEM_ALLOC_HOST_PTR,size,NULL,&error);if(error)goto failed;
  gpu_temp=gpu_clCreateBuffer(gpu_context,CL_MEM_READ_WRITE,size,NULL,&error);if(error)goto failed;
  gpu_output=gpu_clCreateBuffer(gpu_context,CL_MEM_WRITE_ONLY|CL_MEM_ALLOC_HOST_PTR,size,NULL,&error);if(error)goto failed;
  gpu_lut=gpu_clCreateBuffer(gpu_context,CL_MEM_READ_ONLY,256,NULL,&error);if(error)goto failed;gpu_capacity=size;
 }
#define GPU_TRY(expr) if((error=(expr))!=CL_SUCCESS)goto failed
 void *mapped=gpu_clEnqueueMapBuffer(gpu_queue,gpu_input,CL_TRUE,CL_MAP_WRITE_INVALIDATE_REGION,0,size,0,NULL,NULL,&error);if(error||!mapped)goto failed;
 memcpy(mapped,planes,size);GPU_TRY(gpu_clEnqueueUnmapMemObject(gpu_queue,gpu_input,mapped,0,NULL,NULL));
 GPU_TRY(gpu_clEnqueueWriteBuffer(gpu_queue,gpu_lut,CL_FALSE,0,256,lut,0,NULL,NULL));
 GPU_TRY(gpu_clSetKernelArg(gpu_spatial,0,sizeof(gpu_input),&gpu_input));GPU_TRY(gpu_clSetKernelArg(gpu_spatial,1,sizeof(gpu_temp),&gpu_temp));
 GPU_TRY(gpu_clSetKernelArg(gpu_spatial,2,sizeof(w),&w));GPU_TRY(gpu_clSetKernelArg(gpu_spatial,3,sizeof(h),&h));GPU_TRY(gpu_clSetKernelArg(gpu_spatial,4,sizeof(mono),&mono));
 size_t global[2]={(size_t)(w+1+15)&~(size_t)15,(size_t)(h+(h+1)/2+3)&~(size_t)3},local[2]={16,4};
 GPU_TRY(gpu_clEnqueueNDRangeKernel(gpu_queue,gpu_spatial,2,NULL,global,local,0,NULL,NULL));
 GPU_TRY(gpu_clSetKernelArg(gpu_finish,0,sizeof(gpu_temp),&gpu_temp));GPU_TRY(gpu_clSetKernelArg(gpu_finish,1,sizeof(gpu_output),&gpu_output));
 GPU_TRY(gpu_clSetKernelArg(gpu_finish,2,sizeof(gpu_lut),&gpu_lut));GPU_TRY(gpu_clSetKernelArg(gpu_finish,3,sizeof(w),&w));GPU_TRY(gpu_clSetKernelArg(gpu_finish,4,sizeof(h),&h));GPU_TRY(gpu_clSetKernelArg(gpu_finish,5,sizeof(mono),&mono));
 GPU_TRY(gpu_clEnqueueNDRangeKernel(gpu_queue,gpu_finish,2,NULL,global,local,0,NULL,NULL));
 mapped=gpu_clEnqueueMapBuffer(gpu_queue,gpu_output,CL_TRUE,CL_MAP_READ,0,size,0,NULL,NULL,&error);if(error||!mapped)goto failed;
 memcpy(planes,mapped,size);GPU_TRY(gpu_clEnqueueUnmapMemObject(gpu_queue,gpu_output,mapped,0,NULL,NULL));GPU_TRY(gpu_clFinish(gpu_queue));
#undef GPU_TRY
 gpu_frames++;gpu_total_ms+=now_ms()-began;pthread_mutex_unlock(&gpu_mutex);return 1;
failed:
 // Finish outstanding transfers before the caller reuses its host buffers.
 gpu_clFinish(gpu_queue);gpu_failures++;snprintf(gpu_error,sizeof(gpu_error),"OpenCL processing error %d; using CPU",error);
 pthread_mutex_lock(&gpu_phase_mutex);gpu_phase=-1;pthread_mutex_unlock(&gpu_phase_mutex);
 pthread_mutex_unlock(&gpu_mutex);return 0;
}
static void gpu_shutdown(void){
 if(gpu_thread_started){pthread_join(gpu_init_thread,NULL);gpu_thread_started=0;}
 pthread_mutex_lock(&gpu_mutex);
 if(gpu_clReleaseMemObject)gpu_buffers_release();
 if(gpu_spatial)gpu_clReleaseKernel(gpu_spatial);if(gpu_finish)gpu_clReleaseKernel(gpu_finish);if(gpu_program)gpu_clReleaseProgram(gpu_program);
 if(gpu_queue)gpu_clReleaseCommandQueue(gpu_queue);if(gpu_context)gpu_clReleaseContext(gpu_context);
 gpu_spatial=gpu_finish=NULL;gpu_program=NULL;gpu_queue=NULL;gpu_context=NULL;
 // Keep the vendor runtime pinned. Its internal process-wide callbacks may
 // outlive one context; its code/data does not belong to this miniapp.
 pthread_mutex_lock(&gpu_phase_mutex);gpu_phase=0;pthread_mutex_unlock(&gpu_phase_mutex);pthread_mutex_unlock(&gpu_mutex);
}

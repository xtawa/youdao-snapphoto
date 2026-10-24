#ifndef SNAPPHOTO_NPU_BRIDGE_H
#define SNAPPHOTO_NPU_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SNAP_NPU_TILE 256
typedef struct {
 int ready,input_format,output_format,input_type,output_type;
 uint32_t inferences,weight_bytes,internal_bytes;
 uint64_t dma_bytes;
 double last_run_us;
 char sdk[256],driver[256],error[192];
} SnapNpuInfo;
int snap_npu_open(const char *model_path,int collect_performance,SnapNpuInfo *info);
int snap_npu_tile(const unsigned char *rgb,int pitch,int noise_sigma,unsigned char *output,SnapNpuInfo *info);
const char *snap_npu_performance(void);
void snap_npu_close(void);
#ifdef __cplusplus
}
#endif
#endif

# X7 Pro HDR / NPU 设计与验收

## 硬件证据

- X7 Pro 的 8MP 摄像头：https://www.ithome.com/0/789/414.htm
- 社区 X7 Pro PenOS 4.9.6 固件出现 RK3562 MiniLoader，芯片线索（非用户实机验证）：https://github.com/orgs/PenUniverse/discussions/277
- RK3562 系列 NPU 约 1 TOPS（X6 Pro 拆解证据，并非 X7 Pro NPU 可用性证明）：https://www.52audio.com/archives/199766.html
- Rockchip RKNN Toolkit2 SDK 支持 RK3562：https://github.com/airockchip/rknn-toolkit2
- 未获得第三方 MiniApp 已开放 ISP HDR、手动曝光控制、RAW 连拍或 NPU 的证据。

## 三种 HDR 模式

关闭：走普通 capturePhoto。
自动：必须有真实硬件接口 captureHDR；原生侧先分析场景亮度，必要时 HDR。未触发时 hdrApplied=false。
开启：原生侧实现 ISP HDR 或 -2/0/+2 EV 多帧曝光拍照、对齐、融合、去重影并写盘；不能用单帧滤镜冒充多曝光 HDR。

getCapabilities 需要 hdr=true、hdrStrategies 包含 isp/bracket，并具备 captureHDR 函数，前端才开放 HDR。
captureHDR 接收 hdrMode、exposuresEV、saveDirectory、filter、zoom；回执带 path、hdrApplied、hdrBackend。真正的视频 HDR 未纳入此次工作范围。

## CPU HDR 参考实现

native/hdr/hdr_fusion.cpp：对齐后的 8-bit RGB 帧，先做 sRGB 到线性近似，按曝光 EV 归一化，按像素可信度融合，对异常移动区域简单抑制重影，最后将结果进行 SDR 色调映射。

目前代码与 CPU 单测已编写并加入 CI；**尚未接入实际摄像头**。该算法没有对齐模块，必须在原生相机服务内完成几何配准后使用。JPEG 的动态范围和非线性调色有限；需要真实 HDR 更建议 RAW/ISP 链路。输出是 SDR 照片而非 HDR10 显示文件。

## 端侧 NPU

native/rknn/rknn_enhancer.cpp 实现可选 RKNN C API 适配：rknn_init、rknn_query、rknn_inputs_set、rknn_run、rknn_outputs_get、rknn_outputs_release、rknn_destroy。必须在真实设备上，使用与 RK3562/Linux 匹配的官方 RKNN SDK 和可用驱动编译。

严格模型契约：单个 UINT8 NHWC RGB 输入及同尺寸输出，单 tile 最高 1024×1024。调用方负责 tile 重叠、边界融合、相机颜色校正与质量测试。**仓库没有提供 NPU 模型**。没有经训练及 RK3562 转换的兼容去噪模型，不能声称实际 NPU 推理成功。

默认 RKNN 后端关闭。只有原生实现完成并报告 npu=true、npuReady=true，且 enhancePhoto 方法存在时，前端才会传 preferNpu=true。enhancePhoto 失败必须保留原片。不允许未经确认修改固件或替换驱动。

构建 RKNN 后端（须自行提供官方头文件及目标平台库）：

    cmake -S native/rknn -B build/rknn -DSNAPPHOTO_HAVE_RKNN=ON -DRKNN_SDK_INCLUDE=/sdk/include -DRKNN_SDK_LIBRARY=/sdk/lib/librknnrt.so
    cmake --build build/rknn

该库并不会被普通 MiniApp AMR 自动打包进设备可用的原生服务；需要另行实现并验证 JSAPI 加载/权限、相机驱动与视频编码器。

## 相册与性能

所有照片仍优先写入 /userdisk/Pictures。HDR/AI 失败保留原照片。限制最大工作尺寸、按扫描线融合，避免一次让 QuickJS 加载多张 8MP RAW/JPEG；建议离线拍后增强，实时预览不执行 NPU。
实际需测试曝光锁定、运动重影、ISP/RAW 支持、冷启动 NPU 耗时、功耗、内存和温升。

## 只读设备诊断

    adb push tools/x7pro-npu-diagnose.sh /userdisk/Favorite/
    adb shell "sh /userdisk/Favorite/x7pro-npu-diagnose.sh"

查看 CPU/设备树、NPU 节点、librknnrt.so 与 ISP 节点。仅发现文件不代表 NPU 真能推理，也不等于 HDR 曝光控制已经开放。

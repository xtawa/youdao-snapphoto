# X7 Pro HDR / NPU 设计与验收

## 硬件证据

- X7 Pro 的 8MP 摄像头：https://www.ithome.com/0/789/414.htm
- 社区 X7 Pro PenOS 4.9.6 固件出现 RK3562 MiniLoader，芯片线索（非用户实机验证）：https://github.com/orgs/PenUniverse/discussions/277
- RK3562 系列 NPU 约 1 TOPS（X6 Pro 拆解证据，并非 X7 Pro NPU 可用性证明）：https://www.52audio.com/archives/199766.html
- Rockchip RKNN Toolkit2 SDK 支持 RK3562：https://github.com/airockchip/rknn-toolkit2
- 本项目已验证原厂 RKNN 可供自有模块调用；ISP HDR、手动曝光控制与 RAW 连拍仍未接通。

## 三种 HDR 模式

关闭：走普通 capturePhoto。
自动：必须有真实硬件接口 captureHDR；原生侧先分析场景亮度，必要时 HDR。未触发时 hdrApplied=false。
开启：原生侧实现 ISP HDR 或 -2/0/+2 EV 多帧曝光拍照、对齐、融合、去重影并写盘；不能用单帧滤镜冒充多曝光 HDR。

getCapabilities 需要 hdr=true、hdrStrategies 包含 isp/bracket，并具备 captureHDR 函数，前端才开放 HDR。
captureHDR 接收 hdrMode、exposuresEV、saveDirectory、filter、zoom；回执带 path、hdrApplied、hdrBackend。真正的视频 HDR 未纳入此次工作范围。

## CPU HDR 参考实现

native/hdr/hdr_fusion.cpp：对齐后的 8-bit RGB 帧，先做 sRGB 到线性近似，按曝光 EV 归一化，按像素可信度融合，对异常移动区域简单抑制重影，最后将结果进行 SDR 色调映射。

目前代码与 CPU 单测已编写并加入 CI；**尚未接入实际摄像头**。该算法没有对齐模块，必须在原生相机服务内完成几何配准后使用。JPEG 的动态范围和非线性调色有限；需要真实 HDR 更建议 RAW/ISP 链路。输出是 SDR 照片而非 HDR10 显示文件。

## 端侧 NPU：0.9.1 实机接通

设备 X7 Pro / PenOS 4.34.0，RK3562，`/dev/dri/renderD129` 对应 `ff300000.npu`。原厂驱动 0.9.1，OEM 运行库 `/oem/YoudaoDictPen/output/libs/librknnrt.so` 为 1.5.1b18；通过绝对路径 dlopen / dlsym 调用，保留系统另外的 1.4.0 运行库。Toolkit2 1.5.2 生成的格式 6 被原厂库拒绝，已改用 1.5.0 转换兼容模型，没有更换系统库或驱动。

内置 [FFDNet color / KAIR](https://github.com/cszn/KAIR) INT8 模型，MIT 授权随包附带，文件 956,463 字节。原始权重 SHA-256、转换模型 SHA-256、来源及尺寸见 `assets/models/ffdnet.json`。`tools/convert-ffdnet.py` 下载校验公开权重，导出 RK3562 模型；[固定版本 CI](https://github.com/xtawa/youdao-snapphoto/actions/runs/40080751852) 仅使用合成校准与测试图，不上传设备照片。

RGB 256×256 图块由 CPU 做 2× pixel-unshuffle 并附加 sigma 通道，NPU 输入 13×128×128，输出 12×128×128；12 层卷积均在 NPU，CPU pixel-shuffle 恢复 RGB。运行时读取真实张量布局。逐层性能表将所有 12 个 Conv 标记为 NPU。合成 sigma=12 图块：噪声 PSNR 26.58 dB，浮点参考 32.68 dB，实机 FP16 32.68 dB，INT8 32.50 dB；INT8 与参考平均绝对差 1.19/255。这些合成指标不等于实际照片画质保证。

带逐层采集的首次 3 次平均 INT8 约 198 ms/图块，FP16 322 ms/图块；发行路径关闭逐层性能采集。完整 JPEG 设备数据见 `evidence/npu-photo-0.9.1.json` 和 `evidence/npu-full-photo-0.9.1.json`，不能把图块数据当作整张时间。

`native/npu-enhancement.h` 复用系统 TurboJPEG 解码，图块带 24px 上下文，只提交中心 208px。原始 RGB 只读，相邻块不会读取已增强结果；混合原片保护文字细节。自动模式稳健估计噪声并跳过低噪声图，画质优先强制处理；默认关闭。

能力检查必须成功执行一次真实模型推理。异步任务轮询进度，单次推理超时 3 秒、整张任务 120 秒，退出在图块边界取消并释放模型。JPEG 输入限制 12MP / 64MiB；原片不改写，新文件写 `.partial` 后提交，失败清理自己的半成品。增强图沿用原始拍摄时间写 EXIF，独立发布到系统相册。普通预览和录像不运行模型。

构建：`python native/build.py --zig /path/to/zig`，`npm run build`，`python tools/build-x7pro.py`。打包工具校验模型哈希，将原生模块、模型、元数据与 MIT 授权一起封入 AMR，无需联网下载模型。运行依赖固件原有 OEM RKNN 库。

## 0.9.2 后台接入

0.9.2 使用 `native/background-queue.h` 和 AMR 内置 helper，把模型处理从快门与 UI 生命周期分离；上文“关闭取消”的说明属于 0.9.1 的进程内接口。0.9.2 的持久队列与退出后行为见 [版本说明](x7pro-0.9.2.md)。单图任务限制为 120 秒活跃处理时间，系统休眠时计时暂停；不永久占用音频或禁止系统关机。

## 相册与性能

所有照片仍优先写入 /userdisk/Pictures。HDR/AI 失败保留原照片。限制最大工作尺寸、按扫描线融合，避免一次让 QuickJS 加载多张 8MP RAW/JPEG；建议离线拍后增强，实时预览不执行 NPU。
实际需测试曝光锁定、运动重影、ISP/RAW 支持、冷启动 NPU 耗时、功耗、内存和温升。

## 只读设备诊断

    adb push tools/x7pro-npu-diagnose.sh /userdisk/Favorite/
    adb shell "sh /userdisk/Favorite/x7pro-npu-diagnose.sh"

查看 CPU/设备树、NPU 节点、librknnrt.so 与 ISP 节点。仅发现文件不代表 NPU 真能推理，也不等于 HDR 曝光控制已经开放。

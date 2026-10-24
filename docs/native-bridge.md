# SnapPhoto / X7 Pro 原生相机接口契约

**0.2.0 已实现 X7 Pro 原生适配模块 `native/snapphoto.c`。下列为应用层契约；实际能力由 getCapabilities 返回，并非原厂已存在同名接口。**

摄像头 JS 桥接查找 globalThis.SnapPhotoNative 或 $falcon.jsapi.snapphoto。

- getCapabilities(): Promise<{camera,photo,video,preview,zoom,focus,filters,gallery,share,minZoom,maxZoom}>
- openPreview(), getPreviewFrame(), closePreview()
- capturePhoto({filter,zoom,saveDirectory:'/userdisk/Pictures'}): Promise<{path,indexed?:boolean}>
- startRecording({filter,zoom,saveDirectory:'/userdisk/Pictures'}), stopRecording(): Promise<{path,indexed?:boolean}>
- 可选 publishToGallery({sourcePath,targetDirectory:'/userdisk/Pictures',mediaType:'photo'|'video'}): Promise<{path,written:true,indexed?:boolean}>
- listMedia({directory:'/userdisk/Pictures',includeFallback:true}): Promise<Array<{path,name,type,size}>>
- setZoom(zoom), focusAt(x,y), setFilter(id)
- startShare(paths), stopShare()
- 可选 getDisplaySize()
- startOrientationSensor(): boolean / stopOrientationSensor(): void，封装原厂 `gsensor.Gsensor` 的 `orientResChange.on/off` 及 `wakeupEnable/Disable`；事件体按原厂相机同样的 JSON 字符串解析。
- getOrientationSensor(): {available:boolean,x?:number,y?:number,z?:number}，优先通过原生 sensor 命令只读当前状态，缺失时回退到有效事件；停用后返回 unavailable。原生读取 `/dev/input/event7`，核对设备名 `gsensor_orient` 后查询 `ABS_RX/RY/RZ`，不启用 XYZ 连续采样、不抓取或注入系统事件。

原生实现应确保任何成功返回的文件都真实存在、持久化，且位于声明的实际路径；不得仅返回字符串模拟保存。需要验证索引时，应独立确认相册数据库/索引状态，仅在确实完成时返回 indexed:true。

具体相册写入与读取策略见 docs/gallery-storage.md。

局域网分享必须只允许用户明确选择的合法媒体路径，防止路径遍历和软链接越界，不应公开整个 /userdisk/Pictures 目录。使用私网监听、短期凭据，并在切换后台时停止分享服务。

不要在未验证固件相机驱动、编解码器、对焦/变焦权限前将此接口标记为生产可用。

## 0.5.0 新增

`getCapabilities().photoProfiles` 返回 `{id,width,height}[]`。`setPhotoProfile(id)` 只改变软件裁剪；`setCaptureMode('photo'|'video')` 选择匹配的预览比例，录像中禁止切换。横屏比例控件在竖屏拒绝操作。

`capturePhoto()` 新增 `capturedAt`（带毫秒、时区的 ISO 字符串）、`capturedAtEpochMs`、`timeSource:'device-frame-received'`、`processingMs`、`saveMs`。时间取自当前保存帧到达应用的 CLOCK_REALTIME，不代表传感器硬件曝光时间。JPEG 内含同一时间的 EXIF 标签，文件名同源。

分享 URL 下 `f/<index>` 下载原文件并用 RFC 5987 保留原文件名；`p/<index>` 只内嵌预览所选照片；`metadata.json` 返回文件名、体积、文件修改时间和可从 SNAP 文件名恢复的本地拍摄时间。旧文件无相应命名时不提供拍摄时间，原 EXIF 不会被改写。所有入口均受同一临时 token 约束，只读取分享列表内经校验的真实媒体文件。

实际 JS 适配器通过 `photoStart` 启动后台处理并轮询 `photoResult`，避免高分辨率处理阻塞 UI。`close` 和卸载等待已启动的照片任务结束并回收结果；CLI 同步 `photo` 仍供工具使用。

## 0.6.0

`getPreviewFrame()` 返回 image、sequence、captureFps、previewFps、frameAgeMs、convertMs、encodeMs、recording、recordError。原生 JSON 的 image 是本机路径，X7 适配器添加 file://；CameraBridge 保留 frameInfo，过滤重复图片后仍向页面返回字符串或 null。captureFps / previewFps 为会话平均值，frameAgeMs 为处理发布耗时，非显示端端到端延迟。

`audio:true` 表示提供采音实现，不保证设备当前未被其他程序占用。startRecording 必须实际成功打开采音设备后才返回 `{audio:true,audioRate:16000}`；失败不开始无声录像。stopRecording 新增 audioSamples、videoFrames、durationMs、audioXruns，异常则返回 error 与保留文件的 path。退后台走相同的收尾逻辑。

`continuousFocus:true,focus:false`：系统 RKAIQ 管理的连续 AF 状态可读，点击对焦未接通。waitForAutoFocus 通过 focusStart / focusResult 异步等待镜头位置稳定；结果为 settled、position、score、luminance、durationMs，绝不宣称已取得清晰度最优位置。应用不写 V4L2_CID_FOCUS_ABSOLUTE，不创建第二个 AIQ 上下文。capturePhoto({settleFocus:false}) 可跳过连拍后续帧的等待。

依据：Linux V4L2 相机控制文档 https://docs.kernel.org/userspace-api/media/v4l/ext-ctrls-camera.html ，ALSA PCM 接口 https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html 。RKAIQ IPC 源码仅作核对，没有把未验证的命令编号发给系统。

采音设备使用 S16LE/16 kHz/2 通道协商，显式 snd_pcm_start 后读出；合成单声道，4 倍增益与 ±30000 限幅仅用于输出数据。无采样超过 1.2 秒触发录音错误，前台自动收尾，避免只留下空音轨。

前台亮屏 guard 使用固件 `/etc/miniapp/jsapis/libjsapi_export_core.so` 中已核实的 `global.Global.startAlwaysScreenOn()` / `stopAlwaysScreenOn()` 无参数配对方法。没有调用永久亮屏、设置熄屏超时或手动改 CPU governor。

## 0.7.0

`videoProfiles` 返回 id、width、height、portraitWidth、portraitHeight、targetFps。`setVideoProfile(id)` 只改变软件输出几何，录像中拒绝切换，不重新协商 ISP。旧版偏好缺失此字段时使用 hd，持久设置新增 videoProfile。预览按同一画幅缩放，独立于照片比例。

原生 record_submit 只在 MMap 有效期间采样独立录像帧。三个固定槽位依次消费，编码线程仅操作自己的 YUV/RGB 和 JPEG 状态。队列满就丢弃新采样并计数，不无限分配、不把过期图像排成长队；停止会等待正在采样的槽位并收尾待编码帧，然后结束音轨。色彩、变焦按该帧采样时的状态保存。

stopRecord 新增 width、height、droppedFrames。frame 新增 recordSampleMs、recordEncodeMs、recordDropped、recordLimit；这些是应用处理计时，不是屏幕显示延迟。实际 AVI 帧率按已写帧数 / 录制时长计算。

文件/空间限制返回成功保存结果及 stopReason:file-limit|low-space；与音频溢出、写盘失败的 error 区分。每秒检查存储剩余量，前台即使处于非相机面板也处理录制错误/限额；后台和卸载走相同收尾路径。

硬编研究仅保留验证记录 `evidence/hardware-encoder-0.7.0.json`。接口依据：https://github.com/rockchip-linux/mpp/tree/7d6c39741db49a76a28e7648ef00e7dea394ec27 ，并未把当前固件未开放的设备写入内核或临时绑定。

## HDR 与 RK3562/NPU 实验能力（非固件现成接口）

- getCapabilities 可选 hdr、hdrStrategies(isp/bracket)、enhancement、npu、npuReady、npuBackend。
- 只有原生方法 captureHDR({hdrMode:'auto'|'on', exposuresEV:[-2,0,2], saveDirectory:'/userdisk/Pictures', filter, zoom}) 真正实现传感器/ISP 拍照、帧对齐、曝光融合和保存，才能标记 HDR 已就绪。
- 自动模式由原生侧判断场景是否需要 HDR，未合成应返回 hdrApplied:false。
- enhancePhoto({sourcePath, mode:'auto'|'quality', preferNpu:boolean, saveDirectory:'/userdisk/Pictures', preserveOriginal:true}) 应使用经过实际验证的 CPU 或 RKNN 模型处理。
- 回执需声明 written、denoiseApplied、npuUsed，且在真正执行 NPU 模型后才允许 npuUsed:true。
- C++ 参考代码：native/hdr（融合），native/rknn（可选 SDK 推理），文档：docs/hdr-npu.md。
- 0.9.1 已接通 enhanceStart / enhanceResult：内置 FFDNet INT8，实际运行 OEM RKNN，异步回传进度与 npuTimeMs、tiles、processingMs；关闭相机取消任务并释放上下文。capturedAtEpochMs 随请求传递，保留原片与拍摄时间。captureHDR 尚未接通真实多曝光采集，继续禁用。
- 0.9.1 预览期间持续只读观察镜头与清晰度，近期稳定状态可直接复用；未稳定的等待结果不再进入拍摄。原生取帧阶段也检查稳定状态，防止等待结束与实际取帧之间重新寻焦；超时提示重试。不是系统 AF 锁定接口，手持光学效果需要实际复验。

## 0.9.2 快拍与后台契约

`zeroShutterLag:true` 表示 X7 Pro 使用近期全分辨率缓存，快门不等待 `waitForAutoFocus`。`backgroundEnhancement:true` 还要求内置 helper 存在；`queueEnhancement` 在原片保存后持久入队并立即返回 queued/jobId。`queueStatus` 返回 pending/running/workerAlive 和有界记录；`queueResume` 启动有限生命周期 helper。`queueAck` 表示前台完成扫描请求，不代表已查证系统数据库索引。保活字段 keepAlive 决定界面退出后继续或在图块边界暂停。

相册发布在快门返回后异步进行，publicationPending 表示已写盘、索引待确认；不会因为相册扫描或 NPU 未完成而阻止下一次拍摄。旧设备没有零延迟能力时保留原先的等待接口。

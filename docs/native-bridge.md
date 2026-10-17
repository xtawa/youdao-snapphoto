# SnapPhoto / X7 Pro 原生相机接口契约

**原生模块尚未实现。下列方法是 SnapPhoto 期望的接口，不表示原厂固件已经提供。**

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

原生实现应确保任何成功返回的文件都真实存在、持久化，且位于声明的实际路径；不得仅返回字符串模拟保存。需要验证索引时，应独立确认相册数据库/索引状态，仅在确实完成时返回 indexed:true。

具体相册写入与读取策略见 docs/gallery-storage.md。

局域网分享必须只允许用户明确选择的合法媒体路径，防止路径遍历和软链接越界，不应公开整个 /userdisk/Pictures 目录。使用私网监听、短期凭据，并在切换后台时停止分享服务。

不要在未验证固件相机驱动、编解码器、对焦/变焦权限前将此接口标记为生产可用。


## HDR 与 RK3562/NPU 实验能力（非固件现成接口）

- getCapabilities 可选 hdr、hdrStrategies(isp/bracket)、enhancement、npu、npuReady、npuBackend。
- 只有原生方法 captureHDR({hdrMode:'auto'|'on', exposuresEV:[-2,0,2], saveDirectory:'/userdisk/Pictures', filter, zoom}) 真正实现传感器/ISP 拍照、帧对齐、曝光融合和保存，才能标记 HDR 已就绪。
- 自动模式由原生侧判断场景是否需要 HDR，未合成应返回 hdrApplied:false。
- enhancePhoto({sourcePath, mode:'auto'|'quality', preferNpu:boolean, saveDirectory:'/userdisk/Pictures', preserveOriginal:true}) 应使用经过实际验证的 CPU 或 RKNN 模型处理。
- 回执需声明 written、denoiseApplied、npuUsed，且在真正执行 NPU 模型后才允许 npuUsed:true。
- C++ 参考代码：native/hdr（融合），native/rknn（可选 SDK 推理），文档：docs/hdr-npu.md。
- MiniApp 尚未连接 X7 Pro 的原生 Camera/ISP/曝光/硬件 NPU JSAPI。这些接口是约定，不代表真实设备已经实现。

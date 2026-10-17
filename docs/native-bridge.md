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

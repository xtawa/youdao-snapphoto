# SnapPhoto / X7 Pro 原生相机接口契约

此文档定义未来原生模块与前端之间的接口；不表示固件已经提供这些方法。

## JS 侧检测入口

- 优先 globalThis.SnapPhotoNative
- 其次 $falcon.jsapi.snapphoto
- 必须有异步 getCapabilities() 方法，否则明确显示离线状态，不会伪造拍照结果。

## Native API

- getCapabilities(): Promise<{camera:boolean,photo:boolean,video:boolean,preview:boolean,zoom:boolean,focus:boolean,filters:string[],gallery:boolean,share:boolean,minZoom:number,maxZoom:number}>
- openPreview(), getPreviewFrame(): Promise<{image:string}>, closePreview()，取景返回真实 JPEG/PNG data URI；可限帧率和分辨率
- capturePhoto({filter,zoom}): Promise<{path:string}>，完成磁盘写入后再返回
- startRecording({filter,zoom}), stopRecording(): Promise<{path:string}>
- setZoom(zoom:number), focusAt(x:number,y:number)，焦点坐标为 0..1
- setFilter(id:string)，只声明确实支持的滤镜。前端“滤镜”按钮不代表 X7 Pro 已具备实时滤镜能力
- listMedia(): Promise<Array<{path:string,name:string,type:"photo"|"video",size:number}>>
- startShare(paths:string[]): Promise<{url:string}>, stopShare()
- 可选 getDisplaySize(): Promise<{width:number,height:number}>，有助识别真实屏幕方向。前端也能手动调整布局。

## 文件和网络传输

- 推荐在用户可访问的数据分区保留照片，例如 /userdisk/SnapPhoto/；路径必须由硬件实测确认
- 分享时必须把路径限制到自有相册目录，拒绝目录穿越、软链接越界和不属于本应用的文件
- 在私有局域网 IP 上监听，使用强随机 token、短有效期（建议 5 分钟）与允许下载的文件白名单
- 不应在 QuickJS 里模拟一个不存在的 HTTP 服务。真正的二维码扫码下载需要设备端原生 HTTP 服务
- 退出和后台必须停止分享服务；不得无提示常驻监听

## 未完成的真机集成

- X7 Pro 摄像头驱动、图像格式、录像编码器、取景性能
- 自动对焦、变焦实际可支持范围
- 真机原生 JSAPI 模块编译和加载
- 平台是否暴露真实旋转事件及尺寸变化

计时、连拍、相册筛选、九宫格和横竖屏 UI 现为 MiniApp 前端功能；
实际照片仍取决于原生 capturePhoto。

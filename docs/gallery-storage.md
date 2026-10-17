# SnapPhoto X7 Pro：/userdisk/Pictures 原生相册目标

## 路径依据和验证状态

设备用户明确提供了相册路径 **/userdisk/Pictures**。SnapPhoto 将它作为 X7 Pro 首选照片/视频保存与读取目录。
这是**用户给出的设备路径**，没有将之虚构为公开固件文档已验证的固定标准。

- 首选：/userdisk/Pictures
- 原有 SnapPhoto 自有目录：/userdisk/SnapPhoto，仅作为保存失败时的兜底来源
- 不借用 Android /sdcard/DCIM/Camera

## 实现流程

1. capturePhoto({saveDirectory:'/userdisk/Pictures'}) 或 startRecording({saveDirectory:'/userdisk/Pictures'}) 请求原生相机**直接写入**该目录。
2. 原生模块返回实际文件路径。路径落在 /userdisk/Pictures 内时 UI 可报告“文件已写入相册目录”，但并不自动代表官方相册已索引。
3. 若原生摄像头仍将文件写在其他位置，调用 publishToGallery({sourcePath,targetDirectory:'/userdisk/Pictures',mediaType:'photo'|'video'})。
4. 原生模块须确保文件完成写入、持久化和必要媒体索引后返回结果，例如 {path:'/userdisk/Pictures/IMG_...jpg',written:true,indexed:true}。
5. 如果无法复制/发布，保留原始文件，显示警告。**不删除唯一原片**。
6. listMedia({directory:'/userdisk/Pictures',includeFallback:true}) 请求图库优先读取相册目录，同时保留对尚未导入文件的访问（原生适配层需实现）。
7. 把文件写入相册路径和被原厂相册 UI 收录视为两项独立验收：若系统有自己的数据库或索引服务，需要先确认其工作机制。

## 仍需在实际 X7 Pro 上确认

- /userdisk/Pictures 的写权限、容量、映射与是否允许第三方 MiniApp 使用
- 固件是否自动扫描新照片，是否需要额外的相册数据库登记
- 图像格式、文件命名、时间元数据和原厂相册的排序要求
- 视频是否位于同一个目录，是否需要单独注册或缩略图
- MiniApp 原生接口的 camera/文件 API：尚未在 X7 Pro 上实测完成

## 安全检查

需有设备拥有者授权的 ADB：

    adb push tools/x7pro-gallery-diagnose.sh /userdisk/Favorite/
    adb shell "sh /userdisk/Favorite/x7pro-gallery-diagnose.sh"

脚本只检查目录是否存在、可写及挂载信息，不读取用户照片，不进行修改。不要把照片文件、私人文件名或设备序列号公开。

## 工程验证

    npm test
    npm run build:prod

单元测试证明桥接层有正确的路径转发和回退行为，不代表 X7 Pro 原生摄像头服务已经实现。

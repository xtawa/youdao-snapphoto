# SnapPhoto 📸 for Youdao Dictionary Pen

面向有道词典笔 X7 Pro（优先）的第三方 Falcon/HaaS MiniApp 相机应用。

**当前阶段：前端 Alpha / AMR 构建验证。摄像头原生桥接模块尚未完成，故不能宣称真机已经能够拍照、录像或局域网导出。**

## 已实现（前端）

- X7 Pro 横屏主视图、竖屏紧凑布局，自动检测可用的屏幕尺寸，并可手动切换“自动 / 竖屏 / 横屏”
- 拍照、录像模式与硬件可用性检测
- 可取消的 0/3/5/10 秒倒计时，1/3/5 张连拍，后台退出取消剩余拍摄
- 网格辅助线、对焦点选择、变焦控制和按设备可用能力显示的滤镜
- 相册列表、照片/视频筛选、多选/全选、局域网分享二维码显示
- 存储 API 可用时记住拍摄设置；不可用时默认配置
- JS 契约测试，GitHub Actions 生成 AMR 产物

## 需要 X7 Pro 原生适配后才可用

**真实取景、保存照片/视频、对焦和变焦、滤镜运算、读取本地影像、局域网文件服务器** 都需要经过验证的原生接口。前端在未检测到接口时会明确提示，绝不以占位图伪装拍照成功。

参考 docs/native-bridge.md。

## 从 GitHub Actions 获取 AMR

打开 Actions -> Build X7 Pro SnapPhoto AMR -> 选择成功运行 -> Artifacts，下载 SnapPhoto-X7Pro-AMR-UNVERIFIED。下载和安装仅供界面测试，尚未进行 X7 Pro 真机验收。

## 本地构建

Node.js 18（兼容旧版 aiot-cli）：

    npm install
    npm test
    npm run build:prod

使用已有 ADB 权限手动安装包：

    adb push <your-build>.amr /userdisk/Favorite/
    adb shell "miniapp_cli install /userdisk/Favorite/<your-build>.amr"

## 开发说明

- docs/native-bridge.md：X7 Pro 原生摄像头桥接方法和 LAN 安全要求
- docs/app-retention.md：系统删除第三方应用的调查、备份和恢复方案
- tools/x7pro-diagnose.sh：只读硬件探测
- tools/miniapp-retention-diagnose.sh：只读第三方 App 保留情况探测

此项目并非网易有道官方应用。

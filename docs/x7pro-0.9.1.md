# 0.9.1：PR #2 与真实 NPU 接通

[PR #2](https://github.com/xtawa/youdao-snapphoto/pull/2) 已合入远程 main，提交 `4a851a909a2967a383f6c635b085e2db6c21069b`。本地整合此前 0.9.0 的完整 X7 Pro 适配，并已构建、安装 0.9.1。原源码、未提交补丁及安全 stash 均保留。

内置 MIT 授权的 FFDNet color INT8 模型，匹配 PenOS 4.34.0 的原厂 RKNN 1.5.1b18 / 驱动 0.9.1，12 层卷积真实运行在 NPU。见 `assets/models/ffdnet.json`、`evidence/npu-photo-0.9.1.json`、`evidence/npu-full-photo-0.9.1.json`。

本版最初仍等待对焦与拍后处理，用户实机反馈快门等待过长。因此已由 [0.9.2](x7pro-0.9.2.md) 替换：不再阻塞等待 AF 或 NPU，使用近期预览帧快拍和独立后台队列。

CPU HDR 融合参考实现已合并，但不同曝光采集与对齐未接通，真机 HDR 继续禁用。没有修改系统运行库、驱动、启动镜像、电源或 ADB 持久化。

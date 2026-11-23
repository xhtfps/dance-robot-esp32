# ESP32 本地姿态识别

工程现在支持两种互斥视觉源：

- `ESP32 local ESP-DL`：OV2640 在 ESP32-S3 上采集和推理，不启动 WiFi，也不依赖 K230。
- `PC pose runner`：电脑运行高精度 MMPose/RTMPose，ESP32 不启动视觉链路。

旧的 K230 UART 视觉模式已移除。底层 K230 链路源码暂时保留给旧助手模块兼容，但不再作为姿态识别视觉源，也不会由主程序启动。

三组动作及编号完全沿用 `D:\机器人\视觉`：

| 组别 | 6 个动作 |
|---|---|
| 1 | 大字站、弓箭步、举双手、蹲下、立正、单手举手 |
| 2 | 大字站、弓箭步、举双手、蹲下、单膝抬起、俯身 |
| 3 | 大字站、弓箭步、举双手、蹲下、单臂侧平举、双臂交叉 |

## 启用本地模式

先执行 `idf.py menuconfig`：

1. `Robot vision -> Vision source` 选择 `ESP32 local ESP-DL`。
2. 设置 `Action team` 为 1、2 或 3。
3. 设置 `Stable frames`，建议 4。
4. 在 `models: coco_pose` 中选择 `YOLO11N_POSE_S8_V2`。
5. 模型位置选择 `flash_partition`，并启用对应的 `flash coco_pose_yolo11n_pose_s8_v2`。
6. `Partition Table -> Custom partition table CSV` 选择 `partitions-16MiB-pose.csv`。

然后编译、烧录：

```powershell
idf.py -p COMx build flash monitor
```

首次烧录需要把 `coco_pose` 模型分区一起烧入。模型需要约 8 MiB Flash 和 8 MiB PSRAM；请先确认开发板确实有足够 PSRAM。

## 性能预期

ESP-DL 官方 ESP32-S3 的 YOLO11n-Pose 参考模型推理约 27.8 秒/帧，因此当前本地方案定位为“低频静态动作确认”，不是实时视频识别。电脑端 RTX 5060 方案用于实时和更高精度识别；如果以后要让 ESP32 实时，需要训练更小的蒸馏关键点模型，不能只靠改代码解决。

本地推理链路为：

```text
OV2640 JPEG -> ESP JPEG 解码 -> RGB888 -> ESP-DL COCO Pose -> 17 个关键点
-> 纯 C 三组动作分类器 -> 稳定帧确认
```

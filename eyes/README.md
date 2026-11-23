# 电脑端组一姿态识别

本目录只保留电脑端姿态识别。组一通过 `--team 1` 选择；旧 K230 根目录入口和其他组目录已清理。

## 目录结构

```text
pc_pose/                      电脑端推理、分类和浏览器画面
  main.py                     启动入口
  classifier.py               姿态分类和稳定帧判断
  backends/                   Ultralytics/MMPose 后端
  mjpeg.py                    ESP32 MJPEG 读取
  web_overlay.py              浏览器叠加画面服务器
shared/                       姿态编号和 JSON 契约
tests/                        PC 端和契约测试
docs/                         使用说明和设计记录
yolo11n-pose.pt               默认姿态模型
```

## 启动组一识别

```powershell
cd D:\robot\视觉
conda activate robot-pose
$env:ROBOT_WIFI_IFINDEX = "12"

python -u -m pc_pose.main `
  --team 1 `
  --backend ultralytics `
  --model yolo11n-pose.pt `
  --source http://192.168.4.1:81/stream `
  --esp32-url http://192.168.4.1/api/vision/pose
```

浏览器打开：

```text
http://127.0.0.1:8080/
```

当前只保留浏览器画面，不再弹出第二个 OpenCV 窗口。

## 依赖和测试

```powershell
python -m pip install -r pc_pose/requirements.txt
python -B -m unittest discover -s tests -v
```

组一视觉动作固定为六个：大字站、弓箭步、举双手、蹲下、叉腰、敬礼。

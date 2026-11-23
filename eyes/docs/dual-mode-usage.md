# 组一姿态识别使用说明

当前电脑端只运行组一，启动参数为 `--team 1`。

## 允许动作

组一使用共享姿态合同中的基础动作：

| 编号 | 动作 |
|---:|---|
| 1 | 大字站 |
| 2 | 弓箭步 |
| 3 | 举双手 |
| 4 | 蹲下 |
| 5 | 叉腰 |
| 6 | 敬礼 |

## 启动

```powershell
cd D:\robot\视觉
python -m pip install -r pc_pose/requirements.txt
python -m pc_pose.main --team 1 --source http://192.168.4.1:81/stream --esp32-url http://192.168.4.1/api/vision/pose
```

浏览器打开 `http://127.0.0.1:8080/` 查看唯一的姿态画面。

## 代码边界

- `shared/pose_contract.py`：姿态编号合同和结果 JSON。
- `pc_pose/`：电脑推理、分类、ESP32 MJPEG 读取和浏览器叠加。
- `tests/`：PC 端和姿态契约测试。
- ESP32 固件位于 `D:\robot\ESP32`。

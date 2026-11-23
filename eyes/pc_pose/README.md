# PC RTX 5060 Pose Runner

The PC runner uses the group-1 contract with `--team 1`, a YOLO pose backend on
`cuda:0`, and one annotated browser stream.

Install the dependencies in `requirements.txt` using a CUDA-enabled PyTorch
wheel that matches the installed NVIDIA driver. Then run one team:

```powershell
python -m pc_pose.main --team 1 --source http://192.168.4.1:81/stream
```

Open `http://127.0.0.1:8080/` to see the browser stream with the 17 joints,
skeleton, bounding box, FPS, and action result drawn on the image. Use
`--backend mmpose` only when the full MMPose/MMCV stack is installed.

The ESP32 AP stream is pinned to the physical WLAN interface so a Mihomo/Clash
TUN adapter cannot intercept `192.168.4.1`. If Windows assigns a different WLAN
interface index, set it before starting the runner, for example:

```powershell
$env:ROBOT_WIFI_IFINDEX = "12"
```

Use `--serial-port COMx` to emit stable results as `RESULT {json}` lines.
Use `--esp32-url http://192.168.4.1/api/vision/pose` to relay visual results.

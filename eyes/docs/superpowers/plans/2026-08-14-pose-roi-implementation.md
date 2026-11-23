# K230 姿态识别 ROI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 适配 V1.8 固件，在保持不限制姿态推理帧率的前提下，通过离线显示、ROI 配置复用和定期垃圾回收提升准确率与流畅性，同时保持 LCD 和 UART2 接口不变。

**Architecture:** `K230PoseRecognizer` 首帧及每第三帧使用全图；中间 ROI 帧复用已构建的 AI2D crop、letterbox、resize 图，不在每帧重建。ROI 外扩 35%，模型后处理得到的局部框与关键点统一映射回 1280×720 原图后，才进入几何分类、OSD 和 UART 输出。`main.py` 默认关闭 IDE 视频回传，LCD 硬件视频和姿态 OSD 仍然运行。

**Tech Stack:** CanMV K230 V1.8、nncase 2.11、K230 AI2D、AIBase、aidemo、ulab、unittest。

## Global Constraints

- 固定使用 `/sdcard/examples/kmodel/yolov8n-pose.kmodel` 或现有 320×320 候选路径；移除 640 模型自动加载。
- AI 通道为 `1280×720`；LCD 显示为 `800×480`。
- 每个摄像头帧至多执行一次 `self.model.run(frame)`。
- 不限制姿态推理帧率；全图刷新周期为 3 帧；ROI 外扩比例为 0.35；ROI 失败后下一帧回到全图。
- ROI 帧复用 AI2D 配置；只有全图/ROI 模式切换或 ROI 真正变化时才重建配置。
- `ENABLE_IDE_PREVIEW=False` 时不向 IDE 回传视频；LCD 和 UART2 功能不受影响。
- `gc.collect()` 按帧间隔执行，不在每帧调用。
- 输出的 `bbox`、`keypoints` 一律为原图坐标，UART2 `START` / `RESULT` 协议不改变。

---

### Task 1: 恢复固定 320 模型加载

**Files:**
- Modify: `main.py:115-306`
- Modify: `README.md:10-23`
- Modify: `tests/test_pose_model_path.py:1-43`

**Interfaces:**
- Consumes: `POSE_MODEL_CANDIDATES` 和 `find_pose_model_path(exists_fn=None)`。
- Produces: `create_pose_recognizer()` 只向 `K230PoseRecognizer` 传入 `kmodel_path` 与 `rgb888p_size`，使其使用默认 `(320, 320)` 输入。

- [ ] **Step 1: Write the failing test**

将 640 模型专用测试替换为以下行为测试：

```python
def test_create_recognizer_uses_default_320_input(self):
    import main
    received = {}
    model_path = "/sdcard/examples/kmodel/yolov8n-pose.kmodel"

    class RecordingRecognizer:
        def __init__(self, **kwargs):
            received.update(kwargs)

    saved = (main.CANMV_AVAILABLE, main.POSE_RECOGNITION_AVAILABLE,
             main.K230PoseRecognizer, main._model_file_exists)
    try:
        main.CANMV_AVAILABLE = True
        main.POSE_RECOGNITION_AVAILABLE = True
        main.K230PoseRecognizer = RecordingRecognizer
        main._model_file_exists = lambda path: path == model_path
        recognizer = main.create_pose_recognizer()
    finally:
        (main.CANMV_AVAILABLE, main.POSE_RECOGNITION_AVAILABLE,
         main.K230PoseRecognizer, main._model_file_exists) = saved

    self.assertIsInstance(recognizer, RecordingRecognizer)
    self.assertEqual(received["kmodel_path"], model_path)
    self.assertNotIn("model_input_size", received)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
python -B -m unittest tests.test_pose_model_path.PoseModelPathTest.test_create_recognizer_uses_default_320_input
```

Expected: FAIL，因为当前实现会传入 `(640, 640)` 或继续搜索 640 候选路径。

- [ ] **Step 3: Write minimal implementation**

在 `main.py` 中删除 `POSE_640_MODEL_CANDIDATES`、`find_pose_model_paths()` 和 `pose_model_input_size()`；恢复以下路径表与单一路径选择：

```python
POSE_MODEL_CANDIDATES = (
    POSE_KMODEL_PATH,
    "/sdcard/examples/kmodel/yolov8n-pose.kmodel",
    "/sdcard/kmodel/yolov8n-pose.kmodel",
    "/sdcard/yolov8n-pose.kmodel",
    "/data/app/tests/kmodel/yolov8n-pose.kmodel",
    "/data/examples/kmodel/yolov8n-pose.kmodel",
    "/data/kmodel/yolov8n-pose.kmodel",
    "/data/yolov8n-pose.kmodel",
)
```

并恢复：

```python
recognizer = K230PoseRecognizer(
    kmodel_path=model_path,
    rgb888p_size=(IMAGE_WIDTH, IMAGE_HEIGHT),
)
```

更新 README，删除 640 文件名和自动回退说明。

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
python -B -m unittest tests.test_pose_model_path
```

Expected: PASS。

- [ ] **Step 5: Commit**

```powershell
git add main.py README.md tests/test_pose_model_path.py
git commit -m "refactor: restore fixed pose model loading"
```

### Task 2: 增加可测试的 ROI 几何辅助函数

**Files:**
- Modify: `pose_recognition.py:1-110`
- Modify: `tests/test_pose_recognition.py:1-180`

**Interfaces:**
- Produces: `make_roi(bbox, source_size, margin=0.35)`，返回 `(x, y, w, h)` 整数 ROI。
- Produces: `map_roi_detections(boxes, keypoints, roi)`，返回原图坐标的 boxes 和 keypoints。

- [ ] **Step 1: Write the failing tests**

```python
def test_make_roi_expands_box_and_clamps_to_source_bounds(self):
    self.assertEqual(
        make_roi([10, 20, 110, 220], (1280, 720)),
        (0, 0, 145, 290),
    )

def test_map_roi_detections_restores_original_coordinates(self):
    boxes, keypoints = map_roi_detections(
        [[10, 20, 110, 220]],
        [[[30, 40, 0.9]]],
        (100, 50, 400, 500),
    )
    self.assertEqual(boxes, [[110.0, 70.0, 210.0, 270.0]])
    self.assertEqual(keypoints, [[[130.0, 90.0, 0.9]]])
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```powershell
python -B -m unittest tests.test_pose_recognition.PoseRecognitionTest.test_make_roi_expands_box_and_clamps_to_source_bounds tests.test_pose_recognition.PoseRecognitionTest.test_map_roi_detections_restores_original_coordinates
```

Expected: FAIL with missing helper functions.

- [ ] **Step 3: Write minimal implementation**

Implement `make_roi` with floor/ceil expansion, clamping, and at least one-pixel width/height. Implement `map_roi_detections` by adding `(roi_x, roi_y)` to every box corner and each keypoint `x, y`, retaining confidence unchanged.

- [ ] **Step 4: Run tests to verify they pass**

Run the two tests from Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add pose_recognition.py tests/test_pose_recognition.py
git commit -m "feat: add pose roi coordinate helpers"
```

### Task 3: 接入 AI2D ROI 预处理与帧调度

**Files:**
- Modify: `pose_recognition.py:408-576`
- Modify: `tests/test_pose_recognition.py:1-230`

**Interfaces:**
- Consumes: `make_roi`、`map_roi_detections`。
- Produces: `K230PoseRecognizer.process(frame)`；结果字段保持 `task`、`count`、`bbox`、`keypoints`、`pose`、`stable` 不变。

- [ ] **Step 1: Write the failing integration test**

```python
def test_recognizer_uses_four_roi_frames_then_refreshes_full_frame(self):
    class FakeModel:
        def __init__(self):
            self.crops = []

        def config_preprocess(self, crop=None):
            self.crops.append(crop)

        def run(self, frame):
            crop = self.crops[-1]
            if crop is None:
                return [[[100, 100, 300, 500]], [[[150, 200, 0.9]] * 17]]
            return [[[50, 100, 250, 500]], [[[100, 200, 0.9]] * 17]]

    # 使用含 FakeModel 的 K230PoseRecognizer 连续 process 六次；断言：
    # fake_model.crops == [None, (30, 0, 340, 640), None, (30, 0, 340, 640)]
    # 并断言第 2 帧结果的 bbox == [100.0, 100.0, 300.0, 500.0]，关键点
    # 第一个点 == [150.0, 200.0, 0.9]；ROI 帧不重复调用 config_preprocess。
```

另加：ROI 返回空检测后，下一次 `process` 的 `config_preprocess` 参数必须为 `None`，并且分类器历史被清空。

- [ ] **Step 2: Run tests to verify they fail**

Run:

```powershell
python -B -m unittest tests.test_pose_recognition.PoseRecognitionTest.test_recognizer_uses_four_roi_frames_then_refreshes_full_frame
```

Expected: FAIL，因为当前识别器没有 ROI 状态或 `config_preprocess(crop)` 接口。

- [ ] **Step 3: Write minimal implementation**

在 `_PersonKeyPointApp.config_preprocess` 中增加可选参数 `crop=None`：

```python
def config_preprocess(self, crop=None):
    self.ai2d = Ai2d(self.debug)
    self.ai2d.set_ai2d_dtype(
        nn.ai2d_format.NCHW_FMT, nn.ai2d_format.NCHW_FMT,
        np.uint8, np.uint8,
    )
    source_w, source_h = self.full_rgb888p_size
    if crop is not None:
        x, y, w, h = crop
        self.ai2d.crop(x, y, w, h)
        postprocess_size = (w, h)
    else:
        postprocess_size = (source_w, source_h)
    # 根据 postprocess_size 计算 letterbox pad，随后 resize、build。
```

让 `postprocess` 使用当前 ROI 的 `(height, width)`；在 `K230PoseRecognizer.process` 中维护 `self.roi` 与 `self.roi_frames_since_full`，每四次 ROI 后传入 `crop=None`。ROI 结果经 `map_roi_detections` 映射后再选最大框、调用 `classifier.update`，并用该框生成下一轮 ROI。

- [ ] **Step 4: Run tests to verify they pass**

Run:

```powershell
python -B -m unittest tests.test_pose_recognition
```

Expected: PASS。

- [ ] **Step 5: Commit**

```powershell
git add pose_recognition.py tests/test_pose_recognition.py
git commit -m "feat: add tracked roi pose inference"
```

### Task 4: 回归验证与真机验收说明

**Files:**
- Modify: `README.md:1-70`
- Test: `tests/test_direct_camera_lcd.py`

**Interfaces:**
- Consumes: 不改变 `main.py` 的摄像头调用与 `host_uart.py` 的 UART2 协议。
- Produces: 部署说明和可复现实机验收步骤。

- [ ] **Step 1: Run the existing one-inference-per-frame regression test**

Run:

```powershell
python -B -m unittest tests.test_direct_camera_lcd.DirectCameraLcdTest.test_camera_loop_continuously_recognizes_and_start_returns_latest_pose
```

Expected: PASS；两帧显示循环只调用两次识别器 `process`，不会因 ROI 增加第二次模型调用。

- [ ] **Step 2: Update deployment and verification instructions**

在 README 增加以下真机验收项：启动日志必须仍显示 `320x320`；人体入镜后观察三帧一次的全图刷新；分别测试十种动作，每个动作保持 3 秒；记录 FPS 和所有 `snapshot chn(2) failed` 日志。

- [ ] **Step 3: Run full verification**

Run:

```powershell
python -B -m unittest discover -s tests -p "test_*.py"
python -B -m py_compile main.py pose_recognition.py host_uart.py
git diff --check
```

Expected: all unit tests pass, no Python syntax errors, no whitespace errors.

- [ ] **Step 4: Commit**

```powershell
git add README.md tests/test_direct_camera_lcd.py
git commit -m "docs: add pose roi device verification"
```

### Task 5: V1.8 离线运行与资源调度

**Files:**
- Modify: `main.py:90-110,545-552,699`
- Modify: `tests/test_direct_camera_lcd.py:260-330`
- Modify: `README.md:10-45`

**Interfaces:**
- Produces: `ENABLE_IDE_PREVIEW=False`，供 `Display.init(to_ide=...)` 使用。
- Produces: `GC_INTERVAL_FRAMES=30`，供摄像头循环定期执行 `gc.collect()` 使用。
- Does not change: `HostUart`、`START\n` / `RESULT` 协议、摄像头通道和模型输入尺寸。

- [ ] **Step 1: Write the failing tests**

修改 `test_camera_init_matches_working_reference_project`，断言 `Display.init`
的 `to_ide` 为 `False`；增加 30 次循环测试并替换 `gc.collect`，断言只调用一次。

- [ ] **Step 2: Run tests to verify they fail**

```powershell
python -B -m unittest tests.test_direct_camera_lcd.DirectCameraLcdTest.test_camera_init_matches_working_reference_project tests.test_direct_camera_lcd.DirectCameraLcdTest.test_camera_loop_collects_garbage_once_per_interval
```

Expected: FAIL，当前仍将视频发送到 IDE 且每帧执行 GC。

- [ ] **Step 3: Write minimal implementation**

在 `main.py` 中加入 `GC_INTERVAL_FRAMES = 30` 和 `ENABLE_IDE_PREVIEW = False`，将
`Display.init(..., to_ide=True)` 改为 `to_ide=ENABLE_IDE_PREVIEW`，并把循环末尾的
`gc.collect()` 改为 `if frame_count % GC_INTERVAL_FRAMES == 0: gc.collect()`。

- [ ] **Step 4: Run the tests to verify they pass**

运行 Step 2 的命令，Expected: PASS。

- [ ] **Step 5: Update documentation**

README 必须说明离线默认关闭 IDE 回传、每 30 帧 GC、每 3 帧全图刷新、ROI 外扩 35%，
并说明将 `ENABLE_IDE_PREVIEW` 改为 `True` 才启用在线画面预览。

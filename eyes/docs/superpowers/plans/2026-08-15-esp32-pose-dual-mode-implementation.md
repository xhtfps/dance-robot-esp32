# ESP32 Dual-Mode Three-Team Pose Recognition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a high-accuracy PC pose runner and an optional Wi-Fi-independent ESP32-S3 local pose path while preserving the exact three team action sets already defined in `D:\机器人\视觉`.

**Architecture:** Keep the existing K230 package and UART2 protocol intact. Add a pure Python PC adapter around a GPU pose backend and reuse the existing geometry classifier; add a C/C++ ESP32 pose contract and classifier, then connect the OV2640 capture path to Espressif's `COCOPose` model behind an explicit source-mode configuration. The ESP32 local path is a low-frequency static-recognition proof of concept until a smaller distilled keypoint model is available.

**Tech Stack:** Python 3, OpenCV, MMPose/RTMPose on `cuda:0`, PySerial, ESP-IDF, ESP32-S3, OV2640, ESP-DL `COCOPose`/YOLO11n-Pose, Unity tests.

## Global Constraints

- The exact action IDs are `big_stand=1`, `bow_step=2`, `double_hands=3`, `squat=4`, `attention=5`, `knee_raise=6`, `one_arm_side=7`, `one_hand_up=8`, `crossed_arms=9`, `bend_forward=10`.
- Team 1 allow-list is `(1, 2, 3, 4, 5, 8)`; Team 2 is `(1, 2, 3, 4, 6, 10)`; Team 3 is `(1, 2, 3, 4, 7, 9)`.
- `unknown` is ID `0`; incomplete keypoints, low confidence, and rule conflicts return `unknown` rather than forcing a best match.
- Existing K230 files, Team packages, UART2 `START\n` / `RESULT {json}\n` behavior, and unrelated dirty worktree changes must remain intact.
- ESP32 LOCAL mode must not require Wi-Fi; Wi-Fi preview and HTTP APIs are optional diagnostics.
- No production implementation is written before its focused failing test exists and has been observed failing.
- The local ESP32 YOLO11n-Pose path must report measured latency and must not claim real-time performance without hardware evidence.

---

### Task 1: Add the shared Python action contract

**Files:**
- Create: `shared/__init__.py`
- Create: `shared/pose_contract.py`
- Create: `tests/test_pose_contract.py`

**Interfaces:**
- Produces `POSE_LABELS`, `POSE_LABELS_CN`, `TEAM_ALLOWED_POSE_IDS`, `team_pose_ids(team_id)`, and `make_result(team_id, pose_id, confidence, stable, source, fps=0.0)`.
- `team_pose_ids` accepts only `1`, `2`, or `3` and raises `ValueError` for every other value.

- [ ] **Step 1: Write the failing contract tests**

```python
from shared.pose_contract import (
    POSE_LABELS, POSE_LABELS_CN, TEAM_ALLOWED_POSE_IDS, team_pose_ids,
)


def test_three_team_allow_lists_match_existing_packages():
    assert TEAM_ALLOWED_POSE_IDS == {
        1: (1, 2, 3, 4, 5, 8),
        2: (1, 2, 3, 4, 6, 10),
        3: (1, 2, 3, 4, 7, 9),
    }


def test_all_team_labels_are_stable_and_unique():
    assert POSE_LABELS[0] == "unknown"
    assert POSE_LABELS_CN[1] == "大字站"
    assert len(set(POSE_LABELS.values())) == len(POSE_LABELS)


def test_invalid_team_is_rejected():
    try:
        team_pose_ids(4)
    except ValueError:
        pass
    else:
        raise AssertionError("team 4 must be rejected")
```

- [ ] **Step 2: Run the contract tests and observe the expected import failure**

Run: `rtk python -B -m unittest tests.test_pose_contract -v`

Expected: FAIL because `shared.pose_contract` does not exist.

- [ ] **Step 3: Implement the exact constants and result builder**

Use the IDs and labels above verbatim. `make_result` must include `task`, `team`, `pose_id`, `pose`, `pose_cn`, `confidence`, `stable`, `stable_pose_id`, `stable_pose`, `stable_pose_cn`, `fps`, and `source`; it must map unknown IDs to ID `0`.

- [ ] **Step 4: Run the contract tests**

Run: `rtk python -B -m unittest tests.test_pose_contract -v`

Expected: PASS.

- [ ] **Step 5: Commit the isolated contract**

Run: `rtk git add shared tests/test_pose_contract.py` then `rtk git commit -m "feat: add shared three-team pose contract"`.

### Task 2: Build the PC keypoint adapter and team classifier

**Files:**
- Create: `pc_pose/__init__.py`
- Create: `pc_pose/keypoints.py`
- Create: `pc_pose/classifier.py`
- Create: `tests/test_pc_pose_adapter.py`
- Modify: `pose_recognition.py` only if a small import-safe helper is required; do not change its current rules without a regression test.

**Interfaces:**
- `normalize_keypoints(points, image_width, image_height) -> list[list[float]]` returns exactly 17 `[x, y, score]` points in pixel coordinates.
- `TeamPoseClassifier(team_id, stable_frames=4)` exposes `update(keypoints) -> dict` and `reset()`.
- The classifier wraps the existing tested `PoseClassifier` and applies the shared team allow-list.

- [ ] **Step 1: Write failing normalization and allow-list tests**

Test conversion from normalized `(x, y, score)`, pixel `(x, y, score)`, and malformed point counts. Test that Team 1 accepts `attention` but returns `unknown` for `knee_raise`, while Team 2 accepts `knee_raise` and rejects `attention`.

- [ ] **Step 2: Run the focused tests and observe failure**

Run: `rtk python -B -m unittest tests.test_pc_pose_adapter -v`

Expected: FAIL because `pc_pose` does not exist.

- [ ] **Step 3: Implement normalization and the thin classifier wrapper**

Keep model-specific result parsing out of the classifier. Reject `None`, fewer than 17 points, non-finite coordinates, or missing score values by returning a 17-point list with zero scores so the existing classifier safely returns `unknown`.

- [ ] **Step 4: Run the focused tests and the existing pose tests**

Run: `rtk python -B -m unittest tests.test_pc_pose_adapter tests.test_pose_recognition -v`

Expected: PASS.

- [ ] **Step 5: Commit the PC adapter**

Run: `rtk git add pc_pose tests/test_pc_pose_adapter.py pose_recognition.py` then `rtk git commit -m "feat: add PC team pose classifier adapter"`.

### Task 3: Add the RTX 5060 PC inference runner

**Files:**
- Create: `pc_pose/backends/__init__.py`
- Create: `pc_pose/backends/mmpose_backend.py`
- Create: `pc_pose/main.py`
- Create: `pc_pose/requirements.txt`
- Create: `tests/test_pc_runner.py`
- Modify: `README.md`

**Interfaces:**
- `MMPoseBackend(model="human", device="cuda:0")` lazily creates `MMPoseInferencer` and exposes `infer(frame) -> list[dict]`.
- `run_source(source, team_id, backend, serial_port=None, display=True)` consumes a webcam index, a video path, or the ESP32 MJPEG URL.
- Each detection dictionary contains `keypoints`, `bbox`, and `score`; the runner selects the highest-scoring person and passes only its 17 keypoints to `TeamPoseClassifier`.

- [ ] **Step 1: Write failing backend-shape and JSON output tests**

Use a small fake backend object in tests (not a production mock) that returns one COCO-17 detection. Assert that the runner emits the shared result fields, applies the requested team, and never emits an action outside the selected team.

- [ ] **Step 2: Run the tests and observe failure**

Run: `rtk python -B -m unittest tests.test_pc_runner -v`

Expected: FAIL because the backend and runner do not exist.

- [ ] **Step 3: Implement the backend and CLI**

Use MMPose's `human` RTMPose alias, `device="cuda:0"` by default, and lazy imports so contract tests do not require MMPose. Add CLI options:

```text
--team {1,2,3}
--source SOURCE
--device cuda:0
--model human
--serial-port COMx
--no-display
--jsonl PATH
```

The default source is `http://192.168.4.1/stream`; a numeric source is treated as a local camera index. Serial output is one UTF-8 `RESULT {json}\n` line per stable action change, with a 200 ms minimum interval.

- [ ] **Step 4: Run tests and compile all PC modules**

Run: `rtk python -B -m unittest tests.test_pc_runner tests.test_pc_pose_adapter tests.test_pose_recognition -v` and `rtk python -B -m compileall -q pc_pose shared`.

Expected: PASS and no syntax errors.

- [ ] **Step 5: Document installation and commands**

Document the CUDA PyTorch installation, MMPose installation, model download behavior, and:

```text
python -m pc_pose.main --team 1 --source http://192.168.4.1/stream
python -m pc_pose.main --team 2 --source 0
python -m pc_pose.main --team 3 --source video.mp4 --no-display --jsonl results.jsonl
```

- [ ] **Step 6: Commit the PC runner**

Run: `rtk git add pc_pose tests/test_pc_runner.py README.md` then `rtk git commit -m "feat: add RTX PC pose runner"`.

### Task 4: Port the three-team classifier contract to ESP32 C

**Files:**
- Create: `D:\机器人\ESP32\components\BSP\POSE\pose_contract.h`
- Create: `D:\机器人\ESP32\components\BSP\POSE\pose_contract.c`
- Create: `D:\机器人\ESP32\components\BSP\POSE\pose_classifier.h`
- Create: `D:\机器人\ESP32\components\BSP\POSE\pose_classifier.c`
- Create: `D:\机器人\ESP32\tests\test_pose_classifier.c`
- Modify: `D:\机器人\ESP32\components\BSP\CMakeLists.txt`

**Interfaces:**
- `pose_keypoint_t { float x; float y; float score; }` uses COCO-17 order and pixel or normalized coordinates consistently within one call.
- `pose_classifier_init(pose_classifier_t *self, uint8_t team_id, uint8_t stable_frames)` validates Team 1–3.
- `pose_classifier_update(pose_classifier_t *self, const pose_keypoint_t points[17], pose_result_t *out)` applies the same visible-point, geometry, allow-list, priority, and stability semantics as Python.
- `pose_classifier_reset(pose_classifier_t *self)` clears history and stable state.

- [ ] **Step 1: Add failing Unity tests for the public contract**

Add synthetic COCO-17 fixtures for all shared actions and the six team-specific actions. Assert Team 1/2/3 allow-list behavior, mirrored bow-step behavior, missing ankle behavior, and stable-frame activation only after the configured consecutive frames.

- [ ] **Step 2: Run the ESP-IDF tests before implementation**

Run from `D:\机器人\ESP32`: `rtk idf.py build` after registering the test source, or run the host-compilable classifier test command defined by the project if the ESP-IDF environment is unavailable.

Expected: FAIL because the POSE component and classifier symbols do not exist.

- [ ] **Step 3: Implement the C contract and classifier**

Keep camera access, model access, UART, and UI out of the classifier. Use explicit state in `pose_classifier_t`; do not use mutable global action history. Port the Python geometry formulas with `float`, normalized scale, finite-value checks, and a final unknown result for unresolved conflicts.

- [ ] **Step 4: Register the component and run tests**

Add `POSE` source/include directories to the BSP component and run the focused test plus a full firmware build.

Expected: all classifier tests pass and the existing K230 firmware still builds.

- [ ] **Step 5: Commit the ESP32 classifier**

Run: `rtk git add components/BSP/POSE components/BSP/CMakeLists.txt tests/test_pose_classifier.c` then `rtk git commit -m "feat: add ESP32 three-team pose classifier"`.

### Task 5: Add the ESP-DL local pose inference adapter

**Files:**
- Create: `D:\机器人\ESP32\components\BSP\POSE\pose_inference.h`
- Create: `D:\机器人\ESP32\components\BSP\POSE\pose_inference.cpp`
- Create: `D:\机器人\ESP32\components\BSP\POSE\Kconfig`
- Modify: `D:\机器人\ESP32\components\BSP\CMakeLists.txt`
- Modify: `D:\机器人\ESP32\main\idf_component.yml`
- Modify: `D:\机器人\ESP32\partitions-16MiB.csv`
- Create: `D:\机器人\ESP32\docs\local-pose-model.md`

**Interfaces:**
- `pose_inference_init()` loads `COCOPose::YOLO11N_POSE_S8_V1` from the ESP-DL model location selected by menuconfig.
- `pose_inference_run(const uint8_t *rgb888, uint16_t width, uint16_t height, pose_keypoint_t out[17])` returns one highest-score person or `ESP_ERR_NOT_FOUND`.
- `pose_inference_deinit()` releases the model and buffers.

- [ ] **Step 1: Add a failing compile/configuration test**

Add a configuration assertion that the POSE component exposes `CONFIG_POSE_LOCAL_ENABLED`, `CONFIG_POSE_TEAM_ID`, and the `coco_pose` model partition label. The test must fail before the Kconfig and dependency entries exist.

- [ ] **Step 2: Add ESP-DL and the model partition configuration**

Use the official `espressif/coco_pose` ESP-IDF model component and ESP-DL dependency. Select a flash partition named `coco_pose` large enough for the official ESP32-S3 model, preserving NVS, factory, VFS, and storage boundaries. Document the exact model artifact and `idf.py menuconfig` selections instead of silently assuming a model file is present.

- [ ] **Step 3: Implement the C++ adapter**

Construct `COCOPose`, create an `dl::image::img_t` for RGB888, call `run`, select the person with the highest detection score, copy 17 keypoints into the C ABI struct, and reject outputs with fewer than 17 keypoints or non-finite coordinates. Keep the adapter independent of Wi-Fi and HTTP.

- [ ] **Step 4: Build the firmware and record the model limitation**

Run `rtk idf.py build`. If the local model dependency or model artifact is unavailable, keep the adapter behind `CONFIG_POSE_LOCAL_ENABLED` and report the exact missing dependency; do not fake a successful local inference build.

- [ ] **Step 5: Commit the local inference adapter**

Run: `rtk git add components/BSP/POSE main/idf_component.yml partitions-16MiB.csv docs/local-pose-model.md` then `rtk git commit -m "feat: add ESP-DL local pose adapter"`.

### Task 6: Wire OV2640 capture, local mode, and optional MJPEG preview

**Files:**
- Create: `D:\机器人\ESP32\components\BSP\POSE\pose_local_task.h`
- Create: `D:\机器人\ESP32\components\BSP\POSE\pose_local_task.c`
- Modify: `D:\机器人\ESP32\main\main.c`
- Modify: `D:\机器人\ESP32\components\BSP\WEB\web.c`
- Modify: `D:\机器人\ESP32\components\BSP\CAMERA\camera.c`
- Modify: `D:\机器人\ESP32\components\BSP\CAMERA\camera.h` only for narrow capture/format APIs.

**Interfaces:**
- `pose_local_start(uint8_t team_id)` initializes the OV2640 once, allocates one RGB conversion buffer in PSRAM, and starts a FreeRTOS task.
- The task captures one frame at a configured interval, converts JPEG to RGB888, runs pose inference, calls the C classifier, and publishes only the latest JSON result through the existing vision state API.
- `GET /stream` is registered only when camera preview is enabled; it must return MJPEG frames and always return each camera frame buffer.

- [ ] **Step 1: Add failing tests for mode isolation**

Test that LOCAL mode does not call Wi-Fi or K230 initialization, K230 mode keeps the current initialization order, and a camera/model failure produces `unknown` without crashing the rest of the firmware.

- [ ] **Step 2: Implement the local task and source-mode choice**

Add a Kconfig choice with K230 as the existing default and LOCAL as the opt-in mode. In LOCAL mode call `camera_init`, `vision_state_init`, and `pose_local_start`; do not call `wifi_init_sta`, `k230_link_start`, or `web_start` unless the diagnostic preview option is enabled. Keep audio, LED, display, and exit initialization unchanged.

- [ ] **Step 3: Add optional preview and result reporting**

Implement `/stream` as a bounded MJPEG loop using the existing camera API. Use the same VGA/JPEG/PSRAM configuration already present in `camera.c`, and report model latency, successful inferences, rejected frames, and current stable action to the serial log.

- [ ] **Step 4: Run Python contract tests and ESP-IDF build**

Run: `rtk python -B -m unittest discover -s tests -v` in `D:\机器人\视觉` and `rtk idf.py build` in `D:\机器人\ESP32`.

Expected: existing K230 tests remain green and the ESP32 project compiles with LOCAL disabled by default.

- [ ] **Step 5: Commit the mode integration**

Run: `rtk git add main/main.c components/BSP/POSE components/BSP/CAMERA components/BSP/WEB` then `rtk git commit -m "feat: add ESP32 local pose mode"`.

### Task 7: Add cross-target documentation and hardware verification tools

**Files:**
- Create: `D:\机器人\视觉\docs\dual-mode-runbook.md`
- Create: `D:\机器人\视觉\tools\compare_pose_results.py`
- Create: `D:\机器人\视觉\tests\test_compare_pose_results.py`
- Modify: `D:\机器人\视觉\README.md`

**Interfaces:**
- `compare_pose_results.py` reads PC JSONL and ESP32 UART `RESULT` JSONL, groups by team/action, and reports agreement, unknown rate, stable latency, and missing fields.
- The runbook documents K230, PC, and ESP32 LOCAL modes, camera pin safety, model flashing, Team selection, and the known ESP32-S3 local latency limitation.

- [ ] **Step 1: Write failing comparison tests**

Cover exact action agreement, unknown disagreement, malformed JSON, and missing `stable_pose_id` fields.

- [ ] **Step 2: Implement the comparison utility**

Use only the shared contract and standard library; do not require a GPU or board to run the report.

- [ ] **Step 3: Run the complete software test set**

Run: `rtk python -B -m unittest discover -s tests -v` and `rtk python -B -m compileall -q pc_pose shared tools`.

- [ ] **Step 4: Document hardware test procedure**

For each Team, hold all six actions for three seconds with the full body visible, record PC and ESP32 results, and compare them. Record ESP32 model latency instead of imposing an unsupported FPS target.

- [ ] **Step 5: Commit documentation and tooling**

Run: `rtk git add README.md docs/dual-mode-runbook.md tools tests/test_compare_pose_results.py` then `rtk git commit -m "docs: add dual-mode pose runbook"`.

## Final Verification

- [ ] Run `rtk python -B -m unittest discover -s tests -v` in `D:\机器人\视觉`.
- [ ] Run `rtk python -B -m compileall -q pc_pose shared tools`.
- [ ] Run `rtk idf.py build` in `D:\机器人\ESP32` with the default K230 source mode.
- [ ] Run a second ESP-IDF build with LOCAL mode enabled and the official `coco_pose` model component available.
- [ ] Confirm `rtk git status --short` shows only pre-existing user changes plus the intended new commits.

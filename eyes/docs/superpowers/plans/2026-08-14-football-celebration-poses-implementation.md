# 足球庆祝动作识别 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用用户提供的 6 个足球庆祝姿势替换当前 6 个扩展动作，同时保留四个基础动作及 UART2 返回格式。

**Architecture:** `PoseClassifier` 继续仅依赖 YOLOv8-pose 的 COCO-17 关键点。先将标签表替换为新的 10 个动作集合，再用归一化几何条件识别低位跪姿、显著手臂动作和站立 SIUU 动作；规则按冲突优先级返回，关键点不足或条件冲突时返回 `unknown`。

**Tech Stack:** Python/MicroPython、CanMV K230、YOLOv8-pose COCO-17、unittest。

## Global Constraints

- 不新增或更换 `.kmodel`，继续使用现有 `yolov8n-pose.kmodel`。
- 不改变 `HostUart`、UART2 `START\n` / `RESULT` 协议、`empty_pose_result()` 字段或 LCD 骨架绘制接口。
- 最终动作 ID 固定为 1 到 10：前四个为基础动作，5 到 10 为 `knee_slide`、`siuuu`、`crossed_arms`、`kneeling_point_up`、`dab`、`hands_by_ears`。
- 所有新动作必须支持左右镜像；低位动作关键点不足时必须返回 `unknown`。
- 代码修改遵循测试先行；每次生产代码修改前均先运行并确认对应测试失败。
- 未经用户明确授权，不执行 Git 提交；每个任务的提交命令仅作为可选交付步骤记录。

---

### Task 1: 替换扩展动作标签与公开常量

**Files:**
- Modify: `pose_recognition.py:16-55`
- Modify: `tests/test_pose_recognition.py:5-18,34-110`
- Modify: `README.md:68-82`

**Interfaces:**
- Consumes: `POSE_LABELS`、`POSE_LABELS_CN`。
- Produces: `POSE_KNEE_SLIDE=5`、`POSE_SIUUU=6`、`POSE_CROSSED_ARMS=7`、`POSE_KNEELING_POINT_UP=8`、`POSE_DAB=9`、`POSE_HANDS_BY_EARS=10`。

- [ ] **Step 1: 写入失败测试**

在 `tests/test_pose_recognition.py` 中将旧扩展动作导入替换为：

```python
from pose_recognition import (
    POSE_KNEE_SLIDE, POSE_SIUUU, POSE_CROSSED_ARMS,
    POSE_KNEELING_POINT_UP, POSE_DAB, POSE_HANDS_BY_EARS,
)

def test_football_celebration_pose_labels_replace_old_extension_poses(self):
    self.assertEqual(pose_recognition.POSE_LABELS[POSE_KNEE_SLIDE], "knee_slide")
    self.assertEqual(pose_recognition.POSE_LABELS[POSE_SIUUU], "siuuu")
    self.assertEqual(pose_recognition.POSE_LABELS[POSE_CROSSED_ARMS], "crossed_arms")
    self.assertEqual(pose_recognition.POSE_LABELS[POSE_KNEELING_POINT_UP], "kneeling_point_up")
    self.assertEqual(pose_recognition.POSE_LABELS[POSE_DAB], "dab")
    self.assertEqual(pose_recognition.POSE_LABELS[POSE_HANDS_BY_EARS], "hands_by_ears")
    self.assertNotIn("attention", pose_recognition.POSE_LABELS.values())
    self.assertNotIn("hands_on_hips", pose_recognition.POSE_LABELS.values())
    self.assertNotIn("hands_on_head", pose_recognition.POSE_LABELS.values())
    self.assertNotIn("one_hand_up", pose_recognition.POSE_LABELS.values())
    self.assertNotIn("bend_forward", pose_recognition.POSE_LABELS.values())
```

- [ ] **Step 2: 运行测试确认失败**

Run:

```powershell
python -B -m unittest tests.test_pose_recognition.PoseRecognitionTest.test_football_celebration_pose_labels_replace_old_extension_poses
```

Expected: FAIL，原因是新的 `POSE_KNEE_SLIDE` 等公开常量尚不存在。

- [ ] **Step 3: 实现最小标签替换**

在 `pose_recognition.py` 中替换 ID 5 到 10 的常量和两个标签字典：

```python
POSE_KNEE_SLIDE = 5
POSE_SIUUU = 6
POSE_CROSSED_ARMS = 7
POSE_KNEELING_POINT_UP = 8
POSE_DAB = 9
POSE_HANDS_BY_EARS = 10
```

英文标签依次为 `knee_slide`、`siuuu`、`crossed_arms`、`kneeling_point_up`、`dab`、`hands_by_ears`；中文标签依次为“滑跪庆祝”、“SIUU 庆祝”、“双臂交叉”、“跪姿指天”、“DAB”、“双手捂耳”。删除 `attention`、`hands_on_hips`、`hands_on_head`、`one_hand_up`、`bend_forward` 的常量和字典项。

- [ ] **Step 4: 运行测试确认通过**

Run:

```powershell
python -B -m unittest tests.test_pose_recognition.PoseRecognitionTest.test_football_celebration_pose_labels_replace_old_extension_poses
```

Expected: PASS。

- [ ] **Step 5: 可选提交（仅在用户授权后）**

```powershell
git add pose_recognition.py tests/test_pose_recognition.py
git commit -m "feat: replace extension pose labels"
```

### Task 2: 为 6 个庆祝动作建立合成骨架测试

**Files:**
- Modify: `tests/test_pose_recognition.py:34-150`

**Interfaces:**
- Consumes: 现有 `skeleton()` 和 `PoseClassifier.classify(kps)`。
- Produces: `knee_slide_skeleton()`、`siuuu_skeleton()`、`kneeling_point_up_skeleton()`、`dab_skeleton(mirrored=False)`、`hands_by_ears_skeleton()` 测试夹具。

- [ ] **Step 1: 写入失败测试**

在 `tests/test_pose_recognition.py` 加入以下行为测试；所有点的置信度为 `1.0`：

```python
def test_football_celebration_poses(self):
    classifier = PoseClassifier()
    self.assertEqual(classifier.classify(knee_slide_skeleton())[0], POSE_KNEE_SLIDE)
    self.assertEqual(classifier.classify(siuuu_skeleton())[0], POSE_SIUUU)
    self.assertEqual(classifier.classify(crossed_arms_skeleton())[0], POSE_CROSSED_ARMS)
    self.assertEqual(
        classifier.classify(kneeling_point_up_skeleton())[0],
        POSE_KNEELING_POINT_UP,
    )
    self.assertEqual(classifier.classify(dab_skeleton())[0], POSE_DAB)
    self.assertEqual(classifier.classify(dab_skeleton(mirrored=True))[0], POSE_DAB)
    self.assertEqual(
        classifier.classify(hands_by_ears_skeleton())[0],
        POSE_HANDS_BY_EARS,
    )

def test_kneeling_point_up_does_not_become_double_hands(self):
    result = PoseClassifier().classify(kneeling_point_up_skeleton())
    self.assertNotEqual(result[0], POSE_DOUBLE_HANDS)

def test_siuuu_does_not_become_big_stand(self):
    result = PoseClassifier().classify(siuuu_skeleton())
    self.assertNotEqual(result[0], POSE_BIG_STAND)
```

夹具在既有 `skeleton()` 返回值上按以下方式改点，所有点置信度保持 `1.0`：

```python
def kneeling_point_up_skeleton():
    points = skeleton()
    points[13], points[14] = (50, 380, 1.0), (150, 380, 1.0)
    points[15], points[16] = (80, 400, 1.0), (120, 400, 1.0)
    points[7], points[8] = (70, 130, 1.0), (130, 130, 1.0)
    points[9], points[10] = (55, 80, 1.0), (145, 80, 1.0)
    return points

def knee_slide_skeleton():
    points = kneeling_point_up_skeleton()
    points[0] = (170, 130, 1.0)
    points[5], points[6] = (160, 200, 1.0), (180, 200, 1.0)
    points[7], points[8] = (160, 260, 1.0), (180, 260, 1.0)
    points[9], points[10] = (160, 310, 1.0), (190, 310, 1.0)
    return points

def siuuu_skeleton():
    points = skeleton()
    points[7], points[8] = (45, 240, 1.0), (155, 240, 1.0)
    points[9], points[10] = (15, 300, 1.0), (185, 300, 1.0)
    return points

def dab_skeleton(mirrored=False):
    points = skeleton()
    left = ((45, 130, 1.0), (15, 75, 1.0))
    right = ((125, 205, 1.0), (70, 230, 1.0))
    if mirrored:
        left, right = ((75, 205, 1.0), (130, 230, 1.0)), ((145, 130, 1.0), (185, 75, 1.0))
    points[7], points[9] = left
    points[8], points[10] = right
    return points

def hands_by_ears_skeleton():
    points = skeleton()
    points[7], points[8] = (50, 145, 1.0), (150, 145, 1.0)
    points[9], points[10] = (70, 125, 1.0), (130, 125, 1.0)
    return points

def crossed_arms_skeleton():
    points = skeleton()
    points[7], points[8] = (65, 220, 1.0), (135, 220, 1.0)
    points[9], points[10] = (125, 240, 1.0), (75, 240, 1.0)
    return points
```

- [ ] **Step 2: 运行测试确认失败**

Run:

```powershell
python -B -m unittest tests.test_pose_recognition.PoseRecognitionTest.test_football_celebration_poses tests.test_pose_recognition.PoseRecognitionTest.test_kneeling_point_up_does_not_become_double_hands tests.test_pose_recognition.PoseRecognitionTest.test_siuuu_does_not_become_big_stand
```

Expected: FAIL，因为现有分类器仍使用旧的扩展动作规则。

- [ ] **Step 3: 保留四个基础动作回归用例**

保留并运行现有 `test_four_competition_poses`；它必须继续覆盖大字站、举双手、
左右镜像弓箭步和蹲下。不要将足球动作夹具放入生产代码。

- [ ] **Step 4: 可选提交（仅在用户授权后）**

```powershell
git add tests/test_pose_recognition.py
git commit -m "test: specify football celebration poses"
```

### Task 3: 实现低位、手臂和 SIUU 分类规则

**Files:**
- Modify: `pose_recognition.py:280-405`
- Test: `tests/test_pose_recognition.py:34-170`

**Interfaces:**
- Consumes: `_distance()`、`_angle()`、`arms_reliable`、`points`、`scale`、`shoulder_y`、`hip_y`。
- Produces: `PoseClassifier.classify(kps) -> (pose_id, confidence)`，可返回 6 个足球庆祝动作之一。

- [ ] **Step 1: 为低位动作添加最小几何辅助量**

在 `classify()` 中计算以下无状态局部变量：

```python
shoulder_center_x = (points[LEFT_SHOULDER][0] + points[RIGHT_SHOULDER][0]) / 2.0
hip_center_x = (points[LEFT_HIP][0] + points[RIGHT_HIP][0]) / 2.0
ankle_height_gap = abs(points[LEFT_ANKLE][1] - points[RIGHT_ANKLE][1]) / scale
left_ankle_near_knee = _distance(points[LEFT_ANKLE], points[LEFT_KNEE]) < 0.95 * scale
right_ankle_near_knee = _distance(points[RIGHT_ANKLE], points[RIGHT_KNEE]) < 0.95 * scale
```

定义低位跪姿为“左右膝角均小于 140°，且至少一只脚踝接近相应膝盖”。定义后仰为
`abs(shoulder_center_x - hip_center_x) > 0.30 * scale`。

- [ ] **Step 2: 按固定优先级实现布尔规则**

在四个基础动作打分之前依次返回：

```python
if kneeling and both_wrists_above_head:
    return POSE_KNEELING_POINT_UP, 0.93
if kneeling and torso_leaned_back and not both_wrists_above_head:
    return POSE_KNEE_SLIDE, 0.90
if dab_left or dab_right:
    return POSE_DAB, 0.92
if hands_by_ears:
    return POSE_HANDS_BY_EARS, 0.92
if crossed_arms:
    return POSE_CROSSED_ARMS, 0.92
if siuuu:
    return POSE_SIUUU, 0.90
```

`both_wrists_above_head` 要求两个腕点都高于 `nose_y - 0.05 * scale`。
`dab_left` 与 `dab_right` 分别检查一侧腕点高于肩部至少 `0.10 * scale` 且在该侧
远离身体中线，另一腕点位于肩髋之间并跨过中线。`hands_by_ears` 要求双腕的
Y 坐标介于鼻子与肩部之间、X 坐标位于鼻子两侧，且双肘在各自肩部外侧。
`siuuu` 要求脚距大于 `1.15 * scale`、双膝角大于 150°、双腕低于肩部
`0.15 * scale`，并且至少一只腕点不满足大字站的水平手臂条件。

- [ ] **Step 3: 删除旧扩展动作条件**

删除当前 `attention`、`hands_on_hips`、`hands_on_head`、`one_hand_up`、
`bend_forward` 的局部变量和早期 `return` 分支。保留基础动作的 `scores` 字典，
但只包含 `POSE_BIG_STAND`、`POSE_BOW_STEP`、`POSE_DOUBLE_HANDS`、`POSE_SQUAT`。

- [ ] **Step 4: 运行新增和基础动作测试确认通过**

Run:

```powershell
python -B -m unittest tests.test_pose_recognition.PoseRecognitionTest.test_football_celebration_poses tests.test_pose_recognition.PoseRecognitionTest.test_kneeling_point_up_does_not_become_double_hands tests.test_pose_recognition.PoseRecognitionTest.test_siuuu_does_not_become_big_stand tests.test_pose_recognition.PoseRecognitionTest.test_four_competition_poses
```

Expected: PASS。

- [ ] **Step 5: 可选提交（仅在用户授权后）**

```powershell
git add pose_recognition.py tests/test_pose_recognition.py
git commit -m "feat: classify football celebration poses"
```

### Task 4: 更新说明并完成回归验证

**Files:**
- Modify: `README.md:68-82`
- Modify: `docs/superpowers/specs/2026-08-14-football-celebration-poses-design.md`
- Test: `tests/test_pose_recognition.py`

**Interfaces:**
- Consumes: 新 `POSE_LABELS` 与 `POSE_LABELS_CN`。
- Produces: 与实际 UART `pose` / `pose_cn` 字段一致的动作说明。

- [ ] **Step 1: 更新 README 动作列表**

将 `README.md` 的“识别动作”章节更新为以下十项：

```text
big_stand：大字站
bow_step：弓箭步
double_hands：举双手
squat：蹲下
knee_slide：滑跪庆祝
siuuu：SIUU 庆祝
crossed_arms：双臂交叉
kneeling_point_up：跪姿指天
dab：DAB
hands_by_ears：双手捂耳
```

- [ ] **Step 2: 运行完整测试套件**

Run:

```powershell
python -B -m unittest discover -s tests -p "test_*.py"
```

Expected: PASS，所有既有 UART、ROI、LCD 和姿态单元测试通过。

- [ ] **Step 3: 检查 Python 语法与差异格式**

Run:

```powershell
python -B -c "for p in ('main.py','pose_recognition.py','host_uart.py'): compile(open(p, encoding='utf-8').read(), p, 'exec'); print('compiled', p)"
git diff --check
```

Expected: 三个文件均输出 `compiled`，且 `git diff --check` 没有输出。

- [ ] **Step 4: 真机验收**

将 `main.py`、`pose_recognition.py` 和 `host_uart.py` 复制到 K230 SD 卡。让人物全身入镜，
分别保持 10 个动作 3 秒；每次由主机发送 `START\n`，检查返回的 `RESULT` 中
`pose`、`pose_cn`、`stable`。对于滑跪和跪姿指天，若脚踝或膝盖骨架点低置信度，
期望结果是 `unknown` 而不是其他动作。

- [ ] **Step 5: 可选提交（仅在用户授权后）**

```powershell
git add README.md docs/superpowers/specs/2026-08-14-football-celebration-poses-design.md
git commit -m "docs: document football celebration poses"
```

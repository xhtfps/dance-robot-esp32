# Three Team Pose Packages Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the unreliable hip/head actions and produce three independent K230 packages, each recognizing the four basic actions plus exactly two assigned extension actions.

**Architecture:** Keep one tested source implementation in the repository root. Add an allow-list to the classifier/recognizer so a team package can suppress all unassigned labels. Copy the runnable K230 files into `team_1`, `team_2`, and `team_3`, each with a fixed six-action allow-list and its own `main.py`, `pose_recognition.py`, and `host_uart.py`.

**Tech Stack:** MicroPython/CanMV K230, YOLOv8 pose `.kmodel`, COCO-17 geometry classifier, UART2 `START\n` / `RESULT {json}\n`, Python `unittest` on the PC.

## Global Constraints

- Every team recognizes exactly `big_stand`, `bow_step`, `double_hands`, `squat`, plus its assigned two extension labels.
- Team 1 extensions: `attention`, `one_hand_up`.
- Team 2 extensions: `knee_raise`, `bend_forward`.
- Team 3 extensions: `one_arm_side`, `crossed_arms`.
- UART remains `START\n` for the host command and `RESULT {json}\n` for the reply.
- The K230 model path and camera/ROI pipeline remain unchanged.
- Do not add LVGL, network dependencies, or a runtime team-selection protocol.

---

### Task 1: Replace the two unreliable action definitions

**Files:**
- Modify: `pose_recognition.py`
- Modify: `tests/test_pose_recognition.py`
- Modify: `README.md`

**Interfaces:**
- Replace IDs 6 and 7 with `POSE_KNEE_RAISE` / `POSE_ONE_ARM_SIDE`.
- Preserve IDs 0–5 and 8–10 semantics except that ID 6/7 now have the new labels.
- `PoseClassifier.classify(kps)` continues returning `(pose_id, confidence)`.

- [ ] **Step 1: Write failing tests for both mirror directions.**

Add cases that expect `knee_raise` when either left or right ankle is raised,
with the raised knee bent and the support knee straight. Add cases that expect
`one_arm_side` when either arm is horizontal beside the shoulder and the other
wrist is below the shoulder. Keep the other body points from `skeleton()` so
the tests exercise the real classifier.

- [ ] **Step 2: Run the targeted tests and verify failure.**

Run:

```powershell
python -m unittest tests.test_pose_recognition.PoseRecognitionTest
```

Expected: the new tests fail because the current classifier still exposes
`hands_on_hips` and `hands_on_head`.

- [ ] **Step 3: Implement the new constants, labels, and geometry rules.**

Use these normalized conditions:

```python
ankle_gap = abs(points[LEFT_ANKLE][1] - points[RIGHT_ANKLE][1]) / scale
left_raised = (
    points[LEFT_ANKLE][1] < points[RIGHT_ANKLE][1] - 0.55 * scale
    and left_knee < 145.0 and right_knee > 145.0
)
right_raised = (
    points[RIGHT_ANKLE][1] < points[LEFT_ANKLE][1] - 0.55 * scale
    and right_knee < 145.0 and left_knee > 145.0
)

left_side_arm = (
    abs(points[LEFT_WRIST][1] - points[LEFT_SHOULDER][1]) < 0.25 * scale
    and abs(points[LEFT_WRIST][0] - shoulder_center_x) > 0.55 * scale
    and left_arm > 150.0
)
right_side_arm = (
    abs(points[RIGHT_WRIST][1] - points[RIGHT_SHOULDER][1]) < 0.25 * scale
    and abs(points[RIGHT_WRIST][0] - shoulder_center_x) > 0.55 * scale
    and right_arm > 150.0
)
one_arm_side = arms_reliable and (
    (left_side_arm and points[RIGHT_WRIST][1] > shoulder_y + 0.35 * scale)
    or (right_side_arm and points[LEFT_WRIST][1] > shoulder_y + 0.35 * scale)
)
```

Check `knee_raise` before squat/bow and `one_arm_side` before big stand.
Remove the old hip/head predicates and labels from the runtime dictionaries.

- [ ] **Step 4: Run the targeted tests and verify they pass.**

Run the same unittest command; expected result is PASS, including the original
four basic actions and the remaining extension-action tests.

- [ ] **Step 5: Update the action table in `README.md`.**

Document the ten shared IDs as `attention`, `one_hand_up`, `knee_raise`,
`bend_forward`, `one_arm_side`, and `crossed_arms` after the four basics.

### Task 2: Add an action allow-list for team-specific recognition

**Files:**
- Modify: `pose_recognition.py`
- Modify: `tests/test_pose_recognition.py`

**Interfaces:**
- `PoseClassifier(allowed_pose_ids=None)` accepts an optional iterable of IDs.
- `K230PoseRecognizer(..., allowed_pose_ids=None)` passes that allow-list to its classifier.
- If a classified ID is not allowed, `classify()` returns `(POSE_UNKNOWN, 0.0)`.

- [ ] **Step 1: Write failing allow-list tests.**

Create a classifier with `allowed_pose_ids={POSE_BIG_STAND}` and feed a known
double-hands skeleton. Assert that the result is `POSE_UNKNOWN`. Create a
second classifier allowing double-hands and assert that the same skeleton is
returned as `POSE_DOUBLE_HANDS`.

- [ ] **Step 2: Run the tests and verify the new allow-list test fails.**

Run:

```powershell
python -m unittest tests.test_pose_recognition.PoseRecognitionTest
```

- [ ] **Step 3: Implement the allow-list at the classifier output boundary.**

Store `self.allowed_pose_ids` as `None` or a `set`. After all geometry rules
select a pose, reject only disallowed non-unknown poses. Keep the classifier's
history/debounce behavior unchanged.

- [ ] **Step 4: Pass `allowed_pose_ids` through `K230PoseRecognizer`.**

Add the optional constructor argument and instantiate:

```python
self.classifier = PoseClassifier(allowed_pose_ids=allowed_pose_ids)
```

- [ ] **Step 5: Run all existing tests.**

Run:

```powershell
python -m unittest discover -s tests
```

Expected: all tests PASS.

### Task 3: Create the three independent K230 packages

**Files:**
- Create: `team_1/main.py`
- Create: `team_1/pose_recognition.py`
- Create: `team_1/host_uart.py`
- Create: `team_2/main.py`
- Create: `team_2/pose_recognition.py`
- Create: `team_2/host_uart.py`
- Create: `team_3/main.py`
- Create: `team_3/pose_recognition.py`
- Create: `team_3/host_uart.py`
- Create: `team_1/README.md`
- Create: `team_2/README.md`
- Create: `team_3/README.md`

**Interfaces:**
- Each package runs independently when its three Python files and the model
  are copied to `/sdcard/` on a K230.
- Each package keeps `HostUart`, `START\n`, and the JSON response format.

- [ ] **Step 1: Copy the tested root runtime files into each team directory.**

Use the root versions only after Tasks 1 and 2 pass. Do not copy tests,
development documents, or unrelated ESP32 modules into team directories.

- [ ] **Step 2: Configure the fixed allow-list in each team main file.**

Pass these exact IDs when constructing `K230PoseRecognizer`:

```python
# team_1
{POSE_BIG_STAND, POSE_BOW_STEP, POSE_DOUBLE_HANDS, POSE_SQUAT,
 POSE_ATTENTION, POSE_ONE_HAND_UP}

# team_2
{POSE_BIG_STAND, POSE_BOW_STEP, POSE_DOUBLE_HANDS, POSE_SQUAT,
 POSE_KNEE_RAISE, POSE_BEND_FORWARD}

# team_3
{POSE_BIG_STAND, POSE_BOW_STEP, POSE_DOUBLE_HANDS, POSE_SQUAT,
 POSE_ONE_ARM_SIDE, POSE_CROSSED_ARMS}
```

- [ ] **Step 3: Add one README per team with upload instructions.**

Each README must state the six labels, model path
`/sdcard/examples/kmodel/yolov8n-pose.kmodel`, UART2 pins/baud inherited from
the root project, and the `START\n` command.

- [ ] **Step 4: Add package-isolation tests.**

Test the source configuration or exported team constants so each package has
exactly six allowed IDs and no extension ID from another team. Do not require
CanMV imports in PC tests.

- [ ] **Step 5: Run package and full regression tests.**

Run:

```powershell
python -m unittest discover -s tests
python -B -c "from pathlib import Path; [compile(p.read_text(encoding='utf-8'), str(p), 'exec') for p in Path('.').glob('team_*/**/*.py')]"
```

Expected: all tests PASS and all nine team files compile.

### Task 4: Final verification and handoff

**Files:**
- Verify: `main.py`, `pose_recognition.py`, `host_uart.py`
- Verify: `team_1/`, `team_2/`, `team_3/`

- [ ] **Step 1: Run whitespace and syntax checks.**

```powershell
rtk git diff --check
python -B -c "from pathlib import Path; [compile(p.read_text(encoding='utf-8'), str(p), 'exec') for p in Path('.').glob('team_*/**/*.py')]"
```

- [ ] **Step 2: Verify the SD-card layout for each team.**

For each team, copy its three Python files to `/sdcard/`, copy the existing
model to `/sdcard/examples/kmodel/`, reboot the board, and confirm the log
contains the model initialization and `Host UART2 command link initialized`.

- [ ] **Step 3: Hand off the three package paths and assigned labels.**

Report the exact team directories, six labels, model path, and the fact that
the host protocol remains unchanged. No runtime team selection is added.

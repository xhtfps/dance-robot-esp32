# Local Pose Competition Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make ESP32 local pose recognition autonomously complete the visual competition loop while preserving BOOT-button switching between local and Wi-Fi/PC modes.

**Architecture:** Keep camera capture, ESP-DL inference, competition state, serial bridge, and Arduino action dispatch in their current owning modules. Add only the application-level handoff from a stable local pose to the existing competition callbacks, and align the ESP32 Team 1 pose IDs with the PC contract (`1,2,3,4,11,12`).

**Tech Stack:** ESP-IDF 5.5.5, ESP32-S3, OV2640, ESP-DL `COCOPose`, FreeRTOS, C/C++, Python contract tests.

**Spec:** `D:\robot\eyes\docs\superpowers\specs\2026-08-15-esp32-pose-dual-mode-design.md`

## Global Constraints

- Local mode must work without Wi-Fi or a PC.
- BOOT-button mode switching must remain available: local mode stops PC/Wi-Fi services; Wi-Fi/PC mode restarts them.
- The visual stage must accept only one pose after each `0x11` arm command.
- A stable local pose must be announced through SU-03T, dispatched to Arduino, and completed only after Arduino ACK.
- Team 1 action IDs must match the PC contract: `1`, `2`, `3`, `4`, `11`, `12`.
- Existing unrelated dirty music changes must remain untouched.

### Task 1: Add failing contract tests

**Files:**
- Create: `tests/test_local_pose_runtime.py`
- Modify: `tests/test_pose_classifier.c`

- [x] Add tests that require the local task to call the competition gate, speech bridge, and visual action dispatcher.
- [x] Add tests that require the ESP32 contract to expose `POSE_HANDS_ON_HIPS=11`, `POSE_SALUTE=12`, and the Team 1 allow-list.
- [x] Run the focused tests and observe failure before production edits.

### Task 2: Align the ESP32 Team 1 pose contract and classifier

**Files:**
- Modify: `components/BSP/POSE/pose_contract.h`
- Modify: `components/BSP/POSE/pose_contract.c`
- Modify: `components/BSP/POSE/pose_classifier.c`

- [x] Replace the old Team 1 custom IDs with `hands_on_hips=11` and `salute=12` while preserving the four required actions.
- [x] Port the PC custom-pose precedence and geometry conditions into the ESP32 classifier.
- [x] Keep unknown, missing-keypoint, and stable-frame behavior unchanged.
- [x] Run the focused contract tests.

### Task 3: Connect local stable results to the competition loop

**Files:**
- Modify: `main/main.c`

- [x] Add one pose-ID-to-visual-message-ID mapper shared by local dispatch and existing Arduino mapping.
- [x] In `local_pose_task`, accept only stable results and let `competition_on_visual_recognized()` enforce visual-stage and `0x11` arm gating.
- [x] On acceptance, send the visual speech message before dispatching the Arduino action.
- [x] On dispatch failure, call `competition_on_action_done(false)` so the state machine does not remain stuck.
- [x] Keep existing ACK handling as the only success completion path.
- [x] Preserve BOOT-button switching and the existing stop/start order.

### Task 4: Verify and document the handoff

**Files:**
- Modify: `README.md`

- [x] Run all Python contract tests.
- [x] Run the ESP-IDF build with the configured toolchain or report the exact environment blocker.
- [x] Search the final diff for unintended changes and confirm the pre-existing music modifications remain intact.
- [x] Document the local-mode test sequence: `0xBB`, `0x11`, gesture, speech, Arduino ACK, then BOOT switch back to PC/Wi-Fi mode.

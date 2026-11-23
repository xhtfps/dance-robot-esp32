# Replace Ambiguous Pose Actions Design

## Goal

Produce three independent K230 code packages for three teams.  Each package
recognizes the same four basic actions plus only two extension actions, for a
total of six actions per team.  All packages retain the existing UART2
`START\n` / `RESULT {json}\n` protocol.

The two unreliable 2D-skeleton actions are replaced with gestures whose
COCO-17 keypoints remain visually distinct on the K230 camera:

| Replaced action | New action | English label | Pose ID |
| --- | --- | --- | --- |
| 双手叉腰 | 单膝抬起 | `knee_raise` | 6 |
| 双手抱头 | 单臂侧平举 | `one_arm_side` | 7 |

## Team Packages

| Package | Basic actions in every package | Extension actions |
| --- | --- | --- |
| `team_1` | `big_stand`, `bow_step`, `double_hands`, `squat` | `attention`, `one_hand_up` |
| `team_2` | `big_stand`, `bow_step`, `double_hands`, `squat` | `knee_raise`, `bend_forward` |
| `team_3` | `big_stand`, `bow_step`, `double_hands`, `squat` | `one_arm_side`, `crossed_arms` |

Each package contains its own `main.py`, `pose_recognition.py`, and
`host_uart.py`.  The six labels excluded from a package must never be returned
by that package.

## Recognition Rules

### `knee_raise`

The performer faces the camera or stands at a slight angle, raises exactly one
knee, and keeps the supporting leg substantially straighter.

- One ankle is at least `0.55 * scale` above the other ankle.
- The raised-leg knee angle is below 145 degrees.
- The supporting-leg knee angle is above 145 degrees.
- The action is checked before squat and bow step, so a lifted foot cannot be
  mistaken for a low crouch or a lunge.

### `one_arm_side`

The performer raises one straight arm sideways at shoulder height and leaves
the other arm down next to the body.

- Exactly one wrist lies within `0.25 * scale` of its shoulder's vertical
  position and extends at least `0.55 * scale` laterally from the torso centre.
- That arm's elbow angle is above 150 degrees.
- The other wrist is below the shoulder by at least `0.35 * scale`.
- This is distinct from `big_stand` (both arms horizontal) and `one_hand_up`
  (the raised wrist is above the head).

## Interface and Display

- `POSE_HANDS_ON_HIPS` and `POSE_HANDS_ON_HEAD` are replaced by
  `POSE_KNEE_RAISE` and `POSE_ONE_ARM_SIDE` respectively in the shared source
  before team-specific filtering is applied.
- UART `RESULT` output continues to use the same field names; only `pose`,
  `pose_cn`, and `stable_pose` values change.
- The LCD continues to show current-frame `POSE` and, when different, the
  prior debounced `STABLE` action.

## Verification

Add synthetic COCO-17 regression cases for both new actions, including both
left/right mirror directions. Add a test for each package proving it only
returns its assigned six labels. Retain all existing four basic-action, ROI,
UART, and LCD tests. The suite must pass before the files are uploaded to the
K230.

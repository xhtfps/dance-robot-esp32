"""Stable action IDs and result schema shared by pose clients."""

POSE_UNKNOWN = 0
POSE_BIG_STAND = 1
POSE_BOW_STEP = 2
POSE_DOUBLE_HANDS = 3
POSE_SQUAT = 4
POSE_HANDS_ON_HIPS = 11
POSE_SALUTE = 12

POSE_LABELS = {
    POSE_UNKNOWN: "unknown",
    POSE_BIG_STAND: "big_stand",
    POSE_BOW_STEP: "bow_step",
    POSE_DOUBLE_HANDS: "double_hands",
    POSE_SQUAT: "squat",
    POSE_HANDS_ON_HIPS: "hands_on_hips",
    POSE_SALUTE: "salute",
}

POSE_LABELS_CN = {
    POSE_UNKNOWN: "未识别",
    POSE_BIG_STAND: "大字站",
    POSE_BOW_STEP: "弓箭步",
    POSE_DOUBLE_HANDS: "举双手",
    POSE_SQUAT: "蹲下",
    POSE_HANDS_ON_HIPS: "叉腰",
    POSE_SALUTE: "敬礼",
}

TEAM_ALLOWED_POSE_IDS = {
    1: (POSE_BIG_STAND, POSE_HANDS_ON_HIPS, POSE_SALUTE,
        POSE_DOUBLE_HANDS, POSE_BOW_STEP, POSE_SQUAT),
}


def team_pose_ids(team_id):
    """Return the immutable six-action allow-list for a team."""
    try:
        return TEAM_ALLOWED_POSE_IDS[int(team_id)]
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError("only team_id 1 is supported") from exc


def _canonical_pose_id(pose_id):
    try:
        pose_id = int(pose_id)
    except (TypeError, ValueError):
        return POSE_UNKNOWN
    return pose_id if pose_id in POSE_LABELS else POSE_UNKNOWN


def make_result(team_id, pose_id, confidence, stable, source, fps=0.0):
    """Build the JSON-compatible result shape used by all PC clients."""
    team_id = int(team_id)
    team_pose_ids(team_id)
    pose_id = _canonical_pose_id(pose_id)
    stable = bool(stable)
    stable_pose_id = pose_id if stable else POSE_UNKNOWN
    return {
        "task": "pose",
        "team": team_id,
        "pose_id": pose_id,
        "pose": POSE_LABELS[pose_id],
        "pose_cn": POSE_LABELS_CN[pose_id],
        "confidence": round(max(0.0, min(1.0, float(confidence))), 3),
        "stable": stable,
        "stable_pose_id": stable_pose_id,
        "stable_pose": POSE_LABELS[stable_pose_id],
        "stable_pose_cn": POSE_LABELS_CN[stable_pose_id],
        "fps": round(max(0.0, float(fps)), 3),
        "source": str(source),
    }

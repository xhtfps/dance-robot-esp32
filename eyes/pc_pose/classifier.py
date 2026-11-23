"""Team-specific wrapper around the existing geometry classifier."""

from .pose_geometry import PoseClassifier

from shared.pose_contract import team_pose_ids


class TeamPoseClassifier:
    """Apply one of the three existing action allow-lists."""

    def __init__(self, team_id, stable_frames=4):
        self.team_id = int(team_id)
        self._classifier = PoseClassifier(
            stable_frames=stable_frames,
            allowed_pose_ids=team_pose_ids(self.team_id),
        )

    def reset(self):
        self._classifier.reset()

    def update(self, keypoints):
        result = dict(self._classifier.update(keypoints))
        result["team"] = self.team_id
        return result

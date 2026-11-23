class CompetitionModel:
    def __init__(self):
        self.state = "IDLE"
        self.current_stage = None
        self.seen = set()
        self.current_item = 0
        self.visual_armed = False
        self.last_music_id = None

    def prepare(self):
        if self.state != "IDLE":
            return False
        self.state = "READY_FOR_STAGE"
        return True

    def stage(self, stage_id):
        if self.state not in (
            "READY_FOR_STAGE", "RUNNING", "MUSIC_WAIT", "MUSIC_RECOGNITION",
            "MUSIC_ANNOUNCE_DELAY", "MUSIC_PERFORMANCE",
        ):
            return False
        if stage_id not in (0, 1, 2) or stage_id in self.seen:
            return False
        self.current_stage = stage_id
        self.seen.add(stage_id)
        self.current_item = 0
        self.visual_armed = False
        self.state = "MUSIC_WAIT" if stage_id == 2 else "RUNNING"
        return True

    def arm_visual(self):
        if self.state != "RUNNING" or self.current_stage != 1:
            return False
        self.visual_armed = True
        return True

    def voice(self, action_id):
        return (
            self.state == "RUNNING"
            and self.current_stage == 0
            and action_id >= 0
        )

    def music_start_recognition(self):
        if self.state != "MUSIC_WAIT" or self.current_stage != 2:
            return False
        self.last_music_id = None
        self.state = "MUSIC_RECOGNITION"
        return True

    def music_recognized(self, music_id):
        if self.state != "MUSIC_RECOGNITION" or music_id not in range(1, 7):
            return False
        self.last_music_id = music_id
        self.state = "MUSIC_ANNOUNCE_DELAY"
        return True

    def music_announcement_failed(self):
        if self.state != "MUSIC_ANNOUNCE_DELAY":
            return False
        self.last_music_id = None
        self.state = "MUSIC_WAIT"
        return True

    def music_start_performance(self, music_id):
        if self.state != "MUSIC_ANNOUNCE_DELAY" or music_id != self.last_music_id:
            return False
        self.state = "MUSIC_PERFORMANCE"
        return True

    def music_performance_done(self):
        if self.state != "MUSIC_PERFORMANCE":
            return False
        self.current_item += 1
        self.state = "COMPLETE" if self.last_music_id == 6 else "MUSIC_WAIT"
        return True

    def action_done(self, success, now_ms=0):
        if self.state not in ("RUNNING", "DISPATCH_ACTION"):
            return False
        if success:
            self.current_item += 1
        elif self.current_stage == 1:
            self.visual_armed = False
        self.state = "RUNNING"
        return True

    def complete_stage(self):
        if self.state not in (
            "RUNNING", "MUSIC_WAIT", "MUSIC_RECOGNITION",
            "MUSIC_ANNOUNCE_DELAY", "MUSIC_PERFORMANCE",
        ):
            return False
        self.current_stage = None
        self.state = "COMPLETE" if len(self.seen) == 3 else "READY_FOR_STAGE"
        self.visual_armed = False
        return True

    def visual(self, pose_id, now_ms=0):
        accepted = (
            self.state == "RUNNING"
            and self.current_stage == 1
            and pose_id >= 0
            and self.visual_armed
        )
        if accepted:
            self.visual_armed = False
            self.state = "DISPATCH_ACTION"
        return accepted

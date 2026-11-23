class MusicFlowModel:
    def __init__(self):
        self.music_id = None
        self.due_ms = None

    def recognized(self, music_id, now_ms):
        if music_id not in range(1, 7) or self.due_ms is not None:
            return None
        self.music_id = music_id
        self.due_ms = now_ms + 5_000
        return 6 + music_id

    def dance_due(self, now_ms):
        if self.due_ms is None or now_ms < self.due_ms:
            return None
        command = 0x44 if self.music_id <= 5 else 0x45
        self.music_id = None
        self.due_ms = None
        return command

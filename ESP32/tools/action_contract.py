ACTION_ACK_ERROR = 0xFE


class ActionBridgeModel:
    def __init__(self, timeout_ms):
        self.timeout_ms = timeout_ms
        self.pending_command = None
        self.started_at_ms = None
        self.complete = False
        self.success = False

    def start(self, command, now_ms):
        if self.pending_command is not None or self.complete:
            return False
        self.pending_command = command
        self.started_at_ms = now_ms
        return True

    def accept_ack(self, value, now_ms):
        if self.pending_command is None:
            return False
        if value not in (self.pending_command, ACTION_ACK_ERROR):
            return False
        self.success = value == self.pending_command
        self.complete = True
        self.pending_command = None
        self.started_at_ms = None
        return True

    def check_timeout(self, now_ms):
        if self.pending_command is None:
            return False
        if now_ms - self.started_at_ms < self.timeout_ms:
            return False
        self.success = False
        self.complete = True
        self.pending_command = None
        self.started_at_ms = None
        return True

"""Browser MJPEG server for annotated PC pose frames."""

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class OverlayState:
    def __init__(self):
        self._condition = threading.Condition()
        self._jpeg = None
        self._result = {}
        self._sequence = 0

    def update(self, jpeg, result):
        with self._condition:
            self._jpeg = bytes(jpeg)
            self._result = dict(result)
            self._sequence += 1
            self._condition.notify_all()

    def wait_frame(self, last_sequence=0, timeout=5.0):
        with self._condition:
            if self._sequence <= last_sequence:
                self._condition.wait(timeout)
            if self._jpeg is None or self._sequence <= last_sequence:
                return None, last_sequence
            return self._jpeg, self._sequence

    def status(self):
        with self._condition:
            return dict(self._result)


class _OverlayHandler(BaseHTTPRequestHandler):
    server_version = "RobotPose/1.0"

    def log_message(self, format_string, *args):
        return

    @property
    def state(self):
        return self.server.overlay_state

    def do_GET(self):
        if self.path == "/":
            self._send_index()
        elif self.path == "/stream":
            self._send_stream()
        elif self.path == "/status":
            self._send_status()
        else:
            self.send_error(404)

    def _send_index(self):
        page = """<!doctype html>
<html><head><meta charset="utf-8"><title>Robot Pose</title>
<style>
body{margin:0;padding:12px;background:#fff;color:#111;font:16px sans-serif;text-align:center}
img{display:block;width:auto;max-width:96vw;max-height:86vh;margin:0 auto;border:2px solid #3ad}
#status{height:28px;padding:8px 4px;overflow:hidden;white-space:nowrap;text-overflow:ellipsis}
</style></head>
<body><img src="/stream" alt="annotated pose stream">
<div id="status">等待电脑端姿态识别...</div>
<script>
function shortStatus(data){
  const pose=data.pose_cn || data.pose || "未识别";
  const stable=data.stable_pose_cn || data.stable_pose || "-";
  const confidence=Number(data.confidence || 0).toFixed(2);
  const fps=Number(data.fps || 0).toFixed(1);
  const person=Number(data.person_score || 0).toFixed(2);
  return `Team ${data.team ?? "-"} | FPS ${fps} | 姿态 ${pose} | 稳定 ${stable} | 置信度 ${confidence} | 人体 ${person}`;
}
setInterval(async()=>{try{let r=await fetch('/status',{cache:'no-store'});
document.getElementById('status').textContent=shortStatus(await r.json())}catch(e){}},500)
</script>
</body></html>"""
        payload = page.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _send_status(self):
        payload = json.dumps(self.state.status(), ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _send_stream(self):
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()

        sequence = 0
        try:
            while True:
                jpeg, sequence = self.state.wait_frame(sequence)
                if jpeg is None:
                    continue
                header = (
                    b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                    + str(len(jpeg)).encode("ascii")
                    + b"\r\n\r\n"
                )
                self.wfile.write(header)
                self.wfile.write(jpeg)
                self.wfile.write(b"\r\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError):
            return


class PoseOverlayServer:
    def __init__(self, host="127.0.0.1", port=8080):
        self.state = OverlayState()
        self.server = ThreadingHTTPServer((host, int(port)), _OverlayHandler)
        self.server.overlay_state = self.state
        self.server.daemon_threads = True
        self._thread = None

    def start(self):
        self._thread = threading.Thread(
            target=self.server.serve_forever,
            name="pose-web-server",
            daemon=True,
        )
        self._thread.start()

    def stop(self):
        self.server.shutdown()
        self.server.server_close()
        if self._thread is not None:
            self._thread.join(timeout=2.0)

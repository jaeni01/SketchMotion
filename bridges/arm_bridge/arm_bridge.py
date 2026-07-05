#!/usr/bin/env python3
# arm_bridge.py - myCobot 280 Pi 드로잉 브리지 (Bridge Protocol v1, 포트 9101)
#
# 실행 (myCobot 내장 RPi에서):
#   python3 arm_bridge.py                 # 실물 구동
#   python3 arm_bridge.py --dry-run       # 로봇 없이 프로토콜만 (개발용)
#
# 설정: calib.json (같은 디렉터리)
#   { "theta": 0.0, "tx": 0.0, "ty": 0.0,        # T_pr: {paper}mm -> {robot}mm 강체 변환
#     "pen_down_z": 65.0, "pen_up_z": 90.0,      # {robot} Z (mm) - 스프링이 흡수하므로 down은 살짝 과다 지령
#     "limits": { "xmin": 120, "xmax": 260, "ymin": -80, "ymax": 80, "zmin": 55, "zmax": 180 } }
#
# 안전: 소프트리밋 위반 시 경로 전체 거부(부분 실행 금지). estop은 수신 즉시 처리.

import argparse
import json
import math
import socket
import threading
import time
from pathlib import Path

PORT = 9101
PROTO = 1
INTERP_MM = 5.0

class Cfg:
    def __init__(self, path):
        d = json.loads(Path(path).read_text(encoding="utf-8")) if Path(path).exists() else {}
        self.theta = d.get("theta", 0.0)
        self.tx = d.get("tx", 0.0)
        self.ty = d.get("ty", 0.0)
        self.pen_down_z = d.get("pen_down_z", 65.0)
        self.pen_up_z = d.get("pen_up_z", 90.0)
        lim = d.get("limits", {})
        self.limits = (lim.get("xmin", 120), lim.get("xmax", 260),
                       lim.get("ymin", -80), lim.get("ymax", 80),
                       lim.get("zmin", 55), lim.get("zmax", 180))

    def paper_to_robot(self, x, y):
        c, s = math.cos(self.theta), math.sin(self.theta)
        return (c * x - s * y + self.tx, s * x + c * y + self.ty)

    def in_limits(self, x, y, z):
        x0, x1, y0, y1, z0, z1 = self.limits
        return x0 <= x <= x1 and y0 <= y <= y1 and z0 <= z <= z1


class Robot:
    """pymycobot 래퍼. dry_run이면 시간만 흉내낸다."""

    def __init__(self, dry_run):
        self.dry = dry_run
        self.mc = None
        if not dry_run:
            from pymycobot.mycobot import MyCobot  # RPi 전용
            self.mc = MyCobot("/dev/ttyAMA0", 1000000)
            time.sleep(0.5)

    def version(self):
        if self.dry:
            return "dry-run"
        try:
            return str(self.mc.get_system_version())
        except Exception:
            return "unknown"

    def home(self):
        if self.dry:
            time.sleep(0.5); return
        self.mc.send_angles([0, 0, 0, 0, 0, 0], 40)

    def move_to(self, x, y, z, speed_mm_s):
        """직교 좌표 이동 (블로킹 근사). myCobot 자세: 펜 수직(-Z) 고정."""
        if self.dry:
            time.sleep(0.02); return
        # send_coords 속도 인자는 1-100 스케일 - 보수적 매핑
        spd = max(10, min(80, int(speed_mm_s)))
        self.mc.send_coords([x, y, z, -180, 0, -90], spd, 1)
        time.sleep(max(0.05, INTERP_MM / max(1.0, speed_mm_s)))

    def release(self):
        if not self.dry:
            self.mc.release_all_servos()

    def joints(self):
        if self.dry:
            return [0, 0, 0, 0, 0, 0]
        try:
            return self.mc.get_angles() or [0] * 6
        except Exception:
            return [0] * 6


class Bridge:
    def __init__(self, cfg, robot):
        self.cfg = cfg
        self.robot = robot
        self.sock = None
        self.send_lock = threading.Lock()
        self.abort = threading.Event()
        self.drawing = threading.Event()

    # ---- 송신 ----
    def _send(self, obj):
        with self.send_lock:
            if self.sock:
                try:
                    self.sock.sendall((json.dumps(obj) + "\n").encode("utf-8"))
                except OSError:
                    pass

    def ok(self, mid, result=None):
        self._send({"id": mid, "ok": True, "result": result or {}})

    def err(self, mid, msg):
        self._send({"id": mid, "ok": False, "error": msg})

    def event(self, name, data):
        self._send({"event": name, "data": data})

    # ---- 드로잉 실행 ----
    def _interpolate(self, paths):
        """{paper}mm 폴리라인 -> {robot} 세그먼트 포인트열 + 소프트리밋 검사"""
        runs = []
        for path in paths:
            pts = []
            for i in range(len(path)):
                x, y = path[i]["x"], path[i]["y"]
                if i > 0:
                    px, py = path[i - 1]["x"], path[i - 1]["y"]
                    dist = math.hypot(x - px, y - py)
                    n = max(1, int(dist / INTERP_MM))
                    for k in range(1, n + 1):
                        pts.append((px + (x - px) * k / n, py + (y - py) * k / n))
                else:
                    pts.append((x, y))
            run = []
            for (x, y) in pts:
                rx, ry = self.cfg.paper_to_robot(x, y)
                if not self.cfg.in_limits(rx, ry, self.cfg.pen_down_z):
                    raise ValueError(f"soft limit exceeded at paper({x:.1f},{y:.1f})")
                run.append((rx, ry))
            runs.append(run)
        return runs

    def _draw_worker(self, runs, feed, travel):
        try:
            up, down = self.cfg.pen_up_z, self.cfg.pen_down_z
            total = len(runs)
            for i, run in enumerate(runs):
                if self.abort.is_set():
                    break
                self.robot.move_to(run[0][0], run[0][1], up, travel)
                self.robot.move_to(run[0][0], run[0][1], down, 20)
                for (rx, ry) in run[1:]:
                    if self.abort.is_set():
                        break
                    self.robot.move_to(rx, ry, down, feed)
                self.robot.move_to(run[-1][0], run[-1][1], up, 20)
                self.event("progress", {"phase": "draw", "seg": i + 1, "of": total,
                                        "pct": (i + 1) * 100.0 / total})
            if not self.abort.is_set():
                self.event("done", {"phase": "draw"})
        except Exception as e:  # 하드웨어 예외 -> fault + 정지
            self.event("fault", {"reason": "hw", "detail": str(e)})
        finally:
            self.drawing.clear()

    # ---- 명령 처리 ----
    def handle(self, msg):
        mid = msg.get("id", -1)
        cmd = msg.get("cmd", "")
        p = msg.get("params", {}) or {}
        if cmd == "estop":
            self.abort.set()
            self.robot.release()
            self.ok(mid)
        elif cmd == "stop":
            self.abort.set()
            self.ok(mid)
        elif cmd == "hello":
            self.ok(mid, {"device": "mycobot280", "proto": PROTO,
                          "version": self.robot.version()})
        elif cmd == "home":
            self.robot.home()
            self.ok(mid)
        elif cmd == "telemetry":
            self.ok(mid, {"joints": self.robot.joints(),
                          "moving": self.drawing.is_set(), "queued": 0})
        elif cmd == "draw_paths":
            if self.drawing.is_set():
                self.err(mid, "busy")
                return
            try:
                runs = self._interpolate(p.get("paths", []))
            except ValueError as e:
                self.err(mid, str(e))
                return
            if not runs:
                self.err(mid, "paths missing")
                return
            self.abort.clear()
            self.drawing.set()
            self.ok(mid, {"accepted": True})
            threading.Thread(target=self._draw_worker,
                             args=(runs, p.get("feed", 20), p.get("travel", 60)),
                             daemon=True).start()
        else:
            self.err(mid, f"unknown cmd: {cmd}")

    # ---- 서버 루프 ----
    def serve(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", PORT))
        srv.listen(1)
        print(f"[arm_bridge] listening on :{PORT}")
        while True:
            conn, addr = srv.accept()
            print(f"[arm_bridge] client {addr}")
            self.sock = conn
            buf = b""
            try:
                while True:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        if not line.strip():
                            continue
                        try:
                            self.handle(json.loads(line))
                        except json.JSONDecodeError:
                            pass
            finally:
                # 연결 워치독: 끊기면 즉시 정지
                self.abort.set()
                self.sock = None
                conn.close()
                print("[arm_bridge] client disconnected -> motion aborted")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true", help="로봇 없이 프로토콜만")
    ap.add_argument("--calib", default=str(Path(__file__).parent / "calib.json"))
    args = ap.parse_args()
    Bridge(Cfg(args.calib), Robot(args.dry_run)).serve()

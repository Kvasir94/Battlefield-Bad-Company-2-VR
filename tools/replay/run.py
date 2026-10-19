#!/usr/bin/env python3
"""Run one replay scenario offline (sim) or against the live game (live), N times.

    python tools/replay/run.py --scenario xm8-mag-reload --mode sim [--repeat 3]
    python tools/replay/run.py --scenario xm8-mag-reload --mode live --repeat 1   (Windows, BC2 running)

Every attempt gets a fresh directory under --out-root and ends with
replay_assert.py (scenario rules + the tape's `#! expect` line) and, when it is
present, tools/analyze_gun_events.py. summary.json holds the pass rate. A sim
pass is labelled "sim" and never stands in for a live pass; no run is headset
acceptance. See docs/REPLAY_INPUT.md.

Exit status: 0 every attempt passed, 1 any attempt failed, 2 setup refused.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import replay_assert  # noqa: E402

SCHEMA = "fvr.bc2.replay_run.v1"
OTHER_TESTS = ("BC2XrHost.exe", "BC2NativeTrace.exe", "BC2NativeIpcProbe.exe", "BC2ExceptionWatch.exe")
MUTEX = "Local\\FVR_BC2_DevelopmentFixture"


class Refused(RuntimeError):
    pass


def sha256(path: Path | None) -> str | None:
    if path is None or not path.is_file():
        return None
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def stamp() -> str:
    return _dt.datetime.now(_dt.timezone.utc).strftime("%Y%m%d-%H%M%S-%f")[:-3]


def load_scenario(name: str) -> tuple[Path, dict]:
    path = Path(name)
    if not path.suffix:
        path = HERE / "scenarios" / f"{name}.json"
    if not path.is_file():
        raise Refused(f"scenario not found: {path}")
    return path, json.loads(path.read_text(encoding="utf-8"))


def find_sim(explicit: Path | None) -> Path:
    if explicit:
        if not explicit.is_file():
            raise Refused(f"simulator not found: {explicit}")
        return explicit
    names = ("fvr_replay_sim.exe", "fvr_replay_sim")
    for folder in (ROOT / "build" / "x64", ROOT / "build" / "x86", ROOT / "build"):
        for name in names:
            if (folder / name).is_file():
                return folder / name
    raise Refused("fvr_replay_sim not built; pass --sim-exe")


def judge(attempt: Path, events: list[Path], scenario_path: Path, scenario: dict, tape: Path, label: str,
          analyzer: Path | None) -> dict:
    result = {"assert": None, "analyzer": None}
    rows, invalid = replay_assert.load_events([e for e in events if e.is_file()])
    extra = replay_assert.expect_rules(replay_assert.tape_expect(tape))
    report = replay_assert.evaluate(rows, invalid, scenario, extra, label)
    (attempt / "assert.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    result["assert"] = {"passed": report["passed"], "failed": [r["id"] for r in report["rules"] if not r["passed"]]}
    if analyzer and analyzer.is_file() and events:
        rules = attempt / "analyzer-rules.json"
        rules.write_text(json.dumps(scenario.get("analyzer_rules", {})), encoding="utf-8")
        run = subprocess.run([sys.executable, "-I", str(analyzer), "--json", "--rules", str(rules), *map(str, events)],
                             capture_output=True, text=True, timeout=120)
        (attempt / "analysis.json").write_text(run.stdout, encoding="utf-8")
        result["analyzer"] = {"exit": run.returncode, "passed": run.returncode == 0}
    result["passed"] = report["passed"] and (result["analyzer"] is None or result["analyzer"]["passed"])
    return result


# ----------------------------------------------------------------------------- sim

def run_sim(attempt: Path, scenario: dict, tape: Path, sim: Path) -> tuple[list[Path], dict]:
    options = scenario.get("sim", {})
    if not options.get("supported", False):
        raise Refused(f"scenario has no offline model: {options.get('reason', 'sim.supported is false')}")
    events, summary = attempt / "gun-events.sim.jsonl", attempt / "sim-summary.json"
    cmd = [str(sim), "run", "--tape", str(tape), "--events", str(events), "--summary", str(summary)]
    for key in ("loaded", "reserve", "capacity", "tail"):
        if key in options:
            cmd += [f"--{key}", str(options[key])]
    run = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    (attempt / "sim-stderr.txt").write_text(run.stderr, encoding="utf-8")
    info = {"exit": run.returncode, "events_sha256": sha256(events)}
    if summary.is_file():
        info["summary"] = json.loads(summary.read_text(encoding="utf-8"))
    if run.returncode == 3:
        info["verdict"] = "precondition_unmet"
    return [events], info


# ----------------------------------------------------------------------------- live (Windows)

def _processes() -> list[str]:
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True, check=True).stdout
    return [line.split(",")[0].strip('"') for line in out.splitlines() if line.strip()]


def _mutex():
    import ctypes  # Windows only
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = kernel.CreateMutexW(None, True, MUTEX)
    if not handle or ctypes.get_last_error() == 183:  # ERROR_ALREADY_EXISTS
        raise Refused("another development fixture holds " + MUTEX)
    return kernel, handle


def _foreground_game() -> bool:
    import ctypes
    user = ctypes.WinDLL("user32", use_last_error=True)
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def visit(hwnd, _):
        pid = ctypes.c_ulong()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if user.IsWindowVisible(hwnd) and pid.value in _game_pids():
            found.append(hwnd)
        return True

    user.EnumWindows(visit, None)
    if not found:
        return False
    user.SetForegroundWindow(found[0])
    time.sleep(0.5)
    return user.GetForegroundWindow() == found[0]


def _game_pids() -> set[int]:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq BFBC2Game.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True, check=True).stdout
    return {int(line.split(",")[1].strip('"')) for line in out.splitlines() if line.startswith('"BFBC2Game.exe"')}


def _trace_folders() -> list[Path]:
    reports = ROOT / "reports"
    return sorted(reports.glob("native-trace-*")) if reports.is_dir() else []


def _new_trace_folder(before: set[Path]) -> Path | None:
    """The single native-trace folder this attempt created, else None."""
    new = [p for p in _trace_folders() if p not in before and p.is_dir()]
    return new[0] if len(new) == 1 else None


def run_live(attempt: Path, scenario: dict, tape: Path, timeout: int) -> tuple[list[Path], dict]:
    if os.name != "nt":
        raise Refused("live mode needs Windows, BC2 in campaign gameplay and the built x86/x64 binaries")
    live = scenario.get("live", {})
    switches = live.get("trace_switches", ["-MagazineReload"])
    seconds = int(live.get("seconds", 60))
    procs = _processes()
    busy = [p for p in OTHER_TESTS if p in procs]
    if busy:
        raise Refused(f"another BC2 test is active: {busy}")
    if procs.count("BFBC2Game.exe") != 1:
        raise Refused("exactly one BC2 must already be running in campaign gameplay at the scenario checkpoint")
    info = {"foreground": _foreground_game(), "trace_switches": switches, "seconds": seconds}
    if not info["foreground"]:
        raise Refused("BC2 window could not be brought to the foreground (doc 197 -01 failure mode)")
    receiver_dir = attempt / "receiver"
    sink = ROOT / "build" / "x64" / "BC2NativeIpcProbe.exe"
    receiver = subprocess.Popen([str(sink), "--output", str(receiver_dir), "--pairs", "240", "--static-pose", "--async",
                                 "--sink-seconds", str(seconds)], stdout=subprocess.DEVNULL,
                                stderr=(attempt / "receiver-stderr.txt").open("w"))
    try:
        token_file, waited = receiver_dir / "channel.txt", 0.0
        while not token_file.is_file() and waited < 15:
            time.sleep(0.25)
            waited += 0.25
        if not token_file.is_file():
            raise Refused("tape sink did not publish a frame channel")
        token = token_file.read_text(encoding="utf-16" if token_file.read_bytes()[:2] == b"\xff\xfe" else "utf-8").strip()
        before = set(_trace_folders())
        cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "Start-NativeTrace.ps1"),
               "-FrameChannel", token, "-Seconds", str(seconds), "-InputTape", str(tape), "-GunEvents", *switches]
        trace = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        (attempt / "trace-stdout.txt").write_text(trace.stdout, encoding="utf-8")
        (attempt / "trace-stderr.txt").write_text(trace.stderr, encoding="utf-8")
        info["trace_exit"] = trace.returncode
        folder = Path(trace.stdout.strip().splitlines()[0]) if trace.stdout.strip() else None
        if folder is None or not folder.is_dir():
            # A nonzero trace exit (e.g. a probe budget timeout) prints no folder,
            # but the DLL may already have written a complete gun-event log.
            folder = _new_trace_folder(before)
            info["trace_folder_recovered"] = folder is not None
        info["trace_folder"] = str(folder) if folder else None
    finally:
        try:
            receiver.wait(timeout=seconds + 30)
        except subprocess.TimeoutExpired:
            receiver.terminate()
        info["receiver_exit"] = receiver.poll()
    events = sorted(folder.glob("gun-events*.jsonl")) if folder and folder.is_dir() else []
    if folder and (folder / "native-trace.json").is_file():
        trace_json = json.loads((folder / "native-trace.json").read_text(encoding="utf-8"))
        info["input_tape"] = trace_json.get("input_tape")
    if not events:
        info["verdict"] = "no_gun_events"  # the DLL emitter (GUN-EVENT-LOG.md producers) must be present
    for event in events:
        shutil.copy2(event, attempt / event.name)
    return [attempt / e.name for e in events], info


# ----------------------------------------------------------------------------- driver

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--mode", choices=("sim", "live"), required=True)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--tape", type=Path, help="override the scenario's tape")
    parser.add_argument("--out-root", type=Path, default=ROOT / "reports" / "replay")
    parser.add_argument("--sim-exe", type=Path)
    parser.add_argument("--analyzer", type=Path, default=ROOT / "tools" / "analyze_gun_events.py")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args(argv)
    try:
        if not 1 <= args.repeat <= 20:
            raise Refused("repeat must be 1..20")
        if not 30 <= args.timeout <= 600:
            raise Refused("timeout must be 30..600 seconds")
        scenario_path, scenario = load_scenario(args.scenario)
        tape = (args.tape or (scenario_path.parent / scenario.get("tape", ""))).resolve()
        if not tape.is_file():
            raise Refused(f"tape not found: {tape} (record one in the headset, then cut it)")
        replay_assert.tape_expect(tape)  # header must parse before anything starts
        sim = find_sim(args.sim_exe) if args.mode == "sim" else None
        run_dir = args.out_root / f"{scenario.get('name', scenario_path.stem)}-{args.mode}-{stamp()}"
        run_dir.mkdir(parents=True, exist_ok=False)
        lock = _mutex() if args.mode == "live" else None
    except (Refused, OSError, ValueError, replay_assert.RuleError) as error:
        print(f"run: refused: {error}", file=sys.stderr)
        return 2
    run_info = {"schema": SCHEMA, "scenario": scenario.get("name"), "mode": args.mode, "repeat": args.repeat,
                "scenario_sha256": sha256(scenario_path), "tape": str(tape), "tape_sha256": sha256(tape),
                "binaries_sha256": {"fvr_replay_sim": sha256(sim),
                                    "BC2NativeProbe.dll": sha256(ROOT / "build" / "x86" / "BC2NativeProbe.dll"),
                                    "BC2NativeTrace.exe": sha256(ROOT / "build" / "x86" / "BC2NativeTrace.exe"),
                                    "BC2NativeIpcProbe.exe": sha256(ROOT / "build" / "x64" / "BC2NativeIpcProbe.exe")},
                "analyzer_sha256": sha256(args.analyzer), "started_utc": _dt.datetime.now(_dt.timezone.utc).isoformat()}
    (run_dir / "run.json").write_text(json.dumps(run_info, indent=2) + "\n", encoding="utf-8")
    runs = []
    try:
        for index in range(args.repeat):
            attempt = run_dir / f"attempt-{index + 1:02d}"
            attempt.mkdir()
            try:
                if args.mode == "sim":
                    events, info = run_sim(attempt, scenario, tape, sim)
                else:
                    events, info = run_live(attempt, scenario, tape, args.timeout)
                    if index + 1 < args.repeat:
                        info["note"] = "the DLL stays mapped: restart BC2 at the checkpoint before the next attempt"
                verdict = judge(attempt, events, scenario_path, scenario, tape, args.mode, args.analyzer)
                runs.append({"attempt": index + 1, **info, **verdict,
                             "passed": verdict["passed"] and info.get("verdict") is None})
            except Refused as error:
                runs.append({"attempt": index + 1, "passed": False, "refused": str(error)})
                break
            if args.mode == "live" and index + 1 < args.repeat:
                print("restart BC2 at the scenario checkpoint, then press Enter", file=sys.stderr)
                input()
    finally:
        if lock:
            lock[0].ReleaseMutex(lock[1])
            lock[0].CloseHandle(lock[1])
    passed = sum(1 for r in runs if r["passed"])
    hashes = {r.get("events_sha256") for r in runs if r.get("events_sha256")}
    summary = {**run_info, "runs": runs, "passed": passed, "attempts": len(runs),
               "pass_rate": passed / max(1, len(runs)), "all_passed": passed == args.repeat,
               "sim_deterministic": (len(hashes) == 1) if args.mode == "sim" and runs else None,
               "label": args.mode, "headset_tested": False, "headset_accepted": False}
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("scenario", "mode", "attempts", "passed", "pass_rate", "all_passed")}))
    print(str(run_dir))
    return 0 if summary["all_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

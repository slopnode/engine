#!/usr/bin/env python3
import argparse
import os
import re
import resource
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ANSI = re.compile(r"\x1b\[[0-9;]*m")
CLIENT_STATS = re.compile(
    r"NET STATS client (\S+): ([\d.]+) snapshots/s, ([\d.]+) KB/s, ([\d.]+) entities per snapshot, (\d+) at most, "
    r"(\d+) missed, (\d+) of (\d+) bound, (\d+) of (\d+) frames starved, (\d+) corrections, worst ([\d.]+) m")
SERVER_STATS = re.compile(
    r"NET STATS server to (\S+): ([\d.]+) snapshots/s, ([\d.]+) KB/s, ([\d.]+) entities and ([\d.]+) deferred per snapshot, "
    r"(\d+) known, ack lag ([-\d.]+) ticks, ping ([\d.]+) ms")
TROUBLE = re.compile(r"\b(ERROR|FATAL|Assertion|assert|abort|Segmentation|terminate called)\b")


def limit_memory(megabytes):
    def apply():
        size = megabytes * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (size, size))
        os.setsid()
    return apply


def launch(command, log_path, megabytes):
    log = open(log_path, "w")
    return subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, preexec_fn=limit_memory(megabytes)), log


def read_log(path):
    return ANSI.sub("", Path(path).read_text(errors="replace")).splitlines()


def wait_for(path, needle, seconds, process):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return False
        if any(needle in line for line in read_log(path)):
            return True
        time.sleep(0.25)
    return False


def stop(process, grace):
    if process.poll() is not None:
        return process.returncode
    os.killpg(process.pid, signal.SIGINT)
    try:
        return process.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        return "killed"


def mean(values):
    return sum(values) / len(values) if values else 0.0


def main():
    parser = argparse.ArgumentParser(description="Run a dedicated server with synthetic bot clients and summarize the network.")
    parser.add_argument("--engine", default=str(REPO / "build" / "slopengine"))
    parser.add_argument("--game", default="slopdemo")
    parser.add_argument("--map", default="demoroom")
    parser.add_argument("--bots", type=int, default=3)
    parser.add_argument("--behavior", default="fight", help="still, walk or fight, or a comma list cycled over the bots")
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--lag", type=float, default=0.0, help="milliseconds added to everything each process sends")
    parser.add_argument("--jitter", type=float, default=0.0)
    parser.add_argument("--loss", type=float, default=0.0, help="fraction of unreliable messages each process drops")
    parser.add_argument("--port", type=int, default=27991)
    parser.add_argument("--stats", type=float, default=5.0)
    parser.add_argument("--memory-mb", type=int, default=8000)
    parser.add_argument("--max-bots", type=int, default=8)
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    if args.bots < 1 or args.bots > args.max_bots:
        sys.exit(f"--bots must be between 1 and {args.max_bots}")
    out = Path(args.out) if args.out else Path(tempfile.mkdtemp(prefix="net-soak-"))
    out.mkdir(parents=True, exist_ok=True)
    conditions = []
    if args.lag > 0:
        conditions += ["--net-lag", str(args.lag)]
    if args.jitter > 0:
        conditions += ["--net-jitter", str(args.jitter)]
    if args.loss > 0:
        conditions += ["--net-loss", str(args.loss)]
    address = f"127.0.0.1:{args.port}"
    common = [args.engine, "--game", args.game, "--dedicated", "--net-stats", str(args.stats)] + conditions

    server_log = out / "server.log"
    server, server_file = launch(
        common + ["--profile", "soak-server", "--listen", address, "--map", args.map], server_log, args.memory_mb)
    processes = [("server", server, server_file, server_log)]
    failures = []
    try:
        if wait_for(server_log, "NET: listening", 120, server):
            behaviors = args.behavior.split(",")
            for i in range(args.bots):
                log = out / f"bot-{i + 1}.log"
                bot, bot_file = launch(
                    common + ["--profile", f"soak-bot-{i + 1}", "--connect", address, "--bot", behaviors[i % len(behaviors)]],
                    log, args.memory_mb)
                processes.append((f"bot-{i + 1}", bot, bot_file, log))
                time.sleep(1.0)
            time.sleep(args.seconds)
        else:
            failures.append("server never started listening")
    finally:
        for name, process, _, _ in reversed(processes):
            code = stop(process, 10)
            if code != 0:
                failures.append(f"{name} exited with {code}")
        for _, _, handle, _ in processes:
            handle.close()
    return report(out, processes, failures, args)


def report(out, processes, failures, args):
    print(f"logs in {out}")
    print(f"map {args.map}, {args.bots} bots ({args.behavior}), {args.seconds:.0f} s, "
          f"lag {args.lag:.0f} ms, jitter {args.jitter:.0f} ms, loss {args.loss * 100:.0f}%")
    for name, _, _, path in processes:
        lines = read_log(path)
        for line in lines:
            if TROUBLE.search(line):
                failures.append(f"{name}: {line.strip()}")
        if name == "server":
            for line in lines:
                match = re.search(r"NET: (\S+) left: (.*)", line)
                if match and match.group(2) != "client quit":
                    failures.append(f"server: {match.group(1)} left: {match.group(2)}")
            rows = {}
            for line in lines:
                match = SERVER_STATS.search(line)
                if match:
                    rows.setdefault(match.group(1), []).append(match.groups()[1:])
            for player, samples in sorted(rows.items()):
                steady = samples[1:] or samples
                print(f"  server to {player}: {mean([float(s[0]) for s in steady]):.1f} snapshots/s, "
                      f"{mean([float(s[1]) for s in steady]):.1f} KB/s, {mean([float(s[2]) for s in steady]):.1f} entities "
                      f"and {mean([float(s[3]) for s in steady]):.1f} deferred per snapshot, ack lag "
                      f"{mean([float(s[5]) for s in steady]):.1f} ticks, ping {mean([float(s[6]) for s in steady]):.0f} ms")
            continue
        if not any("NET: receiving snapshots" in line for line in lines):
            failures.append(f"{name} never received snapshots")
        samples = [m.groups() for m in (CLIENT_STATS.search(line) for line in lines) if m]
        steady = samples[1:] or samples
        if not steady:
            failures.append(f"{name} logged no statistics")
            continue
        player = steady[-1][0]
        frames = sum(int(s[9]) for s in steady)
        starved = sum(int(s[8]) for s in steady)
        corrections = sum(int(s[10]) for s in steady)
        worst = max(float(s[11]) for s in steady)
        print(f"  {name} as {player}: {mean([float(s[1]) for s in steady]):.1f} snapshots/s, "
              f"{mean([float(s[2]) for s in steady]):.1f} KB/s, {sum(int(s[5]) for s in steady)} missed, "
              f"bound {steady[-1][6]} of {steady[-1][7]}, starved {starved} of {frames} frames, "
              f"{corrections} corrections, worst {worst:.3f} m")
    if failures:
        print("problems:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("no problems")
    return 0


if __name__ == "__main__":
    sys.exit(main())

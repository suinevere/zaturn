#!/usr/bin/env python3
"""Start, stop and check the two asset review servers.

Description: One command for both halves of the tagging pipeline -- the scene
    server on 8081 and the art server on 8080 -- because they are always used
    together and neither is worth remembering a launch line for.

    Every path is derived from this file's own location, so a checkout
    anywhere works and nothing names a user or a drive. The logic lives here
    rather than in the .bat wrappers for the same reason: a shell script is
    tied to one operating system and this is not.

    Stopping does not trust the PID file alone. Repeated manual launches leave
    copies that failed to bind and never exited, and a PID file written by the
    last clean start knows nothing about those. So a stop sweeps for any
    process whose command line names one of the two servers inside this
    checkout, and sweeps again until the sweep comes back empty.

    Expect two processes per server on Windows. The virtualenv's pythonw.exe
    is a launcher stub that runs the real interpreter as a child and stays
    alive as its parent, and both carry the same command line -- so four
    processes for two servers is healthy, and the honest measure of "running"
    is whether the port answers, never a process count.
Author: suinevere
Dependencies: json, os, pathlib, platform, signal, socket, subprocess, sys, time
Globals: ROOT, SERVERS, PIDFILE
Run: tools/.venv/Scripts/python.exe tools/servers.py start|stop|restart|status
"""
import json
import os
import pathlib
import signal
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent

SERVERS = (
    ("scene", "scene_server.py", 8081),
    ("art", "art_server.py", 8080),
)
"""SERVERS

Description: (name, script, port) for each review server. The port is the one
    the script itself binds -- it is not passed in -- so this table is a
    statement about them, not a configuration of them; changing a number here
    without changing the script's own would only make the status check lie.
Author: suinevere
"""

PIDFILE = ROOT / ".asset_servers.json"


def interpreter(windowless=False):
    """The Python that should run a server.

    Description: Prefers the project virtualenv, since Flask is installed
        there and very likely not in whatever interpreter launched this. On
        Windows a windowless run wants pythonw.exe, which is the difference
        between a detached server and a console window that must stay open.
        Falls back to the running interpreter when the virtualenv is absent,
        so the everything-degrades rule holds and the failure is a missing
        Flask rather than a missing file.
    Author: suinevere
    Dependencies: pathlib, sys
    Globals: ROOT
    Params: windowless -- prefer a GUI-subsystem interpreter where one exists
    Returns: a Path to a Python executable
    """
    if sys.platform == "win32":
        names = ("pythonw.exe", "python.exe") if windowless else ("python.exe",)
        for name in names:
            candidate = ROOT / "tools" / ".venv" / "Scripts" / name
            if candidate.exists():
                return candidate
    else:
        candidate = ROOT / "tools" / ".venv" / "bin" / "python"
        if candidate.exists():
            return candidate
    return pathlib.Path(sys.executable)


def port_held(port):
    """Whether something is already listening on a local port.

    Description: Connects rather than binds. Binding to test would race the
        server that is about to start, and would report a port as free while
        another user's process holds it on a different interface.
    Author: suinevere
    Dependencies: socket
    Globals: N/A
    Params: port -- a TCP port number
    Returns: True when a connection succeeds
    """
    with socket.socket() as sock:
        sock.settimeout(0.4)
        return sock.connect_ex(("127.0.0.1", port)) == 0


def running_pids():
    """Every process on this machine running one of our servers, by PID.

    Description: Asks the operating system rather than a file, because the
        processes worth killing are exactly the ones no file recorded. Scoped
        to this checkout by matching the repository path in the command line,
        so a second clone's servers are left alone. Uses PowerShell's CIM
        query on Windows and pgrep elsewhere; if neither is available the
        sweep degrades to nothing found, and the PID file still covers the
        ordinary case.

        Excludes any command line mentioning Get-CimInstance, because the
        query names both servers and so the PowerShell process running it
        matches itself -- which made the sweep-until-empty loop spawn the very
        thing it was waiting to stop seeing.
    Author: suinevere
    Dependencies: subprocess, sys
    Globals: ROOT, SERVERS
    Params: N/A
    Returns: a sorted list of PIDs
    """
    scripts = [script for _name, script, _port in SERVERS]
    found = set()
    if sys.platform == "win32":
        query = (
            "Get-CimInstance Win32_Process | "
            "Where-Object { $_.CommandLine -and "
            "$_.CommandLine -notlike '*Get-CimInstance*' -and "
            "$_.CommandLine -like '*" + ROOT.name + "*' -and ("
            + " -or ".join(f"$_.CommandLine -like '*{s}*'" for s in scripts)
            + ") } | ForEach-Object { $_.ProcessId }")
        try:
            out = subprocess.run(
                ["powershell", "-NoProfile", "-Command", query],
                capture_output=True, text=True, timeout=30).stdout
        except (OSError, subprocess.SubprocessError):
            return []
        for line in out.split():
            if line.strip().isdigit():
                found.add(int(line.strip()))
    else:
        for script in scripts:
            try:
                out = subprocess.run(["pgrep", "-f", script],
                                     capture_output=True, text=True,
                                     timeout=30).stdout
            except (OSError, subprocess.SubprocessError):
                return []
            for line in out.split():
                if line.strip().isdigit():
                    found.add(int(line.strip()))
    found.discard(os.getpid())
    return sorted(found)


def kill(pid):
    """End one process, treating "already gone" as success.

    Description: A PID from a stale file or a sweep may have exited between
        the listing and the kill, which is the expected case rather than an
        error.
    Author: suinevere
    Dependencies: os, signal
    Globals: N/A
    Params: pid -- a process id
    Returns: True if the process is no longer running because of this call
    """
    try:
        os.kill(pid, signal.SIGTERM)
        return True
    except (OSError, PermissionError):
        return False


def stop(quiet=False):
    """Stop both servers, including any the PID file never knew about.

    Description: Sweeps first and merges the PID file in, so a run that was
        started by hand is caught alongside one this script started. Kills
        highest PID first, which is usually the launcher's child, and then
        re-sweeps: killing a parent does not take its child with it on
        Windows, and the child is the one holding the port.
    Author: suinevere
    Dependencies: json, time
    Globals: PIDFILE, SERVERS
    Params: quiet -- suppress the per-server report
    Returns: the number of processes stopped
    """
    targets = set(running_pids())
    if PIDFILE.exists():
        try:
            targets.update(json.loads(PIDFILE.read_text(encoding="utf-8"))
                           .get("pids", []))
        except (json.JSONDecodeError, OSError):
            pass
        PIDFILE.unlink(missing_ok=True)

    stopped = sum(1 for pid in sorted(targets, reverse=True) if kill(pid))
    for _ in range(3):
        time.sleep(0.4)
        left = running_pids()
        if not left:
            break
        stopped += sum(1 for pid in left if kill(pid))
    if not quiet:
        print(f"  stopped {stopped} process(es)")
        for _name, _script, port in SERVERS:
            if port_held(port):
                print(f"  WARNING {port} is still held by something else")
    return stopped


def start():
    """Stop anything running, then launch both servers detached.

    Description: Always stops first. A second copy cannot bind the port, and
        the copy that fails is exactly the kind of process that lingers
        invisibly. Each server's output goes to its own pair of logs in the
        repository root, because a detached windowless process has nowhere
        else to put it.
    Author: suinevere
    Dependencies: json, subprocess, sys, time
    Globals: ROOT, SERVERS, PIDFILE
    Params: N/A
    Returns: 0 when both ports are held afterwards, 1 otherwise
    """
    stop(quiet=True)
    python = interpreter(windowless=True)
    pids = []
    for name, script, _port in SERVERS:
        out = open(ROOT / f"{name}_server.log", "w", encoding="utf-8")
        err = open(ROOT / f"{name}_server.err.log", "w", encoding="utf-8")
        kwargs = {}
        if sys.platform == "win32":
            kwargs["creationflags"] = (subprocess.DETACHED_PROCESS
                                       | subprocess.CREATE_NEW_PROCESS_GROUP)
        else:
            kwargs["start_new_session"] = True
        proc = subprocess.Popen(
            [str(python), str(pathlib.Path("tools") / script)],
            cwd=str(ROOT), stdout=out, stderr=err, stdin=subprocess.DEVNULL,
            **kwargs)
        pids.append(proc.pid)

    PIDFILE.write_text(json.dumps({"pids": pids}, indent=1) + "\n",
                       encoding="utf-8")

    deadline = time.time() + 15
    while time.time() < deadline:
        if all(port_held(port) for _n, _s, port in SERVERS):
            break
        time.sleep(0.4)
    return status()


def status():
    """Report which servers answer, and where.

    Description: Reports the port, not the process: a live PID that failed to
        bind is not a running server, and that distinction is the whole reason
        the stale ones went unnoticed for a day.
    Author: suinevere
    Dependencies: N/A
    Globals: ROOT, SERVERS
    Params: N/A
    Returns: 0 when every server answers, 1 otherwise
    """
    bad = 0
    for name, script, port in SERVERS:
        if port_held(port):
            print(f"  up    {name:6} http://127.0.0.1:{port}")
        else:
            bad += 1
            log = ROOT / f"{name}_server.err.log"
            print(f"  DOWN  {name:6} port {port} -- see {log.name}")
    return 1 if bad else 0


def main(argv):
    """Dispatch start, stop, restart or status.

    Description: Defaults to status, the only harmless one.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: argv -- command-line arguments
    Returns: a process exit code
    """
    command = (argv[0] if argv else "status").lstrip("-").lower()
    if command in ("start", "restart"):
        return start()
    if command == "stop":
        stop()
        return 0
    if command == "status":
        return status()
    print("  usage: servers.py start|stop|restart|status")
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

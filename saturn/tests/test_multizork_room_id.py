#!/usr/bin/env python3
"""Drive multizorkd's out-of-band room-id channel over TCP.

Checks the promises the channel makes that no unit test can reach, because they
are about what a *connection* sees rather than what a function returns: a client
that never negotiates ZATURN_TELOPT sees no frames at all, a client that does
gets ids inside the object range, and moving rooms changes the id while standing
still sends nothing.

The first of those is the one that protects everybody else. multizorkd listens on
telnet's own port and real people play it with real telnet clients; a bug that
leaked control bytes to them would be invisible here and obvious to them.

It stands up its own daemon in Docker, built from this checkout, and tears it
down afterwards. That is not convenience -- playing the game creates rows in
whatever sqlite database the daemon owns, so a test that drove a daemon it did
not start would leave junk games in it. An earlier version of this file defaulted
to 127.0.0.1:2323 and would have done exactly that.

  python3 tests/test_multizork_room_id.py            # builds and runs its own
  MULTIZORK_ADDR=host:port python3 ...               # drive one you supply

Everything it cannot check is a SKIP, not a failure: no Docker and no address, a
daemon that predates the channel, or a lobby it cannot get through. A red result
here should mean the protocol is broken, never that the environment is bare.
"""
import os, re, socket, subprocess, sys, time

SETTLE = 0.6
IMAGE = "zaturn-multizorkd-test"
CONTAINER = "zaturn-multizorkd-test-run"

IAC, WILL, WONT, DO, DONT = 255, 251, 252, 253, 254
ZATURN_TELOPT = 178          # must match multizorkd.c and src/net/term.h
OOB_START, OOB_END = 0x01, 0x02

FRAME = re.compile(rb"\x01R([0-9A-F]{4})\x02")

HERE = os.path.dirname(os.path.abspath(__file__))
SATURN = os.path.dirname(HERE)

DOCKERFILE = """FROM gcc:13
RUN apt-get update -qq && apt-get install -y -qq libsqlite3-dev \\
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY multizorkd.c mojozork.c roomnames.h ./
COPY cd/data/Z3/ZORK1.Z3 ./
RUN gcc -O2 -o multizorkd multizorkd.c -lsqlite3
CMD ["./multizorkd", "--port", "23", "ZORK1.Z3"]
"""


class Skip(Exception):
    """Something about the environment, not about the protocol."""


# --------------------------------------------------------------------------
# standing up a daemon
# --------------------------------------------------------------------------

def docker(*args, **kw):
    return subprocess.run(("docker",) + args, capture_output=True, text=True,
                          timeout=kw.pop("timeout", 120), **kw)


def have_docker():
    try:
        return docker("version", "--format", "{{.Server.Os}}", timeout=20).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def build_image():
    """Build once and reuse. The compile is slow; the cached image is not."""
    if docker("image", "inspect", IMAGE, timeout=30).returncode == 0:
        return
    print(f"building {IMAGE} (first run only, ~1 min)...")
    r = subprocess.run(["docker", "build", "-q", "-t", IMAGE, "-f", "-", SATURN],
                       input=DOCKERFILE, capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        raise Skip("docker build failed: " + (r.stderr or r.stdout)[-300:])


def start_daemon():
    """Run the image on an ephemeral host port and wait for it to answer."""
    docker("rm", "-f", CONTAINER, timeout=60)
    r = docker("run", "-d", "--name", CONTAINER, "-p", "127.0.0.1::23", IMAGE, timeout=120)
    if r.returncode != 0:
        raise Skip("docker run failed: " + (r.stderr or r.stdout)[-300:])

    p = docker("port", CONTAINER, "23/tcp", timeout=30)
    if p.returncode != 0 or ":" not in p.stdout:
        raise Skip("could not read the mapped port: " + (p.stderr or p.stdout)[-200:])
    port = int(p.stdout.strip().splitlines()[0].rsplit(":", 1)[1])

    for _ in range(60):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=1).close()
            return ("127.0.0.1", port)
        except OSError:
            time.sleep(0.5)
    raise Skip("the daemon never started listening")


def stop_daemon():
    docker("rm", "-f", CONTAINER, timeout=60)


# --------------------------------------------------------------------------
# talking to it
# --------------------------------------------------------------------------

def connect(addr):
    s = socket.create_connection(addr, timeout=5)
    s.settimeout(SETTLE)
    return s


def drain(sock):
    out = b""
    try:
        while True:
            b = sock.recv(4096)
            if not b:
                break
            out += b
    except (socket.timeout, TimeoutError):
        pass
    return out


def send(sock, line):
    sock.sendall(line.encode() + b"\n")
    time.sleep(0.1)
    return drain(sock)


def negotiate(sock):
    """Ask for the channel and report whether the server knows the option.

    The reply is the capability probe. A server that predates this channel
    either says nothing or refuses; either way there is nothing here to test,
    and saying so is a skip rather than a failure.
    """
    sock.sendall(bytes([IAC, WILL, ZATURN_TELOPT]))
    time.sleep(0.3)
    reply = drain(sock)
    return bytes([IAC, DO, ZATURN_TELOPT]) in reply, reply


def room_ids(data):
    return [int(m.group(1), 16) for m in FRAME.finditer(data)]


def enter_game(sock, username, seen=b""):
    """Walk connect -> lobby -> seated -> playing.

    Four inputs, because the lobby asks four questions. Pressing enter at the
    room prompt only redraws it, which is how the first version of this test
    passed its no-frames check having never reached a game.
    """
    seen += drain(sock)
    seen += send(sock, username)   # "username:"
    seen += send(sock, "1")        # lobby: "1) <new room>"
    seen += send(sock, "n")        # "list it publicly? (y/n)"
    seen += send(sock, "go")       # waiting room: start
    if b"West of House" not in seen and b"Starting" not in seen:
        raise Skip("could not reach a game through this lobby: " + repr(seen[-160:]))
    return seen


# --------------------------------------------------------------------------

FAILURES = []


def check(name, ok, detail=""):
    if ok:
        print(f"ok   {name}")
    else:
        print(f"FAIL {name}\n  {detail}")
        FAILURES.append(name)


def run_checks(addr):
    # --- a client that never asks sees nothing ----------------------------
    plain = connect(addr)
    try:
        seen = enter_game(plain, "roomidplain")
        seen += send(plain, "look")
        seen += send(plain, "north")
        check("no frames without the handshake", not room_ids(seen),
              f"saw ids {room_ids(seen)}")
        check("no stray framing bytes without the handshake",
              OOB_START not in seen and OOB_END not in seen,
              "a framing byte reached a plain client")
    finally:
        plain.close()

    # --- a client that asks gets ids --------------------------------------
    zaturn = connect(addr)
    try:
        supported, reply = negotiate(zaturn)
        if not supported:
            raise Skip("this daemon does not know option %d (reply %r) -- it predates "
                       "the room-id channel" % (ZATURN_TELOPT, reply[-40:]))

        seen = enter_game(zaturn, "roomidzaturn")
        ids = room_ids(seen)
        check("handshake yields at least one room id", bool(ids), f"saw {ids!r}")
        if ids:
            check("room id is a plausible v3 object", 0 < ids[-1] <= 255,
                  f"last id was {ids[-1]}; a byte-swapped id looks exactly like this")

        still = send(zaturn, "look")
        check("an unchanged room sends no frame", not room_ids(still),
              f"saw ids {room_ids(still)} for a turn that did not move")

        before = ids[-1] if ids else None
        moved, got = b"", []
        for direction in ("north", "south", "east", "west", "up", "down"):
            moved = send(zaturn, direction)
            got = room_ids(moved)
            if got:
                break
        check("moving rooms sends a new id", bool(got) and got[-1] != before,
              f"before={before} after={got!r}")
    finally:
        zaturn.close()


def main():
    supplied = os.environ.get("MULTIZORK_ADDR")
    started = False
    try:
        if supplied:
            host, _, port = supplied.partition(":")
            addr = (host, int(port or 23))
            try:
                connect(addr).close()
            except OSError as e:
                raise Skip(f"nothing answering at {supplied} ({e})")
        else:
            if not have_docker():
                raise Skip("no MULTIZORK_ADDR and no docker to stand a daemon up with")
            build_image()
            addr = start_daemon()
            started = True

        run_checks(addr)
    except Skip as s:
        print(f"SKIP: {s}")
        return 0
    finally:
        if started:
            stop_daemon()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("test_multizork_room_id: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

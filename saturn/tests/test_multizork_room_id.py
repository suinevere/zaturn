#!/usr/bin/env python3
"""Drive multizorkd's out-of-band room-id channel over TCP.

Checks the three promises the channel makes that no unit test can reach, because
all three are about what a *connection* sees rather than what a function
returns: a client that never negotiates ZATURN_TELOPT sees no frames at all, a
client that does gets one whose payload decodes to a plausible room object id,
and moving between rooms changes it while standing still does not.

The first of those is the one that protects everybody else. multizorkd listens
on telnet's own port and real people play it with real telnet clients; a bug
that leaks control bytes to them would be invisible to us and obvious to them.

Drives a running daemon rather than spawning one: multizorkd is POSIX-only and
wants sqlite3, so the caller supplies the address. Skips when nothing answers.

  MULTIZORK_ADDR=127.0.0.1:2323 python3 tests/test_multizork_room_id.py
"""
import os, re, socket, sys, time

ADDR = os.environ.get("MULTIZORK_ADDR", "127.0.0.1:2323")
HOST, _, PORT = ADDR.partition(":")
PORT = int(PORT or 2323)

SETTLE = 0.6

IAC, WILL, DO = 255, 251, 253
ZATURN_TELOPT = 178
OOB_START, OOB_END = 0x01, 0x02

FRAME = re.compile(rb"\x01R([0-9A-F]{4})\x02")


def connect():
    s = socket.create_connection((HOST, PORT), timeout=5)
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
    sock.sendall(bytes([IAC, WILL, ZATURN_TELOPT]))
    time.sleep(0.1)


def room_ids(data):
    return [int(m.group(1), 16) for m in FRAME.finditer(data)]


def enter_game(sock, username):
    """Walk the whole connect -> lobby -> seated -> playing path.

    Four steps, because the lobby is four questions: who are you, which room,
    list it publicly, and is everyone here. Pressing enter at the room prompt
    only redraws the lobby, which is what made the first version of this test
    pass its no-frames check for the wrong reason -- it never reached a game.
    """
    seen = drain(sock)
    seen += send(sock, username)   # "username:"
    seen += send(sock, "1")        # lobby: "1) <new room>"
    seen += send(sock, "n")        # "list it publicly? (y/n)" -- keep it out of the lobby
    seen += send(sock, "go")       # waiting room: start the game
    assert b"West of House" in seen or b"Starting" in seen,         "never reached a game: " + repr(seen[-200:])
    return seen


FAILURES = []


def check(name, ok, detail=""):
    if ok:
        print(f"ok   {name}")
    else:
        print(f"FAIL {name}\n  {detail}")
        FAILURES.append(name)


def main():
    try:
        plain = connect()
    except OSError as e:
        print(f"SKIP: nothing answering at {ADDR} ({e})")
        return 0

    # --- a client that never asks sees nothing -----------------------------
    seen = enter_game(plain, "roomidplain")
    seen += send(plain, "look")
    seen += send(plain, "north")
    check("no frames without the handshake", not room_ids(seen),
          f"saw ids {room_ids(seen)}")
    check("no stray SOH/STX without the handshake",
          OOB_START not in seen and OOB_END not in seen,
          "found a framing byte in a plain client's stream")
    plain.close()

    # --- a client that asks gets ids ---------------------------------------
    zaturn = connect()
    negotiate(zaturn)
    seen = enter_game(zaturn, "roomidzaturn")
    ids = room_ids(seen)
    check("handshake yields at least one room id", bool(ids), f"saw {ids!r}")

    if ids:
        check("room id is a plausible v3 object", 0 < ids[-1] <= 255,
              f"last id was {ids[-1]}, outside 1..255 -- a byte-swap would look like this")

    # --- standing still sends nothing, moving sends a new one --------------
    still = send(zaturn, "look")
    check("an unchanged room sends no frame", not room_ids(still),
          f"saw ids {room_ids(still)} for a turn that did not move")

    before = ids[-1] if ids else None
    moved = b""
    for direction in ("north", "south", "east", "west", "up", "down"):
        moved = send(zaturn, direction)
        if room_ids(moved):
            break
    got = room_ids(moved)
    check("moving rooms sends a new id", bool(got) and got[-1] != before,
          f"before={before} after={got!r}")

    zaturn.close()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("test_multizork_room_id: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

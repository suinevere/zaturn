#!/usr/bin/env python3
"""Assert a room name works at the lobby, now that a plain username prompt removes the guess the old first prompt had to make.

Commit 6521e7f taught the connect prompt to accept a game code, because that
prompt came before the name and a new arrival had no other code. With the name
asked first that ambiguity is gone, so the assertion moves to the lobby
prompt rather than disappearing.

Drives a running daemon rather than spawning one: multizorkd is POSIX-only and
wants sqlite3, so the caller supplies the address. Skips when nothing answers.

  MULTIZORK_ADDR=127.0.0.1:2323 python3 tests/test_multizork_join.py
"""
import os, re, socket, sys, time

ADDR = os.environ.get("MULTIZORK_ADDR", "127.0.0.1:2323")
HOST, _, PORT = ADDR.partition(":")
PORT = int(PORT or 2323)

# Long enough that a prompt written after a database round-trip still lands in
# the same read; multizorkd never bursts, so a quiet socket means it is done.
SETTLE = 0.4

def connect():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(SETTLE)
    return s

def drain(sock):
    out = b""
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk: break
            out += chunk
    except socket.timeout:
        pass
    return out.decode("latin-1")

def send(sock, line):
    sock.sendall((line + "\r\n").encode("latin-1"))
    time.sleep(SETTLE)
    return drain(sock)

def host_a_game():
    """Return (connection, room name) for a host sitting in the waiting room."""
    h = connect()
    drain(h)
    send(h, "seanie")
    out = send(h, "n")
    m = re.search(r"room is called '([\w-]+)'", out)
    assert m, f"host never got a room name, saw: {out!r}"
    send(h, "y")
    return h, m.group(1)

def check(label, condition, saw):
    if condition:
        return 0
    print(f"FAILED {label}: {saw!r}", file=sys.stderr)
    return 1

def main():
    try:
        connect().close()
    except OSError as e:
        print(f"test_multizork_join: SKIP (nothing listening on {ADDR}: {e})")
        return

    fails = 0

    host, room = host_a_game()
    guest = connect(); drain(guest)
    out = send(guest, "bob")
    fails += check("lobby lists the waiting room", room in out, out)
    out = send(guest, room)
    fails += check("room name at the lobby joins", "current party" in out.lower(), out)
    out = drain(host)
    fails += check("host is told a guest joined", "has joined" in out, out)
    guest.close(); host.close()

    host, room = host_a_game()
    guest = connect(); drain(guest)
    send(guest, "bob")
    out = send(guest, room.upper())
    fails += check("room name is case-insensitive", "current party" in out.lower(), out)

    out = send(host, "go")
    fails += check("the game starts", "Starting." in out, out)
    fails += check("and names the room as the way back in",
                   f"'{room}' in the Lobby" in out, out)

    # An access code is handed out when a newcomer sits down at a running game,
    # which is the only place one is offered before you leave.
    late = connect(); drain(late)
    send(late, "cass")
    out = send(late, room)
    m = re.search(r"access code: '(\w+)'", out)
    fails += check("a newcomer to a running game is handed an access code", bool(m), out)
    if m:
        late.close()
        time.sleep(1.0)
        again = connect(); drain(again)
        send(again, "cass")
        out = send(again, m.group(1))
        fails += check("access code at the lobby still rejoins",
                       "Got it." in out, out)
        again.close()
    guest.close(); host.close()

    if fails:
        print(f"test_multizork_join: {fails} FAILED", file=sys.stderr)
        sys.exit(1)
    print("test_multizork_join: OK")

main()

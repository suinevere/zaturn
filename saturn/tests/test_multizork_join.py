#!/usr/bin/env python3
"""Assert a game code works at multizorkd's first prompt, not just three prompts in.

The host is told to hand friends a six-character game code. The first thing a
friend sees is the "hello sailor" prompt, which historically accepted only a
player's rejoin code, so pasting the game code there answered "I can't find a
game with that access code" -- indistinguishable, from the outside, from
multiplayer being broken. Both entry points are checked here because fixing the
first one must not disturb the menu route that already worked.

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
    """Return (connection, game code) for a host sitting in the waiting room."""
    h = connect()
    drain(h)
    send(h, "")
    send(h, "seanie")
    out = send(h, "1")
    m = re.search(r"join game '(\w+)'", out)
    assert m, f"host never got a game code, saw: {out!r}"
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

    # The reported path: paste the code the host read out, at the first prompt.
    host, code = host_a_game()
    guest = connect(); drain(guest)
    out = send(guest, code)
    fails += check("code at first prompt is recognised",
                   "can't find a game" not in out, out)
    fails += check("code at first prompt asks for a name",
                   "name" in out.lower(), out)
    out = send(guest, "bob")
    fails += check("naming after a code joins the game",
                   "guest list" in out.lower(), out)
    fails += check("host is told the guest arrived",
                   "has joined this game" in drain(host), "(nothing)")
    guest.close(); host.close()

    # The menu path has to keep working: enter, name, '2', then the code.
    host, code = host_a_game()
    guest = connect(); drain(guest)
    send(guest, ""); send(guest, "bob"); send(guest, "2")
    out = send(guest, code)
    fails += check("menu route still joins", "Found it" in out, out)
    guest.close(); host.close()

    # A real access code still has to reach reconnect_player, which now sits
    # behind the game-code check.
    host, code = host_a_game()
    guest = connect(); drain(guest)
    send(guest, code)
    send(guest, "bob")
    out = send(host, "go")
    m = re.search(r"access code: '(\w+)'", out)
    fails += check("a started game hands the host an access code", bool(m), out)
    if m:
        host.close()
        again = connect(); drain(again)
        out = send(again, m.group(1))
        fails += check("access code at first prompt still rejoins",
                       "We found you" in out, out)
        again.close()
    guest.close()

    if fails:
        print(f"test_multizork_join: {fails} FAILED", file=sys.stderr)
        sys.exit(1)
    print("test_multizork_join: OK")

main()

#!/usr/bin/env python3
"""Drive multizorkd's lobby over TCP: listing, privacy, case, and resume.

Checks the promises the lobby makes that no unit test can reach, because they
are all about what one connection can see of another's room. A private room is
the interesting case: absent from the list, but still enterable by anyone told
its name, which is the whole point of hiding it.

Drives a running daemon rather than spawning one: multizorkd is POSIX-only and
wants sqlite3, so the caller supplies the address. Skips when nothing answers.

  MULTIZORK_ADDR=127.0.0.1:2323 python3 tests/test_multizork_lobby.py
"""
import os, re, socket, sys, time

ADDR = os.environ.get("MULTIZORK_ADDR", "127.0.0.1:2323")
HOST, _, PORT = ADDR.partition(":")
PORT = int(PORT or 2323)

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

def check(label, condition, saw):
    if condition:
        return 0
    print(f"FAIL: {label}\n  saw: {saw!r}", file=sys.stderr)
    return 1

def make_room(username, listed):
    """Return (connection, room name) for a host in the waiting room."""
    c = connect()
    drain(c)
    send(c, username)
    out = send(c, "n")
    m = re.search(r"room is called '([\w-]+)'", out)
    if not m:
        print(f"FAIL: host never got a room name\n  saw: {out!r}", file=sys.stderr)
        sys.exit(1)
    send(c, "y" if listed else "n")
    return c, m.group(1)

def main():
    try:
        probe = connect()
    except OSError:
        print("test_multizork_lobby: SKIP (nothing listening on %s)" % ADDR)
        return
    fails = 0
    greeting = drain(probe)
    fails += check("first prompt asks for a username", "username:" in greeting, greeting)
    probe.close()

    # Choice 1 is always the new room, and a listed room answers to its number.
    numberer = connect(); drain(numberer)
    out = send(numberer, "picker")
    fails += check("the new room is choice 1", "  1) <new room>" in out, out)
    out = send(numberer, "1")
    m = re.search(r"room is called '([\w-]+)'", out)
    fails += check("typing 1 builds a room", m is not None, out)
    if m:
        send(numberer, "y")
        watcher = connect(); drain(watcher)
        out = send(watcher, "watcher")
        n = re.search(r"\s*(\d+)\) %s" % re.escape(m.group(1)), out)
        fails += check("the listed room carries a number", n is not None, out)
        if n:
            out = send(watcher, n.group(1))
            fails += check("and that number opens it", "current party" in out.lower(), out)
        watcher.close()
    numberer.close()

    # An empty line is the only way to see the list again.
    refresher = connect(); drain(refresher)
    send(refresher, "refresher")
    out = send(refresher, "")
    fails += check("an empty line reprints the lobby",
                   "Lobby" in out and "<new room>" in out, out)
    out = send(refresher, "nosuchroom")
    fails += check("a bad line does not reprint it",
                   "Wrong choice or room name." in out and "<new room>" not in out, out)
    refresher.close()

    # Whatever else is going on, the lobby always offers the two standing choices.
    # That is the "default" a player lands on when nothing is listed at all.
    solo = connect(); drain(solo)
    out = send(solo, "wanderer")
    fails += check("lobby offers a new room", "<new room>" in out, out)
    fails += check("lobby offers to quit", "quit" in out, out)
    solo.close()

    # A generated room name is two real words joined by a hyphen.
    host, room = make_room("seanie", listed=True)
    fails += check("room name is an adjective-noun pair",
                   re.fullmatch(r"[a-z]+-[a-z]+(-\d\d)?", room) is not None, room)

    # A listed room shows up for someone else, named, with its seat count. The
    # lobby has no room for occupant names on a 40 column screen, so it counts
    # them instead.
    guest = connect(); drain(guest)
    out = send(guest, "ashley")
    fails += check("listed room appears in the lobby", room in out, out)
    fails += check("lobby counts the seats taken", "(1/4)" in out, out)
    guest.close()

    # A private room does not appear, but its name still opens it.
    quiet_host, quiet = make_room("mira", listed=False)
    guest = connect(); drain(guest)
    out = send(guest, "bob")
    fails += check("the lobby answered at all", "<new room>" in out, out)
    fails += check("private room is absent from the lobby", quiet not in out, out)
    out = send(guest, quiet)
    fails += check("private room opens to anyone told its name",
                   "current party" in out.lower(), out)
    guest.close(); quiet_host.close()

    # Case is ignored, because the name is meant to be read aloud.
    guest = connect(); drain(guest)
    send(guest, "bob")
    out = send(guest, room.upper())
    fails += check("room name is case-insensitive", "current party" in out.lower(), out)
    guest.close()

    # Bring a second player in so the game outlives the host walking away.
    stayer = connect(); drain(stayer)
    send(stayer, "ashley")
    send(stayer, room)
    out = send(host, "go")
    fails += check("game starts", "Starting." in out, out)

    # The host drops. The game is still running, so it is offered back as such.
    host.close()
    time.sleep(1.0)
    back = connect(); drain(back)
    out = send(back, "seanie")
    fails += check("a game still running is offered back", room in out, out)
    liverow = re.search(r"\s*\d+\) %s\s+\(([^)]*)\)" % re.escape(room[:15]), out)
    fails += check("and shown as live rather than aged",
                   liverow is not None and liverow.group(1) in ("public", "private"), out)
    out = send(back, room)
    fails += check("rejoining reaches the game prompt", ">" in out, out)
    back.close()
    time.sleep(1.0)

    # Now everybody leaves, so the same game is offered back from the archive.
    stayer.close()
    time.sleep(1.0)
    back = connect(); drain(back)
    out = send(back, "seanie")
    fails += check("an archived game is offered back", room in out, out)
    fails += check("and marked as left behind", "ago)" in out, out)
    out = send(back, room)
    fails += check("resuming reaches the game prompt", ">" in out, out)
    back.close()

    # Somebody else does not see it.
    other = connect(); drain(other)
    out = send(other, "nobody")
    fails += check("the lobby answered a different name at all", "<new room>" in out, out)
    fails += check("another name is not offered that game", room not in out, out)
    other.close()

    # A game already under way still has seats, and a stranger can take one.
    host, room = make_room("ivy", listed=True)
    out = send(host, "go")
    fails += check("solo game starts", "Starting." in out, out)

    late = connect(); drain(late)
    out = send(late, "bob")
    fails += check("a started room is still listed", room in out, out)
    out = send(late, room)
    fails += check("late arrival reaches the game prompt", ">" in out, out)
    fails += check("late arrival starts at the beginning", "West of House" in out, out)
    fails += check("late arrival gets an access code",
                   re.search(r"access code: '(\w+)'", out) is not None, out)

    out = drain(host)
    fails += check("the table is told someone sat down", "bob has joined" in out, out)

    # Fill it, and it drops off the list and refuses a fifth.
    extras = []
    for name in ("cass", "dev"):
        e = connect(); drain(e)
        send(e, name)
        send(e, room)
        extras.append(e)

    fifth = connect(); drain(fifth)
    out = send(fifth, "nael")
    fails += check("a full room leaves the lobby", room not in out, out)
    out = send(fifth, room)
    fails += check("a full room refuses a fifth player", "Room is full!" in out, out)
    fifth.close()

    # A seat whose owner merely walked away is still theirs. Drop one of the
    # four and the room must stay off the list and keep refusing strangers --
    # this is the case a careless seat_available_for() would quietly break.
    extras[0].close()
    time.sleep(1.0)
    stranger = connect(); drain(stranger)
    out = send(stranger, "opportunist")
    fails += check("an absent player's seat does not reopen the room", room not in out, out)
    out = send(stranger, room)
    fails += check("an absent player's seat is not handed to a stranger",
                   "Room is full!" in out, out)
    stranger.close()

    # The owner of that seat still gets it back.
    owner = connect(); drain(owner)
    out = send(owner, "cass")
    fails += check("the absent player is offered their game", room in out, out)
    owner.close()

    for e in extras[1:]: e.close()
    late.close(); host.close()

    # Leaving a room that never started is not a hangup: both the guest who
    # walks out and the host who closes the room land back at the lobby on the
    # same connection, and the room is gone from it.
    quitter_host, quitroom = make_room("dana", listed=True)
    quitter = connect(); drain(quitter)
    send(quitter, "eli")
    send(quitter, quitroom)
    out = send(quitter, "quit")
    fails += check("a guest leaving a waiting room lands at the lobby",
                   "Lobby" in out and "<new room>" in out, out)
    out = send(quitter, "q")
    fails += check("and is still talking to us afterwards", "bye" in out.lower(), out)
    quitter.close()

    out = send(quitter_host, "quit")
    fails += check("a host closing a waiting room lands at the lobby",
                   "Lobby" in out and "<new room>" in out, out)
    fails += check("and the closed room is no longer listed", quitroom not in out, out)
    quitter_host.close()

    # The host closing the room tells the guests why, and hands them the lobby.
    told_host, toldroom = make_room("frank", listed=True)
    told = connect(); drain(told)
    send(told, "gina")
    send(told, toldroom)
    send(told_host, "quit")
    time.sleep(1.0)
    out = drain(told)
    fails += check("a guest is told who closed the room", "frank terminated the game" in out, out)
    fails += check("and is handed the lobby", "<new room>" in out, out)
    told.close(); told_host.close()

    if fails:
        print(f"test_multizork_lobby: {fails} FAILED", file=sys.stderr)
        sys.exit(1)
    print("test_multizork_lobby: OK")

main()

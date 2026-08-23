"""The launcher must not lie about which ports the servers actually bind."""
import socket
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import servers


def test_every_listed_port_is_the_one_that_script_really_binds():
    """SERVERS is a claim about the scripts, not a configuration of them. A
    number changed here and nowhere else would only make the status check
    report a healthy server as down, or a dead one as up."""
    for _name, script, port in servers.SERVERS:
        source = (REPO / "tools" / script).read_text(encoding="utf-8")
        assert str(port) in source, f"{script} never mentions {port}"


def test_the_two_servers_do_not_share_a_port():
    ports = [port for _n, _s, port in servers.SERVERS]
    assert len(set(ports)) == len(ports)


def test_every_listed_script_exists():
    for _name, script, _port in servers.SERVERS:
        assert (REPO / "tools" / script).exists(), script


def test_port_held_is_true_only_while_something_listens():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        sock.listen(1)
        port = sock.getsockname()[1]
        assert servers.port_held(port)
    assert not servers.port_held(port)


def test_the_interpreter_it_would_launch_actually_exists():
    assert servers.interpreter().exists()
    assert servers.interpreter(windowless=True).exists()


def test_the_sweep_never_reports_the_process_running_it():
    """The Windows query names both servers, so the shell running it matches
    the pattern it is searching for -- and a stop that killed its own query
    would loop forever waiting for the sweep to come back empty."""
    assert servers.running_pids().count(__import__("os").getpid()) == 0


def test_paths_are_derived_from_the_checkout_not_hardcoded():
    """A path naming a user or a drive would break on the next clone."""
    source = (REPO / "tools" / "servers.py").read_text(encoding="utf-8")
    assert ":\\" not in source and "/Users/" not in source
    assert servers.ROOT == REPO

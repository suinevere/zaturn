"""Thin client for the GhidraMCP HTTP bridge (bethington/ghidra-mcp).

When a Saturn game references text through code rather than a flat pointer table
(e.g. the address is computed in an SH-2 routine), Ghidra is the right tool to
find it. GhidraMCP exposes its analysis over a localhost HTTP API; this client
wraps the handful of endpoints the translation workflow needs so the Saturn MCP
server can drive Ghidra without the user leaving the chat.

Set GHIDRA_MCP_URL (default http://127.0.0.1:8089) and, if you enabled auth on
the Ghidra side, GHIDRA_MCP_AUTH_TOKEN.
"""

from __future__ import annotations

import os

import httpx

DEFAULT_URL = os.environ.get("GHIDRA_MCP_URL", "http://127.0.0.1:8089")


class GhidraError(RuntimeError):
    pass


class GhidraClient:
    def __init__(self, base_url: str | None = None, token: str | None = None, timeout: float = 30.0):
        self.base_url = (base_url or DEFAULT_URL).rstrip("/")
        self.token = token if token is not None else os.environ.get("GHIDRA_MCP_AUTH_TOKEN", "")
        self.timeout = timeout

    def _headers(self) -> dict:
        h = {}
        if self.token:
            h["Authorization"] = f"Bearer {self.token}"
        return h

    def _get(self, path: str, params: dict | None = None) -> str:
        url = f"{self.base_url}/{path.lstrip('/')}"
        with httpx.Client(timeout=self.timeout) as client:
            r = client.get(url, params=params or {}, headers=self._headers())
            r.raise_for_status()
            return r.text

    def _post(self, path: str, data: dict | None = None) -> str:
        url = f"{self.base_url}/{path.lstrip('/')}"
        with httpx.Client(timeout=self.timeout) as client:
            r = client.post(url, data=data or {}, headers=self._headers())
            r.raise_for_status()
            return r.text

    # ── workflow helpers ───────────────────────────────────────────────
    def check_connection(self) -> str:
        return self._get("check_connection")

    def list_strings(self, limit: int = 200, offset: int = 0) -> str:
        """List strings Ghidra extracted from the loaded program."""
        return self._get("list_strings", {"limit": limit, "offset": offset})

    def search_memory_strings(self, pattern: str, limit: int = 100) -> str:
        return self._get("search_memory_strings", {"pattern": pattern, "limit": limit})

    def get_xrefs_to(self, address: str) -> str:
        """Cross-references to an address — find the code/pointer using a string."""
        return self._get("get_xrefs_to", {"address": address})

    def read_memory(self, address: str, length: int) -> str:
        return self._get("read_memory", {"address": address, "length": length})

    def decompile_function(self, address: str) -> str:
        return self._get("decompile_function", {"address": address})

    def load_program(self, file_path: str) -> str:
        """Headless server only: load a binary for analysis."""
        return self._post("load_program", {"file": file_path})

    def run_analysis(self) -> str:
        return self._post("run_analysis")

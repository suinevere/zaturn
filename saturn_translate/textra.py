"""Thin client for the Textra Japanese→English translation API.

This mirrors the auth/translate flow used by hokupod/textra-ja-to-en-mcp so the
Saturn server can translate directly when run standalone, while still letting
users route through the textra MCP server if they prefer. Credentials come from
the TEXTRA_API_KEY / TEXTRA_API_SECRET / TEXTRA_USER_NAME environment variables.

Textra uses OAuth2 client-credentials to mint a bearer token, then a form-POST
to the general JA→EN engine. If credentials are absent, :func:`translate` raises
so callers can fall back to manual translation.
"""

from __future__ import annotations

import os

import httpx

TOKEN_URL = os.environ.get(
    "TEXTRA_TOKEN_URL",
    "https://mt-auto-minhon-mlt.ucri.jgn-x.jp/oauth2/token.php",
)
API_URL = os.environ.get(
    "TEXTRA_JA_EN_API_URL",
    "https://mt-auto-minhon-mlt.ucri.jgn-x.jp/api/mt/generalNT_ja_en/",
)


class TextraError(RuntimeError):
    pass


def credentials() -> tuple[str, str, str]:
    key = os.environ.get("TEXTRA_API_KEY", "")
    secret = os.environ.get("TEXTRA_API_SECRET", "")
    name = os.environ.get("TEXTRA_USER_NAME", "")
    if not (key and secret and name):
        raise TextraError(
            "Textra credentials not set. Export TEXTRA_API_KEY, "
            "TEXTRA_API_SECRET and TEXTRA_USER_NAME, or translate via the "
            "textra-ja-to-en-mcp server instead."
        )
    return key, secret, name


def _get_token(client: httpx.Client, key: str, secret: str) -> str:
    resp = client.post(
        TOKEN_URL,
        data={
            "grant_type": "client_credentials",
            "client_id": key,
            "client_secret": secret,
        },
    )
    resp.raise_for_status()
    token = resp.json().get("access_token")
    if not token:
        raise TextraError("Textra token request returned no access_token")
    return token


def translate_one(text: str, *, timeout: float = 30.0) -> str:
    """Translate a single Japanese string to English via Textra."""
    if not text.strip():
        return ""
    key, secret, name = credentials()
    with httpx.Client(timeout=timeout) as client:
        token = _get_token(client, key, secret)
        resp = client.post(
            API_URL,
            data={
                "access_token": token,
                "key": key,
                "name": name,
                "type": "json",
                "text": text,
            },
        )
        resp.raise_for_status()
        payload = resp.json()
        try:
            return payload["resultset"]["result"]["text"]
        except (KeyError, TypeError) as exc:
            raise TextraError(f"Unexpected Textra response: {payload}") from exc


def translate_many(texts: list[str], *, timeout: float = 30.0) -> list[str]:
    """Translate a list of strings. Reuses one token/connection for the batch."""
    if not texts:
        return []
    key, secret, name = credentials()
    out: list[str] = []
    with httpx.Client(timeout=timeout) as client:
        token = _get_token(client, key, secret)
        for text in texts:
            if not text.strip():
                out.append("")
                continue
            resp = client.post(
                API_URL,
                data={
                    "access_token": token,
                    "key": key,
                    "name": name,
                    "type": "json",
                    "text": text,
                },
            )
            resp.raise_for_status()
            payload = resp.json()
            try:
                out.append(payload["resultset"]["result"]["text"])
            except (KeyError, TypeError) as exc:
                raise TextraError(f"Unexpected Textra response: {payload}") from exc
    return out

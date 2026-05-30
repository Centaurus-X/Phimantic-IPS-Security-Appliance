"""JWT-Auth-Helper + Password-Hashing.

Wir bewusst KEINE Decorators - statt @login_required nutzen wir
die Funktion ``require_auth(role=...)`` direkt am Anfang jeder Route.
"""

import base64
import hashlib
import hmac
import json
import os
import secrets
import time
from datetime import datetime, timedelta, timezone
from functools import partial

from flask import g, request

from db.connection import get_connection

_PBKDF2_ITERATIONS = 200000
_PBKDF2_ALGO = "sha256"


# =========================================================================
# Password Hashing (PBKDF2 - bewusst ohne externe Abhaengigkeit)
# =========================================================================

def hash_password(password):
    """Erzeugt einen kompakten Hash-String im Format
    ``pbkdf2$<iter>$<salt_b64>$<hash_b64>``.
    """
    salt = os.urandom(16)
    dk = hashlib.pbkdf2_hmac(_PBKDF2_ALGO, password.encode("utf-8"),
                             salt, _PBKDF2_ITERATIONS)
    return "pbkdf2${0}${1}${2}".format(
        _PBKDF2_ITERATIONS,
        base64.b64encode(salt).decode("ascii"),
        base64.b64encode(dk).decode("ascii"),
    )


def verify_password(password, stored):
    """Timing-safe Vergleich."""
    if not stored or not isinstance(stored, str):
        return False
    parts = stored.split("$")
    if len(parts) != 4 or parts[0] != "pbkdf2":
        return False
    try:
        iterations = int(parts[1])
        salt = base64.b64decode(parts[2])
        expected = base64.b64decode(parts[3])
    except (ValueError, TypeError):
        return False
    dk = hashlib.pbkdf2_hmac(_PBKDF2_ALGO, password.encode("utf-8"),
                             salt, iterations)
    return hmac.compare_digest(dk, expected)


# =========================================================================
# JWT - minimaler eigener Implementer ohne externe lib
# =========================================================================

def _b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def _b64url_dec(text):
    padding = "=" * (-len(text) % 4)
    return base64.urlsafe_b64decode(text + padding)


def issue_jwt(secret, user_id, role, ttl_minutes, jti=None):
    """Stellt ein JWT (HS256) aus.
    Rueckgabe: (token_string, jti, exp_iso)
    """
    now = int(time.time())
    exp = now + int(ttl_minutes) * 60
    jti_val = jti or secrets.token_hex(12)

    header = {"alg": "HS256", "typ": "JWT"}
    payload = {
        "sub": int(user_id),
        "role": role,
        "iat": now,
        "exp": exp,
        "jti": jti_val,
    }

    header_b64 = _b64url(json.dumps(header, separators=(",", ":")).encode("utf-8"))
    payload_b64 = _b64url(json.dumps(payload, separators=(",", ":")).encode("utf-8"))
    signing_input = "{0}.{1}".format(header_b64, payload_b64).encode("ascii")
    sig = hmac.new(secret.encode("utf-8"), signing_input, hashlib.sha256).digest()
    sig_b64 = _b64url(sig)
    token = "{0}.{1}".format(signing_input.decode("ascii"), sig_b64)

    exp_iso = datetime.fromtimestamp(exp, tz=timezone.utc).isoformat()
    return token, jti_val, exp_iso


def verify_jwt(secret, token):
    """Validiert das JWT, liefert das Payload-Dict oder None."""
    if not token or token.count(".") != 2:
        return None
    header_b64, payload_b64, sig_b64 = token.split(".")
    signing_input = "{0}.{1}".format(header_b64, payload_b64).encode("ascii")
    expected = hmac.new(secret.encode("utf-8"), signing_input,
                        hashlib.sha256).digest()
    try:
        received = _b64url_dec(sig_b64)
        if not hmac.compare_digest(expected, received):
            return None
        payload = json.loads(_b64url_dec(payload_b64).decode("utf-8"))
    except (ValueError, TypeError, json.JSONDecodeError):
        return None

    if payload.get("exp", 0) < int(time.time()):
        return None

    return payload


# =========================================================================
# Request-Guard (kein Decorator - explizit aufrufen)
# =========================================================================

def require_auth(cfg, allowed_roles=None):
    """Pruefe Auth-Cookie. Bei Fehler: Tuple (False, error_response_tuple).
    Bei Erfolg: (True, user_dict).
    Verwendung:
        ok, ctx = require_auth(cfg, allowed_roles=("admin",))
        if not ok:
            return ctx
        user = ctx
    """
    secret = cfg["auth"]["secret_key"]
    cookie_name = cfg["auth"]["cookie_name"]
    token = request.cookies.get(cookie_name)
    if not token:
        # Fallback: Authorization-Header (z.B. fuer curl-Tests)
        bearer = request.headers.get("Authorization", "")
        if bearer.startswith("Bearer "):
            token = bearer[7:].strip()

    payload = verify_jwt(secret, token)
    if payload is None:
        return False, ({"error": "unauthenticated"}, 401)

    # Server-Side Session check (Revoke-Faehigkeit)
    conn = get_connection()
    try:
        row = conn.execute(
            "SELECT s.revoked, u.id, u.username, u.role, u.full_name, u.active "
            "FROM sessions s JOIN users u ON u.id = s.user_id "
            "WHERE s.id = ?",
            (payload.get("jti"),),
        ).fetchone()
    finally:
        conn.close()

    if row is None or row["revoked"] or not row["active"]:
        return False, ({"error": "session_invalid"}, 401)

    user = {
        "id": row["id"],
        "username": row["username"],
        "role": row["role"],
        "full_name": row["full_name"],
    }

    if allowed_roles is not None and user["role"] not in allowed_roles:
        return False, ({"error": "forbidden",
                        "message": "Insufficient role"}, 403)

    g.current_user = user
    return True, user


require_admin = partial(require_auth, allowed_roles=("admin",))
require_analyst = partial(require_auth, allowed_roles=("admin", "analyst"))

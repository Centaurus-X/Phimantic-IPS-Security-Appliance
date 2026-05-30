#!/usr/bin/env bash
set -Eeuo pipefail
FORCE="${1:-}"
LEGACY_CONTAINERS="phimantec-suricata-node phimantec-proxy"
log(){ printf '[cleanup-stale] %s\n' "$*"; }
warn(){ printf '[cleanup-stale] WARN: %s\n' "$*" >&2; }
command -v docker >/dev/null 2>&1 || { log "docker nicht gefunden; nichts zu bereinigen"; exit 0; }
for name in $LEGACY_CONTAINERS; do
  docker container inspect "$name" >/dev/null 2>&1 || continue
  status="$(docker inspect -f '{{.State.Status}}' "$name" 2>/dev/null || true)"
  image="$(docker inspect -f '{{.Config.Image}}' "$name" 2>/dev/null || true)"
  if [[ "$status" == running && "$FORCE" != --force ]]; then
    warn "laufender Legacy-Container bleibt erhalten: ${name}, image=${image:-unknown}. Bewusst entfernen: scripts/cleanup_stale_docker.sh --force"
    continue
  fi
  if [[ "$image" == phimantec/* || "$FORCE" == --force ]]; then
    docker rm -f "$name" >/dev/null 2>&1 || true
    log "entfernt: ${name}"
  else
    warn "skip ${name}: image '${image}' ist nicht phimantec/*; mit --force bewusst entfernen"
  fi
done

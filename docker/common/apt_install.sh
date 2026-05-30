#!/usr/bin/env bash
set -Eeuo pipefail

export DEBIAN_FRONTEND=noninteractive

APT_RETRIES="${PHIMANTEC_APT_RETRIES:-0}"
APT_TIMEOUT="${PHIMANTEC_APT_TIMEOUT:-12}"
APT_FORCE_IPV4="${PHIMANTEC_APT_FORCE_IPV4:-1}"
APT_MIRROR="${PHIMANTEC_APT_MIRROR:-}"
APT_SECURITY_MIRROR="${PHIMANTEC_APT_SECURITY_MIRROR:-${APT_MIRROR}}"

log() {
  printf '[phimantec-apt] %s\n' "$*" >&2
}

fail() {
  printf '[phimantec-apt] ERROR: %s\n' "$*" >&2
  exit 70
}

rewrite_deb822_uri() {
  local file="$1"
  local from="$2"
  local to="$3"
  if [[ -n "$to" && -f "$file" ]]; then
    sed -i -E "s#${from}#${to%/}#g" "$file"
  fi
}

write_apt_config() {
  local force_ipv4="false"
  if [[ "$APT_FORCE_IPV4" == "1" || "$APT_FORCE_IPV4" == "true" || "$APT_FORCE_IPV4" == "yes" ]]; then
    force_ipv4="true"
  fi
  cat >/etc/apt/apt.conf.d/99phimantec-fast-fail <<APTCONF
Acquire::Retries "${APT_RETRIES}";
Acquire::http::Timeout "${APT_TIMEOUT}";
Acquire::https::Timeout "${APT_TIMEOUT}";
Acquire::ForceIPv4 "${force_ipv4}";
APT::Install-Recommends "false";
APT::Install-Suggests "false";
Dpkg::Use-Pty "false";
APTCONF
}

rewrite_sources() {
  local file
  for file in /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
    [[ -e "$file" ]] || continue
    rewrite_deb822_uri "$file" 'http://archive\.ubuntu\.com/ubuntu/?' "$APT_MIRROR"
    rewrite_deb822_uri "$file" 'http://security\.ubuntu\.com/ubuntu/?' "$APT_SECURITY_MIRROR"
  done
}

main() {
  if [[ $# -lt 1 ]]; then
    fail "no packages requested"
  fi

  write_apt_config
  rewrite_sources

  log "apt-get update, timeout=${APT_TIMEOUT}s, retries=${APT_RETRIES}, force_ipv4=${APT_FORCE_IPV4}"
  apt-get update || fail "apt-get update failed; pruefe Docker-DNS, Proxy oder PHIMANTEC_DOCKER_BUILD_NETWORK=host/default"

  local first_pkg="$1"
  if ! apt-cache show "$first_pkg" >/dev/null 2>&1; then
    fail "APT index is not usable, package '${first_pkg}' not found; apt update likely had only warnings"
  fi

  log "install: $*"
  apt-get install -y --no-install-recommends "$@" || fail "apt-get install failed"
  rm -rf /var/lib/apt/lists/*
}

main "$@"

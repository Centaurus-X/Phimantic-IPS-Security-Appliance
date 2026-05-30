#!/usr/bin/env bash
set -Eeuo pipefail

PROXY_URL="${PROXY_URL:-http://127.0.0.1:3128}"
OUT_DIR="${OUT_DIR:-/tmp/phimantec-eicar-url-test}"
EICAR_MARKER="EICAR-STANDARD-ANTIVIRUS-TEST-FILE"
mkdir -p "$OUT_DIR"

URLS=(
  "https://secure.eicar.org/eicar.com.txt"
  "https://secure.eicar.org/eicar_com.zip"
  "https://meineipadresse.de/testvirus/eicar.com"
  "https://meineipadresse.de/testvirus/eicar2.zip"
  "https://meineipadresse.de/testvirus/eicar3.zip"
  "https://meineipadresse.de/testvirus/eicar4.zip"
  "https://meineipadresse.de/testvirus/eicar.tar"
  "https://meineipadresse.de/testvirus/eicar.com.gz"
  "https://meineipadresse.de/testvirus/eicar.cab"
)

append_cache_buster() {
  local url="$1"
  local stamp
  stamp="$(date +%s%N)"
  case "$url" in
    *\?*) printf '%s&phim_cb=%s\n' "$url" "$stamp" ;;
    *) printf '%s?phim_cb=%s\n' "$url" "$stamp" ;;
  esac
}

body_contains_eicar() {
  local body_file="$1"
  local extracted_file="$2"

  grep -a -q "$EICAR_MARKER" "$body_file" && return 0

  if command -v gzip >/dev/null 2>&1; then
    if gzip -dc "$body_file" >"$extracted_file.gzip" 2>/dev/null; then
      grep -a -q "$EICAR_MARKER" "$extracted_file.gzip" && return 0
    fi
  fi

  if command -v unzip >/dev/null 2>&1; then
    if unzip -p "$body_file" >"$extracted_file.zip" 2>/dev/null; then
      grep -a -q "$EICAR_MARKER" "$extracted_file.zip" && return 0
    fi
  fi

  if command -v tar >/dev/null 2>&1; then
    rm -rf "$extracted_file.tar.d"
    mkdir -p "$extracted_file.tar.d"
    if tar -xf "$body_file" -C "$extracted_file.tar.d" 2>/dev/null; then
      grep -a -R -q "$EICAR_MARKER" "$extracted_file.tar.d" && return 0
    fi
  fi

  return 1
}

printf '\nPHIMANTEC EICAR URL Matrix Test\n'
printf 'Proxy:  %s\n' "$PROXY_URL"
printf 'Output: %s\n\n' "$OUT_DIR"

fail_count=0
pass_count=0
idx=0

for base_url in "${URLS[@]}"; do
  idx=$((idx + 1))
  url="$(append_cache_buster "$base_url")"
  body_file="$OUT_DIR/body_${idx}.bin"
  header_file="$OUT_DIR/header_${idx}.txt"
  extracted_prefix="$OUT_DIR/extracted_${idx}"

  printf '[%02d] %s\n' "$idx" "$base_url"

  http_code="$(
    curl \
      --silent \
      --show-error \
      --location \
      --insecure \
      --proxy "$PROXY_URL" \
      --connect-timeout 10 \
      --max-time 60 \
      --header 'Connection: close' \
      --header 'Cache-Control: no-store' \
      --dump-header "$header_file" \
      --output "$body_file" \
      --write-out '%{http_code}' \
      "$url" || printf 'curl_error'
  )"

  if body_contains_eicar "$body_file" "$extracted_prefix"; then
    printf '     RESULT: FAIL - EICAR marker leaked to client body/archive\n'
    printf '     HTTP:   %s\n' "$http_code"
    fail_count=$((fail_count + 1))
    continue
  fi

  if grep -a -qiE 'Zugriff blockiert|Access blocked|PHIMANTEC|suricata-alert|Inspection unavailable|blocked' "$body_file"; then
    printf '     RESULT: PASS - block/unavailable response, no EICAR leak\n'
    printf '     HTTP:   %s\n' "$http_code"
    pass_count=$((pass_count + 1))
    continue
  fi

  if [[ "$http_code" == 403 || "$http_code" == 503 || "$http_code" == 000 || "$http_code" == curl_error ]]; then
    printf '     RESULT: PASS - non-forwarding status, no EICAR leak\n'
    printf '     HTTP:   %s\n' "$http_code"
    pass_count=$((pass_count + 1))
    continue
  fi

  printf '     RESULT: FAIL - no block evidence and no EICAR marker check matched\n'
  printf '     HTTP:   %s\n' "$http_code"
  printf '     BODY:   %s\n' "$body_file"
  fail_count=$((fail_count + 1))
done

printf '\nSummary\n'
printf 'PASS: %d\n' "$pass_count"
printf 'FAIL: %d\n' "$fail_count"

printf '\nRecent Suricata verdict logs:\n'
if command -v docker >/dev/null 2>&1 && [[ -x scripts/phimantec_compose.sh ]]; then
  scripts/phimantec_compose.sh compose logs --no-color suricata-node 2>/dev/null \
    | grep -E 'verdict flow=.*action=(block|allow|pending|error)' \
    | tail -40 || true
fi

if [[ "$fail_count" -ne 0 ]]; then
  exit 1
fi

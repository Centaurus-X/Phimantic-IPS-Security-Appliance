#!/usr/bin/env bash
set -Eeuo pipefail

export SYSTEMD_PAGER=cat
export SYSTEMD_COLORS=0

printf '\n[PHIMANTEC-DIAG] Host/IP\n'
hostname -f 2>/dev/null || hostname
ip -br addr || true

printf '\n[PHIMANTEC-DIAG] Projektdateien\n'
ls -lah /home/philo | grep -E 'phimantec-c11-appliance|phimantec-bump-ca' || true
test -f /home/philo/phimantec-c11-appliance/CMakeLists.txt && printf 'Projektordner: OK\n' || printf 'Projektordner: FEHLT\n'

printf '\n[PHIMANTEC-DIAG] Binaries\n'
ls -lah /usr/local/bin/phimantec-proxyd 2>/dev/null || true
/usr/local/bin/phimantec-proxyd -h 2>/dev/null || true

printf '\n[PHIMANTEC-DIAG] Config/CA\n'
sudo ls -lah /etc/phimantec /etc/phimantec/ca 2>/dev/null || true
sudo sed -n '1,160p' /etc/phimantec/phimantec-proxyd.conf 2>/dev/null || true

printf '\n[PHIMANTEC-DIAG] Services\n'
sudo systemctl --no-pager --full status phimantec-proxyd phimantec-test-upstream 2>/dev/null || true

printf '\n[PHIMANTEC-DIAG] Journal\n'
sudo journalctl -u phimantec-proxyd -u phimantec-test-upstream --no-pager -n 120 2>/dev/null || true

printf '\n[PHIMANTEC-DIAG] Listener\n'
sudo ss -ltnp | grep -E ':(3128|8443)\b' || true

printf '\n[PHIMANTEC-DIAG] Suricata Node Reachability\n'
nc -vz 192.168.0.236 18080 || true
nc -vz 192.168.0.236 18090 || true

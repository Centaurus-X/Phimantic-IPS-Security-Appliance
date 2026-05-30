# Kompakte Test-Commands v0.3.3

## Lokaler Build ohne Docker

```bash
scripts/verify_release.sh
```

## Nur schneller C-Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

## Docker Build + Start

```bash
cp .env.example .env
docker compose build --no-cache
docker compose up -d
docker compose ps
```

## Docker Smoke

```bash
curl -fsS http://127.0.0.1:5050/ >/dev/null && echo monitor_ok
curl -x http://127.0.0.1:3128 -I http://example.org/ || true
docker compose logs --no-color --tail=120 suricata-node
docker compose logs --no-color --tail=120 proxy
```

## Docker DNS-Fix, falls apt im Build scheitert

```bash
printf '%s\n' 'PHIMANTEC_DOCKER_BUILD_NETWORK=host' >> .env
docker compose build --no-cache
```

Falls `host` im Build nicht erlaubt ist:

```bash
sed -i 's/^PHIMANTEC_DOCKER_BUILD_NETWORK=.*/PHIMANTEC_DOCKER_BUILD_NETWORK=default/' .env
sudo mkdir -p /etc/docker
printf '{"dns":["1.1.1.1","8.8.8.8"]}\n' | sudo tee /etc/docker/daemon.json
sudo systemctl restart docker
docker compose build --no-cache
```

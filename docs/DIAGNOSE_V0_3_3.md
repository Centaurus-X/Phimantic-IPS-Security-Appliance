# PHIMANTEC C11 Appliance v0.3.3 — Diagnose und Patchbericht

## Kurzdiagnose

Der v0.3.2-Build war im gezeigten Output bereits erfolgreich. Beide Images wurden gebaut. Der Fehler entstand erst bei `docker compose up -d` durch einen bereits reservierten festen Container-Namen:

```text
Conflict. The container name "/phimantec-suricata-node" is already in use
```

Docker reserviert Namen auch fuer gestoppte Container. Deshalb kann `docker ps` leer sein, waehrend `docker compose up` trotzdem scheitert. Das ist kein C-Kern-, Suricata- oder SuricataMonitor-Bug, sondern ein Compose-Orchestrierungsproblem.

## Lokalisierte Ursachen

```text
1. feste container_name Werte in v0.3.2
2. gestoppte Altcontainer koennen globale Docker-Namen blockieren
3. dadurch startet der Stack nicht vollstaendig
4. anschliessende curl-Fehler auf 5050/33128 sind Folgefehler
5. Port 3128 kann weiterhin durch alte Proxy-Prozesse belegt sein
6. Docker-Bridge-DNS bleibt hostseitig ein separates Build-Netzwerkproblem
```

## Patches in v0.3.3

```text
- container_name vollstaendig entfernt
- Compose-Projektname ueber PHIMANTEC_COMPOSE_PROJECT_NAME steuerbar
- Proxy-Port defaultet auf 127.0.0.1:33128
- Monitor, Lane und FlowBus binden default nur auf 127.0.0.1
- Healthchecks fuer suricata-node und proxy ergaenzt
- proxy wartet per depends_on auf einen healthy suricata-node
- scripts/phimantec_compose.sh als root-sicherer Compose-Wrapper ergaenzt
- scripts/preflight_docker.sh prueft Release-Kontext, Docker, Ports und Altcontainer
- scripts/cleanup_stale_docker.sh entfernt alte feste PHIMANTEC-Containernamen explizit
- scripts/rollout_docker.sh bietet einen endlichen Init/Down/Preflight/Build/Up/Smoke Ablauf
- verify_release.sh bricht bei falschem Release-Kontext oder falscher .env-Version ab
- verify_docker_static.sh verbietet feste container_name Werte
```

## Keine automatische Endlosreparatur

v0.3.3 startet nicht in einer Selbstreparatur-Endlosschleife. Fehler wie Portkonflikte, falsche Versionen oder alte Container werden im Preflight gemeldet. Entfernen alter Container passiert nur explizit:

```bash
scripts/phimantec_compose.sh clean-stale
scripts/phimantec_compose.sh clean-stale --force
```

## Smoke-Test-Korrektur

Der optionale externe Proxy-Smoke wertet `405 CONNECT Required` nicht als Erfolg. Der regulaere Smoke bleibt lokal und prueft Monitor-HTTP sowie Proxy-TCP, damit keine fremde Altinstanz auf 3128 faelschlich als Erfolg gilt.

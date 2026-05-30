# PHIMANTEC v0.3.18 Diagnose — Ruleset-Reload und Webmanagement

## Kurzbefund

v0.3.17 läuft als Security-Gate stabil: EICAR wird blockiert, normale Seiten laden deutlich besser, und Suricata bleibt die einzige fachliche Entscheidungsinstanz. Das verbleibende Webmanagement-Problem lag nicht im Proxy-Enforcement, sondern im Ruleset-Control-Pfad.

Der Button `reload-rules` erzeugte im UI `Fehler: incomplete response`. Gleichzeitig wirkte das Deaktivieren einer Regel so, als ob Suricata die UI-Änderung ignoriert. Tatsächlich wurde die Regeldatei geändert, aber die laufende Suricata Detection Engine blieb ohne erfolgreichen Live-Reload auf dem alten Regelstand.

## Ursachen

```text
1. Der Reload-Pfad nutzte den falschen bzw. zu grob behandelten Reload-Befehl.
2. Der Socket-Timeout war für blockierende Ruleset-Reloads zu kurz.
3. Suricata wurde mit -S auf eine Einzeldatei gestartet; YAML rule-files wurden dadurch nicht sauber genutzt.
4. Der YAML-Patcher überschrieben rule-files statt ET/Open, PHIMANTEC local und external/*.rules zu mergen.
5. Die UI zeigte Datei-Zustand, aber nicht den Runtime-Drift bis zum erfolgreichen Reload.
```

## Implementierter Fix

```text
suricata -T vor jedem Live-Reload
ruleset-reload-rules als bevorzugter Reload-Pfad
ruleset-reload-nonblocking und SIGUSR2 als Fallback
langer Reload-Timeout für große ET/Open-Regelwerke
kein Reload bei fehlerhafter Konfiguration
Suricata-Start ohne -S-Override
persistenter Rule-Pfad /var/lib/suricata/rules
rule-files Merge statt Overwrite
UI-Status: Reload erforderlich / aktiv geladen / Fehlerstage
```

## Security-Invarianten

```text
Suricata entscheidet.
inspectord korreliert EVE/Flow-ID.
FlowBus transportiert das finale Verdict.
Proxy enforced ausschließlich das finale Suricata-/inspectord-Verdict.
Kein finales Suricata-Allow bedeutet kein Forwarding.
Timeout, unavailable oder Reload-Fehler werden nicht zu Allow.
```

## Erwartete Wirkung

```text
Webmanagement Ruleset Reload funktioniert ohne Container-Neustart.
Kein unklarer incomplete-response-Fehler mehr im normalen Reload-Pfad.
UI-Regeländerungen werden erst nach erfolgreichem Reload als runtime-aktiv betrachtet.
ET/Open, PHIMANTEC local und external/custom-local.rules können gemeinsam geladen werden.
Fehlerhafte Regeln werden durch suricata -T erkannt und nicht live geladen.
```

# PHIMANTEC v0.3.18 — Ruleset Reload und ET/Open Integration

## Diagnose

Der Webmanagement-Fehler `Fehler: incomplete response` wurde nicht als Proxy- oder Security-Chain-Defekt behandelt, sondern als Ruleset-Control-Problem. Die laufende Suricata Detection Engine blieb aktiv, waehrend die UI nur die Regeldatei geaendert hatte. Dadurch konnte eine deaktivierte Regel bis zum erfolgreichen Live-Reload weiterhin blockieren.

## Fix

- `reload_rules()` fuehrt vor jedem Live-Reload `suricata -T -c /etc/suricata/suricata.yaml -v` aus.
- Bei fehlerhafter Konfiguration wird kein Reload ausgeloest; die alte Engine bleibt aktiv.
- Der Reload nutzt `suricatasc -c ruleset-reload-rules` und danach Socket-/SIGUSR2-Fallback.
- `ruleset-reload-rules` bekommt ein langes Reload-Timeout, damit ET/Open-Regeln nicht als `incomplete response` abbrechen.
- Suricata startet ohne `-S`, damit `rule-files` aus der YAML geladen werden.
- `default-rule-path` ist `/var/lib/suricata/rules`.
- `rule-files` werden gemerged: `suricata.rules`, `phimantec-local.rules`, `external/custom-local.rules` plus vorhandene Eintraege.
- Die aktive Rules-Directory ist persistent und wird im UI verwaltet.

## Security

Die Proxy/Security-Chain bleibt unveraendert: Suricata entscheidet, inspectord korreliert, FlowBus transportiert das Verdict, der Proxy enforced. Kein finales Suricata-Allow bedeutet weiterhin kein Forwarding.

## Runtime-Test

```bash
scripts/phimantec_smoke.sh
PROXY_URL=http://127.0.0.1:3128 scripts/eicar_url_matrix_test.sh

# im Web UI: SID 900004 deaktivieren, ruleset-reload-rules klicken
# danach SID 900004 wieder aktivieren, erneut reloaden
```

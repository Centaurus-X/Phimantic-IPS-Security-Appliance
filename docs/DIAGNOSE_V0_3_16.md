# Diagnose v0.3.17 — Amazon/YouTube unavailable, TLS-Transienten und FlowBus-Hot-Path

## Kurzdiagnose

v0.3.14a beseitigt das tote Rennen zwischen inspectord-Decision und Proxy-FlowBus-Read. Normale Seiten laden dadurch wieder besser und EICAR bleibt blockiert. Bei Amazon, YouTube und anderen CDN-lastigen Seiten bleiben aber zwei technische Ursachen uebrig:

```text
1. TLS-Record-Splits:
   SSL_read()/SSL_write() konnte SSL_ERROR_WANT_READ/WANT_WRITE als harten Fehler propagieren.
   Folge: sporadisches Inspection unavailable, abgebrochene Subresources, CORS status null.

2. Serieller Response-Inspection-Hot-Path:
   Jede 64-KB-Response-Probe erzeugte Lane-POST + FlowBus-Verdict + no-alert grace.
   Bei vielen Subresources oder grossen Responses entstand ein kuenstlicher Wasserfall.
```

Die bereitgestellten Zielhost-Logs bestaetigen die Ausgangslage: Suricata 8.0.5, Lane und FlowBus laufen, der EICAR-Test bleibt 9/9 PASS, aber vorherige Runs zeigten HTTP 503 im Smoke und viele Allow-Verdicts mit voller Timeout-Latenz bzw. spaeter noch lange Ladezeiten bei Google/CDN-Subrequests.

## Was v0.3.17 korrigiert

### 1. TLS-Transienten sind keine finalen Fehler mehr

Der TLS-Layer unterscheidet jetzt sauber:

```text
SSL_ERROR_WANT_READ / SSL_ERROR_WANT_WRITE -> bounded retry
EINTR                                      -> bounded retry
EAGAIN / EWOULDBLOCK via SSL_ERROR_SYSCALL -> echtes Socket-Timeout, fail-closed nach oben
SSL_ERROR_ZERO_RETURN                       -> EOF
andere Fehler                               -> Fehler
```

Damit bleiben die oberen Deadlines wirksam. Der Patch erzeugt keinen Endlos-Read und keinen stillen Allow.

### 2. FlowBus wird metadata-only

Der Body-Sample muss nicht zweimal transportiert werden. Die Security-relevante Kopie geht weiter an die Lane und wird dort von Suricata gesehen. FlowBus fragt nur das finale Verdict fuer die Flow-ID ab:

```text
Proxy -> Lane:    X-Phim-Flow-Id + entschluesselter Body-Sample
Suricata -> EVE:  Alert oder HTTP-/Lane-Evidence zur Flow-ID
Proxy -> FlowBus: Flow-ID + Metadaten, kein zweiter Body-Transport
inspectord:       korreliert EVE und antwortet allow/block/pending/error
```

Suricata bleibt dadurch die einzige fachliche Entscheidungsinstanz. Der Proxy bekommt keine lokale Malware-Logik.

### 3. Parallelitaet ohne Keepalive-Risiko

FlowBus-Keepalive bleibt deaktiviert. Die stabile Verbindungseinheit bleibt:

```text
connect -> request verdict -> read full response -> close
```

Der Proxy nutzt aber vier parallele FlowBus-Client-Slots. Dadurch blockieren parallele Browser-Subrequests nicht mehr alle auf einem einzigen Mutex. Da jeder Slot frische Verbindungen nutzt, wird der alte Keepalive-/stale-FD-Fehler nicht wieder eingefuehrt.

### 4. Response-Segmentfenster groesser

Das Default-Segmentfenster steigt von 64 KiB auf 512 KiB:

```text
PHIMANTEC_SAMPLE_TARGET_BYTES=524288
PHIMANTEC_SAMPLE_MAX_BYTES=1048576
PHIMANTEC_STREAM_CHUNK_MIN_BYTES=524288
```

Das reduziert die Anzahl serieller Suricata-Verdicts pro grosser Response deutlich. Cross-Segment-Overlap bleibt aktiv und das Suricata-Body-Limit bleibt bei 1 MiB pro Lane-POST.

### 5. Lane-Fehler werden sofort fail-closed

Vorher wurde ein Lane-Send-Fehler ignoriert und erst spaeter als pending/no-eve oder FlowBus unavailable sichtbar. v0.3.17 bricht in diesem Fall sofort fail-closed ab und loggt:

```text
lane send failed; failing closed
```

Das ist keine Security-Absenkung; es entfernt nur unnoetige Wartezeit und macht die Diagnose eindeutig.

## Sicherheitsbewertung

Nicht geaendert:

```text
kein fail-open
kein Domain-/YouTube-/Amazon-/CDN-Bypass
kein lokaler EICAR-/Malware-Fallback im Proxy
kein ungeprueftes Forwarding bei Timeout oder unavailable
H1.1-only bleibt aktiv
Alt-Svc/Upgrade/HTTP2-Settings werden weiter entfernt
EICAR wird weiter durch Suricata-Alerts blockiert
```

Geaendert wurde nur der Transport- und Scheduling-Hot-Path rund um TLS, Lane und FlowBus. Die Entscheidung bleibt bei Suricata.

## Erwartete Wirkung

```text
- weniger sporadisches Inspection unavailable bei CDN-/Amazon-/YouTube-Subresources
- deutlich weniger FlowBus- und JSON-Overhead pro Response-Segment
- weniger Wasserfall-Latenz durch parallele FlowBus-Client-Slots
- keine Rueckkehr des Keepalive-Stale-FD-Problems
- EICAR-Matrix bleibt PASS
```

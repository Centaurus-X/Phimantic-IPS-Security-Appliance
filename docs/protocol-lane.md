# Suricata Lane Protocol

Die Lane ist absichtlich Klartext-HTTP, damit Suricata den Traffic ohne eigene TLS-Terminierung analysieren kann.

## Sicherheitsregel

Entschluesselter Traffic wird nur ueber diese Lane an Suricata IDS/IPS gespiegelt. Es gibt keine zweite Analyse-Route und keinen Domain-/CDN-Bypass.

## Transport

Seit `v0.1.9-lab` nutzt der Proxy einen kleinen persistenten Lane-Client-Pool. Der Lane-Sink akzeptiert mehrere sequenzielle HTTP/1.1-POSTs ueber dieselbe TCP-Verbindung.

```text
Default: [lane] pool_size = 4
Cap:     8
```

Der Client wartet weiterhin pro POST auf eine `204 No Content` Antwort. Es gibt kein HTTP-Pipelining.

## Request Envelope

```http
POST /phimantec/lane/request HTTP/1.1
Host: phimantec-lane.local
X-Phim-Flow-Id: <flow-id>
X-Phim-Direction: request
X-Phim-Phase: headers
X-Phim-Client-IP: <client-ip>
X-Phim-Client-Port: <client-port>
X-Phim-Server-SNI: <sni>
X-Phim-Server-Port: 443
X-Phim-ALPN: http/1.1
X-Phim-HTTP-Version: h1
X-Phim-Method: GET
X-Phim-Path: /
Content-Type: application/octet-stream
Content-Length: N
Connection: keep-alive

<body-sample>
```

## Response Envelope

```http
POST /phimantec/lane/response HTTP/1.1
Host: phimantec-lane.local
X-Phim-Flow-Id: <flow-id>
X-Phim-Direction: response
X-Phim-Phase: body
X-Phim-Client-IP: <client-ip>
X-Phim-Client-Port: <client-port>
X-Phim-Server-SNI: <sni>
X-Phim-Server-Port: 443
X-Phim-ALPN: http/1.1
X-Phim-HTTP-Version: h1
X-Phim-Method: GET
X-Phim-Path: /
Content-Type: application/octet-stream
Content-Length: N
Connection: keep-alive

<body-sample>
```

## Sink-Antwort

```http
HTTP/1.1 204 No Content
Content-Length: 0
Connection: keep-alive
```

Bei `Connection: close` im Request antwortet der Sink ebenfalls mit `Connection: close` und beendet die Verbindung danach sauber.

`X-Phim-*` Header duerfen niemals in Richtung echter Upstream-Server oder Client weitergeleitet werden.

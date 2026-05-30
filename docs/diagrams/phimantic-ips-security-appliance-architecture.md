# Phimantic-IPS-Security-Appliance Architecture Diagrams

These diagrams describe the public repository architecture. They intentionally focus on the stable security chain rather than on historical version numbers.

## Security Chain

```mermaid
flowchart LR
    B[Browser / Client] -->|HTTP proxy CONNECT| P[Proxy container
TLS bump + HTTP/1.1 only]
    P -->|decrypted request / response samples| L[Plain HTTP Lane
127.0.0.1:18080]
    L --> S[Suricata 8
Rules + HTTP app-layer]
    S -->|EVE alert / flow metadata| I[inspectord]
    P -->|FlowBus HMAC verdict request
127.0.0.1:18090| I
    I -->|final allow / block / unavailable| P
    P -->|allow only after final no-alert| O[Origin server]
    P -->|block page 403 on Suricata alert| B
```

## Runtime Verdict Sequence

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant P as Proxy
    participant L as Lane
    participant S as Suricata
    participant I as inspectord / FlowBus
    participant O as Origin

    C->>P: CONNECT host:443
    P-->>C: 200 Connection Established
    P->>P: TLS bump, HTTP/1.1 only
    C->>P: HTTP request over bumped TLS
    P->>L: Plain HTTP sample with Flow-ID
    L->>S: Local packet / app-layer visibility
    S-->>I: EVE alert if rule matches
    P->>I: Verdict request for Flow-ID
    alt Suricata alert
        I-->>P: block
        P-->>C: 403 block page
    else No alert after stability grace
        I-->>P: allow
        P->>O: Forward request
        O-->>P: Response
        P-->>C: Response after strict inspection gate
    else timeout, pending or unavailable
        I-->>P: unavailable / no final allow
        P-->>C: 503 Inspection Unavailable
    end
```

## Ruleset Reload

```mermaid
flowchart LR
    U[Web management UI] --> F[Write rule file]
    F --> R[Mark reload required]
    R --> B[Backup active YAML and rules]
    B --> T[suricata -T validation]
    T -->|OK| SC[ruleset-reload-rules]
    SC -->|OK| A[Runtime rules loaded]
    T -->|NOK| N[No reload; old engine remains active]
    SC -->|NOK| E[Show stage and details]
```

## Supplied legacy planning diagrams

The original architecture bundle supplied with the project is also preserved for reference:

- [Provided lab architecture SVG](provided-lab-architecture.svg)
- [Provided inline bridge vision SVG](provided-inline-bridge-vision.svg)
- [Provided Mermaid architecture notes](provided-mermaid-architecture.md)

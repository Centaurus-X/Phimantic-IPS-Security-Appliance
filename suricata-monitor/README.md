# SuricataMonitor

SuricataMonitor is the web management interface bundled with Phimantic-IPS-Security-Appliance. It provides dashboard, event, ruleset, settings and runtime reload pages for the appliance environment.

The UI is based on and adapted from the `suriguard` UI template by SEc-123: <https://github.com/SEc-123>.

## Main capabilities

- Dashboard with server status, log summary, event summary and recent events.
- Event management based on Suricata EVE output.
- Ruleset management for local and external Suricata-compatible rule files.
- Runtime-safe rules reload with validation before activation.
- Settings UI for the Suricata monitor profile.

## Development quick start

```bash
bash scripts/run_dev.sh
```

Default lab endpoint:

```text
http://<host-ip>:5050/
```

Default lab credentials:

```text
admin / changeme
```

Change the default password before exposing the UI outside a trusted lab network.

## Production installation

```bash
sudo bash scripts/install.sh
sudo systemctl enable --now suricata-monitor
```

## Documentation

- [API](docs/API.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Development](docs/DEVELOPMENT.md)
- [Debugging](docs/DEBUGGING.md)
- [PHIMANTEC integration](docs/INTEGRATION_PHIMANTEC.md)

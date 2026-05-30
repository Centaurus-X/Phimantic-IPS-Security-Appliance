# offline-debs/

Dieses Verzeichnis ist die nachvollziehbare Rebuild-Stufe des Offline-Bundles.
Hier liegen `.deb` Pakete, die aus einem Online- oder lokalen APT-Mirror
vorbereitet wurden.

Zweck:

```text
offline-debs/
  amd64/
    *.deb
    SHA256SUMS
    packages.requested.txt
```

Dieser Ordner ist groesser als `runtime-libs/`, aber kleiner und auditierbarer
als ein voller Builder-Container. Er ist fuer Air-Gap-/Developer-Rebuilds
vorgesehen, nicht fuer das normale schlanke Admin-Bundle.

Befuellen auf einem Online-Host oder gegen einen lokalen APT-Mirror:

```bash
scripts/collect_offline_debs.sh
```

Die `.deb` Pakete werden bewusst nicht vorab in dieses Repository gelegt, damit
das Standard-Repository klein bleibt.

# Pull Request

## Summary

<!-- What does this PR change, and why? -->

## Checklist

- [ ] No unintended product changes: the diff contains only the files this PR is meant to change.
- [ ] Tests executed: `scripts/verify_release.sh` (or the relevant subset) passes locally and/or CI is green.
- [ ] Documentation checked and updated where affected.
- [ ] No secrets, private keys, certificates, or internal identifiers are included.
- [ ] Version and release impact checked (release guard `phimantec-c11-appliance-v0.3.18-monitor-offline-vendor`, `BUILD_INFO.txt`, manifests and checksums remain consistent; existing tags and release artifacts untouched).
- [ ] License and third-party changes checked (`third_party/`, `offline-debs/`, `runtime-libs/` policies respected).

## Notes for the reviewer

<!-- Anything the reviewer should pay special attention to. -->

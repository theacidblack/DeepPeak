# Code signing status

**Status: application submitted to SignPath Foundation on 2026-11-12, awaiting review.**

DeepPeak releases are currently **unsigned**, so Windows SmartScreen may show
"Windows protected your PC" on first run. That is a trust prompt, not a malware
detection. Until signing is approved, the mitigations are: public source,
published SHA256 for every installer, and no network access at all.

## Why SignPath Foundation

It is the only free Authenticode option for open-source projects. The
certificate is issued to the SignPath Foundation (it shows as the publisher),
and it carries OV-level reputation, so SmartScreen warnings fade as downloads
accumulate. It does not grant instant trust the way an EV certificate would.

## Eligibility checklist

| Requirement | Project status |
|---|---|
| OSI-approved license | MIT, see `LICENSE` |
| Public repository with history | `github.com/theacidblack/DeepPeak` |
| Artifacts built by CI, not uploaded by hand | pending, see below |
| No bundled proprietary binaries | only own code and TTS-generated audio |
| Maintainer identity verified | part of the SignPath process |
| Real, non-malicious software | tray utility, no network, no telemetry |

## When approval lands

1. Add `.github/workflows/release.yml`: build on tag with MSVC + NSIS on
   `windows-latest`, upload unsigned artifacts.
2. Install the SignPath GitHub App and connect the project in their dashboard.
3. Replace the artifact upload with a signing request
   (`signpath/github-action-submit-signing-request`) using origin verification,
   so only this workflow can request signatures.
4. Sign both `DeepPeak.exe` and `DeepPeak-<version>-setup.exe`, with an RFC 3161
   timestamp (`http://timestamp.digicert.com`).
5. Attach the signed installer to the GitHub release and publish its SHA256.

## Not free, documented for comparison

| Option | Cost | SmartScreen effect |
|---|---|---|
| EV certificate | ~$250-500/yr | immediate trust |
| OV certificate | ~$150-300/yr | fades in with downloads |
| Certum Open Source cert | from €49/yr + ID check | OV, your name as publisher |
| Azure Trusted Signing | $9.99/mo | OV, requires a company with history |
| Microsoft Store / MSIX | $19 one-time | no prompt at all |
| Self-signed certificate | free | none for end users |
| Sigstore / GitHub attestations | free | provenance only, not OS trust |

## Verifying an unsigned build

```powershell
Get-FileHash .\DeepPeak-<version>-setup.exe -Algorithm SHA256
```

Compare against the hash in the release notes.

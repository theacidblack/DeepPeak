# Code signing status

**Application submitted to SignPath Foundation on 2026-11-12. Acknowledged by
Phillip Deng (SignPath), review in progress.** The reply said the team would come
back within a few business days with next steps or follow-up questions.

Releases published today are **unsigned**, so Windows SmartScreen may show
"Windows protected your PC" on first run. That is a trust prompt, not a malware
detection. Until signing is live the mitigations are: public source, published
SHA256 for every installer, and no network access at all.

## What is already built

`.github/workflows/build.yml` is the only supported way to produce a release:

1. runs on `windows-latest`, builds with MSVC from a clean checkout
2. runs `DeepPeak.exe --selftest` (three years of minutes against an independent
   implementation of the rule) and fails the build if it does not pass
3. uploads `build\DeepPeak.exe`, has it signed, and rebuilds the NSIS installer
   around the **signed** app, so the exe inside the installer is signed too
4. uploads `dist\DeepPeak-<version>-setup.exe`, has that signed as well
5. records status and signer certificate from `Get-AuthenticodeSignature`
6. on a `v*` tag, publishes the release with the installer and `SHA256.txt`

Without `SIGNPATH_API_TOKEN` the same workflow still builds an unsigned
installer, so the pipeline is testable before approval.

`tools/test-live.ps1` (the two-minute tick test) stays a local test: it reads the
tray tooltip, and GitHub runners have no Explorer shell or notification area.

## What is left, and it is only this

After SignPath approves, in the SignPath web UI:

1. Install the **SignPath GitHub App** on `theacidblack/DeepPeak` (only a human
   account can authorise this).
2. Create the project, signing policy and artifact configurations, then note:
   - organization ID
   - project slug
   - signing policy slug
   - artifact configuration slug for a single PE file (the app)
   - artifact configuration slug for a single PE file (the installer)
3. Create an API token for a user with submitter rights on that policy.

Then in GitHub, repository **Settings > Secrets and variables > Actions**:

| Kind | Name | Value |
|---|---|---|
| Secret | `SIGNPATH_API_TOKEN` | API token from step 3 |
| Secret | `SIGNPATH_ORGANIZATION_ID` | organization ID |
| Variable | `SIGNPATH_PROJECT_SLUG` | project slug |
| Variable | `SIGNPATH_SIGNING_POLICY_SLUG` | signing policy slug |
| Variable | `SIGNPATH_APP_ARTIFACT_CONFIG` | artifact config slug, app |
| Variable | `SIGNPATH_INSTALLER_ARTIFACT_CONFIG` | artifact config slug, installer |

No code change is needed after that. Push a tag and the workflow signs, hashes
and publishes:

```powershell
git tag v1.0.1
git push origin v1.0.1
```

## Limits worth stating

- The certificate identifies **SignPath Foundation**, not `theacidblack`.
- It is OV-level: SmartScreen warnings fade as downloads accumulate, they do not
  stop on day one. Only an EV certificate stops them immediately.
- Signing does not change the app, only the trust prompt.

## Not free, for comparison

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

Compare against the hash in the release notes. Once signing is live the same
command plus `Get-AuthenticodeSignature` both matter.

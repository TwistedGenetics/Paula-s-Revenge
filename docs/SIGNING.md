# Signing and notarising the builds

Without these, the Mac build is ad-hoc signed (macOS asks you to clear the quarantine, see
INSTALL.txt) and the Windows build is unsigned (SmartScreen says "Windows protected your PC").
With them, GitHub Actions signs (and on the Mac notarises) every build automatically. Nothing
else changes: add the secrets and the next push does it.

Add them in the repo on GitHub: **Settings → Secrets and variables → Actions → New repository secret**.

## Mac (Apple Developer Program, $99 a year)

1. In your Apple Developer account, make a **Developer ID Application** certificate
   (Certificates, Identifiers & Profiles → Certificates → +). Install it in Keychain Access.
2. In Keychain Access, right-click the certificate (with its private key) → **Export** → a `.p12`
   file with a password.
3. Make an **app-specific password** for your Apple ID at appleid.apple.com (Sign-In and Security).
4. Add these secrets:

| Secret | What |
|---|---|
| `MACOS_CERT_P12` | the `.p12` file as base64: `base64 -i cert.p12 \| pbcopy`, then paste |
| `MACOS_CERT_PASSWORD` | the `.p12`'s password |
| `MACOS_SIGN_IDENTITY` | the certificate's name, e.g. `Developer ID Application: Jamie Hanslip (ABCDE12345)` |
| `APPLE_ID` | your Apple ID email |
| `APPLE_TEAM_ID` | your 10-character Team ID (Membership page) |
| `APPLE_APP_PASSWORD` | the app-specific password |

The app is signed with the hardened runtime and `Packaging/Entitlements.plist` (audio input, and
loading other developers' VST3 / AU plug-ins), sent to Apple's notary service, and stapled.
With only the first three secrets it's signed but not notarised.

## Windows (a code-signing certificate from a CA)

1. Buy an OV or EV code-signing certificate (Sectigo, DigiCert, SSL.com…). EV certificates on a
   hardware token or cloud HSM can't be exported as a file: they need the CA's own signing tool
   in the workflow instead of the step here, which signs with a `.pfx` file.
2. Add these secrets:

| Secret | What |
|---|---|
| `WINDOWS_CERT_PFX` | the `.pfx` file as base64: `base64 -w0 cert.pfx` (or `certutil -encode`) |
| `WINDOWS_CERT_PASSWORD` | the `.pfx`'s password |

SmartScreen still warns about a newly signed app until the certificate builds up a reputation
(an EV certificate skips that).

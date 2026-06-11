# 🔒 Security Policy

## Supported Versions

We patch security issues on the latest release. Always run the newest firmware to
make sure you have every fix.

| Version | Supported          |
| ------- | ------------------ |
| Latest  | :white_check_mark: |
| < Latest| :x:                |

## 🛡️ Threat Model — Read This First

PrintOrb is designed for a **trusted local network**. By design:

- The **web portal is unauthenticated** — anyone who can reach the device's IP can
  read status and change settings. Keep the orb on a LAN/VLAN you trust.
- **`/api/update` accepts a firmware upload.** It is guarded *only* when you set an
  **OTA / Update password** (Settings → Security). With no password, OTA is
  **disabled** as a secure default (the endpoint returns 401). Set a password if you
  use OTA — an open update endpoint is effectively remote code execution on the
  device.
- The **Bambu LAN broker uses a self-signed certificate**, so the client connects
  with `setInsecure()`. This is standard for Bambu LAN mode but means the MQTT/TLS
  link is encrypted, not authenticated against a CA.
- During **first-time setup** the device hosts an **open WiFi access point**
  (`printorb-setup-xxxx`). It is only active until WiFi is configured, then it shuts
  off.

These are intentional trade-offs for a hobby LAN device, not oversights — but please
deploy accordingly.

## 🚨 Reporting a Vulnerability

Found something? Thanks for helping disclose it responsibly!

### Please DO NOT
- Open a public GitHub issue for security vulnerabilities
- Discuss the issue publicly before it's fixed

### Please DO
**Report privately via one of these:**

1. **GitHub Security Advisories (preferred)** —
   [Report a vulnerability](https://github.com/Disane87/printorb/security/advisories/new)
2. **Email** — `mfranke87@icloud.com` with `SECURITY` in the subject line

### What to include
- A clear description of the vulnerability
- The impact — what could an attacker actually do?
- Steps to reproduce (firmware version, printer type, board revision)
- Any suggested fix, if you have one

We'll acknowledge your report, keep you posted on the fix, and credit you if you'd
like. 🙏

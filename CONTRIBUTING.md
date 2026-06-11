# 🤝 Contributing to PrintOrb

Hey there! 👋 Thanks for considering a contribution! Whether you're fixing a typo,
squashing a bug, wiring up a new printer backend or polishing the UI — every bit
helps. 🎉

A few things to keep the ride smooth:

## 🌟 New to Contributing? Start Here!

First time contributing to open source? Welcome! 🎉 Look for issues tagged
[`good first issue`](https://github.com/Disane87/printorb/labels/good%20first%20issue) —
they're well-scoped and a great way to get your feet wet. Comment on the issue to
let others know you're on it, then follow the workflow below.

Need help? Don't be shy —
[open a discussion or an issue](https://github.com/Disane87/printorb/issues) and
let's chat! 💬

## 🛠️ Dev Setup

PrintOrb is **ESP32-S3 firmware** built with [PlatformIO](https://platformio.org/)
(CLI or the VS Code extension). No Node, no Python, no devcontainer — just PlatformIO
and a USB-C cable.

```bash
pio run                 # compile
pio run -t upload       # flash (board connected via USB-C)
pio device monitor      # serial output @ 115200 baud
```

> 💡 On this board the serial monitor must own the COM port, so **close the monitor
> before `pio run -t upload`**. Serial only shows up after flashing.

The build environment is `esp32-s3-touch-lcd-128` (no dots allowed in env names).
The web UI is embedded in flash (`src/web_index.h`) — there's **no** LittleFS upload
step.

## ✅ Before You Open a PR

- 🧱 **It must still build.** Anything touching `src/` or `include/` has to pass
  `pio run` clean. The current baseline sits at ≈44 % RAM and ≈44 % of the 3 MB app
  partition — keep an eye on that headroom.
- 🇬🇧 **English everywhere** — comments, identifiers, docs *and* on-screen strings.
- ⚙️ **No hardcoded config.** IPs, SSIDs, serials and access codes live only in
  `OrbConfig`/NVS, never in source.
- 🧩 **One responsibility per file.** New printer backend? Implement the
  `PrinterClient` interface and instantiate it in `createPrinter()` — the shared
  shape is `PrinterStatus`.
- 📦 **ArduinoJson v7 gotcha:** never `auto x = doc["a"]["b"]` (MemberProxy is
  non-copyable). Use `JsonObject o = doc["a"]["b"].to<JsonObject>();`. Prefer
  streaming/filtered parsing to keep RAM low.

There's more architecture detail in [`CLAUDE.md`](CLAUDE.md) and the design notes
under [`docs/`](docs/).

## 💾 Commit Messages — Conventional Commits

PrintOrb releases are fully automated by
**[semantic-release](https://semantic-release.gitbook.io/)**, driven by
**[Conventional Commits](https://www.conventionalcommits.org/)**. The prefix you
choose decides the next version:

| Prefix | Example | Release |
|--------|---------|---------|
| `fix:` | `fix: correct AMS slot color mapping` | patch (`x.y.Z`) |
| `feat:` | `feat: add scheduled display dimming` | minor (`x.Y.0`) |
| `feat!:` / `BREAKING CHANGE:` footer | `feat!: drop legacy config keys` | major (`X.0.0`) |
| `chore:` / `docs:` / `refactor:` / `ci:` … | `docs: update wiring notes` | no release |

So please phrase commits as `type: summary` — the changelog and GitHub Release take
care of themselves on every push to `main`.

## 🔀 Pull Request Flow

1. 🍴 Fork the repo and create a branch (`feat/my-cool-thing` or `fix/that-bug`).
2. 🔨 Make your change; keep commits conventional.
3. ✅ Run `pio run` and, if you have the hardware, smoke-test on the board.
4. 📤 Open a PR against `main` with a clear description of *what* and *why*.

That's it — thanks for making PrintOrb better! 🙌

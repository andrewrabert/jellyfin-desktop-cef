---
name: investigate-jellyfin-repos
description: Use when researching features in jellyfin-web (the Jellyfin server's web UI rendered in CEF) or jellyfin-desktop (old Qt app) for reference during implementation
---

# Investigating Related Jellyfin Repositories

## Overview

Two external repos provide context for this rewrite:
- **jellyfin-web**: Web UI served by Jellyfin server, rendered in CEF layer
- **jellyfin-desktop**: Old Qt-based desktop app (reference for feature parity)

## Repo Locations

Repos live in `third_party/` (gitignored):
- `third_party/jellyfin-web`
- `third_party/jellyfin-desktop`

Clone if not present:

```sh
git clone --recurse-submodules https://github.com/jellyfin/jellyfin-web.git third_party/jellyfin-web
git clone --recurse-submodules https://github.com/jellyfin/jellyfin-desktop.git third_party/jellyfin-desktop
```

## jellyfin-web

The web UI that runs inside CEF. Key areas:

| Path | Purpose |
|------|---------|
| `src/controllers/` | Page controllers (playback, settings, etc) |
| `src/components/` | Reusable UI components |
| `src/plugins/` | Plugin architecture |
| `src/scripts/` | Core JS logic |

When investigating web UI behavior, search this repo.

## jellyfin-desktop (Qt)

Old desktop app. Useful for:
- Native integration patterns (MPRIS, system tray, shortcuts)
- Feature reference when implementing parity

| Path | Purpose |
|------|---------|
| `src/player/` | Qt media player integration |
| `src/settings/` | Settings management |
| `native/` | Platform-specific code |

## Workflow

1. Clone repos if not present
2. Use Explore agent or grep to find relevant code
3. Compare implementations between old Qt app and web UI
4. Apply learnings to this CEF+mpv rewrite

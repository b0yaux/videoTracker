# External Integrations

**Analysis Date:** 2026-01-28

## APIs & External Services

**Media Download:**
- YouTube (via `yt-dlp`) - Used to import videos/audio from URLs.
  - SDK/Client: `yt-dlp` (external executable)
  - Auth: None (public URLs)
  - Implementation: `src/core/CommandExecutor.cpp`

## Data Storage

**Databases:**
- None detected.

**File Storage:**
- Local filesystem only.
  - Project metadata: `.project.json`
  - Sessions: `*.json` (e.g., `session.json`)
  - Asset indexing: `.assetindex.json`
  - Module layouts: `module_layouts.json`
  - Recent sessions: `videoTracker_recent_sessions.json` (in user home)

**Caching:**
- None (Temporary downloads stored in `data/temp_downloads`).

## Authentication & Identity

**Auth Provider:**
- Custom / None. Application is a local desktop tool with no user account system.

## Monitoring & Observability

**Error Tracking:**
- None.

**Logs:**
- Standard OpenFrameworks logging (`ofLog`).
- Console UI: `src/gui/Console.cpp` displays application logs and command output.

## CI/CD & Deployment

**Hosting:**
- Desktop Application (Local).

**CI Pipeline:**
- None detected.

## Environment Configuration

**Required env vars:**
- `PATH` - Must include `yt-dlp` for URL import functionality.

**Secrets location:**
- None.

## Webhooks & Callbacks

**Incoming:**
- None.

**Outgoing:**
- None.

---

*Integration audit: 2026-01-28*

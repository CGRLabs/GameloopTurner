# 13 — User-facing explanations (tooltip / help text source)

Source text for in-app tooltips, help markers, and the About-page glossary.
Written in plain English for non-technical users. Each entry is short enough to
fit in an ImGui tooltip (wrap at ~35 chars per line).

## Dashboard

- **Drifted badge:** "GameLoop reverted a setting (usually after an app update).
  Re-apply the recommended fix from the Keymaps page."
- **VM Sync:** "How much RAM and CPU cores GameLoop's virtual machine gets.
  Set on the Performance page."
- **Drive headroom:** "GameLoop needs at least 10 GB free for updates and VHD
  growth. Below that, updates fail silently."
- **Service state:** "The background service that starts GameLoop on boot. Set to
  Manual if you don't want it auto-starting."
- **CODM Live Settings:** "Live values read from Configs.db. Clarity = render
  resolution (tier 2 recommended). Low-latency = smaller frame queue. Frame-gen =
  fake frames (keep OFF)."

## Keymaps page

- **Auto-switch:** "GameLoop watches your screen and swaps your entire key layout
  when it recognises the lobby or skill-select box. Your configured keys vanish
  mid-match."
- **Pin:** "Locks a fixed-key layout as the active one so the engine has a stable
  starting point."
- **Flatten:** "Replaces smart layouts (which change keys by scene) with fixed
  keys. Every landing becomes a fixed layout."
- **Blind:** "Removes the image templates the engine uses to detect scenes.
  Without templates, it can't recognise when to switch."
- **Quarantine:** "Moves the engine's local template pack aside and locks smk.conf
  read-only - the real kill-switch. The engine can't overwrite a read-only file."
- **Full chain:** "Does all of the above in one apply. Recommended."
- **Tex / smk.conf:** "Image fingerprints GameLoop matches against your screen.
  Tex 10004 = lobby mailbox (triggers DMZ switch). Tex 10046 = skill-select box
  (triggers BR switch)."
- **Official vs user blobs:** "Official layouts are trigger-guarded by GameLoop
  (can't be edited). User blobs are writable - the tuner only touches those."
- **Drifted (keymap):** "GameLoop reverted a keymap setting. Re-apply the
  recommended fix."

## Performance page

- **VM cores/RAM:** "How much of your PC's CPU and RAM GameLoop's virtual
  machine gets. Leave 1 core and 2 GB for Windows."
- **Clarity tier:** "Render resolution. Tier 2 = clearer textures AND higher FPS
  here. Tier 1 was actually LAGGIER (shifts work to the CPU). Tier 3 = highest
  GPU cost, slower than tier 2 on this engine."
- **Low-latency:** "Smaller frame queue = input sampled closer to frame present.
  Best feel-per-cost. Keep ON."
- **Frame-gen:** "Fake interpolated frames inflate the FPS number but add input
  lag. Real 120 beats fake 240. Keep OFF."
- **RenderMode (display only):** "Engine picks DirectX/OpenGL/Vulkan per map. Not
  writable - forcing it can crash shaders."
- **softvideo:** "CPU decode of flat video clips (lobby/killcam) only - not 3D
  rendering. Leave ON."
- **Host config frame rates:** "default_frame_rate / maximum_frame_rate. 120 is
  the competitive pick; 240 allowed but rarely real."
- **WheelScroll bezier:** "Scroll input is bezier-smoothed with anti-jitter. If
  wheel-aim feels laggy, this is why."

## VM & Priority page

- **Priorities:** "Emulator+VM -> High, UI -> High, Assistant -> Above Normal,
  Download Server -> Below Normal. Applies to running processes only - resets
  when GameLoop exits."
- **Process table:** "Live list of running GameLoop processes with PID and memory.
  Use this to confirm the game is running before applying priorities."

## Backups page

- **SQLite online backup:** "Backs up the database while GameLoop is running.
  Copying the file would produce a torn database (WAL can exceed the main file)."
- **Restore:** "Overwrites the current Configs.db or smk.conf with the backup.
  GameLoop must be closed first."

## Network & Servers page

- **Guest connections:** "Connections made by CODM inside the emulator (read via
  ADB). Shows remote IP, port, protocol, state, role, and ping."
- **Game server:** "The server your current match is running on. Usually Tencent
  Cloud (US West Coast). High ping here is usually physics (distance), not a local
  setting."
- **Matchmaking/API:** "The endpoint that finds matches, logs you in, and syncs
  stats (HTTPS/443)."
- **Telemetry:** "Google SDK sending anonymous usage/crash data. Safe to block for
  privacy, but may break some features."
- **Guest DNS:** "The DNS resolver the emulator uses. 114.114.114.114 is
  unreachable from Algeria (100% packet loss). Changed to 1.1.1.1/8.8.8.8."
- **Flush DNS:** "Clears the DNS cache. Sometimes 'refreshing net' was just
  re-resolving DNS."
- **Bufferbloat:** "When your ping spikes while someone else uses the internet.
  Fix with router SQM/QoS."
- **Wi-Fi banner:** "You're on Wi-Fi - wired Ethernet gives more stable latency
  and lower jitter."

## About page / Diagnostics

- **Diagnostic report:** "A scrubbed (secrets removed), size-capped (200 KB) text
  file covering paths, processes, registry, keymaps, triggers, smk.conf integrity,
  disk, and backups. Safe to share."
- **Drift:** "GameLoop reverted a setting (usually after an update). Re-apply the
  recommended fix."

## General glossary

- **ADB:** "Android Debug Bridge - a tool to communicate with the emulator's
  Android OS. Used to read network connections."
- **ano_tmp:** "A folder in GameLoop's install dir where the engine keeps its
  local template pack (ob_*.zip). Quarantining it starves the runtime restore."
- **Configs.db:** "GameLoop's master settings database (SQLite). Contains keymaps,
  CODM settings, identity, and smart-key templates."
- **smk.conf:** "Smart-key config file with image templates the engine matches
  against your screen. Locking it read-only is the durable kill-switch."
- **SwitchOperation:** "A rule that changes a key's function by screen state
  (e.g., 'in vehicle' -> different keys). 0 rules = flat = immune to auto-switch."
- **Tex template:** "An image fingerprint GameLoop matches against your screen to
  detect scenes (lobby, skill-select, etc.)."
- **WAL:** "Write-Ahead Log - SQLite's journal. Can be larger than the main DB
  file. Copying the file while running produces a torn database."
- **UAC:** "User Account Control - the Windows prompt for admin rights. GameLoop
  recreates the emulator as LocalSystem, so the tuner needs admin to terminate
  those processes."
- **Bufferbloat:** "When your ping spikes while someone else uses the internet."
- **UAC:** "User Account Control - Windows prompt for admin rights."

## In-code locations

- `widgets::help_marker(desc)` - call after a label to show a (?) tooltip on hover.
- `widgets::tooltip(desc)` - call after any widget to attach a hover tooltip.
- `fixes::Def::risk` - per-fix risk note, appended to the `why` text in fix rows.
- `page_about.cpp` - glossary card (already added).
- `page_dashboard.cpp` - help markers on "GameLoop Engine Status" and "CODM Live
  Settings".
- `page_performance.cpp` - help marker on "Render Clarity Tier".
- `page_network.cpp` - help marker on page header.

## Suggested next steps for the tooltip system (not yet built)

1. Add `?` icons next to each section header using `widgets::help_marker()`.
2. Expand the glossary card on the About page (already added, can grow).
3. Add a first-run wizard overlay (3 steps, dismissable, never shown again).
4. Add `risk` strings to all remaining fix entries (currently only FullChain and
   Blind have them).
5. Add "Show raw" toggles to remaining cards (Backups, Logs, About).
6. Add contextual help links (each page gets a "?" button opening the relevant
   docs section).
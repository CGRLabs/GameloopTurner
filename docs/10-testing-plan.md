# 10 — Testing plan: ping & latency experiments

For measuring what actually helps your connection while playing CODM on GameLoop.
Companion to `docs/09-knowledge-map.md` (§4 network section). Fill in the results
table as you go; one row per match.

## 0. Rules of the test

- **Change ONE thing at a time.** Otherwise you can't attribute the result.
- **3 matches per condition** minimum. Ping varies match-to-match; one match is noise.
- **Same time of day** if possible (peak hours vs off-peak changes server load).
- **Record the server region** CODM matched you to (shown in lobby/match) — a region
  change invalidates the comparison.
- **Don't combine with GameLoop updates / reboots mid-test** unless the test requires it.
- Revert after each failed test before starting the next.

## 1. How to measure

### In-game (primary)
CODM shows ping in the match HUD. Note it at 3 points per match: early / mid / late.
Record the **median** of the three.

### Network tools (secondary, run before/after each match)
```powershell
# 1. Get the game server IP while in a match:
netstat -ano | findstr :443

# 2. Path to that server (loss + per-hop latency):
pathping <server_ip>

# 3. DNS resolution time:
Measure-Command { Resolve-DnsName activision.com } | Select-Object TotalMilliseconds

# 4. Gateway + public baseline:
ping -n 20 192.168.1.1
ping -n 20 1.1.1.1

# 5. Jitter-ish: ping with small interval
ping -n 50 -l 32 <server_ip>
```

### What to record per match

| Field | Example |
|---|---|
| Date/time | 2026-10-19 21:30 |
| Test ID | T3 |
| Condition | DNS override ON |
| Server region | EU / ME / Asia |
| In-game ping (early/mid/late) | 45 / 52 / 48 |
| Median ping | 48 |
| Packet loss % | 0.5 |
| Jitter (max-min) | 7 |
| pathping loss to server | 0% |
| DNS resolve ms | 12 |
| Notes | felt smoother, no spikes |

---

## 2. Baseline (T0) — do this first

**Change:** none. Play 3 matches as-is, current settings.

Record everything in §1 table. This is your reference point; every later test is
compared against T0.

Also capture once:
```powershell
ipconfig /all | Out-File "$env:TEMP\net-baseline.txt"
Get-NetAdapter | Where-Object Status -eq Up | Out-File "$env:TEMP\net-baseline.txt" -Append
```

---

## 3. Test matrix (ordered, least → most invasive)

Run in this order. Revert each before the next. Mark result: **Helped / No change / Hurt**.

### T1 — DNS override (lowest risk, highest likely impact)
- **Change:** Windows adapter DNS → `1.1.1.1` / `8.8.8.8` (or ISP). Then `ipconfig /flushdns`.
- **Why:** androws.conf hardcodes Chinese resolvers (114/119); from your geo (36°N/3°E) these are likely slow.
- **Measure:** 3 matches.
- **Revert:** adapter DNS → Automatic (or back to 114.114.114.114 / 119.29.29.29), `ipconfig /flushdns`.

### T2 — DNS flush only (isolate the "refresh" effect)
- **Change:** `ipconfig /flushdns` only, keep current DNS.
- **Why:** confirms whether your "refresh net" was just re-resolving DNS.
- **Measure:** 3 matches.
- **Revert:** none needed.

### T3 — Adapter toggle (light refresh, no reboot)
- **Change:** disable then re-enable the NIC (or airplane-mode toggle).
- **Why:** re-initialises the adapter without a reboot.
- **Measure:** 3 matches.
- **Revert:** none needed (state is restored).

### T4 — ARP cache clear
- **Change:** `arp -d *` (elevated).
- **Why:** clears stale LAN neighbour entries.
- **Measure:** 3 matches.
- **Revert:** none needed.

### T5 — Accelerator toggle (if available)
- **Change:** enable/disable `XyAcc`/`LeigodAcc` in GameLoop settings (or block in firewall).
- **Why:** proxies can help or hurt depending on node.
- **Note:** binaries aren't currently installed locally (verified) — only testable if GameLoop downloads them when you enable the feature.
- **Measure:** 3 matches each way.
- **Revert:** restore firewall rules / disable feature.

### T6 — VPN / TUN isolation
- **Change:** fully uninstall any VPN, "game booster", or TUN adapter (WireGuard/Clash/v2ray). Reboot once.
- **Why:** leftover TUN adapters can capture GameLoop traffic even when "disabled".
- **Measure:** 3 matches.
- **Revert:** reinstall the VPN if needed.

### T7 — QoS packet scheduler
- **Change:** `gpedit.msc → QoS Packet Scheduler → Limit reservable bandwidth → 0`. Reboot.
- **Measure:** 3 matches.
- **Revert:** set back to default (20) or remove the policy.

### T8 — Windows Game Mode
- **Change:** toggle Game Mode ON/OFF (Settings → Gaming → Game Mode).
- **Why:** can reduce background load or starve the emulator on weak CPUs.
- **Measure:** 3 matches each way.
- **Revert:** toggle back.

### T9 — Fullscreen optimization
- **Change:** GameLoop.exe → Properties → Compatibility → "Disable fullscreen optimizations" (on/off).
- **Why:** frame-pacing vs input lag tradeoff.
- **Measure:** 3 matches.
- **Revert:** toggle back.

### T10 — NIC tuning (moderate)
- **Change (one at a time, separate test IDs):**
  - T10a: disable "Interrupt Moderation" in NIC advanced properties.
  - T10b: disable "Large Send Offload (LSO)".
  - T10c: disable NIC power management ("Allow computer to turn off this device").
- **Measure:** 3 matches each.
- **Revert:** re-enable each after testing.

### T11 — Wired vs Wi-Fi (if applicable)
- **Change:** switch from Wi-Fi to Ethernet (or 5 GHz vs 2.4 GHz).
- **Measure:** 3 matches each.
- **Revert:** switch back.

### T12 — In-game server region
- **Change:** force nearest region in CODM settings (if selectable).
- **Measure:** 3 matches.
- **Note:** a region change invalidates comparison with earlier tests — restart the matrix from T0 for that region.

### T13 — Heavy reset (last resort)
- **Change:** `ipconfig /flushdns; ipconfig /release; ipconfig /renew; netsh winsock reset; netsh int ip reset; netsh advfirewall reset` (elevated, reboot).
- **Why:** only if T1–T12 don't help. Wipes the stack; re-add GameLoop firewall Allow rules after.
- **Measure:** 3 matches.
- **Revert:** N/A (this IS the revert point for stack corruption).

---

## 4. Results log (fill in as you go)

| Test | Date | Region | Median ping | Loss % | Jitter | Verdict | Notes |
|---|---|---|---|---|---|---|---|
| T0 | | | | | | baseline | |
| T1 | | | | | | | |
| T2 | | | | | | | |
| T3 | | | | | | | |
| T4 | | | | | | | |
| T5 | | | | | | | |
| T6 | | | | | | | |
| T7 | | | | | | | |
| T8 | | | | | | | |
| T9 | | | | | | | |
| T10a | | | | | | | |
| T10b | | | | | | | |
| T10c | | | | | | | |
| T11 | | | | | | | |
| T12 | | | | | | | |
| T13 | | | | | | | |

## 5. What "helped" looks like

- Median ping down by ≥5 ms **and** jitter down **and** no new packet loss.
- Subjectively: fewer rubber-banding spikes, smoother aim, no "high ping until refresh" stalls.
- If a test helps, **keep it** and move to the next; if not, **revert** before the next test.

## 6. Safety / revert cheat sheet

```powershell
# Revert DNS
# Settings -> Network -> adapter -> DNS -> Automatic, then:
ipconfig /flushdns

# Revert firewall block (if you blocked XyAcc/LeigodAcc)
Remove-NetFirewallRule -DisplayName "Block XyAcc"
Remove-NetFirewallRule -DisplayName "Block LeigodAcc"

# Revert GameLoop firewall rules after advfirewall reset
# Re-run GameLoop once; it re-creates its Allow rules on launch.

# Re-enable NIC features (if T10 changed them)
# Device Manager -> adapter -> Advanced -> re-enable Interrupt Moderation / LSO

# Re-enable NIC power management
# Device Manager -> adapter -> Power Management -> check "Allow computer to turn off"
```

## 6b. Stability-focused tests (jitter & loss)

If raw ping is already acceptable (~40 ms) but aim feels inconsistent, the problem is
**jitter/loss**, not average latency. Run these instead of (or after) T1–T13.

### S1 — Wired vs Wi-Fi (highest impact if on Wi-Fi)
- **Change:** plug Ethernet into the router; disable Wi-Fi.
- **Measure:** 3 matches. Record jitter (max-min) + spike count + loss %.
- **Expected:** jitter 15–30 ms → <3 ms; loss → 0%.
- **Revert:** unplug, re-enable Wi-Fi.

### S2 — Bufferbloat test
- **Change:** none (diagnostic). While in a match, start a download (Steam/Epic/YouTube) and
  watch in-game ping + monitor CSV.
- **Measure:** ping before vs during load.
- **If ping jumps >2×:** bufferbloat. Fix = router SQM/QoS (see 09 §4.5) or limit background traffic.
- **Revert:** N/A (diagnostic only).

### S3 — Background hog isolation
- **Change:** close all non-essential apps (Chrome tabs, Discord overlay, OBS, cloud sync).
- **Measure:** 3 matches, compare jitter/spike count to baseline.
- **Revert:** reopen apps.

### S4 — GameLoop priority re-apply
- **Change:** VM & Priority page → Apply All (priorities reset on exit).
- **Measure:** 3 matches.
- **Revert:** N/A (resets on exit anyway).

### S5 — Power plan
- **Change:** `powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c` (High Performance).
- **Measure:** 3 matches.
- **Revert:** `powercfg /setactive 381b4222-f694-41f0-9685-ff5bb260df2e` (Balanced).

### S6 — NIC power management
- **Change:** Device Manager → adapter → Power Management → uncheck "Allow computer to turn off this device".
- **Measure:** 3 matches.
- **Revert:** re-check the box.

### Stability results log

| Test | Date | Median ping | Jitter (ms) | Loss % | Spikes (>2×) | Verdict | Notes |
|---|---|---|---|---|---|---|---|
| S0 (baseline) | | | | | | | |
| S1 | | | | | | | |
| S2 | | | | | | | |
| S3 | | | | | | | |
| S4 | | | | | | | |
| S5 | | | | | | | |
| S6 | | | | | | | |

## 7. When to stop testing

Stop and keep the winners when:
- You've found 1–2 changes that clearly help and stack (e.g. DNS override + adapter stays on).
- Further tests stop moving the needle (diminishing returns).
- A change makes things worse — revert immediately and move on.

Don't chase a single ms; chase **stability** (jitter + loss), which matters more than raw ping for CODM.

---

*Companion: `docs/09-knowledge-map.md` (§4 network). Live state verified 2026-10-18.*
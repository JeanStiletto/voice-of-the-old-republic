# KOTOR 2 swoop race — round-by-round test history (2026-09-14/15)

Moved out of docs/known-issues.md on 2026-09-15 when the race was declared playable by ear (56–100 s finishes, 46.41 s best; the remaining gap to the 40 s record is skill). The current engineering state lives in docs/llm-docs/swoop-k2-controls-and-jump.md section 9.

### KOTOR 2 swoop race — finishable since round 11 (2026-09-15), not yet winnable

WHERE TO RESUME: read `docs/llm-docs/swoop-k2-controls-and-jump.md` section
9, ending with "Round 12's 73.36 s race, dissected" — the per-incident time
cost, the two engine constants found (speed→world 0.665, predictable
landing point) and what was built on them. Best so far 73.36 s (round 12);
the track's posted times are 58 s to qualify and 40 s best; a hit-free run
computes to ~37 s, so the whole game is avoiding the two bar parks.
Round 16 (`patch-20260915-212249`: wreck / 46.41 / 80.56 / 82.55): the
46.41 s race qualified (the game's own line said 0:46:45 — our stamp
matches) with a single mine hit; the other three were relaunch cascades
(11, 6 and 8 hits) at walls and bars because the player did not know the
escape sequence. Built after round 16, UNTESTED, uncommitted: the parked
situation is classified by the obstacle sweep (`GetParkedBarSituation`:
wedged at a wall / bar with strip / must-jump) and the stuck hint speaks
the matching sequence after 1.5 s ("Links lenken, dann Leertaste, dann W" /
"Dem Ton folgen, dann W" / "Leertaste, dann W"; log: `stuck hint: ... [bar,
wall right]`), plus one centred jump beep at the park for a must-jump bar
(`parked beep:`); the finish time is spoken alone and 1.2 s delayed (`spoke
race time:`); the shift cue is the 0.2 s chirp on both games (`acc_shift.wav`).
Next round: check those three log lines, and whether the hint's 1.5 s is
early enough (the first relaunch usually came within ~2 s).

Round 15 (`patch-20260915-210405`: 69.97 / 85.67 / 79.95) ran the rebuild
below, now committed (122cd3a): finish mine never hit, MGC03/04 strips clean
in every race, strip lines mine-checked. Every race still lost one park at a
must-jump bar (MGC06: MGC05's jump lands 26–29 u short, as computed; MGC08:
jump pressed AT the beep, 40–60 u short) followed by a relaunch cascade at
x +19.9 (up to seven hits, 13 s). Cheapest next lever: a centred beep while
parked inside a bar's span meaning "jump first, then W" (from a standstill
the arc clears a bar 2 u ahead before W moves the bike into it) — or the
relaunch guard. Shift-ready cue changed after round 15 to the 0.2 s chirp
on both games (`acc_shift.wav`, uncommitted). Details in the swoop doc's
"Round 15" subsection.

Rounds 13 and 14 (`patch-20260915-162510`, `patch-20260915-203307`; best
64.08 s) tested the bar-aware guide and measured the mines: the free side
beside MGC03/06/08 is mined, mine collision is spherical with a combined
reach of 5.1–6.2 u (the live sphere_radius of 2.0 is NOT it), and with that
reach MGC06 has no path and is a must-jump that a jump from MGC05 clears
only in a ≤ 0.13 s press window. Rebuilt 2026-09-15 late, installed in both
games, UNTESTED, uncommitted: mine detour on the final line (strips
included), virtual straight-ahead target after the last pad (the finish
mine hit every race of rounds 12–14), measured reach in the strips (MGC03
→ x −12.5, MGC04 −5.9, MGC02 +5.3, MGC06/08 none), wall-loop chatter gate on
strips. Principle agreed with the player: nothing auto-steers unless there
is a path (no sub-3 u corridor ever becomes a target). See the swoop doc's
"Round 14" subsection. Next round: grep the newest K2 log for `bar strip:`
(expect gap=0 for MGC06 now), `detour: mine` lines carrying `line x=`, a
detour for the finish mine after pad 12, and `mine pass:` lines with hit=1
whose minD3 exceeds 6.2 (would raise the reach). Decisions still open with
the player: the mine beside MGC06 is only solvable DATA-side (move/remove it
in the module via the KotOR_IO area-edit path) — offered, not decided;
relaunch guard + K1-parity hit points; beep side semantics. The
round-by-round history below is evidence.


Round 1 (`patch-20260914-143533.log`): the bike never launched — Space is the
JUMP on KOTOR 2 (event `0xd9` → OnBrake slot, `onjump.ncs`), the gear ladder
sits on the OnAccelerate slot reachable only from the left mouse button / pad
B. Fixed: Space / pad A re-routed to the shift at the client input hook, W (new
`SwoopJump` hotkey) / pad B jump, gear from `MIN_RACE_GEAR`, K2's lower gate
ladder.

Round 2 (`patch-20260914-163810.log`): shifting, jumping and the gear
announcements all worked (gear 3 reached, four jumps with peak 10.0), but the
race was unplayable for four reasons, all now fixed:

- The pad co-pilot steered the bike into two MINES (they share the enemy pool
  with the pads; 23 pads + 24 mines on Citadel). Mines are now told apart by
  model name, excluded from targeting and sounded as blocks.
- The obstacle loop sounded 90 lane-edge posts that cannot be hit from the
  lane ("enormous amounts of obstacles") and placed the real hazards at their
  object origins, up to 46 units from the collision mesh. Every K2 obstacle is
  now classified from its collision box (bar / block / post) — see
  `docs/llm-docs/swoop-k2-controls-and-jump.md` section 7 for the atlas.
  Posts are silent, bars get a jump cue (the jump itself stays the player's —
  an auto-jump was built and removed the same day at the user's request),
  blocks keep the panned loop at the right place.
- The shift-ready cue fired eight times but was silent: its sample does not
  exist in K2's archives. K2 now plays a sibling sample that does.
- A bar hit bounced the bike against a post and the mod spoke "23.22 seconds,
  race ended" (coast-to-a-stop fallback, now K1-only; finish is the scripts'
  `MIN_RACE_GEAR == 6`). The bike then stayed pinned for 20 s; the magnet kept
  pulling relaunches back into the bar (now off in gear 0) and nothing said
  what to do (a stuck hint now does).

Round 3 (`patch-20260914-204103.log`): everything above worked — 9 bars / 6
blocks / 90 posts classified from the hitbump boxes exactly as the offline
atlas says, 23 pads / 24 mines split, no mine hits, jump beeps at the right
bars, finish via the script flag. New findings, fixed the same evening:

- The race "finished" at 72 s at y≈2800 of 6000: it was a WRECK. The hit
  script takes 50 of 600 hit points per collision and the heartbeat ends the
  race at 0; the log shows exactly twelve gear-to-0 events. Hit points
  (follower +0x8c) are now watched: "hit, N left" per drop, "swoop wrecked"
  instead of a time at zero.
- The shift-ready cue fired in the same tick as three of the gear changes (a
  late shift is already above K2's next gate), which the player heard as a
  shift confirmation. It now waits 0.8 s after a gear change.
- Jump timing: the player pressed W within 0.1 s of the 1.35 s beep and
  landed just short of the bar (MGC02, second press saved it), pressed too
  early on his own at MGC01 and not at all at MGC04. The beep now fires at
  1.0 s ("press now") and repeats once if the bike touches down with the bar
  still ahead.

Round 4 (`patch-20260915-075412.log`): hits spoken with the count, shift
cues ≥ 1 s after the gear, beeps at 1.00 s, first three bars cleared. Then:

- Two cascades (7 + 3 hits, 500 of 600 hp): the pad co-pilot swung the bike
  to the next pad's lane (x 11) while it was still airborne from MGC03 /
  MGC05, into the right-half bars MGC04 / MGC06 that guard those pads — the
  bar pairs are closer than one jump's air distance. OPEN: bar-aware guide.
- The end at 61.56 s was a wreck on mine 137, two units beside the pad lane
  the guide was steering to; mines deal 100 (script disassembled), the
  follower's hp mirror lagged three seconds, so the mod spoke a time. FIXED:
  wreck decided at the finish flag from the heartbeat's 59:59.99 forfeit
  stamp in `<AREA>_SWOOP_MIN/SEC/MSEC` (hp == 0 fallback).
- Six `211TEL_MGE*` scenery objects sounded the block loop at phantom
  positions, two of them right before the first bar. FIXED: skipped by name.
- "Treffer. Noch N." was noise in the chaos (and said "Noch 19" after a
  5-point bump). REMOVED — only finished-with-time vs "Swoop zerstört" is
  spoken now.
- Keys swapped on request: Space jumps, W shifts (pad A jump, pad B shift).

Round 5 (`patch-20260915-083506.log`, two races: 104.88 s finish at 95 hp,
then a wreck at 78.6 s): scenery skipped (`scenery=6`, no phantom loops), Space
= jump consumed 55×, wreck detected from the 59:59.99 stamp, no hit speech.
Two more root causes of "I cannot do this":

- The K2 shift gates were wrong (misread disassembly): the engine accepts a
  shift only above the NEXT gear's minimum (60 / 100 / 150 / 210, K1's
  ladder) — every accepted shift landed at 70 / 105-110 / 155 / 214, every
  earlier W press ran the script with no gear change. The cue fired up to 40
  units early, so W "did nothing" and the next cue came right after the
  engine's own shift sound. FIXED: K2 table = K1 table.
- The pad co-pilot caught 23 of 23 pads (race 1) with the guide tone resting
  ~90 % of the race, and no pad script plays a sound — so by ear nothing
  happened at pads. FIXED: one burst of the pad tone per caught pad.
- Still OPEN, the real time sink: the magnet's lane passes over mines (mine
  142 at x 12 / y 4443 on the pad-132 lane, mine 137 beside pad 125) and
  through right-half bars that guard pads; 10 obstacle hits + 2 mines in
  race 1, 9 + 1 in race 2. Next lever: bar- and mine-aware guide.

Round 6 (`patch-20260915-090027.log`, wreck at 37 s): gates right (cues at
60 / 104 / 150, each shift within a second), pad burst on the hits. Then a
mine on the centre lane (126 at y 320, 105 hp), a jump from x 11 that landed
drifting into the right wall at x 20, and nine relaunches into the post — the
predictive wall cue never fires while parked, and "steer aside" sent the bike
across to the LEFT wall for the same again. ADDED: wall-pinned cue (thump on
the wall's side every 0.7 s in gear 0 at |x| ≥ 16) and the jump beep panned
to a half-bar's free side (centre = must jump).

What round 7 must show:

- `jump cue: … side=left|right|centre` matching the bar's x span; the beep
  heard on that side.
- Round 7 (patch-20260915-091337, two wrecks): 22 of 26 hits were posts —
  six arriving at the wall ~0.7 s after the single thump, sixteen relaunching
  from a park at |x| = 20 where the gear-0 thump had stopped on each relaunch.
  Player chose an audible warning over a rail. REPLACED by the K2 wall LOOP:
  `wall loop: side=…` whenever |tunnelX| ≥ 12.5 or the 20-tick projection
  crosses 16, any gear; `wall loop: stop` once back inside 11.
- Round 8 must show `wall loop: side=…` BEFORE every post `gear -> 0` (if any),
  the loop still running through a relaunch from the wall, and post hits down
  from ~11 per race.
- Round 9 (patch-20260915-142030, wreck at 41 s and at 86 s reaching y≈5300
  of 6000): posts down to 1-3 per race. Race 2 lost 420 hp to FOUR mines (126
  on the centre lane at the start, 141 / 142 / 145 on the pad diagonals) and
  the rest to two bars each followed by relaunch hits with no beep. The wall
  loop fired on every fast pad swing (proj from x 6). ADDED: mine-aware guide
  line (`detour: mine slot=…` / `detour: clear`), wall projection only on
  overshoot (`overshoot=1`), bar beep re-armed while parked.
- Round 10 must show `detour:` lines before the mines that used to hit, no
  `hit: … drop 105` on a detoured mine, `jump cue:` before each relaunch in
  front of a bar, and `wall loop:` only with `overshoot=1` or |tunnelX| ≥ 12.5.

- `shift-ready cue: gear=1 … gate=60.00`, `gear=2 … gate=100.00`,
  `gear=3 … gate=150.00`, `gear=4 … gate=210.00`, and W shifting within a
  second of each cue (no `shift (OnAccelerate): ran` without a `gear ->`).
- A pad-tone burst on every `CROSS … hit=1`.
- With the K2 assist gap at 6: `guide=1` for a good share of each pad
  approach (was ~10 %), `mag=1` only in the last ~6 u; the `CROSS` hit rate
  tells whether 6 is too tight (misses with `abs` between 4 and 6 → raise
  the near gain, not the gap).

What round 5 had to show (met):

- Inventory lines `class=scenery` for the six `211TEL_MGE0*` objects and no
  `loop start slot=9[3-8]`; `hazards: posts=90 scenery=6 bars=9`.
- `Diag.ClientHIE … CONSUMED — K2 swoop jump` on Space, `shift (OnAccelerate)`
  runs on W; gears climb on W, the bike hops on Space.
- No `Speech.spoke … Treffer` lines; at the end either `race end: finish
  (area=211TEL stamp=M:S.C hp=N)` + the time, or `race end: WRECK (… stamp=
  59:59.99 …)` + "Swoop zerstört. Rennen vorbei."

Round-4 expectations (kept for reference):

- `hit: hp 600 -> 550 (drop 50 of 600)` on the first collision.
- `shift-ready cue:` lines at least 0.8 s after the preceding `gear ->`.
- `jump cue:` at `ttc=1.00s`; `jump cue (again):` only after an early jump.

Round-3 expectations (kept for reference):

- `enemy pool: accelpads=23 mines=24`, `mine slot=… model=[mgf_mine01]`, and
  no `magnet slot=` line whose slot is a mine slot.
- Obstacle inventory lines with `box=hitbump … class=post|bar|block`; the
  `hazards: posts=… bars=…` trace with posts ≈ 90 on Citadel.
- `jump cue: slot=… bar y=880.1` about 1.4 s before y=880 when driving near
  the centre; after the W press `jump end: … peak_z≈10` and NO `gear -> 0`
  at y≈880.
- `shift-ready cue:` lines still present AND the cue audible this time.
- `finish: script=1` at the real finish; no race-end speech mid-race.
- If pinned anyway: `stuck hint:` once, and the hint spoken.

KOTOR 1 regression to schedule: one Taris race — the gear source, the finish
trigger and the magnet gear gate are shared; the bar/post classification,
mines and the jump cue are K2-only paths.


# Claude Code prompts

Paste these into Claude Code on the Mac, one milestone per session. Each assumes the working
directory is the fork's clone (except M0, which creates it).

## M0, remaining steps (run from the folder where the clone should live)

The fork, the `extras` branch, the updater change, the backup scripts and the native test baseline
already exist. What is left needs the Mac.

```
Finish M0 for my fork smrios07mdb/xteink-pokemon-game (Xteink X4 Pro).

1. Clone it if it is not here: gh repo clone smrios07mdb/xteink-pokemon-game, cd in, check out
   `extras` (after the M0 PR is merged) and add the upstream remote if missing
   (git remote add upstream https://github.com/thebitsworld/xteink-pokemon-game).
2. Read docs/extras/PLAN.md, docs/extras/SAVE-SAFETY.md and the "Machine setup" notes in CLAUDE.md.
3. Install PlatformIO if needed, then build and run the simulator baseline:
   pio run -e pokemon-x4-pro-simulator -t run_simulator
4. Build the firmware: pio run -e pokemon-x4-pro. Record its size in docs/extras/PROGRESS.md.
5. Ask me to insert the reader's SD card. Run scripts/extras/backup_sd_save.sh /Volumes/<SD>, then
   copy the backup's .crosspoint folder into fs_/ and confirm my real save loads in the simulator.
   Never commit fs_ or the backup. Never write to the SD card.
6. Fill in the remaining Baseline and M0 cells in docs/extras/PROGRESS.md on a branch
   feat/extras-m0-finish, open a PR into `extras`, and stop. Tell me what failed, if anything.

Rules: build nothing from docs/extras/deferred/. Commit nothing to main. Do not change any existing
save or stats format. Do not flash the device. Ask before anything destructive.
```

## M1 to M5 (same prompt, change the milestone)

```
Milestone M<N> of docs/extras/PLAN.md.

1. Read docs/extras/PLAN.md, docs/extras/SAVE-SAFETY.md, docs/extras/PROGRESS.md and the feature
   specs this milestone lists. If lib/Pokemon already contains PokemonExtras rules and tests for
   these features (from Claude chat), use them; do not rewrite them.
2. Before coding, list every file you will touch and every hook you will add, and check each
   against SAVE-SAFETY rules R1-R11. Show me the list and wait for my OK.
3. Implement on branch feat/extras-m<N>-<topic>, everything behind CROSSINK_POKEMON_EXTRAS.
4. Verify in this order: native tests (including test/pokemon_extras and
   test/pokemon_extras_compat), simulator with a throwaway save, simulator with the copy of my real
   save. Take a screenshot of each new screen (CROSSPOINT_SIM_SCREENSHOTS, see CLAUDE.md).
5. Build pokemon-x4-pro, record the firmware size in PROGRESS.md. Stop if it would pass 99%.
6. Open a PR into `extras` with the screenshots and the test output. Do not merge, tag, or flash.
```

## Releasing a milestone to the device

```
The M<N> PR is merged. Tag extras as v2.1.0-extras.<N>-x4pro and push the tag. When the Release
workflow finishes, give me the link to firmware-x4-pro.bin. Remind me to run
scripts/extras/backup_sd_save.sh before I install it.
```

Install: copy the `.bin` to the SD card, then **Settings > System > SD Card Firmware Update**.

## Pulling upstream changes

```
Update main from upstream (fast-forward only), then merge main into a branch off extras. Resolve
conflicts keeping extras hooks intact, run the full test ladder from SAVE-SAFETY.md, and open a
PR. If upstream changed any save format or codec version, stop and tell me before merging.
```

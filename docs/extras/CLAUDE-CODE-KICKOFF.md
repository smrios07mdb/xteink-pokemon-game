# Claude Code prompts

Paste these into Claude Code on the Mac, one milestone per session. Each assumes the working
directory is the fork's clone (except M0, which creates it).

## M0 (run from the folder where the clone should live)

```
Set up my personal fork of thebitsworld/xteink-pokemon-game for an Xteink X4 Pro.

1. Run: gh repo fork thebitsworld/xteink-pokemon-game --clone, then cd into it.
   Confirm the upstream remote exists. Create branch `extras` from main.
2. Apply the plan patch I downloaded: git am ~/Downloads/0001-extras-plan.patch
   Push `extras` and make it the fork's default branch (gh repo edit --default-branch extras).
3. Read docs/extras/PLAN.md and docs/extras/SAVE-SAFETY.md in full, then do PLAN.md "M0" steps 2-7.
   For step 3 use my GitHub login as <fork-owner> (gh api user -q .login).
4. For step 7, ask me to insert the SD card. Run the backup script first, then copy the backup's
   .crosspoint folder into fs_/ for the simulator. Never commit fs_ or the backup.
5. Fill in the Baseline and M0 rows of docs/extras/PROGRESS.md, commit on a branch
   `extras/m0-setup`, open a PR into `extras`, and stop. Tell me what failed, if anything.

Rules: build nothing from docs/extras/deferred/. Commit nothing to main. Do not change any existing save or stats format. Do not flash
the device. Ask before anything destructive.
```

## M1 to M5 (same prompt, change the milestone)

```
Milestone M<N> of docs/extras/PLAN.md.

1. Read docs/extras/PLAN.md, docs/extras/SAVE-SAFETY.md, docs/extras/PROGRESS.md and the feature
   specs this milestone lists. If lib/Pokemon already contains PokemonExtras rules and tests for
   these features (from Claude chat), use them; do not rewrite them.
2. Before coding, list every file you will touch and every hook you will add, and check each
   against SAVE-SAFETY rules R1-R11. Show me the list and wait for my OK.
3. Implement on branch extras/m<N>-<topic>, everything behind CROSSINK_POKEMON_EXTRAS.
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

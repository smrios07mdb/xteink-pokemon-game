# Extras: progress log

One row per milestone. Claude Code fills this in at the end of each milestone PR.

| Milestone | Date | PR | Firmware size (bytes / % of 6,553,600) | Native tests | Compat test | Simulator screenshots | On device since | Notes |
|-----------|------|----|-----------------------------------------|--------------|-------------|-----------------------|-----------------|-------|
| Baseline (upstream `ace5fe0`) | 2026-11-13 | n/a | not measured yet (needs `pio` on the Mac) | 2229 of 2230 pass (Linux, GCC, cloud workspace) | n/a | n/a | | `AssetCompanionStoreTest` does not compile here (`-Werror=restrict` in an snprintf call); unrelated to Pokémon. Run `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` before CMake or nothing builds |
| M0 fork and safety net | 2026-11-13 (partial) | `feat/extras-m0-setup` | pending | same as baseline | n/a | pending | | Done from chat: fork, `extras` branch, updater repointed, backup/restore scripts (tested on a fake card), native baseline. Still to do on the Mac: Actions build, simulator baseline, firmware size, real-save copy in the simulator |
| M1 extras foundation | | | | | | | | |
| M2 streaks, day care, sleep screen | | | | | | | | |
| M3 eggs, daily quests | | | | | | | | |
| M4 book boss, Kindle credit | | | | | | | | Re-test Kindle sync both ways |
| M5 post-game | | | | | | | | |

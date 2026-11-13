# 10. Party sleep screen

Off by default (it replaces whatever sleep screen is configured).

## What the player sees
When the device sleeps: the lead Pokémon's art, nickname, level and XP bar, today's reading time,
the current streak, and egg progress if an egg is held.

## Rules
- Toggle: **Extras > Pokémon sleep screen**.
- When on, `SleepActivity` draws this screen instead of the configured mode.
- If the Pokémon save, the art, or extras cannot be loaded, fall through to the configured sleep
  screen. Sleep must never fail or stall because of this feature.
- Today's reading time: `DailyReadingStats::summarize()`. Streak:
  `GlobalReadingStats::currentReadingStreak()`. Both read-only.

## State
One toggle bit. Nothing else.

## Hooks
`SleepActivity` render dispatch (`src/activities/boot_sleep/SleepActivity.cpp`), before the
`switch` on the configured mode. Reuse `PokemonArt` and the Home accessory's drawing helpers.

## Safety
- Do not add a `SLEEP_SCREEN_MODE` value (R2): that enum is saved in `settings.json`, and upstream
  firmware would not know the new value.
- No writes at all on the sleep path.
- Check wake-from-sleep still restores the book and that the Game Boy wake-into-game path
  (`lastSleepFromGameBoy`) is not affected.

## Tests
Layout test alongside `test/pokemon_ui_layout`; simulator screenshot with and without an egg and
with no valid clock.

## Tunables
Layout, which lines to show.

#!/usr/bin/env bash
# Back up the reader's data folder (/.crosspoint) from a mounted SD card.
# Usage: scripts/extras/backup_sd_save.sh /Volumes/<SD> [backup-root]
# Makes <backup-root>/x4pro-<timestamp>/.crosspoint plus SHA256SUMS. Never writes to the SD card.
set -euo pipefail

sd="${1:-}"
root="${2:-$HOME/x4pro-backups}"
if [[ -z "$sd" || ! -d "$sd/.crosspoint" ]]; then
  echo "Usage: $0 /Volumes/<SD> [backup-root]   (no .crosspoint folder found at '$sd')" >&2
  exit 1
fi

sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }

dest="$root/x4pro-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$dest"
cp -Rp "$sd/.crosspoint" "$dest/"

(cd "$dest" && find .crosspoint -type f -print0 | sort -z | xargs -0 -n 50 bash -c '
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi' _ > SHA256SUMS)

# Verify the copy against the card for the files that matter most.
fail=0
for f in "$sd"/.crosspoint/pokemon-*.bin "$sd"/.crosspoint/global_stats.bin; do
  [[ -f "$f" ]] || continue
  name="$(basename "$f")"
  a="$(sha "$f" | awk '{print $1}')"
  b="$(sha "$dest/.crosspoint/$name" | awk '{print $1}')"
  if [[ "$a" != "$b" ]]; then echo "MISMATCH: $name" >&2; fail=1; fi
done
if [[ $fail -ne 0 ]]; then echo "Backup verification failed: $dest" >&2; exit 2; fi

count="$(find "$dest/.crosspoint" -type f | wc -l | tr -d ' ')"
saves="$(find "$dest/.crosspoint" -maxdepth 1 -name 'pokemon-*.bin' | wc -l | tr -d ' ')"
echo "Backup OK: $dest"
echo "  files: $count   Pokemon save files: $saves"
if [[ "$saves" -eq 0 ]]; then echo "  WARNING: no pokemon-*.bin found on the card." >&2; fi

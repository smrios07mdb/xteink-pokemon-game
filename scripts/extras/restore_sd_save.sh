#!/usr/bin/env bash
# Restore the Pokemon save and reading statistics from a backup made by backup_sd_save.sh.
# Usage: scripts/extras/restore_sd_save.sh <backup-dir> /Volumes/<SD> [--all]
# Default: restores pokemon-*.bin (except pokemon-extras-*), global_stats.bin(.bak),
#          reading_sessions/ and synced_stats/. --all restores the whole .crosspoint folder.
# Always backs up the card's current state first. Turn the reader off before running.
set -euo pipefail

src="${1:-}"; sd="${2:-}"; mode="${3:-}"
here="$(cd "$(dirname "$0")" && pwd)"
if [[ -z "$src" || -z "$sd" || ! -d "$src/.crosspoint" || ! -d "$sd" ]]; then
  echo "Usage: $0 <backup-dir> /Volumes/<SD> [--all]" >&2
  exit 1
fi

echo "Restore from: $src"
echo "Restore to:   $sd/.crosspoint"
[[ "$mode" == "--all" ]] && echo "Mode: everything in .crosspoint" || echo "Mode: Pokemon save + reading statistics"
read -r -p "Type RESTORE to continue: " answer
[[ "$answer" == "RESTORE" ]] || { echo "Cancelled."; exit 1; }

if [[ -d "$sd/.crosspoint" ]]; then
  "$here/backup_sd_save.sh" "$sd" "${X4PRO_BACKUP_ROOT:-$HOME/x4pro-backups}/pre-restore"
fi
mkdir -p "$sd/.crosspoint"

if [[ "$mode" == "--all" ]]; then
  cp -Rp "$src/.crosspoint/." "$sd/.crosspoint/"
else
  for f in "$src"/.crosspoint/pokemon-*.bin "$src"/.crosspoint/global_stats.bin "$src"/.crosspoint/global_stats.bin.bak; do
    [[ -f "$f" ]] || continue
    case "$(basename "$f")" in pokemon-extras-*) continue ;; esac
    cp -p "$f" "$sd/.crosspoint/"
  done
  for d in reading_sessions synced_stats; do
    [[ -d "$src/.crosspoint/$d" ]] && cp -Rp "$src/.crosspoint/$d" "$sd/.crosspoint/"
  done
fi
echo "Restore done. Eject the card before removing it."

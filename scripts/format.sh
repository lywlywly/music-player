#!/usr/bin/env bash
# Format the C++ sources with clang-format, using the repo's .clang-format.
# Covers tracked files and new files not yet added (anything .gitignore does
# not exclude), so new sources are formatted before their first commit.
# Usage: scripts/format.sh [--check]
#   --check  report files that need formatting and exit non-zero; change nothing
set -euo pipefail

case "${1:-}" in
  "") args=(-i) ;;
  --check) args=(--dry-run --Werror) ;;
  *) echo "usage: $0 [--check]" >&2; exit 2 ;;
esac

cd "$(git rev-parse --show-toplevel)"
files=()
while IFS= read -r -d '' file; do
  # Skip files deleted from the working tree but still in the index.
  [[ -f "$file" ]] && files+=("$file")
done < <(git ls-files -z --cached --others --exclude-standard \
  'src/*.cpp' 'src/*.h' 'src/*.mm')
if [[ ${#files[@]} -eq 0 ]]; then
  exit 0
fi
clang-format "${args[@]}" "${files[@]}"

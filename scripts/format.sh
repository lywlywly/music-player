#!/usr/bin/env bash
# Format tracked C++ sources with clang-format, using the repo's .clang-format.
# Usage: scripts/format.sh [--check]
#   --check  report files that need formatting and exit non-zero; change nothing
set -euo pipefail

case "${1:-}" in
  "") args=(-i) ;;
  --check) args=(--dry-run --Werror) ;;
  *) echo "usage: $0 [--check]" >&2; exit 2 ;;
esac

cd "$(git rev-parse --show-toplevel)"
git ls-files -z 'src/*.cpp' 'src/*.h' 'src/*.mm' | xargs -0 clang-format "${args[@]}"

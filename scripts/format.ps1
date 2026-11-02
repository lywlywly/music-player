# Format the C++ sources with clang-format, using the repo's .clang-format.
# Covers tracked files and new files not yet added (anything .gitignore does
# not exclude), so new sources are formatted before their first commit.
# Usage: scripts/format.ps1 [-Check]
#   -Check  report files that need formatting and exit non-zero; change nothing
[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$mode = if ($Check) { '--dry-run', '--Werror' } else { '-i' }

Push-Location (git rev-parse --show-toplevel)
try {
  # Skip files deleted from the working tree but still in the index.
  $files = @(git ls-files --cached --others --exclude-standard `
      'src/*.cpp' 'src/*.h' 'src/*.mm' |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
  if ($files.Count -eq 0) {
    exit 0
  }
  clang-format $mode $files
} finally {
  Pop-Location
}
exit $LASTEXITCODE

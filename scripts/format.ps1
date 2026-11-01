# Format tracked C++ sources with clang-format, using the repo's .clang-format.
# Usage: scripts/format.ps1 [-Check]
#   -Check  report files that need formatting and exit non-zero; change nothing
[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$mode = if ($Check) { '--dry-run', '--Werror' } else { '-i' }

Push-Location (git rev-parse --show-toplevel)
try {
  clang-format $mode (git ls-files 'src/*.cpp' 'src/*.h' 'src/*.mm')
} finally {
  Pop-Location
}
exit $LASTEXITCODE

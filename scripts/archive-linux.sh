#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
if (( $# > 2 )) || [[ "${1:-}" == --help ]]; then
    printf 'Usage: scripts/archive-linux.sh [packaged-directory] [output.tar.gz]\nDefaults: Builds/Linux and Builds/Samurai-Linux-x86_64.tar.gz. Replaces that archive if it exists.\n'
    if (( $# > 2 )); then exit 2; fi
    exit 0
fi
package_dir="${1:-$PROJECT_ROOT/Builds/Linux}"
output="${2:-$PROJECT_ROOT/Builds/Samurai-Linux-x86_64.tar.gz}"
[[ -x "$package_dir/Ronin.sh" && -x "$package_dir/Ronin/Binaries/Linux/Ronin" ]] || {
    printf 'A completed Linux package with Ronin.sh and its game executable is required. Run scripts/unreal.sh package-linux first.\n' >&2
    exit 1
}
has_cooked_content=false
for container in "$package_dir"/Ronin/Content/Paks/*.pak; do
    if [[ -s "$container" ]]; then has_cooked_content=true; break; fi
done
"$has_cooked_content" || { printf 'No nonempty cooked .pak was found in the package.\n' >&2; exit 1; }
package_dir="$(realpath -- "$package_dir")"
output="$(realpath -m -- "$output")"
case "$output" in "$package_dir"|"$package_dir"/*) printf 'Archive output must be outside the packaged directory.\n' >&2; exit 2 ;; esac

# Keep the control table maintained in one source, while making the archive self-contained.
python3 - "$PROJECT_ROOT" "$package_dir" <<'PY'
from pathlib import Path
import sys
root, package = map(Path, sys.argv[1:])
controls = (root / 'README.md').read_text().split('## Controls\n', 1)[1].split('\n## ', 1)[0]
table = '\n'.join(line for line in controls.splitlines() if line.startswith('|'))
if not table:
    raise RuntimeError('The project README has no controls table')
guide = (root / 'docs' / 'LINUX-PACKAGE.md').read_text()
(package / 'README.md').write_text(guide.rstrip() + '\n\n## Controls\n\n' + table + '\n')
PY
mkdir -p -- "$(dirname -- "$output")"
temporary="$(mktemp --tmpdir="$(dirname -- "$output")" .samurai-archive.XXXXXX)"
trap 'rm -f -- "$temporary"' EXIT
tar -czf "$temporary" -C "$(dirname -- "$package_dir")" -- "$(basename -- "$package_dir")"
mv -- "$temporary" "$output"
(cd -- "$(dirname -- "$output")" && sha256sum -- "$(basename -- "$output")") > "$output.sha256"
printf 'Portable archive: %s\nChecksum: %s.sha256\nLaunch the packaged game separately to verify it.\n' "$output" "$output"

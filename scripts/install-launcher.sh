#!/usr/bin/env bash
set -euo pipefail

if (( $# )); then
    printf 'Usage: scripts/install-launcher.sh\nInstall the RONIN app-menu entry for the current user. No sudo is needed.\n'
    [[ "$1" == --help ]] && exit 0
    exit 2
fi
PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
[[ -x "$PROJECT_ROOT/scripts/unreal.sh" ]] || { printf 'Project launcher is missing or not executable.\n' >&2; exit 1; }
python3 - "$PROJECT_ROOT" <<'PY'
from pathlib import Path
import os
import sys

def desktop_quote(value):
    # Desktop Entry Exec quoting, followed by the key's own string escaping.
    escaped = ''.join('\\' + char if char in '\\"`$' else char for char in value)
    escaped = escaped.replace('%', '%%')
    return '"' + escaped.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t') + '"'

def desktop_entry(project, engine_root):
    arguments = ['/bin/bash', desktop_quote(str(project / 'scripts' / 'unreal.sh')), 'play', '-fullscreen']
    if engine_root:
        arguments = ['/usr/bin/env', desktop_quote('UE_ROOT=' + engine_root)] + arguments
    return '\n'.join([
        '[Desktop Entry]', 'Type=Application', 'Version=1.0', 'Name=RONIN',
        'Comment=Sword combat arena', 'Exec=' + ' '.join(arguments),
        'Icon=applications-games', 'Terminal=false', 'Categories=Game;ActionGame;',
        'Keywords=ronin;sword;combat;arena;', 'StartupNotify=false', '',
    ])

if __name__ == '__main__':
    project = Path(sys.argv[1])
    destination = Path.home() / '.local' / 'share' / 'applications' / 'ronin.desktop'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(desktop_entry(project, os.environ.get('UE_ROOT')), encoding='utf-8')
    destination.chmod(0o644)
    print(f'Installed {destination}. Search for RONIN in the applications menu.')
PY

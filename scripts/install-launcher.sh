#!/usr/bin/env bash
set -euo pipefail

launcher_mode=play
if (( $# )); then
    case "$1" in
        --editor) launcher_mode=editor ;;
        --help) printf 'Usage: scripts/install-launcher.sh [--editor]\nInstall the SAMURAI game or Unreal Editor app-menu entry for the current user. No sudo is needed.\n'; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    (( $# == 1 )) || { printf 'Expected at most one option.\n' >&2; exit 2; }
fi
PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
[[ -x "$PROJECT_ROOT/scripts/unreal.sh" ]] || { printf 'Project launcher is missing or not executable.\n' >&2; exit 1; }
python3 - "$PROJECT_ROOT" "$launcher_mode" <<'PY'
from pathlib import Path
import configparser
import os
import re
import sys

def desktop_escape(value):
    return value.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')

def desktop_quote(value):
    # Desktop Entry Exec quoting, followed by the key's own string escaping.
    escaped = ''.join('\\' + char if char in '\\"`$' else char for char in value)
    escaped = escaped.replace('%', '%%')
    return '"' + desktop_escape(escaped) + '"'

def desktop_entry(project, engine_root, mode):
    editor = mode == 'editor'
    icon = project / 'assets' / 'branding' / 'samurai.png'
    if not editor and not icon.is_file():
        raise SystemExit(f'Game icon is missing: {icon}. Restore it before installing the game launcher.')
    arguments = ['/bin/bash', desktop_quote(str(project / 'scripts' / 'unreal.sh')), mode]
    if editor:
        arguments += [
            '-nosplash',
            desktop_quote('-ini:Engine:[ConsoleVariables]:r.ShaderCompiler.JobCacheDDC=1,[ConsoleVariables]:t.MaxFPS=30'),
            desktop_quote('-ExecCmds=sg.ShadowQuality 1,sg.PostProcessQuality 1,r.ScreenPercentage 50'),
        ]
    else:
        arguments += ['-fullscreen']
    if engine_root:
        arguments = ['/usr/bin/env', desktop_quote('UE_ROOT=' + engine_root)] + arguments
    return '\n'.join([
        '[Desktop Entry]', 'Type=Application', 'Version=1.0',
        'Name=Unreal Editor — SAMURAI' if editor else 'Name=SAMURAI',
        'Comment=Edit the SAMURAI project and import Fab assets' if editor else 'Comment=Sword combat arena',
        'Exec=' + ' '.join(arguments),
        'Icon=applications-development' if editor else 'Icon=' + desktop_escape(str(icon)),
        'Terminal=false', 'Categories=Development;' if editor else 'Categories=Game;ActionGame;',
        'Keywords=unreal;editor;samurai;fab;' if editor else 'Keywords=samurai;sword;combat;arena;',
        'StartupNotify=false', '',
    ])

def remove_legacy_entry(destination, project, mode):
    legacy = destination.with_name('ronin-editor.desktop' if mode == 'editor' else 'ronin.desktop')
    previous = configparser.ConfigParser(interpolation=None)
    try:
        with legacy.open(encoding='utf-8') as source:
            previous.read_file(source)
    except (OSError, UnicodeError, configparser.Error):
        return
    if not previous.has_section('Desktop Entry'):
        return
    entry = previous['Desktop Entry']
    expected_name = 'Unreal Editor — RONIN' if mode == 'editor' else 'RONIN'
    command = '/bin/bash ' + desktop_quote(str(project / 'scripts' / 'unreal.sh')) + ' ' + mode
    # Match the installer command, optionally prefixed by its saved UE_ROOT.
    # A legacy entry from another checkout must remain untouched.
    execution = r'(?:/usr/bin/env "UE_ROOT=(?:[^"\\]|\\.)*" )?' + re.escape(command) + r'(?: .*)?'
    if (entry.get('Type') == 'Application' and entry.get('Name') == expected_name
            and re.fullmatch(execution, entry.get('Exec', ''))):
        legacy.unlink()

if __name__ == '__main__':
    project = Path(sys.argv[1])
    mode = sys.argv[2]
    filename = 'samurai-editor.desktop' if mode == 'editor' else 'samurai.desktop'
    destination = Path.home() / '.local' / 'share' / 'applications' / filename
    entry = desktop_entry(project, os.environ.get('UE_ROOT'), mode)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(entry, encoding='utf-8')
    destination.chmod(0o644)
    remove_legacy_entry(destination, project, mode)
    name = 'Unreal Editor — SAMURAI' if mode == 'editor' else 'SAMURAI'
    print(f'Installed {destination}. Search for {name} in the applications menu.')
PY

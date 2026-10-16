#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
case "${1:-}" in
    -h|--help)
        printf 'Usage: scripts/test-unreal.sh\nRun the compiled Ronin automation tests with NullRHI. Set UE_ROOT or link .tools/unreal.\n'
        printf 'RONIN_TEST_TIMEOUT sets the deadline in seconds (default 600). Reports persist in reports/unreal-tests/.\n'
        exit 0 ;;
    '') ;;
    *) printf 'Unexpected argument. See scripts/test-unreal.sh --help\n' >&2; exit 2 ;;
esac
engine_candidate="${UE_ROOT:-$PROJECT_ROOT/.tools/unreal}"
EDITOR="$engine_candidate/Engine/Binaries/Linux/UnrealEditor-Cmd"
[[ -x "$EDITOR" ]] || { printf 'UnrealEditor-Cmd not found. Set UE_ROOT and run build + bootstrap first.\n' >&2; exit 1; }
command -v python3 >/dev/null || { printf 'python3 is required to verify the automation report.\n' >&2; exit 1; }
command -v timeout >/dev/null || { printf 'GNU timeout is required to bound an unattended test run.\n' >&2; exit 1; }
deadline="${RONIN_TEST_TIMEOUT:-600}"
[[ "$deadline" =~ ^[1-9][0-9]*$ ]] || { printf 'RONIN_TEST_TIMEOUT must be a positive integer in seconds.\n' >&2; exit 2; }
mkdir -p "$PROJECT_ROOT/reports/unreal-tests"
report_dir="$(mktemp -d "$PROJECT_ROOT/reports/unreal-tests/run-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")"
printf 'Unreal automation evidence: %s\n' "$report_dir"

set +e
timeout --signal=TERM --kill-after=30s "$deadline" "$EDITOR" "$PROJECT_ROOT/Ronin.uproject" \
    -NullRHI '-ExecCmds=Module Load BinkAudioDecoder,Automation RunTests Ronin;Quit' '-TestExit=Automation Test Queue Empty' \
    "-ReportExportPath=$report_dir" -unattended -nosound -nop4 -nosplash -log -stdout -FullStdOutLogOutput \
    2>&1 | tee "$report_dir/console.log"
run_status=("${PIPESTATUS[@]}")
set -e
if (( run_status[0] != 0 || run_status[1] != 0 )); then
    printf 'Unreal automation process/report capture failed (engine %s, capture %s). Evidence: %s\n' "${run_status[0]}" "${run_status[1]}" "$report_dir" >&2
    exit 1
fi

python3 - "$report_dir" "$PROJECT_ROOT/Source/Ronin/Private/Tests/RoninAutomationTests.cpp" <<'PY'
import json
from pathlib import Path
import re
import sys

report_dir, source = map(Path, sys.argv[1:])
try:
    report = json.loads((report_dir / 'index.json').read_text(encoding='utf-8-sig'))
    expected = set(re.findall(r'"(Ronin\.Integration\.[^"]+)"', source.read_text()))
    if not expected:
        raise ValueError('No declared Ronin integration tests found in source')
    tests = report.get('tests')
    if not isinstance(tests, list) or not tests:
        raise ValueError('Automation report contains no tests')
    actual = {test.get('fullTestPath'): test for test in tests if str(test.get('fullTestPath', '')).startswith('Ronin.')}
    missing = expected - actual.keys()
    if missing:
        raise ValueError(f'Missing tests (rebuild RoninEditor): {sorted(missing)}')
    for name, test in actual.items():
        if test.get('state') != 'Success' or test.get('errors', 0) != 0:
            raise ValueError(f'{name}: state={test.get("state")}, errors={test.get("errors")}')
    for field in ('failed', 'notRun', 'inProcess'):
        if report.get(field) != 0:
            raise ValueError(f'Report {field} is not zero: {report.get(field)!r}')
    successes = report.get('succeeded', 0) + report.get('succeededWithWarnings', 0)
    if successes < len(expected):
        raise ValueError('Report success count is smaller than the declared test suite')
    console = (report_dir / 'console.log').read_text(errors='replace')
    if not re.search(r'TEST COMPLETE\. EXIT CODE:\s*0', console):
        raise ValueError('No successful automation completion marker in engine log')
except (OSError, ValueError, TypeError, AttributeError) as error:
    print(f'FAIL: Unreal results were not verified: {error}. Evidence: {report_dir}', file=sys.stderr)
    sys.exit(1)
print(f'PASS: {len(actual)} Unreal integration tests. Evidence: {report_dir}')
if report.get('succeededWithWarnings', 0):
    print('The engine reported warnings; review console.log and index.json before signing off.')
PY

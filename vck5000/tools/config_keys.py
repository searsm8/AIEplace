#!/usr/bin/env python3
"""Which config keys does sw_only actually READ? — and a check that nothing sets one it doesn't.

A config key that is set but never read is silent: `value_or(default)` cannot tell a typo from an
absent key, so `--set aux_select_hpwl_rato=1.01` writes the misspelled key, the exe falls back to
the default, and every arm of the sweep runs identical behaviour while reporting success. That
failure mode costs whatever the sweep costs — the 2026-08-17 aux-ratio A/B was 2.7 h — and it
produces a confident wrong answer rather than an error.

The key set is DERIVED FROM THE SOURCE on every call, never hand-maintained, so it cannot drift
away from the code the way a checked-in list would.

    python3 tools/config_keys.py --list             # every key the code reads
    python3 tools/config_keys.py --check-configs    # assert the live configs set nothing unread
    python3 tools/config_keys.py --check a.b c      # assert these keys are read (used by dse.py)

Run from vck5000/. Exits non-zero on any unread key.
"""
import argparse
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'host/src/sw_only/src')

# The two idioms sw_only reads config through. Anything else is invisible here, which is why
# --check-configs is a guard on the CONFIGS rather than proof that a key is dead.
_SUBSCRIPT = re.compile(r'cfg\s*\[\s*"([A-Za-z_]+)"\s*\]\s*\[\s*"([A-Za-z_0-9]+)"\s*\]')
_REQUIRE = re.compile(
    r'ConfigUtils::require\s*<[^>]+>\s*\(\s*cfg\s*,\s*"([A-Za-z_]+)"\s*,\s*"([A-Za-z_0-9]+)"\s*\)')

# Written by dse.py for the exe to echo back into results.csv; read by parseDSEParams(), which
# pulls it off the parsed table rather than through either idiom above.
_EXTRA_READ = {'output.DSE_info'}

# Set but never read, deliberately: sw_only is CPU-only and has no xclbin to load. The key is a
# leftover of the era when one config served the hardware variants too. Listed rather than deleted
# because it also sits in the FROZEN test/regress configs, which must stay byte-identical to the
# inputs that produced their baselines.
_KNOWN_UNREAD = {'input.xclbin'}


def keys_read():
    """{'section.key'} for every config key the sw_only sources read."""
    found = set(_EXTRA_READ)
    for path in glob.glob(os.path.join(SRC, '**', '*.cpp'), recursive=True):
        with open(path, encoding='utf-8', errors='replace') as f:
            text = f.read()
        for rx in (_SUBSCRIPT, _REQUIRE):
            for section, key in rx.findall(text):
                found.add('%s.%s' % (section, key))
    if not found:
        sys.exit('config_keys: found no reads under %s -- the source moved, fix this tool' % SRC)
    return found


def qualify(key, known):
    """'params.x' stays; bare 'x' is resolved the way dse.py's section_for() does."""
    if '.' in key:
        return key
    return 'input.benchmark' if key == 'benchmark' else 'params.%s' % key


def suggest(key, known):
    """Closest known key, so a typo says what was meant."""
    import difflib
    bare = key.rsplit('.', 1)[-1]
    pool = {k.rsplit('.', 1)[-1]: k for k in known}
    near = difflib.get_close_matches(bare, pool, n=1, cutoff=0.7)
    return pool[near[0]] if near else None


def check_keys(keys, known, label):
    """Assert every key is read by the code. Returns the number of failures."""
    bad = 0
    for key in keys:
        full = qualify(key, known)
        if full in known:
            continue
        bad += 1
        hint = suggest(full, known)
        print('%s: "%s" is not read by sw_only%s'
              % (label, key, ' -- did you mean "%s"?' % hint.rsplit('.', 1)[-1] if hint else ''),
              file=sys.stderr)
    return bad


def check_configs(known):
    """Assert the LIVE configs set nothing the code never reads.

    Frozen test/regress configs are deliberately out of scope: they are historical inputs pinned to
    their baselines, audited once (2026-08-17, clean apart from _KNOWN_UNREAD) and not edited since.
    """
    import tomlkit
    bad = 0
    for path in [os.path.join(REPO, 'host/src/sw_only/default_config.toml'),
                 os.path.join(REPO, 'host/src/sw_only/run_config.toml')]:
        if not os.path.isfile(path):
            continue
        doc = tomlkit.parse(open(path, encoding='utf-8').read())
        for section, table in doc.items():
            if not hasattr(table, 'keys'):
                continue
            for key in table:
                full = '%s.%s' % (section, key)
                if full in known or full in _KNOWN_UNREAD:
                    continue
                bad += 1
                hint = suggest(full, known)
                print('%s: sets [%s] %s, which sw_only never reads%s'
                      % (os.path.relpath(path, REPO), section, key,
                         ' -- did you mean "%s"?' % hint.rsplit('.', 1)[-1] if hint else ''),
                      file=sys.stderr)
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--list', action='store_true', help='print every key the code reads')
    ap.add_argument('--check-configs', action='store_true', help='assert live configs set no dead keys')
    ap.add_argument('--check', nargs='*', metavar='KEY', help='assert these keys are read')
    args = ap.parse_args()

    known = keys_read()
    if args.list:
        for k in sorted(known):
            print(k)
        return 0

    bad = 0
    if args.check:
        bad += check_keys(args.check, known, 'config_keys')
    if args.check_configs:
        bad += check_configs(known)
    if bad:
        print('config_keys: %d unread key(s). A key sw_only does not read is silently ignored.'
              % bad, file=sys.stderr)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())

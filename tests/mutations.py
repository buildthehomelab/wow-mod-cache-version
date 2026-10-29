#!/usr/bin/env python3
"""Prove behavioral tests reject representative faults in production code."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/CacheVersion.cpp").read_text()
MUTATIONS = [('wrong startup hook', '{ WORLDHOOK_ON_STARTUP }', '{ WORLDHOOK_ON_AFTER_CONFIG_LOAD }'),
 ('empty identifier accepted', 'name.empty() || name.size() > 64', 'name.size() > 64'),
 ('64 character identifier rejected', 'name.size() > 64', 'name.size() >= 64'),
 ('65 character identifier accepted', 'name.size() > 64', 'name.size() > 65'),
 ('underscore rejected', " || c == '_'", ''),
 ('unsafe identifier accepted',
  "if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))",
  'if ((static_cast<void>(c), false))'),
 ('explicit output ignored', 'if (!file.empty())', 'if (false)'),
 ('fallback filename wrong', '"cache-version.txt";', '"wrong.txt";'),
 ('missing checksum marker changed',
  'std::string checksum = "none";',
  'std::string checksum = "missing";'),
 ('checksum discarded', 'checksum = fields[1].Get<std::string>();', 'checksum = "none";'),
 ('salt discarded',
  'input << "salt=" << sConfigMgr->GetOption<std::string>("CacheVersion.Salt", "", false);',
  'input << "salt=";'),
 ('disabled switch ignored',
  'if (!sConfigMgr->GetOption<bool>("CacheVersion.Enable", true))',
  'if (false)'),
 ('default disabled',
  'GetOption<bool>("CacheVersion.Enable", true)',
  'GetOption<bool>("CacheVersion.Enable", false)'),
 ('unchanged file rewritten', 'if (current == version)', 'if (false)'),
 ('newline omitted', "out << version << '\\n';", 'out << version;'),
 ('temp append', 'std::ios::trunc', 'std::ios::app'),
 ('rename cleanup omitted', 'std::filesystem::remove(temp, error);', '(void)temp;'),
 ('leading trim omitted', 'name.erase(0, name.find_first_not_of(" \\t"));', ''),
 ('trailing trim omitted', 'name.erase(name.find_last_not_of(" \\t") + 1);', '')]


def check_mutation(mutation):
    name, old, new = mutation
    if old not in SOURCE:
        return name, False, "Mutation anchor missing: update the sentinel for the new source"
    with tempfile.TemporaryDirectory(prefix="cache-version-mutation-") as temporary:
        source = Path(temporary) / "CacheVersion.cpp"
        source.write_text(SOURCE.replace(old, new, 1))
        environment = dict(os.environ, CACHE_VERSION_SOURCE=str(source))
        environment.pop("BUILD_DIR", None)
        environment.pop("CXXFLAGS", None)
        result = subprocess.run([sys.executable, str(ROOT / "tests/run.py")], env=environment,
                                capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        # Compile errors do not count as a caught behavioral regression.
        killed = result.returncode != 0 and "FAIL:" in output and "error:" not in output
        return name, killed, output[-1500:]


if __name__ == "__main__":
    subprocess.run([sys.executable, str(ROOT / "tests/run.py")], check=True)
    workers = min(4, os.cpu_count() or 1)
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(check_mutation, MUTATIONS))
    for name, killed, output in results:
        print(f'{"CAUGHT" if killed else "MISSED"}: {name}')
        if not killed:
            print(output)
    caught = sum(killed for _, killed, _ in results)
    print(f"Caught {caught}/{len(results)} representative behavioral regressions")
    sys.exit(0 if caught == len(results) else 1)

# Cache-version regression suite

Run on Linux with Python 3, a C++20 GCC/Clang compiler and OpenSSL development
headers/library (`libssl-dev` on Debian/Ubuntu):

```sh
python3 tests/run.py
CXX=clang++ CXXFLAGS="-fsanitize=address,undefined" python3 tests/run.py
python3 tests/mutations.py
```

The suite includes the unchanged production CacheVersion.cpp and compiles every
other production .cpp, including the real loader. External config, database,
logging and script dispatch are controlled adapters. SHA-1 uses real libcrypto;
expected digests are fixed independent Python hashlib results. Filesystem cases
use real isolated temporary directories; no live database/server is accessed.
The script adapter follows the core's empty-hook-list enable-all contract.

CI runs fatal ASan/UBSan with GCC and Clang and requires every representative
mutant to compile and fail a behavioral assertion. Compile failures do not count.
Coverage uses gcovr 8.4, covering production sources only. Gates are 100% lines,
100% functions and 96% branches after throw/unreachable filtering. The only
uncovered branches in the initial report are three generated allocation/cleanup
edges at `new CacheVersionWorldScript()`; registration is itself exercised.

This establishes the tested module's behavior, not real DB CHECKSUM semantics,
locale-dependent identifier rules, concurrent writers, disk-full/late stream
write failures, power-loss durability, client cache clearing or full realm
integration. In particular production currently checks stream opening but does
not check write/close success before rename; that existing robustness gap is
not covered by the successful-write/failed-open/failed-rename cases.

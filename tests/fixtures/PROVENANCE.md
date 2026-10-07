# Sample file provenance

Every file the tests read from this directory, how it was made and how it was
checked. Samples are rebuilt only with `regenerate.sh`, which rewrites
`SHA256SUMS`; `../check-fixtures.sh` (run by `make check`) fails when a sample
is missing, changed, unlisted here or untracked.

Independent checks come from `regenerate.sh --verify`, run on 2026-10-05 with
osslsigncode 2.14, LLVM 21.1.8 (`llvm-objdump`, `llvm-otool`) and file 5.47.
For the three Windows samples osslsigncode signed a scratch copy with a
throw-away self-signed certificate and recomputed the digest; for the PE files
the stored and calculated digests matched.

All sources in `src/` were written for this project and are under the project
licence (MIT, see `COPYING`). The compiled output also contains runtime code
from mingw-w64 (public domain / ZPL) in the Windows files and nothing from
Apple, so redistribution is permitted.

| Sample | Tool | Version | Command | Source | Licence | Size | Check |
|---|---|---|---|---|---|---|---|
| tiny.exe | x86_64-w64-mingw32-gcc | GCC 16-posix | `x86_64-w64-mingw32-gcc -Os -s -Wl,--gc-sections,--file-alignment,512,--no-insert-timestamp -o tiny.exe src/tiny.c` | src/tiny.c | MIT (project); mingw-w64 runtime public domain/ZPL | 13824 | ok: `file` reports PE32+ console x86-64; osslsigncode signed it and the stored and calculated digests match |
| test.dll | x86_64-w64-mingw32-gcc | GCC 16-posix | `x86_64-w64-mingw32-gcc -Os -shared -s -Wl,--gc-sections,--file-alignment,512,--no-insert-timestamp -o test.dll src/test_dll.c` | src/test_dll.c | MIT (project); mingw-w64 runtime public domain/ZPL | 11264 | ok: `file` reports PE32+ DLL x86-64; `llvm-objdump -p` lists KERNEL32.dll and msvcrt.dll imports; osslsigncode digests match |
| tiny.msi | wixl (msitools) | 0.106 | `cd src && wixl -o ../tiny.msi tiny.wxs` | src/tiny.wxs | MIT (project) | 10240 | ok: `file` reports MSI Installer; osslsigncode signed a copy and recomputed the digest |
| tiny-macho-x86_64 | clang + ld64.lld | 21.1.8 | `clang -target x86_64-apple-macos11 -Os -fno-unwind-tables -fno-asynchronous-unwind-tables -c -o macho-x86_64.o src/macho.c`, then `ld64.lld -arch x86_64 -platform_version macos 11.0 11.0 -e _main -no_adhoc_codesign -o tiny-macho-x86_64 macho-x86_64.o -Lsrc -lSystem` | src/macho.c, src/libSystem.tbd | MIT (project) | 4248 | ok: `file` reports Mach-O 64-bit x86_64 executable; `llvm-objdump --macho -f` parses it and `llvm-otool -l` shows LC_MAIN |
| tiny-macho-arm64 | clang + ld64.lld | 21.1.8 | same as x86_64 with `arm64` in place of `x86_64`, output `tiny-macho-arm64` | src/macho.c, src/libSystem.tbd | MIT (project) | 16536 | ok: `file` reports Mach-O 64-bit arm64 executable; `llvm-objdump --macho -f` parses it and `llvm-otool -l` shows LC_MAIN |
| tiny-macho-universal | llvm-lipo | 21.1.8 | `llvm-lipo -create tiny-macho-x86_64 tiny-macho-arm64 -output tiny-macho-universal` | the two thin files above | MIT (project) | 32920 | ok: `file` reports a universal binary with 2 architectures; `llvm-otool -f` lists 2 slices |
| plain.txt | hand-written | n/a | none (typed by hand) | none | MIT (project) | 64 | ok: `file` reports ASCII text; deliberately not a binary format |

`pe-reference-digests.txt` is not a sample. It records, for `tiny.exe` and
`test.dll` with 0 to 7 fixed bytes appended, the Authenticode SHA-256 that
osslsigncode 2.14 calculates for the signed file, so tests can compare the
library with an independent tool without it being installed. It is written by
`regenerate.sh --reference pe-reference-digests.txt` (tool, commands, date and
input hashes are in its header) and recomputed and compared by
`regenerate.sh --verify`. A program signed by osslsigncode is not stored (it
would take the samples over the size budget); tests derive an already signed
program with the library and compare it with the recorded `tiny-signed.exe`
fingerprint.

Total size of the samples: 89096 bytes (budget 102400).

The Mach-O files are unsigned (`-no_adhoc_codesign`), so signer tests start
from a file with no signature. The installer embeds its creation time, so
rebuilding it changes its hash; rebuild only to replace a sample on purpose.

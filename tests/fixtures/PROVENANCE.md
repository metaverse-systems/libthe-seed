# Sample file provenance

Every file the tests read from this directory, how it was made and how it was
checked. Samples are rebuilt only with `regenerate.sh`, which rewrites
`SHA256SUMS`; `../check-fixtures.sh` (run by `make check`) fails when a sample
is missing, changed, unlisted here or untracked.

Independent checks come from `regenerate.sh --verify`, run on 2026-10-07 with
osslsigncode 2.14, LLVM 21.1.8 (`llvm-objdump`, `llvm-otool`, `llvm-lipo`), Python 3.14 (`check_pages.py`) and file 5.47.
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
| tiny-macho-arm64-adhoc | clang + ld64.lld | 21.1.8 | `clang -target arm64-apple-macos11 -Os -fno-unwind-tables -fno-asynchronous-unwind-tables -c -o macho-arm64.o src/macho.c`, then `ld64.lld -arch arm64 -platform_version macos 11.0 11.0 -e _main -adhoc_codesign -o tiny-macho-arm64-adhoc macho-arm64.o -Lsrc -lSystem` | src/macho.c, src/libSystem.tbd | MIT (project) | 16848 | ok: `file` reports Mach-O 64-bit arm64 executable; `llvm-objdump --macho -f` parses it, `llvm-otool -l` shows LC_MAIN and LC_CODE_SIGNATURE; `check_pages.py` recomputes every page hash of the ad-hoc signature written by ld64.lld |
| tiny-macho-x86_64-adhoc | clang + ld64.lld | 21.1.8 | `clang -target x86_64-apple-macos11 -Os -fno-unwind-tables -fno-asynchronous-unwind-tables -c -o macho-x86_64.o src/macho.c`, then `ld64.lld -arch x86_64 -platform_version macos 11.0 11.0 -e _main -adhoc_codesign -o tiny-macho-x86_64-adhoc macho-x86_64.o -Lsrc -lSystem` | src/macho.c, src/libSystem.tbd | MIT (project) | 4464 | ok: `file` reports Mach-O 64-bit x86_64 executable; `llvm-objdump --macho -f` parses it, `llvm-otool -l` shows LC_MAIN and LC_CODE_SIGNATURE; `check_pages.py` recomputes every page hash |
| tiny-macho-universal-adhoc | llvm-lipo | 21.1.8 | `llvm-lipo -create tiny-macho-x86_64-adhoc tiny-macho-arm64-adhoc -output tiny-macho-universal-adhoc` | the two ad-hoc signed thin files above | MIT (project) | 33232 | ok: `file` reports a universal binary with 2 architectures; `llvm-lipo -archs` lists x86_64 arm64; `check_pages.py` recomputes every page hash in both slices |
| tiny-macho-x86_64-nospace | clang + ld64.lld | 21.1.8 | `clang -target x86_64-apple-macos11 -Os -fno-unwind-tables -fno-asynchronous-unwind-tables -c -o macho-x86_64.o src/macho.c`, then `ld64.lld -arch x86_64 -platform_version macos 11.0 11.0 -e _main -no_adhoc_codesign -headerpad 0 -o tiny-macho-x86_64-nospace macho-x86_64.o -Lsrc -lSystem` | src/macho.c, src/libSystem.tbd | MIT (project) | 4248 | ok: `file` reports Mach-O 64-bit x86_64 executable; `llvm-otool -l` shows LC_MAIN; `check_pages.py` reports 0 free header bytes (load commands end at 680, first section at 680) |
| tiny-macho-x86_64-exactfit | clang + ld64.lld | 21.1.8 | `clang -target x86_64-apple-macos11 -Os -fno-unwind-tables -fno-asynchronous-unwind-tables -c -o macho-x86_64.o src/macho.c`, then `ld64.lld -arch x86_64 -platform_version macos 11.0 11.0 -e _main -no_adhoc_codesign -headerpad 0x10 -o tiny-macho-x86_64-exactfit macho-x86_64.o -Lsrc -lSystem` | src/macho.c, src/libSystem.tbd | MIT (project) | 4248 | ok: `file` reports Mach-O 64-bit x86_64 executable; `llvm-otool -l` shows LC_MAIN; `check_pages.py` reports exactly 16 free header bytes (load commands end at 680, first section at 696) |
| tiny-macho-universal64 | llvm-lipo | 21.1.8 | `llvm-lipo -create -fat64 tiny-macho-x86_64 tiny-macho-arm64 -output tiny-macho-universal64` | the two unsigned thin files above | MIT (project) | 32920 | ok: first four bytes CA FE BA BF; `llvm-lipo -archs` lists x86_64 arm64; `llvm-objdump --macho --universal-headers` reports FAT_MAGIC_64; `file` 5.47 reports only "data" for the 64-bit table, and `llvm-otool -f` prints the 64-bit magic as 0xcafebabe, so neither is used for it |
| tiny-macho-dylib-arm64 | clang + ld64.lld | 21.1.8 | `clang -target arm64-apple-macos11 -Os -fno-unwind-tables -fno-asynchronous-unwind-tables -c -o macho-arm64.o src/macho.c`, then `ld64.lld -arch arm64 -platform_version macos 11.0 11.0 -e _main -no_adhoc_codesign -dylib -o tiny-macho-dylib-arm64 macho-arm64.o -Lsrc -lSystem` | src/macho.c, src/libSystem.tbd | MIT (project) | 16472 | ok: `file` reports Mach-O 64-bit arm64 dynamically linked shared library; `llvm-objdump --macho -f` parses it and `llvm-otool -l` shows LC_ID_DYLIB |
| tiny-macho-x86_64-data-after-sig | cp + printf (SYNTHETIC) | n/a | `cp tiny-macho-x86_64-adhoc tiny-macho-x86_64-data-after-sig`, then 16 fixed bytes (a5 a6 ... b4) appended | tiny-macho-x86_64-adhoc | MIT (project) | 4480 | synthetic (not linker output): used only for the refusal case "data after the signature", never as evidence of validity; `check_pages.py` flags the 16 bytes after the signature |
| plain.txt | hand-written | n/a | none (typed by hand) | none | MIT (project) | 64 | ok: `file` reports ASCII text; deliberately not a binary format |
| dep/libbaz.so | gcc | 16.2.0 | `gcc <elf flags> -shared -fPIC -Wl,-soname,libbaz.so -o libbaz.so src/dep/libbaz.c` | src/dep/libbaz.c | MIT (project) | 1288 | ok: `readelf -d` shows bottom of the chain, soname libbaz.so, no needed libraries; `file` reports ELF 64-bit LSB |
| dep/libbar.so | gcc | 16.2.0 | `gcc <elf flags> -shared -fPIC -Wl,-soname,libbar.so -o libbar.so src/dep/libbar.c -L. -Wl,--no-as-needed -lbaz` | src/dep/libbar.c | MIT (project) | 1720 | ok: `readelf -d` shows needs libbaz.so; `file` reports ELF 64-bit LSB |
| dep/libfoo.so | gcc | 16.2.0 | `gcc <elf flags> -shared -fPIC -Wl,-soname,libfoo.so -o libfoo.so src/dep/libfoo.c -L. -Wl,--no-as-needed -lbar` | src/dep/libfoo.c | MIT (project) | 1720 | ok: `readelf -d` shows needs libbar.so; `file` reports ELF 64-bit LSB |
| dep/appA | gcc | 16.2.0 | `gcc <elf flags> -o appA src/dep/appA.c -L. -Wl,--no-as-needed -lfoo -Wl,-rpath-link,. -Wl,-e,_start` | src/dep/appA.c | MIT (project) | 1888 | ok: `readelf -d` shows needs libfoo.so; `file` reports ELF 64-bit LSB |
| dep/appB | gcc | 16.2.0 | `gcc <elf flags> -o appB src/dep/appB.c -L. -Wl,--no-as-needed -lfoo -Wl,-rpath-link,. -Wl,-e,_start` | src/dep/appB.c | MIT (project) | 1888 | ok: `readelf -d` shows needs libfoo.so; `file` reports ELF 64-bit LSB |
| dep/libbaz.dll | x86_64-w64-mingw32-gcc | GCC 16-posix | `x86_64-w64-mingw32-gcc <pe flags> -shared -Wl,-e,0 -o libbaz.dll src/dep/libbaz.c` | src/dep/libbaz.c | MIT (project) | 3072 | ok: `x86_64-w64-mingw32-objdump -p` shows imports nothing; `file` reports PE32+ |
| dep/libbar.dll | x86_64-w64-mingw32-gcc | GCC 16-posix | `x86_64-w64-mingw32-gcc <pe flags> -shared -Wl,-e,0 -o libbar.dll src/dep/libbar.c -L. -lbaz` | src/dep/libbar.c | MIT (project) | 3072 | ok: `x86_64-w64-mingw32-objdump -p` shows imports libbaz.dll; `file` reports PE32+ |
| dep/libfoo.dll | x86_64-w64-mingw32-gcc | GCC 16-posix | `x86_64-w64-mingw32-gcc <pe flags> -shared -Wl,-e,0 -o libfoo.dll src/dep/libfoo.c -L. -lbar` | src/dep/libfoo.c | MIT (project) | 3072 | ok: `x86_64-w64-mingw32-objdump -p` shows imports libbar.dll; `file` reports PE32+ |
| dep/appA.exe | x86_64-w64-mingw32-gcc | GCC 16-posix | `x86_64-w64-mingw32-gcc <pe flags> -Wl,-e,_start -o appA.exe src/dep/appA.c -L. -lfoo` | src/dep/appA.c | MIT (project) | 2560 | ok: `x86_64-w64-mingw32-objdump -p` shows imports libfoo.dll; `file` reports PE32+ |
| dep/appB.exe | x86_64-w64-mingw32-gcc | GCC 16-posix | `x86_64-w64-mingw32-gcc <pe flags> -Wl,-e,_start -o appB.exe src/dep/appB.c -L. -lfoo` | src/dep/appB.c | MIT (project) | 2560 | ok: `x86_64-w64-mingw32-objdump -p` shows imports libfoo.dll; `file` reports PE32+ |

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

The `dep/` samples are a chain of tiny libraries and programs for the
dependency lister tests: `libbaz` <- `libbar` <- `libfoo` <- `appA`, `appB`,
built once as ELF and once as PE. Each names the next library explicitly
(`-l` with `--no-as-needed` for ELF, an import library for PE) and links with
`-nostdlib`, so the only needed libraries are the ones in the chain. The
programs are never run. Flags used above:

- `<elf flags>`: `-Os -s -nostdlib -Wl,-z,noseparate-code -Wl,-z,max-page-size=16 -Wl,-z,common-page-size=16 -Wl,--hash-style=gnu -Wl,--build-id=none -Wl,-z,norelro -Wl,--no-eh-frame-hdr -fno-asynchronous-unwind-tables -fno-unwind-tables` (small files; the names are unaffected)
- `<pe flags>`: `-Os -s -nostdlib -fno-asynchronous-unwind-tables -fno-unwind-tables -Wl,--gc-sections,--file-alignment,512,--section-alignment,512,--no-insert-timestamp`

No sample has a delay-load import table, because the mingw-w64 linker cannot
write one; tests build those bytes in code (see `src/dep/DELAYLOAD.txt`).

Total size of the samples: 228848 bytes (budget 262144; raised from 131072 to make
room for the eight Mach-O samples added for the signing work, 116912 bytes, which
together with the three existing ones are 170616 bytes; earlier: raised from 102400
to 131072 for the ten `dep/` samples, 22840 bytes).

The first three Mach-O files (`tiny-macho-x86_64`, `-arm64`, `-universal`) are
unsigned (`-no_adhoc_codesign`), so signer tests start from a file with no
signature. They are genuine linker output and are unchanged by the later
samples. The `-adhoc` samples carry the signature `ld64.lld` writes itself (a
different producer from this library), `-nospace` and `-exactfit` differ from
the unsigned x86-64 base only in the header space left after the load commands
(0 and 16 bytes, where the base has 32), `-universal64` is the same two slices
as `-universal` behind a 64-bit offset table, and `-dylib-arm64` is a library.
Only `tiny-macho-x86_64-data-after-sig` is synthetic. Big-endian and 32-bit
programs cannot be made with `ld64.lld` 21, so the tests build header-only
synthetic images in code (`tests/MachOReference.hpp`); they are never stored here.

`check_pages.py` is the independent checker (Python standard library only, no
library code): it walks the header table and slice table of a Mach-O file,
recomputes every 4096-byte page hash and the requirements special slot of the
CodeDirectory and checks the structure facts of a signed program.
`macho-reference.txt` records its output (and `llvm-lipo -archs`) for every
sample; it is written by `regenerate.sh --reference macho-reference.txt` and
recomputed by `regenerate.sh --verify`. The Mach-O samples alone are rebuilt
with `regenerate.sh --macho`, which leaves the Windows samples (and the
installer, whose hash changes on every rebuild) untouched. The installer embeds its creation time, so
rebuilding it changes its hash; rebuild only to replace a sample on purpose.

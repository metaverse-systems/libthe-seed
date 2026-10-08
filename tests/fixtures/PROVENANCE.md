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
| tiny-v4.msi | make_cfb.py (SYNTHETIC) | Python 3.14.8 | `python3 make_cfb.py --convert tiny.msi tiny-v4.msi` | tiny.msi | MIT (project) | 24576 | ok: SYNTHETIC container (the same storages, streams, class identifiers, state bits and times as tiny.msi in 4,096-byte sectors, format version 4); `file` reports MSI Installer; osslsigncode signs a copy and its calculated digest equals the one for tiny.msi (it accepts version 4); the independent reader of `make_cfb.py --check` parses it; `gsf list` lists 19 entries |
| tiny-osslsig-small.msi | osslsigncode | 2.14 | `osslsigncode sign -h sha256 -certs c.pem -key k.pem -in tiny.msi -out tiny-osslsig-small.msi` (throw-away RSA certificate (`openssl req -x509 -newkey rsa:2048 -nodes -subj /CN=sample-check -days 2`, key discarded)) | tiny.msi | MIT (project); the signature is made with a throw-away certificate | 12288 | ok: `file` reports MSI Installer; osslsigncode accepts the unsigned base, and stored and calculated digests match; `msi-open.exe` under Wine finds `\005DigitalSignature` by name and reads its 1,444 bytes (in the mini stream); `gsf list` lists 20 entries with a 1,444-byte DigitalSignature |
| tiny-osslsig-large.msi | osslsigncode | 2.14 | `osslsigncode sign -h sha256 -certs c.pem -key k.pem -addUnauthenticatedBlob -blobFile blob.bin -in tiny.msi -out tiny-osslsig-large.msi`; blob.bin is `python3 make_cfb.py --blob osslsig-large-unauth 6000 blob.bin` | tiny.msi | MIT (project); the signature is made with a throw-away certificate | 18432 | ok: `file` reports MSI Installer; stored and calculated digests match; the signature stream is 7,473 bytes, well above the 4,096-byte cut-off, so it is in ordinary sectors; `msi-open.exe` under Wine reads it equal; `gsf list` lists 20 entries with a 7,473-byte DigitalSignature |
| tiny-osslsig-dse.msi | osslsigncode | 2.14 | `osslsigncode sign -h sha256 -certs c.pem -key k.pem -add-msi-dse -in tiny.msi -out tiny-osslsig-dse.msi` | tiny.msi | MIT (project); the signature is made with a throw-away certificate | 12288 | ok: `file` reports MSI Installer; has a `\005MsiDigitalSignatureEx` stream; osslsigncode: stored and calculated digests and extended digests match (the stored digest differs from the content fingerprint because it covers the extended stream); `msi-open.exe` under Wine reads the signature equal; `gsf list` lists 21 entries |
| nested.msi | make_cfb.py (SYNTHETIC) | Python 3.14.8 | `python3 make_cfb.py --nested tiny.msi nested.msi` | tiny.msi | MIT (project) | 18432 | ok: SYNTHETIC shape only: tiny.msi plus two storages one level deep with non-zero class identifiers, state bits and times, a case-only pair (`Inner`/`INNER`, and `inner` in the other storage), `a`/`B` (raw byte order differs from case-folded order), `Data`/`DataExtra` (prefix), a 5,000-byte stream and an empty one; `file` reports MSI Installer; osslsigncode signs a copy and verifies it (see nested-osslsig.msi); the independent reader of `make_cfb.py --check` parses it; `gsf list` lists 32 entries. Storages two deep are not stored: osslsigncode 2.14 writes them wrongly |
| nested-osslsig.msi | osslsigncode | 2.14 | `osslsigncode sign -h sha256 -certs c.pem -key k.pem -in nested.msi -out nested-osslsig.msi` (throw-away RSA certificate (`openssl req -x509 -newkey rsa:2048 -nodes -subj /CN=sample-check -days 2`, key discarded)) | nested.msi | MIT (project); the signature is made with a throw-away certificate | 20480 | ok: `file` reports MSI Installer; stored and calculated digests match; `msi-open.exe` under Wine reads the signature equal; `gsf list` lists 33 entries with a 1,444-byte DigitalSignature |
| two-neighbours.msi | make_cfb.py (SYNTHETIC) | Python 3.14.8 | `python3 make_cfb.py --extra-neighbours tiny.msi extra.msi`, `osslsigncode sign -h sha256 -certs c.pem -key k.pem -in extra.msi -out extra-signed.msi`, then `python3 make_cfb.py --two-neighbours tiny.msi extra-signed.msi two-neighbours.msi` | tiny.msi, osslsigncode's signature | MIT (project) | 15872 | ok: SYNTHETIC shape only (no signed sample osslsigncode wrote has the signature entry with both a left and a right neighbour, so this one is built): tiny.msi plus fourteen long-named streams, signed by osslsigncode, with the signature stream put at the top of the root's search tree; the independent reader of `make_cfb.py --check` reports the entry with a left and a right neighbour, each with subtrees; `file` reports MSI Installer; osslsigncode: stored and calculated digests match; `msi-open.exe` under Wine reads the signature equal; `gsf list` lists 34 entries |
| legacy-the-seed-0.6.0.msi | libthe-seed 0.6.0 (LEGACY) | commit 6a70071 | `git archive 6a70071 src include external/picosha2.h`, extract, `g++ -std=c++20 -O1 -Iinclude -Isrc legacy_sign.cpp src/MsiSigner.cpp src/internal/FileIO.cpp -o legacy_sign`, then `cp tiny.msi legacy.msi && ./legacy_sign legacy.msi blob.bin`; `legacy_sign.cpp` calls `MsiSigner::EmbedSignature(argv[1], blob read from argv[2])`; blob.bin is `python3 make_cfb.py --blob legacy-fixed 1426 blob.bin` | tiny.msi | MIT (project) | 12288 | LEGACY, damaged on purpose: a package signed by the earlier version (1,426-byte signature written to ordinary sectors, entry not linked into the directory tree). `file` reports MSI Installer; `gsf list` warns that the small-block file has insufficient blocks and lists 19 entries (no DigitalSignature); osslsigncode reports "Failed to get a next mini sector address"; `msi-open.exe` under Wine opens the package and gets STG_E_FILENOTFOUND (0x80030002) for the stream. Built once, not rebuilt by `regenerate.sh` |
| msi-open.exe | x86_64-w64-mingw32-gcc | GCC 16-posix | `x86_64-w64-mingw32-gcc -Os -s -nostdlib -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables -Wl,--gc-sections,--file-alignment,512,--no-insert-timestamp -Wl,-e,entry -o msi-open.exe src/msi_open.c -lole32 -luuid -lshell32 -lkernel32` | src/msi_open.c | MIT (project); no mingw-w64 runtime code (linked without the C runtime) | 4096 | ok: `file` reports PE32+ console x86-64; under Wine 10 it opens each signed sample above with `StgOpenStorageEx`, finds `\005DigitalSignature` by name with `OpenStream` and compares it with the stream bytes (exit status 0); on the legacy sample it exits with status 2 (stream not found) |
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

Total size of the samples: 367600 bytes (budget 393216; raised from 262144 for the
nine files added for the installer signing work, 138752 bytes: tiny-v4.msi 24576,
tiny-osslsig-small.msi 12288, tiny-osslsig-large.msi 18432, tiny-osslsig-dse.msi 12288,
nested.msi 18432, nested-osslsig.msi 20480, two-neighbours.msi 15872,
legacy-the-seed-0.6.0.msi 12288 and msi-open.exe 4096; earlier: 228848 bytes with budget
262144, raised from 131072 to make
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

## Installer samples

The installer samples are all derived from `tiny.msi` (project-authored, MIT, from
`src/tiny.wxs`) and none of them is a Microsoft package: the package used in the
review of the earlier signing code is not redistributed. `msi-reference.txt` is not
a sample. It records, for each installer sample, the fingerprint (osslsigncode's
"Calculated DigitalSignature" for the sample with its signature removed), the entries
and signature size `gsf list` shows, whether `file` reports an installer, the digests
held in the signatures osslsigncode wrote, and the deterministic blobs used. It is
written by `regenerate.sh --msi` and recomputed and compared by `regenerate.sh --verify`
(tool versions and date are in its header). The signed samples are rewritten with a
new signature (and new hashes) on every `--msi`; the recorded fingerprints do not change.

SYNTHETIC (`make_cfb.py`) means the container was written by the script in this folder,
not by an installer tool. Those samples give shapes the real tools do not write
(sector size 4,096, nested storages, a signature entry with two neighbours); they are
used for shape only and never as evidence of what real tools produce.

- Version 4: osslsigncode 2.14 accepts `tiny-v4.msi` and calculates the same fingerprint
  as for the version 3 original, so the sample is used with osslsigncode.
- Fingerprint of the signed samples: the stable value is the one osslsigncode prints
  as "Calculated DigitalSignature". The line "Calculated message digest" changes with
  every signing and is not a fingerprint.
- osslsigncode 2.14 rewrites packages with storages nested two deep wrongly (the output
  loses the inner storage, and `verify` reports "no signature"), so `nested.msi` has
  storages one level deep; deeper shapes are built by the tests.
- No signed sample osslsigncode wrote has a signature entry with both a left and a right
  neighbour (it puts the entry at the end of the root's sequence), hence
  `two-neighbours.msi`.
- The signature of the small sample is 1,444 bytes with this certificate (the size varies
  by a few bytes with the certificate); the large one is 7,473 bytes, well above the
  4,096-byte cut-off; "exactly at the threshold" sizes (4,095, 4,096, 4,097) are
  produced at test time.

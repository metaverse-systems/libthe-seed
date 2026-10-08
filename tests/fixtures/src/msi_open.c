/* Opens an installer package the way the Windows storage functions do, finds
 * the stream "\005DigitalSignature" by name and compares its bytes with a file.
 *
 * Usage: msi-open.exe PACKAGE EXPECTED
 *
 * Exit status: 0 the stream was found and equals EXPECTED, 1 the package could
 * not be opened, 2 the stream was not found by name, 3 the stream could not be
 * read, 4 the stream differs from EXPECTED, 5 bad arguments or EXPECTED could
 * not be read. Built with x86_64-w64-mingw32-gcc (see regenerate.sh); run under
 * Wine in the tests. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

/* No C runtime: the program is linked with -nostdlib so that it stays small. */

static HANDLE out_handle;
static unsigned char chunk[4096];

static void put(const char *text)
{
    DWORD wrote;
    int n = 0;
    while (text[n] != '\0')
        n++;
    WriteFile(out_handle, text, (DWORD)n, &wrote, NULL);
}

static void put_hex(const char *label, unsigned long value)
{
    char buf[11];
    int i;
    put(label);
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++)
        buf[2 + i] = "0123456789abcdef"[(value >> (28 - 4 * i)) & 15];
    buf[10] = '\0';
    put(buf);
    put("\n");
}

static void put_decimal(unsigned long value)
{
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    put(buf + i);
}

static int load_expected(const wchar_t *path, unsigned char **bytes, DWORD *size)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD length, got = 0;
    unsigned char *buf;
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    length = GetFileSize(f, NULL);
    buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, length != 0 ? length : 1);
    if (length == INVALID_FILE_SIZE || buf == NULL || !ReadFile(f, buf, length, &got, NULL) ||
        got != length) {
        CloseHandle(f);
        return 0;
    }
    CloseHandle(f);
    *bytes = buf;
    *size = length;
    return 1;
}

static int same(const unsigned char *a, const unsigned char *b, ULONG n)
{
    ULONG i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int run(void)
{
    IStorage *storage = NULL;
    IStream *stream = NULL;
    unsigned char *expected = NULL;
    DWORD expected_size = 0, offset = 0;
    HRESULT hr;
    ULONG got = 0;
    int result = 0, argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (argv == NULL || argc != 3 || !load_expected(argv[2], &expected, &expected_size)) {
        put("usage: msi-open PACKAGE EXPECTED\n");
        return 5;
    }
    CoInitialize(NULL);
    hr = StgOpenStorageEx(argv[1], STGM_READ | STGM_SHARE_EXCLUSIVE, STGFMT_STORAGE, 0, NULL, NULL,
                          &IID_IStorage, (void **)&storage);
    put_hex("StgOpenStorageEx hr=", (unsigned long)hr);
    if (FAILED(hr))
        return 1;
    hr = IStorage_OpenStream(storage, L"\005DigitalSignature", NULL,
                             STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stream);
    put_hex("OpenStream hr=", (unsigned long)hr);
    if (FAILED(hr))
        return 2;
    for (;;) {
        hr = IStream_Read(stream, chunk, (ULONG)sizeof chunk, &got);
        if (FAILED(hr)) {
            put_hex("Read hr=", (unsigned long)hr);
            result = 3;
            break;
        }
        if (got == 0)
            break;
        if (offset + got > expected_size || !same(chunk, expected + offset, got))
            result = 4;
        offset += got;
    }
    if (result == 0 && offset != expected_size)
        result = 4;
    put("read ");
    put_decimal(offset);
    put(" bytes, expected ");
    put_decimal(expected_size);
    put(result == 0 ? ": equal\n" : ": different\n");
    IStream_Release(stream);
    IStorage_Release(storage);
    return result;
}

void entry(void)
{
    ExitProcess((UINT)run());
}

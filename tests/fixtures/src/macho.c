/* Smallest useful Mach-O program: exits with status 0. Built for x86_64 and
 * arm64 and linked against the stub in libSystem.tbd, so no macOS SDK is
 * needed. */
int main(void)
{
    return 0;
}

/* A second program that needs only libfoo directly, so that two programs
 * share the whole chain. Never run. */
int foo_value(void);

void _start(void)
{
    for(;;)
    {
        foo_value();
    }
}

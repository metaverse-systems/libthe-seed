/* A program that needs only libfoo directly. Never run: it only has to carry
 * the right list of needed libraries. */
int foo_value(void);

void _start(void)
{
    foo_value();
    for(;;)
    {
    }
}

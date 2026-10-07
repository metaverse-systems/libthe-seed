/* Needs libbar, and through it libbaz. */
int bar_value(void);

int foo_value(void)
{
    return bar_value() + 1;
}

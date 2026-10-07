/* Needs libbaz. */
int baz_value(void);

int bar_value(void)
{
    return baz_value() + 2;
}

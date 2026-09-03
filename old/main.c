int main(void)
{
    test_ss_new();
    test_ss_append();
    test_ss_free();
    test_ss_from_cstr();
    test_ss_equals();
    test_ss_find();
    test_ss_substring();
    test_ss_trim();
    test_ss_insert(); -- descomentar cuando este lista

    printf("\n%d/%d tests pasaron\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

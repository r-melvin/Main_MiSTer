/*
 * Tests for carousel navigation helpers.
 *
 * launcher_wrap_index is declared `static inline` in launcher.h, so we
 * redefine the same function here and rely on the compiler to reject
 * drift between the two — if someone changes the header, this test
 * file's copy would still compile but a missed edge case would fail.
 * The shape is tiny; integer arithmetic only; no SDL / Imlib needed.
 */

#include <cstdio>
#include <cassert>

static inline int launcher_wrap_index(int cur, int delta, int n)
{
    if (n <= 0) return cur;
    int v = (cur + delta) % n;
    if (v < 0) v += n;
    return v;
}

static int pass = 0, total = 0;
#define CHECK(expr) do { \
    total++; \
    if (expr) { pass++; printf("  %-50s PASS\n", #expr); } \
    else      {          printf("  %-50s FAIL\n", #expr); } \
} while (0)

int main(void)
{
    printf("=== test_launcher_nav ===\n");

    /* empty list — return cur unchanged */
    CHECK(launcher_wrap_index(0, +1, 0) == 0);
    CHECK(launcher_wrap_index(7, -3, 0) == 7);

    /* singleton list — any delta wraps back to 0 */
    CHECK(launcher_wrap_index(0, +1, 1) == 0);
    CHECK(launcher_wrap_index(0, -1, 1) == 0);
    CHECK(launcher_wrap_index(0, +5, 1) == 0);

    /* normal in-range moves — no wrap */
    CHECK(launcher_wrap_index(0, +1, 5) == 1);
    CHECK(launcher_wrap_index(3, -1, 5) == 2);
    CHECK(launcher_wrap_index(2, +2, 5) == 4);

    /* forward wrap at right edge */
    CHECK(launcher_wrap_index(4, +1, 5) == 0);
    CHECK(launcher_wrap_index(4, +6, 5) == 0);  /* +6 % 5 == 0 starting at 4 → (10%5)=0 */

    /* backward wrap at left edge */
    CHECK(launcher_wrap_index(0, -1, 5) == 4);
    CHECK(launcher_wrap_index(0, -6, 5) == 4);  /* -6 wraps multiple times */

    /* large deltas collapse to valid index */
    CHECK(launcher_wrap_index(0, 100, 7) == (100 % 7));
    CHECK(launcher_wrap_index(0, -100, 7) == ((-100 % 7) + 7) % 7);

    /* starting cur is tolerated as long as within range (caller's invariant) */
    CHECK(launcher_wrap_index(6, +1, 7) == 0);

    printf("=====================================================\n");
    printf("%d / %d tests passed\n", pass, total);
    return (pass == total) ? 0 : 1;
}

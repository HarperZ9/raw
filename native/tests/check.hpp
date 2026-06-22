#pragma once
#include <cmath>
#include <cstdio>
inline int& raw_test_failures() { static int f = 0; return f; }
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    ++raw_test_failures(); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double da=(a), db=(b); \
    if (std::fabs(da-db) > (eps)) { \
    std::printf("CHECK_NEAR failed: %s=%g vs %s=%g eps=%g (%s:%d)\n", \
    #a, da, #b, db, (double)(eps), __FILE__, __LINE__); \
    ++raw_test_failures(); } } while (0)
inline int raw_test_summary() {
    if (raw_test_failures() == 0) { std::printf("OK\n"); return 0; }
    std::printf("%d CHECK(s) failed\n", raw_test_failures()); return 1;
}

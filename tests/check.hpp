// SPDX-License-Identifier: MIT
//
// The tests' one assertion, as the openrac-mods tests have it: a failed CHECK prints where and
// stops the test with a non-zero status, which ctest reports.

#pragma once

#include <cstdio>
#include <cstdlib>

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

#define RUN(test)                                                                                  \
    do {                                                                                           \
        test();                                                                                    \
        std::printf("ok %s\n", #test);                                                             \
    } while (0)

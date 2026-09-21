#pragma once
// Минимальный шим <intrin.h> для g++ (MSVC-специфика в коде мода):
// AudioRedirect.cpp использует только _BitScanForward.
#ifndef _BitScanForward
#define _BitScanForward(index, value) (*(index) = (unsigned long)__builtin_ctz(value))
#endif

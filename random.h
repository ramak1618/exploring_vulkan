#include "pcg/pcg_basic.h"

static float uniform_unit_float(pcg32_random_t* gen) {

    // 23 random bits for mantissa
    uint32_t mantissa = pcg32_boundedrand_r(gen, 1 << 23);

    // set true exponent as 0 to get numbers in range [1.0, 2.0)
    uint32_t val = 0x3F800000U | mantissa;

    union {
        uint32_t as_int32;
        float as_float;
    } converter;
    converter.as_int32 = val;

    // subtract 1 to get numbers in range [0.0, 1.0)
    return converter.as_float - 1.f;
}

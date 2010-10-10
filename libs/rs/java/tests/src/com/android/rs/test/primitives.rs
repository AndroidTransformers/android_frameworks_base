#include "shared.rsh"

#pragma rs export_func(primitives_test)

// Testing primitive types
float floatTest = 1.99f;
double doubleTest = 2.05;
char charTest = -8;
short shortTest = -16;
int intTest = -32;
long longTest = 17179869184l; // 1 << 34
long long longlongTest = 68719476736l; // 1 << 36

uchar ucharTest = 8;
ushort ushortTest = 16;
uint uintTest = 32;
int64_t int64_tTest = -17179869184l; // - 1 << 34

static bool test_primitive_types(uint32_t index) {
    bool failed = false;
    start();

    _RS_ASSERT(floatTest == 1.99f);
    _RS_ASSERT(doubleTest == 2.05);
    _RS_ASSERT(charTest == -8);
    _RS_ASSERT(shortTest == -16);
    _RS_ASSERT(intTest == -32);
    _RS_ASSERT(longTest == 17179869184l);
    _RS_ASSERT(longlongTest == 68719476736l);

    _RS_ASSERT(ucharTest == 8);
    _RS_ASSERT(ushortTest == 16);
    _RS_ASSERT(uintTest == 32);
    _RS_ASSERT(int64_tTest == -17179869184l);

    float time = end(index);

    if (failed) {
        rsDebug("test_primitives FAILED", time);
    }
    else {
        rsDebug("test_primitives PASSED", time);
    }

    return failed;
}

void primitives_test(uint32_t index, int test_num) {
    bool failed = false;
    failed |= test_primitive_types(index);

    if (failed) {
        rsSendToClientBlocking(RS_MSG_TEST_FAILED);
    }
    else {
        rsSendToClientBlocking(RS_MSG_TEST_PASSED);
    }
}


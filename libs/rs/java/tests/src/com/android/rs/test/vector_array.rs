// Copyright (C) 2009 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "shared.rsh"

#pragma rs export_func(vector_array_test)

typedef struct {
    float3 arr[2];
} float3Struct;

float3Struct f;

bool size_test() {
    bool failed = false;
    int expectedSize = 2 * 3 * (int) sizeof(float);
    int actualSize = (int) sizeof(f);

    rsDebug("Size of struct { float3 arr[2]; } (expected):", expectedSize);
    rsDebug("Size of struct { float3 arr[2]; } (actual)  :", actualSize);

    if (expectedSize != actualSize) {
        failed = true;
    }

    return failed;
}

void vector_array_test() {
    bool failed = false;
    failed |= size_test();

    if (failed) {
        rsSendToClientBlocking(RS_MSG_TEST_FAILED);
    }
    else {
        rsSendToClientBlocking(RS_MSG_TEST_PASSED);
    }
}


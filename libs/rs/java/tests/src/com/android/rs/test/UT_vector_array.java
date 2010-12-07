/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.rs.test;

import android.content.res.Resources;
import android.renderscript.*;

public class UT_vector_array extends UnitTest {
    private Resources mRes;

    protected UT_vector_array(RSTestCore rstc, Resources res) {
        super(rstc, "Vector Array");
        mRes = res;
    }

    public void run() {
        RenderScript pRS = RenderScript.create();
        ScriptC_vector_array s = new ScriptC_vector_array(pRS, mRes, R.raw.vector_array);
        pRS.setMessageHandler(mRsMessage);
        s.invoke_vector_array_test();
        pRS.finish();
        waitForMessage();
        pRS.destroy();
    }
}


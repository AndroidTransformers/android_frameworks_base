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

package android.media;

import android.database.AbstractWindowedCursor;
import android.database.CursorWindow;
import android.provider.Ptp;
import android.util.Log;

import java.util.HashMap;

/**
  * Cursor class for PTP content provider
  * @hide
  */
public final class PtpCursor extends AbstractWindowedCursor {
    static final String TAG = "PtpCursor";
    static final int NO_COUNT = -1;

    /* constants for queryType */
    public static final int DEVICE              = 1;
    public static final int DEVICE_ID           = 2;
    public static final int STORAGE             = 3;
    public static final int STORAGE_ID          = 4;
    public static final int OBJECT              = 5;
    public static final int OBJECT_ID           = 6;
    public static final int STORAGE_CHILDREN    = 7;
    public static final int OBJECT_CHILDREN     = 8;
    public static final int OBJECT_IMPORT       = 9;

    /** The names of the columns in the projection */
    private String[] mColumns;

    /** The number of rows in the cursor */
    private int mCount = NO_COUNT;


    public PtpCursor(PtpClient client, int queryType, int deviceID, long storageID, long objectID,
            String[] projection) {
        if (client == null) {
            throw new NullPointerException("client null in PtpCursor constructor");
        }
        mColumns = projection;

        HashMap<String, Integer> map;
        switch (queryType) {
            case DEVICE:
            case DEVICE_ID:
                map = sDeviceProjectionMap;
                break;
            case STORAGE:
            case STORAGE_ID:
                map = sStorageProjectionMap;
                break;
            case OBJECT:
            case OBJECT_ID:
            case STORAGE_CHILDREN:
            case OBJECT_CHILDREN:
                map = sObjectProjectionMap;
                break;
            default:
                throw new IllegalArgumentException("unknown query type "  + queryType);
        }

        int[] columns = new int[projection.length];
        for (int i = 0; i < projection.length; i++) {
            Integer id = map.get(projection[i]);
            if (id == null) {
                throw new IllegalArgumentException("unknown column "  + projection[i]);
            }
            columns[i] = id.intValue();
        }
        native_setup(client, queryType, deviceID, storageID, objectID, columns);
    }

    @Override
    protected void finalize() {
        try {
            native_finalize();
        } finally {
            super.finalize();
        }
    }

    @Override
    public int getCount() {
        if (mCount == NO_COUNT) {
            fillWindow(0);
        }
        return mCount;
    }

    @Override
    public boolean requery() {
        Log.d(TAG, "requery");
        mCount = NO_COUNT;
        if (mWindow != null) {
            mWindow.clear();
        }
        return super.requery();
    }

    private void fillWindow(int startPos) {
        if (mWindow == null) {
            // If there isn't a window set already it will only be accessed locally
            mWindow = new CursorWindow(true /* the window is local only */);
        } else {
                mWindow.clear();
        }
        mWindow.setStartPosition(startPos);
        mCount = native_fill_window(mWindow, startPos);
    }

    @Override
    public String[] getColumnNames() {
        Log.d(TAG, "getColumnNames returning " + mColumns);
        return mColumns;
    }

    /* Device Column IDs */
     /* These must match the values in PtpCursor.cpp */
   private static final int DEVICE_ROW_ID          = 1;
    private static final int DEVICE_MANUFACTURER    = 2;
    private static final int DEVICE_MODEL           = 3;

    /* Storage Column IDs */
    /* These must match the values in PtpCursor.cpp */
    private static final int STORAGE_ROW_ID         = 101;
    private static final int STORAGE_IDENTIFIER     = 102;
    private static final int STORAGE_DESCRIPTION    = 103;

    /* Object Column IDs */
    /* These must match the values in PtpCursor.cpp */
    private static final int OBJECT_ROW_ID              = 201;
    private static final int OBJECT_STORAGE_ID          = 202;
    private static final int OBJECT_FORMAT              = 203;
    private static final int OBJECT_PROTECTION_STATUS   = 204;
    private static final int OBJECT_SIZE                = 205;
    private static final int OBJECT_THUMB_FORMAT        = 206;
    private static final int OBJECT_THUMB_SIZE          = 207;
    private static final int OBJECT_THUMB_WIDTH         = 208;
    private static final int OBJECT_THUMB_HEIGHT        = 209;
    private static final int OBJECT_IMAGE_WIDTH         = 210;
    private static final int OBJECT_IMAGE_HEIGHT        = 211;
    private static final int OBJECT_IMAGE_DEPTH         = 212;
    private static final int OBJECT_PARENT              = 213;
    private static final int OBJECT_ASSOCIATION_TYPE    = 214;
    private static final int OBJECT_ASSOCIATION_DESC    = 215;
    private static final int OBJECT_SEQUENCE_NUMBER     = 216;
    private static final int OBJECT_NAME                = 217;
    private static final int OBJECT_DATE_CREATED        = 218;
    private static final int OBJECT_DATE_MODIFIED       = 219;
    private static final int OBJECT_KEYWORDS            = 220;
    private static final int OBJECT_THUMB               = 221;

    private static HashMap<String, Integer> sDeviceProjectionMap;
    private static HashMap<String, Integer> sStorageProjectionMap;
    private static HashMap<String, Integer> sObjectProjectionMap;

    static {
        sDeviceProjectionMap = new HashMap<String, Integer>();
        sDeviceProjectionMap.put(Ptp.Device._ID, new Integer(DEVICE_ROW_ID));
        sDeviceProjectionMap.put(Ptp.Device.MANUFACTURER, new Integer(DEVICE_MANUFACTURER));
        sDeviceProjectionMap.put(Ptp.Device.MODEL, new Integer(DEVICE_MODEL));

        sStorageProjectionMap = new HashMap<String, Integer>();
        sStorageProjectionMap.put(Ptp.Storage._ID, new Integer(STORAGE_ROW_ID));
        sStorageProjectionMap.put(Ptp.Storage.IDENTIFIER, new Integer(STORAGE_IDENTIFIER));
        sStorageProjectionMap.put(Ptp.Storage.DESCRIPTION, new Integer(STORAGE_DESCRIPTION));

        sObjectProjectionMap = new HashMap<String, Integer>();
        sObjectProjectionMap.put(Ptp.Object._ID, new Integer(OBJECT_ROW_ID));
        sObjectProjectionMap.put(Ptp.Object.STORAGE_ID, new Integer(OBJECT_STORAGE_ID));
        sObjectProjectionMap.put(Ptp.Object.FORMAT, new Integer(OBJECT_FORMAT));
        sObjectProjectionMap.put(Ptp.Object.PROTECTION_STATUS, new Integer(OBJECT_PROTECTION_STATUS));
        sObjectProjectionMap.put(Ptp.Object.SIZE, new Integer(OBJECT_SIZE));
        sObjectProjectionMap.put(Ptp.Object.THUMB_FORMAT, new Integer(OBJECT_THUMB_FORMAT));
        sObjectProjectionMap.put(Ptp.Object.THUMB_SIZE, new Integer(OBJECT_THUMB_SIZE));
        sObjectProjectionMap.put(Ptp.Object.THUMB_WIDTH, new Integer(OBJECT_THUMB_WIDTH));
        sObjectProjectionMap.put(Ptp.Object.THUMB_HEIGHT, new Integer(OBJECT_THUMB_HEIGHT));
        sObjectProjectionMap.put(Ptp.Object.IMAGE_WIDTH, new Integer(OBJECT_IMAGE_WIDTH));
        sObjectProjectionMap.put(Ptp.Object.IMAGE_HEIGHT, new Integer(OBJECT_IMAGE_HEIGHT));
        sObjectProjectionMap.put(Ptp.Object.IMAGE_DEPTH, new Integer(OBJECT_IMAGE_DEPTH));
        sObjectProjectionMap.put(Ptp.Object.PARENT, new Integer(OBJECT_PARENT));
        sObjectProjectionMap.put(Ptp.Object.ASSOCIATION_TYPE, new Integer(OBJECT_ASSOCIATION_TYPE));
        sObjectProjectionMap.put(Ptp.Object.ASSOCIATION_DESC, new Integer(OBJECT_ASSOCIATION_DESC));
        sObjectProjectionMap.put(Ptp.Object.SEQUENCE_NUMBER, new Integer(OBJECT_SEQUENCE_NUMBER));
        sObjectProjectionMap.put(Ptp.Object.NAME, new Integer(OBJECT_NAME));
        sObjectProjectionMap.put(Ptp.Object.DATE_CREATED, new Integer(OBJECT_DATE_CREATED));
        sObjectProjectionMap.put(Ptp.Object.DATE_MODIFIED, new Integer(OBJECT_DATE_MODIFIED));
        sObjectProjectionMap.put(Ptp.Object.KEYWORDS, new Integer(OBJECT_KEYWORDS));
        sObjectProjectionMap.put(Ptp.Object.THUMB, new Integer(OBJECT_THUMB));

        sObjectProjectionMap.put(Ptp.Object.NAME, new Integer(OBJECT_NAME));
    }

    // used by the JNI code
    private int mNativeContext;

    private native final void native_setup(PtpClient client, int queryType,
            int deviceID, long storageID, long objectID, int[] columns);
    private native final void native_finalize();
    private native void native_wait_for_event();
    private native int native_fill_window(CursorWindow window, int startPos);
}

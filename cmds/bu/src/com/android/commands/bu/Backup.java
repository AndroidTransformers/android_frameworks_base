/*
 * Copyright (C) 2011 The Android Open Source Project
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

package com.android.commands.bu;

import android.app.backup.IBackupManager;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;

import java.io.FileDescriptor;
import java.io.IOException;
import java.util.ArrayList;

public final class Backup {
    static final String TAG = "bu";

    static String[] mArgs;
    int mNextArg;
    IBackupManager mBackupManager;

    public static void main(String[] args) {
        Log.d(TAG, "Beginning: " + args[0]);
        mArgs = args;
        try {
            new Backup().run();
        } catch (Exception e) {
            Log.e(TAG, "Error running backup/restore", e);
        }
        Log.d(TAG, "Finished.");
    }

    public void run() {
        mBackupManager = IBackupManager.Stub.asInterface(ServiceManager.getService("backup"));
        if (mBackupManager == null) {
            Log.e(TAG, "Can't obtain Backup Manager binder");
            return;
        }

        String arg = nextArg();
        if (arg.equals("backup")) {
            doFullBackup();
        } else if (arg.equals("restore")) {
            doFullRestore();
        } else {
            Log.e(TAG, "Invalid operation '" + arg + "'");
        }
    }

    private void doFullBackup() {
        ArrayList<String> packages = new ArrayList<String>();
        boolean saveApks = false;
        boolean saveShared = false;
        boolean doEverything = false;

        String arg;
        while ((arg = nextArg()) != null) {
            if (arg.startsWith("-")) {
                if ("-apk".equals(arg)) {
                    saveApks = true;
                } else if ("-noapk".equals(arg)) {
                    saveApks = false;
                } else if ("-shared".equals(arg)) {
                    saveShared = true;
                } else if ("-noshared".equals(arg)) {
                    saveShared = false;
                } else if ("-all".equals(arg)) {
                    doEverything = true;
                } else {
                    Log.w(TAG, "Unknown backup flag " + arg);
                    continue;
                }
            } else {
                // Not a flag; treat as a package name
                packages.add(arg);
            }
        }

        if (doEverything && packages.size() > 0) {
            Log.w(TAG, "-all passed for backup along with specific package names");
        }

        if (!doEverything && !saveShared && packages.size() == 0) {
            Log.e(TAG, "no backup packages supplied and neither -shared nor -all given");
            return;
        }

        try {
            ParcelFileDescriptor fd = ParcelFileDescriptor.dup(FileDescriptor.out);
            String[] packArray = new String[packages.size()];
            mBackupManager.fullBackup(fd, saveApks, saveShared, doEverything,
                    packages.toArray(packArray));
        } catch (IOException e) {
            Log.e(TAG, "Can't dup out");
        } catch (RemoteException e) {
            Log.e(TAG, "Unable to invoke backup manager for backup");
        }
    }

    private void doFullRestore() {
        // No arguments to restore
        try {
            ParcelFileDescriptor fd = ParcelFileDescriptor.dup(FileDescriptor.in);
            mBackupManager.fullRestore(fd);
        } catch (IOException e) {
            Log.e(TAG, "Can't dup System.in");
        } catch (RemoteException e) {
            Log.e(TAG, "Unable to invoke backup manager for restore");
        }
    }

    private String nextArg() {
        if (mNextArg >= mArgs.length) {
            return null;
        }
        String arg = mArgs[mNextArg];
        mNextArg++;
        return arg;
    }
}
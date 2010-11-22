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

#define LOG_TAG "MtpClientJNI"
#include "utils/Log.h"

#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>

#include "jni.h"
#include "JNIHelp.h"
#include "android_runtime/AndroidRuntime.h"
#include "private/android_filesystem_config.h"

#include "MtpClient.h"
#include "MtpDevice.h"
#include "MtpObjectInfo.h"

using namespace android;

// ----------------------------------------------------------------------------

static jmethodID method_deviceAdded;
static jmethodID method_deviceRemoved;
static jfieldID field_context;

#ifdef HAVE_ANDROID_OS

static void checkAndClearExceptionFromCallback(JNIEnv* env, const char* methodName) {
    if (env->ExceptionCheck()) {
        LOGE("An exception was thrown by callback '%s'.", methodName);
        LOGE_EX(env);
        env->ExceptionClear();
    }
}

class MyClient : public MtpClient {
private:
    virtual void    deviceAdded(MtpDevice *device);
    virtual void    deviceRemoved(MtpDevice *device);

    jobject         mClient;
    MtpDevice*      mEventDevice;

public:
                    MyClient(JNIEnv *env, jobject client);
    void            cleanup(JNIEnv *env);
};

MtpClient* get_client_from_object(JNIEnv* env, jobject javaClient)
{
    return (MtpClient*)env->GetIntField(javaClient, field_context);
}


MyClient::MyClient(JNIEnv *env, jobject client)
    :   mClient(env->NewGlobalRef(client))
{
}

void MyClient::cleanup(JNIEnv *env) {
    env->DeleteGlobalRef(mClient);
}

void MyClient::deviceAdded(MtpDevice *device) {
    JNIEnv* env = AndroidRuntime::getJNIEnv();
    const char* name = device->getDeviceName();
    LOGD("MyClient::deviceAdded %s\n", name);

    env->CallVoidMethod(mClient, method_deviceAdded, device->getID());

    checkAndClearExceptionFromCallback(env, __FUNCTION__);
}

void MyClient::deviceRemoved(MtpDevice *device) {
    JNIEnv* env = AndroidRuntime::getJNIEnv();
    const char* name = device->getDeviceName();
    LOGD("MyClient::deviceRemoved %s\n", name);

    env->CallVoidMethod(mClient, method_deviceRemoved, device->getID());

    checkAndClearExceptionFromCallback(env, __FUNCTION__);
}

#endif // HAVE_ANDROID_OS

// ----------------------------------------------------------------------------

static void
android_media_MtpClient_setup(JNIEnv *env, jobject thiz)
{
#ifdef HAVE_ANDROID_OS
    LOGD("setup\n");
    MyClient* client = new MyClient(env, thiz);
    client->start();
    env->SetIntField(thiz, field_context, (int)client);
#endif
}

static void
android_media_MtpClient_finalize(JNIEnv *env, jobject thiz)
{
#ifdef HAVE_ANDROID_OS
    LOGD("finalize\n");
    MyClient *client = (MyClient *)env->GetIntField(thiz, field_context);
    client->cleanup(env);
    delete client;
    env->SetIntField(thiz, field_context, 0);
#endif
}

static jboolean
android_media_MtpClient_start(JNIEnv *env, jobject thiz)
{
#ifdef HAVE_ANDROID_OS
    LOGD("start\n");
    MyClient *client = (MyClient *)env->GetIntField(thiz, field_context);
    return client->start();
#else
    return false;
#endif
}

static void
android_media_MtpClient_stop(JNIEnv *env, jobject thiz)
{
#ifdef HAVE_ANDROID_OS
    LOGD("stop\n");
    MyClient *client = (MyClient *)env->GetIntField(thiz, field_context);
    client->stop();
#endif
}

static jboolean
android_media_MtpClient_delete_object(JNIEnv *env, jobject thiz,
        jint device_id, jlong object_id)
{
#ifdef HAVE_ANDROID_OS
    MyClient *client = (MyClient *)env->GetIntField(thiz, field_context);
    MtpDevice* device = client->getDevice(device_id);
    if (device)
        return device->deleteObject(object_id);
    else
 #endif
        return NULL;
}

static jlong
android_media_MtpClient_get_parent(JNIEnv *env, jobject thiz,
        jint device_id, jlong object_id)
{
#ifdef HAVE_ANDROID_OS
    MyClient *client = (MyClient *)env->GetIntField(thiz, field_context);
    MtpDevice* device = client->getDevice(device_id);
    if (device)
        return device->getParent(object_id);
    else
#endif
        return -1;
}

static jlong
android_media_MtpClient_get_storage_id(JNIEnv *env, jobject thiz,
        jint device_id, jlong object_id)
{
 #ifdef HAVE_ANDROID_OS
    MyClient *client = (MyClient *)env->GetIntField(thiz, field_context);
    MtpDevice* device = client->getDevice(device_id);
    if (device)
        return device->getStorageID(object_id);
    else
#endif
        return -1;
}

static jboolean
android_media_MtpClient_import_file(JNIEnv *env, jobject thiz,
        jint device_id, jlong object_id, jstring dest_path)
{
#ifdef HAVE_ANDROID_OS
    MyClient *client = (MyClient *)env->GetIntField(thiz, field_context);
    MtpDevice* device = client->getDevice(device_id);
    if (device) {
        const char *destPathStr = env->GetStringUTFChars(dest_path, NULL);
        bool result = device->readObject(object_id, destPathStr, AID_SDCARD_RW, 0664);
        env->ReleaseStringUTFChars(dest_path, destPathStr);
        return result;
    }
#endif
    return NULL;
}

// ----------------------------------------------------------------------------

static JNINativeMethod gMethods[] = {
    {"native_setup",            "()V",  (void *)android_media_MtpClient_setup},
    {"native_finalize",         "()V",  (void *)android_media_MtpClient_finalize},
    {"native_start",            "()Z",  (void *)android_media_MtpClient_start},
    {"native_stop",             "()V",  (void *)android_media_MtpClient_stop},
    {"native_delete_object",   "(IJ)Z", (void *)android_media_MtpClient_delete_object},
    {"native_get_parent",      "(IJ)J", (void *)android_media_MtpClient_get_parent},
    {"native_get_storage_id",  "(IJ)J", (void *)android_media_MtpClient_get_storage_id},
    {"native_import_file",     "(IJLjava/lang/String;)Z",
                                        (void *)android_media_MtpClient_import_file},
};

static const char* const kClassPathName = "android/media/MtpClient";

int register_android_media_MtpClient(JNIEnv *env)
{
    jclass clazz;

    LOGD("register_android_media_MtpClient\n");

    clazz = env->FindClass("android/media/MtpClient");
    if (clazz == NULL) {
        LOGE("Can't find android/media/MtpClient");
        return -1;
    }
    method_deviceAdded = env->GetMethodID(clazz, "deviceAdded", "(I)V");
    if (method_deviceAdded == NULL) {
        LOGE("Can't find deviceAdded");
        return -1;
    }
    method_deviceRemoved = env->GetMethodID(clazz, "deviceRemoved", "(I)V");
    if (method_deviceRemoved == NULL) {
        LOGE("Can't find deviceRemoved");
        return -1;
    }
    field_context = env->GetFieldID(clazz, "mNativeContext", "I");
    if (field_context == NULL) {
        LOGE("Can't find MtpClient.mNativeContext");
        return -1;
    }

    return AndroidRuntime::registerNativeMethods(env,
                "android/media/MtpClient", gMethods, NELEM(gMethods));
}

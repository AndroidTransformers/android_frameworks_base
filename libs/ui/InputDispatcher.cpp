//
// Copyright 2010 The Android Open Source Project
//
// The input dispatcher.
//
#define LOG_TAG "InputDispatcher"

//#define LOG_NDEBUG 0

// Log detailed debug messages about each inbound event notification to the dispatcher.
#define DEBUG_INBOUND_EVENT_DETAILS 0

// Log detailed debug messages about each outbound event processed by the dispatcher.
#define DEBUG_OUTBOUND_EVENT_DETAILS 0

// Log debug messages about batching.
#define DEBUG_BATCHING 0

// Log debug messages about the dispatch cycle.
#define DEBUG_DISPATCH_CYCLE 0

// Log debug messages about registrations.
#define DEBUG_REGISTRATION 0

// Log debug messages about performance statistics.
#define DEBUG_PERFORMANCE_STATISTICS 0

// Log debug messages about input event injection.
#define DEBUG_INJECTION 0

// Log debug messages about input event throttling.
#define DEBUG_THROTTLING 0

// Log debug messages about input focus tracking.
#define DEBUG_FOCUS 0

// Log debug messages about the app switch latency optimization.
#define DEBUG_APP_SWITCH 0

#include <cutils/log.h>
#include <ui/InputDispatcher.h>
#include <ui/PowerManager.h>

#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

namespace android {

// Delay between reporting long touch events to the power manager.
const nsecs_t EVENT_IGNORE_DURATION = 300 * 1000000LL; // 300 ms

// Default input dispatching timeout if there is no focused application or paused window
// from which to determine an appropriate dispatching timeout.
const nsecs_t DEFAULT_INPUT_DISPATCHING_TIMEOUT = 5000 * 1000000LL; // 5 sec

// Amount of time to allow for all pending events to be processed when an app switch
// key is on the way.  This is used to preempt input dispatch and drop input events
// when an application takes too long to respond and the user has pressed an app switch key.
const nsecs_t APP_SWITCH_TIMEOUT = 500 * 1000000LL; // 0.5sec


static inline nsecs_t now() {
    return systemTime(SYSTEM_TIME_MONOTONIC);
}

static inline const char* toString(bool value) {
    return value ? "true" : "false";
}


// --- InputWindow ---

bool InputWindow::visibleFrameIntersects(const InputWindow* other) const {
    return visibleFrameRight > other->visibleFrameLeft
        && visibleFrameLeft < other->visibleFrameRight
        && visibleFrameBottom > other->visibleFrameTop
        && visibleFrameTop < other->visibleFrameBottom;
}

bool InputWindow::touchableAreaContainsPoint(int32_t x, int32_t y) const {
    return x >= touchableAreaLeft && x <= touchableAreaRight
            && y >= touchableAreaTop && y <= touchableAreaBottom;
}


// --- InputDispatcher ---

InputDispatcher::InputDispatcher(const sp<InputDispatcherPolicyInterface>& policy) :
    mPolicy(policy),
    mPendingEvent(NULL), mAppSwitchDueTime(LONG_LONG_MAX),
    mDispatchEnabled(true), mDispatchFrozen(false),
    mFocusedWindow(NULL), mTouchDown(false), mTouchedWindow(NULL),
    mFocusedApplication(NULL),
    mCurrentInputTargetsValid(false),
    mInputTargetWaitCause(INPUT_TARGET_WAIT_CAUSE_NONE) {
    mPollLoop = new PollLoop(false);

    mInboundQueue.headSentinel.refCount = -1;
    mInboundQueue.headSentinel.type = EventEntry::TYPE_SENTINEL;
    mInboundQueue.headSentinel.eventTime = LONG_LONG_MIN;

    mInboundQueue.tailSentinel.refCount = -1;
    mInboundQueue.tailSentinel.type = EventEntry::TYPE_SENTINEL;
    mInboundQueue.tailSentinel.eventTime = LONG_LONG_MAX;

    mKeyRepeatState.lastKeyEntry = NULL;

    int32_t maxEventsPerSecond = policy->getMaxEventsPerSecond();
    mThrottleState.minTimeBetweenEvents = 1000000000LL / maxEventsPerSecond;
    mThrottleState.lastDeviceId = -1;

#if DEBUG_THROTTLING
    mThrottleState.originalSampleCount = 0;
    LOGD("Throttling - Max events per second = %d", maxEventsPerSecond);
#endif
}

InputDispatcher::~InputDispatcher() {
    { // acquire lock
        AutoMutex _l(mLock);

        resetKeyRepeatLocked();
        releasePendingEventLocked(true);
        drainInboundQueueLocked();
    }

    while (mConnectionsByReceiveFd.size() != 0) {
        unregisterInputChannel(mConnectionsByReceiveFd.valueAt(0)->inputChannel);
    }
}

void InputDispatcher::dispatchOnce() {
    nsecs_t keyRepeatTimeout = mPolicy->getKeyRepeatTimeout();
    nsecs_t keyRepeatDelay = mPolicy->getKeyRepeatDelay();

    nsecs_t nextWakeupTime = LONG_LONG_MAX;
    { // acquire lock
        AutoMutex _l(mLock);
        dispatchOnceInnerLocked(keyRepeatTimeout, keyRepeatDelay, & nextWakeupTime);

        if (runCommandsLockedInterruptible()) {
            nextWakeupTime = LONG_LONG_MIN;  // force next poll to wake up immediately
        }
    } // release lock

    // Wait for callback or timeout or wake.  (make sure we round up, not down)
    nsecs_t currentTime = now();
    int32_t timeoutMillis;
    if (nextWakeupTime > currentTime) {
        uint64_t timeout = uint64_t(nextWakeupTime - currentTime);
        timeout = (timeout + 999999LL) / 1000000LL;
        timeoutMillis = timeout > INT_MAX ? -1 : int32_t(timeout);
    } else {
        timeoutMillis = 0;
    }

    mPollLoop->pollOnce(timeoutMillis);
}

void InputDispatcher::dispatchOnceInnerLocked(nsecs_t keyRepeatTimeout,
        nsecs_t keyRepeatDelay, nsecs_t* nextWakeupTime) {
    nsecs_t currentTime = now();

    // Reset the key repeat timer whenever we disallow key events, even if the next event
    // is not a key.  This is to ensure that we abort a key repeat if the device is just coming
    // out of sleep.
    if (keyRepeatTimeout < 0) {
        resetKeyRepeatLocked();
    }

    // If dispatching is disabled, drop all events in the queue.
    if (! mDispatchEnabled) {
        if (mPendingEvent || ! mInboundQueue.isEmpty()) {
            LOGI("Dropping pending events because input dispatch is disabled.");
            releasePendingEventLocked(true);
            drainInboundQueueLocked();
        }
        return;
    }

    // If dispatching is frozen, do not process timeouts or try to deliver any new events.
    if (mDispatchFrozen) {
#if DEBUG_FOCUS
        LOGD("Dispatch frozen.  Waiting some more.");
#endif
        return;
    }

    // Optimize latency of app switches.
    // Essentially we start a short timeout when an app switch key (HOME / ENDCALL) has
    // been pressed.  When it expires, we preempt dispatch and drop all other pending events.
    bool isAppSwitchDue = mAppSwitchDueTime <= currentTime;
    if (mAppSwitchDueTime < *nextWakeupTime) {
        *nextWakeupTime = mAppSwitchDueTime;
    }

    // Detect and process timeouts for all connections and determine if there are any
    // synchronous event dispatches pending.  This step is entirely non-interruptible.
    bool havePendingSyncTarget = false;
    size_t activeConnectionCount = mActiveConnections.size();
    for (size_t i = 0; i < activeConnectionCount; i++) {
        Connection* connection = mActiveConnections.itemAt(i);

        if (connection->hasPendingSyncTarget()) {
            if (isAppSwitchDue) {
                connection->preemptSyncTarget();
            } else {
                havePendingSyncTarget = true;
            }
        }

        nsecs_t connectionTimeoutTime  = connection->nextTimeoutTime;
        if (connectionTimeoutTime <= currentTime) {
            mTimedOutConnections.add(connection);
        } else if (connectionTimeoutTime < *nextWakeupTime) {
            *nextWakeupTime = connectionTimeoutTime;
        }
    }

    size_t timedOutConnectionCount = mTimedOutConnections.size();
    for (size_t i = 0; i < timedOutConnectionCount; i++) {
        Connection* connection = mTimedOutConnections.itemAt(i);
        timeoutDispatchCycleLocked(currentTime, connection);
        *nextWakeupTime = LONG_LONG_MIN; // force next poll to wake up immediately
    }
    mTimedOutConnections.clear();

    // If we have a pending synchronous target, skip dispatch.
    if (havePendingSyncTarget) {
        return;
    }

    // Ready to start a new event.
    // If we don't already have a pending event, go grab one.
    if (! mPendingEvent) {
        if (mInboundQueue.isEmpty()) {
            if (isAppSwitchDue) {
                // The inbound queue is empty so the app switch key we were waiting
                // for will never arrive.  Stop waiting for it.
                resetPendingAppSwitchLocked(false);
                isAppSwitchDue = false;
            }

            // Synthesize a key repeat if appropriate.
            if (mKeyRepeatState.lastKeyEntry) {
                if (currentTime >= mKeyRepeatState.nextRepeatTime) {
                    mPendingEvent = synthesizeKeyRepeatLocked(currentTime, keyRepeatDelay);
                } else {
                    if (mKeyRepeatState.nextRepeatTime < *nextWakeupTime) {
                        *nextWakeupTime = mKeyRepeatState.nextRepeatTime;
                    }
                }
            }
            if (! mPendingEvent) {
                return;
            }
        } else {
            // Inbound queue has at least one entry.
            EventEntry* entry = mInboundQueue.headSentinel.next;

            // Throttle the entry if it is a move event and there are no
            // other events behind it in the queue.  Due to movement batching, additional
            // samples may be appended to this event by the time the throttling timeout
            // expires.
            // TODO Make this smarter and consider throttling per device independently.
            if (entry->type == EventEntry::TYPE_MOTION) {
                MotionEntry* motionEntry = static_cast<MotionEntry*>(entry);
                int32_t deviceId = motionEntry->deviceId;
                uint32_t source = motionEntry->source;
                if (! isAppSwitchDue
                        && motionEntry->next == & mInboundQueue.tailSentinel // exactly one event
                        && motionEntry->action == AMOTION_EVENT_ACTION_MOVE
                        && deviceId == mThrottleState.lastDeviceId
                        && source == mThrottleState.lastSource) {
                    nsecs_t nextTime = mThrottleState.lastEventTime
                            + mThrottleState.minTimeBetweenEvents;
                    if (currentTime < nextTime) {
                        // Throttle it!
#if DEBUG_THROTTLING
                        LOGD("Throttling - Delaying motion event for "
                                "device 0x%x, source 0x%08x by up to %0.3fms.",
                                deviceId, source, (nextTime - currentTime) * 0.000001);
#endif
                        if (nextTime < *nextWakeupTime) {
                            *nextWakeupTime = nextTime;
                        }
                        if (mThrottleState.originalSampleCount == 0) {
                            mThrottleState.originalSampleCount =
                                    motionEntry->countSamples();
                        }
                        return;
                    }
                }

#if DEBUG_THROTTLING
                if (mThrottleState.originalSampleCount != 0) {
                    uint32_t count = motionEntry->countSamples();
                    LOGD("Throttling - Motion event sample count grew by %d from %d to %d.",
                            count - mThrottleState.originalSampleCount,
                            mThrottleState.originalSampleCount, count);
                    mThrottleState.originalSampleCount = 0;
                }
#endif

                mThrottleState.lastEventTime = entry->eventTime < currentTime
                        ? entry->eventTime : currentTime;
                mThrottleState.lastDeviceId = deviceId;
                mThrottleState.lastSource = source;
            }

            mInboundQueue.dequeue(entry);
            mPendingEvent = entry;
        }
    }

    // Now we have an event to dispatch.
    assert(mPendingEvent != NULL);
    bool wasDispatched = false;
    bool wasDropped = false;
    switch (mPendingEvent->type) {
    case EventEntry::TYPE_CONFIGURATION_CHANGED: {
        ConfigurationChangedEntry* typedEntry =
                static_cast<ConfigurationChangedEntry*>(mPendingEvent);
        wasDispatched = dispatchConfigurationChangedLocked(currentTime, typedEntry);
        break;
    }

    case EventEntry::TYPE_KEY: {
        KeyEntry* typedEntry = static_cast<KeyEntry*>(mPendingEvent);
        if (isAppSwitchPendingLocked()) {
            if (isAppSwitchKey(typedEntry->keyCode)) {
                resetPendingAppSwitchLocked(true);
            } else if (isAppSwitchDue) {
                LOGI("Dropping key because of pending overdue app switch.");
                wasDropped = true;
                break;
            }
        }
        wasDispatched = dispatchKeyLocked(currentTime, typedEntry, keyRepeatTimeout,
                nextWakeupTime);
        break;
    }

    case EventEntry::TYPE_MOTION: {
        MotionEntry* typedEntry = static_cast<MotionEntry*>(mPendingEvent);
        if (isAppSwitchDue) {
            LOGI("Dropping motion because of pending overdue app switch.");
            wasDropped = true;
            break;
        }
        wasDispatched = dispatchMotionLocked(currentTime, typedEntry, nextWakeupTime);
        break;
    }

    default:
        assert(false);
        wasDropped = true;
        break;
    }

    if (wasDispatched || wasDropped) {
        releasePendingEventLocked(wasDropped);
        *nextWakeupTime = LONG_LONG_MIN;  // force next poll to wake up immediately
    }
}

bool InputDispatcher::enqueueInboundEventLocked(EventEntry* entry) {
    bool needWake = mInboundQueue.isEmpty();
    mInboundQueue.enqueueAtTail(entry);

    switch (entry->type) {
    case EventEntry::TYPE_KEY:
        needWake |= detectPendingAppSwitchLocked(static_cast<KeyEntry*>(entry));
        break;
    }

    return needWake;
}

bool InputDispatcher::isAppSwitchKey(int32_t keyCode) {
    return keyCode == AKEYCODE_HOME || keyCode == AKEYCODE_ENDCALL;
}

bool InputDispatcher::isAppSwitchPendingLocked() {
    return mAppSwitchDueTime != LONG_LONG_MAX;
}

bool InputDispatcher::detectPendingAppSwitchLocked(KeyEntry* inboundKeyEntry) {
    if (inboundKeyEntry->action == AKEY_EVENT_ACTION_UP
            && ! (inboundKeyEntry->flags & AKEY_EVENT_FLAG_CANCELED)
            && isAppSwitchKey(inboundKeyEntry->keyCode)
            && isEventFromReliableSourceLocked(inboundKeyEntry)) {
#if DEBUG_APP_SWITCH
        LOGD("App switch is pending!");
#endif
        mAppSwitchDueTime = inboundKeyEntry->eventTime + APP_SWITCH_TIMEOUT;
        return true; // need wake
    }
    return false;
}

void InputDispatcher::resetPendingAppSwitchLocked(bool handled) {
    mAppSwitchDueTime = LONG_LONG_MAX;

#if DEBUG_APP_SWITCH
    if (handled) {
        LOGD("App switch has arrived.");
    } else {
        LOGD("App switch was abandoned.");
    }
#endif
}

bool InputDispatcher::runCommandsLockedInterruptible() {
    if (mCommandQueue.isEmpty()) {
        return false;
    }

    do {
        CommandEntry* commandEntry = mCommandQueue.dequeueAtHead();

        Command command = commandEntry->command;
        (this->*command)(commandEntry); // commands are implicitly 'LockedInterruptible'

        commandEntry->connection.clear();
        mAllocator.releaseCommandEntry(commandEntry);
    } while (! mCommandQueue.isEmpty());
    return true;
}

InputDispatcher::CommandEntry* InputDispatcher::postCommandLocked(Command command) {
    CommandEntry* commandEntry = mAllocator.obtainCommandEntry(command);
    mCommandQueue.enqueueAtTail(commandEntry);
    return commandEntry;
}

void InputDispatcher::drainInboundQueueLocked() {
    while (! mInboundQueue.isEmpty()) {
        EventEntry* entry = mInboundQueue.dequeueAtHead();
        releaseInboundEventLocked(entry, true /*wasDropped*/);
    }
}

void InputDispatcher::releasePendingEventLocked(bool wasDropped) {
    if (mPendingEvent) {
        releaseInboundEventLocked(mPendingEvent, wasDropped);
        mPendingEvent = NULL;
    }
}

void InputDispatcher::releaseInboundEventLocked(EventEntry* entry, bool wasDropped) {
    if (wasDropped) {
#if DEBUG_DISPATCH_CYCLE
        LOGD("Pending event was dropped.");
#endif
        setInjectionResultLocked(entry, INPUT_EVENT_INJECTION_FAILED);
    }
    mAllocator.releaseEventEntry(entry);
}

bool InputDispatcher::isEventFromReliableSourceLocked(EventEntry* entry) {
    return ! entry->isInjected()
            || entry->injectorUid == 0
            || mPolicy->checkInjectEventsPermissionNonReentrant(
                    entry->injectorPid, entry->injectorUid);
}

void InputDispatcher::resetKeyRepeatLocked() {
    if (mKeyRepeatState.lastKeyEntry) {
        mAllocator.releaseKeyEntry(mKeyRepeatState.lastKeyEntry);
        mKeyRepeatState.lastKeyEntry = NULL;
    }
}

InputDispatcher::KeyEntry* InputDispatcher::synthesizeKeyRepeatLocked(
        nsecs_t currentTime, nsecs_t keyRepeatDelay) {
    KeyEntry* entry = mKeyRepeatState.lastKeyEntry;

    // Reuse the repeated key entry if it is otherwise unreferenced.
    uint32_t policyFlags = entry->policyFlags & POLICY_FLAG_RAW_MASK;
    if (entry->refCount == 1) {
        entry->recycle();
        entry->eventTime = currentTime;
        entry->policyFlags = policyFlags;
        entry->repeatCount += 1;
    } else {
        KeyEntry* newEntry = mAllocator.obtainKeyEntry(currentTime,
                entry->deviceId, entry->source, policyFlags,
                entry->action, entry->flags, entry->keyCode, entry->scanCode,
                entry->metaState, entry->repeatCount + 1, entry->downTime);

        mKeyRepeatState.lastKeyEntry = newEntry;
        mAllocator.releaseKeyEntry(entry);

        entry = newEntry;
    }
    entry->syntheticRepeat = true;

    // Increment reference count since we keep a reference to the event in
    // mKeyRepeatState.lastKeyEntry in addition to the one we return.
    entry->refCount += 1;

    if (entry->repeatCount == 1) {
        entry->flags |= AKEY_EVENT_FLAG_LONG_PRESS;
    }

    mKeyRepeatState.nextRepeatTime = currentTime + keyRepeatDelay;
    return entry;
}

bool InputDispatcher::dispatchConfigurationChangedLocked(
        nsecs_t currentTime, ConfigurationChangedEntry* entry) {
#if DEBUG_OUTBOUND_EVENT_DETAILS
    LOGD("dispatchConfigurationChanged - eventTime=%lld", entry->eventTime);
#endif

    // Reset key repeating in case a keyboard device was added or removed or something.
    resetKeyRepeatLocked();

    // Enqueue a command to run outside the lock to tell the policy that the configuration changed.
    CommandEntry* commandEntry = postCommandLocked(
            & InputDispatcher::doNotifyConfigurationChangedInterruptible);
    commandEntry->eventTime = entry->eventTime;
    return true;
}

bool InputDispatcher::dispatchKeyLocked(
        nsecs_t currentTime, KeyEntry* entry, nsecs_t keyRepeatTimeout,
        nsecs_t* nextWakeupTime) {
    // Preprocessing.
    if (! entry->dispatchInProgress) {
        logOutboundKeyDetailsLocked("dispatchKey - ", entry);

        if (entry->repeatCount == 0
                && entry->action == AKEY_EVENT_ACTION_DOWN
                && ! entry->isInjected()) {
            if (mKeyRepeatState.lastKeyEntry
                    && mKeyRepeatState.lastKeyEntry->keyCode == entry->keyCode) {
                // We have seen two identical key downs in a row which indicates that the device
                // driver is automatically generating key repeats itself.  We take note of the
                // repeat here, but we disable our own next key repeat timer since it is clear that
                // we will not need to synthesize key repeats ourselves.
                entry->repeatCount = mKeyRepeatState.lastKeyEntry->repeatCount + 1;
                resetKeyRepeatLocked();
                mKeyRepeatState.nextRepeatTime = LONG_LONG_MAX; // don't generate repeats ourselves
            } else {
                // Not a repeat.  Save key down state in case we do see a repeat later.
                resetKeyRepeatLocked();
                mKeyRepeatState.nextRepeatTime = entry->eventTime + keyRepeatTimeout;
            }
            mKeyRepeatState.lastKeyEntry = entry;
            entry->refCount += 1;
        } else if (! entry->syntheticRepeat) {
            resetKeyRepeatLocked();
        }

        entry->dispatchInProgress = true;
        startFindingTargetsLocked();
    }

    // Identify targets.
    if (! mCurrentInputTargetsValid) {
        InputWindow* window = NULL;
        int32_t injectionResult = findFocusedWindowLocked(currentTime,
                entry, nextWakeupTime, & window);
        if (injectionResult == INPUT_EVENT_INJECTION_PENDING) {
            return false;
        }

        setInjectionResultLocked(entry, injectionResult);
        if (injectionResult != INPUT_EVENT_INJECTION_SUCCEEDED) {
            return true;
        }

        addMonitoringTargetsLocked();
        finishFindingTargetsLocked(window);
    }

    // Give the policy a chance to intercept the key.
    if (entry->interceptKeyResult == KeyEntry::INTERCEPT_KEY_RESULT_UNKNOWN) {
        CommandEntry* commandEntry = postCommandLocked(
                & InputDispatcher::doInterceptKeyBeforeDispatchingLockedInterruptible);
        commandEntry->inputChannel = mCurrentInputChannel;
        commandEntry->keyEntry = entry;
        entry->refCount += 1;
        return false; // wait for the command to run
    }
    if (entry->interceptKeyResult == KeyEntry::INTERCEPT_KEY_RESULT_SKIP) {
        return true;
    }

    // Dispatch the key.
    dispatchEventToCurrentInputTargetsLocked(currentTime, entry, false);

    // Poke user activity.
    pokeUserActivityLocked(entry->eventTime, mCurrentInputWindowType, POWER_MANAGER_BUTTON_EVENT);
    return true;
}

void InputDispatcher::logOutboundKeyDetailsLocked(const char* prefix, const KeyEntry* entry) {
#if DEBUG_OUTBOUND_EVENT_DETAILS
    LOGD("%seventTime=%lld, deviceId=0x%x, source=0x%x, policyFlags=0x%x, "
            "action=0x%x, flags=0x%x, keyCode=0x%x, scanCode=0x%x, metaState=0x%x, "
            "downTime=%lld",
            prefix,
            entry->eventTime, entry->deviceId, entry->source, entry->policyFlags,
            entry->action, entry->flags, entry->keyCode, entry->scanCode, entry->metaState,
            entry->downTime);
#endif
}

bool InputDispatcher::dispatchMotionLocked(
        nsecs_t currentTime, MotionEntry* entry, nsecs_t* nextWakeupTime) {
    // Preprocessing.
    if (! entry->dispatchInProgress) {
        logOutboundMotionDetailsLocked("dispatchMotion - ", entry);

        entry->dispatchInProgress = true;
        startFindingTargetsLocked();
    }

    bool isPointerEvent = entry->source & AINPUT_SOURCE_CLASS_POINTER;

    // Identify targets.
    if (! mCurrentInputTargetsValid) {
        InputWindow* window = NULL;
        int32_t injectionResult;
        if (isPointerEvent) {
            // Pointer event.  (eg. touchscreen)
            injectionResult = findTouchedWindowLocked(currentTime,
                    entry, nextWakeupTime, & window);
        } else {
            // Non touch event.  (eg. trackball)
            injectionResult = findFocusedWindowLocked(currentTime,
                    entry, nextWakeupTime, & window);
        }
        if (injectionResult == INPUT_EVENT_INJECTION_PENDING) {
            return false;
        }

        setInjectionResultLocked(entry, injectionResult);
        if (injectionResult != INPUT_EVENT_INJECTION_SUCCEEDED) {
            return true;
        }

        addMonitoringTargetsLocked();
        finishFindingTargetsLocked(window);
    }

    // Dispatch the motion.
    dispatchEventToCurrentInputTargetsLocked(currentTime, entry, false);

    // Poke user activity.
    int32_t eventType;
    if (isPointerEvent) {
        switch (entry->action) {
        case AMOTION_EVENT_ACTION_DOWN:
            eventType = POWER_MANAGER_TOUCH_EVENT;
            break;
        case AMOTION_EVENT_ACTION_UP:
            eventType = POWER_MANAGER_TOUCH_UP_EVENT;
            break;
        default:
            if (entry->eventTime - entry->downTime >= EVENT_IGNORE_DURATION) {
                eventType = POWER_MANAGER_TOUCH_EVENT;
            } else {
                eventType = POWER_MANAGER_LONG_TOUCH_EVENT;
            }
            break;
        }
    } else {
        eventType = POWER_MANAGER_BUTTON_EVENT;
    }
    pokeUserActivityLocked(entry->eventTime, mCurrentInputWindowType, eventType);
    return true;
}


void InputDispatcher::logOutboundMotionDetailsLocked(const char* prefix, const MotionEntry* entry) {
#if DEBUG_OUTBOUND_EVENT_DETAILS
    LOGD("%seventTime=%lld, deviceId=0x%x, source=0x%x, policyFlags=0x%x, "
            "action=0x%x, flags=0x%x, "
            "metaState=0x%x, edgeFlags=0x%x, xPrecision=%f, yPrecision=%f, downTime=%lld",
            prefix,
            entry->eventTime, entry->deviceId, entry->source, entry->policyFlags,
            entry->action, entry->flags,
            entry->metaState, entry->edgeFlags, entry->xPrecision, entry->yPrecision,
            entry->downTime);

    // Print the most recent sample that we have available, this may change due to batching.
    size_t sampleCount = 1;
    const MotionSample* sample = & entry->firstSample;
    for (; sample->next != NULL; sample = sample->next) {
        sampleCount += 1;
    }
    for (uint32_t i = 0; i < entry->pointerCount; i++) {
        LOGD("  Pointer %d: id=%d, x=%f, y=%f, pressure=%f, size=%f, "
                "touchMajor=%f, touchMinor=%f, toolMajor=%f, toolMinor=%f, "
                "orientation=%f",
                i, entry->pointerIds[i],
                sample->pointerCoords[i].x, sample->pointerCoords[i].y,
                sample->pointerCoords[i].pressure, sample->pointerCoords[i].size,
                sample->pointerCoords[i].touchMajor, sample->pointerCoords[i].touchMinor,
                sample->pointerCoords[i].toolMajor, sample->pointerCoords[i].toolMinor,
                sample->pointerCoords[i].orientation);
    }

    // Keep in mind that due to batching, it is possible for the number of samples actually
    // dispatched to change before the application finally consumed them.
    if (entry->action == AMOTION_EVENT_ACTION_MOVE) {
        LOGD("  ... Total movement samples currently batched %d ...", sampleCount);
    }
#endif
}

void InputDispatcher::dispatchEventToCurrentInputTargetsLocked(nsecs_t currentTime,
        EventEntry* eventEntry, bool resumeWithAppendedMotionSample) {
#if DEBUG_DISPATCH_CYCLE
    LOGD("dispatchEventToCurrentInputTargets - "
            "resumeWithAppendedMotionSample=%s",
            toString(resumeWithAppendedMotionSample));
#endif

    assert(eventEntry->dispatchInProgress); // should already have been set to true

    for (size_t i = 0; i < mCurrentInputTargets.size(); i++) {
        const InputTarget& inputTarget = mCurrentInputTargets.itemAt(i);

        ssize_t connectionIndex = getConnectionIndex(inputTarget.inputChannel);
        if (connectionIndex >= 0) {
            sp<Connection> connection = mConnectionsByReceiveFd.valueAt(connectionIndex);
            prepareDispatchCycleLocked(currentTime, connection, eventEntry, & inputTarget,
                    resumeWithAppendedMotionSample);
        } else {
            LOGW("Framework requested delivery of an input event to channel '%s' but it "
                    "is not registered with the input dispatcher.",
                    inputTarget.inputChannel->getName().string());
        }
    }
}

void InputDispatcher::startFindingTargetsLocked() {
    mCurrentInputTargetsValid = false;
    mCurrentInputTargets.clear();
    mCurrentInputChannel.clear();
    mInputTargetWaitCause = INPUT_TARGET_WAIT_CAUSE_NONE;
}

void InputDispatcher::finishFindingTargetsLocked(const InputWindow* window) {
    mCurrentInputWindowType = window->layoutParamsType;
    mCurrentInputChannel = window->inputChannel;
    mCurrentInputTargetsValid = true;
}

int32_t InputDispatcher::handleTargetsNotReadyLocked(nsecs_t currentTime,
        const EventEntry* entry, const InputApplication* application, const InputWindow* window,
        nsecs_t* nextWakeupTime) {
    if (application == NULL && window == NULL) {
        if (mInputTargetWaitCause != INPUT_TARGET_WAIT_CAUSE_SYSTEM_NOT_READY) {
#if DEBUG_FOCUS
            LOGD("Waiting for system to become ready for input.");
#endif
            mInputTargetWaitCause = INPUT_TARGET_WAIT_CAUSE_SYSTEM_NOT_READY;
            mInputTargetWaitStartTime = currentTime;
            mInputTargetWaitTimeoutTime = LONG_LONG_MAX;
            mInputTargetWaitTimeoutExpired = false;
        }
    } else {
        if (mInputTargetWaitCause != INPUT_TARGET_WAIT_CAUSE_APPLICATION_NOT_READY) {
#if DEBUG_FOCUS
            LOGD("Waiting for application to become ready for input: name=%s, window=%s",
                    application ? application->name.string() : "<unknown>",
                    window ? window->inputChannel->getName().string() : "<unknown>");
#endif
            nsecs_t timeout = window ? window->dispatchingTimeout :
                application ? application->dispatchingTimeout : DEFAULT_INPUT_DISPATCHING_TIMEOUT;

            mInputTargetWaitCause = INPUT_TARGET_WAIT_CAUSE_APPLICATION_NOT_READY;
            mInputTargetWaitStartTime = currentTime;
            mInputTargetWaitTimeoutTime = currentTime + timeout;
            mInputTargetWaitTimeoutExpired = false;
        }
    }

    if (mInputTargetWaitTimeoutExpired) {
        return INPUT_EVENT_INJECTION_TIMED_OUT;
    }

    if (currentTime >= mInputTargetWaitTimeoutTime) {
        LOGI("Application is not ready for input: name=%s, window=%s,"
                "%01.1fms since event, %01.1fms since wait started",
                application ? application->name.string() : "<unknown>",
                window ? window->inputChannel->getName().string() : "<unknown>",
                (currentTime - entry->eventTime) / 1000000.0,
                (currentTime - mInputTargetWaitStartTime) / 1000000.0);

        CommandEntry* commandEntry = postCommandLocked(
                & InputDispatcher::doTargetsNotReadyTimeoutLockedInterruptible);
        if (application) {
            commandEntry->inputApplicationHandle = application->handle;
        }
        if (window) {
            commandEntry->inputChannel = window->inputChannel;
        }

        // Force poll loop to wake up immediately on next iteration once we get the
        // ANR response back from the policy.
        *nextWakeupTime = LONG_LONG_MIN;
        return INPUT_EVENT_INJECTION_PENDING;
    } else {
        // Force poll loop to wake up when timeout is due.
        if (mInputTargetWaitTimeoutTime < *nextWakeupTime) {
            *nextWakeupTime = mInputTargetWaitTimeoutTime;
        }
        return INPUT_EVENT_INJECTION_PENDING;
    }
}

void InputDispatcher::resumeAfterTargetsNotReadyTimeoutLocked(nsecs_t newTimeout) {
    if (newTimeout > 0) {
        // Extend the timeout.
        mInputTargetWaitTimeoutTime = now() + newTimeout;
    } else {
        // Give up.
        mInputTargetWaitTimeoutExpired = true;
    }
}

nsecs_t InputDispatcher::getTimeSpentWaitingForApplicationWhileFindingTargetsLocked(
        nsecs_t currentTime) {
    if (mInputTargetWaitCause == INPUT_TARGET_WAIT_CAUSE_APPLICATION_NOT_READY) {
        return currentTime - mInputTargetWaitStartTime;
    }
    return 0;
}

void InputDispatcher::resetANRTimeoutsLocked() {
#if DEBUG_FOCUS
        LOGD("Resetting ANR timeouts.");
#endif

    // Reset timeouts for all active connections.
    nsecs_t currentTime = now();
    for (size_t i = 0; i < mActiveConnections.size(); i++) {
        Connection* connection = mActiveConnections[i];
        connection->resetTimeout(currentTime);
    }

    // Reset input target wait timeout.
    mInputTargetWaitCause = INPUT_TARGET_WAIT_CAUSE_NONE;
}

int32_t InputDispatcher::findFocusedWindowLocked(nsecs_t currentTime, const EventEntry* entry,
        nsecs_t* nextWakeupTime, InputWindow** outWindow) {
    *outWindow = NULL;
    mCurrentInputTargets.clear();

    int32_t injectionResult;

    // If there is no currently focused window and no focused application
    // then drop the event.
    if (! mFocusedWindow) {
        if (mFocusedApplication) {
#if DEBUG_FOCUS
            LOGD("Waiting because there is no focused window but there is a "
                    "focused application that may eventually add a window: '%s'.",
                    mFocusedApplication->name.string());
#endif
            injectionResult = handleTargetsNotReadyLocked(currentTime, entry,
                    mFocusedApplication, NULL, nextWakeupTime);
            goto Unresponsive;
        }

        LOGI("Dropping event because there is no focused window or focused application.");
        injectionResult = INPUT_EVENT_INJECTION_FAILED;
        goto Failed;
    }

    // Check permissions.
    if (! checkInjectionPermission(mFocusedWindow, entry->injectorPid, entry->injectorUid)) {
        injectionResult = INPUT_EVENT_INJECTION_PERMISSION_DENIED;
        goto Failed;
    }

    // If the currently focused window is paused then keep waiting.
    if (mFocusedWindow->paused) {
#if DEBUG_FOCUS
        LOGD("Waiting because focused window is paused.");
#endif
        injectionResult = handleTargetsNotReadyLocked(currentTime, entry,
                mFocusedApplication, mFocusedWindow, nextWakeupTime);
        goto Unresponsive;
    }

    // Success!  Output targets.
    injectionResult = INPUT_EVENT_INJECTION_SUCCEEDED;
    *outWindow = mFocusedWindow;
    addWindowTargetLocked(mFocusedWindow, InputTarget::FLAG_SYNC,
            getTimeSpentWaitingForApplicationWhileFindingTargetsLocked(currentTime));

    // Done.
Failed:
Unresponsive:
#if DEBUG_FOCUS
    LOGD("findFocusedWindow finished: injectionResult=%d",
            injectionResult);
    logDispatchStateLocked();
#endif
    return injectionResult;
}

int32_t InputDispatcher::findTouchedWindowLocked(nsecs_t currentTime, const MotionEntry* entry,
        nsecs_t* nextWakeupTime, InputWindow** outWindow) {
    enum InjectionPermission {
        INJECTION_PERMISSION_UNKNOWN,
        INJECTION_PERMISSION_GRANTED,
        INJECTION_PERMISSION_DENIED
    };

    *outWindow = NULL;
    mCurrentInputTargets.clear();

    nsecs_t startTime = now();

    // For security reasons, we defer updating the touch state until we are sure that
    // event injection will be allowed.
    //
    // FIXME In the original code, screenWasOff could never be set to true.
    //       The reason is that the POLICY_FLAG_WOKE_HERE
    //       and POLICY_FLAG_BRIGHT_HERE flags were set only when preprocessing raw
    //       EV_KEY, EV_REL and EV_ABS events.  As it happens, the touch event was
    //       actually enqueued using the policyFlags that appeared in the final EV_SYN
    //       events upon which no preprocessing took place.  So policyFlags was always 0.
    //       In the new native input dispatcher we're a bit more careful about event
    //       preprocessing so the touches we receive can actually have non-zero policyFlags.
    //       Unfortunately we obtain undesirable behavior.
    //
    //       Here's what happens:
    //
    //       When the device dims in anticipation of going to sleep, touches
    //       in windows which have FLAG_TOUCHABLE_WHEN_WAKING cause
    //       the device to brighten and reset the user activity timer.
    //       Touches on other windows (such as the launcher window)
    //       are dropped.  Then after a moment, the device goes to sleep.  Oops.
    //
    //       Also notice how screenWasOff was being initialized using POLICY_FLAG_BRIGHT_HERE
    //       instead of POLICY_FLAG_WOKE_HERE...
    //
    bool screenWasOff = false; // original policy: policyFlags & POLICY_FLAG_BRIGHT_HERE;

    int32_t action = entry->action;

    // Update the touch state as needed based on the properties of the touch event.
    int32_t injectionResult;
    InjectionPermission injectionPermission;
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        /* Case 1: ACTION_DOWN */

        InputWindow* newTouchedWindow = NULL;
        mTempTouchedOutsideTargets.clear();

        int32_t x = int32_t(entry->firstSample.pointerCoords[0].x);
        int32_t y = int32_t(entry->firstSample.pointerCoords[0].y);
        InputWindow* topErrorWindow = NULL;
        bool obscured = false;

        // Traverse windows from front to back to find touched window and outside targets.
        size_t numWindows = mWindows.size();
        for (size_t i = 0; i < numWindows; i++) {
            InputWindow* window = & mWindows.editItemAt(i);
            int32_t flags = window->layoutParamsFlags;

            if (flags & InputWindow::FLAG_SYSTEM_ERROR) {
                if (! topErrorWindow) {
                    topErrorWindow = window;
                }
            }

            if (window->visible) {
                if (! (flags & InputWindow::FLAG_NOT_TOUCHABLE)) {
                    bool isTouchModal = (flags & (InputWindow::FLAG_NOT_FOCUSABLE
                            | InputWindow::FLAG_NOT_TOUCH_MODAL)) == 0;
                    if (isTouchModal || window->touchableAreaContainsPoint(x, y)) {
                        if (! screenWasOff || flags & InputWindow::FLAG_TOUCHABLE_WHEN_WAKING) {
                            newTouchedWindow = window;
                            obscured = isWindowObscuredLocked(window);
                        }
                        break; // found touched window, exit window loop
                    }
                }

                if (flags & InputWindow::FLAG_WATCH_OUTSIDE_TOUCH) {
                    OutsideTarget outsideTarget;
                    outsideTarget.window = window;
                    outsideTarget.obscured = isWindowObscuredLocked(window);
                    mTempTouchedOutsideTargets.push(outsideTarget);
                }
            }
        }

        // If there is an error window but it is not taking focus (typically because
        // it is invisible) then wait for it.  Any other focused window may in
        // fact be in ANR state.
        if (topErrorWindow && newTouchedWindow != topErrorWindow) {
#if DEBUG_FOCUS
            LOGD("Waiting because system error window is pending.");
#endif
            injectionResult = handleTargetsNotReadyLocked(currentTime, entry,
                    NULL, NULL, nextWakeupTime);
            injectionPermission = INJECTION_PERMISSION_UNKNOWN;
            goto Unresponsive;
        }

        // If we did not find a touched window then fail.
        if (! newTouchedWindow) {
            if (mFocusedApplication) {
#if DEBUG_FOCUS
                LOGD("Waiting because there is no touched window but there is a "
                        "focused application that may eventually add a new window: '%s'.",
                        mFocusedApplication->name.string());
#endif
                injectionResult = handleTargetsNotReadyLocked(currentTime, entry,
                        mFocusedApplication, NULL, nextWakeupTime);
                injectionPermission = INJECTION_PERMISSION_UNKNOWN;
                goto Unresponsive;
            }

            LOGI("Dropping event because there is no touched window or focused application.");
            injectionResult = INPUT_EVENT_INJECTION_FAILED;
            injectionPermission = INJECTION_PERMISSION_UNKNOWN;
            goto Failed;
        }

        // Check permissions.
        if (! checkInjectionPermission(newTouchedWindow, entry->injectorPid, entry->injectorUid)) {
            injectionResult = INPUT_EVENT_INJECTION_PERMISSION_DENIED;
            injectionPermission = INJECTION_PERMISSION_DENIED;
            goto Failed;
        }

        // If the touched window is paused then keep waiting.
        if (newTouchedWindow->paused) {
#if DEBUG_INPUT_DISPATCHER_POLICY
            LOGD("Waiting because touched window is paused.");
#endif
            injectionResult = handleTargetsNotReadyLocked(currentTime, entry,
                    NULL, newTouchedWindow, nextWakeupTime);
            injectionPermission = INJECTION_PERMISSION_GRANTED;
            goto Unresponsive;
        }

        // Success!  Update the touch dispatch state for real.
        releaseTouchedWindowLocked();

        mTouchedWindow = newTouchedWindow;
        mTouchedWindowIsObscured = obscured;

        if (newTouchedWindow->hasWallpaper) {
            mTouchedWallpaperWindows.appendVector(mWallpaperWindows);
        }
    } else {
        /* Case 2: Everything but ACTION_DOWN */

        // Check permissions.
        if (! checkInjectionPermission(mTouchedWindow, entry->injectorPid, entry->injectorUid)) {
            injectionResult = INPUT_EVENT_INJECTION_PERMISSION_DENIED;
            injectionPermission = INJECTION_PERMISSION_DENIED;
            goto Failed;
        }

        // If the pointer is not currently down, then ignore the event.
        if (! mTouchDown) {
            LOGI("Dropping event because the pointer is not down.");
            injectionResult = INPUT_EVENT_INJECTION_FAILED;
            injectionPermission = INJECTION_PERMISSION_GRANTED;
            goto Failed;
        }

        // If there is no currently touched window then fail.
        if (! mTouchedWindow) {
#if DEBUG_INPUT_DISPATCHER_POLICY
            LOGD("Dropping event because there is no touched window to receive it.");
#endif
            injectionResult = INPUT_EVENT_INJECTION_FAILED;
            injectionPermission = INJECTION_PERMISSION_GRANTED;
            goto Failed;
        }

        // If the touched window is paused then keep waiting.
        if (mTouchedWindow->paused) {
#if DEBUG_INPUT_DISPATCHER_POLICY
            LOGD("Waiting because touched window is paused.");
#endif
            injectionResult = handleTargetsNotReadyLocked(currentTime, entry,
                    NULL, mTouchedWindow, nextWakeupTime);
            injectionPermission = INJECTION_PERMISSION_GRANTED;
            goto Unresponsive;
        }
    }

    // Success!  Output targets.
    injectionResult = INPUT_EVENT_INJECTION_SUCCEEDED;
    injectionPermission = INJECTION_PERMISSION_GRANTED;

    {
        size_t numWallpaperWindows = mTouchedWallpaperWindows.size();
        for (size_t i = 0; i < numWallpaperWindows; i++) {
            addWindowTargetLocked(mTouchedWallpaperWindows[i],
                    InputTarget::FLAG_WINDOW_IS_OBSCURED, 0);
        }

        size_t numOutsideTargets = mTempTouchedOutsideTargets.size();
        for (size_t i = 0; i < numOutsideTargets; i++) {
            const OutsideTarget& outsideTarget = mTempTouchedOutsideTargets[i];
            int32_t outsideTargetFlags = InputTarget::FLAG_OUTSIDE;
            if (outsideTarget.obscured) {
                outsideTargetFlags |= InputTarget::FLAG_WINDOW_IS_OBSCURED;
            }
            addWindowTargetLocked(outsideTarget.window, outsideTargetFlags, 0);
        }
        mTempTouchedOutsideTargets.clear();

        int32_t targetFlags = InputTarget::FLAG_SYNC;
        if (mTouchedWindowIsObscured) {
            targetFlags |= InputTarget::FLAG_WINDOW_IS_OBSCURED;
        }
        addWindowTargetLocked(mTouchedWindow, targetFlags,
                getTimeSpentWaitingForApplicationWhileFindingTargetsLocked(currentTime));
        *outWindow = mTouchedWindow;
    }

Failed:
    // Check injection permission once and for all.
    if (injectionPermission == INJECTION_PERMISSION_UNKNOWN) {
        if (checkInjectionPermission(action == AMOTION_EVENT_ACTION_DOWN ? NULL : mTouchedWindow,
                entry->injectorPid, entry->injectorUid)) {
            injectionPermission = INJECTION_PERMISSION_GRANTED;
        } else {
            injectionPermission = INJECTION_PERMISSION_DENIED;
        }
    }

    // Update final pieces of touch state if the injector had permission.
    if (injectionPermission == INJECTION_PERMISSION_GRANTED) {
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            if (mTouchDown) {
                // This is weird.  We got a down but we thought it was already down!
                LOGW("Pointer down received while already down.");
            } else {
                mTouchDown = true;
            }

            if (injectionResult != INPUT_EVENT_INJECTION_SUCCEEDED) {
                // Since we failed to identify a target for this touch down, we may still
                // be holding on to an earlier target from a previous touch down.  Release it.
                releaseTouchedWindowLocked();
            }
        } else if (action == AMOTION_EVENT_ACTION_UP) {
            mTouchDown = false;
            releaseTouchedWindowLocked();
        }
    } else {
        LOGW("Not updating touch focus because injection was denied.");
    }

Unresponsive:
#if DEBUG_FOCUS
    LOGD("findTouchedWindow finished: injectionResult=%d, injectionPermission=%d",
            injectionResult, injectionPermission);
    logDispatchStateLocked();
#endif
    return injectionResult;
}

void InputDispatcher::releaseTouchedWindowLocked() {
    mTouchedWindow = NULL;
    mTouchedWindowIsObscured = false;
    mTouchedWallpaperWindows.clear();
}

void InputDispatcher::addWindowTargetLocked(const InputWindow* window, int32_t targetFlags,
        nsecs_t timeSpentWaitingForApplication) {
    mCurrentInputTargets.push();

    InputTarget& target = mCurrentInputTargets.editTop();
    target.inputChannel = window->inputChannel;
    target.flags = targetFlags;
    target.timeout = window->dispatchingTimeout;
    target.timeSpentWaitingForApplication = timeSpentWaitingForApplication;
    target.xOffset = - window->frameLeft;
    target.yOffset = - window->frameTop;
}

void InputDispatcher::addMonitoringTargetsLocked() {
    for (size_t i = 0; i < mMonitoringChannels.size(); i++) {
        mCurrentInputTargets.push();

        InputTarget& target = mCurrentInputTargets.editTop();
        target.inputChannel = mMonitoringChannels[i];
        target.flags = 0;
        target.timeout = -1;
        target.timeSpentWaitingForApplication = 0;
        target.xOffset = 0;
        target.yOffset = 0;
    }
}

bool InputDispatcher::checkInjectionPermission(const InputWindow* window,
        int32_t injectorPid, int32_t injectorUid) {
    if (injectorUid > 0 && (window == NULL || window->ownerUid != injectorUid)) {
        bool result = mPolicy->checkInjectEventsPermissionNonReentrant(injectorPid, injectorUid);
        if (! result) {
            if (window) {
                LOGW("Permission denied: injecting event from pid %d uid %d to window "
                        "with input channel %s owned by uid %d",
                        injectorPid, injectorUid, window->inputChannel->getName().string(),
                        window->ownerUid);
            } else {
                LOGW("Permission denied: injecting event from pid %d uid %d",
                        injectorPid, injectorUid);
            }
            return false;
        }
    }
    return true;
}

bool InputDispatcher::isWindowObscuredLocked(const InputWindow* window) {
    size_t numWindows = mWindows.size();
    for (size_t i = 0; i < numWindows; i++) {
        const InputWindow* other = & mWindows.itemAt(i);
        if (other == window) {
            break;
        }
        if (other->visible && window->visibleFrameIntersects(other)) {
            return true;
        }
    }
    return false;
}

void InputDispatcher::pokeUserActivityLocked(nsecs_t eventTime,
        int32_t windowType, int32_t eventType) {
    CommandEntry* commandEntry = postCommandLocked(
            & InputDispatcher::doPokeUserActivityLockedInterruptible);
    commandEntry->eventTime = eventTime;
    commandEntry->windowType = windowType;
    commandEntry->userActivityEventType = eventType;
}

void InputDispatcher::prepareDispatchCycleLocked(nsecs_t currentTime,
        const sp<Connection>& connection, EventEntry* eventEntry, const InputTarget* inputTarget,
        bool resumeWithAppendedMotionSample) {
#if DEBUG_DISPATCH_CYCLE
    LOGD("channel '%s' ~ prepareDispatchCycle - flags=%d, timeout=%lldns, "
            "xOffset=%f, yOffset=%f, resumeWithAppendedMotionSample=%s",
            connection->getInputChannelName(), inputTarget->flags, inputTarget->timeout,
            inputTarget->xOffset, inputTarget->yOffset,
            toString(resumeWithAppendedMotionSample));
#endif

    // Skip this event if the connection status is not normal.
    // We don't want to enqueue additional outbound events if the connection is broken or
    // not responding.
    if (connection->status != Connection::STATUS_NORMAL) {
        LOGW("channel '%s' ~ Dropping event because the channel status is %s",
                connection->getInputChannelName(), connection->getStatusLabel());

        // If the connection is not responding but the user is poking the application anyways,
        // retrigger the original timeout.
        if (connection->status == Connection::STATUS_NOT_RESPONDING) {
            timeoutDispatchCycleLocked(currentTime, connection);
        }
        return;
    }

    // Resume the dispatch cycle with a freshly appended motion sample.
    // First we check that the last dispatch entry in the outbound queue is for the same
    // motion event to which we appended the motion sample.  If we find such a dispatch
    // entry, and if it is currently in progress then we try to stream the new sample.
    bool wasEmpty = connection->outboundQueue.isEmpty();

    if (! wasEmpty && resumeWithAppendedMotionSample) {
        DispatchEntry* motionEventDispatchEntry =
                connection->findQueuedDispatchEntryForEvent(eventEntry);
        if (motionEventDispatchEntry) {
            // If the dispatch entry is not in progress, then we must be busy dispatching an
            // earlier event.  Not a problem, the motion event is on the outbound queue and will
            // be dispatched later.
            if (! motionEventDispatchEntry->inProgress) {
#if DEBUG_BATCHING
                LOGD("channel '%s' ~ Not streaming because the motion event has "
                        "not yet been dispatched.  "
                        "(Waiting for earlier events to be consumed.)",
                        connection->getInputChannelName());
#endif
                return;
            }

            // If the dispatch entry is in progress but it already has a tail of pending
            // motion samples, then it must mean that the shared memory buffer filled up.
            // Not a problem, when this dispatch cycle is finished, we will eventually start
            // a new dispatch cycle to process the tail and that tail includes the newly
            // appended motion sample.
            if (motionEventDispatchEntry->tailMotionSample) {
#if DEBUG_BATCHING
                LOGD("channel '%s' ~ Not streaming because no new samples can "
                        "be appended to the motion event in this dispatch cycle.  "
                        "(Waiting for next dispatch cycle to start.)",
                        connection->getInputChannelName());
#endif
                return;
            }

            // The dispatch entry is in progress and is still potentially open for streaming.
            // Try to stream the new motion sample.  This might fail if the consumer has already
            // consumed the motion event (or if the channel is broken).
            MotionSample* appendedMotionSample = static_cast<MotionEntry*>(eventEntry)->lastSample;
            status_t status = connection->inputPublisher.appendMotionSample(
                    appendedMotionSample->eventTime, appendedMotionSample->pointerCoords);
            if (status == OK) {
#if DEBUG_BATCHING
                LOGD("channel '%s' ~ Successfully streamed new motion sample.",
                        connection->getInputChannelName());
#endif
                return;
            }

#if DEBUG_BATCHING
            if (status == NO_MEMORY) {
                LOGD("channel '%s' ~ Could not append motion sample to currently "
                        "dispatched move event because the shared memory buffer is full.  "
                        "(Waiting for next dispatch cycle to start.)",
                        connection->getInputChannelName());
            } else if (status == status_t(FAILED_TRANSACTION)) {
                LOGD("channel '%s' ~ Could not append motion sample to currently "
                        "dispatched move event because the event has already been consumed.  "
                        "(Waiting for next dispatch cycle to start.)",
                        connection->getInputChannelName());
            } else {
                LOGD("channel '%s' ~ Could not append motion sample to currently "
                        "dispatched move event due to an error, status=%d.  "
                        "(Waiting for next dispatch cycle to start.)",
                        connection->getInputChannelName(), status);
            }
#endif
            // Failed to stream.  Start a new tail of pending motion samples to dispatch
            // in the next cycle.
            motionEventDispatchEntry->tailMotionSample = appendedMotionSample;
            return;
        }
    }

    // Bring the input state back in line with reality in case it drifted off during an ANR.
    if (connection->inputState.isOutOfSync()) {
        mTempCancelationEvents.clear();
        connection->inputState.synthesizeCancelationEvents(& mAllocator, mTempCancelationEvents);
        connection->inputState.resetOutOfSync();

        if (! mTempCancelationEvents.isEmpty()) {
            LOGI("channel '%s' ~ Generated %d cancelation events to bring channel back in sync "
                    "with reality.",
                    connection->getInputChannelName(), mTempCancelationEvents.size());

            for (size_t i = 0; i < mTempCancelationEvents.size(); i++) {
                EventEntry* cancelationEventEntry = mTempCancelationEvents.itemAt(i);
                switch (cancelationEventEntry->type) {
                case EventEntry::TYPE_KEY:
                    logOutboundKeyDetailsLocked("  ",
                            static_cast<KeyEntry*>(cancelationEventEntry));
                    break;
                case EventEntry::TYPE_MOTION:
                    logOutboundMotionDetailsLocked("  ",
                            static_cast<MotionEntry*>(cancelationEventEntry));
                    break;
                }

                DispatchEntry* cancelationDispatchEntry =
                        mAllocator.obtainDispatchEntry(cancelationEventEntry,
                        0, inputTarget->xOffset, inputTarget->yOffset, inputTarget->timeout);
                connection->outboundQueue.enqueueAtTail(cancelationDispatchEntry);

                mAllocator.releaseEventEntry(cancelationEventEntry);
            }
        }
    }

    // This is a new event.
    // Enqueue a new dispatch entry onto the outbound queue for this connection.
    DispatchEntry* dispatchEntry = mAllocator.obtainDispatchEntry(eventEntry, // increments ref
            inputTarget->flags, inputTarget->xOffset, inputTarget->yOffset,
            inputTarget->timeout);
    if (dispatchEntry->isSyncTarget()) {
        eventEntry->pendingSyncDispatches += 1;
    }

    // Handle the case where we could not stream a new motion sample because the consumer has
    // already consumed the motion event (otherwise the corresponding dispatch entry would
    // still be in the outbound queue for this connection).  We set the head motion sample
    // to the list starting with the newly appended motion sample.
    if (resumeWithAppendedMotionSample) {
#if DEBUG_BATCHING
        LOGD("channel '%s' ~ Preparing a new dispatch cycle for additional motion samples "
                "that cannot be streamed because the motion event has already been consumed.",
                connection->getInputChannelName());
#endif
        MotionSample* appendedMotionSample = static_cast<MotionEntry*>(eventEntry)->lastSample;
        dispatchEntry->headMotionSample = appendedMotionSample;
    }

    // Enqueue the dispatch entry.
    connection->outboundQueue.enqueueAtTail(dispatchEntry);

    // If the outbound queue was previously empty, start the dispatch cycle going.
    if (wasEmpty) {
        activateConnectionLocked(connection.get());
        startDispatchCycleLocked(currentTime, connection,
                inputTarget->timeSpentWaitingForApplication);
    }
}

void InputDispatcher::startDispatchCycleLocked(nsecs_t currentTime,
        const sp<Connection>& connection, nsecs_t timeSpentWaitingForApplication) {
#if DEBUG_DISPATCH_CYCLE
    LOGD("channel '%s' ~ startDispatchCycle",
            connection->getInputChannelName());
#endif

    assert(connection->status == Connection::STATUS_NORMAL);
    assert(! connection->outboundQueue.isEmpty());

    DispatchEntry* dispatchEntry = connection->outboundQueue.headSentinel.next;
    assert(! dispatchEntry->inProgress);

    // Mark the dispatch entry as in progress.
    dispatchEntry->inProgress = true;

    // Update the connection's input state.
    InputState::Consistency consistency = connection->inputState.trackEvent(
            dispatchEntry->eventEntry);

#if FILTER_INPUT_EVENTS
    // Filter out inconsistent sequences of input events.
    // The input system may drop or inject events in a way that could violate implicit
    // invariants on input state and potentially cause an application to crash
    // or think that a key or pointer is stuck down.  Technically we make no guarantees
    // of consistency but it would be nice to improve on this where possible.
    // XXX: This code is a proof of concept only.  Not ready for prime time.
    if (consistency == InputState::TOLERABLE) {
#if DEBUG_DISPATCH_CYCLE
        LOGD("channel '%s' ~ Sending an event that is inconsistent with the connection's "
                "current input state but that is likely to be tolerated by the application.",
                connection->getInputChannelName());
#endif
    } else if (consistency == InputState::BROKEN) {
        LOGI("channel '%s' ~ Dropping an event that is inconsistent with the connection's "
                "current input state and that is likely to cause the application to crash.",
                connection->getInputChannelName());
        startNextDispatchCycleLocked(currentTime, connection);
        return;
    }
#endif

    // Publish the event.
    status_t status;
    switch (dispatchEntry->eventEntry->type) {
    case EventEntry::TYPE_KEY: {
        KeyEntry* keyEntry = static_cast<KeyEntry*>(dispatchEntry->eventEntry);

        // Apply target flags.
        int32_t action = keyEntry->action;
        int32_t flags = keyEntry->flags;
        if (dispatchEntry->targetFlags & InputTarget::FLAG_CANCEL) {
            flags |= AKEY_EVENT_FLAG_CANCELED;
        }

        // Publish the key event.
        status = connection->inputPublisher.publishKeyEvent(keyEntry->deviceId, keyEntry->source,
                action, flags, keyEntry->keyCode, keyEntry->scanCode,
                keyEntry->metaState, keyEntry->repeatCount, keyEntry->downTime,
                keyEntry->eventTime);

        if (status) {
            LOGE("channel '%s' ~ Could not publish key event, "
                    "status=%d", connection->getInputChannelName(), status);
            abortDispatchCycleLocked(currentTime, connection, true /*broken*/);
            return;
        }
        break;
    }

    case EventEntry::TYPE_MOTION: {
        MotionEntry* motionEntry = static_cast<MotionEntry*>(dispatchEntry->eventEntry);

        // Apply target flags.
        int32_t action = motionEntry->action;
        int32_t flags = motionEntry->flags;
        if (dispatchEntry->targetFlags & InputTarget::FLAG_OUTSIDE) {
            action = AMOTION_EVENT_ACTION_OUTSIDE;
        }
        if (dispatchEntry->targetFlags & InputTarget::FLAG_CANCEL) {
            action = AMOTION_EVENT_ACTION_CANCEL;
        }
        if (dispatchEntry->targetFlags & InputTarget::FLAG_WINDOW_IS_OBSCURED) {
            flags |= AMOTION_EVENT_FLAG_WINDOW_IS_OBSCURED;
        }

        // If headMotionSample is non-NULL, then it points to the first new sample that we
        // were unable to dispatch during the previous cycle so we resume dispatching from
        // that point in the list of motion samples.
        // Otherwise, we just start from the first sample of the motion event.
        MotionSample* firstMotionSample = dispatchEntry->headMotionSample;
        if (! firstMotionSample) {
            firstMotionSample = & motionEntry->firstSample;
        }

        // Set the X and Y offset depending on the input source.
        float xOffset, yOffset;
        if (motionEntry->source & AINPUT_SOURCE_CLASS_POINTER) {
            xOffset = dispatchEntry->xOffset;
            yOffset = dispatchEntry->yOffset;
        } else {
            xOffset = 0.0f;
            yOffset = 0.0f;
        }

        // Publish the motion event and the first motion sample.
        status = connection->inputPublisher.publishMotionEvent(motionEntry->deviceId,
                motionEntry->source, action, flags, motionEntry->edgeFlags, motionEntry->metaState,
                xOffset, yOffset,
                motionEntry->xPrecision, motionEntry->yPrecision,
                motionEntry->downTime, firstMotionSample->eventTime,
                motionEntry->pointerCount, motionEntry->pointerIds,
                firstMotionSample->pointerCoords);

        if (status) {
            LOGE("channel '%s' ~ Could not publish motion event, "
                    "status=%d", connection->getInputChannelName(), status);
            abortDispatchCycleLocked(currentTime, connection, true /*broken*/);
            return;
        }

        // Append additional motion samples.
        MotionSample* nextMotionSample = firstMotionSample->next;
        for (; nextMotionSample != NULL; nextMotionSample = nextMotionSample->next) {
            status = connection->inputPublisher.appendMotionSample(
                    nextMotionSample->eventTime, nextMotionSample->pointerCoords);
            if (status == NO_MEMORY) {
#if DEBUG_DISPATCH_CYCLE
                    LOGD("channel '%s' ~ Shared memory buffer full.  Some motion samples will "
                            "be sent in the next dispatch cycle.",
                            connection->getInputChannelName());
#endif
                break;
            }
            if (status != OK) {
                LOGE("channel '%s' ~ Could not append motion sample "
                        "for a reason other than out of memory, status=%d",
                        connection->getInputChannelName(), status);
                abortDispatchCycleLocked(currentTime, connection, true /*broken*/);
                return;
            }
        }

        // Remember the next motion sample that we could not dispatch, in case we ran out
        // of space in the shared memory buffer.
        dispatchEntry->tailMotionSample = nextMotionSample;
        break;
    }

    default: {
        assert(false);
    }
    }

    // Send the dispatch signal.
    status = connection->inputPublisher.sendDispatchSignal();
    if (status) {
        LOGE("channel '%s' ~ Could not send dispatch signal, status=%d",
                connection->getInputChannelName(), status);
        abortDispatchCycleLocked(currentTime, connection, true /*broken*/);
        return;
    }

    // Record information about the newly started dispatch cycle.
    connection->lastEventTime = dispatchEntry->eventEntry->eventTime;
    connection->lastDispatchTime = currentTime;

    nsecs_t timeout = dispatchEntry->timeout - timeSpentWaitingForApplication;
    connection->setNextTimeoutTime(currentTime, timeout);

    // Notify other system components.
    onDispatchCycleStartedLocked(currentTime, connection);
}

void InputDispatcher::finishDispatchCycleLocked(nsecs_t currentTime,
        const sp<Connection>& connection) {
#if DEBUG_DISPATCH_CYCLE
    LOGD("channel '%s' ~ finishDispatchCycle - %01.1fms since event, "
            "%01.1fms since dispatch",
            connection->getInputChannelName(),
            connection->getEventLatencyMillis(currentTime),
            connection->getDispatchLatencyMillis(currentTime));
#endif

    if (connection->status == Connection::STATUS_BROKEN
            || connection->status == Connection::STATUS_ZOMBIE) {
        return;
    }

    // Clear the pending timeout.
    connection->nextTimeoutTime = LONG_LONG_MAX;

    if (connection->status == Connection::STATUS_NOT_RESPONDING) {
        // Recovering from an ANR.
        connection->status = Connection::STATUS_NORMAL;

        // Notify other system components.
        onDispatchCycleFinishedLocked(currentTime, connection, true /*recoveredFromANR*/);
    } else {
        // Normal finish.  Not much to do here.

        // Notify other system components.
        onDispatchCycleFinishedLocked(currentTime, connection, false /*recoveredFromANR*/);
    }

    // Reset the publisher since the event has been consumed.
    // We do this now so that the publisher can release some of its internal resources
    // while waiting for the next dispatch cycle to begin.
    status_t status = connection->inputPublisher.reset();
    if (status) {
        LOGE("channel '%s' ~ Could not reset publisher, status=%d",
                connection->getInputChannelName(), status);
        abortDispatchCycleLocked(currentTime, connection, true /*broken*/);
        return;
    }

    startNextDispatchCycleLocked(currentTime, connection);
}

void InputDispatcher::startNextDispatchCycleLocked(nsecs_t currentTime,
        const sp<Connection>& connection) {
    // Start the next dispatch cycle for this connection.
    while (! connection->outboundQueue.isEmpty()) {
        DispatchEntry* dispatchEntry = connection->outboundQueue.headSentinel.next;
        if (dispatchEntry->inProgress) {
             // Finish or resume current event in progress.
            if (dispatchEntry->tailMotionSample) {
                // We have a tail of undispatched motion samples.
                // Reuse the same DispatchEntry and start a new cycle.
                dispatchEntry->inProgress = false;
                dispatchEntry->headMotionSample = dispatchEntry->tailMotionSample;
                dispatchEntry->tailMotionSample = NULL;
                startDispatchCycleLocked(currentTime, connection, 0);
                return;
            }
            // Finished.
            connection->outboundQueue.dequeueAtHead();
            if (dispatchEntry->isSyncTarget()) {
                decrementPendingSyncDispatchesLocked(dispatchEntry->eventEntry);
            }
            mAllocator.releaseDispatchEntry(dispatchEntry);
        } else {
            // If the head is not in progress, then we must have already dequeued the in
            // progress event, which means we actually aborted it (due to ANR).
            // So just start the next event for this connection.
            startDispatchCycleLocked(currentTime, connection, 0);
            return;
        }
    }

    // Outbound queue is empty, deactivate the connection.
    deactivateConnectionLocked(connection.get());
}

void InputDispatcher::timeoutDispatchCycleLocked(nsecs_t currentTime,
        const sp<Connection>& connection) {
#if DEBUG_DISPATCH_CYCLE
    LOGD("channel '%s' ~ timeoutDispatchCycle",
            connection->getInputChannelName());
#endif

    if (connection->status == Connection::STATUS_NORMAL) {
        // Enter the not responding state.
        connection->status = Connection::STATUS_NOT_RESPONDING;
        connection->lastANRTime = currentTime;
    } else if (connection->status != Connection::STATUS_NOT_RESPONDING) {
        // Connection is broken or dead.
        return;
    }

    // Notify other system components.
    // This enqueues a command which will eventually call resumeAfterTimeoutDispatchCycleLocked.
    onDispatchCycleANRLocked(currentTime, connection);
}

void InputDispatcher::resumeAfterTimeoutDispatchCycleLocked(nsecs_t currentTime,
        const sp<Connection>& connection, nsecs_t newTimeout) {
#if DEBUG_DISPATCH_CYCLE
    LOGD("channel '%s' ~ resumeAfterTimeoutDispatchCycleLocked - newTimeout=%lld",
            connection->getInputChannelName(), newTimeout);
#endif

    if (connection->status != Connection::STATUS_NOT_RESPONDING) {
        return;
    }

    if (newTimeout > 0) {
        // The system has decided to give the application some more time.
        // Keep waiting synchronously and resume normal dispatch.
        connection->status = Connection::STATUS_NORMAL;
        connection->setNextTimeoutTime(currentTime, newTimeout);
    } else {
        // The system is about to throw up an ANR dialog and has requested that we abort dispatch.
        // Reset the timeout.
        connection->nextTimeoutTime = LONG_LONG_MAX;

        // Input state will no longer be realistic.
        connection->inputState.setOutOfSync();

        if (! connection->outboundQueue.isEmpty()) {
            // Make the current pending dispatch asynchronous (if it isn't already) so that
            // subsequent events can be delivered to the ANR dialog or to another application.
            DispatchEntry* currentDispatchEntry = connection->outboundQueue.headSentinel.next;
            currentDispatchEntry->preemptSyncTarget();

            // Drain all but the first entry in the outbound queue.  We keep the first entry
            // since that is the one that dispatch is stuck on.  We throw away the others
            // so that we don't spam the application with stale messages if it eventually
            // wakes up and recovers from the ANR.
            drainOutboundQueueLocked(connection.get(), currentDispatchEntry->next);
        }
    }
}

void InputDispatcher::abortDispatchCycleLocked(nsecs_t currentTime,
        const sp<Connection>& connection, bool broken) {
#if DEBUG_DISPATCH_CYCLE
    LOGD("channel '%s' ~ abortDispatchCycle - broken=%s",
            connection->getInputChannelName(), toString(broken));
#endif

    // Clear the pending timeout.
    connection->nextTimeoutTime = LONG_LONG_MAX;

    // Input state will no longer be realistic.
    connection->inputState.setOutOfSync();

    // Clear the outbound queue.
    drainOutboundQueueLocked(connection.get(), connection->outboundQueue.headSentinel.next);

    // Handle the case where the connection appears to be unrecoverably broken.
    // Ignore already broken or zombie connections.
    if (broken) {
        if (connection->status == Connection::STATUS_NORMAL
                || connection->status == Connection::STATUS_NOT_RESPONDING) {
            connection->status = Connection::STATUS_BROKEN;

            // Notify other system components.
            onDispatchCycleBrokenLocked(currentTime, connection);
        }
    }
}

void InputDispatcher::drainOutboundQueueLocked(Connection* connection,
        DispatchEntry* firstDispatchEntryToDrain) {
    for (DispatchEntry* dispatchEntry = firstDispatchEntryToDrain;
            dispatchEntry != & connection->outboundQueue.tailSentinel;) {
        DispatchEntry* next = dispatchEntry->next;
        connection->outboundQueue.dequeue(dispatchEntry);

        if (dispatchEntry->isSyncTarget()) {
            decrementPendingSyncDispatchesLocked(dispatchEntry->eventEntry);
        }
        mAllocator.releaseDispatchEntry(dispatchEntry);

        dispatchEntry = next;
    }

    if (connection->outboundQueue.isEmpty()) {
        deactivateConnectionLocked(connection);
    }
}

bool InputDispatcher::handleReceiveCallback(int receiveFd, int events, void* data) {
    InputDispatcher* d = static_cast<InputDispatcher*>(data);

    { // acquire lock
        AutoMutex _l(d->mLock);

        ssize_t connectionIndex = d->mConnectionsByReceiveFd.indexOfKey(receiveFd);
        if (connectionIndex < 0) {
            LOGE("Received spurious receive callback for unknown input channel.  "
                    "fd=%d, events=0x%x", receiveFd, events);
            return false; // remove the callback
        }

        nsecs_t currentTime = now();

        sp<Connection> connection = d->mConnectionsByReceiveFd.valueAt(connectionIndex);
        if (events & (POLLERR | POLLHUP | POLLNVAL)) {
            LOGE("channel '%s' ~ Consumer closed input channel or an error occurred.  "
                    "events=0x%x", connection->getInputChannelName(), events);
            d->abortDispatchCycleLocked(currentTime, connection, true /*broken*/);
            d->runCommandsLockedInterruptible();
            return false; // remove the callback
        }

        if (! (events & POLLIN)) {
            LOGW("channel '%s' ~ Received spurious callback for unhandled poll event.  "
                    "events=0x%x", connection->getInputChannelName(), events);
            return true;
        }

        status_t status = connection->inputPublisher.receiveFinishedSignal();
        if (status) {
            LOGE("channel '%s' ~ Failed to receive finished signal.  status=%d",
                    connection->getInputChannelName(), status);
            d->abortDispatchCycleLocked(currentTime, connection, true /*broken*/);
            d->runCommandsLockedInterruptible();
            return false; // remove the callback
        }

        d->finishDispatchCycleLocked(currentTime, connection);
        d->runCommandsLockedInterruptible();
        return true;
    } // release lock
}

void InputDispatcher::notifyConfigurationChanged(nsecs_t eventTime) {
#if DEBUG_INBOUND_EVENT_DETAILS
    LOGD("notifyConfigurationChanged - eventTime=%lld", eventTime);
#endif

    bool needWake;
    { // acquire lock
        AutoMutex _l(mLock);

        ConfigurationChangedEntry* newEntry = mAllocator.obtainConfigurationChangedEntry(eventTime);
        needWake = enqueueInboundEventLocked(newEntry);
    } // release lock

    if (needWake) {
        mPollLoop->wake();
    }
}

void InputDispatcher::notifyKey(nsecs_t eventTime, int32_t deviceId, int32_t source,
        uint32_t policyFlags, int32_t action, int32_t flags,
        int32_t keyCode, int32_t scanCode, int32_t metaState, nsecs_t downTime) {
#if DEBUG_INBOUND_EVENT_DETAILS
    LOGD("notifyKey - eventTime=%lld, deviceId=0x%x, source=0x%x, policyFlags=0x%x, action=0x%x, "
            "flags=0x%x, keyCode=0x%x, scanCode=0x%x, metaState=0x%x, downTime=%lld",
            eventTime, deviceId, source, policyFlags, action, flags,
            keyCode, scanCode, metaState, downTime);
#endif

    bool needWake;
    { // acquire lock
        AutoMutex _l(mLock);

        int32_t repeatCount = 0;
        KeyEntry* newEntry = mAllocator.obtainKeyEntry(eventTime,
                deviceId, source, policyFlags, action, flags, keyCode, scanCode,
                metaState, repeatCount, downTime);

        needWake = enqueueInboundEventLocked(newEntry);
    } // release lock

    if (needWake) {
        mPollLoop->wake();
    }
}

void InputDispatcher::notifyMotion(nsecs_t eventTime, int32_t deviceId, int32_t source,
        uint32_t policyFlags, int32_t action, int32_t flags, int32_t metaState, int32_t edgeFlags,
        uint32_t pointerCount, const int32_t* pointerIds, const PointerCoords* pointerCoords,
        float xPrecision, float yPrecision, nsecs_t downTime) {
#if DEBUG_INBOUND_EVENT_DETAILS
    LOGD("notifyMotion - eventTime=%lld, deviceId=0x%x, source=0x%x, policyFlags=0x%x, "
            "action=0x%x, flags=0x%x, metaState=0x%x, edgeFlags=0x%x, "
            "xPrecision=%f, yPrecision=%f, downTime=%lld",
            eventTime, deviceId, source, policyFlags, action, flags, metaState, edgeFlags,
            xPrecision, yPrecision, downTime);
    for (uint32_t i = 0; i < pointerCount; i++) {
        LOGD("  Pointer %d: id=%d, x=%f, y=%f, pressure=%f, size=%f, "
                "touchMajor=%f, touchMinor=%f, toolMajor=%f, toolMinor=%f, "
                "orientation=%f",
                i, pointerIds[i], pointerCoords[i].x, pointerCoords[i].y,
                pointerCoords[i].pressure, pointerCoords[i].size,
                pointerCoords[i].touchMajor, pointerCoords[i].touchMinor,
                pointerCoords[i].toolMajor, pointerCoords[i].toolMinor,
                pointerCoords[i].orientation);
    }
#endif

    bool needWake;
    { // acquire lock
        AutoMutex _l(mLock);

        // Attempt batching and streaming of move events.
        if (action == AMOTION_EVENT_ACTION_MOVE) {
            // BATCHING CASE
            //
            // Try to append a move sample to the tail of the inbound queue for this device.
            // Give up if we encounter a non-move motion event for this device since that
            // means we cannot append any new samples until a new motion event has started.
            for (EventEntry* entry = mInboundQueue.tailSentinel.prev;
                    entry != & mInboundQueue.headSentinel; entry = entry->prev) {
                if (entry->type != EventEntry::TYPE_MOTION) {
                    // Keep looking for motion events.
                    continue;
                }

                MotionEntry* motionEntry = static_cast<MotionEntry*>(entry);
                if (motionEntry->deviceId != deviceId) {
                    // Keep looking for this device.
                    continue;
                }

                if (motionEntry->action != AMOTION_EVENT_ACTION_MOVE
                        || motionEntry->pointerCount != pointerCount
                        || motionEntry->isInjected()) {
                    // Last motion event in the queue for this device is not compatible for
                    // appending new samples.  Stop here.
                    goto NoBatchingOrStreaming;
                }

                // The last motion event is a move and is compatible for appending.
                // Do the batching magic.
                mAllocator.appendMotionSample(motionEntry, eventTime, pointerCoords);
#if DEBUG_BATCHING
                LOGD("Appended motion sample onto batch for most recent "
                        "motion event for this device in the inbound queue.");
#endif
                return; // done!
            }

            // STREAMING CASE
            //
            // There is no pending motion event (of any kind) for this device in the inbound queue.
            // Search the outbound queues for a synchronously dispatched motion event for this
            // device.  If found, then we append the new sample to that event and then try to
            // push it out to all current targets.  It is possible that some targets will already
            // have consumed the motion event.  This case is automatically handled by the
            // logic in prepareDispatchCycleLocked by tracking where resumption takes place.
            //
            // The reason we look for a synchronously dispatched motion event is because we
            // want to be sure that no other motion events have been dispatched since the move.
            // It's also convenient because it means that the input targets are still valid.
            // This code could be improved to support streaming of asynchronously dispatched
            // motion events (which might be significantly more efficient) but it may become
            // a little more complicated as a result.
            //
            // Note: This code crucially depends on the invariant that an outbound queue always
            //       contains at most one synchronous event and it is always last (but it might
            //       not be first!).
            if (mCurrentInputTargetsValid) {
                for (size_t i = 0; i < mActiveConnections.size(); i++) {
                    Connection* connection = mActiveConnections.itemAt(i);
                    if (! connection->outboundQueue.isEmpty()) {
                        DispatchEntry* dispatchEntry = connection->outboundQueue.tailSentinel.prev;
                        if (dispatchEntry->isSyncTarget()) {
                            if (dispatchEntry->eventEntry->type != EventEntry::TYPE_MOTION) {
                                goto NoBatchingOrStreaming;
                            }

                            MotionEntry* syncedMotionEntry = static_cast<MotionEntry*>(
                                    dispatchEntry->eventEntry);
                            if (syncedMotionEntry->action != AMOTION_EVENT_ACTION_MOVE
                                    || syncedMotionEntry->deviceId != deviceId
                                    || syncedMotionEntry->pointerCount != pointerCount
                                    || syncedMotionEntry->isInjected()) {
                                goto NoBatchingOrStreaming;
                            }

                            // Found synced move entry.  Append sample and resume dispatch.
                            mAllocator.appendMotionSample(syncedMotionEntry, eventTime,
                                    pointerCoords);
    #if DEBUG_BATCHING
                            LOGD("Appended motion sample onto batch for most recent synchronously "
                                    "dispatched motion event for this device in the outbound queues.");
    #endif
                            nsecs_t currentTime = now();
                            dispatchEventToCurrentInputTargetsLocked(currentTime, syncedMotionEntry,
                                    true /*resumeWithAppendedMotionSample*/);

                            runCommandsLockedInterruptible();
                            return; // done!
                        }
                    }
                }
            }

NoBatchingOrStreaming:;
        }

        // Just enqueue a new motion event.
        MotionEntry* newEntry = mAllocator.obtainMotionEntry(eventTime,
                deviceId, source, policyFlags, action, flags, metaState, edgeFlags,
                xPrecision, yPrecision, downTime,
                pointerCount, pointerIds, pointerCoords);

        needWake = enqueueInboundEventLocked(newEntry);
    } // release lock

    if (needWake) {
        mPollLoop->wake();
    }
}

int32_t InputDispatcher::injectInputEvent(const InputEvent* event,
        int32_t injectorPid, int32_t injectorUid, int32_t syncMode, int32_t timeoutMillis) {
#if DEBUG_INBOUND_EVENT_DETAILS
    LOGD("injectInputEvent - eventType=%d, injectorPid=%d, injectorUid=%d, "
            "syncMode=%d, timeoutMillis=%d",
            event->getType(), injectorPid, injectorUid, syncMode, timeoutMillis);
#endif

    nsecs_t endTime = now() + milliseconds_to_nanoseconds(timeoutMillis);

    EventEntry* injectedEntry;
    bool needWake;
    { // acquire lock
        AutoMutex _l(mLock);

        injectedEntry = createEntryFromInjectedInputEventLocked(event);
        if (! injectedEntry) {
            return INPUT_EVENT_INJECTION_FAILED;
        }

        injectedEntry->refCount += 1;
        injectedEntry->injectorPid = injectorPid;
        injectedEntry->injectorUid = injectorUid;

        if (syncMode == INPUT_EVENT_INJECTION_SYNC_NONE) {
            injectedEntry->injectionIsAsync = true;
        }

        needWake = enqueueInboundEventLocked(injectedEntry);
    } // release lock

    if (needWake) {
        mPollLoop->wake();
    }

    int32_t injectionResult;
    { // acquire lock
        AutoMutex _l(mLock);

        if (syncMode == INPUT_EVENT_INJECTION_SYNC_NONE) {
            injectionResult = INPUT_EVENT_INJECTION_SUCCEEDED;
        } else {
            for (;;) {
                injectionResult = injectedEntry->injectionResult;
                if (injectionResult != INPUT_EVENT_INJECTION_PENDING) {
                    break;
                }

                nsecs_t remainingTimeout = endTime - now();
                if (remainingTimeout <= 0) {
#if DEBUG_INJECTION
                    LOGD("injectInputEvent - Timed out waiting for injection result "
                            "to become available.");
#endif
                    injectionResult = INPUT_EVENT_INJECTION_TIMED_OUT;
                    break;
                }

                mInjectionResultAvailableCondition.waitRelative(mLock, remainingTimeout);
            }

            if (injectionResult == INPUT_EVENT_INJECTION_SUCCEEDED
                    && syncMode == INPUT_EVENT_INJECTION_SYNC_WAIT_FOR_FINISHED) {
                while (injectedEntry->pendingSyncDispatches != 0) {
#if DEBUG_INJECTION
                    LOGD("injectInputEvent - Waiting for %d pending synchronous dispatches.",
                            injectedEntry->pendingSyncDispatches);
#endif
                    nsecs_t remainingTimeout = endTime - now();
                    if (remainingTimeout <= 0) {
#if DEBUG_INJECTION
                    LOGD("injectInputEvent - Timed out waiting for pending synchronous "
                            "dispatches to finish.");
#endif
                        injectionResult = INPUT_EVENT_INJECTION_TIMED_OUT;
                        break;
                    }

                    mInjectionSyncFinishedCondition.waitRelative(mLock, remainingTimeout);
                }
            }
        }

        mAllocator.releaseEventEntry(injectedEntry);
    } // release lock

#if DEBUG_INJECTION
    LOGD("injectInputEvent - Finished with result %d.  "
            "injectorPid=%d, injectorUid=%d",
            injectionResult, injectorPid, injectorUid);
#endif

    return injectionResult;
}

void InputDispatcher::setInjectionResultLocked(EventEntry* entry, int32_t injectionResult) {
    if (entry->isInjected()) {
#if DEBUG_INJECTION
        LOGD("Setting input event injection result to %d.  "
                "injectorPid=%d, injectorUid=%d",
                 injectionResult, entry->injectorPid, entry->injectorUid);
#endif

        if (entry->injectionIsAsync) {
            // Log the outcome since the injector did not wait for the injection result.
            switch (injectionResult) {
            case INPUT_EVENT_INJECTION_SUCCEEDED:
                LOGV("Asynchronous input event injection succeeded.");
                break;
            case INPUT_EVENT_INJECTION_FAILED:
                LOGW("Asynchronous input event injection failed.");
                break;
            case INPUT_EVENT_INJECTION_PERMISSION_DENIED:
                LOGW("Asynchronous input event injection permission denied.");
                break;
            case INPUT_EVENT_INJECTION_TIMED_OUT:
                LOGW("Asynchronous input event injection timed out.");
                break;
            }
        }

        entry->injectionResult = injectionResult;
        mInjectionResultAvailableCondition.broadcast();
    }
}

void InputDispatcher::decrementPendingSyncDispatchesLocked(EventEntry* entry) {
    entry->pendingSyncDispatches -= 1;

    if (entry->isInjected() && entry->pendingSyncDispatches == 0) {
        mInjectionSyncFinishedCondition.broadcast();
    }
}

static bool isValidKeyAction(int32_t action) {
    switch (action) {
    case AKEY_EVENT_ACTION_DOWN:
    case AKEY_EVENT_ACTION_UP:
        return true;
    default:
        return false;
    }
}

static bool isValidMotionAction(int32_t action) {
    switch (action & AMOTION_EVENT_ACTION_MASK) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
    case AMOTION_EVENT_ACTION_MOVE:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_UP:
    case AMOTION_EVENT_ACTION_OUTSIDE:
        return true;
    default:
        return false;
    }
}

InputDispatcher::EventEntry* InputDispatcher::createEntryFromInjectedInputEventLocked(
        const InputEvent* event) {
    switch (event->getType()) {
    case AINPUT_EVENT_TYPE_KEY: {
        const KeyEvent* keyEvent = static_cast<const KeyEvent*>(event);
        if (! isValidKeyAction(keyEvent->getAction())) {
            LOGE("Dropping injected key event since it has invalid action code 0x%x",
                    keyEvent->getAction());
            return NULL;
        }

        uint32_t policyFlags = POLICY_FLAG_INJECTED;

        KeyEntry* keyEntry = mAllocator.obtainKeyEntry(keyEvent->getEventTime(),
                keyEvent->getDeviceId(), keyEvent->getSource(), policyFlags,
                keyEvent->getAction(), keyEvent->getFlags(),
                keyEvent->getKeyCode(), keyEvent->getScanCode(), keyEvent->getMetaState(),
                keyEvent->getRepeatCount(), keyEvent->getDownTime());
        return keyEntry;
    }

    case AINPUT_EVENT_TYPE_MOTION: {
        const MotionEvent* motionEvent = static_cast<const MotionEvent*>(event);
        if (! isValidMotionAction(motionEvent->getAction())) {
            LOGE("Dropping injected motion event since it has invalid action code 0x%x.",
                    motionEvent->getAction());
            return NULL;
        }
        if (motionEvent->getPointerCount() == 0
                || motionEvent->getPointerCount() > MAX_POINTERS) {
            LOGE("Dropping injected motion event since it has an invalid pointer count %d.",
                    motionEvent->getPointerCount());
        }

        uint32_t policyFlags = POLICY_FLAG_INJECTED;

        const nsecs_t* sampleEventTimes = motionEvent->getSampleEventTimes();
        const PointerCoords* samplePointerCoords = motionEvent->getSamplePointerCoords();
        size_t pointerCount = motionEvent->getPointerCount();

        MotionEntry* motionEntry = mAllocator.obtainMotionEntry(*sampleEventTimes,
                motionEvent->getDeviceId(), motionEvent->getSource(), policyFlags,
                motionEvent->getAction(), motionEvent->getFlags(),
                motionEvent->getMetaState(), motionEvent->getEdgeFlags(),
                motionEvent->getXPrecision(), motionEvent->getYPrecision(),
                motionEvent->getDownTime(), uint32_t(pointerCount),
                motionEvent->getPointerIds(), samplePointerCoords);
        for (size_t i = motionEvent->getHistorySize(); i > 0; i--) {
            sampleEventTimes += 1;
            samplePointerCoords += pointerCount;
            mAllocator.appendMotionSample(motionEntry, *sampleEventTimes, samplePointerCoords);
        }
        return motionEntry;
    }

    default:
        assert(false);
        return NULL;
    }
}

void InputDispatcher::setInputWindows(const Vector<InputWindow>& inputWindows) {
#if DEBUG_FOCUS
    LOGD("setInputWindows");
#endif
    { // acquire lock
        AutoMutex _l(mLock);

        sp<InputChannel> touchedWindowChannel;
        if (mTouchedWindow) {
            touchedWindowChannel = mTouchedWindow->inputChannel;
            mTouchedWindow = NULL;
        }
        size_t numTouchedWallpapers = mTouchedWallpaperWindows.size();
        if (numTouchedWallpapers != 0) {
            for (size_t i = 0; i < numTouchedWallpapers; i++) {
                mTempTouchedWallpaperChannels.push(mTouchedWallpaperWindows[i]->inputChannel);
            }
            mTouchedWallpaperWindows.clear();
        }

        bool hadFocusedWindow = mFocusedWindow != NULL;

        mFocusedWindow = NULL;
        mWallpaperWindows.clear();

        mWindows.clear();
        mWindows.appendVector(inputWindows);

        size_t numWindows = mWindows.size();
        for (size_t i = 0; i < numWindows; i++) {
            InputWindow* window = & mWindows.editItemAt(i);
            if (window->hasFocus) {
                mFocusedWindow = window;
            }

            if (window->layoutParamsType == InputWindow::TYPE_WALLPAPER) {
                mWallpaperWindows.push(window);

                for (size_t j = 0; j < numTouchedWallpapers; j++) {
                    if (window->inputChannel == mTempTouchedWallpaperChannels[i]) {
                        mTouchedWallpaperWindows.push(window);
                    }
                }
            }

            if (window->inputChannel == touchedWindowChannel) {
                mTouchedWindow = window;
            }
        }

        mTempTouchedWallpaperChannels.clear();

        if ((hadFocusedWindow && ! mFocusedWindow)
                || (mFocusedWindow && ! mFocusedWindow->visible)) {
            preemptInputDispatchInnerLocked();
        }

#if DEBUG_FOCUS
        logDispatchStateLocked();
#endif
    } // release lock

    // Wake up poll loop since it may need to make new input dispatching choices.
    mPollLoop->wake();
}

void InputDispatcher::setFocusedApplication(const InputApplication* inputApplication) {
#if DEBUG_FOCUS
    LOGD("setFocusedApplication");
#endif
    { // acquire lock
        AutoMutex _l(mLock);

        releaseFocusedApplicationLocked();

        if (inputApplication) {
            mFocusedApplicationStorage = *inputApplication;
            mFocusedApplication = & mFocusedApplicationStorage;
        }

#if DEBUG_FOCUS
        logDispatchStateLocked();
#endif
    } // release lock

    // Wake up poll loop since it may need to make new input dispatching choices.
    mPollLoop->wake();
}

void InputDispatcher::releaseFocusedApplicationLocked() {
    if (mFocusedApplication) {
        mFocusedApplication = NULL;
        mFocusedApplicationStorage.handle.clear();
    }
}

void InputDispatcher::setInputDispatchMode(bool enabled, bool frozen) {
#if DEBUG_FOCUS
    LOGD("setInputDispatchMode: enabled=%d, frozen=%d", enabled, frozen);
#endif

    bool changed;
    { // acquire lock
        AutoMutex _l(mLock);

        if (mDispatchEnabled != enabled || mDispatchFrozen != frozen) {
            if (mDispatchFrozen && ! frozen) {
                resetANRTimeoutsLocked();
            }

            mDispatchEnabled = enabled;
            mDispatchFrozen = frozen;
            changed = true;
        } else {
            changed = false;
        }

#if DEBUG_FOCUS
        logDispatchStateLocked();
#endif
    } // release lock

    if (changed) {
        // Wake up poll loop since it may need to make new input dispatching choices.
        mPollLoop->wake();
    }
}

void InputDispatcher::preemptInputDispatch() {
#if DEBUG_FOCUS
    LOGD("preemptInputDispatch");
#endif

    bool preemptedOne;
    { // acquire lock
        AutoMutex _l(mLock);
        preemptedOne = preemptInputDispatchInnerLocked();
    } // release lock

    if (preemptedOne) {
        // Wake up the poll loop so it can get a head start dispatching the next event.
        mPollLoop->wake();
    }
}

bool InputDispatcher::preemptInputDispatchInnerLocked() {
    bool preemptedOne = false;
    for (size_t i = 0; i < mActiveConnections.size(); i++) {
        Connection* connection = mActiveConnections[i];
        if (connection->hasPendingSyncTarget()) {
#if DEBUG_DISPATCH_CYCLE
            LOGD("channel '%s' ~ Preempted pending synchronous dispatch",
                    connection->getInputChannelName());
#endif
            connection->preemptSyncTarget();
            preemptedOne = true;
        }
    }
    return preemptedOne;
}

void InputDispatcher::logDispatchStateLocked() {
    String8 dump;
    dumpDispatchStateLocked(dump);
    LOGD("%s", dump.string());
}

void InputDispatcher::dumpDispatchStateLocked(String8& dump) {
    dump.appendFormat("  dispatchEnabled: %d\n", mDispatchEnabled);
    dump.appendFormat("  dispatchFrozen: %d\n", mDispatchFrozen);

    if (mFocusedApplication) {
        dump.appendFormat("  focusedApplication: name='%s', dispatchingTimeout=%0.3fms\n",
                mFocusedApplication->name.string(),
                mFocusedApplication->dispatchingTimeout / 1000000.0);
    } else {
        dump.append("  focusedApplication: <null>\n");
    }
    dump.appendFormat("  focusedWindow: '%s'\n",
            mFocusedWindow != NULL ? mFocusedWindow->inputChannel->getName().string() : "<null>");
    dump.appendFormat("  touchedWindow: '%s', touchDown=%d\n",
            mTouchedWindow != NULL ? mTouchedWindow->inputChannel->getName().string() : "<null>",
            mTouchDown);
    for (size_t i = 0; i < mTouchedWallpaperWindows.size(); i++) {
        dump.appendFormat("  touchedWallpaperWindows[%d]: '%s'\n",
                i, mTouchedWallpaperWindows[i]->inputChannel->getName().string());
    }
    for (size_t i = 0; i < mWindows.size(); i++) {
        dump.appendFormat("  windows[%d]: '%s', paused=%s, hasFocus=%s, hasWallpaper=%s, "
                "visible=%s, flags=0x%08x, type=0x%08x, "
                "frame=[%d,%d][%d,%d], "
                "visibleFrame=[%d,%d][%d,%d], "
                "touchableArea=[%d,%d][%d,%d], "
                "ownerPid=%d, ownerUid=%d, dispatchingTimeout=%0.3fms\n",
                i, mWindows[i].inputChannel->getName().string(),
                toString(mWindows[i].paused),
                toString(mWindows[i].hasFocus),
                toString(mWindows[i].hasWallpaper),
                toString(mWindows[i].visible),
                mWindows[i].layoutParamsFlags, mWindows[i].layoutParamsType,
                mWindows[i].frameLeft, mWindows[i].frameTop,
                mWindows[i].frameRight, mWindows[i].frameBottom,
                mWindows[i].visibleFrameLeft, mWindows[i].visibleFrameTop,
                mWindows[i].visibleFrameRight, mWindows[i].visibleFrameBottom,
                mWindows[i].touchableAreaLeft, mWindows[i].touchableAreaTop,
                mWindows[i].touchableAreaRight, mWindows[i].touchableAreaBottom,
                mWindows[i].ownerPid, mWindows[i].ownerUid,
                mWindows[i].dispatchingTimeout / 1000000.0);
    }

    for (size_t i = 0; i < mMonitoringChannels.size(); i++) {
        const sp<InputChannel>& channel = mMonitoringChannels[i];
        dump.appendFormat("  monitoringChannel[%d]: '%s'\n",
                i, channel->getName().string());
    }

    for (size_t i = 0; i < mActiveConnections.size(); i++) {
        const Connection* connection = mActiveConnections[i];
        dump.appendFormat("  activeConnection[%d]: '%s', status=%s, hasPendingSyncTarget=%s, "
                "inputState.isNeutral=%s, inputState.isOutOfSync=%s\n",
                i, connection->getInputChannelName(), connection->getStatusLabel(),
                toString(connection->hasPendingSyncTarget()),
                toString(connection->inputState.isNeutral()),
                toString(connection->inputState.isOutOfSync()));
    }

    if (isAppSwitchPendingLocked()) {
        dump.appendFormat("  appSwitch: pending, due in %01.1fms\n",
                (mAppSwitchDueTime - now()) / 1000000.0);
    } else {
        dump.append("  appSwitch: not pending\n");
    }
}

status_t InputDispatcher::registerInputChannel(const sp<InputChannel>& inputChannel, bool monitor) {
#if DEBUG_REGISTRATION
    LOGD("channel '%s' ~ registerInputChannel - monitor=%s", inputChannel->getName().string(),
            toString(monitor));
#endif

    { // acquire lock
        AutoMutex _l(mLock);

        if (getConnectionIndex(inputChannel) >= 0) {
            LOGW("Attempted to register already registered input channel '%s'",
                    inputChannel->getName().string());
            return BAD_VALUE;
        }

        sp<Connection> connection = new Connection(inputChannel);
        status_t status = connection->initialize();
        if (status) {
            LOGE("Failed to initialize input publisher for input channel '%s', status=%d",
                    inputChannel->getName().string(), status);
            return status;
        }

        int32_t receiveFd = inputChannel->getReceivePipeFd();
        mConnectionsByReceiveFd.add(receiveFd, connection);

        if (monitor) {
            mMonitoringChannels.push(inputChannel);
        }

        mPollLoop->setCallback(receiveFd, POLLIN, handleReceiveCallback, this);

        runCommandsLockedInterruptible();
    } // release lock
    return OK;
}

status_t InputDispatcher::unregisterInputChannel(const sp<InputChannel>& inputChannel) {
#if DEBUG_REGISTRATION
    LOGD("channel '%s' ~ unregisterInputChannel", inputChannel->getName().string());
#endif

    { // acquire lock
        AutoMutex _l(mLock);

        ssize_t connectionIndex = getConnectionIndex(inputChannel);
        if (connectionIndex < 0) {
            LOGW("Attempted to unregister already unregistered input channel '%s'",
                    inputChannel->getName().string());
            return BAD_VALUE;
        }

        sp<Connection> connection = mConnectionsByReceiveFd.valueAt(connectionIndex);
        mConnectionsByReceiveFd.removeItemsAt(connectionIndex);

        connection->status = Connection::STATUS_ZOMBIE;

        for (size_t i = 0; i < mMonitoringChannels.size(); i++) {
            if (mMonitoringChannels[i] == inputChannel) {
                mMonitoringChannels.removeAt(i);
                break;
            }
        }

        mPollLoop->removeCallback(inputChannel->getReceivePipeFd());

        nsecs_t currentTime = now();
        abortDispatchCycleLocked(currentTime, connection, true /*broken*/);

        runCommandsLockedInterruptible();
    } // release lock

    // Wake the poll loop because removing the connection may have changed the current
    // synchronization state.
    mPollLoop->wake();
    return OK;
}

ssize_t InputDispatcher::getConnectionIndex(const sp<InputChannel>& inputChannel) {
    ssize_t connectionIndex = mConnectionsByReceiveFd.indexOfKey(inputChannel->getReceivePipeFd());
    if (connectionIndex >= 0) {
        sp<Connection> connection = mConnectionsByReceiveFd.valueAt(connectionIndex);
        if (connection->inputChannel.get() == inputChannel.get()) {
            return connectionIndex;
        }
    }

    return -1;
}

void InputDispatcher::activateConnectionLocked(Connection* connection) {
    for (size_t i = 0; i < mActiveConnections.size(); i++) {
        if (mActiveConnections.itemAt(i) == connection) {
            return;
        }
    }
    mActiveConnections.add(connection);
}

void InputDispatcher::deactivateConnectionLocked(Connection* connection) {
    for (size_t i = 0; i < mActiveConnections.size(); i++) {
        if (mActiveConnections.itemAt(i) == connection) {
            mActiveConnections.removeAt(i);
            return;
        }
    }
}

void InputDispatcher::onDispatchCycleStartedLocked(
        nsecs_t currentTime, const sp<Connection>& connection) {
}

void InputDispatcher::onDispatchCycleFinishedLocked(
        nsecs_t currentTime, const sp<Connection>& connection, bool recoveredFromANR) {
    if (recoveredFromANR) {
        LOGI("channel '%s' ~ Recovered from ANR.  %01.1fms since event, "
                "%01.1fms since dispatch, %01.1fms since ANR",
                connection->getInputChannelName(),
                connection->getEventLatencyMillis(currentTime),
                connection->getDispatchLatencyMillis(currentTime),
                connection->getANRLatencyMillis(currentTime));

        CommandEntry* commandEntry = postCommandLocked(
                & InputDispatcher::doNotifyInputChannelRecoveredFromANRLockedInterruptible);
        commandEntry->connection = connection;
    }
}

void InputDispatcher::onDispatchCycleANRLocked(
        nsecs_t currentTime, const sp<Connection>& connection) {
    LOGI("channel '%s' ~ Not responding!  %01.1fms since event, %01.1fms since dispatch",
            connection->getInputChannelName(),
            connection->getEventLatencyMillis(currentTime),
            connection->getDispatchLatencyMillis(currentTime));

    CommandEntry* commandEntry = postCommandLocked(
            & InputDispatcher::doNotifyInputChannelANRLockedInterruptible);
    commandEntry->connection = connection;
}

void InputDispatcher::onDispatchCycleBrokenLocked(
        nsecs_t currentTime, const sp<Connection>& connection) {
    LOGE("channel '%s' ~ Channel is unrecoverably broken and will be disposed!",
            connection->getInputChannelName());

    CommandEntry* commandEntry = postCommandLocked(
            & InputDispatcher::doNotifyInputChannelBrokenLockedInterruptible);
    commandEntry->connection = connection;
}

void InputDispatcher::doNotifyConfigurationChangedInterruptible(
        CommandEntry* commandEntry) {
    mLock.unlock();

    mPolicy->notifyConfigurationChanged(commandEntry->eventTime);

    mLock.lock();
}

void InputDispatcher::doNotifyInputChannelBrokenLockedInterruptible(
        CommandEntry* commandEntry) {
    sp<Connection> connection = commandEntry->connection;

    if (connection->status != Connection::STATUS_ZOMBIE) {
        mLock.unlock();

        mPolicy->notifyInputChannelBroken(connection->inputChannel);

        mLock.lock();
    }
}

void InputDispatcher::doNotifyInputChannelANRLockedInterruptible(
        CommandEntry* commandEntry) {
    sp<Connection> connection = commandEntry->connection;

    if (connection->status != Connection::STATUS_ZOMBIE) {
        mLock.unlock();

        nsecs_t newTimeout = mPolicy->notifyInputChannelANR(connection->inputChannel);

        mLock.lock();

        nsecs_t currentTime = now();
        resumeAfterTimeoutDispatchCycleLocked(currentTime, connection, newTimeout);
    }
}

void InputDispatcher::doNotifyInputChannelRecoveredFromANRLockedInterruptible(
        CommandEntry* commandEntry) {
    sp<Connection> connection = commandEntry->connection;

    if (connection->status != Connection::STATUS_ZOMBIE) {
        mLock.unlock();

        mPolicy->notifyInputChannelRecoveredFromANR(connection->inputChannel);

        mLock.lock();
    }
}

void InputDispatcher::doInterceptKeyBeforeDispatchingLockedInterruptible(
        CommandEntry* commandEntry) {
    KeyEntry* entry = commandEntry->keyEntry;
    mReusableKeyEvent.initialize(entry->deviceId, entry->source, entry->action, entry->flags,
            entry->keyCode, entry->scanCode, entry->metaState, entry->repeatCount,
            entry->downTime, entry->eventTime);

    mLock.unlock();

    bool consumed = mPolicy->interceptKeyBeforeDispatching(commandEntry->inputChannel,
            & mReusableKeyEvent, entry->policyFlags);

    mLock.lock();

    entry->interceptKeyResult = consumed
            ? KeyEntry::INTERCEPT_KEY_RESULT_SKIP
            : KeyEntry::INTERCEPT_KEY_RESULT_CONTINUE;
    mAllocator.releaseKeyEntry(entry);
}

void InputDispatcher::doPokeUserActivityLockedInterruptible(CommandEntry* commandEntry) {
    mLock.unlock();

    mPolicy->pokeUserActivity(commandEntry->eventTime, commandEntry->windowType,
            commandEntry->userActivityEventType);

    mLock.lock();
}

void InputDispatcher::doTargetsNotReadyTimeoutLockedInterruptible(
        CommandEntry* commandEntry) {
    mLock.unlock();

    nsecs_t newTimeout;
    if (commandEntry->inputChannel.get()) {
        newTimeout = mPolicy->notifyInputChannelANR(commandEntry->inputChannel);
    } else if (commandEntry->inputApplicationHandle.get()) {
        newTimeout = mPolicy->notifyANR(commandEntry->inputApplicationHandle);
    } else {
        newTimeout = 0;
    }

    mLock.lock();

    resumeAfterTargetsNotReadyTimeoutLocked(newTimeout);
}

void InputDispatcher::dump(String8& dump) {
    dumpDispatchStateLocked(dump);
}


// --- InputDispatcher::Allocator ---

InputDispatcher::Allocator::Allocator() {
}

void InputDispatcher::Allocator::initializeEventEntry(EventEntry* entry, int32_t type,
        nsecs_t eventTime) {
    entry->type = type;
    entry->refCount = 1;
    entry->dispatchInProgress = false;
    entry->eventTime = eventTime;
    entry->injectionResult = INPUT_EVENT_INJECTION_PENDING;
    entry->injectionIsAsync = false;
    entry->injectorPid = -1;
    entry->injectorUid = -1;
    entry->pendingSyncDispatches = 0;
}

InputDispatcher::ConfigurationChangedEntry*
InputDispatcher::Allocator::obtainConfigurationChangedEntry(nsecs_t eventTime) {
    ConfigurationChangedEntry* entry = mConfigurationChangeEntryPool.alloc();
    initializeEventEntry(entry, EventEntry::TYPE_CONFIGURATION_CHANGED, eventTime);
    return entry;
}

InputDispatcher::KeyEntry* InputDispatcher::Allocator::obtainKeyEntry(nsecs_t eventTime,
        int32_t deviceId, int32_t source, uint32_t policyFlags, int32_t action,
        int32_t flags, int32_t keyCode, int32_t scanCode, int32_t metaState,
        int32_t repeatCount, nsecs_t downTime) {
    KeyEntry* entry = mKeyEntryPool.alloc();
    initializeEventEntry(entry, EventEntry::TYPE_KEY, eventTime);

    entry->deviceId = deviceId;
    entry->source = source;
    entry->policyFlags = policyFlags;
    entry->action = action;
    entry->flags = flags;
    entry->keyCode = keyCode;
    entry->scanCode = scanCode;
    entry->metaState = metaState;
    entry->repeatCount = repeatCount;
    entry->downTime = downTime;
    entry->syntheticRepeat = false;
    entry->interceptKeyResult = KeyEntry::INTERCEPT_KEY_RESULT_UNKNOWN;
    return entry;
}

InputDispatcher::MotionEntry* InputDispatcher::Allocator::obtainMotionEntry(nsecs_t eventTime,
        int32_t deviceId, int32_t source, uint32_t policyFlags, int32_t action, int32_t flags,
        int32_t metaState, int32_t edgeFlags, float xPrecision, float yPrecision,
        nsecs_t downTime, uint32_t pointerCount,
        const int32_t* pointerIds, const PointerCoords* pointerCoords) {
    MotionEntry* entry = mMotionEntryPool.alloc();
    initializeEventEntry(entry, EventEntry::TYPE_MOTION, eventTime);

    entry->eventTime = eventTime;
    entry->deviceId = deviceId;
    entry->source = source;
    entry->policyFlags = policyFlags;
    entry->action = action;
    entry->flags = flags;
    entry->metaState = metaState;
    entry->edgeFlags = edgeFlags;
    entry->xPrecision = xPrecision;
    entry->yPrecision = yPrecision;
    entry->downTime = downTime;
    entry->pointerCount = pointerCount;
    entry->firstSample.eventTime = eventTime;
    entry->firstSample.next = NULL;
    entry->lastSample = & entry->firstSample;
    for (uint32_t i = 0; i < pointerCount; i++) {
        entry->pointerIds[i] = pointerIds[i];
        entry->firstSample.pointerCoords[i] = pointerCoords[i];
    }
    return entry;
}

InputDispatcher::DispatchEntry* InputDispatcher::Allocator::obtainDispatchEntry(
        EventEntry* eventEntry,
        int32_t targetFlags, float xOffset, float yOffset, nsecs_t timeout) {
    DispatchEntry* entry = mDispatchEntryPool.alloc();
    entry->eventEntry = eventEntry;
    eventEntry->refCount += 1;
    entry->targetFlags = targetFlags;
    entry->xOffset = xOffset;
    entry->yOffset = yOffset;
    entry->timeout = timeout;
    entry->inProgress = false;
    entry->headMotionSample = NULL;
    entry->tailMotionSample = NULL;
    return entry;
}

InputDispatcher::CommandEntry* InputDispatcher::Allocator::obtainCommandEntry(Command command) {
    CommandEntry* entry = mCommandEntryPool.alloc();
    entry->command = command;
    return entry;
}

void InputDispatcher::Allocator::releaseEventEntry(EventEntry* entry) {
    switch (entry->type) {
    case EventEntry::TYPE_CONFIGURATION_CHANGED:
        releaseConfigurationChangedEntry(static_cast<ConfigurationChangedEntry*>(entry));
        break;
    case EventEntry::TYPE_KEY:
        releaseKeyEntry(static_cast<KeyEntry*>(entry));
        break;
    case EventEntry::TYPE_MOTION:
        releaseMotionEntry(static_cast<MotionEntry*>(entry));
        break;
    default:
        assert(false);
        break;
    }
}

void InputDispatcher::Allocator::releaseConfigurationChangedEntry(
        ConfigurationChangedEntry* entry) {
    entry->refCount -= 1;
    if (entry->refCount == 0) {
        mConfigurationChangeEntryPool.free(entry);
    } else {
        assert(entry->refCount > 0);
    }
}

void InputDispatcher::Allocator::releaseKeyEntry(KeyEntry* entry) {
    entry->refCount -= 1;
    if (entry->refCount == 0) {
        mKeyEntryPool.free(entry);
    } else {
        assert(entry->refCount > 0);
    }
}

void InputDispatcher::Allocator::releaseMotionEntry(MotionEntry* entry) {
    entry->refCount -= 1;
    if (entry->refCount == 0) {
        for (MotionSample* sample = entry->firstSample.next; sample != NULL; ) {
            MotionSample* next = sample->next;
            mMotionSamplePool.free(sample);
            sample = next;
        }
        mMotionEntryPool.free(entry);
    } else {
        assert(entry->refCount > 0);
    }
}

void InputDispatcher::Allocator::releaseDispatchEntry(DispatchEntry* entry) {
    releaseEventEntry(entry->eventEntry);
    mDispatchEntryPool.free(entry);
}

void InputDispatcher::Allocator::releaseCommandEntry(CommandEntry* entry) {
    mCommandEntryPool.free(entry);
}

void InputDispatcher::Allocator::appendMotionSample(MotionEntry* motionEntry,
        nsecs_t eventTime, const PointerCoords* pointerCoords) {
    MotionSample* sample = mMotionSamplePool.alloc();
    sample->eventTime = eventTime;
    uint32_t pointerCount = motionEntry->pointerCount;
    for (uint32_t i = 0; i < pointerCount; i++) {
        sample->pointerCoords[i] = pointerCoords[i];
    }

    sample->next = NULL;
    motionEntry->lastSample->next = sample;
    motionEntry->lastSample = sample;
}


// --- InputDispatcher::EventEntry ---

void InputDispatcher::EventEntry::recycle() {
    injectionResult = INPUT_EVENT_INJECTION_PENDING;
    dispatchInProgress = false;
    pendingSyncDispatches = 0;
}


// --- InputDispatcher::KeyEntry ---

void InputDispatcher::KeyEntry::recycle() {
    EventEntry::recycle();
    syntheticRepeat = false;
    interceptKeyResult = INTERCEPT_KEY_RESULT_UNKNOWN;
}


// --- InputDispatcher::MotionEntry ---

uint32_t InputDispatcher::MotionEntry::countSamples() const {
    uint32_t count = 1;
    for (MotionSample* sample = firstSample.next; sample != NULL; sample = sample->next) {
        count += 1;
    }
    return count;
}


// --- InputDispatcher::InputState ---

InputDispatcher::InputState::InputState() :
        mIsOutOfSync(false) {
}

InputDispatcher::InputState::~InputState() {
}

bool InputDispatcher::InputState::isNeutral() const {
    return mKeyMementos.isEmpty() && mMotionMementos.isEmpty();
}

bool InputDispatcher::InputState::isOutOfSync() const {
    return mIsOutOfSync;
}

void InputDispatcher::InputState::setOutOfSync() {
    if (! isNeutral()) {
        mIsOutOfSync = true;
    }
}

void InputDispatcher::InputState::resetOutOfSync() {
    mIsOutOfSync = false;
}

InputDispatcher::InputState::Consistency InputDispatcher::InputState::trackEvent(
        const EventEntry* entry) {
    switch (entry->type) {
    case EventEntry::TYPE_KEY:
        return trackKey(static_cast<const KeyEntry*>(entry));

    case EventEntry::TYPE_MOTION:
        return trackMotion(static_cast<const MotionEntry*>(entry));

    default:
        return CONSISTENT;
    }
}

InputDispatcher::InputState::Consistency InputDispatcher::InputState::trackKey(
        const KeyEntry* entry) {
    int32_t action = entry->action;
    for (size_t i = 0; i < mKeyMementos.size(); i++) {
        KeyMemento& memento = mKeyMementos.editItemAt(i);
        if (memento.deviceId == entry->deviceId
                && memento.source == entry->source
                && memento.keyCode == entry->keyCode
                && memento.scanCode == entry->scanCode) {
            switch (action) {
            case AKEY_EVENT_ACTION_UP:
                mKeyMementos.removeAt(i);
                if (isNeutral()) {
                    mIsOutOfSync = false;
                }
                return CONSISTENT;

            case AKEY_EVENT_ACTION_DOWN:
                return TOLERABLE;

            default:
                return BROKEN;
            }
        }
    }

    switch (action) {
    case AKEY_EVENT_ACTION_DOWN: {
        mKeyMementos.push();
        KeyMemento& memento = mKeyMementos.editTop();
        memento.deviceId = entry->deviceId;
        memento.source = entry->source;
        memento.keyCode = entry->keyCode;
        memento.scanCode = entry->scanCode;
        memento.downTime = entry->downTime;
        return CONSISTENT;
    }

    default:
        return BROKEN;
    }
}

InputDispatcher::InputState::Consistency InputDispatcher::InputState::trackMotion(
        const MotionEntry* entry) {
    int32_t action = entry->action & AMOTION_EVENT_ACTION_MASK;
    for (size_t i = 0; i < mMotionMementos.size(); i++) {
        MotionMemento& memento = mMotionMementos.editItemAt(i);
        if (memento.deviceId == entry->deviceId
                && memento.source == entry->source) {
            switch (action) {
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                mMotionMementos.removeAt(i);
                if (isNeutral()) {
                    mIsOutOfSync = false;
                }
                return CONSISTENT;

            case AMOTION_EVENT_ACTION_DOWN:
                return TOLERABLE;

            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                if (entry->pointerCount == memento.pointerCount + 1) {
                    memento.setPointers(entry);
                    return CONSISTENT;
                }
                return BROKEN;

            case AMOTION_EVENT_ACTION_POINTER_UP:
                if (entry->pointerCount == memento.pointerCount - 1) {
                    memento.setPointers(entry);
                    return CONSISTENT;
                }
                return BROKEN;

            case AMOTION_EVENT_ACTION_MOVE:
                if (entry->pointerCount == memento.pointerCount) {
                    return CONSISTENT;
                }
                return BROKEN;

            default:
                return BROKEN;
            }
        }
    }

    switch (action) {
    case AMOTION_EVENT_ACTION_DOWN: {
        mMotionMementos.push();
        MotionMemento& memento = mMotionMementos.editTop();
        memento.deviceId = entry->deviceId;
        memento.source = entry->source;
        memento.xPrecision = entry->xPrecision;
        memento.yPrecision = entry->yPrecision;
        memento.downTime = entry->downTime;
        memento.setPointers(entry);
        return CONSISTENT;
    }

    default:
        return BROKEN;
    }
}

void InputDispatcher::InputState::MotionMemento::setPointers(const MotionEntry* entry) {
    pointerCount = entry->pointerCount;
    for (uint32_t i = 0; i < entry->pointerCount; i++) {
        pointerIds[i] = entry->pointerIds[i];
        pointerCoords[i] = entry->lastSample->pointerCoords[i];
    }
}

void InputDispatcher::InputState::synthesizeCancelationEvents(
        Allocator* allocator, Vector<EventEntry*>& outEvents) const {
    for (size_t i = 0; i < mKeyMementos.size(); i++) {
        const KeyMemento& memento = mKeyMementos.itemAt(i);
        outEvents.push(allocator->obtainKeyEntry(now(),
                memento.deviceId, memento.source, 0,
                AKEY_EVENT_ACTION_UP, AKEY_EVENT_FLAG_CANCELED,
                memento.keyCode, memento.scanCode, 0, 0, memento.downTime));
    }

    for (size_t i = 0; i < mMotionMementos.size(); i++) {
        const MotionMemento& memento = mMotionMementos.itemAt(i);
        outEvents.push(allocator->obtainMotionEntry(now(),
                memento.deviceId, memento.source, 0,
                AMOTION_EVENT_ACTION_CANCEL, 0, 0, 0,
                memento.xPrecision, memento.yPrecision, memento.downTime,
                memento.pointerCount, memento.pointerIds, memento.pointerCoords));
    }
}

void InputDispatcher::InputState::clear() {
    mKeyMementos.clear();
    mMotionMementos.clear();
    mIsOutOfSync = false;
}


// --- InputDispatcher::Connection ---

InputDispatcher::Connection::Connection(const sp<InputChannel>& inputChannel) :
        status(STATUS_NORMAL), inputChannel(inputChannel), inputPublisher(inputChannel),
        nextTimeoutTime(LONG_LONG_MAX),
        lastEventTime(LONG_LONG_MAX), lastDispatchTime(LONG_LONG_MAX),
        lastANRTime(LONG_LONG_MAX) {
}

InputDispatcher::Connection::~Connection() {
}

status_t InputDispatcher::Connection::initialize() {
    return inputPublisher.initialize();
}

void InputDispatcher::Connection::setNextTimeoutTime(nsecs_t currentTime, nsecs_t timeout) {
    nextTimeoutTime = (timeout >= 0) ? currentTime + timeout : LONG_LONG_MAX;
}

void InputDispatcher::Connection::resetTimeout(nsecs_t currentTime) {
    if (outboundQueue.isEmpty()) {
        nextTimeoutTime = LONG_LONG_MAX;
    } else {
        setNextTimeoutTime(currentTime, outboundQueue.headSentinel.next->timeout);
    }
}

const char* InputDispatcher::Connection::getStatusLabel() const {
    switch (status) {
    case STATUS_NORMAL:
        return "NORMAL";

    case STATUS_BROKEN:
        return "BROKEN";

    case STATUS_NOT_RESPONDING:
        return "NOT_RESPONDING";

    case STATUS_ZOMBIE:
        return "ZOMBIE";

    default:
        return "UNKNOWN";
    }
}

InputDispatcher::DispatchEntry* InputDispatcher::Connection::findQueuedDispatchEntryForEvent(
        const EventEntry* eventEntry) const {
    for (DispatchEntry* dispatchEntry = outboundQueue.tailSentinel.prev;
            dispatchEntry != & outboundQueue.headSentinel; dispatchEntry = dispatchEntry->prev) {
        if (dispatchEntry->eventEntry == eventEntry) {
            return dispatchEntry;
        }
    }
    return NULL;
}


// --- InputDispatcher::CommandEntry ---

InputDispatcher::CommandEntry::CommandEntry() :
    keyEntry(NULL) {
}

InputDispatcher::CommandEntry::~CommandEntry() {
}


// --- InputDispatcherThread ---

InputDispatcherThread::InputDispatcherThread(const sp<InputDispatcherInterface>& dispatcher) :
        Thread(/*canCallJava*/ true), mDispatcher(dispatcher) {
}

InputDispatcherThread::~InputDispatcherThread() {
}

bool InputDispatcherThread::threadLoop() {
    mDispatcher->dispatchOnce();
    return true;
}

} // namespace android

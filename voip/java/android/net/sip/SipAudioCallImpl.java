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

package android.net.sip;

import gov.nist.javax.sdp.fields.SDPKeywords;

import android.content.Context;
import android.media.AudioManager;
import android.media.Ringtone;
import android.media.RingtoneManager;
import android.media.ToneGenerator;
import android.net.Uri;
import android.net.rtp.AudioCodec;
import android.net.rtp.AudioGroup;
import android.net.rtp.AudioStream;
import android.net.rtp.RtpStream;
import android.os.Message;
import android.os.RemoteException;
import android.os.Vibrator;
import android.provider.Settings;
import android.util.Log;

import java.io.IOException;
import java.net.InetAddress;
import java.text.ParseException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.sdp.SdpException;

/**
 * Class that handles an audio call over SIP. 
 */
/** @hide */
public class SipAudioCallImpl extends SipSessionAdapter
        implements SipAudioCall {
    private static final String TAG = SipAudioCallImpl.class.getSimpleName();
    private static final boolean RELEASE_SOCKET = true;
    private static final boolean DONT_RELEASE_SOCKET = false;
    private static final String AUDIO = "audio";
    private static final int DTMF = 101;
    private static final int SESSION_TIMEOUT = 5; // in seconds

    private Context mContext;
    private SipProfile mLocalProfile;
    private SipAudioCall.Listener mListener;
    private ISipSession mSipSession;
    private SdpSessionDescription mPeerSd;

    private AudioStream mAudioStream;
    private AudioGroup mAudioGroup;
    private SdpSessionDescription.AudioCodec mCodec;
    private long mSessionId = -1L; // SDP session ID
    private boolean mInCall = false;
    private boolean mMuted = false;
    private boolean mHold = false;

    private boolean mRingbackToneEnabled = true;
    private boolean mRingtoneEnabled = true;
    private Ringtone mRingtone;
    private ToneGenerator mRingbackTone;

    private SipProfile mPendingCallRequest;

    private SipErrorCode mErrorCode;
    private String mErrorMessage;

    public SipAudioCallImpl(Context context, SipProfile localProfile) {
        mContext = context;
        mLocalProfile = localProfile;
    }

    public void setListener(SipAudioCall.Listener listener) {
        setListener(listener, false);
    }

    public void setListener(SipAudioCall.Listener listener,
            boolean callbackImmediately) {
        mListener = listener;
        try {
            if ((listener == null) || !callbackImmediately) {
                // do nothing
            } else if (mErrorCode != null) {
                listener.onError(this, mErrorCode, mErrorMessage);
            } else if (mInCall) {
                if (mHold) {
                    listener.onCallHeld(this);
                } else {
                    listener.onCallEstablished(this);
                }
            } else {
                SipSessionState state = getState();
                switch (state) {
                    case READY_TO_CALL:
                        listener.onReadyToCall(this);
                        break;
                    case INCOMING_CALL:
                        listener.onRinging(this, getPeerProfile(mSipSession));
                        break;
                    case OUTGOING_CALL:
                        listener.onCalling(this);
                        break;
                    case OUTGOING_CALL_RING_BACK:
                        listener.onRingingBack(this);
                        break;
                }
            }
        } catch (Throwable t) {
            Log.e(TAG, "setListener()", t);
        }
    }

    public synchronized boolean isInCall() {
        return mInCall;
    }

    public synchronized boolean isOnHold() {
        return mHold;
    }

    public void close() {
        close(true);
    }

    private synchronized void close(boolean closeRtp) {
        if (closeRtp) stopCall(RELEASE_SOCKET);
        stopRingbackTone();
        stopRinging();

        mInCall = false;
        mHold = false;
        mSessionId = -1L;
        mErrorCode = null;
        mErrorMessage = null;

        if (mSipSession != null) {
            try {
                mSipSession.setListener(null);
            } catch (RemoteException e) {
                // don't care
            }
            mSipSession = null;
        }
    }

    public synchronized SipProfile getLocalProfile() {
        return mLocalProfile;
    }

    public synchronized SipProfile getPeerProfile() {
        try {
            return (mSipSession == null) ? null : mSipSession.getPeerProfile();
        } catch (RemoteException e) {
            return null;
        }
    }

    public synchronized SipSessionState getState() {
        if (mSipSession == null) return SipSessionState.READY_TO_CALL;
        try {
            return Enum.valueOf(SipSessionState.class, mSipSession.getState());
        } catch (RemoteException e) {
            return SipSessionState.REMOTE_ERROR;
        }
    }


    public synchronized ISipSession getSipSession() {
        return mSipSession;
    }

    @Override
    public void onCalling(ISipSession session) {
        Log.d(TAG, "calling... " + session);
        Listener listener = mListener;
        if (listener != null) {
            try {
                listener.onCalling(this);
            } catch (Throwable t) {
                Log.e(TAG, "onCalling()", t);
            }
        }
    }

    @Override
    public void onRingingBack(ISipSession session) {
        Log.d(TAG, "sip call ringing back: " + session);
        if (!mInCall) startRingbackTone();
        Listener listener = mListener;
        if (listener != null) {
            try {
                listener.onRingingBack(this);
            } catch (Throwable t) {
                Log.e(TAG, "onRingingBack()", t);
            }
        }
    }

    @Override
    public synchronized void onRinging(ISipSession session,
            SipProfile peerProfile, String sessionDescription) {
        try {
            if ((mSipSession == null) || !mInCall
                    || !session.getCallId().equals(mSipSession.getCallId())) {
                // should not happen
                session.endCall();
                return;
            }

            // session changing request
            try {
                mPeerSd = new SdpSessionDescription(sessionDescription);
                answerCall(SESSION_TIMEOUT);
            } catch (Throwable e) {
                Log.e(TAG, "onRinging()", e);
                session.endCall();
            }
        } catch (RemoteException e) {
            Log.e(TAG, "onRinging()", e);
        }
    }

    private synchronized void establishCall(String sessionDescription) {
        stopRingbackTone();
        stopRinging();
        try {
            SdpSessionDescription sd =
                    new SdpSessionDescription(sessionDescription);
            Log.d(TAG, "sip call established: " + sd);
            startCall(sd);
            mInCall = true;
        } catch (SdpException e) {
            Log.e(TAG, "createSessionDescription()", e);
        }
    }

    @Override
    public void onCallEstablished(ISipSession session,
            String sessionDescription) {
        establishCall(sessionDescription);
        Listener listener = mListener;
        if (listener != null) {
            try {
                if (mHold) {
                    listener.onCallHeld(this);
                } else {
                    listener.onCallEstablished(this);
                }
            } catch (Throwable t) {
                Log.e(TAG, "onCallEstablished()", t);
            }
        }
    }

    @Override
    public void onCallEnded(ISipSession session) {
        Log.d(TAG, "sip call ended: " + session);
        close();
        Listener listener = mListener;
        if (listener != null) {
            try {
                listener.onCallEnded(this);
            } catch (Throwable t) {
                Log.e(TAG, "onCallEnded()", t);
            }
        }
    }

    @Override
    public void onCallBusy(ISipSession session) {
        Log.d(TAG, "sip call busy: " + session);
        close(false);
        Listener listener = mListener;
        if (listener != null) {
            try {
                listener.onCallBusy(this);
            } catch (Throwable t) {
                Log.e(TAG, "onCallBusy()", t);
            }
        }
    }

    private SipErrorCode getErrorCode(String errorCode) {
        return Enum.valueOf(SipErrorCode.class, errorCode);
    }

    @Override
    public void onCallChangeFailed(ISipSession session, String errorCode,
            String message) {
        Log.d(TAG, "sip call change failed: " + message);
        mErrorCode = getErrorCode(errorCode);
        mErrorMessage = message;
        Listener listener = mListener;
        if (listener != null) {
            try {
                listener.onError(this, mErrorCode, message);
            } catch (Throwable t) {
                Log.e(TAG, "onCallBusy()", t);
            }
        }
    }

    @Override
    public void onError(ISipSession session, String errorCodeString,
            String message) {
        Log.d(TAG, "sip session error: " + errorCodeString + ": " + message);
        SipErrorCode errorCode = mErrorCode = getErrorCode(errorCodeString);
        mErrorMessage = message;
        synchronized (this) {
            if ((mErrorCode == SipErrorCode.DATA_CONNECTION_LOST)
                    || !isInCall()) {
                close(true);
            }
        }
        Listener listener = mListener;
        if (listener != null) {
            try {
                listener.onError(this, errorCode, message);
            } catch (Throwable t) {
                Log.e(TAG, "onError()", t);
            }
        }
    }

    public synchronized void attachCall(ISipSession session,
            String sessionDescription) throws SipException {
        mSipSession = session;
        try {
            mPeerSd = new SdpSessionDescription(sessionDescription);
            session.setListener(this);

            if (getState() == SipSessionState.INCOMING_CALL) startRinging();
        } catch (Throwable e) {
            Log.e(TAG, "attachCall()", e);
            throwSipException(e);
        }
    }

    public synchronized void makeCall(SipProfile peerProfile,
            SipManager sipManager, int timeout) throws SipException {
        try {
            mSipSession = sipManager.createSipSession(mLocalProfile, this);
            if (mSipSession == null) {
                throw new SipException(
                        "Failed to create SipSession; network available?");
            }
            mSipSession.makeCall(peerProfile, createOfferSessionDescription(),
                    timeout);
        } catch (Throwable e) {
            if (e instanceof SipException) {
                throw (SipException) e;
            } else {
                throwSipException(e);
            }
        }
    }

    public synchronized void endCall() throws SipException {
        try {
            stopRinging();
            stopCall(true);
            mInCall = false;

            // perform the above local ops first and then network op
            if (mSipSession != null) mSipSession.endCall();
        } catch (Throwable e) {
            throwSipException(e);
        }
    }

    public synchronized void holdCall(int timeout) throws SipException {
        if (mHold) return;
        try {
            mSipSession.changeCall(createHoldSessionDescription(), timeout);
            mHold = true;
        } catch (Throwable e) {
            throwSipException(e);
        }

        AudioGroup audioGroup = getAudioGroup();
        if (audioGroup != null) audioGroup.setMode(AudioGroup.MODE_ON_HOLD);
    }

    public synchronized void answerCall(int timeout) throws SipException {
        try {
            stopRinging();
            mSipSession.answerCall(createAnswerSessionDescription(), timeout);
        } catch (Throwable e) {
            Log.e(TAG, "answerCall()", e);
            throwSipException(e);
        }
    }

    public synchronized void continueCall(int timeout) throws SipException {
        if (!mHold) return;
        try {
            mHold = false;
            mSipSession.changeCall(createContinueSessionDescription(), timeout);
        } catch (Throwable e) {
            throwSipException(e);
        }

        AudioGroup audioGroup = getAudioGroup();
        if (audioGroup != null) audioGroup.setMode(AudioGroup.MODE_NORMAL);
    }

    private String createOfferSessionDescription() {
        AudioCodec[] codecs = AudioCodec.getSystemSupportedCodecs();
        return createSdpBuilder(true, convert(codecs)).build();
    }

    private String createAnswerSessionDescription() {
        try {
            // choose an acceptable media from mPeerSd to answer
            SdpSessionDescription.AudioCodec codec = getCodec(mPeerSd);
            SdpSessionDescription.Builder sdpBuilder =
                    createSdpBuilder(false, codec);
            if (mPeerSd.isSendOnly(AUDIO)) {
                sdpBuilder.addMediaAttribute(AUDIO, "recvonly", (String) null);
            } else if (mPeerSd.isReceiveOnly(AUDIO)) {
                sdpBuilder.addMediaAttribute(AUDIO, "sendonly", (String) null);
            }
            return sdpBuilder.build();
        } catch (SdpException e) {
            throw new RuntimeException(e);
        }
    }

    private String createHoldSessionDescription() {
        try {
            return createSdpBuilder(false, mCodec)
                    .addMediaAttribute(AUDIO, "sendonly", (String) null)
                    .build();
        } catch (SdpException e) {
            throw new RuntimeException(e);
        }
    }

    private String createContinueSessionDescription() {
        return createSdpBuilder(true, mCodec).build();
    }

    private String getMediaDescription(SdpSessionDescription.AudioCodec codec) {
        return String.format("%d %s/%d", codec.payloadType, codec.name,
                codec.sampleRate);
    }

    private long getSessionId() {
        if (mSessionId < 0) {
            mSessionId = System.currentTimeMillis();
        }
        return mSessionId;
    }

    private SdpSessionDescription.Builder createSdpBuilder(
            boolean addTelephoneEvent,
            SdpSessionDescription.AudioCodec... codecs) {
        String localIp = getLocalIp();
        SdpSessionDescription.Builder sdpBuilder;
        try {
            long sessionVersion = System.currentTimeMillis();
            sdpBuilder = new SdpSessionDescription.Builder("SIP Call")
                    .setOrigin(mLocalProfile, getSessionId(), sessionVersion,
                            SDPKeywords.IN, SDPKeywords.IPV4, localIp)
                    .setConnectionInfo(SDPKeywords.IN, SDPKeywords.IPV4,
                            localIp);
            List<Integer> codecIds = new ArrayList<Integer>();
            for (SdpSessionDescription.AudioCodec codec : codecs) {
                codecIds.add(codec.payloadType);
            }
            if (addTelephoneEvent) codecIds.add(DTMF);
            sdpBuilder.addMedia(AUDIO, getLocalMediaPort(), 1, "RTP/AVP",
                    codecIds.toArray(new Integer[codecIds.size()]));
            for (SdpSessionDescription.AudioCodec codec : codecs) {
                sdpBuilder.addMediaAttribute(AUDIO, "rtpmap",
                        getMediaDescription(codec));
            }
            if (addTelephoneEvent) {
                sdpBuilder.addMediaAttribute(AUDIO, "rtpmap",
                        DTMF + " telephone-event/8000");
            }
            // FIXME: deal with vbr codec
            sdpBuilder.addMediaAttribute(AUDIO, "ptime", "20");
        } catch (SdpException e) {
            throw new RuntimeException(e);
        }
        return sdpBuilder;
    }

    public synchronized void toggleMute() {
        AudioGroup audioGroup = getAudioGroup();
        if (audioGroup != null) {
            audioGroup.setMode(
                    mMuted ? AudioGroup.MODE_NORMAL : AudioGroup.MODE_MUTED);
            mMuted = !mMuted;
        }
    }

    public synchronized boolean isMuted() {
        return mMuted;
    }

    public synchronized void setSpeakerMode(boolean speakerMode) {
        ((AudioManager) mContext.getSystemService(Context.AUDIO_SERVICE))
                .setSpeakerphoneOn(speakerMode);
    }

    public void sendDtmf(int code) {
        sendDtmf(code, null);
    }

    public synchronized void sendDtmf(int code, Message result) {
        AudioGroup audioGroup = getAudioGroup();
        if ((audioGroup != null) && (mSipSession != null)
                && (SipSessionState.IN_CALL == getState())) {
            Log.v(TAG, "send DTMF: " + code);
            audioGroup.sendDtmf(code);
        }
        if (result != null) result.sendToTarget();
    }

    public synchronized AudioStream getAudioStream() {
        return mAudioStream;
    }

    public synchronized AudioGroup getAudioGroup() {
        if (mAudioGroup != null) return mAudioGroup;
        return ((mAudioStream == null) ? null : mAudioStream.getAudioGroup());
    }

    public synchronized void setAudioGroup(AudioGroup group) {
        if ((mAudioStream != null) && (mAudioStream.getAudioGroup() != null)) {
            mAudioStream.join(group);
        }
        mAudioGroup = group;
    }

    private SdpSessionDescription.AudioCodec getCodec(SdpSessionDescription sd) {
        HashMap<String, AudioCodec> acceptableCodecs =
                new HashMap<String, AudioCodec>();
        for (AudioCodec codec : AudioCodec.getSystemSupportedCodecs()) {
            acceptableCodecs.put(codec.name, codec);
        }
        for (SdpSessionDescription.AudioCodec codec : sd.getAudioCodecs()) {
            AudioCodec matchedCodec = acceptableCodecs.get(codec.name);
            if (matchedCodec != null) return codec;
        }
        Log.w(TAG, "no common codec is found, use PCM/0");
        return convert(AudioCodec.ULAW);
    }

    private AudioCodec convert(SdpSessionDescription.AudioCodec codec) {
        AudioCodec c = AudioCodec.getSystemSupportedCodec(codec.name);
        return ((c == null) ? AudioCodec.ULAW : c);
    }

    private SdpSessionDescription.AudioCodec convert(AudioCodec codec) {
        return new SdpSessionDescription.AudioCodec(codec.defaultType,
                codec.name, codec.sampleRate, codec.sampleCount);
    }

    private SdpSessionDescription.AudioCodec[] convert(AudioCodec[] codecs) {
        SdpSessionDescription.AudioCodec[] copies =
                new SdpSessionDescription.AudioCodec[codecs.length];
        for (int i = 0, len = codecs.length; i < len; i++) {
            copies[i] = convert(codecs[i]);
        }
        return copies;
    }

    private void startCall(SdpSessionDescription peerSd) {
        stopCall(DONT_RELEASE_SOCKET);

        mPeerSd = peerSd;
        String peerMediaAddress = peerSd.getPeerMediaAddress(AUDIO);
        // TODO: handle multiple media fields
        int peerMediaPort = peerSd.getPeerMediaPort(AUDIO);
        Log.i(TAG, "start audiocall " + peerMediaAddress + ":" + peerMediaPort);

        int localPort = getLocalMediaPort();
        int sampleRate = 8000;
        int frameSize = sampleRate / 50; // 160
        try {
            // TODO: get sample rate from sdp
            mCodec = getCodec(peerSd);

            AudioStream audioStream = mAudioStream;
            audioStream.associate(InetAddress.getByName(peerMediaAddress),
                    peerMediaPort);
            audioStream.setCodec(convert(mCodec), mCodec.payloadType);
            audioStream.setDtmfType(DTMF);
            Log.d(TAG, "start media: localPort=" + localPort + ", peer="
                    + peerMediaAddress + ":" + peerMediaPort);

            audioStream.setMode(RtpStream.MODE_NORMAL);
            if (!mHold) {
                // FIXME: won't work if peer is not sending nor receiving
                if (!peerSd.isSending(AUDIO)) {
                    Log.d(TAG, "   not receiving");
                    audioStream.setMode(RtpStream.MODE_SEND_ONLY);
                }
                if (!peerSd.isReceiving(AUDIO)) {
                    Log.d(TAG, "   not sending");
                    audioStream.setMode(RtpStream.MODE_RECEIVE_ONLY);
                }

                /* The recorder volume will be very low if the device is in
                 * IN_CALL mode. Therefore, we have to set the mode to NORMAL
                 * in order to have the normal microphone level.
                 */
                ((AudioManager) mContext.getSystemService
                        (Context.AUDIO_SERVICE))
                        .setMode(AudioManager.MODE_NORMAL);
            }

            // AudioGroup logic:
            AudioGroup audioGroup = getAudioGroup();
            if (mHold) {
                if (audioGroup != null) {
                    audioGroup.setMode(AudioGroup.MODE_ON_HOLD);
                }
                // don't create an AudioGroup here; doing so will fail if
                // there's another AudioGroup out there that's active
            } else {
                if (audioGroup == null) audioGroup = new AudioGroup();
                audioStream.join(audioGroup);
                if (mMuted) {
                    audioGroup.setMode(AudioGroup.MODE_MUTED);
                } else {
                    audioGroup.setMode(AudioGroup.MODE_NORMAL);
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "call()", e);
        }
    }

    private void stopCall(boolean releaseSocket) {
        Log.d(TAG, "stop audiocall");
        if (mAudioStream != null) {
            mAudioStream.join(null);

            if (releaseSocket) {
                mAudioStream.release();
                mAudioStream = null;
            }
        }
    }

    private int getLocalMediaPort() {
        if (mAudioStream != null) return mAudioStream.getLocalPort();
        try {
            AudioStream s = mAudioStream =
                    new AudioStream(InetAddress.getByName(getLocalIp()));
            return s.getLocalPort();
        } catch (IOException e) {
            Log.w(TAG, "getLocalMediaPort(): " + e);
            throw new RuntimeException(e);
        }
    }

    private String getLocalIp() {
        try {
            return mSipSession.getLocalIp();
        } catch (RemoteException e) {
            // FIXME
            return "127.0.0.1";
        }
    }

    public synchronized void setRingbackToneEnabled(boolean enabled) {
        mRingbackToneEnabled = enabled;
    }

    public synchronized void setRingtoneEnabled(boolean enabled) {
        mRingtoneEnabled = enabled;
    }

    private void startRingbackTone() {
        if (!mRingbackToneEnabled) return;
        if (mRingbackTone == null) {
            // The volume relative to other sounds in the stream
            int toneVolume = 80;
            mRingbackTone = new ToneGenerator(
                    AudioManager.STREAM_VOICE_CALL, toneVolume);
        }
        mRingbackTone.startTone(ToneGenerator.TONE_CDMA_LOW_PBX_L);
    }

    private void stopRingbackTone() {
        if (mRingbackTone != null) {
            mRingbackTone.stopTone();
            mRingbackTone.release();
            mRingbackTone = null;
        }
    }

    private void startRinging() {
        if (!mRingtoneEnabled) return;
        ((Vibrator) mContext.getSystemService(Context.VIBRATOR_SERVICE))
                .vibrate(new long[] {0, 1000, 1000}, 1);
        AudioManager am = (AudioManager)
                mContext.getSystemService(Context.AUDIO_SERVICE);
        if (am.getStreamVolume(AudioManager.STREAM_RING) > 0) {
            String ringtoneUri =
                    Settings.System.DEFAULT_RINGTONE_URI.toString();
            mRingtone = RingtoneManager.getRingtone(mContext,
                    Uri.parse(ringtoneUri));
            mRingtone.play();
        }
    }

    private void stopRinging() {
        ((Vibrator) mContext.getSystemService(Context.VIBRATOR_SERVICE))
                .cancel();
        if (mRingtone != null) mRingtone.stop();
    }

    private void throwSipException(Throwable throwable) throws SipException {
        if (throwable instanceof SipException) {
            throw (SipException) throwable;
        } else {
            throw new SipException("", throwable);
        }
    }

    private SipProfile getPeerProfile(ISipSession session) {
        try {
            return session.getPeerProfile();
        } catch (RemoteException e) {
            return null;
        }
    }
}

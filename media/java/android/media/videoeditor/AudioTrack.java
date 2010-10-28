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

package android.media.videoeditor;

import java.io.IOException;

/**
 * This class allows to handle an audio track. This audio file is mixed with the
 * audio samples of the media items.
 * {@hide}
 */
public class AudioTrack {
    // Instance variables
    private final String mUniqueId;
    private final String mFilename;
    private long mStartTimeMs;
    private long mTimelineDurationMs;
    private int mVolumePercent;
    private long mBeginBoundaryTimeMs;
    private long mEndBoundaryTimeMs;
    private boolean mLoop;
    private boolean mMuted;

    private final long mDurationMs;
    private final int mAudioChannels;
    private final int mAudioType;
    private final int mAudioBitrate;
    private final int mAudioSamplingFrequency;

    // Ducking variables
    private int mDuckingThreshold;
    private int mDuckedTrackVolume;
    private boolean mIsDuckingEnabled;

    // The audio waveform filename
    private String mAudioWaveformFilename;
    // The audio waveform data
    private WaveformData mWaveformData;

    /**
     * An object of this type cannot be instantiated by using the default
     * constructor
     */
    @SuppressWarnings("unused")
    private AudioTrack() throws IOException {
        this(null, null, null);
    }

    /**
     * Constructor
     *
     * @param editor The video editor reference
     * @param audioTrackId The audio track id
     * @param filename The absolute file name
     *
     * @throws IOException if file is not found
     * @throws IllegalArgumentException if file format is not supported or if
     *             the codec is not supported
     */
    public AudioTrack(VideoEditor editor, String audioTrackId, String filename)
            throws IOException {
        mUniqueId = audioTrackId;
        mFilename = filename;
        mStartTimeMs = 0;
        // TODO: This value represents to the duration of the audio file
        mDurationMs = 300000;
        // TODO: This value needs to be read from the audio track of the source
        // file
        mAudioChannels = 2;
        mAudioType = MediaProperties.ACODEC_AAC_LC;
        mAudioBitrate = 128000;
        mAudioSamplingFrequency = 44100;

        mTimelineDurationMs = mDurationMs;
        mVolumePercent = 100;

        // Play the entire audio track
        mBeginBoundaryTimeMs = 0;
        mEndBoundaryTimeMs = mDurationMs;

        // By default loop is disabled
        mLoop = false;

        // By default the audio track is not muted
        mMuted = false;

        // Ducking is enabled by default
        mDuckingThreshold = 0;
        mDuckedTrackVolume = 0;
        mIsDuckingEnabled = true;

        // The audio waveform file is generated later
        mAudioWaveformFilename = null;
        mWaveformData = null;
    }

    /**
     * Constructor
     *
     * @param editor The video editor reference
     * @param audioTrackId The audio track id
     * @param filename The audio filename
     * @param startTimeMs the start time in milliseconds (relative to the
     *              timeline)
     * @param beginMs start time in the audio track in milliseconds (relative to
     *            the beginning of the audio track)
     * @param endMs end time in the audio track in milliseconds (relative to the
     *            beginning of the audio track)
     * @param loop true to loop the audio track
     * @param volume The volume in percentage
     * @param muted true if the audio track is muted
     * @param threshold Ducking will be activated when the relative energy in
     *      the media items audio signal goes above this value. The valid
     *      range of values is 0 to 100.
     * @param duckedTrackVolume The relative volume of the audio track when ducking
     *      is active. The valid range of values is 0 to 100.
     * @param audioWaveformFilename The name of the waveform file
     *
     * @throws IOException if file is not found
     */
    AudioTrack(VideoEditor editor, String audioTrackId, String filename, long startTimeMs,
            long beginMs, long endMs, boolean loop, int volume, boolean muted,
            boolean duckingEnabled, int duckThreshold, int duckedTrackVolume,
            String audioWaveformFilename) throws IOException {
        mUniqueId = audioTrackId;
        mFilename = filename;
        mStartTimeMs = startTimeMs;

        // TODO: This value represents to the duration of the audio file
        mDurationMs = 300000;

        // TODO: This value needs to be read from the audio track of the source
        // file
        mAudioChannels = 2;
        mAudioType = MediaProperties.ACODEC_AAC_LC;
        mAudioBitrate = 128000;
        mAudioSamplingFrequency = 44100;

        mTimelineDurationMs = endMs - beginMs;
        mVolumePercent = volume;

        mBeginBoundaryTimeMs = beginMs;
        mEndBoundaryTimeMs = endMs;

        mLoop = loop;
        mMuted = muted;

        mIsDuckingEnabled = duckingEnabled;
        mDuckingThreshold = duckThreshold;
        mDuckedTrackVolume = duckedTrackVolume;

        mAudioWaveformFilename = audioWaveformFilename;
        if (audioWaveformFilename != null) {
            mWaveformData = new WaveformData(audioWaveformFilename);
        } else {
            mWaveformData = null;
        }
    }

    /**
     * @return The id of the audio track
     */
    public String getId() {
        return mUniqueId;
    }

    /**
     * Get the filename source for this audio track.
     *
     * @return The filename as an absolute file name
     */
    public String getFilename() {
        return mFilename;
    }

    /**
     * @return The number of audio channels in the source of this audio track
     */
    public int getAudioChannels() {
        return mAudioChannels;
    }

    /**
     * @return The audio codec of the source of this audio track
     */
    public int getAudioType() {
        return mAudioType;
    }

    /**
     * @return The audio sample frequency of the audio track
     */
    public int getAudioSamplingFrequency() {
        return mAudioSamplingFrequency;
    }

    /**
     * @return The audio bitrate of the audio track
     */
    public int getAudioBitrate() {
        return mAudioBitrate;
    }

    /**
     * Set the volume of this audio track as percentage of the volume in the
     * original audio source file.
     *
     * @param volumePercent Percentage of the volume to apply. If it is set to
     *            0, then volume becomes mute. It it is set to 100, then volume
     *            is same as original volume. It it is set to 200, then volume
     *            is doubled (provided that volume amplification is supported)
     *
     * @throws UnsupportedOperationException if volume amplification is
     *             requested and is not supported.
     */
    public void setVolume(int volumePercent) {
        mVolumePercent = volumePercent;
    }

    /**
     * Get the volume of the audio track as percentage of the volume in the
     * original audio source file.
     *
     * @return The volume in percentage
     */
    public int getVolume() {
        return mVolumePercent;
    }

    /**
     * @param muted true to mute the audio track
     */
    public void setMute(boolean muted) {
        mMuted = muted;
    }

    /**
     * @return true if the audio track is muted
     */
    public boolean isMuted() {
        return mMuted;
    }

    /**
     * Set the start time of this audio track relative to the storyboard
     * timeline. Default value is 0.
     *
     * @param startTimeMs the start time in milliseconds
     */
    public void setStartTime(long startTimeMs) {
        mStartTimeMs = startTimeMs;
    }

    /**
     * Get the start time of this audio track relative to the storyboard
     * timeline.
     *
     * @return The start time in milliseconds
     */
    public long getStartTime() {
        return mStartTimeMs;
    }

    /**
     * @return The duration in milliseconds. This value represents the audio
     *         track duration (not looped)
     */
    public long getDuration() {
        return mDurationMs;
    }

    /**
     * @return The timeline duration. If looping is enabled this value
     *         represents the duration of the looped audio track, otherwise it
     *         is the duration of the audio track (mDurationMs).
     */
    public long getTimelineDuration() {
        return mTimelineDurationMs;
    }

    /**
     * Sets the start and end marks for trimming an audio track
     *
     * @param beginMs start time in the audio track in milliseconds (relative to
     *            the beginning of the audio track)
     * @param endMs end time in the audio track in milliseconds (relative to the
     *            beginning of the audio track)
     */
    public void setExtractBoundaries(long beginMs, long endMs) {
        if (beginMs > mDurationMs) {
            throw new IllegalArgumentException("Invalid start time");
        }
        if (endMs > mDurationMs) {
            throw new IllegalArgumentException("Invalid end time");
        }

        mBeginBoundaryTimeMs = beginMs;
        mEndBoundaryTimeMs = endMs;
        if (mLoop) {
            // TODO: Compute mDurationMs (from the beginning of the loop until
            // the end of all the loops.
            mTimelineDurationMs = mEndBoundaryTimeMs - mBeginBoundaryTimeMs;
        } else {
            mTimelineDurationMs = mEndBoundaryTimeMs - mBeginBoundaryTimeMs;
        }
    }

    /**
     * @return The boundary begin time
     */
    public long getBoundaryBeginTime() {
        return mBeginBoundaryTimeMs;
    }

    /**
     * @return The boundary end time
     */
    public long getBoundaryEndTime() {
        return mEndBoundaryTimeMs;
    }

    /**
     * Enable the loop mode for this audio track. Note that only one of the
     * audio tracks in the timeline can have the loop mode enabled. When looping
     * is enabled the samples between mBeginBoundaryTimeMs and
     * mEndBoundaryTimeMs are looped.
     */
    public void enableLoop() {
        mLoop = true;
    }

    /**
     * Disable the loop mode
     */
    public void disableLoop() {
        mLoop = false;
    }

    /**
     * @return true if looping is enabled
     */
    public boolean isLooping() {
        return mLoop;
    }

    /**
     * Disable the audio duck effect
     */
    public void disableDucking() {
        mIsDuckingEnabled = false;
    }

    /**
     * Enable ducking by specifying the required parameters
     *
     * @param threshold Ducking will be activated when the relative energy in
     *      the media items audio signal goes above this value. The valid
     *      range of values is 0 to 100.
     * @param duckedTrackVolume The relative volume of the audio track when ducking
     *      is active. The valid range of values is 0 to 100.
     */
    public void enableDucking(int threshold, int duckedTrackVolume) {
        if (threshold < 0 || threshold > 100) {
            throw new IllegalArgumentException("Invalid threshold value: " + threshold);
        }

        if (duckedTrackVolume < 0 || duckedTrackVolume > 100) {
            throw new IllegalArgumentException("Invalid duckedTrackVolume value: "
                    + duckedTrackVolume);
        }

        mDuckingThreshold = threshold;
        mDuckedTrackVolume = duckedTrackVolume;
        mIsDuckingEnabled = true;
    }

    /**
     * @return true if ducking is enabled
     */
    public boolean isDuckingEnabled() {
        return mIsDuckingEnabled;
    }

    /**
     * @return The ducking threshold
     */
    public int getDuckingThreshhold() {
        return mDuckingThreshold;
    }

    /**
     * @return The ducked track volume
     */
    public int getDuckedTrackVolume() {
        return mDuckedTrackVolume;
    }

    /**
     * This API allows to generate a file containing the sample volume levels of
     * this audio track object. This function may take significant time and is
     * blocking. The filename can be retrieved using getAudioWaveformFilename().
     *
     * @param listener The progress listener
     *
     * @throws IOException if the output file cannot be created
     * @throws IllegalArgumentException if the audio file does not have a valid
     *             audio track
     */
    public void extractAudioWaveform(ExtractAudioWaveformProgressListener listener)
            throws IOException {
        // TODO: Set mAudioWaveformFilename at the end once the extract is
        // complete
        mWaveformData = new WaveformData(mAudioWaveformFilename);
    }

    /**
     * Get the audio waveform file name if extractAudioWaveform was successful.
     * The file format is as following:
     * <ul>
     * <li>first 4 bytes provide the number of samples for each value, as
     * big-endian signed</li>
     * <li>4 following bytes is the total number of values in the file, as
     * big-endian signed</li>
     * <li>then, all values follow as bytes</li>
     * </ul>
     *
     * @return the name of the file, null if the file does not exist
     */
    String getAudioWaveformFilename() {
        return mAudioWaveformFilename;
    }

    /**
     * @return The waveform data
     */
    public WaveformData getWaveformData() {
        return mWaveformData;
    }

    /*
     * {@inheritDoc}
     */
    @Override
    public boolean equals(Object object) {
        if (!(object instanceof AudioTrack)) {
            return false;
        }
        return mUniqueId.equals(((AudioTrack)object).mUniqueId);
    }

    /*
     * {@inheritDoc}
     */
    @Override
    public int hashCode() {
        return mUniqueId.hashCode();
    }
}

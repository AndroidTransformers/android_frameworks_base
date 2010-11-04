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

package com.android.systemui.statusbar.tablet;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.app.StatusBarManager;
import android.bluetooth.BluetoothAdapter;
import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.Resources;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.location.LocationManager;
import android.media.AudioManager;
import android.net.NetworkInfo;
import android.net.wifi.SupplicantState;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.AsyncTask;
import android.os.Handler;
import android.os.IBinder;
import android.os.IPowerManager;
import android.os.Message;
import android.os.RemoteException;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.provider.Settings;
import android.telephony.PhoneStateListener;
import android.telephony.ServiceState;
import android.telephony.SignalStrength;
import android.telephony.TelephonyManager;
import android.util.AttributeSet;
import android.util.Slog;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.IWindowManager;
import android.view.WindowManager;
import android.view.WindowManagerImpl;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RemoteViews;
import android.widget.ScrollView;
import android.widget.TextSwitcher;
import android.widget.TextView;
import android.widget.Toast;

import java.util.List;

import com.android.internal.telephony.IccCard;
import com.android.internal.telephony.TelephonyIntents;
import com.android.internal.telephony.cdma.EriInfo;
import com.android.internal.telephony.cdma.TtyIntent;

import com.android.server.WindowManagerService;

import com.android.systemui.statusbar.*;
import com.android.systemui.R;

public class SystemPanel extends LinearLayout implements StatusBarPanel {
    private static final String TAG = "SystemPanel";
    private static final boolean DEBUG = TabletStatusBarService.DEBUG;
    private static final boolean DEBUG_SIGNAL = false;

    private static final int MINIMUM_BACKLIGHT = android.os.Power.BRIGHTNESS_DIM + 5;
    private static final int MAXIMUM_BACKLIGHT = android.os.Power.BRIGHTNESS_ON;
    private static final int DEFAULT_BACKLIGHT = (int) (android.os.Power.BRIGHTNESS_ON * 0.4f);

    private TabletStatusBarService mBar;
    private boolean mAirplaneMode;

    private ImageButton mBrightnessButton;
    private ImageButton mSoundButton;
    private ImageButton mOrientationButton;
    private ImageButton mAirplaneButton;
    private ImageButton mGpsButton;
    private ImageButton mBluetoothButton;

    private ImageView mBatteryMeter;
    private ImageView mSignalMeter;

    private TextView mBatteryText;
    private TextView mSignalText;

    private final IWindowManager mWM;

    private final AudioManager mAudioManager;
    private final WifiManager mWifiManager;
    private final TelephonyManager mPhone;
    private final BluetoothAdapter mBluetoothAdapter;

    // state trackers for telephony code
    IccCard.State mSimState = IccCard.State.READY;
    int mPhoneState = TelephonyManager.CALL_STATE_IDLE;
    int mDataState = TelephonyManager.DATA_DISCONNECTED;
    ServiceState mServiceState;
    SignalStrength mSignalStrength;

    // state for the meters
    boolean mWifiEnabled, mWifiConnected;
    int mWifiLevel;
    String mWifiSsid;

    boolean mDataEnabled, mDataConnected, mDataRoaming;
    int mDataLevel;

    public boolean isInContentArea(int x, int y) {
        final int l = getPaddingLeft();
        final int r = getWidth() - getPaddingRight();
        final int t = getPaddingTop();
        final int b = getHeight() - getPaddingBottom();
        return x >= l && x < r && y >= t && y < b;
    }

    private BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();
            if (action.equals(AudioManager.RINGER_MODE_CHANGED_ACTION)) {
                refreshSound();
            } else if (action.equals(Intent.ACTION_BATTERY_CHANGED)) {
                updateBattery(intent);
            } else if (action.equals(WifiManager.RSSI_CHANGED_ACTION)
                    || action.equals(WifiManager.WIFI_STATE_CHANGED_ACTION)
                    || action.equals(WifiManager.SUPPLICANT_STATE_CHANGED_ACTION)
                    || action.equals(WifiManager.NETWORK_STATE_CHANGED_ACTION)) {
                updateWifiState(intent);
            } else if (action.equals(TelephonyIntents.ACTION_SIM_STATE_CHANGED)) {
                updateSimState(intent);
            } else if (action.equals(BluetoothAdapter.ACTION_STATE_CHANGED)) {
                refreshBluetooth();
            }
        }
    };

    private final void updateSimState(Intent intent) {
        String stateExtra = intent.getStringExtra(IccCard.INTENT_KEY_ICC_STATE);
        if (IccCard.INTENT_VALUE_ICC_ABSENT.equals(stateExtra)) {
            mSimState = IccCard.State.ABSENT;
        }
        else if (IccCard.INTENT_VALUE_ICC_READY.equals(stateExtra)) {
            mSimState = IccCard.State.READY;
        }
        else if (IccCard.INTENT_VALUE_ICC_LOCKED.equals(stateExtra)) {
            final String lockedReason = intent.getStringExtra(IccCard.INTENT_KEY_LOCKED_REASON);
            if (IccCard.INTENT_VALUE_LOCKED_ON_PIN.equals(lockedReason)) {
                mSimState = IccCard.State.PIN_REQUIRED;
            }
            else if (IccCard.INTENT_VALUE_LOCKED_ON_PUK.equals(lockedReason)) {
                mSimState = IccCard.State.PUK_REQUIRED;
            }
            else {
                mSimState = IccCard.State.NETWORK_LOCKED;
            }
        } else {
            mSimState = IccCard.State.UNKNOWN;
        }
        updateDataState();
    }

    private boolean isCdma() {
        return (mSignalStrength != null) && !mSignalStrength.isGsm();
    }

    private boolean isEvdo() {
        return ( (mServiceState != null)
                 && ((mServiceState.getRadioTechnology()
                        == ServiceState.RADIO_TECHNOLOGY_EVDO_0)
                     || (mServiceState.getRadioTechnology()
                        == ServiceState.RADIO_TECHNOLOGY_EVDO_A)
                     || (mServiceState.getRadioTechnology()
                        == ServiceState.RADIO_TECHNOLOGY_EVDO_B)));
    }

    private boolean hasService() {
        if (mServiceState != null) {
            switch (mServiceState.getState()) {
                case ServiceState.STATE_OUT_OF_SERVICE:
                case ServiceState.STATE_POWER_OFF:
                    return false;
                default:
                    return true;
            }
        } else {
            return false;
        }
    }

    private int getCdmaLevel() {
        if (mSignalStrength == null) return 0;
        final int cdmaDbm = mSignalStrength.getCdmaDbm();
        final int cdmaEcio = mSignalStrength.getCdmaEcio();
        int levelDbm = 0;
        int levelEcio = 0;

        if (cdmaDbm >= -75) levelDbm = 4;
        else if (cdmaDbm >= -85) levelDbm = 3;
        else if (cdmaDbm >= -95) levelDbm = 2;
        else if (cdmaDbm >= -100) levelDbm = 1;
        else levelDbm = 0;

        // Ec/Io are in dB*10
        if (cdmaEcio >= -90) levelEcio = 4;
        else if (cdmaEcio >= -110) levelEcio = 3;
        else if (cdmaEcio >= -130) levelEcio = 2;
        else if (cdmaEcio >= -150) levelEcio = 1;
        else levelEcio = 0;

        return (levelDbm < levelEcio) ? levelDbm : levelEcio;
    }

    private int getEvdoLevel() {
        if (mSignalStrength == null) return 0;
        int evdoDbm = mSignalStrength.getEvdoDbm();
        int evdoSnr = mSignalStrength.getEvdoSnr();
        int levelEvdoDbm = 0;
        int levelEvdoSnr = 0;

        if (evdoDbm >= -65) levelEvdoDbm = 4;
        else if (evdoDbm >= -75) levelEvdoDbm = 3;
        else if (evdoDbm >= -90) levelEvdoDbm = 2;
        else if (evdoDbm >= -105) levelEvdoDbm = 1;
        else levelEvdoDbm = 0;

        if (evdoSnr >= 7) levelEvdoSnr = 4;
        else if (evdoSnr >= 5) levelEvdoSnr = 3;
        else if (evdoSnr >= 3) levelEvdoSnr = 2;
        else if (evdoSnr >= 1) levelEvdoSnr = 1;
        else levelEvdoSnr = 0;

        return (levelEvdoDbm < levelEvdoSnr) ? levelEvdoDbm : levelEvdoSnr;
    }

    private void updateDataState() {
        mDataConnected = hasService() && (mDataState == TelephonyManager.DATA_CONNECTED);

        if (isCdma()) {
            // these functions return a value from 0 to 4, inclusive
            if ((mPhoneState == TelephonyManager.CALL_STATE_IDLE) && isEvdo()){
                mDataLevel = getEvdoLevel() * 25;
            } else {
                mDataLevel = getCdmaLevel() * 25;
            }
        } else {
            // GSM
            
            int asu = (mSignalStrength == null) ? 0 : mSignalStrength.getGsmSignalStrength();

            // asu on [0,31]; 99 = unknown
            // Android has historically shown anything >=12 as "full"
            // XXX: tune this based on Industry Best Practices(TM)
            if (asu <= 2 || asu == 99) mDataLevel = 0;
            else mDataLevel = (int)(((float)Math.max(asu, 15) / 15) * 100);

            mDataRoaming = mPhone.isNetworkRoaming();

            mDataConnected = mDataConnected
                && (mSimState == IccCard.State.READY || mSimState == IccCard.State.UNKNOWN);
        }

        if (DEBUG_SIGNAL || DEBUG) {
            Slog.d(TAG, "updateDataState: connected=" + mDataConnected 
                    + " level=" + mDataLevel
                    + " isEvdo=" + isEvdo()
                    + " isCdma=" + isCdma()
                    + " mPhoneState=" + mPhoneState
                    + " mDataState=" + mDataState
                    );
        }

        refreshSignalMeters();
    }

    private void updateWifiState(Intent intent) {
        if (DEBUG)
            Slog.d(TAG, "updateWifiState: " + intent);

        final String action = intent.getAction();
        final boolean wasConnected = mWifiConnected;

        if (action.equals(WifiManager.WIFI_STATE_CHANGED_ACTION)) {
            mWifiEnabled = intent.getIntExtra(WifiManager.EXTRA_WIFI_STATE,
                    WifiManager.WIFI_STATE_UNKNOWN) == WifiManager.WIFI_STATE_ENABLED;
        } else if (action.equals(WifiManager.NETWORK_STATE_CHANGED_ACTION)) {
            final NetworkInfo networkInfo = (NetworkInfo)
                    intent.getParcelableExtra(WifiManager.EXTRA_NETWORK_INFO);
            mWifiConnected = networkInfo != null && networkInfo.isConnected();
        } else if (action.equals(WifiManager.SUPPLICANT_STATE_CHANGED_ACTION)) {
            final NetworkInfo.DetailedState detailedState = WifiInfo.getDetailedStateOf(
                    (SupplicantState)intent.getParcelableExtra(WifiManager.EXTRA_NEW_STATE));
            mWifiConnected = detailedState == NetworkInfo.DetailedState.CONNECTED;
        } else if (action.equals(WifiManager.RSSI_CHANGED_ACTION)) {
            final int newRssi = intent.getIntExtra(WifiManager.EXTRA_NEW_RSSI, -200);
            int newSignalLevel = WifiManager.calculateSignalLevel(newRssi, 101);
            mWifiLevel = mWifiConnected ? newSignalLevel : 0;
        }

        if (mWifiConnected && !wasConnected) {
            WifiInfo info = mWifiManager.getConnectionInfo();
            if (DEBUG)
                Slog.d(TAG, "updateWifiState: just connected: info=" + info);

            if (info != null) {
                // grab the initial signal strength
                mWifiLevel = WifiManager.calculateSignalLevel(info.getRssi(), 101);

                // find the SSID
                mWifiSsid = info.getSSID();
                if (mWifiSsid == null) {
                    // OK, it's not in the connectionInfo; we have to go hunting for it
                    List<WifiConfiguration> networks = mWifiManager.getConfiguredNetworks();
                    for (WifiConfiguration net : networks) {
                        if (net.networkId == info.getNetworkId()) {
                            mWifiSsid = net.SSID;
                            break;
                        }
                    }
                }
            }
        }

        refreshSignalMeters();
    }

    // figure out what to show: first wifi, then 3G, then nothing
    void refreshSignalMeters() {
        if (mSignalMeter == null) return; // no UI yet

        Context ctxt = getContext();

        String text = null;
        int level = 0;

        if (mWifiConnected) {
            if (mWifiSsid == null) {
                text = ctxt.getString(R.string.system_panel_signal_meter_wifi_nossid);
            } else {
                text = ctxt.getString(R.string.system_panel_signal_meter_wifi_ssid_format,
                                      mWifiSsid);
            }
            level = mWifiLevel;
        } else if (mDataConnected) {
            text = ctxt.getString(R.string.system_panel_signal_meter_data_connected);
            level = mDataLevel;
        } else {
            text = ctxt.getString(R.string.system_panel_signal_meter_disconnected);
            level = 0;
        }

        mSignalMeter.setImageResource(R.drawable.sysbar_signal);
        mSignalMeter.setImageLevel(level);
        mSignalText.setText(text);

        // hack for now
        mBar.setSignalMeter(level, mWifiConnected);
    }

    public void setBar(TabletStatusBarService bar) {
        mBar = bar;
    }

    public void updateBattery(Intent intent) {
        final int level = intent.getIntExtra("level", 0);
        final boolean plugged = intent.getIntExtra("plugged", 0) != 0;

        mBatteryMeter.setImageResource(R.drawable.sysbar_battery);
        mBatteryMeter.setImageLevel(level);
        mBatteryText.setText(getContext()
                .getString(R.string.system_panel_battery_meter_format, level));

        // hack for now
        mBar.setBatteryMeter(level, plugged);
    }
    
    public SystemPanel(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public SystemPanel(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);

        // our mighty overlord
        mWM = IWindowManager.Stub.asInterface(
                    ServiceManager.getService("window"));


        // get notified of phone state changes
        TelephonyManager telephonyManager =
                (TelephonyManager) context.getSystemService(Context.TELEPHONY_SERVICE);
        telephonyManager.listen(mPhoneStateListener,
                          PhoneStateListener.LISTEN_SERVICE_STATE
                        | PhoneStateListener.LISTEN_SIGNAL_STRENGTHS
                        | PhoneStateListener.LISTEN_CALL_STATE
                        | PhoneStateListener.LISTEN_DATA_CONNECTION_STATE
                        | PhoneStateListener.LISTEN_DATA_ACTIVITY);

        // wifi status info
        mWifiManager = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
        
        // audio status 
        mAudioManager = (AudioManager) mContext.getSystemService(Context.AUDIO_SERVICE);

        // mobile data 
        mPhone = (TelephonyManager)context.getSystemService(Context.TELEPHONY_SERVICE);

        // Bluetooth
        mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
    }

    public void onAttachedToWindow() {
        TextView settingsButton = (TextView)findViewById(R.id.settings_button);
        settingsButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                getContext().startActivity(new Intent(android.provider.Settings.ACTION_SETTINGS)
                        .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                mBar.animateCollapse();
            }});

        mBrightnessButton = (ImageButton)findViewById(R.id.brightness);
        mBrightnessButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                rotateBrightness();
            }
        });

        mSoundButton = (ImageButton)findViewById(R.id.sound);
        mSoundButton.setAlpha(getSilentMode() ? 0x7F : 0xFF);
        mSoundButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                setSilentMode(!getSilentMode());
                mSoundButton.setAlpha(getSilentMode() ? 0x7F : 0xFF);
            }
        });
        mOrientationButton = (ImageButton)findViewById(R.id.orientation);
        mOrientationButton.setImageResource(
            getAutoRotate()
                ? R.drawable.ic_sysbar_rotate_on
                : R.drawable.ic_sysbar_rotate_off);
        mOrientationButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                setAutoRotate(!getAutoRotate());
                mOrientationButton.setImageResource(
                    getAutoRotate()
                        ? R.drawable.ic_sysbar_rotate_on
                        : R.drawable.ic_sysbar_rotate_off);
                Toast.makeText(getContext(), 
                    getAutoRotate() 
                        ? R.string.toast_rotation_free
                        : R.string.toast_rotation_locked,
                    Toast.LENGTH_SHORT).show();
            }
        });

        mAirplaneButton = (ImageButton)findViewById(R.id.airplane);
        mAirplaneButton.setAlpha(mAirplaneMode ? 0xFF : 0x7F);
        mAirplaneButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                boolean newMode = !getAirplaneMode();
                Toast.makeText(getContext(), "Attempting to turn "
                    + (newMode ? "on" : "off") + " airplane mode (flaky).",
                    Toast.LENGTH_SHORT).show();
                setAirplaneMode(newMode);
            }
        });

        mGpsButton = (ImageButton)findViewById(R.id.gps);
        mGpsButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                toggleGps();
                refreshGps();
            }
        });

        mBluetoothButton = (ImageButton)findViewById(R.id.bluetooth);
        mBluetoothButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                toggleBluetooth();
                refreshBluetooth();
            }
        });

        // register for broadcasts
        IntentFilter filter = new IntentFilter();
        filter.addAction(AudioManager.RINGER_MODE_CHANGED_ACTION);
        filter.addAction(Intent.ACTION_BATTERY_CHANGED);
        filter.addAction(WifiManager.WIFI_STATE_CHANGED_ACTION);
        filter.addAction(WifiManager.SUPPLICANT_STATE_CHANGED_ACTION);
        filter.addAction(WifiManager.NETWORK_STATE_CHANGED_ACTION);
        filter.addAction(WifiManager.RSSI_CHANGED_ACTION);
        filter.addAction(TelephonyIntents.ACTION_SIM_STATE_CHANGED);
        filter.addAction(BluetoothAdapter.ACTION_STATE_CHANGED);
        getContext().registerReceiver(mReceiver, filter);
        
        mBatteryMeter = (ImageView)findViewById(R.id.battery_meter);
        mBatteryMeter.setImageResource(R.drawable.sysbar_battery);
        mBatteryMeter.setImageLevel(0);
        mSignalMeter = (ImageView)findViewById(R.id.signal_meter);
        mBatteryMeter.setImageResource(R.drawable.sysbar_signal);
        mBatteryMeter.setImageLevel(0);

        mBatteryText = (TextView)findViewById(R.id.battery_info);
        mSignalText = (TextView)findViewById(R.id.signal_info);

        refreshSignalMeters();
        refreshBluetooth();
        refreshGps();
    }

    public void onDetachedFromWindow() {
        getContext().unregisterReceiver(mReceiver);
    }

    // ----------------------------------------------------------------------

//    private boolean isAutoBrightness() {
//        Context context = getContext();
//        try {
//            IPowerManager power = IPowerManager.Stub.asInterface(
//                    ServiceManager.getService("power"));
//            if (power != null) {
//                int brightnessMode = Settings.System.getInt(context.getContentResolver(),
//                        Settings.System.SCREEN_BRIGHTNESS_MODE);
//                return brightnessMode == Settings.System.SCREEN_BRIGHTNESS_MODE_AUTOMATIC;
//            }
//        } catch (RemoteException e) {
//        } catch (Settings.SettingNotFoundException e) {
//        }
//        return false;
//    }

    private void rotateBrightness() {
        int icon = R.drawable.ic_sysbar_brightness;
        int bg = R.drawable.sysbar_toggle_bg_on;
        Context context = getContext();
        try {
            IPowerManager power = IPowerManager.Stub.asInterface(
                    ServiceManager.getService("power"));
            if (power != null) {
                ContentResolver cr = context.getContentResolver();
                int brightness = Settings.System.getInt(cr,
                        Settings.System.SCREEN_BRIGHTNESS);
                int brightnessMode = Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL;
                //Only get brightness setting if available
                if (context.getResources().getBoolean(
                        com.android.internal.R.bool.config_automatic_brightness_available)) {
                    brightnessMode = Settings.System.getInt(cr,
                            Settings.System.SCREEN_BRIGHTNESS_MODE);
                }

                // Rotate AUTO -> MINIMUM -> DEFAULT -> MAXIMUM
                // Technically, not a toggle...
                if (brightnessMode == Settings.System.SCREEN_BRIGHTNESS_MODE_AUTOMATIC) {
                    brightness = MINIMUM_BACKLIGHT;
                    icon = R.drawable.ic_sysbar_brightness_low;
                    brightnessMode = Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL;
                } else if (brightness < DEFAULT_BACKLIGHT) {
                    brightness = DEFAULT_BACKLIGHT;
                } else if (brightness < MAXIMUM_BACKLIGHT) {
                    brightness = MAXIMUM_BACKLIGHT;
                } else {
                    brightnessMode = Settings.System.SCREEN_BRIGHTNESS_MODE_AUTOMATIC;
                    brightness = MINIMUM_BACKLIGHT;
                    icon = R.drawable.ic_sysbar_brightness_auto;
                }

                if (context.getResources().getBoolean(
                        com.android.internal.R.bool.config_automatic_brightness_available)) {
                    // Set screen brightness mode (automatic or manual)
                    Settings.System.putInt(context.getContentResolver(),
                            Settings.System.SCREEN_BRIGHTNESS_MODE,
                            brightnessMode);
                } else {
                    // Make sure we set the brightness if automatic mode isn't available
                    brightnessMode = Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL;
                }
                if (brightnessMode == Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL) {
                    power.setBacklightBrightness(brightness);
                    Settings.System.putInt(cr, Settings.System.SCREEN_BRIGHTNESS, brightness);
                }
            }
        } catch (RemoteException e) {
        } catch (Settings.SettingNotFoundException e) {
        }

        mBrightnessButton.setImageResource(icon);
        mBrightnessButton.setBackgroundResource(bg);
    }

    PhoneStateListener mPhoneStateListener = new PhoneStateListener() {
        @Override
        public void onServiceStateChanged(ServiceState serviceState) {
            if (DEBUG_SIGNAL || DEBUG) {
                Slog.d(TAG, "phone service state changed: " + serviceState.getState());
            }
            mServiceState = serviceState;
            mAirplaneMode = serviceState.getState() == ServiceState.STATE_POWER_OFF;
            if (mAirplaneButton != null) {
                mAirplaneButton.setImageResource(mAirplaneMode 
                                                 ? R.drawable.ic_sysbar_airplane_on
                                                 : R.drawable.ic_sysbar_airplane_off);
                mAirplaneButton.setBackgroundResource(mAirplaneMode 
                                                 ? R.drawable.sysbar_toggle_bg_on
                                                 : R.drawable.sysbar_toggle_bg_off);
            }
            updateDataState();
        }
        @Override
        public void onSignalStrengthsChanged(SignalStrength signalStrength) {
            if (DEBUG_SIGNAL || DEBUG) {
                Slog.d(TAG, "onSignalStrengthsChanged: " + signalStrength);
            }
            mSignalStrength = signalStrength;
            updateDataState();
        }
        @Override
        public void onCallStateChanged(int state, String incomingNumber) {
            mPhoneState = state;
            // In cdma, if a voice call is made, RSSI should switch to 1x.
            if (isCdma()) {
                updateDataState();
            }
        }

        @Override
        public void onDataConnectionStateChanged(int state, int networkType) {
            if (DEBUG_SIGNAL || DEBUG) {
                Slog.d(TAG, "onDataConnectionStateChanged: state=" + state 
                        + " type=" + networkType);
            }
            mDataState = state;
//            updateDataNetType(networkType);
            updateDataState();
        }
    };

    private boolean getAirplaneMode() {
        return mAirplaneMode;
    }

    private void setAirplaneMode(boolean on) {
        Settings.System.putInt(
                mContext.getContentResolver(),
                Settings.System.AIRPLANE_MODE_ON,
                on ? 1 : 0);
        Intent intent = new Intent(Intent.ACTION_AIRPLANE_MODE_CHANGED);
        intent.addFlags(Intent.FLAG_RECEIVER_REPLACE_PENDING);
        intent.putExtra("state", on);
        getContext().sendBroadcast(intent);
    }

    boolean getSilentMode() {
        return mAudioManager.getRingerMode() != AudioManager.RINGER_MODE_NORMAL;
    }

    void setSilentMode(boolean on) {
        if (on) {
            mAudioManager.setRingerMode((Settings.System.getInt(mContext.getContentResolver(),
                Settings.System.VIBRATE_IN_SILENT, 1) == 1)
                ? AudioManager.RINGER_MODE_VIBRATE
                : AudioManager.RINGER_MODE_SILENT);
        } else {
            mAudioManager.setRingerMode(AudioManager.RINGER_MODE_NORMAL);
        }
    }

    void refreshSound() {
        boolean silent = getSilentMode();
        mSoundButton.setImageResource(!silent 
                                         ? R.drawable.ic_sysbar_sound_on
                                         : R.drawable.ic_sysbar_sound_off);
        mSoundButton.setBackgroundResource(!silent 
                                         ? R.drawable.sysbar_toggle_bg_on
                                         : R.drawable.sysbar_toggle_bg_off);
    }

    void toggleBluetooth() {
        if (mBluetoothAdapter == null) return;
        if (mBluetoothAdapter.isEnabled()) {
            mBluetoothAdapter.disable();
        } else {
            mBluetoothAdapter.enable();
        }
    }

    void refreshBluetooth() {
        boolean on = mBluetoothAdapter != null && mBluetoothAdapter.isEnabled();
        mBluetoothButton.setImageResource(on ? R.drawable.ic_sysbar_bluetooth_on
                                             : R.drawable.ic_sysbar_bluetooth_off);
        mBluetoothButton.setBackgroundResource(on
                                         ? R.drawable.sysbar_toggle_bg_on
                                         : R.drawable.sysbar_toggle_bg_off);
    }

    private boolean isGpsEnabled() {
        ContentResolver res = mContext.getContentResolver();
        return Settings.Secure.isLocationProviderEnabled(
                                res, LocationManager.GPS_PROVIDER);
    }

    private void toggleGps() {
        Settings.Secure.setLocationProviderEnabled(mContext.getContentResolver(),
                LocationManager.GPS_PROVIDER, !isGpsEnabled());
    }

    private void refreshGps() {
        boolean on = isGpsEnabled();
        mGpsButton.setImageResource(on ? R.drawable.ic_sysbar_gps_on
                                       : R.drawable.ic_sysbar_gps_off);
        mGpsButton.setBackgroundResource(on
                                         ? R.drawable.sysbar_toggle_bg_on
                                         : R.drawable.sysbar_toggle_bg_off);
    }

    void setAutoRotate(boolean rot) {
        try {
            ContentResolver cr = getContext().getContentResolver();
            if (rot) {
                mWM.thawRotation();
            } else {
                mWM.freezeRotation();
            }
        } catch (android.os.RemoteException exc) {
        }
    }

    boolean getAutoRotate() {
        ContentResolver cr = getContext().getContentResolver();
        return 1 == Settings.System.getInt(cr,
                Settings.System.ACCELEROMETER_ROTATION,
                1);
    }

    int getDisplayRotation() {
        try {
            return mWM.getRotation();
        } catch (android.os.RemoteException exc) {
            return 0;
        }
    }
}

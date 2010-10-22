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

import java.io.FileDescriptor;
import java.io.PrintWriter;

import android.app.ActivityManagerNative;
import android.app.PendingIntent;
import android.app.Notification;
import android.app.StatusBarManager;
import android.content.Context;
import android.content.Intent;
import android.content.res.Resources;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.RemoteException;
import android.text.TextUtils;
import android.util.Slog;
import android.view.animation.Animation;
import android.view.animation.AnimationUtils;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.WindowManagerImpl;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RemoteViews;
import android.widget.ScrollView;
import android.widget.TextSwitcher;
import android.widget.TextView;

import com.android.internal.statusbar.StatusBarIcon;
import com.android.internal.statusbar.StatusBarNotification;

import com.android.systemui.statusbar.*;
import com.android.systemui.recent.RecentApplicationsActivity;
import com.android.systemui.R;

public class TabletStatusBarService extends StatusBarService {
    public static final boolean DEBUG = false;
    public static final String TAG = "TabletStatusBarService";

    public static final int MSG_OPEN_NOTIFICATION_PANEL = 1000;
    public static final int MSG_CLOSE_NOTIFICATION_PANEL = 1001;
    public static final int MSG_OPEN_SYSTEM_PANEL = 1010;
    public static final int MSG_CLOSE_SYSTEM_PANEL = 1011;
    
    private static final int MAX_IMAGE_LEVEL = 10000;

    int mIconSize;

    H mHandler = new H();

    // tracking all current notifications
    private NotificationData mNotns = new NotificationData();
    
    TabletStatusBarView mStatusBarView;
    ImageView mNotificationTrigger;
    NotificationIconArea mNotificationIconArea;
    View mNotificationButtons;
    View mSystemInfo;
    View mNavigationArea;
    View mMenuButton;
    View mRecentButton;

    NotificationPanel mNotificationPanel;
    SystemPanel mSystemPanel;

    ViewGroup mPile;
    TextView mClearButton;
    TextView mDoNotDisturbButton;

    ImageView mBatteryMeter;
    ImageView mSignalMeter;
    ImageView mSignalIcon;

    View mBarContents;
    View mCurtains;

    NotificationIconArea.IconLayout mIconLayout;

    TabletTicker mTicker;
    View mTickerView;
    boolean mTicking;

    // for disabling the status bar
    int mDisabled = 0;

    boolean mNotificationsOn = true;

    protected void addPanelWindows() {
        final Context context = mContext;

        final Resources res = context.getResources();
        final int barHeight= res.getDimensionPixelSize(
            com.android.internal.R.dimen.status_bar_height);

        mNotificationPanel = (NotificationPanel)View.inflate(context,
                R.layout.sysbar_panel_notifications, null);
        mNotificationPanel.setVisibility(View.GONE);
        mNotificationPanel.setOnTouchListener(
                new TouchOutsideListener(MSG_CLOSE_NOTIFICATION_PANEL, mNotificationPanel));

        mStatusBarView.setIgnoreChildren(0, mNotificationTrigger, mNotificationPanel);

        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                400, // ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_STATUS_BAR_PANEL,
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                    | WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM,
                PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.BOTTOM | Gravity.RIGHT;
        lp.setTitle("NotificationPanel");
        lp.windowAnimations = com.android.internal.R.style.Animation_SlidingCard;

        WindowManagerImpl.getDefault().addView(mNotificationPanel, lp);

        mSystemPanel = (SystemPanel) View.inflate(context, R.layout.sysbar_panel_system, null);
        mSystemPanel.setVisibility(View.GONE);
        mSystemPanel.setOnTouchListener(new TouchOutsideListener(MSG_CLOSE_SYSTEM_PANEL,
                    mSystemPanel));

        mStatusBarView.setIgnoreChildren(1, mSystemInfo, mSystemPanel);

        lp = new WindowManager.LayoutParams(
                800,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_STATUS_BAR_PANEL,
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                    | WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM,
                PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
        lp.setTitle("SystemPanel");
        lp.windowAnimations = com.android.internal.R.style.Animation_SlidingCard;

        WindowManagerImpl.getDefault().addView(mSystemPanel, lp);
        mSystemPanel.setBar(this);
    }

    @Override
    public void start() {
        super.start(); // will add the main bar view
    }

    protected View makeStatusBarView() {
        final Context context = mContext;
        final Resources res = context.getResources();

        mIconSize = res.getDimensionPixelSize(com.android.internal.R.dimen.status_bar_icon_size);

        final TabletStatusBarView sb = (TabletStatusBarView)View.inflate(
                context, R.layout.status_bar, null);
        mStatusBarView = sb;

        sb.setHandler(mHandler);

        mBarContents = sb.findViewById(R.id.bar_contents);
        mCurtains = sb.findViewById(R.id.lights_out);
        mSystemInfo = sb.findViewById(R.id.systemInfo);

        mSystemInfo.setOnClickListener(mOnClickListener);
        mSystemInfo.setOnLongClickListener(new SetLightsOnListener(false));

        mRecentButton = sb.findViewById(R.id.recent);
        mRecentButton.setOnClickListener(mOnClickListener);

        SetLightsOnListener on = new SetLightsOnListener(true);
        mCurtains.setOnClickListener(on);
        mCurtains.setOnLongClickListener(on);

        // the button to open the notification area
        mNotificationTrigger = (ImageView) sb.findViewById(R.id.notificationTrigger);
        mNotificationTrigger.setOnClickListener(mOnClickListener);

        // the more notifications icon
        mNotificationIconArea = (NotificationIconArea)sb.findViewById(R.id.notificationIcons);

        // the clear and dnd buttons
        mNotificationButtons = sb.findViewById(R.id.notificationButtons);
        mClearButton = (TextView)mNotificationButtons.findViewById(R.id.clear_all_button);
        mClearButton.setOnClickListener(mOnClickListener);
        mDoNotDisturbButton = (TextView)mNotificationButtons.findViewById(R.id.do_not_disturb);
        mDoNotDisturbButton.setOnClickListener(mOnClickListener);


        // where the icons go
        mIconLayout = (NotificationIconArea.IconLayout) sb.findViewById(R.id.icons);

        mTicker = new TabletTicker(context, (FrameLayout)sb.findViewById(R.id.ticker));

        // System info (center)
        mBatteryMeter = (ImageView) sb.findViewById(R.id.battery);
        mSignalMeter = (ImageView) sb.findViewById(R.id.signal);
        mSignalIcon = (ImageView) sb.findViewById(R.id.signal_icon);

        // The navigation buttons
        mNavigationArea = sb.findViewById(R.id.navigationArea);
        mMenuButton = mNavigationArea.findViewById(R.id.menu);

        // set the initial view visibility
        setAreThereNotifications();
        refreshNotificationTrigger();

        // Add the windows
        addPanelWindows();

        mPile = (ViewGroup)mNotificationPanel.findViewById(R.id.content);
        mPile.removeAllViews();
        
        ScrollView scroller = (ScrollView)mPile.getParent();
        scroller.setFillViewport(true);

        return sb;
    }

    protected int getStatusBarGravity() {
        return Gravity.BOTTOM | Gravity.FILL_HORIZONTAL;
    }

    private class H extends Handler {
        public void handleMessage(Message m) {
            switch (m.what) {
                case MSG_OPEN_NOTIFICATION_PANEL:
                    if (DEBUG) Slog.d(TAG, "opening notifications panel");
                    if (mNotificationPanel.getVisibility() == View.GONE) {
                        mDoNotDisturbButton.setText(mNotificationsOn
                                ? R.string.status_bar_do_not_disturb_button
                                : R.string.status_bar_please_disturb_button);
                        mNotificationPanel.setVisibility(View.VISIBLE);
                        setViewVisibility(mNotificationIconArea, View.GONE,
                                R.anim.notification_icons_out);
                        setViewVisibility(mNotificationButtons, View.VISIBLE,
                                R.anim.notification_buttons_in);
                        refreshNotificationTrigger();
                    }
                    break;
                case MSG_CLOSE_NOTIFICATION_PANEL:
                    if (DEBUG) Slog.d(TAG, "closing notifications panel");
                    if (mNotificationPanel.getVisibility() == View.VISIBLE) {
                        mNotificationPanel.setVisibility(View.GONE);
                        setViewVisibility(mNotificationIconArea, View.VISIBLE,
                                R.anim.notification_icons_in);
                        setViewVisibility(mNotificationButtons, View.GONE,
                                R.anim.notification_buttons_out);
                        refreshNotificationTrigger();
                    }
                    break;
                case MSG_OPEN_SYSTEM_PANEL:
                    if (DEBUG) Slog.d(TAG, "opening system panel");
                    mSystemPanel.setVisibility(View.VISIBLE);
                    break;
                case MSG_CLOSE_SYSTEM_PANEL:
                    if (DEBUG) Slog.d(TAG, "closing system panel");
                    mSystemPanel.setVisibility(View.GONE);
                    break;
            }
        }
    }

    public void refreshNotificationTrigger() {
        int resId;
        boolean panel = (mNotificationPanel != null 
                && mNotificationPanel.getVisibility() == View.VISIBLE);
        if (!mNotificationsOn) {
            resId = R.drawable.ic_sysbar_noti_dnd;
        } else if (mNotns.size() > 0) {
            resId = panel ? R.drawable.ic_sysbar_noti_avail_open : R.drawable.ic_sysbar_noti_avail;
        } else {
            resId = panel ? R.drawable.ic_sysbar_noti_none_open : R.drawable.ic_sysbar_noti_none;
        }
        mNotificationTrigger.setImageResource(resId);
    }
    
    public void setBatteryMeter(int level, boolean plugged) {
        if (DEBUG) Slog.d(TAG, "battery=" + level + (plugged ? " - plugged" : " - unplugged"));
        mBatteryMeter.setImageResource(R.drawable.sysbar_batterymini);
        // adjust percent to permyriad for ClipDrawable's sake
        mBatteryMeter.setImageLevel(level * (MAX_IMAGE_LEVEL / 100));
    }

    public void setSignalMeter(int level, boolean isWifi) {
        if (DEBUG) Slog.d(TAG, "signal=" + level);
        if (level < 0) {
            mSignalMeter.setImageDrawable(null);
            mSignalMeter.setImageLevel(0);
            mSignalIcon.setImageDrawable(null);
        } else {
            mSignalMeter.setImageResource(R.drawable.sysbar_wifimini);
            // adjust to permyriad
            mSignalMeter.setImageLevel(level * (MAX_IMAGE_LEVEL / 100));
            mSignalIcon.setImageResource(isWifi ? R.drawable.ic_sysbar_wifi_mini 
                                                : R.drawable.ic_sysbar_wifi_mini); // XXX
        }
    }

    public void addIcon(String slot, int index, int viewIndex, StatusBarIcon icon) {
        if (DEBUG) Slog.d(TAG, "addIcon(" + slot + ") -> " + icon);
    }

    public void updateIcon(String slot, int index, int viewIndex,
            StatusBarIcon old, StatusBarIcon icon) {
        if (DEBUG) Slog.d(TAG, "updateIcon(" + slot + ") -> " + icon);
    }

    public void removeIcon(String slot, int index, int viewIndex) {
        if (DEBUG) Slog.d(TAG, "removeIcon(" + slot + ")");
    }

    public void addNotification(IBinder key, StatusBarNotification notification) {
        if (DEBUG) Slog.d(TAG, "addNotification(" + key + " -> " + notification + ")");
        addNotificationViews(key, notification);

        boolean immersive = false;
        try {
            immersive = ActivityManagerNative.getDefault().isTopActivityImmersive();
            Slog.d(TAG, "Top activity is " + (immersive?"immersive":"not immersive"));
        } catch (RemoteException ex) {
        }
        if (immersive) {
            // TODO: immersive mode popups for tablet
        } else if (notification.notification.fullScreenIntent != null) {
            // not immersive & a full-screen alert should be shown
            Slog.d(TAG, "Notification has fullScreenIntent and activity is not immersive;"
                    + " sending fullScreenIntent");
            try {
                notification.notification.fullScreenIntent.send();
            } catch (PendingIntent.CanceledException e) {
            }
        } else {
            tick(notification);
        }

        setAreThereNotifications();
    }

    public void updateNotification(IBinder key, StatusBarNotification notification) {
        if (DEBUG) Slog.d(TAG, "updateNotification(" + key + " -> " + notification + ") // TODO");
        
        final NotificationData.Entry oldEntry = mNotns.findByKey(key);
        if (oldEntry == null) {
            Slog.w(TAG, "updateNotification for unknown key: " + key);
            return;
        }

        final StatusBarNotification oldNotification = oldEntry.notification;
        final RemoteViews oldContentView = oldNotification.notification.contentView;

        final RemoteViews contentView = notification.notification.contentView;

        if (false) {
            Slog.d(TAG, "old notification: when=" + oldNotification.notification.when
                    + " ongoing=" + oldNotification.isOngoing()
                    + " expanded=" + oldEntry.expanded
                    + " contentView=" + oldContentView);
            Slog.d(TAG, "new notification: when=" + notification.notification.when
                    + " ongoing=" + oldNotification.isOngoing()
                    + " contentView=" + contentView);
        }

        // Can we just reapply the RemoteViews in place?  If when didn't change, the order
        // didn't change.
        if (notification.notification.when == oldNotification.notification.when
                && notification.isOngoing() == oldNotification.isOngoing()
                && oldEntry.expanded != null
                && contentView != null
                && oldContentView != null
                && contentView.getPackage() != null
                && oldContentView.getPackage() != null
                && oldContentView.getPackage().equals(contentView.getPackage())
                && oldContentView.getLayoutId() == contentView.getLayoutId()) {
            if (DEBUG) Slog.d(TAG, "reusing notification for key: " + key);
            oldEntry.notification = notification;
            try {
                // Reapply the RemoteViews
                contentView.reapply(mContext, oldEntry.content);
                // update the contentIntent
                final PendingIntent contentIntent = notification.notification.contentIntent;
                if (contentIntent != null) {
                    oldEntry.content.setOnClickListener(new NotificationClicker(contentIntent,
                                notification.pkg, notification.tag, notification.id));
                } else {
                    oldEntry.content.setOnClickListener(null);
                }
                // Update the icon.
                final StatusBarIcon ic = new StatusBarIcon(notification.pkg,
                        notification.notification.icon, notification.notification.iconLevel,
                        notification.notification.number);
                if (!oldEntry.icon.set(ic)) {
                    handleNotificationError(key, notification, "Couldn't update icon: " + ic);
                    return;
                }
            }
            catch (RuntimeException e) {
                // It failed to add cleanly.  Log, and remove the view from the panel.
                Slog.w(TAG, "Couldn't reapply views for package " + contentView.getPackage(), e);
                removeNotificationViews(key);
                addNotificationViews(key, notification);
            }
        } else {
            if (DEBUG) Slog.d(TAG, "not reusing notification for key: " + key);
            removeNotificationViews(key);
            addNotificationViews(key, notification);
        }
        // TODO: ticker; immersive mode

        setAreThereNotifications();
    }

    public void removeNotification(IBinder key) {
        if (DEBUG) Slog.d(TAG, "removeNotification(" + key + ") // TODO");
        removeNotificationViews(key);
        setAreThereNotifications();
    }

    public void disable(int state) {
        int old = mDisabled;
        int diff = state ^ old;
        Slog.d(TAG, "disable... old=0x" + Integer.toHexString(old)
                + " diff=0x" + Integer.toHexString(diff)
                + " state=0x" + Integer.toHexString(state));
        mDisabled = state;

        // act accordingly
        if ((diff & StatusBarManager.DISABLE_EXPAND) != 0) {
            if ((state & StatusBarManager.DISABLE_EXPAND) != 0) {
                Slog.d(TAG, "DISABLE_EXPAND: yes");
                animateCollapse();
            }
        }
        if ((diff & StatusBarManager.DISABLE_NOTIFICATION_ICONS) != 0) {
            if ((state & StatusBarManager.DISABLE_NOTIFICATION_ICONS) != 0) {
                Slog.d(TAG, "DISABLE_NOTIFICATION_ICONS: yes");
                setViewVisibility(mNotificationTrigger, View.GONE,
                        R.anim.notification_icons_out);
                setViewVisibility(mNotificationIconArea, View.GONE,
                        R.anim.notification_icons_out);
                mTicker.halt();
            } else {
                Slog.d(TAG, "DISABLE_NOTIFICATION_ICONS: no");
                setViewVisibility(mNotificationTrigger, View.VISIBLE,
                        R.anim.notification_icons_in);
                setViewVisibility(mNotificationIconArea, View.VISIBLE,
                        R.anim.notification_icons_in);
            }
        } else if ((diff & StatusBarManager.DISABLE_NOTIFICATION_TICKER) != 0) {
            if ((state & StatusBarManager.DISABLE_NOTIFICATION_TICKER) != 0) {
                mTicker.halt();
            }
        }
        if ((diff & StatusBarManager.DISABLE_SYSTEM_INFO) != 0) {
            if ((state & StatusBarManager.DISABLE_SYSTEM_INFO) != 0) {
                Slog.d(TAG, "DISABLE_SYSTEM_INFO: yes");
                setViewVisibility(mSystemInfo, View.GONE, R.anim.navigation_out);
            } else {
                Slog.d(TAG, "DISABLE_SYSTEM_INFO: no");
                setViewVisibility(mSystemInfo, View.VISIBLE, R.anim.navigation_in);
            }
        }
        if ((diff & StatusBarManager.DISABLE_NAVIGATION) != 0) {
            if ((state & StatusBarManager.DISABLE_NAVIGATION) != 0) {
                Slog.d(TAG, "DISABLE_NAVIGATION: yes");
                setViewVisibility(mNavigationArea, View.GONE, R.anim.navigation_out);
            } else {
                Slog.d(TAG, "DISABLE_NAVIGATION: no");
                setViewVisibility(mNavigationArea, View.VISIBLE, R.anim.navigation_in);
            }
        }
    }

    private boolean hasTicker(Notification n) {
        return !TextUtils.isEmpty(n.tickerText)
                || !TextUtils.isEmpty(n.tickerTitle)
                || !TextUtils.isEmpty(n.tickerSubtitle);
    }

    private void tick(StatusBarNotification n) {
        // Don't show the ticker when the windowshade is open.
        if (mNotificationPanel.getVisibility() == View.VISIBLE) {
            return;
        }
        // Show the ticker if one is requested. Also don't do this
        // until status bar window is attached to the window manager,
        // because...  well, what's the point otherwise?  And trying to
        // run a ticker without being attached will crash!
        if (hasTicker(n.notification) && mStatusBarView.getWindowToken() != null) {
            if (0 == (mDisabled & (StatusBarManager.DISABLE_NOTIFICATION_ICONS
                            | StatusBarManager.DISABLE_NOTIFICATION_TICKER))) {
                mTicker.add(n);
            }
        }
    }

    public void animateExpand() {
        mHandler.removeMessages(MSG_OPEN_NOTIFICATION_PANEL);
        mHandler.sendEmptyMessage(MSG_OPEN_NOTIFICATION_PANEL);
    }

    public void animateCollapse() {
        mHandler.removeMessages(MSG_CLOSE_NOTIFICATION_PANEL);
        mHandler.sendEmptyMessage(MSG_CLOSE_NOTIFICATION_PANEL);
        mHandler.removeMessages(MSG_CLOSE_SYSTEM_PANEL);
        mHandler.sendEmptyMessage(MSG_CLOSE_SYSTEM_PANEL);
    }

    public void setLightsOn(boolean on) {
        if (on) {
            setViewVisibility(mCurtains, View.GONE, R.anim.lights_out_out);
            setViewVisibility(mBarContents, View.VISIBLE, R.anim.status_bar_in);
        } else {
            animateCollapse();
            setViewVisibility(mCurtains, View.VISIBLE, R.anim.lights_out_in);
            setViewVisibility(mBarContents, View.GONE, R.anim.status_bar_out);
        }
    }

    public void setMenuKeyVisible(boolean visible) {
        if (DEBUG) {
            Slog.d(TAG, (visible?"showing":"hiding") + " the MENU button");
        }
        setViewVisibility(mMenuButton,
                visible ? View.VISIBLE : View.INVISIBLE,
                visible ? R.anim.navigation_in : R.anim.navigation_out);
    }

    private void setAreThereNotifications() {
        final boolean hasClearable = mNotns.hasClearableItems();

        //Slog.d(TAG, "setAreThereNotifications hasClerable=" + hasClearable);

        // Show or hide the "Clear all" button.  Note that we don't do an animation
        // if it's not on screen, so that if someone opens the bar right then they
        // don't see the animation in progress.
        // (no ongoing notifications are clearable)
        if (hasClearable) {
            if (mNotificationButtons.getVisibility() == View.VISIBLE) {
                setViewVisibility(mClearButton, View.VISIBLE, R.anim.notification_buttons_in);
            } else {
                mClearButton.setVisibility(View.VISIBLE);
            }
        } else {
            if (mNotificationButtons.getVisibility() == View.VISIBLE) {
                setViewVisibility(mClearButton, View.GONE, R.anim.notification_buttons_out);
            } else {
                mClearButton.setVisibility(View.GONE);
            }
        }

        /*
        mOngoingTitle.setVisibility(ongoing ? View.VISIBLE : View.GONE);
        mLatestTitle.setVisibility(latest ? View.VISIBLE : View.GONE);

        if (ongoing || latest) {
            mNoNotificationsTitle.setVisibility(View.GONE);
        } else {
            mNoNotificationsTitle.setVisibility(View.VISIBLE);
        }
        */
    }

    /**
     * Cancel this notification and tell the status bar service about the failure. Hold no locks.
     */
    void handleNotificationError(IBinder key, StatusBarNotification n, String message) {
        removeNotification(key);
        try {
            mBarService.onNotificationError(n.pkg, n.tag, n.id, n.uid, n.initialPid, message);
        } catch (RemoteException ex) {
            // The end is nigh.
        }
    }

    private View.OnClickListener mOnClickListener = new View.OnClickListener() {
        public void onClick(View v) {
            if (v == mClearButton) {
                onClickClearButton();
            } else if (v == mDoNotDisturbButton) {
                onClickDoNotDisturb();
            } else if (v == mNotificationTrigger) {
                onClickNotificationTrigger();
            } else if (v == mSystemInfo) {
                onClickSystemInfo();
            } else if (v == mRecentButton) {
                onClickRecentButton();
            }
        }
    };

    void onClickClearButton() {
        try {
            mBarService.onClearAllNotifications();
        } catch (RemoteException ex) {
            // system process is dead if we're here.
        }
        animateCollapse();
        refreshNotificationTrigger();
    }

    void onClickDoNotDisturb() {
        mNotificationsOn = !mNotificationsOn;
        setViewVisibility(mIconLayout,
                mNotificationsOn ? View.VISIBLE : View.INVISIBLE,
                mNotificationsOn ? R.anim.notification_dnd_off : R.anim.notification_dnd_on);
        animateCollapse();
        refreshNotificationTrigger();
    }

    public void onClickNotificationTrigger() {
        if (DEBUG) Slog.d(TAG, "clicked notification icons");
        if ((mDisabled & StatusBarManager.DISABLE_EXPAND) == 0) {
            if (!mNotificationsOn) {
                mNotificationsOn = true;
                setViewVisibility(mIconLayout,
                        View.VISIBLE,
                        R.anim.notification_dnd_off);
                refreshNotificationTrigger();
            } else {
                int msg = (mNotificationPanel.getVisibility() == View.GONE) 
                    ? MSG_OPEN_NOTIFICATION_PANEL
                    : MSG_CLOSE_NOTIFICATION_PANEL;
                mHandler.removeMessages(msg);
                mHandler.sendEmptyMessage(msg);
            }
        }
    }

    public void onClickSystemInfo() {
        if (DEBUG) Slog.d(TAG, "clicked system info");
        if ((mDisabled & StatusBarManager.DISABLE_EXPAND) == 0) {
            int msg = (mSystemPanel.getVisibility() == View.GONE) 
                ? MSG_OPEN_SYSTEM_PANEL
                : MSG_CLOSE_SYSTEM_PANEL;
            mHandler.removeMessages(msg);
            mHandler.sendEmptyMessage(msg);
        }
    }

    public void onClickRecentButton() {
        if (DEBUG) Slog.d(TAG, "clicked recent apps");
        Intent intent = new Intent();
        intent.setClass(mContext, RecentApplicationsActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                | Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS);
        mContext.startActivity(intent);
    }

    private class NotificationClicker implements View.OnClickListener {
        private PendingIntent mIntent;
        private String mPkg;
        private String mTag;
        private int mId;

        NotificationClicker(PendingIntent intent, String pkg, String tag, int id) {
            mIntent = intent;
            mPkg = pkg;
            mTag = tag;
            mId = id;
        }

        public void onClick(View v) {
            try {
                // The intent we are sending is for the application, which
                // won't have permission to immediately start an activity after
                // the user switches to home.  We know it is safe to do at this
                // point, so make sure new activity switches are now allowed.
                ActivityManagerNative.getDefault().resumeAppSwitches();
            } catch (RemoteException e) {
            }

            if (mIntent != null) {
                int[] pos = new int[2];
                v.getLocationOnScreen(pos);
                Intent overlay = new Intent();
                overlay.setSourceBounds(
                        new Rect(pos[0], pos[1], pos[0]+v.getWidth(), pos[1]+v.getHeight()));
                try {
                    mIntent.send(mContext, 0, overlay);
                } catch (PendingIntent.CanceledException e) {
                    // the stack trace isn't very helpful here.  Just log the exception message.
                    Slog.w(TAG, "Sending contentIntent failed: " + e);
                }
            }

            try {
                mBarService.onNotificationClick(mPkg, mTag, mId);
            } catch (RemoteException ex) {
                // system process is dead if we're here.
            }

            // close the shade if it was open
            animateCollapse();

            // If this click was on the intruder alert, hide that instead
//            mHandler.sendEmptyMessage(MSG_HIDE_INTRUDER);
        }
    }

    StatusBarNotification removeNotificationViews(IBinder key) {
        NotificationData.Entry entry = mNotns.remove(key);
        if (entry == null) {
            Slog.w(TAG, "removeNotification for unknown key: " + key);
            return null;
        }
        // Remove the expanded view.
        ViewGroup rowParent = (ViewGroup)entry.row.getParent();
        if (rowParent != null) rowParent.removeView(entry.row);
        // Remove the icon.
//        ViewGroup iconParent = (ViewGroup)entry.icon.getParent();
//        if (iconParent != null) iconParent.removeView(entry.icon);
        refreshIcons();

        return entry.notification;
    }

    StatusBarIconView addNotificationViews(IBinder key, StatusBarNotification notification) {
        if (DEBUG) {
            Slog.d(TAG, "addNotificationViews(key=" + key + ", notification=" + notification);
        }
        // Construct the icon.
        final StatusBarIconView iconView = new StatusBarIconView(mContext,
                notification.pkg + "/0x" + Integer.toHexString(notification.id));
        iconView.setScaleType(ImageView.ScaleType.CENTER_INSIDE);

        final StatusBarIcon ic = new StatusBarIcon(notification.pkg,
                    notification.notification.icon,
                    notification.notification.iconLevel,
                    notification.notification.number);
        if (!iconView.set(ic)) {
            handleNotificationError(key, notification, "Couldn't attach StatusBarIcon: " + ic);
            return null;
        }
        // Construct the expanded view.
        NotificationData.Entry entry = new NotificationData.Entry(key, notification, iconView);
        if (!inflateViews(entry, mPile)) {
            handleNotificationError(key, notification, "Couldn't expand RemoteViews for: "
                    + notification);
            return null;
        }
        // Add the icon.
        mNotns.add(entry);
        refreshIcons();

        return iconView;
    }

    private void refreshIcons() {
        // XXX: need to implement a new limited linear layout class
        // to avoid removing & readding everything

        int N = mNotns.size();
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(mIconSize, mIconSize);

        if (DEBUG) {
            Slog.d(TAG, "refreshing icons (" + N + " notifications, mIconLayout="
                    + mIconLayout + ", mPile=" + mPile);
        }

        mIconLayout.removeAllViews();
        for (int i=0; i<4; i++) {
            if (i>=N) break;
            mIconLayout.addView(mNotns.get(N-i-1).icon, i, params);
        }

        mPile.removeAllViews();
        for (int i=0; i<N; i++) {
            mPile.addView(mNotns.get(N-i-1).row);
        }

        refreshNotificationTrigger();
    }

    private boolean inflateViews(NotificationData.Entry entry, ViewGroup parent) {
        StatusBarNotification sbn = entry.notification;
        RemoteViews remoteViews = sbn.notification.contentView;
        if (remoteViews == null) {
            return false;
        }

        // create the row view
        LayoutInflater inflater = (LayoutInflater)mContext.getSystemService(
                Context.LAYOUT_INFLATER_SERVICE);
        View row = inflater.inflate(R.layout.status_bar_latest_event, parent, false);
        View vetoButton = row.findViewById(R.id.veto);
        if (entry.notification.isClearable()) {
            final String _pkg = sbn.pkg;
            final String _tag = sbn.tag;
            final int _id = sbn.id;
            vetoButton.setOnClickListener(new View.OnClickListener() { 
                    public void onClick(View v) {
                        try {
                            mBarService.onNotificationClear(_pkg, _tag, _id);
                        } catch (RemoteException ex) {
                            // system process is dead if we're here.
                        }
    //                    animateCollapse();
                    }
                });
        } else {
            vetoButton.setVisibility(View.INVISIBLE);
        }

        // bind the click event to the content area
        ViewGroup content = (ViewGroup)row.findViewById(R.id.content);
        // XXX: update to allow controls within notification views
        content.setDescendantFocusability(ViewGroup.FOCUS_BLOCK_DESCENDANTS);
//        content.setOnFocusChangeListener(mFocusChangeListener);
        PendingIntent contentIntent = sbn.notification.contentIntent;
        if (contentIntent != null) {
            content.setOnClickListener(new NotificationClicker(contentIntent,
                        sbn.pkg, sbn.tag, sbn.id));
        } else {
            content.setOnClickListener(null);
        }

        View expanded = null;
        Exception exception = null;
        try {
            expanded = remoteViews.apply(mContext, content);
        }
        catch (RuntimeException e) {
            exception = e;
        }
        if (expanded == null) {
            String ident = sbn.pkg + "/0x" + Integer.toHexString(sbn.id);
            Slog.e(TAG, "couldn't inflate view for notification " + ident, exception);
            return false;
        } else {
            content.addView(expanded);
            row.setDrawingCacheEnabled(true);
        }

        entry.row = row;
        entry.content = content;
        entry.expanded = expanded;

        return true;
    }

    public class SetLightsOnListener implements View.OnLongClickListener,
           View.OnClickListener {
        private boolean mOn;

        SetLightsOnListener(boolean on) {
            mOn = on;
        }

        public void onClick(View v) {
            try {
                mBarService.setLightsOn(mOn);
            } catch (RemoteException ex) {
                // system process
            }
        }

        public boolean onLongClick(View v) {
            try {
                mBarService.setLightsOn(mOn);
            } catch (RemoteException ex) {
                // system process
            }
            return true;
        }

    }

    public class TouchOutsideListener implements View.OnTouchListener {
        private int mMsg;
        private StatusBarPanel mPanel;

        public TouchOutsideListener(int msg, StatusBarPanel panel) {
            mMsg = msg;
            mPanel = panel;
        }

        public boolean onTouch(View v, MotionEvent ev) {
            final int action = ev.getAction();
            if (action == MotionEvent.ACTION_OUTSIDE
                    || (action == MotionEvent.ACTION_DOWN
                        && !mPanel.isInContentArea((int)ev.getX(), (int)ev.getY()))) {
                mHandler.removeMessages(mMsg);
                mHandler.sendEmptyMessage(mMsg);
                return true;
            }
            return false;
        }
    }

    private void setViewVisibility(View v, int vis, int anim) {
        if (v.getVisibility() != vis) {
            //Slog.d(TAG, "setViewVisibility vis=" + (vis == View.VISIBLE) + " v=" + v);
            v.setAnimation(AnimationUtils.loadAnimation(mContext, anim));
            v.setVisibility(vis);
        }
    }


    public void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        pw.print("mDisabled=0x");
        pw.println(Integer.toHexString(mDisabled));
    }
}



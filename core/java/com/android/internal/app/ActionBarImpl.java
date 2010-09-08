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

package com.android.internal.app;

import com.android.internal.view.menu.MenuBuilder;
import com.android.internal.view.menu.MenuPopupHelper;
import com.android.internal.view.menu.SubMenuBuilder;
import com.android.internal.widget.ActionBarContextView;
import com.android.internal.widget.ActionBarView;

import android.app.ActionBar;
import android.app.Activity;
import android.app.Dialog;
import android.app.Fragment;
import android.app.FragmentTransaction;
import android.content.Context;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.view.ActionMode;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SpinnerAdapter;
import android.widget.ViewAnimator;

import java.lang.ref.WeakReference;
import java.util.ArrayList;

/**
 * ActionBarImpl is the ActionBar implementation used
 * by devices of all screen sizes. If it detects a compatible decor,
 * it will split contextual modes across both the ActionBarView at
 * the top of the screen and a horizontal LinearLayout at the bottom
 * which is normally hidden.
 */
public class ActionBarImpl extends ActionBar {
    private static final int NORMAL_VIEW = 0;
    private static final int CONTEXT_VIEW = 1;
    
    private static final int TAB_SWITCH_SHOW_HIDE = 0;
    private static final int TAB_SWITCH_ADD_REMOVE = 1;

    private Context mContext;
    private Activity mActivity;
    private Dialog mDialog;

    private ViewAnimator mAnimatorView;
    private ActionBarView mActionView;
    private ActionBarContextView mUpperContextView;
    private LinearLayout mLowerContextView;

    private ArrayList<TabImpl> mTabs = new ArrayList<TabImpl>();

    private int mTabContainerViewId = android.R.id.content;
    private TabImpl mSelectedTab;
    private int mTabSwitchMode = TAB_SWITCH_ADD_REMOVE;
    
    private ActionMode mActionMode;
    
    private static final int CONTEXT_DISPLAY_NORMAL = 0;
    private static final int CONTEXT_DISPLAY_SPLIT = 1;
    
    private int mContextDisplayMode;

    private boolean mClosingContext;

    final Handler mHandler = new Handler();
    final Runnable mCloseContext = new Runnable() {
        public void run() {
            mUpperContextView.closeMode();
            if (mLowerContextView != null) {
                mLowerContextView.removeAllViews();
            }
            mClosingContext = false;
        }
    };

    public ActionBarImpl(Activity activity) {
        mActivity = activity;
        init(activity.getWindow().getDecorView());
    }

    public ActionBarImpl(Dialog dialog) {
        mDialog = dialog;
        init(dialog.getWindow().getDecorView());
    }

    private void init(View decor) {
        mContext = decor.getContext();
        mActionView = (ActionBarView) decor.findViewById(com.android.internal.R.id.action_bar);
        mUpperContextView = (ActionBarContextView) decor.findViewById(
                com.android.internal.R.id.action_context_bar);
        mLowerContextView = (LinearLayout) decor.findViewById(
                com.android.internal.R.id.lower_action_context_bar);
        mAnimatorView = (ViewAnimator) decor.findViewById(
                com.android.internal.R.id.action_bar_animator);

        if (mActionView == null || mUpperContextView == null || mAnimatorView == null) {
            throw new IllegalStateException(getClass().getSimpleName() + " can only be used " +
                    "with a compatible window decor layout");
        }

        mActionView.setContextView(mUpperContextView);
        mContextDisplayMode = mLowerContextView == null ?
                CONTEXT_DISPLAY_NORMAL : CONTEXT_DISPLAY_SPLIT;
    }

    @Override
    public void setTitle(int resId) {
        setTitle(mContext.getString(resId));
    }

    @Override
    public void setSubtitle(int resId) {
        setSubtitle(mContext.getString(resId));
    }

    public void setCustomNavigationMode(View view) {
        cleanupTabs();
        mActionView.setCustomNavigationView(view);
        mActionView.setCallback(null);
    }

    public void setDropdownNavigationMode(SpinnerAdapter adapter, NavigationCallback callback) {
        setDropdownNavigationMode(adapter, callback, -1);
    }

    public void setDropdownNavigationMode(SpinnerAdapter adapter, NavigationCallback callback,
            int defaultSelectedPosition) {
        cleanupTabs();
        mActionView.setNavigationMode(NAVIGATION_MODE_DROPDOWN_LIST);
        mActionView.setDropdownAdapter(adapter);
        if (defaultSelectedPosition >= 0) {
            mActionView.setDropdownSelectedPosition(defaultSelectedPosition);
        }
        mActionView.setCallback(callback);
    }

    public void setStandardNavigationMode() {
        cleanupTabs();
        mActionView.setNavigationMode(NAVIGATION_MODE_STANDARD);
        mActionView.setCallback(null);
    }

    public void setSelectedNavigationItem(int position) {
        switch (mActionView.getNavigationMode()) {
        case NAVIGATION_MODE_TABS:
            selectTab(mTabs.get(position));
            break;
        case NAVIGATION_MODE_DROPDOWN_LIST:
            mActionView.setDropdownSelectedPosition(position);
            break;
        default:
            throw new IllegalStateException(
                    "setSelectedNavigationItem not valid for current navigation mode");
        }
    }

    public int getSelectedNavigationItem() {
        switch (mActionView.getNavigationMode()) {
        case NAVIGATION_MODE_TABS:
            return mSelectedTab.getPosition();
        case NAVIGATION_MODE_DROPDOWN_LIST:
            return mActionView.getDropdownSelectedPosition();
        default:
            return -1;
        }
    }

    private void cleanupTabs() {
        if (mSelectedTab != null) {
            selectTab(null);
        }
        mTabs.clear();
    }

    public void setTitle(CharSequence title) {
        mActionView.setTitle(title);
    }

    public void setSubtitle(CharSequence subtitle) {
        mActionView.setSubtitle(subtitle);
    }

    public void setDisplayOptions(int options) {
        mActionView.setDisplayOptions(options);
    }

    public void setDisplayOptions(int options, int mask) {
        final int current = mActionView.getDisplayOptions(); 
        mActionView.setDisplayOptions((options & mask) | (current & ~mask));
    }

    public void setBackgroundDrawable(Drawable d) {
        mActionView.setBackgroundDrawable(d);
    }

    public View getCustomNavigationView() {
        return mActionView.getCustomNavigationView();
    }

    public CharSequence getTitle() {
        return mActionView.getTitle();
    }

    public CharSequence getSubtitle() {
        return mActionView.getSubtitle();
    }

    public int getNavigationMode() {
        return mActionView.getNavigationMode();
    }

    public int getDisplayOptions() {
        return mActionView.getDisplayOptions();
    }

    public ActionMode startActionMode(ActionMode.Callback callback) {
        if (mActionMode != null) {
            mActionMode.finish();
        }

        // Don't wait for the close context mode animation to finish.
        if (mClosingContext) {
            mAnimatorView.clearAnimation();
            mHandler.removeCallbacks(mCloseContext);
            mCloseContext.run();
        }

        ActionMode mode = new ActionModeImpl(callback);
        if (callback.onCreateActionMode(mode, mode.getMenu())) {
            mode.invalidate();
            mUpperContextView.initForMode(mode);
            mAnimatorView.setDisplayedChild(CONTEXT_VIEW);
            if (mLowerContextView != null) {
                // TODO animate this
                mLowerContextView.setVisibility(View.VISIBLE);
            }
            mActionMode = mode;
            return mode;
        }
        return null;
    }

    private void configureTab(Tab tab, int position) {
        final TabImpl tabi = (TabImpl) tab;
        final boolean isFirstTab = mTabs.isEmpty();
        final ActionBar.TabListener callback = tabi.getCallback();

        if (callback == null) {
            throw new IllegalStateException("Action Bar Tab must have a Callback");
        }

        tabi.setPosition(position);
        mTabs.add(position, tabi);

        if (isFirstTab) {
            final FragmentTransaction trans = mActivity.getFragmentManager().openTransaction();
            mSelectedTab = tabi;
            callback.onTabSelected(tab, trans);
            if (!trans.isEmpty()) {
                trans.commit();
            }
        }
    }

    @Override
    public void addTab(Tab tab) {
        mActionView.addTab(tab);
        configureTab(tab, mTabs.size());
    }

    @Override
    public void addTab(Tab tab, int position) {
        mActionView.addTab(tab, position);
        configureTab(tab, position);
    }

    @Override
    public Tab newTab() {
        return new TabImpl();
    }

    @Override
    public void removeTab(Tab tab) {
        removeTabAt(tab.getPosition());
    }

    @Override
    public void removeTabAt(int position) {
        mActionView.removeTabAt(position);
        mTabs.remove(position);

        final int newTabCount = mTabs.size();
        for (int i = position; i < newTabCount; i++) {
            mTabs.get(i).setPosition(i);
        }

        selectTab(mTabs.isEmpty() ? null : mTabs.get(Math.max(0, position - 1)));
    }

    @Override
    public void setTabNavigationMode() {
        if (mActivity == null) {
            throw new IllegalStateException(
                    "Tab navigation mode cannot be used outside of an Activity");
        }
        mActionView.setNavigationMode(NAVIGATION_MODE_TABS);
    }

    @Override
    public void selectTab(Tab tab) {
        if (mSelectedTab == tab) {
            return;
        }

        mActionView.setTabSelected(tab != null ? tab.getPosition() : Tab.INVALID_POSITION);
        final FragmentTransaction trans = mActivity.getFragmentManager().openTransaction();
        if (mSelectedTab != null) {
            mSelectedTab.getCallback().onTabUnselected(mSelectedTab, trans);
        }
        mSelectedTab = (TabImpl) tab;
        if (mSelectedTab != null) {
            mSelectedTab.getCallback().onTabSelected(mSelectedTab, trans);
        }

        if (!trans.isEmpty()) {
            trans.commit();
        }
    }

    @Override
    public Tab getSelectedTab() {
        return mSelectedTab;
    }

    @Override
    public int getHeight() {
        return mActionView.getHeight();
    }

    @Override
    public void show() {
        // TODO animate!
        mAnimatorView.setVisibility(View.VISIBLE);
    }

    @Override
    public void hide() {
        // TODO animate!
        mAnimatorView.setVisibility(View.GONE);
    }

    public boolean isShowing() {
        return mAnimatorView.getVisibility() == View.VISIBLE;
    }

    /**
     * @hide 
     */
    public class ActionModeImpl extends ActionMode implements MenuBuilder.Callback {
        private ActionMode.Callback mCallback;
        private MenuBuilder mMenu;
        private WeakReference<View> mCustomView;
        
        public ActionModeImpl(ActionMode.Callback callback) {
            mCallback = callback;
            mMenu = new MenuBuilder(mActionView.getContext())
                    .setDefaultShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM);
            mMenu.setCallback(this);
        }

        @Override
        public MenuInflater getMenuInflater() {
            return new MenuInflater(mContext);
        }

        @Override
        public Menu getMenu() {
            return mMenu;
        }

        @Override
        public void finish() {
            if (mActionMode != this) {
                // Not the active action mode - no-op
                return;
            }

            mCallback.onDestroyActionMode(this);
            mAnimatorView.setDisplayedChild(NORMAL_VIEW);

            // Clear out the context mode views after the animation finishes
            mClosingContext = true;
            mHandler.postDelayed(mCloseContext, mAnimatorView.getOutAnimation().getDuration());

            if (mLowerContextView != null && mLowerContextView.getVisibility() != View.GONE) {
                // TODO Animate this
                mLowerContextView.setVisibility(View.GONE);
            }
            mActionMode = null;
        }

        @Override
        public void invalidate() {
            if (mCallback.onPrepareActionMode(this, mMenu)) {
                // Refresh content in both context views
            }
        }

        @Override
        public void setCustomView(View view) {
            mUpperContextView.setCustomView(view);
            mCustomView = new WeakReference<View>(view);
        }

        @Override
        public void setSubtitle(CharSequence subtitle) {
            mUpperContextView.setSubtitle(subtitle);
        }

        @Override
        public void setTitle(CharSequence title) {
            mUpperContextView.setTitle(title);
        }

        @Override
        public void setTitle(int resId) {
            setTitle(mActivity.getString(resId));
        }

        @Override
        public void setSubtitle(int resId) {
            setSubtitle(mActivity.getString(resId));
        }

        @Override
        public CharSequence getTitle() {
            return mUpperContextView.getTitle();
        }

        @Override
        public CharSequence getSubtitle() {
            return mUpperContextView.getSubtitle();
        }
        
        @Override
        public View getCustomView() {
            return mCustomView != null ? mCustomView.get() : null;
        }

        public boolean onMenuItemSelected(MenuBuilder menu, MenuItem item) {
            return mCallback.onActionItemClicked(this, item);
        }

        public void onCloseMenu(MenuBuilder menu, boolean allMenusAreClosing) {
        }

        public boolean onSubMenuSelected(SubMenuBuilder subMenu) {
            if (!subMenu.hasVisibleItems()) {
                return true;
            }

            new MenuPopupHelper(mContext, subMenu).show();
            return true;
        }

        public void onCloseSubMenu(SubMenuBuilder menu) {
        }

        public void onMenuModeChange(MenuBuilder menu) {
            invalidate();
            mUpperContextView.showOverflowMenu();
        }
    }

    /**
     * @hide
     */
    public class TabImpl extends ActionBar.Tab {
        private ActionBar.TabListener mCallback;
        private Object mTag;
        private Drawable mIcon;
        private CharSequence mText;
        private int mPosition;
        private View mCustomView;

        @Override
        public Object getTag() {
            return mTag;
        }

        @Override
        public void setTag(Object tag) {
            mTag = tag;
        }

        public ActionBar.TabListener getCallback() {
            return mCallback;
        }

        @Override
        public void setTabListener(ActionBar.TabListener callback) {
            mCallback = callback;
        }

        @Override
        public View getCustomView() {
            return mCustomView;
        }

        @Override
        public void setCustomView(View view) {
            mCustomView = view;
        }

        @Override
        public Drawable getIcon() {
            return mIcon;
        }

        @Override
        public int getPosition() {
            return mPosition;
        }

        public void setPosition(int position) {
            mPosition = position;
        }

        @Override
        public CharSequence getText() {
            return mText;
        }

        @Override
        public void setIcon(Drawable icon) {
            mIcon = icon;
        }

        @Override
        public void setText(CharSequence text) {
            mText = text;
        }

        @Override
        public void select() {
            selectTab(this);
        }
    }
}

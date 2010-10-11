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

package com.android.internal.widget;

import com.android.internal.R;
import com.android.internal.view.menu.ActionMenuItem;
import com.android.internal.view.menu.ActionMenuView;
import com.android.internal.view.menu.MenuBuilder;

import android.app.ActionBar;
import android.app.ActionBar.NavigationCallback;
import android.app.Activity;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.TypedArray;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.drawable.Drawable;
import android.text.TextUtils.TruncateAt;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.view.ActionMode;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.SpinnerAdapter;
import android.widget.TextView;

/**
 * @hide
 */
public class ActionBarView extends ViewGroup {
    private static final String TAG = "ActionBarView";

    /**
     * Display options applied by default
     */
    public static final int DISPLAY_DEFAULT = 0;

    /**
     * Display options that require re-layout as opposed to a simple invalidate
     */
    private static final int DISPLAY_RELAYOUT_MASK =
            ActionBar.DISPLAY_HIDE_HOME |
            ActionBar.DISPLAY_USE_LOGO;
    
    private final int mContentHeight;

    private int mNavigationMode;
    private int mDisplayOptions;
    private CharSequence mTitle;
    private CharSequence mSubtitle;
    private Drawable mIcon;
    private Drawable mLogo;
    private Drawable mDivider;

    private ImageView mIconView;
    private ImageView mLogoView;
    private LinearLayout mTitleLayout;
    private TextView mTitleView;
    private TextView mSubtitleView;
    private Spinner mSpinner;
    private HorizontalScrollView mTabScrollView;
    private LinearLayout mTabLayout;
    private View mCustomNavView;
    
    private int mTitleStyleRes;
    private int mSubtitleStyleRes;

    private boolean mShowMenu;
    private boolean mUserTitle;

    private MenuBuilder mOptionsMenu;
    private ActionMenuView mMenuView;
    
    private ActionBarContextView mContextView;

    private ActionMenuItem mLogoNavItem;
    
    private NavigationCallback mCallback;

    private final AdapterView.OnItemSelectedListener mNavItemSelectedListener =
            new AdapterView.OnItemSelectedListener() {
        public void onItemSelected(AdapterView parent, View view, int position, long id) {
            if (mCallback != null) {
                mCallback.onNavigationItemSelected(position, id);
            }
        }
        public void onNothingSelected(AdapterView parent) {
            // Do nothing
        }
    };

    private OnClickListener mHomeClickListener = null;

    private OnClickListener mTabClickListener = null;

    public ActionBarView(Context context, AttributeSet attrs) {
        super(context, attrs);

        TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.ActionBar);

        final int colorFilter = a.getColor(R.styleable.ActionBar_colorFilter, 0);

        if (colorFilter != 0) {
            final Drawable d = getBackground();
            d.setDither(true);
            d.setColorFilter(new PorterDuffColorFilter(colorFilter, PorterDuff.Mode.OVERLAY));
        }

        ApplicationInfo info = context.getApplicationInfo();
        PackageManager pm = context.getPackageManager();
        mNavigationMode = a.getInt(R.styleable.ActionBar_navigationMode,
                ActionBar.NAVIGATION_MODE_STANDARD);
        mTitle = a.getText(R.styleable.ActionBar_title);
        mSubtitle = a.getText(R.styleable.ActionBar_subtitle);
        mDisplayOptions = a.getInt(R.styleable.ActionBar_displayOptions, DISPLAY_DEFAULT);
        
        mLogo = a.getDrawable(R.styleable.ActionBar_logo);
        if (mLogo == null) {
            mLogo = info.loadLogo(pm);
        }
        mIcon = a.getDrawable(R.styleable.ActionBar_icon);
        if (mIcon == null) {
            mIcon = info.loadIcon(pm);
        }
        
        Drawable background = a.getDrawable(R.styleable.ActionBar_background);
        if (background != null) {
            setBackgroundDrawable(background);
        }
        
        mTitleStyleRes = a.getResourceId(R.styleable.ActionBar_titleTextStyle, 0);
        mSubtitleStyleRes = a.getResourceId(R.styleable.ActionBar_subtitleTextStyle, 0);

        final int customNavId = a.getResourceId(R.styleable.ActionBar_customNavigationLayout, 0);
        if (customNavId != 0) {
            LayoutInflater inflater = LayoutInflater.from(context);
            mCustomNavView = (View) inflater.inflate(customNavId, null);
            mNavigationMode = ActionBar.NAVIGATION_MODE_CUSTOM;
        }

        mContentHeight = a.getLayoutDimension(R.styleable.ActionBar_height, 0);
        
        mDivider = a.getDrawable(R.styleable.ActionBar_divider);

        a.recycle();
        
        if (mLogo != null || mIcon != null || mTitle != null) {
            mLogoNavItem = new ActionMenuItem(context, 0, android.R.id.home, 0, 0, mTitle);
            mHomeClickListener = new OnClickListener() {
                public void onClick(View v) {
                    Context context = getContext();
                    if (context instanceof Activity) {
                        Activity activity = (Activity) context;
                        activity.onOptionsItemSelected(mLogoNavItem);
                    }
                }
            };
        }
    }

    @Override
    public ActionMode startActionModeForChild(View child, ActionMode.Callback callback) {
        // No starting an action mode for an action bar child! (Where would it go?)
        return null;
    }

    public void setCallback(NavigationCallback callback) {
        mCallback = callback;
    }

    public void setMenu(Menu menu) {
        MenuBuilder builder = (MenuBuilder) menu;
        mOptionsMenu = builder;
        if (mMenuView != null) {
            removeView(mMenuView);
        }
        final ActionMenuView menuView = (ActionMenuView) builder.getMenuView(
                MenuBuilder.TYPE_ACTION_BUTTON, null);
        final LayoutParams layoutParams = new LayoutParams(LayoutParams.WRAP_CONTENT,
                LayoutParams.MATCH_PARENT);
        menuView.setLayoutParams(layoutParams);
        addView(menuView);
        mMenuView = menuView;
    }

    public boolean showOverflowMenu() {
        if (mMenuView != null) {
            return mMenuView.showOverflowMenu();
        }
        return false;
    }

    public void postShowOverflowMenu() {
        post(new Runnable() {
            public void run() {
                showOverflowMenu();
            }
        });
    }

    public boolean hideOverflowMenu() {
        if (mMenuView != null) {
            return mMenuView.hideOverflowMenu();
        }
        return false;
    }

    public boolean isOverflowMenuShowing() {
        if (mMenuView != null) {
            return mMenuView.isOverflowMenuShowing();
        }
        return false;
    }

    public boolean isOverflowReserved() {
        return mMenuView != null && mMenuView.isOverflowReserved();
    }

    public void setCustomNavigationView(View view) {
        mCustomNavView = view;
        if (view != null) {
            setNavigationMode(ActionBar.NAVIGATION_MODE_CUSTOM);
        }
    }

    public CharSequence getTitle() {
        return mTitle;
    }

    /**
     * Set the action bar title. This will always replace or override window titles.
     * @param title Title to set
     *
     * @see #setWindowTitle(CharSequence)
     */
    public void setTitle(CharSequence title) {
        mUserTitle = true;
        setTitleImpl(title);
    }

    /**
     * Set the window title. A window title will always be replaced or overridden by a user title.
     * @param title Title to set
     *
     * @see #setTitle(CharSequence)
     */
    public void setWindowTitle(CharSequence title) {
        if (!mUserTitle) {
            setTitleImpl(title);
        }
    }

    private void setTitleImpl(CharSequence title) {
        mTitle = title;
        if (mTitleView != null) {
            mTitleView.setText(title);
        }
        if (mLogoNavItem != null) {
            mLogoNavItem.setTitle(title);
        }
    }

    public CharSequence getSubtitle() {
        return mSubtitle;
    }

    public void setSubtitle(CharSequence subtitle) {
        mSubtitle = subtitle;
        if (mSubtitleView != null) {
            mSubtitleView.setText(subtitle);
        }
    }

    public void setDisplayOptions(int options) {
        final int flagsChanged = options ^ mDisplayOptions;
        mDisplayOptions = options;
        if ((flagsChanged & DISPLAY_RELAYOUT_MASK) != 0) {
            final int vis = (options & ActionBar.DISPLAY_HIDE_HOME) != 0 ? GONE : VISIBLE;
            if (mLogoView != null) {
                mLogoView.setVisibility(vis);
            }
            if (mIconView != null) {
                mIconView.setVisibility(vis);
            }
            
            requestLayout();
        } else {
            invalidate();
        }
    }

    public void setNavigationMode(int mode) {
        final int oldMode = mNavigationMode;
        if (mode != oldMode) {
            switch (oldMode) {
            case ActionBar.NAVIGATION_MODE_STANDARD:
                if (mTitleLayout != null) {
                    removeView(mTitleLayout);
                    mTitleLayout = null;
                    mTitleView = null;
                    mSubtitleView = null;
                }
                break;
            case ActionBar.NAVIGATION_MODE_DROPDOWN_LIST:
                if (mSpinner != null) {
                    removeView(mSpinner);
                    mSpinner = null;
                }
                break;
            case ActionBar.NAVIGATION_MODE_CUSTOM:
                if (mCustomNavView != null) {
                    removeView(mCustomNavView);
                    mCustomNavView = null;
                }
                break;
            case ActionBar.NAVIGATION_MODE_TABS:
                if (mTabLayout != null) {
                    removeView(mTabScrollView);
                    mTabLayout = null;
                    mTabScrollView = null;
                }
            }
            
            switch (mode) {
            case ActionBar.NAVIGATION_MODE_STANDARD:
                initTitle();
                break;
            case ActionBar.NAVIGATION_MODE_DROPDOWN_LIST:
                mSpinner = new Spinner(mContext, null,
                        com.android.internal.R.attr.dropDownSpinnerStyle);
                mSpinner.setOnItemSelectedListener(mNavItemSelectedListener);
                addView(mSpinner);
                break;
            case ActionBar.NAVIGATION_MODE_CUSTOM:
                addView(mCustomNavView);
                break;
            case ActionBar.NAVIGATION_MODE_TABS:
                mTabScrollView = new HorizontalScrollView(getContext());
                mTabLayout = new LinearLayout(getContext(), null,
                        com.android.internal.R.attr.actionBarTabBarStyle);
                mTabScrollView.addView(mTabLayout);
                addView(mTabScrollView);
                break;
            }
            mNavigationMode = mode;
            requestLayout();
        }
    }
    
    public void setDropdownAdapter(SpinnerAdapter adapter) {
        mSpinner.setAdapter(adapter);
    }

    public void setDropdownSelectedPosition(int position) {
        mSpinner.setSelection(position);
    }

    public int getDropdownSelectedPosition() {
        return mSpinner.getSelectedItemPosition();
    }

    public View getCustomNavigationView() {
        return mCustomNavView;
    }
    
    public int getNavigationMode() {
        return mNavigationMode;
    }
    
    public int getDisplayOptions() {
        return mDisplayOptions;
    }

    private TabView createTabView(ActionBar.Tab tab) {
        final TabView tabView = new TabView(getContext(), tab);
        tabView.setFocusable(true);

        if (mTabClickListener == null) {
            mTabClickListener = new TabClickListener();
        }
        tabView.setOnClickListener(mTabClickListener);
        return tabView;
    }

    public void addTab(ActionBar.Tab tab) {
        final boolean isFirst = mTabLayout.getChildCount() == 0;
        View tabView = createTabView(tab);
        mTabLayout.addView(tabView);
        if (isFirst) {
            tabView.setSelected(true);
        }
    }

    public void addTab(ActionBar.Tab tab, int position) {
        final boolean isFirst = mTabLayout.getChildCount() == 0;
        final TabView tabView = createTabView(tab);
        mTabLayout.addView(tabView, position);
        if (isFirst) {
            tabView.setSelected(true);
        }
    }

    public void removeTabAt(int position) {
        mTabLayout.removeViewAt(position);
    }

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        // Used by custom nav views if they don't supply layout params. Everything else
        // added to an ActionBarView should have them already.
        return new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        if ((mDisplayOptions & ActionBar.DISPLAY_HIDE_HOME) == 0) {
            if (mLogo != null && (mDisplayOptions & ActionBar.DISPLAY_USE_LOGO) != 0) {
                mLogoView = new ImageView(getContext(), null,
                        com.android.internal.R.attr.actionButtonStyle);
                mLogoView.setAdjustViewBounds(true);
                mLogoView.setLayoutParams(new LayoutParams(LayoutParams.WRAP_CONTENT,
                        LayoutParams.MATCH_PARENT));
                mLogoView.setImageDrawable(mLogo);
                mLogoView.setClickable(true);
                mLogoView.setFocusable(true);
                mLogoView.setOnClickListener(mHomeClickListener);
                addView(mLogoView);
            } else if (mIcon != null) {
                mIconView = new ImageView(getContext(), null,
                        com.android.internal.R.attr.actionButtonStyle);
                mIconView.setAdjustViewBounds(true);
                mIconView.setLayoutParams(new LayoutParams(LayoutParams.WRAP_CONTENT,
                        LayoutParams.MATCH_PARENT));
                mIconView.setImageDrawable(mIcon);
                mIconView.setClickable(true);
                mIconView.setFocusable(true);
                mIconView.setOnClickListener(mHomeClickListener);
                addView(mIconView);
            }
        }

        switch (mNavigationMode) {
        case ActionBar.NAVIGATION_MODE_STANDARD:
            if (mLogoView == null) {
                initTitle();
            }
            break;
            
        case ActionBar.NAVIGATION_MODE_DROPDOWN_LIST:
            throw new UnsupportedOperationException(
                    "Inflating dropdown list navigation isn't supported yet!");
            
        case ActionBar.NAVIGATION_MODE_TABS:
            throw new UnsupportedOperationException(
                    "Inflating tab navigation isn't supported yet!");
            
        case ActionBar.NAVIGATION_MODE_CUSTOM:
            if (mCustomNavView != null) {
                addView(mCustomNavView);
            }
            break;
        }
    }
    
    private void initTitle() {
        LayoutInflater inflater = LayoutInflater.from(getContext());
        mTitleLayout = (LinearLayout) inflater.inflate(R.layout.action_bar_title_item, null);
        mTitleView = (TextView) mTitleLayout.findViewById(R.id.action_bar_title);
        mSubtitleView = (TextView) mTitleLayout.findViewById(R.id.action_bar_subtitle);

        if (mTitleStyleRes != 0) {
            mTitleView.setTextAppearance(mContext, mTitleStyleRes);
        }
        if (mTitle != null) {
            mTitleView.setText(mTitle);
        }

        if (mSubtitleStyleRes != 0) {
            mSubtitleView.setTextAppearance(mContext, mSubtitleStyleRes);
        }
        if (mSubtitle != null) {
            mSubtitleView.setText(mSubtitle);
            mSubtitleView.setVisibility(VISIBLE);
        }

        addView(mTitleLayout);
    }

    public void setTabSelected(int position) {
        final int tabCount = mTabLayout.getChildCount();
        for (int i = 0; i < tabCount; i++) {
            final View child = mTabLayout.getChildAt(i);
            child.setSelected(i == position);
        }
    }

    public void setContextView(ActionBarContextView view) {
        mContextView = view;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        if (widthMode != MeasureSpec.EXACTLY) {
            throw new IllegalStateException(getClass().getSimpleName() + " can only be used " +
                    "with android:layout_width=\"match_parent\" (or fill_parent)");
        }
        
        int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        if (heightMode != MeasureSpec.AT_MOST) {
            throw new IllegalStateException(getClass().getSimpleName() + " can only be used " +
                    "with android:layout_height=\"wrap_content\"");
        }

        int contentWidth = MeasureSpec.getSize(widthMeasureSpec);

        int maxHeight = mContentHeight > 0 ?
                mContentHeight : MeasureSpec.getSize(heightMeasureSpec);
        
        final int verticalPadding = getPaddingTop() + getPaddingBottom();
        int availableWidth = contentWidth - getPaddingLeft() - getPaddingRight();
        final int height = maxHeight - verticalPadding;
        final int childSpecHeight = MeasureSpec.makeMeasureSpec(height, MeasureSpec.AT_MOST);

        if (mLogoView != null && mLogoView.getVisibility() != GONE) {
            availableWidth = measureChildView(mLogoView, availableWidth, childSpecHeight, 0);
        }
        if (mIconView != null && mIconView.getVisibility() != GONE) {
            availableWidth = measureChildView(mIconView, availableWidth, childSpecHeight, 0);
        }
        
        if (mMenuView != null) {
            availableWidth = measureChildView(mMenuView, availableWidth,
                    childSpecHeight, 0);
        }
        
        switch (mNavigationMode) {
        case ActionBar.NAVIGATION_MODE_STANDARD:
            if (mTitleLayout != null) {
                measureChildView(mTitleLayout, availableWidth, childSpecHeight, 0);
            }
            break;
        case ActionBar.NAVIGATION_MODE_DROPDOWN_LIST:
            if (mSpinner != null) {
                mSpinner.measure(
                        MeasureSpec.makeMeasureSpec(availableWidth, MeasureSpec.AT_MOST),
                        MeasureSpec.makeMeasureSpec(height, MeasureSpec.AT_MOST));
            }
            break;
        case ActionBar.NAVIGATION_MODE_CUSTOM:
            if (mCustomNavView != null) {
                LayoutParams lp = mCustomNavView.getLayoutParams();
                final int customNavWidthMode = lp.width != LayoutParams.WRAP_CONTENT ?
                        MeasureSpec.EXACTLY : MeasureSpec.AT_MOST;
                final int customNavWidth = lp.width >= 0 ?
                        Math.min(lp.width, availableWidth) : availableWidth;

                // If the action bar is wrapping to its content height, don't allow a custom
                // view to MATCH_PARENT.
                int customNavHeightMode;
                if (mContentHeight <= 0) {
                    customNavHeightMode = MeasureSpec.AT_MOST;
                } else {
                    customNavHeightMode = lp.height != LayoutParams.WRAP_CONTENT ?
                            MeasureSpec.EXACTLY : MeasureSpec.AT_MOST;
                }
                final int customNavHeight = lp.height >= 0 ?
                        Math.min(lp.height, height) : height;
                mCustomNavView.measure(
                        MeasureSpec.makeMeasureSpec(customNavWidth, customNavWidthMode),
                        MeasureSpec.makeMeasureSpec(customNavHeight, customNavHeightMode));
            }
            break;
        case ActionBar.NAVIGATION_MODE_TABS:
            if (mTabScrollView != null) {
                mTabScrollView.measure(
                        MeasureSpec.makeMeasureSpec(availableWidth, MeasureSpec.AT_MOST),
                        MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY));
            }
            break;
        }

        if (mContentHeight <= 0) {
            int measuredHeight = 0;
            final int count = getChildCount();
            for (int i = 0; i < count; i++) {
                View v = getChildAt(i);
                int paddedViewHeight = v.getMeasuredHeight() + verticalPadding;
                if (paddedViewHeight > measuredHeight) {
                    measuredHeight = paddedViewHeight;
                }
            }
            setMeasuredDimension(contentWidth, measuredHeight);
        } else {
            setMeasuredDimension(contentWidth, maxHeight);
        }

        if (mContextView != null) {
            mContextView.setHeight(getMeasuredHeight());
        }
    }

    private int measureChildView(View child, int availableWidth, int childSpecHeight, int spacing) {
        child.measure(MeasureSpec.makeMeasureSpec(availableWidth, MeasureSpec.AT_MOST),
                childSpecHeight);

        availableWidth -= child.getMeasuredWidth();
        availableWidth -= spacing;

        return availableWidth;
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int x = getPaddingLeft();
        final int y = getPaddingTop();
        final int contentHeight = b - t - getPaddingTop() - getPaddingBottom();

        if (mLogoView != null && mLogoView.getVisibility() != GONE) {
            x += positionChild(mLogoView, x, y, contentHeight);
        }
        if (mIconView != null && mIconView.getVisibility() != GONE) {
            x += positionChild(mIconView, x, y, contentHeight);
        }
        
        switch (mNavigationMode) {
        case ActionBar.NAVIGATION_MODE_STANDARD:
            if (mTitleLayout != null) {
                x += positionChild(mTitleLayout, x, y, contentHeight);
            }
            break;
        case ActionBar.NAVIGATION_MODE_DROPDOWN_LIST:
            if (mSpinner != null) {
                x += positionChild(mSpinner, x, y, contentHeight);
            }
            break;
        case ActionBar.NAVIGATION_MODE_CUSTOM:
            if (mCustomNavView != null) {
                x += positionChild(mCustomNavView, x, y, contentHeight);
            }
            break;
        case ActionBar.NAVIGATION_MODE_TABS:
            if (mTabScrollView != null) {
                x += positionChild(mTabScrollView, x, y, contentHeight);
            }
        }

        x = r - l - getPaddingRight();

        if (mMenuView != null) {
            x -= positionChildInverse(mMenuView, x, y, contentHeight);
        }
    }

    private int positionChild(View child, int x, int y, int contentHeight) {
        int childWidth = child.getMeasuredWidth();
        int childHeight = child.getMeasuredHeight();
        int childTop = y + (contentHeight - childHeight) / 2;

        child.layout(x, childTop, x + childWidth, childTop + childHeight);

        return childWidth;
    }
    
    private int positionChildInverse(View child, int x, int y, int contentHeight) {
        int childWidth = child.getMeasuredWidth();
        int childHeight = child.getMeasuredHeight();
        int childTop = y + (contentHeight - childHeight) / 2;

        child.layout(x - childWidth, childTop, x, childTop + childHeight);

        return childWidth;
    }

    private static class TabView extends LinearLayout {
        private ActionBar.Tab mTab;

        public TabView(Context context, ActionBar.Tab tab) {
            super(context, null, com.android.internal.R.attr.actionBarTabStyle);
            mTab = tab;

            final View custom = tab.getCustomView();
            if (custom != null) {
                addView(custom);
            } else {
                // TODO Style tabs based on the theme

                final Drawable icon = tab.getIcon();
                final CharSequence text = tab.getText();

                if (icon != null) {
                    ImageView iconView = new ImageView(context);
                    iconView.setImageDrawable(icon);
                    LayoutParams lp = new LayoutParams(LayoutParams.WRAP_CONTENT,
                            LayoutParams.WRAP_CONTENT);
                    lp.gravity = Gravity.CENTER_VERTICAL;
                    iconView.setLayoutParams(lp);
                    addView(iconView);
                }

                if (text != null) {
                    TextView textView = new TextView(context, null,
                            com.android.internal.R.attr.actionBarTabTextStyle);
                    textView.setText(text);
                    textView.setSingleLine();
                    textView.setEllipsize(TruncateAt.END);
                    LayoutParams lp = new LayoutParams(LayoutParams.WRAP_CONTENT,
                            LayoutParams.WRAP_CONTENT);
                    lp.gravity = Gravity.CENTER_VERTICAL;
                    textView.setLayoutParams(lp);
                    addView(textView);
                }
            }

            setLayoutParams(new LayoutParams(LayoutParams.WRAP_CONTENT,
                    LayoutParams.MATCH_PARENT, 1));
        }

        public ActionBar.Tab getTab() {
            return mTab;
        }
    }

    private class TabClickListener implements OnClickListener {
        public void onClick(View view) {
            TabView tabView = (TabView) view;
            tabView.getTab().select();
            final int tabCount = mTabLayout.getChildCount();
            for (int i = 0; i < tabCount; i++) {
                final View child = mTabLayout.getChildAt(i);
                child.setSelected(child == view);
            }
        }
    }
}

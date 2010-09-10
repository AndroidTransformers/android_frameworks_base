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
import com.android.internal.view.menu.ActionMenuView;
import com.android.internal.view.menu.MenuBuilder;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.view.ActionMode;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.View.MeasureSpec;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * @hide
 */
public class ActionBarContextView extends ViewGroup {
    private int mItemPadding;
    private int mActionSpacing;
    private int mContentHeight;
    
    private CharSequence mTitle;
    private CharSequence mSubtitle;
    
    private ImageButton mCloseButton;
    private View mCustomView;
    private LinearLayout mTitleLayout;
    private TextView mTitleView;
    private TextView mSubtitleView;
    private int mCloseButtonStyle;
    private int mTitleStyleRes;
    private int mSubtitleStyleRes;
    private ActionMenuView mMenuView;
    
    public ActionBarContextView(Context context) {
        this(context, null);
    }
    
    public ActionBarContextView(Context context, AttributeSet attrs) {
        this(context, attrs, com.android.internal.R.attr.actionModeStyle);
    }
    
    public ActionBarContextView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        
        TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.ActionMode, defStyle, 0);
        mItemPadding = a.getDimensionPixelOffset(
                com.android.internal.R.styleable.ActionMode_itemPadding, 0);
        setBackgroundDrawable(a.getDrawable(
                com.android.internal.R.styleable.ActionMode_background));
        mCloseButtonStyle = a.getResourceId(
                com.android.internal.R.styleable.ActionMode_closeButtonStyle, 0);
        mTitleStyleRes = a.getResourceId(
                com.android.internal.R.styleable.ActionMode_titleTextStyle, 0);
        mSubtitleStyleRes = a.getResourceId(
                com.android.internal.R.styleable.ActionMode_subtitleTextStyle, 0);

        mContentHeight = a.getLayoutDimension(
                com.android.internal.R.styleable.ActionMode_height, 0);
        a.recycle();
    }
    
    @Override
    public ActionMode startActionModeForChild(View child, ActionMode.Callback callback) {
        // No starting an action mode for an existing action mode UI child! (Where would it go?)
        return null;
    }

    public void setHeight(int height) {
        mContentHeight = height;
    }

    public void setCustomView(View view) {
        if (mCustomView != null) {
            removeView(mCustomView);
        }
        mCustomView = view;
        if (mTitleLayout != null) {
            removeView(mTitleLayout);
            mTitleLayout = null;
        }
        if (view != null) {
            addView(view);
        }
        requestLayout();
    }

    public void setTitle(CharSequence title) {
        mTitle = title;
        initTitle();
    }

    public void setSubtitle(CharSequence subtitle) {
        mSubtitle = subtitle;
        initTitle();
    }

    public CharSequence getTitle() {
        return mTitle;
    }

    public CharSequence getSubtitle() {
        return mSubtitle;
    }

    private void initTitle() {
        if (mTitleLayout == null) {
            LayoutInflater inflater = LayoutInflater.from(getContext());
            mTitleLayout = (LinearLayout) inflater.inflate(R.layout.action_bar_title_item, null);
            mTitleView = (TextView) mTitleLayout.findViewById(R.id.action_bar_title);
            mSubtitleView = (TextView) mTitleLayout.findViewById(R.id.action_bar_subtitle);
            if (mTitle != null) {
                mTitleView.setText(mTitle);
                if (mTitleStyleRes != 0) {
                    mTitleView.setTextAppearance(mContext, mTitleStyleRes);
                }
            }
            if (mSubtitle != null) {
                mSubtitleView.setText(mSubtitle);
                if (mSubtitleStyleRes != 0) {
                    mSubtitleView.setTextAppearance(mContext, mSubtitleStyleRes);
                }
                mSubtitleView.setVisibility(VISIBLE);
            }
            addView(mTitleLayout);
        } else {
            mTitleView.setText(mTitle);
            mSubtitleView.setText(mSubtitle);
            mSubtitleView.setVisibility(mSubtitle != null ? VISIBLE : GONE);
            if (mTitleLayout.getParent() == null) {
                addView(mTitleLayout);
            }
        }
    }

    public void initForMode(final ActionMode mode) {
        if (mCloseButton == null) {
            mCloseButton = new ImageButton(getContext(), null, mCloseButtonStyle);
        }
        mCloseButton.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                mode.finish();
            }
        });
        addView(mCloseButton);

        final MenuBuilder menu = (MenuBuilder) mode.getMenu();
        mMenuView = (ActionMenuView) menu.getMenuView(MenuBuilder.TYPE_ACTION_BUTTON, this);
        mMenuView.setOverflowReserved(true);
        mMenuView.updateChildren(false);
        addView(mMenuView);
    }

    public void closeMode() {
        removeAllViews();
        mCustomView = null;
        mMenuView = null;
    }

    public boolean showOverflowMenu() {
        if (mMenuView != null) {
            return mMenuView.showOverflowMenu();
        }
        return false;
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

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        // Used by custom views if they don't supply layout params. Everything else
        // added to an ActionBarContextView should have them already.
        return new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        final int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        if (widthMode != MeasureSpec.EXACTLY) {
            throw new IllegalStateException(getClass().getSimpleName() + " can only be used " +
                    "with android:layout_width=\"match_parent\" (or fill_parent)");
        }

        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        if (heightMode != MeasureSpec.AT_MOST) {
            throw new IllegalStateException(getClass().getSimpleName() + " can only be used " +
                    "with android:layout_height=\"wrap_content\"");
        }
        
        final int contentWidth = MeasureSpec.getSize(widthMeasureSpec);
        final int itemMargin = mItemPadding;

        int maxHeight = mContentHeight > 0 ?
                mContentHeight : MeasureSpec.getSize(heightMeasureSpec);

        final int verticalPadding = getPaddingTop() + getPaddingBottom();
        int availableWidth = contentWidth - getPaddingLeft() - getPaddingRight();
        final int height = maxHeight - verticalPadding;
        final int childSpecHeight = MeasureSpec.makeMeasureSpec(height, MeasureSpec.AT_MOST);
        
        if (mCloseButton != null) {
            availableWidth = measureChildView(mCloseButton, availableWidth,
                    childSpecHeight, itemMargin);
        }

        if (mTitleLayout != null && mCustomView == null) {
            availableWidth = measureChildView(mTitleLayout, availableWidth,
                    childSpecHeight, itemMargin);
        }

        final int childCount = getChildCount();
        for (int i = 0; i < childCount; i++) {
            final View child = getChildAt(i);
            if (child == mCloseButton || child == mTitleLayout || child == mCustomView) {
                continue;
            }
            
            availableWidth = measureChildView(child, availableWidth, childSpecHeight, itemMargin);
        }

        if (mCustomView != null) {
            LayoutParams lp = mCustomView.getLayoutParams();
            final int customWidthMode = lp.width != LayoutParams.WRAP_CONTENT ?
                    MeasureSpec.EXACTLY : MeasureSpec.AT_MOST;
            final int customWidth = lp.width >= 0 ?
                    Math.min(lp.width, availableWidth) : availableWidth;
            final int customHeightMode = lp.height != LayoutParams.WRAP_CONTENT ?
                    MeasureSpec.EXACTLY : MeasureSpec.AT_MOST;
            final int customHeight = lp.height >= 0 ?
                    Math.min(lp.height, height) : height;
            mCustomView.measure(MeasureSpec.makeMeasureSpec(customWidth, customWidthMode),
                    MeasureSpec.makeMeasureSpec(customHeight, customHeightMode));
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
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int x = getPaddingLeft();
        final int y = getPaddingTop();
        final int contentHeight = b - t - getPaddingTop() - getPaddingBottom();
        final int itemMargin = mItemPadding;
        
        if (mCloseButton != null && mCloseButton.getVisibility() != GONE) {
            x += positionChild(mCloseButton, x, y, contentHeight);
        }
        
        if (mTitleLayout != null && mCustomView == null) {
            x += positionChild(mTitleLayout, x, y, contentHeight) + itemMargin;
        }
        
        if (mCustomView != null) {
            x += positionChild(mCustomView, x, y, contentHeight) + itemMargin;
        }
        
        x = r - l - getPaddingRight();

        if (mMenuView != null) {
            x -= positionChildInverse(mMenuView, x + mActionSpacing, y, contentHeight)
                    - mActionSpacing;
        }
    }

    private int measureChildView(View child, int availableWidth, int childSpecHeight, int spacing) {
        child.measure(MeasureSpec.makeMeasureSpec(availableWidth, MeasureSpec.AT_MOST),
                childSpecHeight);

        availableWidth -= child.getMeasuredWidth();
        availableWidth -= spacing;

        return availableWidth;
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
}

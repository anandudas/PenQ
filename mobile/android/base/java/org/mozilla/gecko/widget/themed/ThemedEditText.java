// This file is generated by generate_themed_views.py; do not edit.

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.widget.themed;

import android.support.v4.content.ContextCompat;
import org.mozilla.gecko.GeckoApplication;
import org.mozilla.gecko.lwt.LightweightTheme;
import org.mozilla.gecko.R;
import org.mozilla.gecko.util.DrawableUtil;

import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.TypedArray;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;

public class ThemedEditText extends android.widget.EditText
                                     implements LightweightTheme.OnChangeListener {
    private LightweightTheme theme;

    private static final int[] STATE_PRIVATE_MODE = { R.attr.state_private };
    private static final int[] STATE_LIGHT = { R.attr.state_light };
    private static final int[] STATE_DARK = { R.attr.state_dark };

    protected static final int[] PRIVATE_PRESSED_STATE_SET = { R.attr.state_private, android.R.attr.state_pressed };
    protected static final int[] PRIVATE_FOCUSED_STATE_SET = { R.attr.state_private, android.R.attr.state_focused };
    protected static final int[] PRIVATE_STATE_SET = { R.attr.state_private };

    private boolean isPrivate;
    private boolean isLight;
    private boolean isDark;
    private boolean autoUpdateTheme;        // always false if there's no theme.

    private ColorStateList drawableColors;

    public ThemedEditText(Context context, AttributeSet attrs) {
        super(context, attrs);
        initialize(context, attrs, 0);
    }

    public ThemedEditText(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        initialize(context, attrs, defStyle);
    }

    private void initialize(final Context context, final AttributeSet attrs, final int defStyle) {
        // The theme can be null, particularly if we might be instantiating this
        // View in an IDE, with no ambient GeckoApplication.
        final Context applicationContext = context.getApplicationContext();
        if (applicationContext instanceof GeckoApplication) {
            theme = ((GeckoApplication) applicationContext).getLightweightTheme();
        }

        final TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.LightweightTheme);
        autoUpdateTheme = theme != null && a.getBoolean(R.styleable.LightweightTheme_autoUpdateTheme, true);
        a.recycle();
    }

    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();

        if (autoUpdateTheme)
            theme.addListener(this);
    }

    @Override
    public void onDetachedFromWindow() {
        super.onDetachedFromWindow();

        if (autoUpdateTheme)
            theme.removeListener(this);
    }

    @Override
    public int[] onCreateDrawableState(int extraSpace) {
        final int[] drawableState = super.onCreateDrawableState(extraSpace + 1);

        if (isPrivate)
            mergeDrawableStates(drawableState, STATE_PRIVATE_MODE);
        else if (isLight)
            mergeDrawableStates(drawableState, STATE_LIGHT);
        else if (isDark)
            mergeDrawableStates(drawableState, STATE_DARK);

        return drawableState;
    }

    @Override
    public void onLightweightThemeChanged() {
        if (autoUpdateTheme && theme.isEnabled())
            setTheme(theme.isLightTheme());
    }

    @Override
    public void onLightweightThemeReset() {
        if (autoUpdateTheme)
            resetTheme();
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        super.onLayout(changed, left, top, right, bottom);
        onLightweightThemeChanged();
    }

    public boolean isPrivateMode() {
        return isPrivate;
    }

    public void setPrivateMode(boolean isPrivate) {
        if (this.isPrivate != isPrivate) {
            this.isPrivate = isPrivate;
            refreshDrawableState();
            invalidate();
        }
    }

    public void setTheme(boolean isLight) {
        // Set the theme only if it is different from existing theme.
        if ((isLight && this.isLight != isLight) ||
            (!isLight && this.isDark == isLight)) {
            if (isLight) {
                this.isLight = true;
                this.isDark = false;
            } else {
                this.isLight = false;
                this.isDark = true;
            }

            refreshDrawableState();
            invalidate();
        }
    }

    public void resetTheme() {
        if (isLight || isDark) {
            isLight = false;
            isDark = false;
            refreshDrawableState();
            invalidate();
        }
    }

    public void setAutoUpdateTheme(boolean autoUpdateTheme) {
        if (theme == null) {
            return;
        }

        if (this.autoUpdateTheme != autoUpdateTheme) {
            this.autoUpdateTheme = autoUpdateTheme;

            if (autoUpdateTheme)
                theme.addListener(this);
            else
                theme.removeListener(this);
        }
    }

    public ColorDrawable getColorDrawable(int id) {
        return new ColorDrawable(ContextCompat.getColor(getContext(), id));
    }

    protected LightweightTheme getTheme() {
        return theme;
    }
}

package com.anland.termux;

import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Insets;
import android.os.Build;
import android.view.RoundedCorner;
import android.view.WindowInsets;

final class DisplayCutoutMode {
    static final String KEY = "hide_display_cutout";

    static final String HIDE_ALL = "hide_all";
    static final String HIDE_ROUNDED_CORNERS_ONLY = "rounded_corners_only";
    static final String HIDE_CUTOUT_ONLY = "cutout_only";
    static final String HIDE_NONE = "none";

    static final String[] VALUES = {
        HIDE_ALL, HIDE_ROUNDED_CORNERS_ONLY, HIDE_CUTOUT_ONLY, HIDE_NONE
    };

    private DisplayCutoutMode() {}

    static String get(SharedPreferences prefs) {
        Object stored = prefs.getAll().get(KEY);
        if (stored instanceof String && isValid((String) stored))
            return (String) stored;

        String mode = stored instanceof Boolean && !((Boolean) stored)
            ? HIDE_NONE
            : HIDE_ALL;
        if (stored != null)
            prefs.edit().putString(KEY, mode).apply();
        return mode;
    }

    private static boolean hidesCutout(String mode) {
        return HIDE_ALL.equals(mode) || HIDE_CUTOUT_ONLY.equals(mode);
    }

    private static boolean hidesRoundedCorners(String mode) {
        return HIDE_ALL.equals(mode) || HIDE_ROUNDED_CORNERS_ONLY.equals(mode);
    }

    static Insets safeInsets(String mode, WindowInsets insets, int orientation) {
        Insets cutout = hidesCutout(mode) ? Insets.NONE
            : insets.getInsetsIgnoringVisibility(WindowInsets.Type.displayCutout());
        if (hidesRoundedCorners(mode) || Build.VERSION.SDK_INT < Build.VERSION_CODES.S)
            return cutout;

        int topLeft = radius(insets, RoundedCorner.POSITION_TOP_LEFT);
        int topRight = radius(insets, RoundedCorner.POSITION_TOP_RIGHT);
        int bottomLeft = radius(insets, RoundedCorner.POSITION_BOTTOM_LEFT);
        int bottomRight = radius(insets, RoundedCorner.POSITION_BOTTOM_RIGHT);
        // In landscape, keep the full height of the rectangular desktop when
        // avoiding all cutouts. Keep the camera inset on whichever edge owns it.
        Insets corners = HIDE_NONE.equals(mode) && orientation == Configuration.ORIENTATION_LANDSCAPE
            ? Insets.of(Math.max(topLeft, bottomLeft), 0, Math.max(topRight, bottomRight), 0)
            : Insets.of(0, Math.max(topLeft, topRight), 0, Math.max(bottomLeft, bottomRight));
        return Insets.max(cutout, corners);
    }

    private static int radius(WindowInsets insets, int position) {
        RoundedCorner corner = insets.getRoundedCorner(position);
        return corner == null ? 0 : corner.getRadius();
    }

    private static boolean isValid(String mode) {
        for (String value : VALUES) {
            if (value.equals(mode))
                return true;
        }
        return false;
    }
}

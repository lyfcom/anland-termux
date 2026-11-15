package com.anland.termux;

import android.app.Activity;
import android.content.SharedPreferences;
import android.view.Display;
import android.view.Surface;
import android.view.WindowManager;

import java.util.Arrays;
import java.util.stream.IntStream;
import java.util.stream.Stream;

/** App-scoped refresh preference; the producer still follows the actual display rate. */
final class DisplayRefreshRate {
    static final String KEY = "preferred_refresh_rate";

    private DisplayRefreshRate() {}

    private static Stream<Display.Mode> supportedModes(Display display) {
        Display.Mode current = display.getMode();
        return Arrays.stream(display.getSupportedModes()).filter(mode ->
            mode.getPhysicalWidth() == current.getPhysicalWidth()
                && mode.getPhysicalHeight() == current.getPhysicalHeight());
    }

    static int[] supportedRates(Display display) {
        if (display == null) return new int[]{0};
        return IntStream.concat(IntStream.of(0), supportedModes(display)
            .mapToInt(mode -> Math.round(mode.getRefreshRate())).distinct().sorted()).toArray();
    }

    static void apply(Activity activity, SharedPreferences prefs, Surface surface) {
        int preference = prefs.getInt(KEY, 0);
        Display display = activity.getDisplay();
        float rate = display == null ? 0f : supportedModes(display)
            .filter(mode -> Math.round(mode.getRefreshRate()) == preference)
            .map(Display.Mode::getRefreshRate).findFirst().orElse(0f);
        WindowManager.LayoutParams params = activity.getWindow().getAttributes();
        if (Float.compare(params.preferredRefreshRate, rate) != 0) {
            params.preferredRefreshRate = rate;
            activity.getWindow().setAttributes(params);
        }
        if (surface.isValid())
            surface.setFrameRate(rate, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
    }
}

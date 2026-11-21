package com.anland.latencyprobe;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.hardware.HardwareBuffer;
import android.hardware.SyncFence;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Choreographer;
import android.view.Display;
import android.view.Surface;
import android.view.SurfaceControl;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowInsets;
import android.view.WindowManager;

import java.io.IOException;

public final class DisplayProbe extends Activity implements SurfaceHolder.Callback {
    private final Paint stripe = new Paint();
    private SurfaceView view;
    private Surface windowSurface;
    private SurfaceControl layer;
    private ImageReader reader;
    private int frame;
    private final Choreographer.FrameCallback vsync = this::drawWindowFrame;

    @Override
    public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Display display = getDisplay();
        Display.Mode current = display.getMode();
        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                && mode.getPhysicalHeight() == current.getPhysicalHeight()
                && Math.round(mode.getRefreshRate()) == 120) {
                WindowManager.LayoutParams params = getWindow().getAttributes();
                params.preferredDisplayModeId = mode.getModeId();
                params.preferredRefreshRate = mode.getRefreshRate();
                getWindow().setAttributes(params);
                break;
            }
        }
        stripe.setColor(Color.rgb(100, 145, 190));
        view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        setContentView(view);
        view.post(() -> view.getWindowInsetsController().hide(WindowInsets.Type.systemBars()));
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        stopDrawing();
        if (!getIntent().getBooleanExtra("direct", false)) {
            windowSurface = holder.getSurface();
            Choreographer.getInstance().postFrameCallback(vsync);
            return;
        }
        layer = new SurfaceControl.Builder()
                .setName("AnlandDisplayProbeDirect")
                .setParent(view.getSurfaceControl())
                .setOpaque(true)
                .setHidden(false)
                .build();
        reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 3,
                HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE | HardwareBuffer.USAGE_COMPOSER_OVERLAY);
        reader.setOnImageAvailableListener(this::submit, new Handler(Looper.getMainLooper()));
        draw(reader.getSurface());
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        stopDrawing();
    }

    private void drawWindowFrame(long time) {
        if (windowSurface != null) {
            draw(windowSurface);
            Choreographer.getInstance().postFrameCallback(vsync);
        }
    }

    private void draw(Surface surface) {
        Canvas canvas = surface.lockHardwareCanvas();
        try {
            float x = (frame++ * 8) % canvas.getWidth();
            canvas.drawColor(Color.DKGRAY);
            canvas.drawRect(x, 0, x + 80, canvas.getHeight(), stripe);
        } finally {
            surface.unlockCanvasAndPost(canvas);
        }
    }

    private void submit(ImageReader source) {
        if (reader != source) return;
        Image image = source.acquireNextImage();
        if (image == null) return;
        HardwareBuffer buffer = image.getHardwareBuffer();
        try (SyncFence ready = image.getFence();
             SurfaceControl.Transaction t = new SurfaceControl.Transaction()) {
            t.setBuffer(layer, buffer, ready, release -> {
                try (release; buffer; image) {
                    release.awaitForever();
                }
            }).addTransactionCommittedListener(getMainExecutor(), () -> {
                // A new frame is produced only after the previous one was latched.
                if (reader == source) draw(source.getSurface());
            }).apply();
        } catch (IOException e) {
            buffer.close();
            image.close();
            throw new IllegalStateException("Could not obtain the frame fence", e);
        }
    }

    private void stopDrawing() {
        Choreographer.getInstance().removeFrameCallback(vsync);
        windowSurface = null;
        if (reader != null) {
            reader.close();
            reader = null;
        }
        if (layer != null) {
            try (SurfaceControl.Transaction t = new SurfaceControl.Transaction()) {
                t.reparent(layer, null).apply();
            }
            layer.release();
            layer = null;
        }
    }
}

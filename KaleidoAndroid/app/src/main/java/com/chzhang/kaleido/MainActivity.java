package com.chzhang.kaleido;

import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.MotionEvent;
import android.view.KeyEvent;
import android.content.res.Configuration;
import androidx.annotation.NonNull;


import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("kaleido");
    }

    private native void nativeSetAssetManager(android.content.res.AssetManager mgr);
    private native void nativeInit(Surface surface);
    private native void nativeRender();
    private native void nativeDestroy();
    private native void nativeOnTouchEvent(int action, float x, float y, int pointerId);
    // Virtual sticks for movement (left) and looking (right).
    private native void nativeOnVirtualSticks(float moveX, float moveY, float lookX, float lookY);
    private native void nativeOnKeyEvent(int keyCode, boolean down);

    private SurfaceView surfaceView;
    private Thread renderThread;
    private volatile boolean running = false;

    // Touch state for dual virtual sticks.
    private int leftPointerId = -1;
    private int rightPointerId = -1;
    private float leftCenterX, leftCenterY;
    private float rightCenterX, rightCenterY;
    private float moveX, moveY; // left stick output [-1, 1]
    private float lookX, lookY; // right stick output [-1, 1]
    private static final float STICK_RADIUS = 200.0f; // pixels

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        surfaceView = new SurfaceView(this);
        setContentView(surfaceView);

        nativeSetAssetManager(getAssets());

        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                nativeInit(holder.getSurface());  // Surface to C++

                running = true;
                renderThread = new Thread(() -> {
                    while (running) {
                        nativeRender();
                    }
                });
                renderThread.start();
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                running = false;
                if (renderThread != null) {
                    try {
                        renderThread.join();
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    renderThread = null;
                }

            }

        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        nativeDestroy();
    }

    @Override
    public void onConfigurationChanged(@NonNull Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        final int index = event.getActionIndex();
        final int pointerCount = event.getPointerCount();

        // Still forward primary pointer to ImGui for UI interaction.
        if (pointerCount > 0) {
            nativeOnTouchEvent(action,
                    event.getX(0),
                    event.getY(0),
                    event.getPointerId(0));
        }

        int width = surfaceView.getWidth();
        float midX = width * 0.5f;

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int id = event.getPointerId(index);
                float x = event.getX(index);
                float y = event.getY(index);

                if (x < midX) {
                    if (leftPointerId == -1) {
                        leftPointerId = id;
                        leftCenterX = x;
                        leftCenterY = y;
                        moveX = 0.0f;
                        moveY = 0.0f;
                    }
                } else {
                    if (rightPointerId == -1) {
                        rightPointerId = id;
                        rightCenterX = x;
                        rightCenterY = y;
                        lookX = 0.0f;
                        lookY = 0.0f;
                    }
                }
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                // Update all active pointers.
                for (int i = 0; i < pointerCount; ++i) {
                    int id = event.getPointerId(i);
                    float x = event.getX(i);
                    float y = event.getY(i);

                    if (id == leftPointerId) {
                        float dx = x - leftCenterX;
                        float dy = y - leftCenterY;
                        float nx = dx / STICK_RADIUS;
                        float ny = dy / STICK_RADIUS;
                        float len = (float)Math.sqrt(nx * nx + ny * ny);
                        if (len > 1.0f) {
                            nx /= len;
                            ny /= len;
                        }
                        // Invert Y so that up is positive.
                        moveX = nx;
                        moveY = -ny;
                    } else if (id == rightPointerId) {
                        float dx = x - rightCenterX;
                        float dy = y - rightCenterY;
                        float nx = dx / STICK_RADIUS;
                        float ny = dy / STICK_RADIUS;
                        float len = (float)Math.sqrt(nx * nx + ny * ny);
                        if (len > 1.0f) {
                            nx /= len;
                            ny /= len;
                        }
                        // Right stick controls look; same mapping: up -> positive lookY.
                        lookX = nx;
                        lookY = -ny;
                    }
                }
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                int id = event.getPointerId(index);
                if (id == leftPointerId) {
                    leftPointerId = -1;
                    moveX = 0.0f;
                    moveY = 0.0f;
                }
                if (id == rightPointerId) {
                    rightPointerId = -1;
                    lookX = 0.0f;
                    lookY = 0.0f;
                }
                break;
            }
        }

        nativeOnVirtualSticks(moveX, moveY, lookX, lookY);
        return true;
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        nativeOnKeyEvent(keyCode, true);
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        nativeOnKeyEvent(keyCode, false);
        return super.onKeyUp(keyCode, event);
    }
}

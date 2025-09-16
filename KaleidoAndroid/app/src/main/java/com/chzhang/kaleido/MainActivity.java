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
    private native void nativeOnKeyEvent(int keyCode, boolean down);

    private SurfaceView surfaceView;
    private Thread renderThread;
    private volatile boolean running = false;

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
        nativeOnTouchEvent(event.getActionMasked(),
                (int)event.getX(),
                (int)event.getY(),
                event.getPointerId(0));
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

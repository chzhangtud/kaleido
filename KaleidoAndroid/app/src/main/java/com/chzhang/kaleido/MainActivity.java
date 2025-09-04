package com.chzhang.kaleido;

import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("kaleido");
    }

    private native void nativeSetAssetManager(android.content.res.AssetManager mgr);
    private native void nativeInit(Surface surface);
    private native void nativeRender();
    private native void nativeDestroy();

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
                        try {
                            Thread.sleep(16); // ~60fps
                        } catch (InterruptedException e) {
                            break;
                        }
                    }
                });
                renderThread.start();
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                // or use nativeResize(width, height);
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

                nativeDestroy();
            }
        });
    }
}

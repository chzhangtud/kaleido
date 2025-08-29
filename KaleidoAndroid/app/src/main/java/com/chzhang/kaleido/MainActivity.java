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

    private native void nativeInit(Surface surface);
    private native void nativeRender();
    private native void nativeDestroy();

    private SurfaceView surfaceView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        surfaceView = new SurfaceView(this);
        setContentView(surfaceView);

        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                nativeInit(holder.getSurface());  // Surface to C++
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                // or use nativeResize(width, height);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                nativeDestroy();
            }
        });

        // Create a new thread for nativeRender()
        new Thread(() -> {
            while (true) {
                nativeRender();
                try {
                    Thread.sleep(16); // ~60fps
                } catch (InterruptedException e) {
                    break;
                }
            }
        }).start();
    }
}

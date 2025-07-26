package com.otyazilim.toolkit;

import android.content.res.AssetManager;
import android.os.Bundle;
import org.libsdl.app.SDLActivity;
import android.content.pm.ApplicationInfo;

public class ToolKitActivity extends SDLActivity {
    private static native void load(AssetManager mgr); // Implemented in android_main.h

    public void loadAssetManagerToCpp(AssetManager mgr) {
        load(mgr);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        loadAssetManagerToCpp(getResources().getAssets());
    }

    @Override
    protected String[] getLibraries() {
        boolean isDebug = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        String sdl2LibName = isDebug ? "SDL2d" : "SDL2";

        return new String[] {
                sdl2LibName,
                "main"
        };
    }
}

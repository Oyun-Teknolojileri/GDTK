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
        // The Android build of SDL2 in Dependency/ carries no debug postfix (see the
        // CMAKE_DEBUG_POSTFIX "" in its CMake branch), so the library is always libSDL2.so.
        // Asking for libSDL2d.so in a debug build made loadLibraries fail before "main" was loaded,
        // which is what produced "No implementation found for ...ToolKitActivity.load".
        return new String[] {
                "SDL2",
                "main"
        };
    }
}

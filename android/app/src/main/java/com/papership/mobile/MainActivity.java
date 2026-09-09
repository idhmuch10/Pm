package com.papership.mobile;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * PaperShip Mobile entry point.
 *
 * SDL's activity creates the GL surface and runs the native game (SDL_main in port/Game.cpp)
 * on its own thread. This class adds what a phone needs around it:
 *  - copies the bundled data files (papership.o2r, gamecontrollerdb.txt) into the app's
 *    private external files directory, which is where libultraship looks for them,
 *  - imports the user's Paper Mario (USA) ROM through the system file picker,
 *  - hosts the on-screen controller overlay (TouchControlsView),
 *  - maps the back button to the in-game settings menu.
 *
 * The native side blocks in port_android_wait_for_setup() until nativeSetupDone() is called.
 */
public class MainActivity extends SDLActivity {
    private static final String TAG = "PaperShip";
    private static final String PREFS_NAME = "papership";
    private static final String PREF_TOUCH_VISIBLE = "touch_controls_visible";
    private static final int REQUEST_PICK_ROM = 0x5052;
    private static final String[] BUNDLED_FILES = { "papership.o2r", "gamecontrollerdb.txt" };

    private TouchControlsView mTouchControls;
    private volatile boolean mSetupDone = false;

    // --- JNI: implemented in port/android/AndroidPort.cpp ---
    public static native void nativeSetupDone(String romPath);
    public static native void nativeSetTouchButton(int mask, boolean down);
    public static native void nativeSetTouchStick(float x, float y);
    public static native void nativeToggleMenu();
    public static native boolean nativeIsMenuVisible();

    @Override
    protected String[] getLibraries() {
        // The last entry is the library SDLActivity looks SDL_main up in.
        return new String[] { "SDL2", "PaperShip" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || mLayout == null) {
            return; // SDLActivity already showed an error dialog
        }

        mTouchControls = new TouchControlsView(this);
        mTouchControls.setControlsVisible(getPrefs().getBoolean(PREF_TOUCH_VISIBLE, !isGamepadConnected()));
        mLayout.addView(mTouchControls, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        new Thread(() -> {
            copyBundledFiles();
            runOnUiThread(this::checkRom);
        }, "papership-setup").start();
    }

    SharedPreferences getPrefs() {
        return getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
    }

    boolean isSetupDone() {
        return mSetupDone;
    }

    void setTouchControlsVisiblePref(boolean visible) {
        getPrefs().edit().putBoolean(PREF_TOUCH_VISIBLE, visible).apply();
    }

    /** The directory SDL_AndroidGetExternalStoragePath() returns to the native side. */
    File getGameDataDir() {
        File dir = getExternalFilesDir(null);
        if (dir == null) {
            dir = getFilesDir();
        }
        if (!dir.exists() && !dir.mkdirs()) {
            Log.w(TAG, "Could not create data directory " + dir);
        }
        return dir;
    }

    /** Always refresh the bundled files: they are tiny and must match this build. */
    private void copyBundledFiles() {
        File dir = getGameDataDir();
        for (String name : BUNDLED_FILES) {
            File dest = new File(dir, name);
            try (InputStream in = getAssets().open(name); OutputStream out = new FileOutputStream(dest)) {
                byte[] buffer = new byte[64 * 1024];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                }
            } catch (IOException e) {
                Log.w(TAG, "Could not copy bundled file " + name, e);
            }
        }
        File mods = new File(dir, "mods");
        if (!mods.exists() && !mods.mkdirs()) {
            Log.w(TAG, "Could not create mods directory");
        }
    }

    private void checkRom() {
        File rom = new File(getGameDataDir(), RomImporter.ROM_FILE_NAME);
        if (RomImporter.isUsRom(rom)) {
            finishSetup(rom);
        } else {
            promptForRom(null);
        }
    }

    private void promptForRom(String error) {
        if (isFinishing()) {
            return;
        }
        String message = getString(R.string.rom_dialog_message);
        if (error != null) {
            message = getString(R.string.rom_import_failed, error) + "\n\n" + message;
        }
        new AlertDialog.Builder(this)
                .setTitle(R.string.rom_dialog_title)
                .setMessage(message)
                .setCancelable(false)
                .setPositiveButton(R.string.rom_dialog_select, (dialog, which) -> openRomPicker())
                .setNegativeButton(R.string.rom_dialog_quit, (dialog, which) -> finishAndRemoveTask())
                .show();
    }

    private void openRomPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, REQUEST_PICK_ROM);
        } catch (ActivityNotFoundException e) {
            promptForRom("no file picker is available on this device");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_ROM) {
            return;
        }
        Uri uri = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (uri == null) {
            promptForRom(null);
            return;
        }
        Toast.makeText(this, R.string.rom_importing, Toast.LENGTH_SHORT).show();
        File dest = new File(getGameDataDir(), RomImporter.ROM_FILE_NAME);
        new Thread(() -> {
            RomImporter.Result result = RomImporter.importRom(getContentResolver(), uri, dest);
            runOnUiThread(() -> {
                if (result.ok) {
                    finishSetup(dest);
                } else {
                    promptForRom(result.error);
                }
            });
        }, "papership-rom-import").start();
    }

    private void finishSetup(File rom) {
        if (mSetupDone) {
            return;
        }
        mSetupDone = true;
        Log.i(TAG, "Setup complete, ROM: " + rom.getAbsolutePath());
        nativeSetupDone(rom.getAbsolutePath());
    }

    /** The back button (or back gesture) opens and closes the settings menu. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP && mSetupDone) {
                nativeToggleMenu();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    private static boolean isGamepadConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null) {
                continue;
            }
            int sources = device.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
                return true;
            }
        }
        return false;
    }
}

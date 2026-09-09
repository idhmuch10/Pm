package com.papership.mobile;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentValues;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

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
    private static final String CRASH_LOG_NAME = "papership_crash.log";
    private static final String SESSION_LOG_NAME = "papership_log.txt";
    private static final String PREVIOUS_LOG_NAME = "papership_log_prev.txt";
    private static final String CLEAN_SHUTDOWN_MARKER = "=== PaperShip clean shutdown ===";
    private static final int REPORT_LOG_TAIL = 160 * 1024;
    private static final int REPORT_CRASH_LIMIT = 64 * 1024;

    private TouchControlsView mTouchControls;
    private volatile boolean mSetupDone = false;

    // --- JNI: implemented in port/android/AndroidPort.cpp ---
    public static native void nativeSetDataDir(String dir);
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
        // Keep the previous session's log so an unexpected exit can be reported, then let
        // the native side start a fresh papership_log.txt.
        boolean previousRunCrashed = rotateSessionLog();

        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || mLayout == null) {
            return; // SDLActivity already showed an error dialog
        }
        nativeSetDataDir(getGameDataDir().getAbsolutePath());
        applyImmersiveMode();

        mTouchControls = new TouchControlsView(this);
        mTouchControls.setControlsVisible(getPrefs().getBoolean(PREF_TOUCH_VISIBLE, !isGamepadConnected()));
        mLayout.addView(mTouchControls, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        new Thread(() -> {
            copyBundledFiles();
            runOnUiThread(() -> {
                offerCrashReport(previousRunCrashed);
                checkRom();
            });
        }, "papership-setup").start();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    /**
     * Fill the whole panel: hide the status and navigation bars and draw under the camera
     * cutout. Without this a black band stays visible along one edge in landscape.
     */
    private void applyImmersiveMode() {
        WindowManager.LayoutParams params = getWindow().getAttributes();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        getWindow().setAttributes(params);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    /** Move papership_log.txt to papership_log_prev.txt; true if the last run did not end cleanly. */
    private boolean rotateSessionLog() {
        File dir = getGameDataDir();
        File current = new File(dir, SESSION_LOG_NAME);
        File previous = new File(dir, PREVIOUS_LOG_NAME);
        if (!current.isFile()) {
            return false;
        }
        boolean crashed = !fileEndsWith(current, CLEAN_SHUTDOWN_MARKER);
        previous.delete();
        if (!current.renameTo(previous)) {
            current.delete();
        }
        return crashed;
    }

    private static boolean fileEndsWith(File file, String marker) {
        try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
            long length = raf.length();
            int tail = (int) Math.min(length, 512);
            raf.seek(length - tail);
            byte[] data = new byte[tail];
            raf.readFully(data);
            return new String(data, StandardCharsets.UTF_8).contains(marker);
        } catch (IOException e) {
            return false;
        }
    }

    /**
     * If the previous run crashed (papership_crash.log exists or the session log has no
     * clean-shutdown marker), offer the report: copy to the clipboard, save to Downloads
     * (visible in the Files app) or share it with another app.
     */
    private void offerCrashReport(boolean previousRunCrashed) {
        File crashLog = new File(getGameDataDir(), CRASH_LOG_NAME);
        boolean haveCrashLog = crashLog.isFile() && crashLog.length() > 0;
        if ((!haveCrashLog && !previousRunCrashed) || isFinishing()) {
            return;
        }
        final String report = buildReport(crashLog);
        new AlertDialog.Builder(this)
                .setTitle(R.string.crash_dialog_title)
                .setMessage(R.string.crash_dialog_message)
                .setPositiveButton(R.string.crash_dialog_copy, (dialog, which) -> {
                    copyToClipboard(report);
                    crashLog.delete();
                })
                .setNeutralButton(R.string.crash_dialog_save, (dialog, which) -> {
                    saveReportToDownloads(report);
                    crashLog.delete();
                })
                .setNegativeButton(R.string.crash_dialog_share, (dialog, which) -> {
                    shareReport(report);
                    crashLog.delete();
                })
                .setCancelable(true)
                .setOnCancelListener(dialog -> crashLog.delete())
                .show();
    }

    private String buildReport(File crashLog) {
        StringBuilder report = new StringBuilder();
        report.append("PaperShip Mobile report, ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(", Android ").append(Build.VERSION.RELEASE).append('\n');
        if (crashLog.isFile()) {
            report.append("\n----- papership_crash.log -----\n");
            report.append(readTail(crashLog, REPORT_CRASH_LIMIT));
        }
        File previous = new File(getGameDataDir(), PREVIOUS_LOG_NAME);
        if (previous.isFile()) {
            report.append("\n----- last session log (tail) -----\n");
            report.append(readTail(previous, REPORT_LOG_TAIL));
        }
        return report.toString();
    }

    private static String readTail(File file, int limit) {
        try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
            long length = raf.length();
            int size = (int) Math.min(length, limit);
            raf.seek(length - size);
            byte[] data = new byte[size];
            raf.readFully(data);
            String text = new String(data, StandardCharsets.UTF_8);
            return (size < length ? "[...]\n" : "") + text;
        } catch (IOException e) {
            return "(could not read " + file.getName() + ": " + e.getMessage() + ")\n";
        }
    }

    private void copyToClipboard(String report) {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (clipboard == null) {
            Toast.makeText(this, "Clipboard unavailable", Toast.LENGTH_LONG).show();
            return;
        }
        clipboard.setPrimaryClip(ClipData.newPlainText("PaperShip report", report));
        Toast.makeText(this, R.string.crash_report_copied, Toast.LENGTH_LONG).show();
    }

    private void saveReportToDownloads(String report) {
        String name = "papership_report_" + new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date()) + ".txt";
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                ContentValues values = new ContentValues();
                values.put(MediaStore.Downloads.DISPLAY_NAME, name);
                values.put(MediaStore.Downloads.MIME_TYPE, "text/plain");
                values.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
                Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
                if (uri == null) {
                    throw new IOException("MediaStore insert failed");
                }
                try (OutputStream out = getContentResolver().openOutputStream(uri)) {
                    out.write(report.getBytes(StandardCharsets.UTF_8));
                }
            } else {
                // Older Android: the app's own directory is browsable with any file manager.
                File dest = new File(getGameDataDir(), name);
                try (OutputStream out = new FileOutputStream(dest)) {
                    out.write(report.getBytes(StandardCharsets.UTF_8));
                }
                name = dest.getAbsolutePath();
            }
            Toast.makeText(this, getString(R.string.crash_report_saved, name), Toast.LENGTH_LONG).show();
        } catch (IOException | SecurityException e) {
            Toast.makeText(this, "Could not save report: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void shareReport(String report) {
        Intent share = new Intent(Intent.ACTION_SEND);
        share.setType("text/plain");
        share.putExtra(Intent.EXTRA_SUBJECT, "PaperShip Mobile crash report");
        share.putExtra(Intent.EXTRA_TEXT, report);
        try {
            startActivity(Intent.createChooser(share, getString(R.string.crash_dialog_share)));
        } catch (ActivityNotFoundException e) {
            Toast.makeText(this, "No app available to share the report", Toast.LENGTH_LONG).show();
        }
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

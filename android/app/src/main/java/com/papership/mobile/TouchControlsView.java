package com.papership.mobile;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * On-screen N64 controller overlay.
 *
 * Draws a translucent control stick, A/B, the four C buttons, Z, L, R, Start and a D-pad
 * and forwards their state to the native side (MainActivity.nativeSetTouchButton /
 * nativeSetTouchStick), where port/android/AndroidPort.cpp merges it into the pad the
 * game polls. Two pills at the top of the screen open the settings menu (MENU) and
 * hide or show the controls (PAD).
 *
 * While the overlay is visible it owns every touch, so a second finger can always press
 * a button. While the in-game settings menu is open the overlay hides itself and lets
 * touches through to SDL, so the menu can be operated by touch.
 */
public class TouchControlsView extends View {
    // N64 button bits as seen by the game (OSContPad.button).
    static final int BTN_A = 0x8000, BTN_B = 0x4000, BTN_Z = 0x2000, BTN_START = 0x1000;
    static final int BTN_DUP = 0x0800, BTN_DDOWN = 0x0400, BTN_DLEFT = 0x0200, BTN_DRIGHT = 0x0100;
    static final int BTN_L = 0x0020, BTN_R = 0x0010;
    static final int BTN_CUP = 0x0008, BTN_CDOWN = 0x0004, BTN_CLEFT = 0x0002, BTN_CRIGHT = 0x0001;

    private static final int COLOR_A = 0xFF2B6CD8;
    private static final int COLOR_B = 0xFF3DA35D;
    private static final int COLOR_C = 0xFFE3C22D;
    private static final int COLOR_START = 0xFFD0393A;
    private static final int COLOR_GREY = 0xFF6E6E6E;
    private static final int COLOR_PILL = 0xFF404040;

    private static final float HIT_SLOP = 1.35f;      // buttons accept touches slightly outside their circle
    private static final float STICK_GRAB = 1.6f;     // stick accepts touches within 1.6x its base radius
    private static final float STICK_DEADZONE = 0.08f;
    private static final long MENU_POLL_MS = 250;

    private static final class Control {
        final String label;
        final int mask;
        final int color;
        float cx, cy, rx, ry;   // centre and half extents (rx == ry for round buttons)
        boolean round = true;
        float textScale = 1.0f;
        int pointerId = -1;

        Control(String label, int mask, int color) {
            this.label = label;
            this.mask = mask;
            this.color = color;
        }

        void place(float cx, float cy, float rx, float ry, boolean round) {
            this.cx = cx;
            this.cy = cy;
            this.rx = rx;
            this.ry = ry;
            this.round = round;
        }

        boolean pressed() {
            return pointerId != -1;
        }

        boolean hit(float x, float y, float slop) {
            float dx = x - cx, dy = y - cy;
            if (round) {
                float r = rx * slop;
                return dx * dx + dy * dy <= r * r;
            }
            return Math.abs(dx) <= rx * slop && Math.abs(dy) <= ry * slop;
        }

        float distanceSq(float x, float y) {
            float dx = x - cx, dy = y - cy;
            return dx * dx + dy * dy;
        }
    }

    private final List<Control> mButtons = new ArrayList<>();
    private final Control mA = new Control("A", BTN_A, COLOR_A);
    private final Control mB = new Control("B", BTN_B, COLOR_B);
    private final Control mZ = new Control("Z", BTN_Z, COLOR_GREY);
    private final Control mStart = new Control("START", BTN_START, COLOR_START);
    private final Control mL = new Control("L", BTN_L, COLOR_GREY);
    private final Control mR = new Control("R", BTN_R, COLOR_GREY);
    private final Control mCUp = new Control("C▲", BTN_CUP, COLOR_C);
    private final Control mCDown = new Control("C▼", BTN_CDOWN, COLOR_C);
    private final Control mCLeft = new Control("C◀", BTN_CLEFT, COLOR_C);
    private final Control mCRight = new Control("C▶", BTN_CRIGHT, COLOR_C);
    private final Control mDUp = new Control("▲", BTN_DUP, COLOR_GREY);
    private final Control mDDown = new Control("▼", BTN_DDOWN, COLOR_GREY);
    private final Control mDLeft = new Control("◀", BTN_DLEFT, COLOR_GREY);
    private final Control mDRight = new Control("▶", BTN_DRIGHT, COLOR_GREY);
    private final Control mMenu = new Control("MENU", 0, COLOR_PILL);
    private final Control mToggle = new Control("PAD", 0, COLOR_PILL);

    // Control stick
    private float mStickCx, mStickCy, mStickR, mKnobR;
    private int mStickPointer = -1;
    private float mStickDx, mStickDy; // knob offset, each in [-1, 1]

    private boolean mControlsVisible = true;
    private boolean mMenuVisible = false;
    private float mAlpha = 0.55f;
    private float mLabelSize = 24f;

    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mRect = new RectF();

    private final Runnable mMenuPoll = new Runnable() {
        @Override
        public void run() {
            pollMenuState();
            postDelayed(this, MENU_POLL_MS);
        }
    };

    public TouchControlsView(Context context) {
        this(context, null);
    }

    public TouchControlsView(Context context, AttributeSet attrs) {
        super(context, attrs);
        mFill.setStyle(Paint.Style.FILL);
        mStroke.setStyle(Paint.Style.STROKE);
        mStroke.setColor(Color.WHITE);
        mText.setColor(Color.WHITE);
        mText.setTextAlign(Paint.Align.CENTER);
        mText.setFakeBoldText(true);

        mButtons.add(mA);
        mButtons.add(mB);
        mButtons.add(mZ);
        mButtons.add(mStart);
        mButtons.add(mL);
        mButtons.add(mR);
        mButtons.add(mCUp);
        mButtons.add(mCDown);
        mButtons.add(mCLeft);
        mButtons.add(mCRight);
        mButtons.add(mDUp);
        mButtons.add(mDDown);
        mButtons.add(mDLeft);
        mButtons.add(mDRight);
    }

    public void setControlsVisible(boolean visible) {
        if (mControlsVisible == visible) {
            return;
        }
        mControlsVisible = visible;
        if (!visible) {
            releaseAll();
        }
        invalidate();
    }

    public boolean areControlsVisible() {
        return mControlsVisible;
    }

    public void setOverlayAlpha(float alpha) {
        mAlpha = Math.max(0.1f, Math.min(1.0f, alpha));
        invalidate();
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        postDelayed(mMenuPoll, MENU_POLL_MS);
    }

    @Override
    protected void onDetachedFromWindow() {
        removeCallbacks(mMenuPoll);
        releaseAll();
        super.onDetachedFromWindow();
    }

    private MainActivity activity() {
        Context context = getContext();
        return context instanceof MainActivity ? (MainActivity) context : null;
    }

    private void pollMenuState() {
        MainActivity activity = activity();
        boolean visible = activity != null && activity.isSetupDone() && MainActivity.nativeIsMenuVisible();
        if (visible != mMenuVisible) {
            mMenuVisible = visible;
            if (visible) {
                releaseAll();
            }
            invalidate();
        }
    }

    // ------------------------------------------------------------------ layout

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        layoutControls(w, h);
    }

    private void layoutControls(int w, int h) {
        // Everything scales with a unit derived from the screen height (a 16:9 layout is
        // assumed; wider screens keep the same control sizes and gain empty space).
        float u = Math.min(h, w * 9f / 16f) / 24f;
        mLabelSize = 0.85f * u;
        mStroke.setStrokeWidth(Math.max(2f, 0.08f * u));

        // Control stick: left thumb.
        mStickCx = w * 0.16f;
        mStickCy = h * 0.60f;
        mStickR = 2.7f * u;
        mKnobR = 1.25f * u;

        // D-pad above the stick (rarely used by Paper Mario, kept small).
        float dcx = w * 0.16f, dcy = h * 0.24f, dr = 0.75f * u, doff = 1.35f * u;
        mDUp.place(dcx, dcy - doff, dr, dr, true);
        mDDown.place(dcx, dcy + doff, dr, dr, true);
        mDLeft.place(dcx - doff, dcy, dr, dr, true);
        mDRight.place(dcx + doff, dcy, dr, dr, true);

        // Face buttons: right thumb. B sits lower-left of A like on the N64 pad.
        mA.place(w * 0.905f, h * 0.60f, 1.7f * u, 1.7f * u, true);
        mB.place(w * 0.80f, h * 0.74f, 1.35f * u, 1.35f * u, true);

        // C buttons above the face buttons.
        float ccx = w * 0.87f, ccy = h * 0.28f, cr = 0.85f * u, coff = 1.55f * u;
        mCUp.place(ccx, ccy - coff, cr, cr, true);
        mCDown.place(ccx, ccy + coff, cr, cr, true);
        mCLeft.place(ccx - coff, ccy, cr, cr, true);
        mCRight.place(ccx + coff, ccy, cr, cr, true);

        // Z (trigger) to the left of the C cluster, Start at the bottom centre.
        mZ.place(w * 0.72f, h * 0.38f, 1.15f * u, 1.15f * u, true);
        mStart.place(w * 0.5f, h * 0.90f, 1.0f * u, 1.0f * u, true);

        // Shoulder buttons in the top corners.
        mL.place(w * 0.07f, h * 0.09f, 1.7f * u, 0.7f * u, false);
        mR.place(w * 0.93f, h * 0.09f, 1.7f * u, 0.7f * u, false);

        // Menu / pad-toggle pills at the top centre.
        mMenu.place(w * 0.44f, h * 0.055f, 1.6f * u, 0.55f * u, false);
        mToggle.place(w * 0.56f, h * 0.055f, 1.6f * u, 0.55f * u, false);
        mMenu.textScale = 0.7f;
        mToggle.textScale = 0.7f;
        for (Control c : new Control[] { mCUp, mCDown, mCLeft, mCRight, mDUp, mDDown, mDLeft, mDRight }) {
            c.textScale = 0.75f;
        }
        mStart.textScale = 0.55f;
        mL.textScale = 0.8f;
        mR.textScale = 0.8f;
    }

    // ------------------------------------------------------------------ drawing

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (mMenuVisible) {
            return;
        }
        drawControl(canvas, mToggle, mControlsVisible ? 1.0f : 0.6f);
        if (!mControlsVisible) {
            return;
        }
        drawControl(canvas, mMenu, 1.0f);
        drawStick(canvas);
        for (Control c : mButtons) {
            drawControl(canvas, c, 1.0f);
        }
    }

    private void drawStick(Canvas canvas) {
        int base = withAlpha(COLOR_GREY, mAlpha * 0.6f);
        mFill.setColor(base);
        canvas.drawCircle(mStickCx, mStickCy, mStickR, mFill);
        mStroke.setColor(withAlpha(Color.WHITE, mAlpha));
        canvas.drawCircle(mStickCx, mStickCy, mStickR, mStroke);

        float travel = mStickR - mKnobR * 0.6f;
        float kx = mStickCx + mStickDx * travel;
        float ky = mStickCy + mStickDy * travel;
        boolean active = mStickPointer != -1;
        mFill.setColor(withAlpha(active ? Color.WHITE : 0xFFBBBBBB, active ? 0.9f : mAlpha));
        canvas.drawCircle(kx, ky, mKnobR, mFill);
    }

    private void drawControl(Canvas canvas, Control c, float alphaScale) {
        float alpha = (c.pressed() ? 0.95f : mAlpha) * alphaScale;
        mFill.setColor(withAlpha(c.color, alpha));
        mStroke.setColor(withAlpha(Color.WHITE, alpha));
        if (c.round) {
            canvas.drawCircle(c.cx, c.cy, c.rx, mFill);
            canvas.drawCircle(c.cx, c.cy, c.rx, mStroke);
        } else {
            mRect.set(c.cx - c.rx, c.cy - c.ry, c.cx + c.rx, c.cy + c.ry);
            float corner = c.ry;
            canvas.drawRoundRect(mRect, corner, corner, mFill);
            canvas.drawRoundRect(mRect, corner, corner, mStroke);
        }
        mText.setColor(withAlpha(Color.WHITE, Math.min(1.0f, alpha + 0.3f)));
        mText.setTextSize(mLabelSize * c.textScale);
        float baseline = c.cy - (mText.descent() + mText.ascent()) / 2f;
        canvas.drawText(c.label, c.cx, baseline, mText);
    }

    private static int withAlpha(int color, float alpha) {
        int a = Math.round(Math.max(0f, Math.min(1f, alpha)) * 255f);
        return (a << 24) | (color & 0x00FFFFFF);
    }

    // ------------------------------------------------------------------ touch input

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (mMenuVisible) {
            return false; // let SDL/ImGui handle the settings menu
        }
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        int pointerId = event.getPointerId(index);

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                boolean handled = handleDown(pointerId, event.getX(index), event.getY(index));
                if (action == MotionEvent.ACTION_DOWN && !handled && !mControlsVisible) {
                    return false; // controls hidden: pass the gesture to SDL
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (mStickPointer != -1) {
                    for (int i = 0; i < event.getPointerCount(); i++) {
                        if (event.getPointerId(i) == mStickPointer) {
                            updateStick(event.getX(i), event.getY(i));
                        }
                    }
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_POINTER_UP:
                handleUp(pointerId);
                invalidate();
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                invalidate();
                return true;
            default:
                return super.onTouchEvent(event);
        }
    }

    private boolean handleDown(int pointerId, float x, float y) {
        MainActivity activity = activity();

        if (mToggle.hit(x, y, 1.3f)) {
            setControlsVisible(!mControlsVisible);
            if (activity != null) {
                activity.setTouchControlsVisiblePref(mControlsVisible);
            }
            return true;
        }
        if (!mControlsVisible) {
            return false;
        }
        if (mMenu.hit(x, y, 1.3f)) {
            if (activity != null && activity.isSetupDone()) {
                MainActivity.nativeToggleMenu();
            }
            return true;
        }

        float dx = x - mStickCx, dy = y - mStickCy;
        float grab = mStickR * STICK_GRAB;
        if (mStickPointer == -1 && dx * dx + dy * dy <= grab * grab) {
            mStickPointer = pointerId;
            updateStick(x, y);
            return true;
        }

        Control best = null;
        float bestDistance = Float.MAX_VALUE;
        for (Control c : mButtons) {
            if (c.pressed() || !c.hit(x, y, HIT_SLOP)) {
                continue;
            }
            float d = c.distanceSq(x, y);
            if (d < bestDistance) {
                bestDistance = d;
                best = c;
            }
        }
        if (best != null) {
            best.pointerId = pointerId;
            MainActivity.nativeSetTouchButton(best.mask, true);
            return true;
        }
        return false;
    }

    private void updateStick(float x, float y) {
        float dx = (x - mStickCx) / mStickR;
        float dy = (y - mStickCy) / mStickR;
        float len = (float) Math.sqrt(dx * dx + dy * dy);
        if (len > 1f) {
            dx /= len;
            dy /= len;
            len = 1f;
        }
        if (len < STICK_DEADZONE) {
            dx = 0f;
            dy = 0f;
        }
        mStickDx = dx;
        mStickDy = dy;
        MainActivity.nativeSetTouchStick(dx, -dy); // screen y grows downwards, N64 y grows upwards
    }

    private void handleUp(int pointerId) {
        if (pointerId == mStickPointer) {
            resetStick();
        }
        for (Control c : mButtons) {
            if (c.pointerId == pointerId) {
                c.pointerId = -1;
                MainActivity.nativeSetTouchButton(c.mask, false);
            }
        }
    }

    private void resetStick() {
        mStickPointer = -1;
        mStickDx = 0f;
        mStickDy = 0f;
        MainActivity.nativeSetTouchStick(0f, 0f);
    }

    private void releaseAll() {
        if (mStickPointer != -1) {
            resetStick();
        }
        for (Control c : mButtons) {
            if (c.pressed()) {
                c.pointerId = -1;
                MainActivity.nativeSetTouchButton(c.mask, false);
            }
        }
    }
}

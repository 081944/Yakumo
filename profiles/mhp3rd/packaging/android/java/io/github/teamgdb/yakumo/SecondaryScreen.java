package io.github.teamgdb.yakumo;

import android.app.Presentation;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Choreographer;
import android.view.Display;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

import java.nio.ByteBuffer;

/**
 * The lower screen of a two-screen handheld: the quest map and three item
 * shortcuts on the left, and the full item pouch on the right.
 *
 * <p>The renderer replays only Map-tagged HUD draws into an independent target.
 * This view displays that map bitmap; no main-screen scene pixels are copied.
 * Item slots mirror the live pouch; taps queue native use requests for the
 * guest thread, without pad input.
 *
 * <p>The native side lives in libmain.so, which SDLActivity has already
 * loaded, so the methods below bind to it without a loadLibrary call.
 */
public final class SecondaryScreen {
    private static final String TAG = "YakumoSecondary";

    private static Presentation sPresentation;
    private static PanelView sPanel;
    private static DisplayManager sDisplayManager;
    private static DisplayManager.DisplayListener sListener;

    private SecondaryScreen() {}

    /** Shows the panel on the first presentation display, if there is one. */
    public static void show(android.app.Activity activity) {
        if (sDisplayManager == null) {
            sDisplayManager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        }
        if (sListener == null) {
            sListener = new DisplayManager.DisplayListener() {
                @Override public void onDisplayAdded(int id) { show(activity); }
                @Override public void onDisplayRemoved(int id) {
                    if (sPresentation != null && sPresentation.getDisplay().getDisplayId() == id) hide();
                }
                @Override public void onDisplayChanged(int id) {}
            };
            sDisplayManager.registerDisplayListener(sListener, null);
        }
        Display[] displays = sDisplayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (displays.length == 0) {
            Log.i(TAG, "no second display");
            return;
        }
        Display target = displays[0];
        if (sPresentation != null && sPresentation.isShowing()
                && sPresentation.getDisplay().getDisplayId() == target.getDisplayId()) {
            return;
        }
        hide();
        try {
            sPresentation = new Presentation(activity, target);
            sPanel = new PanelView(activity, target);
            sPresentation.setContentView(sPanel);
            final PanelView panel = sPanel;
            sPresentation.setOnDismissListener(d -> { panel.stop(); Log.i(TAG, "dismissed"); });
            sPresentation.show();
            Log.i(TAG, "shown on display " + target.getDisplayId()
                    + " " + target.getMode().getPhysicalWidth() + "x" + target.getMode().getPhysicalHeight());
        } catch (Exception e) {
            Log.e(TAG, "cannot show on display " + target.getDisplayId(), e);
            if (sPanel != null) sPanel.stop();
            sPresentation = null;
            sPanel = null;
        }
    }

    /** Tears the panel down; the game keeps running on the main screen. */
    public static void hide() {
        if (sPanel != null) sPanel.stop();
        sPanel = null;
        if (sPresentation != null) {
            try {
                sPresentation.dismiss();
            } catch (Exception ignored) {
            }
            sPresentation = null;
        }
    }

    // Implemented in libmain.so (host/platform/android_secondary_screen.cpp).
    private static native ByteBuffer nativeMapBuffer(int width, int height);
    private static native boolean nativeMapSize(int[] size);
    private static native boolean nativeCopyMap();
    private static native void nativeMapActive(boolean active);
    private static native void nativeItem(int action);
    private static native String[] nativeItemState();
    private static native int[] nativeItemIcon(int icon, int color);
    private static native void nativeForceMap(boolean forced);

    /**
     * The panel: map and three item shortcuts on the left, full item pouch on the right. Sizes are all
     * fractions of the panel, so the same layout fits a 1240x1080 lower screen
     * and anything else a device reports.
     */
    private static final class PanelView extends View implements Choreographer.FrameCallback {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rect = new RectF();

        private Bitmap map; // sized to the map image the renderer sends
        private final ByteBuffer mapPixels;
        private final int[] mapSize = new int[2];
        private boolean running = true;
        // False when libmain.so has no bridge (a build without the lower
        // screen): the panel then shows its frame and nothing else.
        private boolean nativeReady = true;
        private long lastPreview;
        private long lastMarkerCheck;
        private boolean forceMap;
        private boolean traceMap;
        private int traceSamples, traceMissing, traceBlank;
        // Read from the game's selected pouch position; never predicted locally.
        private int selectedSlot = -1;
        private final SecondaryItemList itemList = new SecondaryItemList();
        private float rowStride, columnStride, cellWidth, cellHeight;
        private float downX, downY, lastY;
        private boolean dragging, listGesture;
        private int touchPointer = -1, pressedAction = Integer.MIN_VALUE;
        private final int touchSlop;
        private String[] itemState;

        /**
         * Debug hook for headless emulator testing: when a file named
         * "dump_lower" exists in the app's external files directory, render
         * this view into lower.png once a second. screencap cannot capture a
         * virtual presentation display on the emulator, so this is the only
         * way to inspect the lower screen without a visible window.
         */
        private void maybeSavePreview(long now) {
            if (now - lastPreview < 1_000_000_000L) return;
            lastPreview = now;
            java.io.File dir = getContext().getExternalFilesDir(null);
            if (dir == null || !new java.io.File(dir, "dump_lower").exists()) return;
            try {
                final int w = getWidth();
                final int h = getHeight();
                if (w <= 0 || h <= 0) return;
                Bitmap bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                draw(new Canvas(bitmap));
                try (java.io.FileOutputStream out =
                        new java.io.FileOutputStream(new java.io.File(dir, "lower.png"))) {
                    bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
                }
                bitmap.recycle();
            } catch (Throwable t) {
                Log.w(TAG, "preview failed: " + t);
            }
        }

        /**
         * Second debug hook for headless testing: a "force_map" marker makes the
         * renderer publish the map rectangle even outside a quest, so the whole
         * Vulkan readback -> JNI -> Bitmap path can be checked on the title
         * screen. Developers only; the marker is never present in normal use.
         */
        private void maybeUpdateForceMap(long now) {
            if (now - lastMarkerCheck < 1_000_000_000L) return;
            lastMarkerCheck = now;
            java.io.File dir = getContext().getExternalFilesDir(null);
            traceMap = dir != null && new java.io.File(dir, "trace_lower_map").exists();
            boolean wanted = dir != null && new java.io.File(dir, "force_map").exists();
            if (wanted == forceMap) return;
            forceMap = wanted;
            try {
                nativeForceMap(wanted);
                Log.i(TAG, "force_map " + wanted);
            } catch (Throwable t) {
                Log.w(TAG, "no force_map bridge: " + t);
            }
        }

        private final RectF mapPanel = new RectF();
        private final RectF itemShortcuts = new RectF();
        private final RectF[] shortcutButtons = {new RectF(), new RectF(), new RectF()};
        private final int[] shortcutItemSlots = {-1, -1, -1};
        private final RectF itemViewport = new RectF();
        private final RectF[] slots = new RectF[SecondaryItemList.CAPACITY];

        private static final int BG = Color.rgb(18, 14, 12);
        private static final int PANEL = Color.rgb(38, 29, 23);
        private static final int EDGE = Color.rgb(150, 116, 60);
        private static final int INK = Color.rgb(232, 214, 178);

        PanelView(Context context, Display display) {
            super(context);
            touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
            text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            setBackgroundColor(BG);
            int w = Math.max(1, display.getMode().getPhysicalWidth());
            int h = Math.max(1, display.getMode().getPhysicalHeight());
            ByteBuffer buffer = null;
            try {
                buffer = nativeMapBuffer(w, h);
            } catch (Throwable t) {
                nativeReady = false;
                Log.w(TAG, "no lower-screen bridge: " + t);
            }
            mapPixels = buffer;
            Choreographer.getInstance().postFrameCallback(this);
        }

        void stop() {
            running = false;
            if (nativeReady) nativeMapActive(false);
        }

        @Override
        public void doFrame(long frameTimeNanos) {
            if (!running) return;
            if (nativeReady) {
                try {
                    itemState = nativeItemState();
                    selectedSlot = itemState[0].isEmpty() ? -1 : Integer.parseInt(itemState[0]);
                    itemList.update(itemState, selectedSlot, touchPointer != -1);
                    invalidate();
                } catch (Throwable t) {
                    Log.w(TAG, "item snapshot unavailable", t);
                }
            }
            if (nativeReady && mapPixels != null && mapSize(mapSize) && mapSize[0] > 0 && mapSize[1] > 0) {
                if (map == null || map.getWidth() != mapSize[0] || map.getHeight() != mapSize[1]) {
                    if (map != null) map.recycle();
                    map = Bitmap.createBitmap(mapSize[0], mapSize[1], Bitmap.Config.ARGB_8888);
                }
                if (copyMap()) {
                    mapPixels.rewind();
                    map.copyPixelsFromBuffer(mapPixels);
                    invalidate();
                }
            } else if (map != null) {
                // No quest running: the native side has dropped the map. Show
                // the standby panel instead of a stale crop of a menu.
                map.recycle();
                map = null;
                invalidate();
            }
            maybeUpdateForceMap(frameTimeNanos);
            if (traceMap) {
                if (map == null) traceMissing++;
                else {
                    boolean ink = false;
                    for (int y = 0; y < map.getHeight() && !ink; y += 8)
                        for (int x = 0; x < map.getWidth() && !ink; x += 8)
                            ink = (map.getPixel(x, y) & 0xFFFFFF) != (PANEL & 0xFFFFFF);
                    if (!ink) traceBlank++;
                }
                if (++traceSamples == 60) {
                    Log.i(TAG, "map continuity samples=" + traceSamples + " missing=" + traceMissing + " blank=" + traceBlank);
                    traceSamples = traceMissing = traceBlank = 0;
                }
            } else traceSamples = traceMissing = traceBlank = 0;
            maybeSavePreview(frameTimeNanos);
            Choreographer.getInstance().postFrameCallbackDelayed(this, 66); // ~15 Hz is plenty for a map
        }

        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            super.onSizeChanged(w, h, oldw, oldh);
            float pad = h * 0.035f;
            float mapW = w * 0.60f - pad * 1.5f;
            float shortcutGap = pad * 0.45f;
            float shortcutHeight = Math.max(h * 0.12f, pad * 1.5f);
            float mapBottom = h - pad - shortcutGap - shortcutHeight;
            mapPanel.set(pad, pad * 0.65f, pad + mapW, mapBottom);
            itemShortcuts.set(pad, mapBottom + shortcutGap, pad + mapW, h - pad);
            float left = mapPanel.right + pad;
            float right = w - pad;
            float gap = (right - left) * 0.05f;
            itemViewport.set(left, mapPanel.top, right, h - pad);
            cellWidth = (itemViewport.width() - gap) / 2f;
            cellHeight = (itemViewport.height() - gap * 4f) / 5f;
            columnStride = cellWidth + gap;
            rowStride = cellHeight + gap;
            updateSlotBounds();
        }

        private void updateSlotBounds() {
            for (int i = 0; i < slots.length; ++i) {
                float x = itemViewport.left + (i % 2) * columnStride;
                float y = itemViewport.top + (i / 2 - itemList.offset()) * rowStride;
                if (slots[i] == null) slots[i] = new RectF();
                slots[i].set(x, y, x + cellWidth, y + cellHeight);
            }
        }

        @Override
        protected void onDraw(Canvas c) {
            super.onDraw(c);
            float radius = Math.min(mapPanel.width(), mapPanel.height()) * 0.02f;

            // Map: the game's own pixels, scaled into the panel.
            rect.set(mapPanel);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(PANEL);
            c.drawRoundRect(rect, radius, radius, paint);
            if (map != null) {
                paint.setFilterBitmap(true);
                // Centre the map at its own aspect instead of stretching it to
                // the tall panel; a stretched minimap is hard to read.
                float scale = Math.min(rect.width() / map.getWidth(), rect.height() / map.getHeight());
                float dw = map.getWidth() * scale, dh = map.getHeight() * scale;
                RectF fitted = new RectF(rect.centerX() - dw / 2f, rect.centerY() - dh / 2f,
                        rect.centerX() + dw / 2f, rect.centerY() + dh / 2f);
                c.drawBitmap(map, null, fitted, paint);
            } else {
                // The minimap is only drawn during a quest; outside one the
                // panel says so rather than showing a stale or empty rectangle.
                String standby = "地图待机 · 任务中显示";
                text.setColor(Color.rgb(150, 132, 106));
                float size = mapPanel.width() * 0.045f;
                text.setTextSize(size);
                float room = mapPanel.width() * 0.88f;
                if (text.measureText(standby) > room) {
                    size *= room / text.measureText(standby);
                    text.setTextSize(size);
                }
                c.drawText(standby, mapPanel.centerX() - text.measureText(standby) / 2f,
                        mapPanel.centerY() + size * 0.36f, text);
            }
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(radius * 0.2f);
            paint.setColor(EDGE);
            c.drawRoundRect(rect, radius, radius, paint);

            drawItemShortcuts(c);
            drawItems(c);
        }

        private void drawItemShortcuts(Canvas c) {
            for (int i = 0; i < shortcutItemSlots.length; ++i) shortcutItemSlots[i] = -1;
            int shortcut = 0;
            if (itemState != null) {
                for (int slot = 0; slot < SecondaryItemList.CAPACITY && shortcut < shortcutItemSlots.length; ++slot) {
                    if (slot + 1 >= itemState.length || itemState[slot + 1] == null
                            || itemState[slot + 1].isEmpty()) continue;
                    shortcutItemSlots[shortcut++] = slot;
                }
            }
            float gap = itemShortcuts.width() * 0.035f;
            float width = (itemShortcuts.width() - gap * 2f) / shortcutButtons.length;
            for (int i = 0; i < shortcutButtons.length; ++i) {
                RectF button = shortcutButtons[i];
                float left = itemShortcuts.left + i * (width + gap);
                button.set(left, itemShortcuts.top, left + width, itemShortcuts.bottom);
                float radius = button.height() * 0.16f;
                paint.setStyle(Paint.Style.FILL);
                int slot = shortcutItemSlots[i];
                boolean occupied = slot >= 0;
                boolean selected = occupied && slot == selectedSlot;
                boolean pressed = occupied && pressedAction == SecondaryItemActions.forSlot(slot);
                paint.setColor(pressed || selected ? Color.rgb(70, 56, 34) : Color.rgb(30, 24, 19));
                c.drawRoundRect(button, radius, radius, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(Math.max(2f, button.height() * 0.025f));
                paint.setColor(selected ? Color.rgb(226, 178, 84) : EDGE);
                c.drawRoundRect(button, radius, radius, paint);
                if (!occupied) continue;
                String[] fields = itemState[slot + 1].split("\t", 4);
                if (fields.length != 4) continue;
                String name = chineseItemName(fields[3]);
                drawItemIcon(c, button, Integer.parseInt(fields[1]), Integer.parseInt(fields[2]), name);
                text.setColor(INK);
                text.setTextSize(Math.min(button.width(), button.height()) * 0.14f);
                String count = "x" + fields[0];
                c.drawText(count, button.right - text.measureText(count) - button.width() * 0.08f,
                        button.top + button.height() * 0.20f, text);
                text.setTextSize(Math.min(button.width(), button.height()) * 0.16f);
                c.drawText(String.valueOf(slot + 1), button.left + button.width() * 0.08f,
                        button.top + button.height() * 0.20f, text);
            }
        }

        private void drawItems(Canvas c) {
            updateSlotBounds();
            c.save();
            c.clipRect(itemViewport);
            for (int i = 0; i < itemList.count(); ++i) {
                RectF slot = slots[i];
                if (slot == null || !RectF.intersects(slot, itemViewport)) continue;
                int pouchSlot = i;
                String value = itemState != null && pouchSlot + 1 < itemState.length ? itemState[pouchSlot + 1] : "";
                String[] fields = value == null ? new String[0] : value.split("\t", 4);
                boolean occupied = fields.length == 4;
                String name = occupied ? chineseItemName(fields[3]) : "\u7a7a";
                boolean selected = pouchSlot == selectedSlot;
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(selected ? Color.rgb(70, 56, 34) : Color.rgb(30, 24, 19));
                c.drawRoundRect(slot, slot.width() * 0.1f, slot.width() * 0.1f, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(slot.width() * (selected ? 0.05f : 0.025f));
                paint.setColor(selected ? Color.rgb(226, 178, 84) : EDGE);
                c.drawRoundRect(slot, slot.width() * 0.1f, slot.width() * 0.1f, paint);

                if (occupied) {
                    drawItemIcon(c, slot, Integer.parseInt(fields[1]), Integer.parseInt(fields[2]), name);
                    text.setColor(INK);
                    text.setTextSize(Math.min(slot.width(), slot.height()) * 0.16f);
                    String count = "x" + fields[0];
                    c.drawText(count, slot.right - text.measureText(count) - slot.width() * 0.08f,
                            slot.top + slot.height() * 0.22f, text);
                }
                text.setColor(INK);
                float size = Math.min(slot.width(), slot.height()) * 0.2f;
                text.setTextSize(size);
                float room = slot.width() * 0.86f;
                if (text.measureText(name) > room) {
                    size *= room / text.measureText(name);
                    text.setTextSize(size);
                }
                c.drawText(name, slot.centerX() - text.measureText(name) / 2f,
                        slot.top + slot.height() * 0.80f, text);

                text.setColor(Color.rgb(150, 132, 106));
                text.setTextSize(slot.width() * 0.14f);
                c.drawText(String.valueOf(pouchSlot + 1), slot.left + slot.width() * 0.08f,
                        slot.top + slot.height() * 0.2f, text);
            }
            c.restore();
            if (itemList.maxOffset() > 0) {
                float totalRows = itemList.maxOffset() + SecondaryItemList.VISIBLE_ROWS;
                float thumb = itemViewport.height() * SecondaryItemList.VISIBLE_ROWS / totalRows;
                float top = itemViewport.top + (itemViewport.height() - thumb)
                        * itemList.offset() / itemList.maxOffset();
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(EDGE);
                float width = Math.max(3f, getWidth() * 0.003f);
                c.drawRoundRect(itemViewport.right + width, top, itemViewport.right + width * 2,
                        top + thumb, width, width, paint);
            }
        }

        // Labels only; bitmap icons come from the game's live atlas and palettes.
        private static String chineseItemName(String name) {
            String[][] names = {
                {"\u56de\u5fa9\u85ac\u30b0\u30ec\u30fc\u30c8", "\u56de\u590d\u836f\u00b7\u5927"},
                {"\u56de\u5fa9\u85acG", "\u56de\u590d\u836f\u00b7\u5927"},
                {"\u56de\u5fa9\u85ac", "\u56de\u590d\u836f"},
                {"\u643a\u5e2f\u98df\u6599", "\u643a\u5e26\u98df\u6599"},
                {"\u643a\u5e2f\u783a\u77f3", "\u643a\u5e26\u7825\u77f3"},
                {"\u783a\u77f3", "\u7825\u77f3"},
                {"\u30da\u30a4\u30f3\u30c8\u30dc\u30fc\u30eb", "\u67d3\u8272\u7389"},
                {"\u30da\u30a4\u30f3\u30c8", "\u67d3\u8272\u7389"},
                {"\u9583\u5149\u7389", "\u95ea\u5149\u7389"},
                {"\u843d\u3068\u3057\u7a74", "\u843d\u7a74\u9677\u9631"},
                {"\u30b7\u30d3\u30ec\u7f60", "\u9ebb\u75f9\u9677\u9631"},
                {"\u3053\u3093\u304c\u308a\u8089", "\u70e4\u719f\u8089"},
                {"\u751f\u713c\u3051\u8089", "\u534a\u719f\u8089"},
                {"\u30db\u30c3\u30c8\u30c9\u30ea\u30f3\u30af", "\u70ed\u996e"},
                {"\u30af\u30fc\u30e9\u30fc\u30c9\u30ea\u30f3\u30af", "\u51b7\u996e"},
                {"\u30a6\u30c1\u30b1\u30b7\u306e\u5b9f", "\u6d88\u9664\u679c"},
                {"\u30e2\u30c9\u30ea\u7389", "\u56de\u5bb6\u7389"},
                {"\u3053\u3084\u3057\u7389", "\u5f02\u81ed\u7389"},
                {"\u30da\u30a4\u30f3\u30c8\u30dc\u30fc\u30eb", "\u67d3\u8272\u7389"},
                {"\u30d4\u30c3\u30b1\u30eb", "\u5341\u5b57\u9550"},
                {"\u866b\u3042\u307f", "\u6355\u866b\u7f51"},
                {"\u3051\u3080\u308a\u7389", "\u70df\u96fe\u7389"},
                {"\u6bd2\u3051\u3080\u308a\u7389", "\u6bd2\u70df\u96fe\u7389"},
                {"\u5927\u30bf\u30eb\u7206\u5f3e", "\u5927\u6876\u7206\u5f39"},
                {"\u5c0f\u30bf\u30eb\u7206\u5f3e", "\u5c0f\u6876\u7206\u5f39"},
                {"\u6355\u7372\u7528\u9ebb\u9154\u7389", "\u6355\u83b7\u7528\u9ebb\u9189\u7389"},
                {"\u652f\u7d66\u5c02\u7528", "\u652f\u7ed9\u4e13\u7528"},
                {"\u5fdc\u6025\u85ac", "\u5e94\u6025\u836f"},
                {"\u89e3\u6bd2\u85ac", "\u89e3\u6bd2\u836f"},
                {"\u79d8\u85ac", "\u79d8\u836f"},
                {"\u3044\u306b\u3057\u3048\u306e\u79d8\u85ac", "\u8fdc\u53e4\u79d8\u836f"},
                {"\u6f22\u65b9\u85ac", "\u6c49\u65b9\u836f"},
                {"\u30da\u30a4\u30f3\u30c8\u5f3e", "\u67d3\u8272\u5f39"},
                {"\u9b3c\u4eba\u85ac", "\u9b3c\u4eba\u836f"},
                {"\u786c\u5316\u85ac", "\u786c\u5316\u836f"},
                {"\u5143\u6c17\u30c9\u30ea\u30f3\u30af", "\u5143\u6c14\u996e\u6599"}
            };
            for (String[] pair : names) name = name.replace(pair[0], pair[1]);
            return name;
        }

        private final java.util.HashMap<Integer, Bitmap> originalIcons = new java.util.HashMap<>();

        private void drawItemIcon(Canvas c, RectF slot, int icon, int color, String name) {
            int key = icon * 256 + color;
            Bitmap bitmap = originalIcons.get(key);
            if (bitmap == null) {
                int[] pixels = nativeItemIcon(icon, color);
                if (pixels == null || pixels.length < 3) return;
                int width = pixels[0], height = pixels[1];
                if (width <= 0 || height <= 0 || width > 64 || height > 64 || pixels.length != 2 + width * height) return;
                bitmap = Bitmap.createBitmap(pixels, 2, width, width, height, Bitmap.Config.ARGB_8888);
                originalIcons.put(key, bitmap);
            }
            float size = Math.min(slot.width(), slot.height()) * 0.42f;
            float height = size * bitmap.getHeight() / bitmap.getWidth();
            RectF destination = new RectF(slot.centerX() - size / 2f, slot.top + slot.height() * 0.25f,
                    slot.centerX() + size / 2f, slot.top + slot.height() * 0.25f + height);
            paint.setFilterBitmap(false);
            paint.setStyle(Paint.Style.FILL);
            c.drawBitmap(bitmap, null, destination, paint);
        }
        @Override
        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                touchPointer = event.getPointerId(0);
                downX = event.getX();
                downY = lastY = event.getY();
                dragging = false;
                listGesture = itemViewport.contains(downX, downY);
                pressedAction = actionAt(downX, downY);
                return true;
            }
            if (action == MotionEvent.ACTION_CANCEL || action == MotionEvent.ACTION_POINTER_DOWN) {
                touchPointer = -1;
                pressedAction = Integer.MIN_VALUE;
                return true;
            }
            int pointer = event.findPointerIndex(touchPointer);
            if (pointer < 0) return true;
            float x = event.getX(pointer), y = event.getY(pointer);
            if (action == MotionEvent.ACTION_MOVE) {
                if (!dragging && Math.hypot(x - downX, y - downY) > touchSlop) {
                    dragging = true;
                    pressedAction = Integer.MIN_VALUE;
                    lastY = downY;
                }
                if (dragging && listGesture && rowStride > 0) {
                    itemList.scroll((lastY - y) / rowStride);
                    lastY = y;
                    invalidate();
                }
            } else if (action == MotionEvent.ACTION_UP) {
                if (!dragging && Math.hypot(x - downX, y - downY) <= touchSlop
                        && pressedAction != Integer.MIN_VALUE && actionAt(x, y) == pressedAction)
                    sendItem(pressedAction);
                touchPointer = -1;
                pressedAction = Integer.MIN_VALUE;
            }
            return true;
        }

        private int actionAt(float x, float y) {
            for (int i = 0; i < shortcutButtons.length; ++i) {
                if (shortcutButtons[i].contains(x, y) && shortcutItemSlots[i] >= 0)
                    return SecondaryItemActions.forSlot(shortcutItemSlots[i]);
            }
            if (itemViewport.contains(x, y) && rowStride > 0 && columnStride > 0) {
                int slot = itemList.hit((x - itemViewport.left) / columnStride,
                        (y - itemViewport.top) / rowStride);
                updateSlotBounds();
                if (slot >= 0 && slots[slot].contains(x, y) && itemState != null
                        && slot + 1 < itemState.length && itemState[slot + 1] != null
                        && !itemState[slot + 1].isEmpty()) return SecondaryItemActions.forSlot(slot);
            }
            return Integer.MIN_VALUE;
        }

        private boolean mapSize(int[] size) {
            try {
                return nativeMapSize(size);
            } catch (Throwable t) {
                nativeReady = false;
                return false;
            }
        }

        private boolean copyMap() {
            try {
                return nativeCopyMap();
            } catch (Throwable t) {
                nativeReady = false;
                return false;
            }
        }

        private void sendItem(int action) {
            Log.d(TAG, "item " + action);
            invalidate();
            try {
                nativeItem(action);
            } catch (Throwable t) {
                nativeReady = false;
            }
        }
    }
}

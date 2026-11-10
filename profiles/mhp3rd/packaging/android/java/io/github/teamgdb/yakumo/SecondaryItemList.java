package io.github.teamgdb.yakumo;

/** Scroll state in rows, independent of display density and Android widgets. */
final class SecondaryItemList {
    static final int COLUMNS = 2;
    static final int VISIBLE_ROWS = 5;
    static final int CAPACITY = 32;
    private int count = 10;
    private int selected = -1;
    private float offset;

    void update(String[] items, int nextSelected, boolean dragging) {
        count = 10;
        if (items != null)
            for (int i = 1; i < Math.min(items.length, CAPACITY + 1); ++i)
                if (items[i] != null && !items[i].isEmpty()) count = Math.max(count, i);
        scroll(0);
        if (nextSelected != selected && nextSelected >= 0 && nextSelected < count && !dragging) {
            int row = nextSelected / COLUMNS;
            if (row < offset) offset = row;
            else if (row + 1 > offset + VISIBLE_ROWS) offset = row + 1 - VISIBLE_ROWS;
        }
        selected = nextSelected;
    }

    void scroll(float rows) {
        if (!Float.isFinite(rows)) return;
        offset = Math.max(0, Math.min(maxOffset(), offset + rows));
    }

    int count() { return count; }
    float offset() { return offset; }
    int maxOffset() { return (count + COLUMNS - 1) / COLUMNS - VISIBLE_ROWS; }

    int hit(float column, float row) {
        if (!Float.isFinite(column) || !Float.isFinite(row)
                || column < 0 || column >= COLUMNS || row < 0 || row >= VISIBLE_ROWS) return -1;
        float contentRow = row + offset;
        int slot = (int) contentRow * COLUMNS + (int) column;
        return slot < count ? slot : -1;
    }
}

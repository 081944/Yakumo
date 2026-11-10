package io.github.teamgdb.yakumo;

import org.junit.Test;
import static org.junit.Assert.*;

public class SecondaryItemListTest {
    @Test public void lowerScreenShortcutUsesThePouchSlot() {
        assertEquals(100, SecondaryItemActions.forSlot(0));
        assertEquals(131, SecondaryItemActions.forSlot(31));
        assertEquals(Integer.MIN_VALUE, SecondaryItemActions.forSlot(-1));
        assertEquals(Integer.MIN_VALUE, SecondaryItemActions.forSlot(32));
    }

    private String[] pouch(int count) {
        String[] items = new String[count + 1];
        for (int i = 1; i <= count; ++i) items[i] = "1\t1\t1\titem";
        return items;
    }

    @Test public void fiveRowsWithoutOverflowAndInvalidContacts() {
        SecondaryItemList list = new SecondaryItemList();
        list.update(null, -1, false);
        list.scroll(100);
        assertEquals(0, list.offset(), 0);
        assertEquals(10, list.count());
        for (int slot = 0; slot < 10; ++slot)
            assertEquals(slot, list.hit(slot % 2 + 0.5f, slot / 2 + 0.5f));
        assertEquals(-1, list.hit(2, 0));
        assertEquals(-1, list.hit(-1, 0));
        assertEquals(-1, list.hit(0, 5));
        assertEquals(-1, list.hit(0, -1));
        assertEquals(-1, list.hit(Float.NaN, 0));
        assertEquals(-1, list.hit(0, Float.POSITIVE_INFINITY));
        list.scroll(Float.NaN);
        assertEquals(0, list.offset(), 0);
    }

    @Test public void scrollReachesEverySlotWithoutSnapshotSnapBack() {
        SecondaryItemList list = new SecondaryItemList();
        String[] items = pouch(32);
        list.update(items, 0, false);
        for (int topRow = 0; topRow <= 11; ++topRow) {
            list.scroll(topRow - list.offset());
            list.update(items, 0, false);
            assertEquals(topRow, list.offset(), 0);
            for (int i = 0; i < 10; ++i)
                assertEquals(topRow * 2 + i, list.hit(i % 2 + 0.5f, i / 2 + 0.5f));
        }
        list.scroll(1000);
        assertEquals(11, list.offset(), 0);
        list.scroll(-1000);
        assertEquals(0, list.offset(), 0);
        list.scroll(0.75f);
        assertEquals(2, list.hit(0.5f, 0.5f));
        list.update(items, 31, false);
        assertEquals(11, list.offset(), 0);
        list.update(items, 0, false);
        assertEquals(0, list.offset(), 0);
    }

    @Test public void oddCountsShrinkHolesAndDraggingSelection() {
        SecondaryItemList list = new SecondaryItemList();
        String[] items = pouch(11);
        items[4] = null;
        items[5] = "";
        list.update(items, 0, false);
        list.scroll(10);
        assertEquals(1, list.offset(), 0);
        assertEquals(10, list.hit(0.5f, 4.5f));
        assertEquals(-1, list.hit(1.5f, 4.5f));
        list.update(pouch(32), 31, true);
        assertEquals(1, list.offset(), 0);
        list.update(pouch(32), -1, false);
        assertEquals(1, list.offset(), 0);
        list.update(pouch(32), 32, false);
        assertEquals(1, list.offset(), 0);
        list.scroll(20);
        list.update(new String[1], -1, false);
        assertEquals(0, list.offset(), 0);
        list.update(pouch(40), 31, false);
        assertEquals(32, list.count());
        assertEquals(11, list.offset(), 0);
    }
}

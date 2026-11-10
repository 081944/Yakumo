package io.github.teamgdb.yakumo;

/** Native request codes shared by the lower-screen item controls. */
final class SecondaryItemActions {
    static final int SLOT_BASE = 100;

    private SecondaryItemActions() {}

    static int forSlot(int slot) {
        return slot >= 0 && slot < SecondaryItemList.CAPACITY
                ? SLOT_BASE + slot : Integer.MIN_VALUE;
    }
}

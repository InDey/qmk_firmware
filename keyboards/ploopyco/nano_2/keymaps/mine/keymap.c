/* Copyright 2021 Colin Lam (Ploopy Corporation)
 * Copyright 2020 Christopher Courtney, aka Drashna Jael're  (@drashna) <drashna@live.com>
 * Copyright 2019 Sunjun Kim
 * Copyright 2019 Hiroyuki Okada
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H

/* Single-button behavior:
 *   tap        -> cycle DPI
 *   hold       -> temporarily switch to the other mode (move <-> scroll) while held
 *   double tap -> toggle the base mode between move and scroll
 *
 * Lock keys on the host keyboard (seen through the LED state the host sends):
 *   double tap Num Lock    -> toggle the base mode between move and scroll
 *   double tap Scroll Lock -> cycle DPI
 */

#ifndef LOCK_DOUBLE_TAP_TERM
#    define LOCK_DOUBLE_TAP_TERM 500 // ms between the two lock changes; the host round trip adds latency
#endif

enum { MULTI_BTN = SAFE_RANGE };

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT( MULTI_BTN )
};

extern bool is_drag_scroll; // defined in ploopyco.c, drives scroll vs. move

static bool     scroll_mode    = false; // base mode, toggled by double tap
static bool     tap_pending    = false; // first tap seen, waiting to see if a second one follows
static bool     ignore_release = false; // release belongs to a double tap, skip tap handling
static uint16_t press_timer;
static uint16_t release_timer;

// Flip the base mode; flipping is_drag_scroll too keeps an in-progress hold inverted
static void toggle_scroll_mode(void) {
    scroll_mode    = !scroll_mode;
    is_drag_scroll = !is_drag_scroll;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode != MULTI_BTN) {
        return true;
    }

    if (record->event.pressed) {
        if (tap_pending && timer_elapsed(release_timer) < TAPPING_TERM) {
            // Second tap: toggle the base mode
            tap_pending    = false;
            ignore_release = true;
            toggle_scroll_mode();
        } else {
            // Start of a tap or hold: switch to the other mode right away so holds feel instant
            press_timer    = timer_read();
            is_drag_scroll = !scroll_mode;
        }
    } else {
        is_drag_scroll = scroll_mode;
        if (ignore_release) {
            ignore_release = false;
        } else if (timer_elapsed(press_timer) < TAPPING_TERM) {
            // Short press: wait to see whether it becomes a double tap
            tap_pending   = true;
            release_timer = timer_read();
        }
    }
    return false;
}

void housekeeping_task_user(void) {
    // No second tap arrived in time, so it was a single tap
    if (tap_pending && timer_elapsed(release_timer) >= TAPPING_TERM) {
        tap_pending = false;
        cycle_dpi();
    }
}

typedef struct {
    bool     last_state; // starts false to match QMK's initial LED state
    bool     change_pending;
    uint32_t change_timer;
} lock_tracker_t;

// Returns true when the lock state changed twice within LOCK_DOUBLE_TAP_TERM (a double tap).
// The host's first report after plugging in counts as a single change, so it never triggers.
static bool lock_double_tapped(lock_tracker_t *tracker, bool state) {
    if (state == tracker->last_state) {
        return false;
    }
    tracker->last_state = state;

    if (tracker->change_pending && timer_elapsed32(tracker->change_timer) < LOCK_DOUBLE_TAP_TERM) {
        tracker->change_pending = false;
        return true;
    }
    tracker->change_pending = true;
    tracker->change_timer   = timer_read32();
    return false;
}

bool led_update_user(led_t led_state) {
    static lock_tracker_t num_lock    = {0};
    static lock_tracker_t scroll_lock = {0};

    if (lock_double_tapped(&num_lock, led_state.num_lock)) {
        toggle_scroll_mode();
    }
    if (lock_double_tapped(&scroll_lock, led_state.scroll_lock)) {
        cycle_dpi();
    }
    return true;
}

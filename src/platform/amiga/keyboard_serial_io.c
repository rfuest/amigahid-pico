/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * amiga keyboard serial interface.
 */

#include "keyboard_serial_io.h"
#include "keyboard.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

// default pins for amiga keyboard communication: GP11 for reset, GP12 for clock, GP13 for data
#ifndef PIN_AMIGA_DAT
#  define PIN_AMIGA_DAT 13  // pin 17, GP13
#endif
#ifndef PIN_AMIGA_CLK
#  define PIN_AMIGA_CLK 12  // pin 16, GP12
#endif
#ifndef PIN_AMIGA_RST
#  define PIN_AMIGA_RST 11  // pin 15, GP11
#endif

enum SYNC_STATE { IDLE, SYNC };
// don't optimise variables hit by the timer isr (timer callback?)
volatile uint8_t sync_state = IDLE;
volatile bool clock_timer_fired = false;

// _.-._.-._ @todo i've not been doing sync correctly for sooooooo long; fix/remove? -._.-._.-
int64_t sync_timer_cb(alarm_id_t id, void *user_data)
{
    clock_timer_fired = true;
    gpio_put(PIN_AMIGA_DAT, 0);
    sync_state = SYNC;
}

void amiga_init()
{
    // setup digital mode, direction and active high/low on /clk, /dat and /rst.
    gpio_init(PIN_AMIGA_DAT);
    gpio_init(PIN_AMIGA_CLK);
    gpio_init(PIN_AMIGA_RST);

    // default direction is out; for keycode acknowledgement, we change /dat to input for 5ms after sending a code,
    // then back to output. amigahid for the avr did nothing with this; we _should_ be checking that the amiga
    // has acked the code, but hey, we're not a 6502, and if the comms are ropey you'll notice rather than rely
    // on error recovery in the keyboard mcu (which is us, actually, in this case).
    gpio_set_dir(PIN_AMIGA_DAT, GPIO_OUT);
    gpio_set_dir(PIN_AMIGA_CLK, GPIO_OUT);
    gpio_set_dir(PIN_AMIGA_RST, GPIO_OUT);

    // all pins are active low, meaning if /rst is current at 0, the amiga is held in reset.
    // rectify this by putting all pins high. this should bring the amiga to boot.
    gpio_put(PIN_AMIGA_DAT, 1);
    gpio_put(PIN_AMIGA_CLK, 1);
    gpio_put(PIN_AMIGA_RST, 1);

    // now the pins are setup, setup the timer callback to maintain keyboard comms in sync.
    // @todo add_alarm_in_ms() here


    // wait a full second then send initpower, pause 200ms and then termpower. unlike the amiga kbd 6502, we're
    // not doing anything during this time, so it's just so the computer is happy in the knowledge that we are
    // here.
    sleep_ms(1000);
    amiga_send(AMIGA_INITPOWER, false);
    sleep_ms(200);
    amiga_send(AMIGA_TERMPOWER, false);

    // fin.
}

void amiga_send(uint8_t keycode, bool up)
{
    uint8_t bit_position, bit_mask = 0x80, sendcode;

    if (keycode == AMIGA_UNKNOWN)
        return; // cowardly refuse to send an unknown scancode to the amiga

    // copy input code, roll left, move msb to lsb
    sendcode = keycode | (up == true ? 0x80 : 0x00);
    sendcode <<= 1;
    if (keycode & 0x80)
        sendcode |= 1;

    for (bit_position = 0; bit_position < 8; bit_position++) {
        if (sendcode & bit_mask)
            gpio_put(PIN_AMIGA_DAT, 0);
        else
            gpio_put(PIN_AMIGA_DAT, 1);

        // hold /dat for 20us before pulsing /clk, then wait 50us before next bit
        sleep_us(20);
        gpio_put(PIN_AMIGA_CLK, 0);
        sleep_us(20);
        gpio_put(PIN_AMIGA_CLK, 1);
        sleep_us(50); // @todo should be 20?

        // shift the bit pattern for next iteration
        bit_mask >>= 1;
    }

    // set /dat to input for 5ms to signal end of key
    gpio_put(PIN_AMIGA_DAT, 1);
    gpio_set_dir(PIN_AMIGA_DAT, GPIO_IN);
    sleep_ms(5);
    gpio_set_dir(PIN_AMIGA_DAT, GPIO_OUT);
}

void amiga_assert_reset()
{
    gpio_put(PIN_AMIGA_RST, 0);
}

void amiga_release_reset()
{
    gpio_put(PIN_AMIGA_RST, 1);
}

bool amiga_trinity_check(uint8_t len, uint8_t *buf)
{
    return false;
}

void amiga_service()
{
    if ((sync_state == SYNC) && clock_timer_fired) {
        // @todo THIS IS WRONG
        gpio_put(PIN_AMIGA_DAT, 1);
        sync_state = IDLE;
        clock_timer_fired = false;
    }
}
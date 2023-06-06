/*
 *  Copyright (C) 2023  System76
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "analog.h"
#include "../system76_ec.h"

#include "thelio_io_2.h"

#define TACH0 GP5
#define TACH1 GP7
#define PBSENSE GP8
#define TACH2 GP9
#define TACH3 GP11
#define DIS_PWMIN GP12
#define PLED GP13
#define PWM0 GP16
#define PWM1 GP17
#define PWM2 GP18
#define PWM3 GP19
#define PBRELAY GP22
#define ADC0 GP26
#define ADC1 GP27
#define ADC2 GP28
#define ADC3 GP29

#define FAN_TIMEOUT 5000
static uint32_t fan_time = 0;

#define UNLOCK_TIMEOUT 15000
static uint32_t unlock_time = 0;

const static PWMConfig PWM_CFG = {
    .frequency = 25000, /* PWM clock frequency, 25KHz */
    .period    = 255, /* PWM period in counter ticks. e.g. clock frequency is 25KHz, period is 255 ticks then t_period is 10.2ms */
    .channels[RP2040_PWM_CHANNEL_A - 1].mode = PWM_OUTPUT_ACTIVE_LOW,
    .channels[RP2040_PWM_CHANNEL_B - 1].mode = PWM_OUTPUT_ACTIVE_LOW,
};

struct Fan {
    PWMDriver * pwm_drv;
    pwmchannel_t pwm_chan;
    int pwm_gpio;
    int tach_gpio;
    uint8_t duty;
    uint32_t duty_time;
    uint16_t tach;
    uint8_t tach_state;
    uint16_t tach_edge;
    uint32_t tach_time;
};

static struct Fan FANOUT1 = {
    .pwm_drv = &PWMD0,
    .pwm_chan = RP2040_PWM_CHANNEL_A,
    .pwm_gpio = PWM0,
    .tach_gpio = TACH0,
    .duty = 0,
    .tach = 0,
    .tach_state = 0,
    .tach_edge = 0,
    .tach_time = 0,
};

static struct Fan FANOUT2 = {
    .pwm_drv = &PWMD0,
    .pwm_chan = RP2040_PWM_CHANNEL_B,
    .pwm_gpio = PWM1,
    .tach_gpio = TACH1,
    .duty = 0,
    .tach = 0,
    .tach_state = 0,
    .tach_edge = 0,
    .tach_time = 0,
};

static struct Fan FANOUT3 = {
    .pwm_drv = &PWMD1,
    .pwm_chan = RP2040_PWM_CHANNEL_A,
    .pwm_gpio = PWM2,
    .tach_gpio = TACH2,
    .duty = 0,
    .tach = 0,
    .tach_state = 0,
    .tach_edge = 0,
    .tach_time = 0,
};

static struct Fan FANOUT4 = {
    .pwm_drv = &PWMD1,
    .pwm_chan = RP2040_PWM_CHANNEL_B,
    .pwm_gpio = PWM3,
    .tach_gpio = TACH3,
    .duty = 0,
    .tach = 0,
    .tach_state = 0,
    .tach_edge = 0,
    .tach_time = 0,
};

void fan_init(struct Fan * fan) {
    palSetPadMode(PAL_PORT(fan->pwm_gpio), PAL_PAD(fan->pwm_gpio), BACKLIGHT_PAL_MODE);
    pwmEnableChannel(fan->pwm_drv, fan->pwm_chan - 1, fan->duty);

    setPinInputHigh(fan->tach_gpio);
    fan->tach_state = readPin(fan->tach_gpio);
}

void fan_scan(struct Fan * fan, uint32_t time) {
    uint8_t tach_state = readPin(fan->tach_gpio);
    if (tach_state != fan->tach_state) {
        fan->tach_edge += 1;
        fan->tach_state = tach_state;
    }

    if (TIMER_DIFF_32(time, fan->tach_time) >= 1000) {
        // Each rotation of the fan has two pulses. Each pulse has two edges.
        // There are four edges per rotation. RPM is therefore:
        // 60 * edges per second / 4, or 15 * edges per second
        fan->tach = 15 * fan->tach_edge;
        fan->tach_edge = 0;
        fan->tach_time = time;
    }
}

void keyboard_pre_init_kb(void) {
    // PBRELAY must be high to capture power button presses
    setPinOutput(PBRELAY);
    writePinLow(PBRELAY);

    // DIS_PWMIN must be high to control fans
    setPinOutput(DIS_PWMIN);
    writePinLow(DIS_PWMIN);

    // Start PWM devices used for fans
    pwmStart(&PWMD0, &PWM_CFG);
    pwmStart(&PWMD1, &PWM_CFG);

    // Initialize fans
    fan_init(&FANOUT1);
    fan_init(&FANOUT2);
    fan_init(&FANOUT3);
    fan_init(&FANOUT4);

    // Call user pre_init function
    keyboard_pre_init_user();
}

void keyboard_post_init_kb(void) {
#ifdef CONSOLE_ENABLE
    // Set console verbosity
    debug_enable   = true;
    debug_matrix   = false;
    debug_keyboard = false;
#endif // CONSOLE_ENABLE

#ifdef BACKLIGHT_ENABLE
    // Set backlight defaults
    backlight_enable();
    backlight_level(BACKLIGHT_LEVELS);
    breathing_disable();
#endif // BACKLIGHT_ENABLE

    // Call user post_init function
    keyboard_post_init_user();
}

void housekeeping_task_kb(void) {
    uint32_t time = timer_read32();

    // Scan fans for tachometer
    fan_scan(&FANOUT1, time);
    fan_scan(&FANOUT2, time);
    fan_scan(&FANOUT3, time);
    fan_scan(&FANOUT4, time);

    // Check if fans are controlled by this device
    if (readPin(DIS_PWMIN)) {
        // If fan timeout reached
        if (TIMER_DIFF_32(time, fan_time) >= FAN_TIMEOUT) {
            printf("Disable fan control\n");
            // Disable fan control by this device
            writePinLow(DIS_PWMIN);
        }
    }

    // Check if trying to unlock
    if (readPin(PBRELAY)) {
        uint32_t unlock_elapsed = TIMER_DIFF_32(time, unlock_time);
        if (
            // If not unlocked
            !system76_ec_is_unlocked() &&
            // If unlock timer not expired
            (unlock_elapsed <= UNLOCK_TIMEOUT)
        ) {
#ifdef BACKLIGHT_ENABLE
        backlight_set(((time % 1000) * (BACKLIGHT_LEVELS + 1)) / 1000);
#endif // BACKLIGHT_ENABLE
        } else {
            // Disable power button override
            writePinLow(PBRELAY);
#ifdef BACKLIGHT_ENABLE
            backlight_set(get_backlight_level());
#endif // BACKLIGHT_ENABLE
        }
    }

    if (system76_ec_is_unlocked()) {
#ifdef BACKLIGHT_ENABLE
        backlight_set(BACKLIGHT_LEVELS - ((time % 1000) * (BACKLIGHT_LEVELS + 1)) / 1000);
#endif // BACKLIGHT_ENABLE
    }

    //TODO: use info from ADCs
    static uint32_t adc_time = 0;
    if (TIMER_DIFF_32(time, adc_time) >= 500) {
        uint16_t adc0 = analogReadPin(ADC0);
        uint16_t adc1 = analogReadPin(ADC1);
        uint16_t adc2 = analogReadPin(ADC2);
        uint16_t adc3 = analogReadPin(ADC3);

        printf("ADC: %d %d %d %d\n", adc0, adc1, adc2, adc3);

        uint32_t mv0 = (3300 * ((uint32_t)adc0)) / 1024;
        uint32_t mv1 = (3300 * ((uint32_t)adc1)) / 1024;
        uint32_t mv2 = (3300 * ((uint32_t)adc2)) / 1024;
        uint32_t mv3 = (3300 * ((uint32_t)adc3)) / 1024;

        printf("mV: %ld %ld %ld %ld\n", mv0, mv1, mv2, mv3);

        adc_time = time;
    }
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (input_disabled) {
        return false;
    }

    if (!process_record_user(keycode, record)) {
        return false;
    }

    switch (keycode) {
        case QK_BOOT:
            if (record->event.pressed) {
                system76_ec_unlock();
            }
            return false;
    }

    return true;
}

void suspend_wakeup_init_kb(void) {
#ifdef BACKLIGHT_ENABLE
    breathing_disable();
#endif // BACKLIGHT_ENABLE
}

void suspend_power_down_kb(void) {
#ifdef BACKLIGHT_ENABLE
    breathing_enable();
#endif // BACKLIGHT_ENABLE
}

bool system76_ec_fan_get(uint8_t index, uint8_t * duty) {
    switch (index) {
        case 0:
            *duty = FANOUT1.duty;
            return true;
        case 1:
            *duty = FANOUT2.duty;
            return true;
        case 2:
            *duty = FANOUT3.duty;
            return true;
        case 3:
            *duty = FANOUT4.duty;
            return true;
        default:
            return false;
    }
}

bool system76_ec_fan_set(uint8_t index, uint8_t duty) {
    switch (index) {
        case 0:
            FANOUT1.duty = duty;
            fan_init(&FANOUT1);
            break;
        case 1:
            FANOUT2.duty = duty;
            fan_init(&FANOUT2);
            break;
        case 2:
            FANOUT3.duty = duty;
            fan_init(&FANOUT3);
            break;
        case 3:
            FANOUT4.duty = duty;
            fan_init(&FANOUT4);
            break;
        default:
            return false;
    }

    // Enable fan control by this device and set time
    printf("Set fan %d to %d\n", index, duty);
    fan_time = timer_read32();
    writePinHigh(DIS_PWMIN);

    return true;
}

bool system76_ec_fan_tach(uint8_t index, uint16_t * tach) {
    switch (index) {
        case 0:
            *tach = FANOUT1.tach;
            return true;
        case 1:
            *tach = FANOUT2.tach;
            return true;
        case 2:
            *tach = FANOUT3.tach;
            return true;
        case 3:
            *tach = FANOUT4.tach;
            return true;
        default:
            return false;
    }
}

bool system76_ec_security_get(enum SecurityState * state) {
    if (system76_ec_is_unlocked()) {
        *state = SECURITY_STATE_UNLOCK;
    } else if (readPin(PBRELAY)) {
        *state = SECURITY_STATE_PREPARE_UNLOCK;
    } else {
        *state = SECURITY_STATE_LOCK;
    }
    return true;
}

bool system76_ec_security_set(enum SecurityState state) {
    if (state == SECURITY_STATE_PREPARE_UNLOCK) {
        if (
            // Not already unlocked
            !system76_ec_is_unlocked() &&
            // Not already overriding power button
            !readPin(PBRELAY)
        ) {
            // Set power button override
            unlock_time = timer_read32();
            writePinHigh(PBRELAY);
        }
        return true;
    } else {
        return false;
    }
}

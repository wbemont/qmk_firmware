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

#pragma once

extern bool input_disabled;

void system76_ec_unlock(void);
bool system76_ec_is_unlocked(void);

bool system76_ec_fan_get(uint8_t index, uint8_t * duty);
bool system76_ec_fan_set(uint8_t index, uint8_t duty);
bool system76_ec_fan_tach(uint8_t index, uint16_t * tach);

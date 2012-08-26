/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Iztok Jeras <iztok.jeras@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include <inttypes.h>

class pdc_uart {

	// configuration structure
	struct {
		// baudrate in samplerate units u16.16
		unsigned int baudrate;
		// buffer size
		unsigned int buffer_size;
		// observed LA port
		unsigned int port;
                // UART properties
		unsigned int byte_len;
		unsigned int parity_len;
		unsigned int parity_pol;
		unsigned int stop_len;
		// data length
		unsigned int parity_pos;
		unsigned int stop_pos;
	} cfg;

	// status structure
	struct {
		// run status
		unsigned int run;
		// bit counter and character data
		unsigned int bit_cnt;
		char bit_dat;
		// timing (current and sample time)
		unsigned int tc, ts;
	} sts;

public:

	// combines __init_ and start from python decoders
	int init (char *config)
	{
		// select LA port
		cfg.port = 0;
		// parse configuration
		unsigned int baudrate = 9600;
		unsigned int samplerate = 8000000; // 8MHz
		unsigned int packet_size = 256;
		cfg.byte_len = 8;
		cfg.parity_len = 0;
		cfg.stop_len = 1;

		// calculate UART configuration
		cfg.parity_pos = cfg.byte_len + 1;
		cfg.stop_pos = cfg.byte_len + cfg.parity_len + cfg.stop_len;
		// for now use constant values
		cfg.baudrate = (baudrate << 16) / samplerate;
		cfg.buffer_size = (packet_size << 16) / cfg.baudrate / (cfg.stop_pos+1) + 1;

		// initialize status
		sts.run = 0;
		sts.bit_cnt = 0;
		sts.bit_dat = 0x00;
	}

        // decode 8 bit data
	void decode_8 (uint8_t *logic, unsigned int size)
	{
		char buffer[cfg.buffer_size];
		unsigned int buffer_cnt = 0;
		unsigned int value;
		for (unsigned int i; i<size; i++) {
			value = (logic[i] >> cfg.port) & 0x1;
			// idle state
			if (!sts.run) {
				if (!value) {
					sts.tc = 0;
					sts.ts = cfg.baudrate + cfg.baudrate/2;
					sts.run = 1;
				}
			}
			else {
				// increment time and compare against sample
				sts.tc += 1<<16;
				if (sts.tc >= sts.ts) {
					sts.tc -= sts.ts;
					sts.ts = cfg.baudrate;
					// data bits
					if (sts.bit_cnt < cfg.byte_len) {
						sts.bit_dat |= value << sts.bit_cnt;
					}
					// last stop bit
					if (sts.bit_cnt == cfg.stop_pos) {
						sts.bit_cnt = 0;
						buffer[buffer_cnt] = sts.bit_dat;
						buffer_cnt++;
						sts.run = 0;
					}
					// optional parity bit
					if (sts.bit_cnt == cfg.parity_pos) {
						// do nothing
					}
					sts.bit_cnt++;
				}
			}
		}
	}

};


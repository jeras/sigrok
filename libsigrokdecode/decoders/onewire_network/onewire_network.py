##
## This file is part of the sigrok project.
##
## Copyright (C) 2012 Iztok Jeras <iztok.jeras@gmail.com>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
##

# 1-Wire protocol decoder (network layer)

import sigrokdecode as srd

# Dictionary of ROM commands and their names, next state.
command = {
    0x33: ['Read ROM'              , 'GET ROM'   ],
    0x0f: ['Conditional read ROM'  , 'GET ROM'   ],
    0xcc: ['Skip ROM'              , 'TRANSPORT' ],
    0x55: ['Match ROM'             , 'GET ROM'   ],
    0xf0: ['Search ROM'            , 'SEARCH ROM'],
    0xec: ['Conditional search ROM', 'SEARCH ROM'],
    0x3c: ['Overdrive skip ROM'    , 'TRANSPORT' ],
    0x69: ['Overdrive match ROM'   , 'GET ROM'   ]
}

class Decoder(srd.Decoder):
    api_version = 1
    id = 'onewire_network'
    name = '1-Wire network layer'
    longname = '1-Wire serial communication bus (network layer)'
    desc = 'Bidirectional, half-duplex, asynchronous serial bus.'
    license = 'gplv2+'
    inputs = ['onewire_link']
    outputs = ['onewire_network']
    probes = []
    optional_probes = []
    options = {}
    annotations = [
        ['Text', 'Human-readable text'],
    ]

    def __init__(self, **kwargs):
        self.beg = 0  # Bitstream beginning.
        self.end = 0  # Bitstream end.
        self.cnt = 0  # Bit counter.
        self.pol = 'P'  # Search polarity (P-positive, N-negative, D-data).
        self.dtp = 0x0  # Search positive bits.
        self.dtn = 0x0  # Search negative bits.
        self.dat = 0x0  # Data stream bits.
        self.rom = 0x0000000000000000  # Current device ROM address.
        self.state = 'COMMAND'  # Current decoder state.

    def start(self, metadata):
        self.out_proto = self.add(srd.OUTPUT_PROTO, 'onewire_network')
        self.out_ann = self.add(srd.OUTPUT_ANN, 'onewire_network')

    def report(self):
        pass

    def puta(self, data):
        # Helper function for most annotations.
        self.put(self.beg, self.end, self.out_ann, data)

    def putp(self, data):
        # Helper function for most protocol packets.
        self.put(self.beg, self.end, self.out_proto, data)

    def decode(self, ss, es, data):
        code, val = data

        # State machine.
        if code == 'RESET/PRESENCE':
            # If a reset is received, reset the decoder state.
            self.pol = 'P'
            self.cnt = 0
            self.put(ss, es, self.out_ann,
                     [0, ['Reset/presence: %s' % ('true' if val else 'false')]])
            self.put(ss, es, self.out_proto, ['RESET/PRESENCE', val])
            self.state = 'COMMAND'
            return
        elif code == 'POWER':
            # If a power event is received, forward it to the next protocol layer.
            self.put(ss, es, self.out_ann,
                     [0, ['Power: %s' % ('applied' if val else 'removed')]])
            self.put(ss, es, self.out_proto, ['POWER', val])
            return
        elif code != 'BIT':
            # For here on we're only interested in 'BIT' events.
            raise Exception('Invalied protocol event: \'%s\'' % code)
            return

        if self.state == 'COMMAND':
            # Receiving and decoding a ROM command.
            if not self.onewire_collect(8, val, ss, es):
                # Still waiting to reseive 8 bits.
                return
            if self.dat in command:
                # If a recognized command is received,
                # the next state is derived from the command dictionary.
                self.puta([0, ['ROM command: 0x%02x \'%s\''
                          % (self.dat, command[self.dat][0])]])
                self.state = command[self.dat][1]
            else:
                # Else if the command is not recognized,
                # go into an error state, where only raw bytes are printed.
                self.puta([0, ['ROM command: 0x%02x \'%s\''
                          % (self.dat, 'unrecognized')]])
                self.state = 'COMMAND ERROR'
        elif self.state == 'GET ROM':
            # A 64 bit device address is selected.
            # Family code (1 byte) + serial number (6 bytes) + CRC (1 byte)
            if not self.onewire_collect(64, val, ss, es):
                return
            self.rom = self.dat & 0xffffffffffffffff
            self.puta([0, ['ROM: 0x%016x' % self.rom]])
            self.putp(['ROM', self.rom])
            self.state = 'TRANSPORT'
        elif self.state == 'SEARCH ROM':
            # A 64 bit device address is searched for.
            # Family code (1 byte) + serial number (6 bytes) + CRC (1 byte)
            if not self.onewire_search(64, val, ss, es):
                return
            self.rom = self.dat & 0xffffffffffffffff
            self.puta([0, ['ROM: 0x%016x' % self.rom]])
            self.putp(['ROM', self.rom])
            self.state = 'TRANSPORT'
        elif self.state == 'TRANSPORT':
            # The transport layer is handled in byte sized units.
            if not self.onewire_collect(8, val, ss, es):
                return
            self.puta([0, ['Data: 0x%02x' % self.dat]])
            self.putp(['DATA', self.dat])
        elif self.state == 'COMMAND ERROR':
            # Since the command is not recognized, print raw data.
            if not self.onewire_collect(8, val, ss, es):
                return
            self.puta([0, ['ROM error data: 0x%02x' % self.dat]])
        else:
            raise Exception('Invalid state: %s' % self.state)

    # Data collector.
    def onewire_collect(self, length, val, ss, es):
        # Storing the sample this sequence begins with.
        if self.cnt == 1:
            self.beg = ss
        self.dat = self.dat & ~(1 << self.cnt) | (val << self.cnt)
        self.cnt += 1
        # Storing the sample this sequence ends with.
        # In case the full length of the sequence is received, return 1.
        if self.cnt == length:
            self.end = es
            self.dat = self.dat & ((1 << length) - 1)
            self.cnt = 0
            return 1
        else:
            return 0

    # Search collector.
    def onewire_search(self, length, val, ss, es):
        # Storing the sample this sequence begins with.
        if (self.cnt == 0) and (self.pol == 'P'):
            self.beg = ss

        if self.pol == 'P':
            # Master receives an original address bit.
            self.dtp = self.dtp & ~(1 << self.cnt) | (val << self.cnt)
            self.pol = 'N'
        elif self.pol == 'N':
            # Master receives a complemented address bit.
            self.dtn = self.dtn & ~(1 << self.cnt) | (val << self.cnt)
            self.pol = 'D'
        elif self.pol == 'D':
            # Master transmits an address bit.
            self.dat = self.dat & ~(1 << self.cnt) | (val << self.cnt)
            self.pol = 'P'
            self.cnt += 1

        # Storing the sample this sequence ends with.
        # In case the full length of the sequence is received, return 1.
        if self.cnt == length:
            self.end = es
            self.dtp = self.dtp & ((1 << length) - 1)
            self.dtn = self.dtn & ((1 << length) - 1)
            self.dat = self.dat & ((1 << length) - 1)
            self.pol = 'P'
            self.cnt = 0
            return 1
        else:
            return 0

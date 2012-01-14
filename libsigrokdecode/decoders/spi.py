##
## This file is part of the sigrok project.
##
## Copyright (C) 2011 Gareth McMullin <gareth@blacksphere.co.nz>
## Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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

import sigrokdecode as srd

# Chip-select options
ACTIVE_LOW = 0
ACTIVE_HIGH = 1

# Clock polarity options
CPOL_0 = 0 # Clock is low when inactive
CPOL_1 = 1 # Clock is high when inactive

# Clock phase options
CPHA_0 = 0 # Data is valid on the rising clock edge
CPHA_1 = 1 # Data is valid on the falling clock edge

# Bit order options
MSB_FIRST = 0
LSB_FIRST = 1

# Annotation formats
ANN_HEX = 0

class Decoder(srd.Decoder):
    id = 'spi'
    name = 'SPI'
    longname = 'Serial Peripheral Interface (SPI) bus'
    desc = '...desc...'
    longdesc = '...longdesc...'
    author = 'Gareth McMullin'
    email = 'gareth@blacksphere.co.nz'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = ['spi']
    probes = [
        {'id': 'mosi', 'name': 'MOSI',
         'desc': 'SPI MOSI line (Master out, slave in)'},
        {'id': 'miso', 'name': 'MISO',
         'desc': 'SPI MISO line (Master in, slave out)'},
        {'id': 'sck', 'name': 'CLK', 'desc': 'SPI clock line'},
        {'id': 'cs', 'name': 'CS#', 'desc': 'SPI CS (chip select) line'},
    ]
    options = {
        'cs_active_low': ['CS# active low', ACTIVE_LOW],
        'clock_polarity': ['Clock polarity', CPOL_0],
        'clock_phase': ['Clock phase', CPHA_0],
        'bit_order': ['Bit order within the SPI data', MSB_FIRST],
        'word_size': ['Word size of SPI data', 8], # 1-64?
    }
    annotations = [
        ['Hex', 'SPI data bytes in hex format'],
    ]

    def __init__(self):
        self.oldsck = 1
        self.bitcount = 0
        self.mosidata = 0
        self.misodata = 0
        self.bytesreceived = 0
        self.samplenum = -1

    def start(self, metadata):
        self.out_proto = self.add(srd.OUTPUT_PROTO, 'spi')
        self.out_ann = self.add(srd.OUTPUT_ANN, 'spi')

    def report(self):
        return 'SPI: %d bytes received' % self.bytesreceived

    def decode(self, ss, es, data):
        # HACK! At the moment the number of probes is not handled correctly.
        # E.g. if an input file (-i foo.sr) has more than two probes enabled.
        # for (samplenum, (mosi, sck, x, y, z, a)) in data:
        # for (samplenum, (cs, miso, sck, mosi, wp, hold)) in data:
        for (samplenum, (cs, miso, sck, mosi, wp, hold)) in data:

            self.samplenum += 1 # FIXME

            # Sample data on rising SCK edges.
            if sck == self.oldsck:
                continue
            self.oldsck = sck
            if sck == 0:
                continue

            # If this is the first bit, save its sample number.
            if self.bitcount == 0:
                self.start_sample = samplenum

            # Receive bit into our shift register.
            if mosi == 1:
                self.mosidata |= 1 << (7 - self.bitcount)
            if miso == 1:
                self.misodata |= 1 << (7 - self.bitcount)

            self.bitcount += 1

            # Continue to receive if not a byte yet.
            if self.bitcount != 8:
                continue

            self.put(self.start_sample, self.samplenum, self.out_proto,
                     ['data', self.mosidata, self.misodata])
            self.put(self.start_sample, self.samplenum, self.out_ann,
                     [ANN_HEX, ['MOSI: 0x%02x, MISO: 0x%02x' % (self.mosidata,
                     self.misodata)]])

            # Reset decoder state.
            self.mosidata = 0
            self.misodata = 0
            self.bitcount = 0

            # Keep stats for summary.
            self.bytesreceived += 1


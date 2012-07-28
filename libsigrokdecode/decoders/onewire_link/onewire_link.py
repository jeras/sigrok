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

# 1-Wire protocol decoder (link layer)

import sigrokdecode as srd

# minimum and suggested sample rates
samplerates = [
#                 minimum, suggested
    ['normal'   , [ 400000, 1000000]],  # normal    mode
    ['overdrive', [2000000, 5000000]]   # overdrive mode
]

# 1-Wire protocol timing as defined by the standard, in micro seconds
timings = {
#          [                      [time in micro seconds  ], [time in clock periods  ]] ]
#          [                      [[ normal ], [overdrive]]  [[ normal ], [overdrive]]] ]
#    name: ['description'       , [[min, max], [ min, max]], [[min, max], [ min, max]]] ]
    'rsl': ['reset low time'    , [[480, 640], [  48,  80]], [[  0,   0], [   0,   0,]] ],
    'rsh': ['reset high time'   , [[480,   0], [  48,   0]], [[  0,   0], [   0,   0,]] ],
    'pdh': ['presence high time', [[ 15,  60], [   2,   6]], [[  0,   0], [   0,   0,]] ],
    'pdl': ['presence low time' , [[ 60, 240], [   8,  24]], [[  0,   0], [   0,   0,]] ],
    'd0l': ['data 0 low time'   , [[ 60, 120], [   6,  16]], [[  0,   0], [   0,   0,]] ],
    'd1l': ['data 1 low time'   , [[  5,  15], [   1,   2]], [[  0,   0], [   0,   0,]] ],
    'rec': ['recovery time'     , [[  5,   0], [   2,   0]], [[  0,   0], [   0,   0,]] ]
}

# extreme constants
MIN = 0  # minimum
MAX = 1  # maximum
# mode constants
NRM = 0  # normal
OVD = 1  # overdrive
# column selection constants
DSC = 0  # desctiption
TMG = 1  # time in micto seconds
CNT = 2  # counter in clock periods

class Decoder(srd.Decoder):
    api_version = 1
    id = 'onewire_link'
    name = '1-Wire link layer'
    longname = '1-Wire serial communication bus (link layer)'
    desc = 'Bidirectional, half-duplex, asynchronous serial bus.'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = ['onewire_link']
    probes = [
        {'id': 'owr', 'name': 'OWR', 'desc': '1-Wire signal line'},
    ]
    optional_probes = [
        {'id': 'pwr', 'name': 'PWR', 'desc': '1-Wire power supply pin'},
    ]
    options = {
        'overdrive': ['Overdrive', 1]
    }
    options.update(dict(('t_nrm_min_'+key, ['normal mode min '   +timings[key][0], 0]) for key in iter(timings)))
    options.update(dict(('t_nrm_max_'+key, ['normal mode max '   +timings[key][0], 0]) for key in iter(timings)))
    options.update(dict(('t_ovd_min_'+key, ['overdrive mode min '+timings[key][0], 0]) for key in iter(timings)))
    options.update(dict(('t_ovd_max_'+key, ['overdrive mode max '+timings[key][0], 0]) for key in iter(timings)))
    annotations = [
        ['Text', 'Human-readable text'],
        ['Warnings', 'Human-readable warnings'],
    ]

    def __init__(self, **kwargs):
        self.samplerate = 0
        self.owr = 1
        self.pwr = 0
        self.trigger = 0
        self.state = 'WAIT FOR FALLING EDGE'
        self.present = 0
        self.bit = 0
        self.bit_cnt = 0
        self.command = 0
        self.overdrive = 0
        self.fall = 0
        self.rise = 0

    def start(self, metadata):
        self.out_proto = self.add(srd.OUTPUT_PROTO, 'onewire_link')
        self.out_ann = self.add(srd.OUTPUT_ANN, 'onewire_link')

        self.samplerate = metadata['samplerate']

        ovd = self.options['overdrive']
        self.put(0, 0, self.out_ann, [0, ['NOTE: Sample rate is %0.0fMHz and %s mode is supported.' % (self.samplerate/1000000, samplerates[ovd][0])]])
        if (self.samplerate < samplerates[ovd][1][0]):
            self.put(0, 0, self.out_ann, [0, ['ERROR: Sampling rate must be above %fMHz.' % (samplerates[ovd][1][0]/1000000)]])
        if (self.samplerate < samplerates[ovd][1][1]):
            self.put(0, 0, self.out_ann, [0, ['WARNING: Sampling rate is suggested to be above %fMHz.' % (samplerates[ovd][1][1]/1000000)]])

        # if options were provided use them, otherwise recalculate timings into clock periods
        self.put(0, 0, self.out_ann, [0, ['NOTE:  Timing before.']])
        for key in iter(self.options):
            val = self.options[key]
            # do not process overdrive, it is not a timing option
            if not (key == 'overdrive'):
                # decode mode
                modes = {'nrm': 0, 'ovd': 1}
                mod = modes[key[2:5]]
                # decode extreme
                extremes = {'min': 0, 'max': 1}
                ext = extremes[key[6:9]]
                # check if option was modified
                if (val == 0):
                    timings[key[10:13]][CNT][mod][ext] = int(self.samplerate * 0.000001 * timings[key[10:13]][TMG][mod][ext]) - 1
                else:
                    timings[key[10:13]][CNT][mod][ext] = val
                self.put(0, 0, self.out_ann, [0, ['NOTE: key='+str(key)+' val='+str(val)+' tmg='+str(timings[key[10:13]][TMG][mod][ext])+' cnt='+str(timings[key[10:13]][CNT][mod][ext])]])

    def report(self):
        pass

    def decode(self, ss, es, data):
        for (samplenum, (owr, pwr)) in data:

            # Process data only if there is a a change or if the trigger is reached
            #if ((self.owr != owr) or (samplenum == self.trigger)):
            if ((self.owr != owr)):

                # check if recovery time is met
                if (self.trigger > samplenum):
                    self.put(samplenum, self.trigger, self.out_ann, [0, ['WARNING: master timing issue, recovery time not met']])

                # State machine.
                if self.state == 'WAIT FOR FALLING EDGE':
                    if (owr):
                        self.put(self.fall, samplenum, self.out_ann, [0, ['error: ovd=1']])
                    # Save the sample number for the falling edge.
                    self.fall = samplenum
                    self.put(self.fall, samplenum, self.out_ann, [0, ['debug: falling edge detected']])
                    # Go to waiting for a rising edge
                    self.state = 'WAIT FOR RISING EDGE'
                elif self.state == 'WAIT FOR RISING EDGE':
                    if (not owr):
                        self.put(self.fall, samplenum, self.out_ann, [0, ['error: ovd=0']])
                    # Save the sample number for the rising edge.
                    self.rise = samplenum
                    self.low = self.rise - self.fall
                    self.put(self.fall, samplenum, self.out_ann, [0, ['debug: rising edge detected T=%fus'%(float(self.low)/self.samplerate*1000000)]])
                    # process pulse length
                    if (self.low < timings['d0l'][CNT][self.overdrive][MAX]):
                        # This is a data slot
                        if (self.low < timings['d1l'][CNT][self.overdrive][MAX]):
                            # Check if slot is too short
                            if (self.low < timings['d1l'][CNT][self.overdrive][MIN]):
                                self.put(self.fall, samplenum, self.out_ann, [0, ['debug: data 1 too short']])
                            # This a data 1 slot
                            self.put(self.fall, samplenum, self.out_ann, [0, ['BIT: 1']])
                            self.put(self.fall, samplenum, self.out_proto, ['BIT', 1])
                            self.trigger = samplenum + (timings['d0l'][CNT][self.overdrive][MIN] +
                                                        timings['rec'][CNT][self.overdrive][MIN])
                        else:
                            # This a data 0 slot
                            self.put(self.fall, samplenum, self.out_ann, [0, ['BIT: 0']])
                            self.put(self.fall, samplenum, self.out_proto, ['BIT', 0])
                            self.trigger = samplenum + timings['rec'][CNT][self.overdrive][MIN]
                        # This is a data slot, another slot comes next
                        self.state = 'WAIT FOR FALLING EDGE'
                    else:
                        # Check if slot is too short
                        if (self.low < timings['rsl'][CNT][self.overdrive][MIN]):
                            self.put(self.fall, samplenum, self.out_ann, [0, ['debug: reset too short']])
                        # Check if slot is too short to be a normal reset and too long for overdrive
                        if ((self.low > timings['rsl'][CNT][OVD][MAX]) and
                            (self.low < timings['rsl'][CNT][NRM][MIN])):
                            self.put(self.fall, samplenum, self.out_ann, [0, ['debug: reset between normal and overdrive']])
                        # Check if this pulse clears overdrive mode
                        if (self.low > timings['rsl'][CNT][NRM][MIN]):
                            if (self.overdrive):
                                self.put(self.fall, samplenum, self.out_ann, [0, ['EXIT OVERDRIVE MODE']])
                                self.overdrive = 0
                        # Check if slot is too long
                        if (self.low > timings['rsl'][CNT][NRM][MAX]):
                            self.put(self.fall, samplenum, self.out_ann, [0, ['debug: reset too long']])
                        # This is a reset slot presence pulse follows
                        self.state = 'WAIT FOR PRESENCE PULSE'
                        # Clear command bit counter and data register
                        self.bit_cnt = 0
                        self.command = 0
                elif self.state == 'WAIT FOR PRESENCE PULSE':
                    # Start of the presence pulse
                    if (not owr):
                        self.put(self.fall, samplenum, self.out_ann, [0, ['debug: presence start']])
                        self.state = 'WAIT FOR PRESENCE PULSE'
                    # End of the presence pulse
                    elif (owr):
                        self.put(self.fall, samplenum, self.out_ann, [0, ['debug: presence end']])
                        self.put(self.fall, samplenum, self.out_ann, [0, ['RESET/PRESENCE: True']])
                        self.put(self.fall, samplenum, self.out_proto, ['RESET/PRESENCE', 1])
                        self.state = 'WAIT FOR FALLING EDGE'
                    else:
                        self.put(self.fall, samplenum, self.out_ann, [0, ['RESET/PRESENCE: False']])
                        self.put(self.fall, samplenum, self.out_proto, ['RESET/PRESENCE', 0])
                        self.state = 'WAIT FOR FALLING EDGE'
                else:
                    raise Exception('Invalid state: %s' % self.state)

            # store the previous sample
            self.owr = owr
            self.pwr = pwr


def overdrive_command (bit):
    # Checking the first command to see if overdrive mode should be entered
    if   (self.bit_cnt <= 8):
        self.command = self.command | (bit << self.bit_cnt)
    elif (self.bit_cnt == 8):
        if (self.command in [0x3c, 0x69]):
            self.put(self.fall, self.cnt_bit[self.overdrive], self.out_ann, [0, ['ENTER OVERDRIVE MODE']])
            self.overdrive = 1
    # Incrementing the bit counter
    self.bit_cnt += 1

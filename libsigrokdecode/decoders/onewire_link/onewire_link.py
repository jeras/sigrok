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

# List of minimum and suggested sample rates.
samplerates = [
#                 minimum, suggested
    ['normal'   , [ 400000, 1000000]],  # normal    mode
    ['overdrive', [2000000, 5000000]]   # overdrive mode
]

# 1-Wire protocol timing as defined by the standard (micro seconds).
timings = {
#          [                      [time in micro seconds  ], [time in clock periods  ]] ]
#          [                      [[ normal ], [overdrive]]  [[ normal ], [overdrive]]] ]
#    name: ['description'       , [[min, max], [ min, max]], [[min, max], [ min, max]]] ]
    'rsl': ['reset low time'    , [[480, 640], [  48,  80]], [[  0,   0], [   0,   0,]] ],
#   'rsh': ['reset high time'   , [[480,   0], [  48,   0]], [[  0,   0], [   0,   0,]] ],  # unused
    'pdh': ['presence high time', [[ 15,  60], [   2,   6]], [[  0,   0], [   0,   0,]] ],
    'pdl': ['presence low time' , [[ 60, 240], [   8,  24]], [[  0,   0], [   0,   0,]] ],
    'd0l': ['data 0 low time'   , [[ 60, 120], [   6,  16]], [[  0,   0], [   0,   0,]] ],
    'd1l': ['data 1 low time'   , [[  5,  15], [   1,   2]], [[  0,   0], [   0,   0,]] ],
    'rec': ['recovery time'     , [[  5,   0], [   2,   0]], [[  0,   0], [   0,   0,]] ]
}

# Extreme constants.
MIN = 0  # minimum
MAX = 1  # maximum
# Mode constants.
NRM = 0  # normal
OVD = 1  # overdrive
# Column selection constants.
DSC = 0  # description
TMG = 1  # time in micro seconds
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
        {'id': 'pwr', 'name': 'PWR', 'desc': '1-Wire power signal'},
    ]
    options = {
        'overdrive': ['Overdrive', 1]
    }
    options.update(dict(('t_nrm_min_'+key, ['normal mode minimum '   +timings[key][DSC], 0]) for key in iter(timings)))
    options.update(dict(('t_nrm_max_'+key, ['normal mode maximum '   +timings[key][DSC], 0]) for key in iter(timings)))
    options.update(dict(('t_ovd_min_'+key, ['overdrive mode minimum '+timings[key][DSC], 0]) for key in iter(timings)))
    options.update(dict(('t_ovd_max_'+key, ['overdrive mode maximum '+timings[key][DSC], 0]) for key in iter(timings)))
    annotations = [
        ['Text', 'Human-readable text'],
        ['Timing', 'Human-readable events with timings in us']
    ]

    def __init__(self, **kwargs):
        self.samplerate = 0
        self.owr = 1  # 1-Wire signal old value.
        self.pwr = 0  # 1-Wire power old value.
        self.trg = 0  # Programmable timeout trigger.
        self.bit = 0  # Current data bit.
        self.cnt = 0  # Data bit counter.
        self.cmd = 0  # ROM command.
        self.ovd = 0  # Current overdrive status.
        self.fall = 0  # Signal fall sample.
        self.rise = 0  # Signal rise sample.
        self.pss = 0  # Presence start sample.
        self.pes = 0  # Presence end sample.
        self.state = 'WAIT FOR FALLING EDGE'

    def start(self, metadata):
        self.out_proto = self.add(srd.OUTPUT_PROTO, 'onewire_link')
        self.out_ann = self.add(srd.OUTPUT_ANN, 'onewire_link')

        self.samplerate = metadata['samplerate']

        ovd = self.options['overdrive']
        self.put(0, 0, self.out_ann, [1, ['NOTE: Sample rate is %0.0fMHz and %s mode is supported.' % (self.samplerate/1000000, samplerates[ovd][0])]])
        if self.samplerate < samplerates[ovd][1][0]:
            self.put(0, 0, self.out_ann, [1, ['ERROR: Sampling rate must be above %fMHz.' % (samplerates[ovd][1][0]/1000000)]])
        if self.samplerate < samplerates[ovd][1][1]:
            self.put(0, 0, self.out_ann, [1, ['WARNING: Sampling rate is suggested to be above %fMHz.' % (samplerates[ovd][1][1]/1000000)]])

        # If options were provided use them,
        # otherwise recalculate timings into clock periods.
        for key in iter(self.options):
            val = self.options[key]
            # Do not process overdrive, it is not a timing option.
            if not (key == 'overdrive'):
                # Decode mode.
                modes = {'nrm': NRM, 'ovd': OVD}
                mod = modes[key[2:5]]
                # Decode extreme.
                extremes = {'min': MIN, 'max': MAX}
                ext = extremes[key[6:9]]
                # Check if option was modified.
                if val:
                    timings[key[10:13]][CNT][mod][ext] = val
                else:
                    timings[key[10:13]][CNT][mod][ext] = int(self.samplerate * 0.000001 * timings[key[10:13]][TMG][mod][ext])
        # Print out all calculated timing options.
        for key in timings: 
            self.put(0, 0, self.out_ann, [1, ['NOTE: '+str(timings[key])]])

    def report(self):
        pass

    def decode(self, ss, es, data):
        for (samplenum, (owr, pwr)) in data:

            # Check if the trigger sample is reached.
            if samplenum == self.trg:

                # Check if recovery time is met.
                if self.trg > samplenum:
                    self.put(self_rise, samplenum, self.out_ann, [1, ['WARNING: master timing issue, recovery time not met']])

                # After timeout report no presence pulse was received.
                if self.state == 'WAIT FOR PRESENCE PULSE':
                    self.put(self.fall, samplenum, self.out_ann, [0, ['Reset/presence: false']])
                    self.put(self.fall, samplenum, self.out_proto, ['RESET/PRESENCE', 0])
                    self.state = 'WAIT FOR FALLING EDGE'

            # Process data only if there is a signal change.
            if self.owr != owr:

                # State machine.
                if self.state == 'WAIT FOR FALLING EDGE':
                    # Save the sample number for the falling edge.
                    self.fall = samplenum
                    # Go to waiting for a rising edge.
                    self.state = 'WAIT FOR RISING EDGE'
                elif self.state == 'WAIT FOR RISING EDGE':
                    # Save the sample number for the rising edge.
                    self.rise = samplenum
                    # Measure signal low time in samples and micro seconds.
                    self.low = self.rise - self.fall
                    time = float(self.low) / self.samplerate * 1000000
                    # Process pulse length.
                    # Check if this is a data slot.
                    if self.low < timings['d0l'][CNT][self.ovd][MAX]:
                        # This is a data slot.
                        # Check if it is 1 or 0.
                        if self.low < timings['d1l'][CNT][self.ovd][MAX]:
                            # This a data 1 slot.
                            self.bit = 1
                            # Check if slot is too short for data 1.
                            if self.low < timings['d1l'][CNT][self.ovd][MIN]:
                                self.put(self.fall, samplenum, self.out_ann, [1, ['WARNING: data 1 pulse too short']])
                            # Trigger time is set to end of slot plus recovery time.
                            self.trg = self.fall + timings['d0l'][CNT][self.ovd][MIN] + \
                                                   timings['rec'][CNT][self.ovd][MIN] - 1
                        else:
                            # This a data 0 slot.
                            self.bit = 0
                            # Trigger time is set to end of recovery time.
                            self.trg = samplenum + timings['rec'][CNT][self.ovd][MIN] - 1
                        # Report received data bit.
                        self.put(self.fall, self.fall + timings['d0l'][CNT][self.ovd][MIN], self.out_ann, [0, ['Bit: %d' % self.bit]])
                        self.put(self.fall, self.rise, self.out_ann, [1, ['Bit: %d (%.1fus)' % (self.bit, time)]])
                        self.put(self.fall, self.fall + timings['d0l'][CNT][self.ovd][MIN], self.out_proto, ['BIT', self.bit])
                        # Detect overdrive commands.
                        if self.cnt < 8:
                            self.cmd = self.cmd | (self.bit << self.cnt)
                        elif self.cnt == 8:
                            if self.cmd in [0x3c, 0x69]:
                                self.ovd = 1
                                self.put(self.fall, 0, self.out_ann, [0, ['Enter overdrive mode']])
                        # Incrementing bit counter.
                        self.cnt += 1
                        # This is a data slot, another slot is expected next.
                        self.state = 'WAIT FOR FALLING EDGE'
                    else:
                        # Check if slot is too short for a reset.
                        if self.low < timings['rsl'][CNT][self.ovd][MIN]:
                            self.put(self.fall, samplenum, self.out_ann, [1, ['WARNING: reset pulse too short']])
                        # Check if slot is too short to be a normal reset and too long for overdrive.
                        if (self.low > timings['rsl'][CNT][OVD][MAX]) and \
                           (self.low < timings['rsl'][CNT][NRM][MIN]):
                            self.put(self.fall, samplenum, self.out_ann, [1, ['WARNING: reset pulse length between normal and overdrive']])
                        # Check if this reset pulse clears overdrive mode.
                        if self.low >= timings['rsl'][CNT][NRM][MIN]:
                            self.ovd = 0
                            if self.ovd:
                                self.put(self.fall, samplenum, self.out_ann, [0, ['Exit overdrive mode']])
                        # Check if slot is too long.
                        if (self.low > timings['rsl'][CNT][NRM][MAX]):
                            self.put(self.fall, samplenum, self.out_ann, [1, ['WARNING: reset pulse too long']])
                        # Trigger time is set to the end of the slowest presence pulse.
                        self.trg = samplenum + timings['pdh'][CNT][self.ovd][MAX] + \
                                               timings['pdl'][CNT][self.ovd][MAX] - 1
                        # Report reset pulse timing.
                        self.put(self.fall, self.rise, self.out_ann, [1, ['Reset: (%.1fus)' % time]])
                        # This is a reset slot, presence pulse follows.
                        self.state = 'WAIT FOR PRESENCE PULSE'
                        # Clear command bit counter and data register.
                        self.cnt = 0
                        self.cmd = 0
                elif self.state == 'WAIT FOR PRESENCE PULSE':
                    # Start of the presence pulse.
                    if not owr:
                        self.pss = samplenum
                        self.state = 'WAIT FOR PRESENCE PULSE'
                    # End of the presence pulse.
                    else:
                        self.pes = samplenum
                        time = float(self.pes - self.pss) / self.samplerate * 1000000
                        self.put(self.fall, samplenum, self.out_ann, [0, ['Reset/presence: True']])
                        self.put(self.pss, self.pes, self.out_ann, [1, ['Presence: (%.1fus)' % time]])
                        self.put(self.fall, samplenum, self.out_proto, ['RESET/PRESENCE', 1])
                        self.state = 'WAIT FOR FALLING EDGE'
                else:
                    raise Exception('Invalid state: %s' % self.state)

            # Process power data only if there is a power change.
            if self.pwr != pwr:

                if pwr:
                    self.put(samplenum, samplenum, self.out_ann, [0, ['POWER: Applied']])
                    self.put(samplenum, samplenum, self.out_ann, [1, ['POWER: Applied']])
                    self.put(samplenum, samplenum, self.out_proto, ['POWER', 1])
                else:
                    self.put(samplenum, samplenum, self.out_ann, [0, ['POWER: Removed']])
                    self.put(samplenum, samplenum, self.out_ann, [1, ['POWER: Removed']])
                    self.put(samplenum, samplenum, self.out_proto, ['POWER', 0])

            # Store the previous sample.
            self.owr = owr
            self.pwr = pwr

##
## This file is part of the sigrok project.
##
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

'''
1-Wire protocol decoder (link layer).

The 1-Wire protocol enables bidirectional communication over a single wire
(and ground) between a single master and one or multiple slaves. The protocol
is layered:

 - Link layer (reset, presence detection, reading/writing bits)
 - Network layer (skip/search/match device ROM addresses)
 - Transport layer (transport data between 1-Wire master and device)

Link layer protocol details:

Sample rate:
A sufficiently high samplerate is required to properly detect all the elements
of the protocol. A lower samplerate can be used if the master does not use
overdrive communication speed. The following minimal values should be used:

 - overdrive available: 2MHz minimum, 5MHz suggested
 - overdrive not available: 400kHz minimum, 1MHz suggested

Probes:
1-Wire requires a single signal, but some master implementations might have a
separate signal used to deliver power to the bus during temperature conversion
as an example. Changes to the power signal state are propagated to the next
protocol layer.

 - owr (1-Wire signal line)
 - pwr (optional, dedicated power supply pin)

Options:
Only one option is documented, it is possible to configure the decoder to
support overdrive mode or not (to avoid minimum samplerate warnings).

 - overdrive (selected '1' by defaullt)

1-Wire is an asynchronous protocol, so the decoder must know the samplerate.
The timing for sampling bits, presence, and reset is calculated by the decoder,
but in case the user wishes to use different values, it is possible to
configure them. This options are not documented, since they should be used only
if the master or slave requires timings slightly out of the standard, or if
extremely low sampling rates are used. So the user should read the decoder
source code if he/she wishes to use this options.

Protocol output format:
Protocol outputs are pairs [event, value]. The next event strings are
available, value is limited to 0 or 1:

 - 'RESET/PRESENCE' (the value is 1 if a presence pulse was received)
 - 'BIT' (the value holds the bit being written or read)
 - 'POWER' (power state changes to the current value are reported)

Annotations:
Two annotation options are available:

 - 'Text' (provides protocol events as short human readable text)
 - 'Timing' (protocol events are accompanied with timing details and warnings)

Link layer 'Text' annotations show the following events:

 - Reset/presence: <true/false>
   The event is marked from the signal falling edge to the end of the presence
   pulse or the window in which such a pulse is expected. It's also reported
   if there are any devices attached to the bus.
 - Bit: <1bit data>
   The event is marked from the signal negative edge to the end of the data
   slot. The value of each received bit is also provided.
 - Power: <applied/removed>
   Every change to the power state is reported.

TODO:
- Maybe add support for interrupts, check if this feature is deprecated.
'''

from .onewire_link import *


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
1-Wire protocol decoder (network layer).

The 1-Wire protocol enables bidirectional communication over a single wire
(and ground) between a single master and one or multiple slaves. The protocol
is layered:

 - Link layer (reset, presence detection, reading/writing bits)
 - Network layer (skip/search/match device ROM addresses)
 - Transport layer (transport data between 1-Wire master and device)

Network layer:

Options:
The only optuin can be used to configure the default 64bit device ROM in case
a'Skip ROM' command is issued by the master. In case a different device is
accessed it overrides the provided ROM option.

 - rom (0x0000000000000000 by defaullt)

Protocol output format:
Protocol outputs are pairs [event, value]. The next event strings are
available, the value is an integer:

 - 'RESET/PRESENCE' (the value is 1 if a presence pulse was received)
 - 'ROM' (the value is a 64bit ROM for the currently selected device)
 - 'DATA' (the value is a 8bit byte transport layer transfer)
 - 'POWER' (power state changes, forwarded from the link layer)

Annotations:
Only one annotation option is available:

 - 'Text' (provides protocol events as short human readable text)

Network layer 'Text' annotations show the following events:

 - Reset/presence: <true/false>
   The event is marked from the signal falling edge to the end of the presence
   pulse or the window in which such a pulse is expected. It's also reported
   if there are any devices attached to the bus.
 - ROM command: <val> <name>
   The requested ROM command is reported as an 8bit hex value and by name.
 - ROM: <64bit value>
   The event is marked from the first to the last ROM bit. For now the
   conflicts from the search algorithm are not shown, only the selected ROM.
 - Data: <8bit value>
   8bit hex raw data bytes for the transport layer are reported.
 - CRC check: <8bit value> <match/error>
   The status of the 8bit CRC check for the 64bit ROM is reported.
 - ROM error data: <8bit value>
   8bit hex raw data bytes in case the ROM command is not recognized.
 - Power: <applied/removed>
   Every change to the power state is reported.

TODO:
 - Add proper support for 'Skip ROM' command, currently does not fit well
   into the transport layer.
 - Add reporting original/complement address values from the search algorithm.
'''

from .onewire_network import *


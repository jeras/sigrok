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

'''
1-Wire protocol decoder.

The 1-Wire protocol enables bidirectional communication over a single wire (and
ground) between a single master and one or multiple slaves. The protocol is
layered, the provided parser decodes the next layers:
- Link layer (reset, presence detection, reading/writing bits)
- Network layer (skip, search, match device ROM addresses)
The higher layers (transport, presentation) are not decoded, since they are
mostly device specific and it would take a lot of code to interpret them.

Probes:
1-Wire requires a single signal, but some master implementations might have a
separate signal use to deliver power to the bus during temperature conversion
as an example. This power signal is currently not parsed.
- owr (1-Wire bus)
- pwr (1-Wire power)

Options:
1-Wire is an asynchronous protocol, so the decoder must know the sample rate.
The timing for sampling bits, presence and reset is calculated by the decoder,
but in case the user wishes to use different values, it is possible to
configure the next timing values (number of sample rate periods):
- overdrive              (if active the decoder will be prepared for overdrive)
- cnt_normal_bit         (time for normal mode sample bit)
- cnt_normal_presence    (time for normal mode sample presence)
- cnt_normal_reset       (time for normal mode reset)
- cnt_overdrive_bit      (time for overdrive mode sample bit)
- cnt_overdrive_presence (time for overdrive mode sample presence)
- cnt_overdrive_reset    (time for overdrive mode reset)
This options should be configured only on very rare cases and the user should
read the decoder source code to understand them correctly.

Annotations:
Annotations can be shown for each layer of the protocol separately:
- link (the value of each transmitted bit is shown separately)
- network (the ROM command, and address are shown)
- transport (only transport layer byte transfers are shown)
If link layer annotations are shown, possible issues with sample rate and sample
timing are also shown.

TODO:
- fix annotations to have event duration instead of begin end time
- add CRC checks for network layer
- add transport layer code
- review link layer code, to check for protocol correctness
'''

from .onewire import *


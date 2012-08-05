/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <inttypes.h>
#include <glib.h>
#include <libusb.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "config.h"
#include "dso.h"


/* Max time in ms before we want to check on USB events */
/* TODO tune this properly */
#define TICK    1

static const int hwcaps[] = {
	SR_HWCAP_OSCILLOSCOPE,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	SR_HWCAP_TIMEBASE,
	SR_HWCAP_BUFFERSIZE,
	SR_HWCAP_TRIGGER_SOURCE,
	SR_HWCAP_TRIGGER_SLOPE,
	SR_HWCAP_HORIZ_TRIGGERPOS,
	SR_HWCAP_FILTER,
	SR_HWCAP_VDIV,
	SR_HWCAP_COUPLING,
	0,
};

static const char *probe_names[] = {
	"CH1",
	"CH2",
	NULL,
};

static const struct dso_profile dev_profiles[] = {
	{	0x04b4, 0x2090, 0x04b5, 0x2090,
		"Hantek", "DSO-2090",
		FIRMWARE_DIR "/hantek-dso-2xxx.fw" },
	{	0x04b4, 0x2150, 0x04b5, 0x2150,
		"Hantek", "DSO-2150",
		FIRMWARE_DIR "/hantek-dso-2xxx.fw" },
	{	0x04b4, 0x2250, 0x04b5, 0x2250,
		"Hantek", "DSO-2250",
		FIRMWARE_DIR "/hantek-dso-2xxx.fw" },
	{	0x04b4, 0x5200, 0x04b5, 0x5200,
		"Hantek", "DSO-5200",
		FIRMWARE_DIR "/hantek-dso-5xxx.fw" },
	{	0x04b4, 0x520a, 0x04b5, 0x520a,
		"Hantek", "DSO-5200A",
		FIRMWARE_DIR "/hantek-dso-5xxx.fw" },
	{ 0, 0, 0, 0, 0, 0, 0 },
};

static const uint64_t buffersizes[] = {
	10240,
	32768,
	/* TODO: 65535 */
	0,
};

static const struct sr_rational timebases[] = {
	/* microseconds */
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 40, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 400, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 4, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 40, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 400, 1000 },
	{ 0, 0},
};

static const struct sr_rational vdivs[] = {
	/* millivolts */
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 0, 0 },
};

static const char *trigger_sources[] = {
	"CH1",
	"CH2",
	"EXT",
	/* TODO: forced */
	NULL,
};

static const char *filter_targets[] = {
	"CH1",
	"CH2",
	/* TODO: "TRIGGER", */
	NULL,
};

static const char *coupling[] = {
	"AC",
	"DC",
	"GND",
	NULL,
};

SR_PRIV struct sr_dev_driver hantek_dso_driver_info;
static struct sr_dev_driver *hdi = &hantek_dso_driver_info;
static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data);

static struct sr_dev_inst *dso_dev_new(int index, const struct dso_profile *prof)
{
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	struct drv_context *drvc;
	struct dev_context *devc;
	int i;

	sdi = sr_dev_inst_new(index, SR_ST_INITIALIZING,
		prof->vendor, prof->model, NULL);
	if (!sdi)
		return NULL;
	sdi->driver = hdi;

	/* Add only the real probes -- EXT isn't a source of data, only
	 * a trigger source internal to the device.
	 */
	for (i = 0; probe_names[i]; i++) {
		if (!(probe = sr_probe_new(i, SR_PROBE_ANALOG, TRUE,
				probe_names[i])))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("hantek-dso: devc malloc failed");
		return NULL;
	}
	devc->profile = prof;
	devc->dev_state = IDLE;
	devc->timebase = DEFAULT_TIMEBASE;
	devc->ch1_enabled = TRUE;
	devc->ch2_enabled = TRUE;
	devc->voltage_ch1 = DEFAULT_VOLTAGE;
	devc->voltage_ch2 = DEFAULT_VOLTAGE;
	devc->coupling_ch1 = DEFAULT_COUPLING;
	devc->coupling_ch2 = DEFAULT_COUPLING;
	devc->voffset_ch1 = DEFAULT_VERT_OFFSET;
	devc->voffset_ch2 = DEFAULT_VERT_OFFSET;
	devc->voffset_trigger = DEFAULT_VERT_TRIGGERPOS;
	devc->framesize = DEFAULT_FRAMESIZE;
	devc->triggerslope = SLOPE_POSITIVE;
	devc->triggersource = g_strdup(DEFAULT_TRIGGER_SOURCE);
	devc->triggerposition = DEFAULT_HORIZ_TRIGGERPOS;
	sdi->priv = devc;
	drvc = hdi->priv;
	drvc->instances = g_slist_append(drvc->instances, sdi);

	return sdi;
}

static int configure_probes(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const struct sr_probe *probe;
	const GSList *l;

	devc = sdi->priv;

	devc->ch1_enabled = devc->ch2_enabled = FALSE;
	for (l = sdi->probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->index == 0)
			devc->ch1_enabled = probe->enabled;
		else if (probe->index == 1)
			devc->ch2_enabled = probe->enabled;
	}

	return SR_OK;
}

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;

	drvc = hdi->priv;
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("hantek-dso: %s: sdi was NULL, continuing", __func__);
			continue;
		}
		if (!(devc = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("hantek-dso: %s: sdi->priv was NULL, continuing", __func__);
			continue;
		}
		dso_close(sdi);
		sr_usb_dev_inst_free(devc->usb);
		g_free(devc->triggersource);

		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(void)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("hantek-dso: driver context malloc failed.");
		return SR_ERR;
	}

	if (libusb_init(&drvc->usb_context) != 0) {
		g_free(drvc);
		sr_err("hantek-dso: Failed to initialize USB.");
		return SR_ERR;
	}

	hdi->priv = drvc;

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	const struct dso_profile *prof;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int devcnt, ret, i, j;

	(void)options;
	devcnt = 0;
	devices = 0;
	drvc = hdi->priv;
	drvc->instances = NULL;

	clear_instances();

	/* Find all Hantek DSO devices and upload firmware to all of them. */
	libusb_get_device_list(drvc->usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("hantek-dso: failed to get device descriptor: %d", ret);
			continue;
		}

		prof = NULL;
		for (j = 0; dev_profiles[j].orig_vid; j++) {
			if (des.idVendor == dev_profiles[j].orig_vid
				&& des.idProduct == dev_profiles[j].orig_pid) {
				/* Device matches the pre-firmware profile. */
				prof = &dev_profiles[j];
				sr_dbg("hantek-dso: Found a %s %s.", prof->vendor, prof->model);
				sdi = dso_dev_new(devcnt, prof);
				devices = g_slist_append(devices, sdi);
				devc = sdi->priv;
				if (ezusb_upload_firmware(devlist[i], USB_CONFIGURATION,
						prof->firmware) == SR_OK)
					/* Remember when the firmware on this device was updated */
					devc->fw_updated = g_get_monotonic_time();
				else
					sr_err("hantek-dso: firmware upload failed for "
					       "device %d", devcnt);
				/* Dummy USB address of 0xff will get overwritten later. */
				devc->usb = sr_usb_dev_inst_new(
						libusb_get_bus_number(devlist[i]), 0xff, NULL);
				devcnt++;
				break;
			} else if (des.idVendor == dev_profiles[j].fw_vid
				&& des.idProduct == dev_profiles[j].fw_pid) {
				/* Device matches the post-firmware profile. */
				prof = &dev_profiles[j];
				sr_dbg("hantek-dso: Found a %s %s.", prof->vendor, prof->model);
				sdi = dso_dev_new(devcnt, prof);
				sdi->status = SR_ST_INACTIVE;
				devices = g_slist_append(devices, sdi);
				devc = sdi->priv;
				devc->usb = sr_usb_dev_inst_new(
						libusb_get_bus_number(devlist[i]),
						libusb_get_device_address(devlist[i]), NULL);
				devcnt++;
				break;
			}
		}
		if (!prof)
			/* not a supported VID/PID */
			continue;
	}
	libusb_free_device_list(devlist, 1);

	return devices;
}

static GSList *hw_dev_list(void)
{
	struct drv_context *drvc;

	drvc = hdi->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int64_t timediff_us, timediff_ms;
	int err;

	devc = sdi->priv;

	/*
	 * if the firmware was recently uploaded, wait up to MAX_RENUM_DELAY_MS
	 * for the FX2 to renumerate
	 */
	err = SR_ERR;
	if (devc->fw_updated > 0) {
		sr_info("hantek-dso: waiting for device to reset");
		/* takes at least 300ms for the FX2 to be gone from the USB bus */
		g_usleep(300 * 1000);
		timediff_ms = 0;
		while (timediff_ms < MAX_RENUM_DELAY_MS) {
			if ((err = dso_open(sdi)) == SR_OK)
				break;
			g_usleep(100 * 1000);
			timediff_us = g_get_monotonic_time() - devc->fw_updated;
			timediff_ms = timediff_us / 1000;
			sr_spew("hantek-dso: waited %" PRIi64 " ms", timediff_ms);
		}
		sr_info("hantek-dso: device came back after %d ms", timediff_ms);
	} else {
		err = dso_open(sdi);
	}

	if (err != SR_OK) {
		sr_err("hantek-dso: unable to open device");
		return SR_ERR;
	}

	err = libusb_claim_interface(devc->usb->devhdl, USB_INTERFACE);
	if (err != 0) {
		sr_err("hantek-dso: Unable to claim interface: %d", err);
		return SR_ERR;
	}

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{

	dso_close(sdi);

	return SR_OK;
}

static int hw_cleanup(void)
{
	struct drv_context *drvc;

	if (!(drvc = hdi->priv))
		return SR_OK;

	clear_instances();

	if (drvc->usb_context)
		libusb_exit(drvc->usb_context);
	drvc->usb_context = NULL;

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
       const struct sr_dev_inst *sdi)
{
	uint64_t tmp;

	(void)sdi;

	switch (info_id) {
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_NUM_PROBES:
		*data = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
		break;
	case SR_DI_BUFFERSIZES:
		*data = buffersizes;
		break;
	case SR_DI_TIMEBASES:
		*data = timebases;
		break;
	case SR_DI_TRIGGER_SOURCES:
		*data = trigger_sources;
		break;
	case SR_DI_FILTERS:
		*data = filter_targets;
		break;
	case SR_DI_VDIVS:
		*data = vdivs;
		break;
	case SR_DI_COUPLING:
		*data = coupling;
		break;
	/* TODO remove this */
	case SR_DI_CUR_SAMPLERATE:
		*data = &tmp;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	struct dev_context *devc;
	struct sr_rational tmp_rat;
	float tmp_float;
	uint64_t tmp_u64;
	int ret, i;
	char **targets;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	ret = SR_OK;
	devc = sdi->priv;
	switch (hwcap) {
	case SR_HWCAP_LIMIT_FRAMES:
		devc->limit_frames = *(const uint64_t *)value;
		break;
	case SR_HWCAP_TRIGGER_SLOPE:
		tmp_u64 = *(const int *)value;
		if (tmp_u64 != SLOPE_NEGATIVE && tmp_u64 != SLOPE_POSITIVE)
			ret = SR_ERR_ARG;
		devc->triggerslope = tmp_u64;
		break;
	case SR_HWCAP_HORIZ_TRIGGERPOS:
		tmp_float = *(const float *)value;
		if (tmp_float < 0.0 || tmp_float > 1.0) {
			sr_err("hantek-dso: trigger position should be between 0.0 and 1.0");
			ret = SR_ERR_ARG;
		} else
			devc->triggerposition = tmp_float;
		break;
	case SR_HWCAP_BUFFERSIZE:
		tmp_u64 = *(const int *)value;
		for (i = 0; buffersizes[i]; i++) {
			if (buffersizes[i] == tmp_u64) {
				devc->framesize = tmp_u64;
				break;
			}
		}
		if (buffersizes[i] == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_HWCAP_TIMEBASE:
		tmp_rat = *(const struct sr_rational *)value;
		for (i = 0; timebases[i].p && timebases[i].q; i++) {
			if (timebases[i].p == tmp_rat.p
					&& timebases[i].q == tmp_rat.q) {
				devc->timebase = i;
				break;
			}
		}
		if (timebases[i].p == 0 && timebases[i].q == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_HWCAP_TRIGGER_SOURCE:
		for (i = 0; trigger_sources[i]; i++) {
			if (!strcmp(value, trigger_sources[i])) {
				devc->triggersource = g_strdup(value);
				break;
			}
		}
		if (trigger_sources[i] == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_HWCAP_FILTER:
		devc->filter_ch1 = devc->filter_ch2 = devc->filter_trigger = 0;
		targets = g_strsplit(value, ",", 0);
		for (i = 0; targets[i]; i++) {
			if (targets[i] == '\0')
				/* Empty filter string can be used to clear them all. */
				;
			else if (!strcmp(targets[i], "CH1"))
				devc->filter_ch1 = TRUE;
			else if (!strcmp(targets[i], "CH2"))
				devc->filter_ch2 = TRUE;
			else if (!strcmp(targets[i], "TRIGGER"))
				devc->filter_trigger = TRUE;
			else {
				sr_err("invalid filter target %s", targets[i]);
				ret = SR_ERR_ARG;
			}
		}
		g_strfreev(targets);
		break;
	case SR_HWCAP_VDIV:
		/* TODO not supporting vdiv per channel yet */
		tmp_rat = *(const struct sr_rational *)value;
		for (i = 0; vdivs[i].p && vdivs[i].q; i++) {
			if (vdivs[i].p == tmp_rat.p
					&& vdivs[i].q == tmp_rat.q) {
				devc->voltage_ch1 = i;
				devc->voltage_ch2 = i;
				break;
			}
		}
		if (vdivs[i].p == 0 && vdivs[i].q == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_HWCAP_COUPLING:
		/* TODO not supporting coupling per channel yet */
		for (i = 0; coupling[i]; i++) {
			if (!strcmp(value, coupling[i])) {
				devc->coupling_ch1 = i;
				devc->coupling_ch2 = i;
				break;
			}
		}
		if (coupling[i] == 0)
			ret = SR_ERR_ARG;
		break;
	default:
		ret = SR_ERR_ARG;
	}

	return ret;
}

static void send_chunk(struct dev_context *devc, unsigned char *buf,
		int num_samples)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	float ch1, ch2, range;
	int num_probes, data_offset, i;

	num_probes = (devc->ch1_enabled && devc->ch2_enabled) ? 2 : 1;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	/* TODO: support for 5xxx series 9-bit samples */
	analog.num_samples = num_samples;
	analog.mq = SR_MQ_VOLTAGE;
	analog.unit = SR_UNIT_VOLT;
	analog.data = g_try_malloc(analog.num_samples * sizeof(float) * num_probes);
	data_offset = 0;
	for (i = 0; i < analog.num_samples; i++) {
		/* The device always sends data for both channels. If a channel
		 * is disabled, it contains a copy of the enabled channel's
		 * data. However, we only send the requested channels to the bus.
		 *
		 * Voltage values are encoded as a value 0-255 (0-512 on the 5200*),
		 * where the value is a point in the range represented by the vdiv
		 * setting. There are 8 vertical divs, so e.g. 500mV/div represents
		 * 4V peak-to-peak where 0 = -2V and 255 = +2V.
		 */
		/* TODO: support for 5xxx series 9-bit samples */
		if (devc->ch1_enabled) {
			range = ((float)vdivs[devc->voltage_ch1].p / vdivs[devc->voltage_ch1].q) * 8;
			ch1 = range / 255 * *(buf + i * 2 + 1);
			/* Value is centered around 0V. */
			ch1 -= range / 2;
			analog.data[data_offset++] = ch1;
		}
		if (devc->ch2_enabled) {
			range = ((float)vdivs[devc->voltage_ch2].p / vdivs[devc->voltage_ch2].q) * 8;
			ch2 = range / 255 * *(buf + i * 2);
			ch2 -= range / 2;
			analog.data[data_offset++] = ch2;
		}
	}
	sr_session_send(devc->cb_data, &packet);

}

/* Called by libusb (as triggered by handle_event()) when a transfer comes in.
 * Only channel data comes in asynchronously, and all transfers for this are
 * queued up beforehand, so this just needs so chuck the incoming data onto
 * the libsigrok session bus.
 */
static void receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;
	int num_samples, pre;

	devc = transfer->user_data;
	sr_dbg("hantek-dso: receive_transfer(): status %d received %d bytes",
			transfer->status, transfer->actual_length);

	if (transfer->actual_length == 0)
		/* Nothing to send to the bus. */
		return;

	num_samples = transfer->actual_length / 2;

	sr_dbg("hantek-dso: got %d-%d/%d samples in frame", devc->samp_received + 1,
			devc->samp_received + num_samples, devc->framesize);

	/* The device always sends a full frame, but the beginning of the frame
	 * doesn't represent the trigger point. The offset at which the trigger
	 * happened came in with the capture state, so we need to start sending
	 * from there up the session bus. The samples in the frame buffer before
	 * that trigger point came after the end of the device's frame buffer was
	 * reached, and it wrapped around to overwrite up until the trigger point.
	 */
	if (devc->samp_received < devc->trigger_offset) {
		/* Trigger point not yet reached. */
		if (devc->samp_received + num_samples < devc->trigger_offset) {
			/* The entire chunk is before the trigger point. */
			memcpy(devc->framebuf + devc->samp_buffered * 2,
					transfer->buffer, num_samples * 2);
			devc->samp_buffered += num_samples;
		} else {
			/* This chunk hits or overruns the trigger point.
			 * Store the part before the trigger fired, and
			 * send the rest up to the session bus. */
			pre = devc->trigger_offset - devc->samp_received;
			memcpy(devc->framebuf + devc->samp_buffered * 2,
					transfer->buffer, pre * 2);
			devc->samp_buffered += pre;

			/* The rest of this chunk starts with the trigger point. */
			sr_dbg("hantek-dso: reached trigger point, %d samples buffered",
					devc->samp_buffered);

			/* Avoid the corner case where the chunk ended at
			 * exactly the trigger point. */
			if (num_samples > pre)
				send_chunk(devc, transfer->buffer + pre * 2,
						num_samples - pre);
		}
	} else {
		/* Already past the trigger point, just send it all out. */
		send_chunk(devc, transfer->buffer,
				num_samples);
	}

	devc->samp_received += num_samples;

	/* Everything in this transfer was either copied to the buffer or
	 * sent to the session bus. */
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);

	if (devc->samp_received >= devc->framesize) {
		/* That was the last chunk in this frame. Send the buffered
		 * pre-trigger samples out now, in one big chunk. */
		sr_dbg("hantek-dso: end of frame, sending %d pre-trigger buffered samples",
				devc->samp_buffered);
		send_chunk(devc, devc->framebuf, devc->samp_buffered);

		/* Mark the end of this frame. */
		packet.type = SR_DF_FRAME_END;
		sr_session_send(devc->cb_data, &packet);

		if (devc->limit_frames && ++devc->num_frames == devc->limit_frames) {
			/* Terminate session */
			devc->dev_state = STOPPING;
		} else {
			devc->dev_state = NEW_CAPTURE;
		}
	}

}

static int handle_event(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct sr_datafeed_packet packet;
	struct timeval tv;
	struct drv_context *drvc;
	struct dev_context *devc;
	const struct libusb_pollfd **lupfd;
	int num_probes, i;
	uint32_t trigger_offset;
	uint8_t capturestate;

	/* Avoid compiler warnings. */
	(void)fd;
	(void)revents;

	drvc = hdi->priv;
	sdi = cb_data;
	devc = sdi->priv;
	if (devc->dev_state == STOPPING) {
		/* We've been told to wind up the acquisition. */
		sr_dbg("hantek-dso: stopping acquisition");
		/* TODO: doesn't really cancel pending transfers so they might
		 * come in after SR_DF_END is sent. */
		lupfd = libusb_get_pollfds(drvc->usb_context);
		for (i = 0; lupfd[i]; i++)
			sr_source_remove(lupfd[i]->fd);
		free(lupfd);

		packet.type = SR_DF_END;
		sr_session_send(sdi, &packet);

		devc->dev_state = IDLE;

		return TRUE;
	}

	/* Always handle pending libusb events. */
	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->usb_context, &tv);

	/* TODO: ugh */
	if (devc->dev_state == NEW_CAPTURE) {
		if (dso_capture_start(devc) != SR_OK)
			return TRUE;
		if (dso_enable_trigger(devc) != SR_OK)
			return TRUE;
//		if (dso_force_trigger(devc) != SR_OK)
//			return TRUE;
		sr_dbg("hantek-dso: successfully requested next chunk");
		devc->dev_state = CAPTURE;
		return TRUE;
	}
	if (devc->dev_state != CAPTURE)
		return TRUE;

	if ((dso_get_capturestate(devc, &capturestate, &trigger_offset)) != SR_OK)
		return TRUE;

	sr_dbg("hantek-dso: capturestate %d", capturestate);
	sr_dbg("hantek-dso: trigger offset 0x%.6x", trigger_offset);
	switch (capturestate) {
	case CAPTURE_EMPTY:
		if (++devc->capture_empty_count >= MAX_CAPTURE_EMPTY) {
			devc->capture_empty_count = 0;
			if (dso_capture_start(devc) != SR_OK)
				break;
			if (dso_enable_trigger(devc) != SR_OK)
				break;
//			if (dso_force_trigger(devc) != SR_OK)
//				break;
			sr_dbg("hantek-dso: successfully requested next chunk");
		}
		break;
	case CAPTURE_FILLING:
		/* no data yet */
		break;
	case CAPTURE_READY_8BIT:
		/* Remember where in the captured frame the trigger is. */
		devc->trigger_offset = trigger_offset;

		num_probes = (devc->ch1_enabled && devc->ch2_enabled) ? 2 : 1;
		devc->framebuf = g_try_malloc(devc->framesize * num_probes * 2);
		devc->samp_buffered = devc->samp_received = 0;

		/* Tell the scope to send us the first frame. */
		if (dso_get_channeldata(devc, receive_transfer) != SR_OK)
			break;

		/* Don't hit the state machine again until we're done fetching
		 * the data we just told the scope to send.
		 */
		devc->dev_state = FETCH_DATA;

		/* Tell the frontend a new frame is on the way. */
		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);
		break;
	case CAPTURE_READY_9BIT:
		/* TODO */
		sr_err("not yet supported");
		break;
	case CAPTURE_TIMEOUT:
		/* Doesn't matter, we'll try again next time. */
		break;
	default:
		sr_dbg("unknown capture state");
	}

	return TRUE;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	const struct libusb_pollfd **lupfd;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct drv_context *drvc;
	struct dev_context *devc;
	int i;

	drvc = hdi->priv;
	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	devc = sdi->priv;
	devc->cb_data = cb_data;

	if (configure_probes(sdi) != SR_OK) {
		sr_err("hantek-dso: failed to configured probes");
		return SR_ERR;
	}

	if (dso_init(devc) != SR_OK)
		return SR_ERR;

	if (dso_capture_start(devc) != SR_OK)
		return SR_ERR;

	devc->dev_state = CAPTURE;
	lupfd = libusb_get_pollfds(drvc->usb_context);
	for (i = 0; lupfd[i]; i++)
		sr_source_add(lupfd[i]->fd, lupfd[i]->events, TICK, handle_event,
				(void *)sdi);
	free(lupfd);

	/* Send header packet to the session bus. */
	packet.type = SR_DF_HEADER;
	packet.payload = (unsigned char *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(cb_data, &packet);

	/* Send metadata about the SR_DF_ANALOG packets to come. */
	packet.type = SR_DF_META_ANALOG;
	packet.payload = &meta;
	meta.num_probes = NUM_PROBES;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	devc = sdi->priv;
	devc->dev_state = STOPPING;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver hantek_dso_driver_info = {
	.name = "hantek-dso",
	.longname = "Hantek DSO",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

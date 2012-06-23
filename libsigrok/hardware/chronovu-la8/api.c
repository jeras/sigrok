/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011-2012 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <ftdi.h>
#include <glib.h>
#include <string.h>
#include "sigrok.h"
#include "sigrok-internal.h"
#include "driver.h"

static GSList *dev_insts = NULL;

/* Function prototypes. */
static int hw_dev_acquisition_stop(int dev_index, void *cb_data);

static int hw_init(const char *devinfo)
{
	int ret;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	/* Avoid compiler errors. */
	(void)devinfo;

	/* Allocate memory for our private driver context. */
	if (!(ctx = g_try_malloc(sizeof(struct context)))) {
		sr_err("la8: %s: struct context malloc failed", __func__);
		goto err_free_nothing;
	}

	/* Set some sane defaults. */
	ctx->ftdic = NULL;
	ctx->cur_samplerate = SR_MHZ(100); /* 100MHz == max. samplerate */
	ctx->limit_msec = 0;
	ctx->limit_samples = 0;
	ctx->session_dev_id = NULL;
	memset(ctx->mangled_buf, 0, BS);
	ctx->final_buf = NULL;
	ctx->trigger_pattern = 0x00; /* Value irrelevant, see trigger_mask. */
	ctx->trigger_mask = 0x00; /* All probes are "don't care". */
	ctx->trigger_timeout = 10; /* Default to 10s trigger timeout. */
	ctx->trigger_found = 0;
	ctx->done = 0;
	ctx->block_counter = 0;
	ctx->divcount = 0; /* 10ns sample period == 100MHz samplerate */

	/* Allocate memory where we'll store the de-mangled data. */
	if (!(ctx->final_buf = g_try_malloc(SDRAM_SIZE))) {
		sr_err("la8: %s: final_buf malloc failed", __func__);
		goto err_free_ctx;
	}

	/* Allocate memory for the FTDI context (ftdic) and initialize it. */
	if (!(ctx->ftdic = ftdi_new())) {
		sr_err("la8: %s: ftdi_new failed", __func__);
		goto err_free_final_buf;
	}

	/* Check for the device and temporarily open it. */
	if ((ret = ftdi_usb_open_desc(ctx->ftdic, USB_VENDOR_ID,
			USB_PRODUCT_ID, USB_DESCRIPTION, NULL)) < 0) {
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
		goto err_free_ftdic;
	}
	sr_dbg("la8: Found LA8 device (%04x:%04x).", USB_VENDOR_ID,
	       USB_PRODUCT_ID);

	/* Register the device with libsigrok. */
	sdi = sr_dev_inst_new(0, SR_ST_INITIALIZING,
			USB_VENDOR_NAME, USB_MODEL_NAME, USB_MODEL_VERSION);
	if (!sdi) {
		sr_err("la8: %s: sr_dev_inst_new failed", __func__);
		goto err_close_ftdic;
	}

	sdi->priv = ctx;

	dev_insts = g_slist_append(dev_insts, sdi);

	sr_spew("la8: Device init successful.");

	/* Close device. We'll reopen it again when we need it. */
	(void) la8_close(ctx); /* Log, but ignore errors. */

	return 1;

err_close_ftdic:
	(void) la8_close(ctx); /* Log, but ignore errors. */
err_free_ftdic:
	free(ctx->ftdic); /* NOT g_free()! */
err_free_final_buf:
	g_free(ctx->final_buf);
err_free_ctx:
	g_free(ctx);
err_free_nothing:

	return 0;
}

static int hw_dev_open(int dev_index)
{
	int ret;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_dbg("la8: Opening LA8 device (%04x:%04x).", USB_VENDOR_ID,
	       USB_PRODUCT_ID);

	/* Open the device. */
	if ((ret = ftdi_usb_open_desc(ctx->ftdic, USB_VENDOR_ID,
			USB_PRODUCT_ID, USB_DESCRIPTION, NULL)) < 0) {
		sr_err("la8: %s: ftdi_usb_open_desc: (%d) %s",
		       __func__, ret, ftdi_get_error_string(ctx->ftdic));
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
		return SR_ERR;
	}
	sr_dbg("la8: Device opened successfully.");

	/* Purge RX/TX buffers in the FTDI chip. */
	if ((ret = ftdi_usb_purge_buffers(ctx->ftdic)) < 0) {
		sr_err("la8: %s: ftdi_usb_purge_buffers: (%d) %s",
		       __func__, ret, ftdi_get_error_string(ctx->ftdic));
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("la8: FTDI buffers purged successfully.");

	/* Enable flow control in the FTDI chip. */
	if ((ret = ftdi_setflowctrl(ctx->ftdic, SIO_RTS_CTS_HS)) < 0) {
		sr_err("la8: %s: ftdi_setflowcontrol: (%d) %s",
		       __func__, ret, ftdi_get_error_string(ctx->ftdic));
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("la8: FTDI flow control enabled successfully.");

	/* Wait 100ms. */
	g_usleep(100 * 1000);

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;

err_dev_open_close_ftdic:
	(void) la8_close(ctx); /* Log, but ignore errors. */
	return SR_ERR;
}

static int hw_dev_close(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_dbg("la8: Closing device.");

	if (sdi->status == SR_ST_ACTIVE) {
		sr_dbg("la8: Status ACTIVE, closing device.");
		/* TODO: Really ignore errors here, or return SR_ERR? */
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
	} else {
		sr_spew("la8: Status not ACTIVE, nothing to do.");
	}

	sdi->status = SR_ST_INACTIVE;

	sr_dbg("la8: Freeing sample buffer.");
	g_free(ctx->final_buf);

	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	int ret = SR_OK;

	/* Properly close all devices. */
	for (l = dev_insts; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("la8: %s: sdi was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		sr_dev_inst_free(sdi); /* Returns void. */
	}
	g_slist_free(dev_insts); /* Returns void. */
	dev_insts = NULL;

	return ret;
}

static const void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	const void *info;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return NULL;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return NULL;
	}

	sr_spew("la8: %s: dev_index %d, dev_info_id %d.", __func__,
		dev_index, dev_info_id);

	switch (dev_info_id) {
	case SR_DI_INST:
		info = sdi;
		sr_spew("la8: %s: Returning sdi.", __func__);
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		sr_spew("la8: %s: Returning number of probes: %d.", __func__,
			NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		info = probe_names;
		sr_spew("la8: %s: Returning probenames.", __func__);
		break;
	case SR_DI_SAMPLERATES:
		fill_supported_samplerates_if_needed();
		info = &samplerates;
		sr_spew("la8: %s: Returning samplerates.", __func__);
		break;
	case SR_DI_TRIGGER_TYPES:
		info = (char *)TRIGGER_TYPES;
		sr_spew("la8: %s: Returning trigger types: %s.", __func__,
			TRIGGER_TYPES);
		break;
	case SR_DI_CUR_SAMPLERATE:
		info = &ctx->cur_samplerate;
		sr_spew("la8: %s: Returning samplerate: %" PRIu64 "Hz.",
			__func__, ctx->cur_samplerate);
		break;
	default:
		/* Unknown device info ID, return NULL. */
		sr_err("la8: %s: Unknown device info ID", __func__);
		info = NULL;
		break;
	}

	return info;
}

static int hw_dev_status_get(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL, device not found", __func__);
		return SR_ST_NOT_FOUND;
	}

	sr_dbg("la8: Returning status: %d.", sdi->status);

	return sdi->status;
}

static const int *hw_hwcap_get_all(void)
{
	sr_spew("la8: Returning list of device capabilities.");

	return hwcaps;
}

static int hw_dev_config_set(int dev_index, int hwcap, const void *value)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_spew("la8: %s: dev_index %d, hwcap %d", __func__, dev_index, hwcap);

	switch (hwcap) {
	case SR_HWCAP_SAMPLERATE:
		if (set_samplerate(sdi, *(const uint64_t *)value) == SR_ERR) {
			sr_err("la8: %s: setting samplerate failed.", __func__);
			return SR_ERR;
		}
		sr_dbg("la8: SAMPLERATE = %" PRIu64, ctx->cur_samplerate);
		break;
	case SR_HWCAP_PROBECONFIG:
		if (configure_probes(ctx, (const GSList *)value) != SR_OK) {
			sr_err("la8: %s: probe config failed.", __func__);
			return SR_ERR;
		}
		break;
	case SR_HWCAP_LIMIT_MSEC:
		if (*(const uint64_t *)value == 0) {
			sr_err("la8: %s: LIMIT_MSEC can't be 0.", __func__);
			return SR_ERR;
		}
		ctx->limit_msec = *(const uint64_t *)value;
		sr_dbg("la8: LIMIT_MSEC = %" PRIu64, ctx->limit_msec);
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
		if (*(const uint64_t *)value < MIN_NUM_SAMPLES) {
			sr_err("la8: %s: LIMIT_SAMPLES too small.", __func__);
			return SR_ERR;
		}
		ctx->limit_samples = *(const uint64_t *)value;
		sr_dbg("la8: LIMIT_SAMPLES = %" PRIu64, ctx->limit_samples);
		break;
	default:
		/* Unknown capability, return SR_ERR. */
		sr_err("la8: %s: Unknown capability.", __func__);
		return SR_ERR;
		break;
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	int i, ret;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	/* Avoid compiler errors. */
	(void)fd;
	(void)revents;

	if (!(sdi = cb_data)) {
		sr_err("la8: %s: cb_data was NULL", __func__);
		return FALSE;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return FALSE;
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return FALSE;
	}

	/* Get one block of data. */
	if ((ret = la8_read_block(ctx)) < 0) {
		sr_err("la8: %s: la8_read_block error: %d", __func__, ret);
		hw_dev_acquisition_stop(sdi->index, sdi);
		return FALSE;
	}

	/* We need to get exactly NUM_BLOCKS blocks (i.e. 8MB) of data. */
	if (ctx->block_counter != (NUM_BLOCKS - 1)) {
		ctx->block_counter++;
		return TRUE;
	}

	sr_dbg("la8: Sampling finished, sending data to session bus now.");

	/* All data was received and demangled, send it to the session bus. */
	for (i = 0; i < NUM_BLOCKS; i++)
		send_block_to_session_bus(ctx, i);

	hw_dev_acquisition_stop(sdi->index, sdi);

	// return FALSE; /* FIXME? */
	return TRUE;
}

static int hw_dev_acquisition_start(int dev_index, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_logic meta;
	uint8_t buf[4];
	int bytes_written;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return SR_ERR_BUG;
	}

	ctx->divcount = samplerate_to_divcount(ctx->cur_samplerate);
	if (ctx->divcount == 0xff) {
		sr_err("la8: %s: invalid divcount/samplerate", __func__);
		return SR_ERR;
	}

	sr_dbg("la8: Starting acquisition.");

	/* Fill acquisition parameters into buf[]. */
	buf[0] = ctx->divcount;
	buf[1] = 0xff; /* This byte must always be 0xff. */
	buf[2] = ctx->trigger_pattern;
	buf[3] = ctx->trigger_mask;

	/* Start acquisition. */
	bytes_written = la8_write(ctx, buf, 4);

	if (bytes_written < 0) {
		sr_err("la8: Acquisition failed to start.");
		return SR_ERR;
	} else if (bytes_written != 4) {
		sr_err("la8: Acquisition failed to start.");
		return SR_ERR; /* TODO: Other error and return code? */
	}

	sr_dbg("la8: Acquisition started successfully.");

	ctx->session_dev_id = cb_data;

	/* Send header packet to the session bus. */
	sr_dbg("la8: Sending SR_DF_HEADER.");
	packet.type = SR_DF_HEADER;
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(ctx->session_dev_id, &packet);

	/* Send metadata about the SR_DF_LOGIC packets to come. */
	packet.type = SR_DF_META_LOGIC;
	packet.payload = &meta;
	meta.samplerate = ctx->cur_samplerate;
	meta.num_probes = NUM_PROBES;
	sr_session_send(ctx->session_dev_id, &packet);

	/* Time when we should be done (for detecting trigger timeouts). */
	ctx->done = (ctx->divcount + 1) * 0.08388608 + time(NULL)
			+ ctx->trigger_timeout;
	ctx->block_counter = 0;
	ctx->trigger_found = 0;

	/* Hook up a dummy handler to receive data from the LA8. */
	sr_source_add(-1, G_IO_IN, 0, receive_data, sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(int dev_index, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	struct sr_datafeed_packet packet;

	sr_dbg("la8: Stopping acquisition.");

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	/* Send end packet to the session bus. */
	sr_dbg("la8: Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver chronovu_la8_driver_info = {
	.name = "chronovu-la8",
	.longname = "ChronoVu LA8",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
};

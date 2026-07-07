// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

/**
 * TEST: kms histogram
 * Category: Display
 * Description: Test to verify histogram features.
 * Functionality: histogram
 * Mega feature: Display
 * Test category: functionality test
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "igt.h"
#include "igt_vec.h"
#include "igt_psr.h"

#ifdef HAVE_LIBGHE
#include <ghe/ghe.h>
#endif

#ifdef HAVE_LIBDPST
#include <dpst/DisplayPcDpst.h>

#define DPST_MAX_AGGRESSIVENESS    5
#endif

#define GLOBAL_HIST_DISABLE		0
#define GLOBAL_HIST_ENABLE		1
#define GLOBAL_HIST_DELAY		2
#define FLIP_COUNT			20
#define SQUARE_SIZE			100
#define SQUARE_OFFSET			100

/**
 * SUBTEST: global-basic
 * Description: Test to enable histogram, flip monochrome fbs, wait for
 *		histogram event and then read the histogram data
 *
 * SUBTEST: global-color
 * Description: Test to enable histogram, flip color fbs, wait for
 *		histogram event and then read the histogram data
 *
 * SUBTEST: algo-basic
 * Description: Test to enable histogram, flip monochrome fbs, wait for
 *		histogram event and then read the histogram data and enhance pixels by
 *		multiplying by a pixel factor using algo
 *
 * SUBTEST: algo-color
 * Description: Test to enable histogram, flip color fbs, wait for histogram event
 *		and then read the histogram data and enhance pixels by multiplying
 *		by a pixel factor using algo
 *
 * SUBTEST: dpst-basic
 * Description: Test to enable histogram, flip monochrome fbs, wait for histogram
 *              event and then read the histogram data and enhance pixels by multiplying
 *              by a pixel factor using DPST algorithm with brightness adjustment
 *
 * SUBTEST: dpst-color
 * Description: Test to enable histogram, flip color fbs, wait for histogram event
 *              and then read the histogram data and enhance pixels by multiplying
 *              by a pixel factor using DPST algorithm with brightness adjustment
 *
 * SUBTEST: dpst-color-selective-fetch
 * Description: Test to enable global histogram, update SF clip, read histogram blob
 *              and enhances the pixels by multiplying by a pixel factor using dpst algo
 *              with brightness adjustment
 */

IGT_TEST_DESCRIPTION("This test will verify the display histogram.");

typedef struct data {
	igt_display_t display;
	int drm_fd;
	int debugfs_fd;
	int bin_count;
	igt_fb_t fb[5];
	bool selective_fetch;
} data_t;

typedef void (*test_t)(data_t*, enum pipe, igt_output_t*, drmModePropertyBlobRes*);

static void get_histogram_caps(data_t *data, enum pipe pipe,
			       struct drm_histogram_caps *caps_out)
{
	drmModePropertyBlobRes *caps_blob;
	struct drm_histogram_caps *caps;
	uint64_t prop_id;

	prop_id = igt_crtc_get_prop(&data->display.crtcs[pipe], IGT_CRTC_HISTOGRAM_CAPS);
	igt_assert_f(prop_id, "No histogram caps property for pipe %s", kmstest_pipe_name(pipe));

	caps_blob = drmModeGetPropertyBlob(data->drm_fd, prop_id);
	igt_assert_f(caps_blob && caps_blob->data, "Failed to get histogram caps blob");

	caps = (struct drm_histogram_caps *)caps_blob->data;

	caps_out->histogram_mode = caps->histogram_mode;
	caps_out->bins_count = caps->bins_count;

	drmModeFreePropertyBlob(caps_blob);
}

static void configure_and_verify_histogram(data_t *data, enum pipe pipe,
					   struct drm_histogram_caps *caps,
					   bool enable)
{
	struct drm_histogram_config *config;
	drmModePropertyBlobRes *enable_blob;
	const char *state_str;
	int expected_value;
	igt_crtc_t *crtc;
	uint64_t prop_id;

	crtc = igt_crtc_for_pipe(&data->display, pipe);
	expected_value =  enable ? GLOBAL_HIST_ENABLE : GLOBAL_HIST_DISABLE;
	state_str = enable ? "enabled" : "disabled";

	/* Create and apply configuration */
	config = calloc(1, sizeof(*config));
	igt_assert(config);

	config->hist_mode_data = 0;
	config->nr_hist_mode_data = 0;
	config->hist_mode = DRM_MODE_HISTOGRAM_HSV_MAX_RGB;
	config->enable = expected_value;

	/* If SelectiveFetch test is enabled set the damage area */
	if (data->selective_fetch) {
		config->sf = true;
		config->clip.x1 = SQUARE_OFFSET;
		config->clip.y1 = SQUARE_OFFSET;
		config->clip.x2 = SQUARE_OFFSET + SQUARE_SIZE;
		config->clip.y2 = SQUARE_OFFSET + SQUARE_SIZE;
	}

	igt_crtc_replace_prop_blob(crtc, IGT_CRTC_HISTOGRAM_ENABLE, config, sizeof(*config));
	igt_display_commit2(&data->display, COMMIT_ATOMIC);
	free(config);

	/* Verify the state */
	prop_id = igt_crtc_get_prop(&data->display.crtcs[pipe], IGT_CRTC_HISTOGRAM_ENABLE);
	enable_blob = drmModeGetPropertyBlob(data->drm_fd, prop_id);
	igt_assert_f(enable_blob && enable_blob->data, "Failed to get histogram enable blob");

	config = (struct drm_histogram_config *)enable_blob->data;
	igt_assert_f(config->enable == expected_value, "Histogram not %s", state_str);

	igt_debug("Histogram successfully %s for pipe %s\n", state_str, kmstest_pipe_name(pipe));
	drmModeFreePropertyBlob(enable_blob);
}

static void enable_and_verify_global_histogram(data_t *data, enum pipe pipe)
{
	struct drm_histogram_caps caps;

	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	get_histogram_caps(data, pipe, &caps);

	data->bin_count = caps.bins_count;

	igt_info("Enabling histogram: mode=%d, bins=%d\n", caps.histogram_mode, caps.bins_count);

	configure_and_verify_histogram(data, pipe, &caps, true);
}

static void disable_and_verify_global_histogram(data_t *data, igt_output_t *output, enum pipe pipe)
{
	struct drm_histogram_caps caps;

	get_histogram_caps(data, pipe, &caps);
	
	igt_debug("Disabling histogram: mode=%d, bins=%d\n", caps.histogram_mode, caps.bins_count);

	configure_and_verify_histogram(data, pipe, &caps, false);

	/* Clear primary plane */
	igt_plane_set_fb(igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY), NULL);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);
}

static bool is_global_histogram_enabled(data_t *data, enum pipe pipe)
{
	drmModePropertyBlobRes *histogram_enable_blob;
	struct drm_histogram_config *histogram_config;
	uint64_t prop_id;
	bool enabled;

	prop_id = igt_crtc_get_prop(&data->display.crtcs[pipe], IGT_CRTC_HISTOGRAM_ENABLE);
	if (prop_id == 0) {
		igt_debug("No histogram enable property for pipe %s\n", kmstest_pipe_name(pipe));
		return false;
	}

	histogram_enable_blob = drmModeGetPropertyBlob(data->drm_fd, prop_id);
	if (!histogram_enable_blob || !histogram_enable_blob->data) {
		igt_debug("Failed to get histogram enable blob for pipe %s\n", kmstest_pipe_name(pipe));
		return false;
	}

	histogram_config = (struct drm_histogram_config *)histogram_enable_blob->data;
	enabled = histogram_config->enable == GLOBAL_HIST_ENABLE;

	igt_debug("Pipe %s histogram enabled status: %s\n",
			kmstest_pipe_name(pipe), enabled ? "enabled" : "disabled");

	drmModeFreePropertyBlob(histogram_enable_blob);

	return enabled;
}

static void cleanup_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	int loop;
	igt_plane_t *plane;

	if (is_global_histogram_enabled(data, pipe))
		disable_and_verify_global_histogram(data, output, pipe);
	else
		igt_debug("Histogram already disabled on pipe %s\n", kmstest_pipe_name(pipe));

	for_each_plane_on_pipe(&data->display, pipe, plane)
		igt_plane_set_fb(plane, NULL);

	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	for (loop = 0; loop < 5; loop++) {
		if (data->fb[loop].fb_id)
			igt_remove_fb(data->display.drm_fd, &data->fb[loop]);
	}
}

static drmModePropertyBlobRes *get_global_histogram_data(data_t *data, enum pipe pipe)
{
	uint64_t blob_id;

	blob_id = igt_crtc_get_prop(&data->display.crtcs[pipe],
					IGT_CRTC_HISTOGRAM_DATA);
	if (blob_id == 0)
		return NULL;

	return drmModeGetPropertyBlob(data->drm_fd, blob_id);
}

static void read_global_histogram(data_t *data, enum pipe pipe,
				  drmModePropertyBlobRes **hist_blob_ptr)
{
	struct drm_histogram *histogram_ptr;
	drmModePropertyBlobRes *global_hist_blob = NULL;

	igt_set_timeout(GLOBAL_HIST_DELAY, "Waiting to read global histogram blob.\n");
	do {
		global_hist_blob = get_global_histogram_data(data, pipe);
	} while (!global_hist_blob);

	igt_reset_timeout();

	*hist_blob_ptr = global_hist_blob;
	histogram_ptr = (struct drm_histogram *) global_hist_blob->data;

	for (int i = 0; i < data->bin_count; i++)
		igt_debug("Histogram[%d] = %u\n", i, histogram_ptr->max_rgb[i]);
}

#ifdef HAVE_LIBGHE
static void set_pixel_factor(data_t *data, enum pipe pipe, uint32_t *ietlutentries, size_t size)
{
	uint32_t i;
	igt_crtc_t *crtc;
	struct drm_iet_1dlut_sample iet_sample = {0};

	crtc = igt_crtc_for_pipe(&data->display, pipe);

	for (i = 0; i < size; i++) {
		/* Displaying IET LUT */
		igt_debug("IET LUT Entry[%d] = %u\n", i, ietlutentries[i]);
	}

	/* Configure IET sample structure for new DRM interface */
	iet_sample.iet_lut = (uint64_t)(uintptr_t)ietlutentries;
	iet_sample.nr_elements = size;
	iet_sample.iet_mode = DRM_MODE_IET_MULTIPLICATIVE;

	igt_debug("IET sample config: lut_ptr=0x%llx, nr_elements=%u, mode=%d\n",
		  (unsigned long long)iet_sample.iet_lut, iet_sample.nr_elements, iet_sample.iet_mode);

	igt_crtc_replace_prop_blob(crtc, IGT_CRTC_IET_LUT,
				   &iet_sample, sizeof(iet_sample));
}

static struct globalhist_args *algo_get_pixel_factor(drmModePropertyBlobRes *global_hist_blob,
						    igt_output_t *output, enum pipe pipe)
{
	int i;
	struct globalhist_args *argsPtr =
		(struct globalhist_args *)malloc(sizeof(struct globalhist_args));
	struct drm_histogram *histogram_data;
	drmModeModeInfo *mode;

	if(!argsPtr)
		return NULL;

	/* Initialize IET LUT entries array */
	memset(argsPtr->ietlutentries, 0, sizeof(argsPtr->ietlutentries));

	mode = igt_output_get_mode(output);
	histogram_data = (struct drm_histogram *)global_hist_blob->data;

	switch (pipe) {
		case PIPE_A: argsPtr->pipeid = GlobalHist_PIPE_A; break;
		case PIPE_B: argsPtr->pipeid = GlobalHist_PIPE_B; break;
		case PIPE_C: argsPtr->pipeid = GlobalHist_PIPE_C; break;
		case PIPE_D: argsPtr->pipeid = GlobalHist_PIPE_D; break;
		default:     argsPtr->pipeid = GlobalHist_PIPE_A; break;
	}

	/* Extract actual histogram values */
	for (i = 0; i < min((uint32_t)32, histogram_data->nr_elements); i++)
                argsPtr->histogram[i] = histogram_data->max_rgb[i];

	/* Fill remaining bins with zero if DRM has fewer bins */
        for (i = histogram_data->nr_elements; i < 32; i++)
                argsPtr->histogram[i] = 0;

	argsPtr->binscount = histogram_data->nr_elements;
	/* Set IET mode (0=disabled, 1=enabled) */
	argsPtr->ietmode = 1;

	argsPtr->isprogramdiet = true;
	argsPtr->resolution_x = mode->hdisplay;
	argsPtr->resolution_y = mode->vdisplay;
	argsPtr->histogrammode = DRM_MODE_HISTOGRAM_HSV_MAX_RGB;

	igt_debug("Algorithm input: pipeid=%d, histogrammode=%d, binscount=%d, "
		  "resolution=%dx%d, isprogramdiet=%s, ietmode=%d\n",
		  argsPtr->pipeid, argsPtr->histogrammode, argsPtr->binscount,
		  argsPtr->resolution_x, argsPtr->resolution_y,
		  argsPtr->isprogramdiet ? "true" : "false", argsPtr->ietmode);

	igt_debug("Making call to global histogram algorithm.\n");

	histogram_compute_generate_data_bin(argsPtr);

	return argsPtr;
}

static void algo_image_enhancement_factor(data_t *data, enum pipe pipe,
					  igt_output_t *output,
					  drmModePropertyBlobRes *global_hist_blob)
{
	struct globalhist_args *args = algo_get_pixel_factor(global_hist_blob, output, pipe);

	igt_assert(args);
	igt_debug("Writing pixel factor blob.\n");

	set_pixel_factor(data, pipe, args->ietlutentries, 32);
	free(args);

	igt_display_commit2(&data->display, COMMIT_ATOMIC);
}
#endif

#ifdef HAVE_LIBDPST
static int set_pixel_factor_and_brightness(data_t *data, enum pipe pipe,
					   DD_DPST_ARGS *argsPtr)
{
	uint32_t i;
	igt_crtc_t *crtc;
	struct drm_iet_1dlut_sample iet_sample = {0};
	size_t diet_factor_size = DPST_IET_LUT_LENGTH + 1;

	crtc = igt_crtc_for_pipe(&data->display, pipe);

	/* Debug output for IET LUT entries (first 33) and brightness (34th) */
	for (i = 0; i < DPST_IET_LUT_LENGTH; i++)
		igt_debug("IET LUT Entry[%d] = %u\n", i, argsPtr->DietFactor[i]);

	igt_debug("Brightness Entry[%d] = %u\n", DPST_IET_LUT_LENGTH,
			argsPtr->DietFactor[DPST_IET_LUT_LENGTH]);

	/* Configure IET sample structure for new DRM interface */
	iet_sample.iet_lut = (uint64_t)(uintptr_t)argsPtr->DietFactor;
	iet_sample.nr_elements = diet_factor_size;
	iet_sample.iet_mode = DRM_MODE_IET_MULTIPLICATIVE;

	igt_crtc_replace_prop_blob(crtc, IGT_CRTC_IET_LUT,
			&iet_sample, sizeof(iet_sample));

	return 0;
}

static DD_DPST_ARGS *dpst_get_pixel_factor(drmModePropertyBlobRes *global_hist_blob,
					   igt_output_t *output, enum pipe pipe)
{
	int i;
	drmModeModeInfo *mode;
	struct drm_histogram *histogram_data;
	DD_DPST_ARGS *argsPtr = (DD_DPST_ARGS *)malloc(sizeof(DD_DPST_ARGS));

	if (!argsPtr)
		return NULL;

	/* Initialize DietFactor array */
	memset(argsPtr->DietFactor, 0, sizeof(argsPtr->DietFactor));

	mode = igt_output_get_mode(output);
	histogram_data = (struct drm_histogram *)global_hist_blob->data;

	/* Map pipe enum to DPST PIPE_ID */
	switch (pipe) {
		case PIPE_A: argsPtr->PipeId = DPST_PIPE_A; break;
		case PIPE_B: argsPtr->PipeId = DPST_PIPE_B; break;
		case PIPE_C: argsPtr->PipeId = DPST_PIPE_C; break;
		case PIPE_D: argsPtr->PipeId = DPST_PIPE_D; break;
		default:     argsPtr->PipeId = DPST_PIPE_A; break;
	}

	/* Extract actual histogram values */
	for (i = 0; i < min((uint32_t)DPST_BIN_COUNT, histogram_data->nr_elements); i++)
		argsPtr->Histogram[i] = histogram_data->max_rgb[i];

	/* Fill remaining bins with zero if DRM has fewer bins */
	for (i = histogram_data->nr_elements; i < DPST_BIN_COUNT; i++)
		argsPtr->Histogram[i] = 0;

	/* Set DPST parameters */
	argsPtr->DpstOpReq = DD_DPST_PROGRAM_DIET_REGS;
	argsPtr->IsSwDpst = true;
	argsPtr->IsProgramDiet = true;
	argsPtr->Aggressiveness_Level = DPST_MAX_AGGRESSIVENESS;
	argsPtr->Resolution_X = mode->hdisplay;
	argsPtr->Resolution_Y = mode->vdisplay;

	igt_debug("DPST Algorithm input: PipeId=%d, DpstOpReq=%d (%s), Aggressiveness=%d, "
		  "resolution=%dx%d, IsProgramDiet=%s, IsSwDpst=%s\n",
		  argsPtr->PipeId, argsPtr->DpstOpReq,
		  (argsPtr->DpstOpReq == DD_DPST_PROGRAM_DIET_REGS) ? "PROGRAM_DIET_REGS" : "OTHER",
		  argsPtr->Aggressiveness_Level,
		  argsPtr->Resolution_X, argsPtr->Resolution_Y,
		  argsPtr->IsProgramDiet ? "true" : "false",
		  argsPtr->IsSwDpst ? "true" : "false");

	igt_debug("Making call to DPST algorithm.\n");

	set_histogram_data_bin(argsPtr);

	return argsPtr;
}

static void dpst_image_enhancement_factor(data_t *data, enum pipe pipe,
					  igt_output_t *output,
					  drmModePropertyBlobRes *global_hist_blob)
{
	DD_DPST_ARGS *args = dpst_get_pixel_factor(global_hist_blob, output, pipe);
	int ret;

	igt_assert(args);
	igt_debug("Writing DPST pixel factor blob and adjusting brightness.\n");

	ret = set_pixel_factor_and_brightness(data, pipe, args);
	igt_assert_eq(ret, 0);

	free(args);

	igt_display_commit2(&data->display, COMMIT_ATOMIC);
}
#endif

static void create_monochrome_fbs(data_t *data, drmModeModeInfo *mode)
{
	/* TODO: Extend the tests for different formats/modifiers. */
	/* These frame buffers used to flip monochrome fbs to get histogram event. */
	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       0, 0, 0, &data->fb[0]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       1, 1, 1, &data->fb[1]));
}

static void create_color_fbs(data_t *data, drmModeModeInfo *mode)
{
	/* TODO: Extend the tests for different formats/modifiers. */
	/* These frame buffers used to flip color fbs to get histogram event. */
	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       0.5, 0, 0.5, &data->fb[0]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       1, 0, 0, &data->fb[1]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       0, 1, 0, &data->fb[2]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       0, 0, 1, &data->fb[3]));

	igt_assert(igt_create_color_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
					       1, 0, 1, &data->fb[4]));
}

static void flip_fb(data_t *data, enum pipe pipe, igt_output_t *output, struct igt_fb *fb)
{
	igt_plane_set_fb(igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY), fb);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);
}

static void prepare_pipe(data_t *data, enum pipe pipe, igt_output_t *output, bool color_fb)
{
	int i;
	struct udev_monitor *mon = igt_watch_uevents();
	drmModeModeInfo *mode = igt_output_get_mode(output);
	bool event_detected = false;
	int fb_count = color_fb ? 5 : 2;

	if (color_fb)
		create_color_fbs(data, mode);
	else
		create_monochrome_fbs(data, mode);

	flip_fb(data, pipe, output, &data->fb[0]);
	enable_and_verify_global_histogram(data, pipe);

	igt_flush_uevents(mon);
	for (i = 1; i <= FLIP_COUNT; i++) {
		flip_fb(data, pipe, output, &data->fb[i % fb_count]);

		/* Check for histogram event on every flip and break the loop if detected. */
		if (igt_global_histogram_event_detected(mon, 0)) {
			event_detected = true;
			break;
		}
	}

	igt_cleanup_uevents(mon);

	if (!event_detected)
		cleanup_pipe(data, pipe, output);

	igt_assert_f(event_detected, "Histogram event not generated.\n");
}

static void run_global_histogram_pipeline(data_t *data, enum pipe pipe, igt_output_t *output,
					  bool color_fb, test_t test_pixel_factor)
{
	drmModePropertyBlobRes *global_hist_blob = NULL;

	prepare_pipe(data, pipe, output, color_fb);

	if (!is_global_histogram_enabled(data, pipe)) {
                igt_debug("Skipping read: global histogram is disabled on pipe %s\n",
                          kmstest_pipe_name(pipe));
                cleanup_pipe(data, pipe, output);
                igt_skip("Global histogram disabled; skipping histogram data read.\n");
        }

	read_global_histogram(data, pipe, &global_hist_blob);

	if (test_pixel_factor)
		test_pixel_factor(data, pipe, output, global_hist_blob);

	drmModeFreePropertyBlob(global_hist_blob);

	cleanup_pipe(data, pipe, output);
}

static void run_tests_for_global_histogram(data_t *data, bool color_fb,
					   test_t test_pixel_factor)
{
	enum pipe pipe;
	igt_output_t *output;

	for_each_connected_output(&data->display, output) {
		for_each_pipe(&data->display, pipe) {
			if (!igt_crtc_has_prop(&data->display.crtcs[pipe], IGT_CRTC_HISTOGRAM_CAPS))
				continue;

			igt_display_reset(&data->display);

			igt_output_set_crtc(output, igt_crtc_for_pipe(&data->display, pipe));
			if (!intel_pipe_output_combo_valid(&data->display))
				continue;

			igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(pipe), igt_output_name(output))
				run_global_histogram_pipeline(data, pipe, output, color_fb, test_pixel_factor);
		}
	}
}

static void run_algo_test(data_t *data, bool color_fb)
{
#ifdef HAVE_LIBGHE
	run_tests_for_global_histogram(data, color_fb, algo_image_enhancement_factor);
#else
	igt_skip("Histogram algorithm library not found.\n");
#endif
}

static void run_dpst_test(data_t *data, bool color_fb)
{
#ifdef HAVE_LIBDPST
	run_tests_for_global_histogram(data, color_fb, dpst_image_enhancement_factor);
#else
	igt_skip("DPST algorithm library not found.\n");
#endif
}

int igt_main()
{
	data_t data = {};

	igt_fixture() {
		data.drm_fd = drm_open_driver_master(DRIVER_ANY);
		data.debugfs_fd = igt_debugfs_dir(data.drm_fd);
		kmstest_set_vt_graphics_mode();
		igt_display_require(&data.display, data.drm_fd);
		igt_display_require_output(&data.display);
		igt_require(data.display.is_atomic);
	}

	igt_describe("Test to enable histogram, flip monochrome fbs, wait for histogram "
		     "event and then read the histogram data.");
	igt_subtest_with_dynamic("global-basic")
		run_tests_for_global_histogram(&data, false, NULL);

	igt_describe("Test to enable histogram, flip color fbs, wait for histogram event "
		     "and then read the histogram data.");
	igt_subtest_with_dynamic("global-color")
		run_tests_for_global_histogram(&data, true, NULL);

	igt_describe("Test to enable histogram, flip monochrome fbs, wait for histogram "
		     "event and then read the histogram data and enhance pixels by multiplying "
		     "by a pixel factor using algo.");
	igt_subtest_with_dynamic("algo-basic")
		run_algo_test(&data, false);

	igt_describe("Test to enable histogram, flip color fbs, wait for histogram event "
		     "and then read the histogram data and enhance pixels by multiplying "
		     "by a pixel factor using algo.");
	igt_subtest_with_dynamic("algo-color")
		run_algo_test(&data, true);

	igt_describe("Test to enable histogram, flip monochrome fbs, wait for histogram "
		     "event and then read the histogram data and enhance pixels by multiplying "
		     "by a pixel factor using DPST algorithm with brightness adjustment.");
	igt_subtest_with_dynamic("dpst-basic")
		run_dpst_test(&data, false);

	igt_describe("Test to enable histogram, flip color fbs, wait for histogram event "
		     "and then read the histogram data and enhance pixels by multiplying "
		     "by a pixel factor using DPST algorithm with brightness adjustment.");
	igt_subtest_with_dynamic("dpst-color")
		run_dpst_test(&data, true);

	igt_describe("Test to enable global histogram, update SF clip, read histogram blob "
		     "and enhances the pixels by multiplying by a pixel factor using dpst algo "
		     "with brightness adjustment.");
	igt_subtest_with_dynamic("dpst-color-selective-fetch") {
		igt_require_f(psr_sink_support(data.drm_fd, data.debugfs_fd, PSR_MODE_2, NULL),
			      "Sink does not support PSR2\n");
		/* Test if PSR2 can be enabled */
		igt_require_f(psr_enable(data.drm_fd, data.debugfs_fd, PSR_MODE_2, NULL),
			      "Error enabling PSR2\n");
		data.selective_fetch = true;
		run_dpst_test(&data, true);
		igt_require_f(psr_disable(data.drm_fd, data.debugfs_fd, NULL),
			      "Error disabling PSR2\n");
	}

	igt_fixture() {
		igt_display_fini(&data.display);
		drm_close_driver(data.drm_fd);
	}
}

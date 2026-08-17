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

#ifdef HAVE_LIBGHE
#include <ghe/ghe.h>
#endif

#ifdef HAVE_LIBDPST
#include <dpst/DisplayPcDpst.h>

#define BACKLIGHT_PATH			"/sys/class/backlight"
#define DPST_MAX_AGGRESSIVENESS		3
#define DPST_FACTOR_TOLERANCE		150ULL
#define DPST_MAX_PHASE_IN_VBLANKS	4
#define ALGO_VER			80
#endif

#define GLOBAL_HIST_DISABLE		0
#define GLOBAL_HIST_ENABLE		1
#define FLIP_COUNT			20

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
 */

IGT_TEST_DESCRIPTION("This test will verify the display histogram.");

typedef struct data {
	int drm_fd;
	igt_fb_t fb[5];
	igt_display_t display;
	struct drm_histogram_caps caps_data;
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

	caps_out->histogram_mode = data->caps_data.histogram_mode = caps->histogram_mode;
	caps_out->bins_count = data->caps_data.bins_count = caps->bins_count;

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

	igt_info("Enabling histogram: mode=%d, bins=%d\n", caps.histogram_mode, caps.bins_count);

	configure_and_verify_histogram(data, pipe, &caps, true);
}

static void disable_and_verify_global_histogram(data_t *data, igt_output_t *output, enum pipe pipe)
{

	igt_debug("Disabling histogram: mode=%d, bins=%d\n",
		  data->caps_data.histogram_mode, data->caps_data.bins_count);

	configure_and_verify_histogram(data, pipe, &data->caps_data, false);

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
	if (!histogram_enable_blob) {
		igt_debug("Failed to get histogram enable blob for pipe %s\n",
			  kmstest_pipe_name(pipe));
		drmModeFreePropertyBlob(histogram_enable_blob);
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

	if (is_global_histogram_enabled(data, pipe))
		disable_and_verify_global_histogram(data, output, pipe);
	else
		igt_debug("Histogram already disabled on pipe %s\n", kmstest_pipe_name(pipe));

	igt_output_set_crtc(output, NULL);
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
	global_hist_blob = get_global_histogram_data(data, pipe);

	*hist_blob_ptr = global_hist_blob;
	histogram_ptr = (struct drm_histogram *) global_hist_blob->data;

	/* Verify bin count matches caps before reading histogram data. */
	igt_assert_f(histogram_ptr->nr_elements == data->caps_data.bins_count,
		     "Histogram bin count mismatch: expected %d, got %u\n",
		     data->caps_data.bins_count, histogram_ptr->nr_elements);

	for (int i = 0; i < data->caps_data.bins_count; i++)
		igt_debug("Histogram[%d] = %u\n", i, histogram_ptr->max_rgb[i]);
}

#if defined(HAVE_LIBGHE) || defined(HAVE_LIBDPST)
static void set_pixel_factor(data_t *data, enum pipe pipe, uint32_t *ietlutentries,
			     size_t size, uint32_t iet_mode)
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
	iet_sample.iet_mode = iet_mode;

	igt_debug("IET sample config: lut_ptr=0x%llx, nr_elements=%u, mode=%d\n",
		   (unsigned long long)iet_sample.iet_lut, iet_sample.nr_elements, iet_sample.iet_mode);

	igt_crtc_replace_prop_blob(crtc, IGT_CRTC_IET_LUT,
				   &iet_sample, sizeof(iet_sample));
}
#endif

#ifdef HAVE_LIBGHE
static void algo_image_enhancement_factor(data_t *data, enum pipe pipe,
					  igt_output_t *output,
					  drmModePropertyBlobRes *global_hist_blob)
{
	struct drm_histogram *histogram_data;
	drmModePropertyBlobRes *iet_caps_blob;
	struct drm_iet_caps iet_caps = {0};
	struct globalhist_args args = {0};
	drmModeModeInfo *mode;
	int64_t prop_id;
	int i;

	mode = igt_output_get_mode(output);
	histogram_data = (struct drm_histogram *)global_hist_blob->data;

	/* Extract actual histogram values */
	for (i = 0;  i < histogram_data->nr_elements; i++)
		args.histogram[i] = histogram_data->max_rgb[i];

	args.binscount = histogram_data->nr_elements;
	args.resolution_x = mode->hdisplay;
	args.resolution_y = mode->vdisplay;
	args.histogrammode = DRM_MODE_HISTOGRAM_HSV_MAX_RGB;

	igt_debug("Algorithm input: histogrammode=%d, binscount=%d, "
		  "resolution=%dx%d\n",
		  args.histogrammode, args.binscount,
		  args.resolution_x, args.resolution_y);

	/* Read IET_LUT_CAPS */
	prop_id = igt_crtc_get_prop(&data->display.crtcs[pipe],
				    IGT_CRTC_IET_LUT_CAPS);
	igt_assert_f(prop_id, "No IET_LUT_CAPS property for pipe %s\n",
		     kmstest_pipe_name(pipe));

	iet_caps_blob = drmModeGetPropertyBlob(data->drm_fd, prop_id);
	igt_assert_f(iet_caps_blob && iet_caps_blob->data,
		     "Failed to get IET_LUT_CAPS blob for pipe %s\n",
		     kmstest_pipe_name(pipe));

	memcpy(&iet_caps, iet_caps_blob->data, sizeof(iet_caps));
	drmModeFreePropertyBlob(iet_caps_blob);

	igt_debug("IET LUT Caps on pipe %s:\n"
		  "  iet_mode           = 0x%x\n"
		  "  nr_iet_lut_entries = %u\n",
		  kmstest_pipe_name(pipe),
		  iet_caps.iet_mode,
		  iet_caps.nr_iet_lut_entries);

	igt_debug("Making call to global histogram algorithm.\n");

	histogram_compute_generate_data_bin(&args);

	igt_debug("Writing pixel factor blob.\n");

	set_pixel_factor(data, pipe, args.ietlutentries,
			 iet_caps.nr_iet_lut_entries,
			 iet_caps.iet_mode);

	igt_display_commit2(&data->display, COMMIT_ATOMIC);
}
#endif

#ifdef HAVE_LIBDPST
static void dpst_image_enhancement_factor(data_t *data, enum pipe pipe,
					  igt_output_t *output,
					  drmModePropertyBlobRes *global_hist_blob)
{
	int i;
	int max_brightness;
	int actual_brightness;
	int initial_brightness;
	uint64_t prop_id, diff;
	uint64_t expected_factor;
	uint64_t actual_factor;
	drmModeModeInfo *mode;
	DD_DPST_ARGS args = {0};
	struct drm_iet_caps *iet_caps;
	struct drm_histogram *histogram_data;
	drmModePropertyBlobRes *iet_caps_blob;
	igt_backlight_context_t context = {
		.backlight_dir_path = BACKLIGHT_PATH,
		.path = "intel_backlight",
	};

	mode = igt_output_get_mode(output);
	histogram_data = (struct drm_histogram *)global_hist_blob->data;

	/* Extract actual histogram values */
	for (i = 0; i < histogram_data->nr_elements; i++)
		args.Histogram[i] = histogram_data->max_rgb[i];

	/* Set DPST parameters */
	args.Aggressiveness_Level = DPST_MAX_AGGRESSIVENESS;
	args.Resolution_X = mode->hdisplay;
	args.Resolution_Y = mode->vdisplay;
	args.AlgorithmVersion = ALGO_VER;

	igt_info("DPST Algorithm input: Aggressiveness=%d, resolution=%dx%d\n",
		  (int)args.Aggressiveness_Level, (int)args.Resolution_X,
		  (int)args.Resolution_Y);

	/* Read IET_LUT_CAPS */
	prop_id = igt_crtc_get_prop(&data->display.crtcs[pipe],
				    IGT_CRTC_IET_LUT_CAPS);
	igt_assert_f(prop_id, "No IET_LUT_CAPS property for pipe %s\n",
			       kmstest_pipe_name(pipe));

        iet_caps_blob = drmModeGetPropertyBlob(data->drm_fd, prop_id);
	igt_assert_f(iet_caps_blob && iet_caps_blob->data,
		     "Failed to get IET_LUT_CAPS blob for pipe %s\n",
		     kmstest_pipe_name(pipe));

	iet_caps = (struct drm_iet_caps *)iet_caps_blob->data;

	igt_debug("IET LUT Caps on pipe %s:\n"
		  "  iet_mode           = 0x%x\n"
		  "  nr_iet_lut_entries = %u\n",
		  kmstest_pipe_name(pipe),
		  iet_caps->iet_mode,
		  iet_caps->nr_iet_lut_entries);

	/* Read Brightness Before DPST */
	igt_assert_eq(igt_backlight_read(&max_brightness, "max_brightness", &context), 0);
	igt_assert_f(max_brightness > 0,"Invalid max_brightness = %d\n", max_brightness);
	igt_debug("max_brightness = %d\n", max_brightness);


	/* Read actual_brightness BEFORE applying DPST */
	igt_assert_eq(igt_backlight_read(&initial_brightness, "actual_brightness",
					 &context), 0);
	igt_debug("initial_brightness (before DPST) = %d\n", initial_brightness);

	igt_debug("Making call to DPST algorithm.\n");
	set_histogram_data_bin(&args);

	/*
	 * "Backlight dimming factor is reciprocal of slope
	 *  of the transfer function in the shadow region."
	 *
	 * "shadow_slope      = ShadowSegmentHeight /
	 *                      ShadowSegmentLength"
	 * "adjustment_factor = shadow_slope ^ panel_gamma
	 *                      (2.2 for sRGB)"
	 * "backlight_factor  = 1.0 / adjustment_factor"
	 *
	 * "Example:
	 *   shadow_slope     = 1.41
	 *   adjustment       = 1.41 ^ 2.2 = 2.129
	 *   backlight factor = 1/2.129 = 0.47 = 47%"
	 *
	 * DietFactor[DPST_IET_LUT_LENGTH] (index 33):
	 * Library encodes backlight_factor as basis points
	 * (0-10000). Scale is FIXED regardless of platform
	 * max_brightness value.
	 *   10000 = 100% (no dimming)
	 */
	expected_factor = (uint64_t)args.DietFactor[DPST_IET_LUT_LENGTH];

	igt_debug("Writing DPST pixel factor blob and adjusting brightness.\n");
	set_pixel_factor(data, pipe, args.DietFactor,
			 iet_caps->nr_iet_lut_entries + 1,
			 iet_caps->iet_mode);

	drmModeFreePropertyBlob(iet_caps_blob);

	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	/*
	 * [Temporal Smoothening]
	 * "Purpose: Apply backlight factor and IET in small
	 *  steps to avoid a sudden visual change."
	 *
	 * "A 3rd order Butterworth IIR filter is used to
	 *  calculate intermediate backlight level and IET
	 *  to be applied."
	 *
	 * "Phase in is terminated once IET factor reaches
	 *  close to the target IET under tolerance limit."
	 *
	 * Wait 1 vblank per iteration - synced to actual
	 * display refresh rate (60Hz, 120Hz, 144Hz etc).
	 */
	actual_brightness = 0;
	actual_factor     = 0;
	diff              = 0;

	for (i = 0; i < DPST_MAX_PHASE_IN_VBLANKS; i++) {

		igt_wait_for_vblank_count(
				igt_crtc_for_pipe(&data->display, pipe), 1);

		igt_assert_eq(igt_backlight_read(&actual_brightness,
					"actual_brightness", &context), 0);

		actual_factor = ((uint64_t)actual_brightness * 10000ULL) /
				 (uint64_t)max_brightness;

		diff = (actual_factor > expected_factor) ?
			(actual_factor - expected_factor) :
			(expected_factor - actual_factor);

		igt_debug("vblank[%02d/%02d]: "
			  "actual_brightness = %d"
			  " actual_factor = %llu (%.2f%%)"
			  " expected_factor = %llu (%.2f%%)"
			  " diff = %llu\n",
			  i + 1,
			  DPST_MAX_PHASE_IN_VBLANKS,
			  actual_brightness,
			  (unsigned long long)actual_factor, actual_factor / 100.0,
			  (unsigned long long)expected_factor, expected_factor / 100.0,
			  (unsigned long long)diff);

		/*
		 * "Phase in is terminated once IET factor
		 *  reaches close to target IET under
		 *  tolerance limit defined."
		 */
		if (diff <= DPST_FACTOR_TOLERANCE) {
			igt_debug("Brightness settled at "
				  "vblank %d of %d\n",
				  i + 1,
				  DPST_MAX_PHASE_IN_VBLANKS);
			break;
		}
	}

	/*  Final Validation */
	igt_assert_f(diff <= DPST_FACTOR_TOLERANCE,
		     "Brightness did not settle after %u vblanks:\n"
		     "  expected_factor    = %llu (%.2f%%)\n"
		     "  actual_factor      = %llu (%.2f%%)\n"
		     "  diff               = %llu\n"
		     "  tolerance (1.5%%)    = %llu\n"
		     "  initial_brightness = %d\n"
		     "  actual_brightness  = %d\n"
		     "  max_brightness     = %d\n"
		     "  pipe               = %s\n",
		     DPST_MAX_PHASE_IN_VBLANKS,
		     (unsigned long long)expected_factor,
		     expected_factor / 100.0,
		     (unsigned long long)actual_factor,
		     actual_factor / 100.0,
		     (unsigned long long)diff,
		     (unsigned long long)DPST_FACTOR_TOLERANCE,
		     initial_brightness,
		     actual_brightness,
		     max_brightness,
		     kmstest_pipe_name(pipe));

	igt_info("DPST test PASSED\n"
		 "  pipe               = %s\n"
		 "  expected_factor    = %llu (%.2f%%)\n"
		 "  actual_factor      = %llu (%.2f%%)\n"
		 "  diff               = %llu"
		 " (within tolerance 1.5%% = %llu)\n"
		 "  settled at vblank  = %d of %u\n"
		 "  initial_brightness = %d\n"
		 "  actual_brightness  = %d\n"
		 "  max_brightness     = %d\n",
		 kmstest_pipe_name(pipe),
		 (unsigned long long)expected_factor, expected_factor / 100.0,
		 (unsigned long long)actual_factor, actual_factor / 100.0,
		 (unsigned long long)diff,
		 DPST_FACTOR_TOLERANCE,
		 i + 1,
		 DPST_MAX_PHASE_IN_VBLANKS,
		 initial_brightness,
		 actual_brightness,
		 max_brightness);
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

		/* TODO: for handling multiple interrupts/events */
		/* Flip and break as soon as a histogram event is detected. */
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
	igt_crtc_t *crtc;
	igt_output_t *output;

	for_each_crtc(&data->display, crtc) {
		pipe = crtc->pipe;
		for_each_valid_output_on_crtc(&data->display, crtc, output) {

			if (!igt_crtc_has_prop(crtc, IGT_CRTC_HISTOGRAM_CAPS))
				continue;

			igt_display_reset(&data->display);

			igt_output_set_crtc(output, igt_crtc_for_pipe(&data->display, pipe));
			if (!intel_pipe_output_combo_valid(&data->display))
				continue;

			igt_dynamic_f("pipe-%s-%s",
				      kmstest_pipe_name(pipe),
				      igt_output_name(output))
				      run_global_histogram_pipeline(data, pipe, output,
								    color_fb,
								    test_pixel_factor);
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

	igt_fixture() {
		igt_display_fini(&data.display);
		drm_close_driver(data.drm_fd);
	}
}

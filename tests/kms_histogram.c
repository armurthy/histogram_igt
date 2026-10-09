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

#define BACKLIGHT_PATH "/sys/class/backlight"
#define DPST_MAX_AGGRESSIVENESS   3
/* DPST8 only supports levels 1-3 (confirmed with algo owner); expected values are captured at the max */
#define DPST_STATIC_IMAGE_AGGRESSIVENESS	DPST_MAX_AGGRESSIVENESS
/* Expected values were captured with DPST 8.0 (DPST_VERSION_8_0 in the library) */
#define DPST_STATIC_IMAGE_ALGO_VERSION		80
/* Set IGT_DPST_CAPTURE_REF=1 to print copy-pasteable reference values instead of asserting */

/* DPST library programs IET in 1.9 fixed point and backlight in x100 percent */
#define DPST_IET_UNITY			512
#define DPST_BACKLIGHT_UNITY		100
/* Iterations the DPST temporal filter gets to settle on its target */
#define DPST_CONVERGE_MAX_ITERATIONS	1000
#define DPST_CONVERGE_STABLE_COUNT	5
#endif

#define GLOBAL_HIST_DISABLE		0
#define GLOBAL_HIST_ENABLE		1
#define FLIP_COUNT			20

/* Reference IET values are scaled so that 10000 is a 1.0x multiplier */
#define REF_IET_LUT_LENGTH		33
#define REF_IET_UNITY			10000
#define REF_IET_TOLERANCE		(REF_IET_UNITY / 100)	/* 1% */
#define REF_BACKLIGHT_TOLERANCE		2			/* percent */

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
 * SUBTEST: dpst-static-%s
 * Description: Test to enable histogram, flip the %arg[1] static image, wait for
 *              histogram event, compute IET LUT and backlight using DPST algorithm
 *              and compare them against the expected values for that image
 *
 * arg[1]:
 *
 * @fuschia:	fuschia
 * @wall:	wall
 * @leaf:	leaf
 * @colorbars:	color bars
 * @winxp:	WinXP desktop
 * @youtube:	youtube
 * @white100:	100% white
 * @white080:	80% white
 * @white050:	50% white
 * @white000:	0% white (black)
 * @washout:	washout test
 */

IGT_TEST_DESCRIPTION("This test will verify the display histogram.");

struct dpst_static_image {
	const char *name;
	const char *filename;
	/* Expected values at aggressiveness level 5 with EPSM disabled */
	uint32_t backlight;	/* percent */
	uint32_t iet_lut[REF_IET_LUT_LENGTH];
};

static const struct dpst_static_image dpst_static_images[] = {
	{
		.name = "fuschia",
		.filename = "dpst_fuschia.png",
		.backlight = 100,
		.iet_lut = {
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000
		},
	},
	{
		.name = "wall",
		.filename = "dpst_wall.png",
		.backlight = 64,
		.iet_lut = {
			12857, 12857, 12666, 12608, 12580, 12564, 12553, 12545,
			12698, 12676, 12658, 12643, 12631, 12621, 12522, 12436,
			12362, 12222, 12097, 11986, 11823, 11676, 11542, 11366,
			11204, 11055, 10869, 10697, 10538, 10389, 10251, 10121,
			10000
		},
	},
	{
		.name = "leaf",
		.filename = "dpst_leaf.png",
		.backlight = 45,
		.iet_lut = {
			14285, 14285, 14666, 14347, 14516, 14358, 14468, 14363,
			14444, 14366, 14303, 14137, 14000, 13786, 13603, 13445,
			13228, 13037, 12797, 12582, 12327, 12095, 11828, 11584,
			11361, 11155, 10966, 10744, 10582, 10432, 10251, 10121,
			10000
		},
	},
	{
		.name = "colorbars",
		.filename = "dpst_colorbars.png",
		.backlight = 71,
		.iet_lut = {
			11428, 11428, 12000, 11739, 11612, 11794, 11702, 11636,
			11746, 11690, 11645, 11724, 11684, 11650, 11711, 11680,
			11653, 11629, 11538, 11456, 11383, 11317, 11200, 11092,
			10942, 10804, 10676, 10558, 10403, 10259, 10125, 10080,
			10000
		},
	},
	{
		.name = "winxp",
		.filename = "dpst_winxp.png",
		.backlight = 97,
		.iet_lut = {
			10000, 10000, 10000, 10000, 10000, 10256, 10212, 10181,
			10158, 10140, 10126, 10114, 10210, 10194, 10180, 10168,
			10157, 10148, 10139, 10132, 10188, 10179, 10171, 10163,
			10157, 10150, 10144, 10139, 10179, 10173, 10125, 10040,
			10000
		},
	},
	{
		.name = "youtube",
		.filename = "dpst_youtube.png",
		.backlight = 100,
		.iet_lut = {
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000
		},
	},
	{
		.name = "white100",
		.filename = "dpst_white100.png",
		.backlight = 100,
		.iet_lut = {
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000
		},
	},
	{
		.name = "white080",
		.filename = "dpst_white080.png",
		.backlight = 100,
		.iet_lut = {
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000
		},
	},
	{
		.name = "white050",
		.filename = "dpst_white050.png",
		.backlight = 100,
		.iet_lut = {
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000
		},
	},
	{
		.name = "white000",
		.filename = "dpst_white000.png",
		.backlight = 45,
		.iet_lut = {
			14285, 14285, 14666, 14347, 14516, 14358, 14468, 14363,
			14444, 14507, 14430, 14482, 14421, 14466, 14414, 14453,
			14409, 13925, 13426, 13046, 12641, 12275, 12000, 11693,
			11465, 11206, 11014, 10790, 10582, 10432, 10251, 10121,
			10000
		},
	},
	{
		.name = "washout",
		.filename = "dpst_washout.png",
		.backlight = 45,
		.iet_lut = {
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
			10000
		},
	},
};

typedef struct data {
	int drm_fd;
	igt_fb_t fb[5];
	igt_display_t display;
	struct drm_histogram_caps caps_data;
	const struct dpst_static_image *static_image;
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
	uint64_t prop_id;
	int max_brightness;
	int initial_brightness;
	int actual_brightness;
	int expected_brightness;
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

	/* Read max_brightness for tolerance */
	igt_backlight_read(&max_brightness, "max_brightness", &context);

	/* Read actual_brightness BEFORE applying DPST */
	igt_assert_eq(igt_backlight_read(&initial_brightness, "actual_brightness",
					 &context), 0);
	igt_debug("initial_brightness (before DPST) = %d\n", initial_brightness);

	igt_debug("Making call to DPST algorithm.\n");
	set_histogram_data_bin(&args);

	/* Convert percent from DPST library to expected brightness */
	expected_brightness = ((uint64_t)args.DietFactor[DPST_IET_LUT_LENGTH] *
				initial_brightness) / 10000;

	igt_debug("Writing DPST pixel factor blob and adjusting brightness.\n");
	set_pixel_factor(data, pipe, args.DietFactor,
			 iet_caps->nr_iet_lut_entries + 1,
			 iet_caps->iet_mode);

	drmModeFreePropertyBlob(iet_caps_blob);

	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	/* Wait for vblank cycles after commit to read actual_brightness */
	igt_wait_for_vblank_count(igt_crtc_for_pipe(&data->display, pipe), 3);

	/* Read max_brightness after DPST applied */
	igt_assert_eq(igt_backlight_read(&actual_brightness, "actual_brightness",
		      &context), 0);
	igt_debug("actual_brightness = %d\n", actual_brightness);

	igt_assert_f(abs(expected_brightness - actual_brightness) <=
		     max_brightness / 100,
		     "Brightness mismatch :\n"
		     "  percent from library = %u\n"
		     "  expected_brightness  = %d\n"
		     "  actual_brightness    = %d\n"
		     "  diff                 = %d\n"
		     "  tolerance (1%%)      = %d\n",
		     args.DietFactor[DPST_IET_LUT_LENGTH],
		     expected_brightness,
		     actual_brightness,
		     abs(expected_brightness - actual_brightness),
		     max_brightness / 100);


	igt_info("DPST test PASSED \n"
		 "  expected = %d\n"
		 "  actual   = %d\n"
		 "  diff     = %d (within tolerance (1%%) %d)\n",
		 expected_brightness,
		 actual_brightness,
		 abs(expected_brightness - actual_brightness),
		 max_brightness / 100);
}

/*
 * The DPST library runs a temporal filter and its first call only initializes
 * the context, so feed the same histogram until the IET LUT and backlight stop
 * changing to get the final values.
 */
static void dpst_run_until_converged(DD_DPST_ARGS *args)
{
	uint32_t prev[DPST_IET_LUT_LENGTH + 1] = {0};
	int stable = 0;
	int i;

	for (i = 0; i < DPST_CONVERGE_MAX_ITERATIONS; i++) {
		set_histogram_data_bin(args);

		if (args->DietFactor[DPST_IET_LUT_LENGTH] &&
		    !memcmp(prev, args->DietFactor, sizeof(prev))) {
			if (++stable == DPST_CONVERGE_STABLE_COUNT) {
				igt_debug("DPST converged after %d iterations\n", i + 1);
				return;
			}
		} else {
			stable = 0;
			memcpy(prev, args->DietFactor, sizeof(prev));
		}
	}

	igt_assert_f(false, "DPST output did not converge in %d iterations\n",
		     DPST_CONVERGE_MAX_ITERATIONS);
}

static void dpst_static_image_validate(data_t *data, enum pipe pipe,
				       igt_output_t *output,
				       drmModePropertyBlobRes *global_hist_blob)
{
	const struct dpst_static_image *img = data->static_image;
	struct drm_histogram *histogram_data;
	uint32_t actual_backlight;
	DD_DPST_ARGS args = {0};
	drmModeModeInfo *mode;
	int mismatches = 0;
	int i;

	igt_assert(img);

	mode = igt_output_get_mode(output);
	histogram_data = (struct drm_histogram *)global_hist_blob->data;

	/* Extract actual histogram values */
	for (i = 0; i < histogram_data->nr_elements; i++)
		args.Histogram[i] = histogram_data->max_rgb[i];

	/* Expected values are captured in default configuration */
	args.Aggressiveness_Level = DPST_STATIC_IMAGE_AGGRESSIVENESS;
	args.Resolution_X = mode->hdisplay;
	args.Resolution_Y = mode->vdisplay;
	args.AlgorithmVersion = DPST_STATIC_IMAGE_ALGO_VERSION;

	igt_info("DPST Algorithm input: image=%s, version=%d, Aggressiveness=%d, resolution=%dx%d\n",
		 img->filename, (int)args.AlgorithmVersion, (int)args.Aggressiveness_Level,
		 (int)args.Resolution_X, (int)args.Resolution_Y);

	dpst_run_until_converged(&args);

	igt_info("%-6s %10s %10s %10s\n", "Index", "Expected", "Actual", "Diff");
	for (i = 0; i < REF_IET_LUT_LENGTH; i++) {
		/* Scale library 1.9 fixed point to the reference scale */
		int actual = ((uint64_t)args.DietFactor[i] * REF_IET_UNITY +
			      DPST_IET_UNITY / 2) / DPST_IET_UNITY;
		int diff = actual - (int)img->iet_lut[i];
		bool match = abs(diff) <= REF_IET_TOLERANCE;

		igt_info("%-6d %10u %10d %10d%s\n", i, img->iet_lut[i], actual,
			 diff, match ? "" : "  <-- mismatch");
		mismatches += !match;
	}

	actual_backlight = (args.DietFactor[DPST_IET_LUT_LENGTH] +
			    DPST_BACKLIGHT_UNITY / 2) / DPST_BACKLIGHT_UNITY;
	igt_info("Backlight: expected = %u%%, actual = %u%% (raw %u)\n",
		 img->backlight, actual_backlight,
		 args.DietFactor[DPST_IET_LUT_LENGTH]);

	if (igt_check_boolean_env_var("IGT_DPST_CAPTURE_REF", false)) {
		char lut_str[REF_IET_LUT_LENGTH * 16 + 1] = {};

		for (i = 0; i < REF_IET_LUT_LENGTH; i++) {
			int actual = ((uint64_t)args.DietFactor[i] * REF_IET_UNITY +
				      DPST_IET_UNITY / 2) / DPST_IET_UNITY;
			char val[16];

			snprintf(val, sizeof(val), i % 8 == 7 ? "%5d,\n\t\t\t" : "%5d, ", actual);
			strcat(lut_str, val);
		}

		igt_info("/* --- paste below into dpst_static_images[] for \"%s\" --- */\n"
			 "\t\t.backlight = %u,\n"
			 "\t\t.iet_lut = {\n\t\t\t%s\n\t\t},\n"
			 "/* --- end paste --- */\n",
			 img->name, actual_backlight, lut_str);
		return;
	}

	igt_assert_f(mismatches == 0,
		     "%s: %d IET LUT entries outside tolerance (%d)\n",
		     img->name, mismatches, REF_IET_TOLERANCE);
	igt_assert_f(abs((int)actual_backlight - (int)img->backlight) <=
		     REF_BACKLIGHT_TOLERANCE,
		     "%s: backlight mismatch, expected %u%%, actual %u%% (tolerance %d%%)\n",
		     img->name, img->backlight, actual_backlight,
		     REF_BACKLIGHT_TOLERANCE);
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

static void create_static_image_fbs(data_t *data, drmModeModeInfo *mode,
				    const char *filename)
{
	/* Mid gray differs from every reference image, so flipping to the image always triggers an event. */
	igt_assert(igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
				       0.5, 0.5, 0.5, &data->fb[0]));

	/* Scale the reference image to full screen */
	igt_assert(igt_create_image_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				       DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
				       filename, &data->fb[1]));
}

static void prepare_pipe_static_image(data_t *data, enum pipe pipe, igt_output_t *output)
{
	int i;
	struct udev_monitor *mon = igt_watch_uevents();
	drmModeModeInfo *mode = igt_output_get_mode(output);
	igt_crtc_t *crtc = igt_crtc_for_pipe(&data->display, pipe);
	bool event_detected = false;

	create_static_image_fbs(data, mode, data->static_image->filename);

	flip_fb(data, pipe, output, &data->fb[0]);
	enable_and_verify_global_histogram(data, pipe);

	for (i = 0; i < FLIP_COUNT && !event_detected; i++) {
		/* Drop events caused by the gray fb, so the event we act on belongs to the image. */
		flip_fb(data, pipe, output, &data->fb[0]);
		igt_wait_for_vblank_count(crtc, 2);
		igt_flush_uevents(mon);

		flip_fb(data, pipe, output, &data->fb[1]);
		event_detected = igt_global_histogram_event_detected(mon, 1);
	}

	igt_cleanup_uevents(mon);

	if (!event_detected)
		cleanup_pipe(data, pipe, output);

	igt_assert_f(event_detected, "Histogram event not generated for %s.\n",
		     data->static_image->filename);
}

static void run_global_histogram_pipeline(data_t *data, enum pipe pipe, igt_output_t *output,
					  bool color_fb, test_t test_pixel_factor)
{
	drmModePropertyBlobRes *global_hist_blob = NULL;

	if (data->static_image)
		prepare_pipe_static_image(data, pipe, output);
	else
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

			if (!igt_crtc_has_prop(crtc, IGT_CRTC_HISTOGRAM_CAPS)) {
				igt_debug("pipe-%s-%s: no HISTOGRAM_CAPS property, skipping\n",
					  kmstest_pipe_name(pipe), igt_output_name(output));
				continue;
			}

			igt_display_reset(&data->display);

			igt_output_set_crtc(output, igt_crtc_for_pipe(&data->display, pipe));
			if (!intel_pipe_output_combo_valid(&data->display)) {
				igt_debug("pipe-%s-%s: invalid pipe/output combo, skipping\n",
					  kmstest_pipe_name(pipe), igt_output_name(output));
				continue;
			}

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

static void run_dpst_static_image_test(data_t *data, const struct dpst_static_image *img)
{
#ifdef HAVE_LIBDPST
	data->static_image = img;
	run_tests_for_global_histogram(data, false, dpst_static_image_validate);
	data->static_image = NULL;
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

	for (int i = 0; i < ARRAY_SIZE(dpst_static_images); i++) {
		igt_describe_f("Test to enable histogram, flip the %s static image, wait for "
			       "histogram event, compute IET LUT and backlight using DPST "
			       "algorithm and compare them against the expected values.",
			       dpst_static_images[i].name);
		igt_subtest_with_dynamic_f("dpst-static-%s", dpst_static_images[i].name)
			run_dpst_static_image_test(&data, &dpst_static_images[i]);
	}

	igt_fixture() {
		igt_display_fini(&data.display);
		drm_close_driver(data.drm_fd);
	}
}

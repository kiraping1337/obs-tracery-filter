/*
Tracery plugin for OBS
Copyright (C) 2026 kiraping

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>

#define MAX_BLOBS 64

struct blob {
	int x, y, width, height;
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

struct tracery_data {
	obs_source_t *source;
	uint32_t key_color;
	int threshold;

	struct blob blobs[MAX_BLOBS];
	int blob_count;

	gs_stagesurf_t *stagesurface;
	uint32_t width;
	uint32_t height;
	int min_distance;
};

static const char *filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Tracery Filter";
}

static bool color_match(uint8_t r, uint8_t g, uint8_t b, uint8_t kr, uint8_t kg, uint8_t kb, int threshold)
{
	int dr = abs((int)r - (int)kr);
	int dg = abs((int)g - (int)kg);
	int db = abs((int)b - (int)kb);
	return dr <= threshold && dg <= threshold && db <= threshold;
}

static obs_properties_t *filter_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_color(props, "key_color", "Key Color");
	obs_properties_add_int_slider(props, "threshold", "Threshold", 0, 255, 1);
	obs_properties_add_int_slider(props, "min_distance", "Min Distance", 1, 500, 1);
	return props;
}

static void filter_update(void *data, obs_data_t *settings)
{
	struct tracery_data *filter = data;
	filter->key_color = (uint32_t)obs_data_get_int(settings, "key_color");
	filter->threshold = (int)obs_data_get_int(settings, "threshold");
	filter->min_distance = (int)obs_data_get_int(settings, "min_distance");
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct tracery_data *filter = bzalloc(sizeof(struct tracery_data));
	filter->source = source;
	obs_log(LOG_INFO, "Tracery filter created");
	return filter;
}

static void filter_destroy(void *data)
{
	struct tracery_data *filter = data;
	obs_enter_graphics();
	if (filter->stagesurface)
		gs_stagesurface_destroy(filter->stagesurface);
	obs_leave_graphics();
	bfree(filter);
	obs_log(LOG_INFO, "Tracery filter destroyed");
}

static void filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct tracery_data *filter = data;

	obs_source_t *target = obs_filter_get_target(filter->source);
	if (!target)
		return;

	uint32_t w = obs_source_get_base_width(target);
	uint32_t h = obs_source_get_base_height(target);

	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	if (filter->width != w || filter->height != h) {
		if (filter->stagesurface)
			gs_stagesurface_destroy(filter->stagesurface);
		filter->stagesurface = gs_stagesurface_create(w, h, GS_BGRA);
		filter->width = w;
		filter->height = h;
	}

	obs_source_skip_video_filter(filter->source);

	gs_stage_texture(filter->stagesurface, gs_get_render_target());

	uint8_t *ptr;
	uint32_t linesize;
	if (gs_stagesurface_map(filter->stagesurface, &ptr, &linesize)) {
		uint8_t kr = (filter->key_color >> 16) & 0xFF;
		uint8_t kg = (filter->key_color >> 8) & 0xFF;
		uint8_t kb = (filter->key_color) & 0xFF;

		filter->blob_count = 0;

		for (uint32_t y = 0; y < h; y++) {
			uint8_t *row = ptr + y * linesize;
			for (uint32_t x = 0; x < w; x++) {
				uint8_t b = row[x * 4 + 0];
				uint8_t g = row[x * 4 + 1];
				uint8_t r = row[x * 4 + 2];

				if (!color_match(r, g, b, kr, kg, kb, filter->threshold))
					continue;

				int nearest = -1;
				for (int i = 0; i < filter->blob_count; i++) {
					struct blob *bl = &filter->blobs[i];
					int cx = bl->x + bl->width / 2;
					int cy = bl->y + bl->height / 2;
					int dx = (int)x - cx;
					int dy = (int)y - cy;
					int dist = dx * dx + dy * dy;
					int threshold = filter->min_distance * filter->min_distance;
					if (dist < threshold) {
						nearest = i;
						break;
					}
				}

				if (nearest == -1) {
					if (filter->blob_count < MAX_BLOBS) {
						filter->blobs[filter->blob_count].x = (int)x;
						filter->blobs[filter->blob_count].y = (int)y;
						filter->blobs[filter->blob_count].width = 1;
						filter->blobs[filter->blob_count].height = 1;
						filter->blob_count++;
					}
				} else {
					struct blob *bl = &filter->blobs[nearest];
					int x2 = bl->x + bl->width;
					int y2 = bl->y + bl->height;
					if ((int)x < bl->x)
						bl->x = (int)x;
					if ((int)y < bl->y)
						bl->y = (int)y;
					if ((int)x > x2)
						x2 = (int)x;
					if ((int)y > y2)
						y2 = (int)y;
					bl->width = x2 - bl->x;
					bl->height = y2 - bl->y;
				}
			}
		}
	}

	//рисование боксов
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");

	struct vec4 red;
	vec4_set(&red, 1.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(color_param, &red);

	while (gs_effect_loop(solid, "Solid")) {
		for (int i = 0; i < filter->blob_count; i++) {
			struct blob *b = &filter->blobs[i];
			float x = (float)b->x;
			float y = (float)b->y;
			float w2 = (float)b->width;
			float h2 = (float)b->height;
			float t = 2.0f; //толщина рамки

			//верх
			gs_matrix_push();
			gs_matrix_translate3f(x, y, 0.0f);
			gs_draw_sprite(NULL, 0, (uint32_t)w2, (uint32_t)t);
			gs_matrix_pop();

			//низ
			gs_matrix_push();
			gs_matrix_translate3f(x, y + h2 - t, 0.0f);
			gs_draw_sprite(NULL, 0, (uint32_t)w2, (uint32_t)t);
			gs_matrix_pop();

			//лево
			gs_matrix_push();
			gs_matrix_translate3f(x, y, 0.0f);
			gs_draw_sprite(NULL, 0, (uint32_t)t, (uint32_t)h2);
			gs_matrix_pop();

			//право
			gs_matrix_push();
			gs_matrix_translate3f(x + w2 - t, y, 0.0f);
			gs_draw_sprite(NULL, 0, (uint32_t)t, (uint32_t)h2);
			gs_matrix_pop();
		}
	}
}

static struct obs_source_info tracery_filter_info = {
	.id = "tracery_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_get_name,
	.create = filter_create,
	.destroy = filter_destroy,
	.video_render = filter_render,
	.get_properties = filter_properties,
	.update = filter_update,
};

bool obs_module_load(void)
{
	obs_register_source(&tracery_filter_info);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
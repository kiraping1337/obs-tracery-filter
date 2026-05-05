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
#include <windows.h>
#include <stdio.h>
#include <math.h>

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

	bool show_markers;
	float curvature;

	float line_thickness;
	bool dashed_lines;
	float dash_length;
	float gap_length;

	bool corner_style;
	float corner_length;

	uint32_t box_color;
	uint32_t line_color;
	uint32_t marker_color;

	bool show_lines;
	bool show_boxes;
	bool show_labels;

	char font_name[256];
	int font_size;
	bool text_outline;
	uint32_t outline_color;
	int outline_thickness;
	uint32_t text_color;

	int scan_step;
	int frame_skip;
	int frame_counter;

	gs_texrender_t *texrender;

	int min_blob_size;

	struct blob prev_blobs[MAX_BLOBS];
	int prev_blob_count;
	float smoothing;
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
	obs_properties_add_float_slider(props, "smoothing", "Smoothing", 0.0f, 0.95f, 0.05f);
	obs_properties_add_int_slider(props, "min_distance", "Min Distance", 1, 500, 1);
	obs_properties_add_int_slider(props, "min_blob_size", "Min Blob Size", 1, 200, 1);
	obs_properties_add_int_slider(props, "scan_step", "Detection Quality (1=best)", 1, 8, 1);
	obs_properties_add_int_slider(props, "frame_skip", "Update Every N Frames", 1, 10, 1);
	obs_properties_add_color(props, "box_color", "Box Color");
	obs_properties_add_bool(props, "show_markers", "Show Center Markers");
	obs_properties_add_bool(props, "show_boxes", "Show boxes");
	obs_properties_add_bool(props, "show_labels", "Show labels with coordinates");
	obs_properties_add_color(props, "marker_color", "Marker Color");
	obs_properties_add_float_slider(props, "curvature", "Curvature", 0.0f, 1.0f, 0.01f);
	obs_properties_add_float_slider(props, "line_thickness", "Line Thickness", 1.0f, 20.0f, 0.5f);
	obs_properties_add_bool(props, "show_lines", "Show lines");
	obs_properties_add_color(props, "line_color", "Line Color");
	obs_properties_add_bool(props, "dashed_lines", "Dashed Lines");
	obs_properties_add_float_slider(props, "dash_length", "Dash Length", 1.0f, 100.0f, 1.0f);
	obs_properties_add_float_slider(props, "gap_length", "Gap Length", 1.0f, 100.0f, 1.0f);
	obs_properties_add_bool(props, "corner_style", "Corner Style");
	obs_properties_add_float_slider(props, "corner_length", "Corner Length", 5.0f, 100.0f, 1.0f);
	obs_properties_add_font(props, "font", "Font");
	obs_properties_add_bool(props, "text_outline", "Outline");
	obs_properties_add_color(props, "outline_color", "Outline Color");
	obs_properties_add_int_slider(props, "outline_thickness", "Outline Thickness", 1, 10, 1);
	obs_properties_add_color(props, "text_color", "Text Color");
	return props;
}

static void filter_update(void *data, obs_data_t *settings)
{
	struct tracery_data *filter = data;
	filter->key_color = (uint32_t)obs_data_get_int(settings, "key_color");
	filter->threshold = (int)obs_data_get_int(settings, "threshold");
	filter->min_distance = (int)obs_data_get_int(settings, "min_distance");
	filter->show_markers = obs_data_get_bool(settings, "show_markers");
	filter->curvature = (float)obs_data_get_double(settings, "curvature");
	filter->line_thickness = (float)obs_data_get_double(settings, "line_thickness");
	filter->dashed_lines = obs_data_get_bool(settings, "dashed_lines");
	filter->dash_length = (float)obs_data_get_double(settings, "dash_length");
	filter->gap_length = (float)obs_data_get_double(settings, "gap_length");
	filter->corner_style = obs_data_get_bool(settings, "corner_style");
	filter->corner_length = (float)obs_data_get_double(settings, "corner_length");
	filter->box_color = (uint32_t)obs_data_get_int(settings, "box_color");
	filter->line_color = (uint32_t)obs_data_get_int(settings, "line_color");
	filter->marker_color = (uint32_t)obs_data_get_int(settings, "marker_color");
	filter->show_boxes = obs_data_get_bool(settings, "show_boxes");
	filter->show_labels = obs_data_get_bool(settings, "show_labels");
	filter->show_lines = obs_data_get_bool(settings, "show_lines");
	obs_data_t *font_obj = obs_data_get_obj(settings, "font");
	const char *fn = obs_data_get_string(font_obj, "face");
	strncpy(filter->font_name, fn, sizeof(filter->font_name) - 1);
	filter->font_size = (int)obs_data_get_int(font_obj, "size");
	obs_data_release(font_obj);
	filter->text_outline = obs_data_get_bool(settings, "text_outline");
	filter->outline_color = (uint32_t)obs_data_get_int(settings, "outline_color");
	filter->outline_thickness = (int)obs_data_get_int(settings, "outline_thickness");
	filter->text_color = (uint32_t)obs_data_get_int(settings, "text_color");
	filter->scan_step = (int)obs_data_get_int(settings, "scan_step");
	filter->frame_skip = (int)obs_data_get_int(settings, "frame_skip");
	filter->min_blob_size = (int)obs_data_get_int(settings, "min_blob_size");
	filter->smoothing = (float)obs_data_get_double(settings, "smoothing");
}

static void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "corner_length", 20.0);
	obs_data_set_default_double(settings, "line_thickness", 2.0);
	obs_data_set_default_double(settings, "curvature", 0.5);
	obs_data_set_default_double(settings, "dash_length", 10.0);
	obs_data_set_default_double(settings, "gap_length", 10.0);
	obs_data_set_default_int(settings, "threshold", 30);
	obs_data_set_default_int(settings, "min_distance", 50);
	obs_data_set_default_int(settings, "box_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "line_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "marker_color", 0xFFFFFFFF);
	obs_data_set_default_bool(settings, "show_lines", true);
	obs_data_set_default_bool(settings, "show_boxes", true);
	obs_data_set_default_bool(settings, "show_labels", true);
	obs_data_t *font_obj = obs_data_create();
	obs_data_set_string(font_obj, "face", "Arial");
	obs_data_set_int(font_obj, "size", 20);
	obs_data_set_default_obj(settings, "font", font_obj);
	obs_data_release(font_obj);
	obs_data_set_default_int(settings, "outline_color", 0x000000FF);
	obs_data_set_default_int(settings, "outline_thickness", 2);
	obs_data_set_default_int(settings, "text_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "scan_step", 2);
	obs_data_set_default_int(settings, "frame_skip", 2);
	obs_data_set_default_int(settings, "min_blob_size", 10);
	obs_data_set_default_double(settings, "smoothing", 0.5);
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct tracery_data *filter = bzalloc(sizeof(struct tracery_data));
	obs_enter_graphics();
	filter->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	obs_leave_graphics();
	filter->source = source;
	filter_update(filter, settings);
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
	if (filter->texrender)
		gs_texrender_destroy(filter->texrender);
	bfree(filter);
	obs_log(LOG_INFO, "Tracery filter destroyed");
}



static void filter_detect_blobs(struct tracery_data *filter, uint8_t *ptr, uint32_t linesize, uint32_t w, uint32_t h)
{
	uint8_t kr = (filter->key_color >> 16) & 0xFF;
	uint8_t kg = (filter->key_color >> 8) & 0xFF;
	uint8_t kb = (filter->key_color) & 0xFF;

	filter->blob_count = 0;

	for (uint32_t y = 0; y < h; y += filter->scan_step) {
		uint8_t *row = ptr + y * linesize;
		for (uint32_t x = 0; x < w; x += filter->scan_step) {
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
	//удаляем слишком маленькие боксы
	int new_count = 0;
	for (int i = 0; i < filter->blob_count; i++) {
		if (filter->blobs[i].width >= filter->min_blob_size &&
		    filter->blobs[i].height >= filter->min_blob_size) {
			filter->blobs[new_count++] = filter->blobs[i];
		}
	}
	filter->blob_count = new_count;

	//сглаживаем позиции относительно предыдущего кадра
	if (filter->prev_blob_count == filter->blob_count) {
		for (int i = 0; i < filter->blob_count; i++) {
			float s = filter->smoothing;
			filter->blobs[i].x = (int)(filter->blobs[i].x * (1 - s) + filter->prev_blobs[i].x * s);
			filter->blobs[i].y = (int)(filter->blobs[i].y * (1 - s) + filter->prev_blobs[i].y * s);
			filter->blobs[i].width =
				(int)(filter->blobs[i].width * (1 - s) + filter->prev_blobs[i].width * s);
			filter->blobs[i].height =
				(int)(filter->blobs[i].height * (1 - s) + filter->prev_blobs[i].height * s);
		}
	}

	memcpy(filter->prev_blobs, filter->blobs, sizeof(struct blob) * filter->blob_count);
	filter->prev_blob_count = filter->blob_count;
}

static void filter_draw_boxes(struct tracery_data *filter)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");

	struct vec4 box_col;
	uint32_t c = filter->box_color;
	float b = ((c >> 24) & 0xFF) / 255.0f;
	float g = ((c >> 16) & 0xFF) / 255.0f;
	float r = ((c >> 8) & 0xFF) / 255.0f;
	float a = 1.0f;
	vec4_set(&box_col, r, g, b, a);
	gs_effect_set_vec4(color_param, &box_col);

	while (gs_effect_loop(solid, "Solid")) {
		for (int i = 0; i < filter->blob_count; i++) {
			struct blob *b = &filter->blobs[i];
			float x = (float)b->x;
			float y = (float)b->y;
			float w2 = (float)b->width;
			float h2 = (float)b->height;
			float t = 2.0f; //толщина рамки

			if (filter->corner_style) {
				float cl = filter->corner_length;
				if (cl > w2 / 2.0f)
					cl = w2 / 2.0f;
				if (cl > h2 / 2.0f)
					cl = h2 / 2.0f;

				// верхний левый
				gs_matrix_push();
				gs_matrix_translate3f(x, y, 0);
				gs_draw_sprite(NULL, 0, (uint32_t)cl, (uint32_t)t);
				gs_matrix_pop();
				gs_matrix_push();
				gs_matrix_translate3f(x, y, 0);
				gs_draw_sprite(NULL, 0, (uint32_t)t, (uint32_t)cl);
				gs_matrix_pop();

				// верхний правый
				gs_matrix_push();
				gs_matrix_translate3f(x + w2 - cl, y, 0);
				gs_draw_sprite(NULL, 0, (uint32_t)cl, (uint32_t)t);
				gs_matrix_pop();
				gs_matrix_push();
				gs_matrix_translate3f(x + w2 - t, y, 0);
				gs_draw_sprite(NULL, 0, (uint32_t)t, (uint32_t)cl);
				gs_matrix_pop();

				// нижний левый
				gs_matrix_push();
				gs_matrix_translate3f(x, y + h2 - t, 0);
				gs_draw_sprite(NULL, 0, (uint32_t)cl, (uint32_t)t);
				gs_matrix_pop();
				gs_matrix_push();
				gs_matrix_translate3f(x, y + h2 - cl, 0);
				gs_draw_sprite(NULL, 0, (uint32_t)t, (uint32_t)cl);
				gs_matrix_pop();

				// нижний правый
				gs_matrix_push();
				gs_matrix_translate3f(x + w2 - cl, y + h2 - t, 0);
				gs_draw_sprite(NULL, 0, (uint32_t)cl, (uint32_t)t);
				gs_matrix_pop();
				gs_matrix_push();
				gs_matrix_translate3f(x + w2 - t, y + h2 - cl, 0);
				gs_draw_sprite(NULL, 0, (uint32_t)t, (uint32_t)cl);
				gs_matrix_pop();

			} else {

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
}

static void filter_draw_labels(struct tracery_data *filter)
{
	if (filter->blob_count == 0)
		return;

	int bw = (int)filter->width;
	int bh = (int)filter->height;

	HDC hdc = CreateCompatibleDC(NULL);
	HFONT hfont = CreateFontA(filter->font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
				  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
				  DEFAULT_PITCH | FF_DONTCARE, filter->font_name);
	SelectObject(hdc, hfont);

	BITMAPINFO bmi = {0};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = bw;
	bmi.bmiHeader.biHeight = -bh;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *bits = NULL;
	HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	SelectObject(hdc, hbmp);
	memset(bits, 1, bw * bh * 4);
	SetBkMode(hdc, TRANSPARENT);

	uint8_t or2 = (filter->outline_color >> 0) & 0xFF;
	uint8_t og = (filter->outline_color >> 8) & 0xFF;
	uint8_t ob = (filter->outline_color >> 16) & 0xFF;
	COLORREF outline_rgb = RGB(or2, og, ob);

	uint8_t tr = (filter->text_color >> 0) & 0xFF;
	uint8_t tg = (filter->text_color >> 8) & 0xFF;
	uint8_t tb = (filter->text_color >> 16) & 0xFF;

	for (int i = 0; i < filter->blob_count; i++) {
		struct blob *b = &filter->blobs[i];
		int cx = b->x + b->width / 2;
		int cy = b->y + b->height / 2;
		char label[64];
		snprintf(label, sizeof(label), "x:%d y:%d", cx, cy);
		int len = (int)strlen(label);

		int tx = b->x + filter->outline_thickness;
		int ty = b->y - filter->font_size - 4 + filter->outline_thickness;

		if (filter->text_outline) {
			HPEN pen = CreatePen(PS_SOLID, filter->outline_thickness * 2, outline_rgb);
			SelectObject(hdc, pen);
			SelectObject(hdc, GetStockObject(NULL_BRUSH));
			BeginPath(hdc);
			TextOutA(hdc, tx, ty, label, len);
			EndPath(hdc);
			StrokePath(hdc);
			DeleteObject(pen);
		}

		SetTextColor(hdc, RGB(tr, tg, tb));
		TextOutA(hdc, tx, ty, label, len);
	}

	GdiFlush();

	uint8_t *p = (uint8_t *)bits;
	for (int i = 0; i < bw * bh; i++) {
		if (p[i * 4 + 0] != 1 || p[i * 4 + 1] != 1 || p[i * 4 + 2] != 1) {
			p[i * 4 + 3] = 255;
		} else {
			p[i * 4 + 0] = 0;
			p[i * 4 + 1] = 0;
			p[i * 4 + 2] = 0;
			p[i * 4 + 3] = 0;
		}
	}

	gs_texture_t *tex = gs_texture_create(bw, bh, GS_BGRA, 1, (const uint8_t **)&bits, 0);
	gs_effect_t *eff = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *img = gs_effect_get_param_by_name(eff, "image");
	gs_effect_set_texture(img, tex);

	while (gs_effect_loop(eff, "Draw")) {
		gs_draw_sprite(tex, 0, (uint32_t)bw, (uint32_t)bh);
	}

	gs_texture_destroy(tex);
	DeleteObject(hbmp);
	DeleteObject(hfont);
	DeleteDC(hdc);
}

static void filter_draw_markers(struct tracery_data *filter)
{
	gs_effect_t *solid2 = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color2 = gs_effect_get_param_by_name(solid2, "color");

	struct vec4 marker_col;
	uint32_t c = filter->marker_color;
	float b = ((c >> 24) & 0xFF) / 255.0f;
	float g = ((c >> 16) & 0xFF) / 255.0f;
	float r = ((c >> 8) & 0xFF) / 255.0f;
	float a = 1.0f;
	vec4_set(&marker_col, r, g, b, a);
	gs_effect_set_vec4(color2, &marker_col);

	while (gs_effect_loop(solid2, "Solid")) {
		for (int i = 0; i < filter->blob_count; i++) {
			struct blob *b = &filter->blobs[i];
			float cx = (float)(b->x + b->width / 2);
			float cy = (float)(b->y + b->height / 2);
			float size = 5.0f;

			gs_matrix_push();
			gs_matrix_translate3f(cx - size / 2, cy - size / 2, 0.0f);
			gs_draw_sprite(NULL, 0, (uint32_t)size, (uint32_t)size);
			gs_matrix_pop();
		}
	}
}

static void filter_draw_connections(struct tracery_data *filter)
{
	if (filter->blob_count < 2)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");

	struct vec4 line_col;
	uint32_t c = filter->line_color;
	float b = ((c >> 24) & 0xFF) / 255.0f;
	float g = ((c >> 16) & 0xFF) / 255.0f;
	float r = ((c >> 8) & 0xFF) / 255.0f;
	float a = 1.0f;
	vec4_set(&line_col, r, g, b, a);
	gs_effect_set_vec4(color_param, &line_col);

	float half_t = filter->line_thickness / 2.0f;
	int steps = 60;

	while (gs_effect_loop(solid, "Solid")) {
		for (int i = 0; i < filter->blob_count - 1; i++) {
			float x1 = (float)(filter->blobs[i].x + filter->blobs[i].width / 2);
			float y1 = (float)(filter->blobs[i].y + filter->blobs[i].height / 2);
			float x2 = (float)(filter->blobs[i + 1].x + filter->blobs[i + 1].width / 2);
			float y2 = (float)(filter->blobs[i + 1].y + filter->blobs[i + 1].height / 2);

			float dx = x2 - x1;
			float dy = y2 - y1;
			float len = sqrtf(dx * dx + dy * dy);
			if (len < 1.0f)
				continue;

			float mx = (x1 + x2) / 2.0f;
			float my = (y1 + y2) / 2.0f;
			float offset = len * filter->curvature * 0.5f;
			float cpx = mx - dy / len * offset;
			float cpy = my + dx / len * offset;

			float pts_x[61], pts_y[61];
			for (int s = 0; s <= steps; s++) {
				float t = (float)s / (float)steps;
				float mt = 1.0f - t;
				pts_x[s] = mt * mt * x1 + 2.0f * mt * t * cpx + t * t * x2;
				pts_y[s] = mt * mt * y1 + 2.0f * mt * t * cpy + t * t * y2;
			}

			float dash_accum = 0.0f;
			bool dash_on = true;

			for (int s = 0; s < steps; s++) {
				float ax = pts_x[s], ay = pts_y[s];
				float bx = pts_x[s + 1], by = pts_y[s + 1];

				float sdx = bx - ax;
				float sdy = by - ay;
				float seg_len = sqrtf(sdx * sdx + sdy * sdy);
				if (seg_len < 0.001f)
					continue;

				if (filter->dashed_lines) {
					dash_accum += seg_len;
					if (dash_on && dash_accum >= filter->dash_length) {
						dash_on = false;
						dash_accum -= filter->dash_length;
					} else if (!dash_on && dash_accum >= filter->gap_length) {
						dash_on = true;
						dash_accum -= filter->gap_length;
					}
					if (!dash_on)
						continue;
				}

				float nx = -sdy / seg_len * half_t;
				float ny = sdx / seg_len * half_t;

				gs_render_start(false);
				gs_vertex2f(ax + nx, ay + ny);
				gs_vertex2f(ax - nx, ay - ny);
				gs_vertex2f(bx + nx, by + ny);
				gs_vertex2f(bx - nx, by - ny);
				gs_render_stop(GS_TRISTRIP);
			}
		}
	}
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

	filter->frame_counter++;
	if (filter->frame_counter >= filter->frame_skip) {
		filter->frame_counter = 0;

		gs_texrender_reset(filter->texrender);
		if (gs_texrender_begin(filter->texrender, w, h)) {
			struct vec4 clear_color;
			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
			obs_source_video_render(obs_filter_get_target(filter->source));
			gs_texrender_end(filter->texrender);
		}

		gs_stage_texture(filter->stagesurface, gs_texrender_get_texture(filter->texrender));

		uint8_t *ptr;
		uint32_t linesize;
		if (gs_stagesurface_map(filter->stagesurface, &ptr, &linesize)) {
			filter_detect_blobs(filter, ptr, linesize, w, h);
			gs_stagesurface_unmap(filter->stagesurface);
		}
	}

	if (filter->show_boxes) {
		filter_draw_boxes(filter);
	}
	if (filter->show_labels) {
		filter_draw_labels(filter);
	}

	if (filter->show_markers) {
		filter_draw_markers(filter);
	}
	if (filter->show_lines) {
		filter_draw_connections(filter);
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
	.get_defaults = filter_defaults,
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
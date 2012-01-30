/*
 * Copyright (c) 2011-2012 Luc Verhaegen <libv@codethink.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 *
 * Code to help deal with setting up command streams for GP vs/plbu.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "premali.h"
#include "ioctl.h"
#include "plb.h"
#include "symbols.h"
#include "gp.h"
#include "jobs.h"
#include "vs.h"
#include "plbu.h"
#include "render_state.h"
#include "hfloat.h"

int
vs_command_queue_create(struct premali_state *state, int offset, int size)
{
	state->vs_commands = state->mem_address + offset;
	state->vs_commands_physical = state->mem_physical + offset;
	state->vs_commands_count = 0;
	state->vs_commands_size = size / 8;

	return 0;
}

int
plbu_command_queue_create(struct premali_state *state, int offset, int size)
{
	struct plb *plb = state->plb;
	struct mali_cmd *cmds;
	int i = 0;

	state->plbu_commands = state->mem_address + offset;
	state->plbu_commands_physical = state->mem_physical + offset;
	state->plbu_commands_count = 0;
	state->plbu_commands_size = size / 8;

	cmds = state->plbu_commands;

	cmds[i].val = plb->shift_w | (plb->shift_h << 16);
	if (state->type == PREMALI_TYPE_MALI400) {
		int block_max;

		if (plb->shift_h > plb->shift_w)
			block_max = plb->shift_h;
		else
			block_max = plb->shift_w;

		if (block_max > 2)
			block_max = 2;

		cmds[i].val |= block_max << 28;
	}
	cmds[i].cmd = MALI_PLBU_CMD_BLOCK_STEP;
	i++;

	cmds[i].val = ((plb->width - 1) << 24) | ((plb->height - 1) << 8);
	cmds[i].cmd = MALI_PLBU_CMD_TILED_DIMENSIONS;
	i++;

	cmds[i].val = plb->width >> plb->shift_w;
	cmds[i].cmd = MALI_PLBU_CMD_PLBU_BLOCK_STRIDE;
	i++;

	cmds[i].val = plb->mem_physical + plb->plbu_offset;
	if (state->type == PREMALI_TYPE_MALI200)
		cmds[i].cmd = MALI200_PLBU_CMD_PLBU_ARRAY_ADDRESS;
	else if (state->type == PREMALI_TYPE_MALI400) {
		cmds[i].cmd = MALI400_PLBU_CMD_PLBU_ARRAY_ADDRESS;
		cmds[i].cmd |= ((plb->width * plb->height) >>
				(plb->shift_w + plb->shift_h)) - 1;
	}
	i++;

#if 0
	cmds[i].val = 0x40100000;
	cmds[i].cmd = MALI_PLBU_CMD_TILE_HEAP_START;
	i++;

	cmds[i].val = 0x40150000;
	cmds[i].cmd = MALI_PLBU_CMD_TILE_HEAP_END;
	i++;
#endif

	cmds[i].val = from_float(0.0);
	cmds[i].cmd = MALI_PLBU_CMD_VIEWPORT_Y;
	i++;

	cmds[i].val = from_float(state->height);
	cmds[i].cmd = MALI_PLBU_CMD_VIEWPORT_H;
	i++;

	cmds[i].val = from_float(0.0);
	cmds[i].cmd = MALI_PLBU_CMD_VIEWPORT_X;
	i++;

	cmds[i].val = from_float(state->width);
	cmds[i].cmd = MALI_PLBU_CMD_VIEWPORT_W;
	i++;

	cmds[i].val = 0x00000000;
	cmds[i].cmd = 0x1000010a;
	i++;

	cmds[i].val = from_float(0.0);
	cmds[i].cmd = MALI_PLBU_CMD_DEPTH_RANGE_NEAR;
	i++;

	cmds[i].val = from_float(1.0);
	cmds[i].cmd = MALI_PLBU_CMD_DEPTH_RANGE_FAR;
	i++;

	state->plbu_commands_count = i;

	return 0;
}

void
vs_info_setup(struct premali_state *state, struct draw_info *draw)
{
	struct vs_info *info = draw->vs;
	int i;

	if (state->type == PREMALI_TYPE_MALI200) {
		/* mali200 has a common area for attributes and varyings. */
		info->common = info->mem_address + info->mem_used;
		info->common_offset = info->mem_used;
		info->common_size = sizeof(struct gp_common);
		info->mem_used += ALIGN(info->common_size, 0x40);

		/* initialize common */
		for (i = 0; i < 0x10; i++) {
			info->common->attributes[i].physical = 0;
			info->common->attributes[i].size = 0x3F;
		}
		for (i = 0; i < 0x10; i++) {
			info->common->varyings[i].physical = 0;
			info->common->varyings[i].size = 0x3F;
		}
	} else if (state->type == PREMALI_TYPE_MALI400) {
		info->attribute_area = info->mem_address + info->mem_used;
		info->attribute_area_size = 0x10 * sizeof(struct gp_common_entry);
		info->attribute_area_offset = info->mem_used;
		info->mem_used += ALIGN(info->attribute_area_size, 0x40);

		info->varying_area = info->mem_address + info->mem_used;
		info->varying_area_size = 0x10 * sizeof(struct gp_common_entry);
		info->varying_area_offset = info->mem_used;
		info->mem_used += ALIGN(info->varying_area_size, 0x40);
	}
}

int
vs_info_attach_uniforms(struct vs_info *info, struct symbol **uniforms,
			int count, int size)
{
	void *address;
	int i;

	info->uniform_offset = info->mem_used;
	info->uniform_size = size;
	info->mem_used += ALIGN(size, 0x40);

	address = info->mem_address + info->uniform_offset;

	for (i = 0; i < count; i++) {
		struct symbol *symbol = uniforms[i];

		memcpy(address + symbol->component_size * symbol->offset,
		       symbol->data, symbol->size);
	}

	return 0;
}

int
vs_info_attach_attribute(struct vs_info *info, struct symbol *attribute)
{
	int size;

	if (info->attribute_count == 0x10) {
		printf("%s: No more attributes\n", __func__);
		return -1;
	}

	size = ALIGN(attribute->size, 0x40);
	if (size > (info->mem_size - info->mem_used)) {
		printf("%s: No more space\n", __func__);
		return -2;
	}

	attribute->address = info->mem_address + info->mem_used;
	attribute->physical = info->mem_physical + info->mem_used;
	info->mem_used += size;

	info->attributes[info->attribute_count] = attribute;
	info->attribute_count++;

	memcpy(attribute->address, attribute->data, attribute->size);

	return 0;
}

/* varyings are still a bit of black magic at this point */
int
vs_info_attach_varying(struct vs_info *info, struct symbol *varying)
{
	int size;

	size = ALIGN(varying->size, 0x40);
	if (size > (info->mem_size - info->mem_used)) {
		printf("%s: No more space\n", __func__);
		return -2;
	}

	varying->physical = info->mem_physical + info->mem_used;
	info->mem_used += size;

	info->varyings[info->varying_count] = varying;
	info->varying_count++;

	/* the vertex shader fills in the varyings */

	return 0;
}

int
vs_info_attach_shader(struct vs_info *info, unsigned int *shader, int size)
{
	int mem_size;

	if (info->shader != NULL) {
		printf("%s: shader already assigned\n", __func__);
		return -1;
	}

	mem_size = ALIGN(size * 16, 0x40);
	if (mem_size > (info->mem_size - info->mem_used)) {
		printf("%s: no more space\n", __func__);
		return -2;
	}

	info->shader = info->mem_address + info->mem_used;
	info->shader_offset = info->mem_used;
	info->shader_size = size;
	info->mem_used += mem_size;

	memcpy(info->shader, shader, 16 * size);

	return 0;
}

void
vs_commands_draw_add(struct premali_state *state, struct draw_info *draw)
{
	struct vs_info *vs = draw->vs;
	struct mali_cmd *cmds = state->vs_commands;
	int i = state->vs_commands_count;

	cmds[i].val = MALI_VS_CMD_ARRAYS_SEMAPHORE_BEGIN_1;
	cmds[i].cmd = MALI_VS_CMD_ARRAYS_SEMAPHORE;
	i++;

	cmds[i].val = MALI_VS_CMD_ARRAYS_SEMAPHORE_BEGIN_2;
	cmds[i].cmd = MALI_VS_CMD_ARRAYS_SEMAPHORE;
	i++;

	cmds[i].val = vs->mem_physical + vs->shader_offset;
	cmds[i].cmd = MALI_VS_CMD_SHADER_ADDRESS | (vs->shader_size << 16);
	i++;

	cmds[i].val = (5 - 1) << 20; /* will become clearer when linking */
	cmds[i].val |= (vs->shader_size - 1) << 10;
	cmds[i].cmd = 0x10000040;
	i++;

	cmds[i].val = ((vs->varying_count - 1) << 8) | ((vs->attribute_count - 1) << 24);
	cmds[i].cmd = MALI_VS_CMD_VARYING_ATTRIBUTE_COUNT;
	i++;

	cmds[i].val = vs->mem_physical + vs->uniform_offset;
	cmds[i].cmd = MALI_VS_CMD_UNIFORMS_ADDRESS |
		(ALIGN(vs->uniform_size, 4) << 14);
	i++;

	if (state->type == PREMALI_TYPE_MALI200) {
		cmds[i].val = vs->mem_physical + vs->common_offset;
		cmds[i].cmd = MALI_VS_CMD_COMMON_ADDRESS |
			(vs->common_size << 14);
		i++;
	} else if (state->type == PREMALI_TYPE_MALI400) {
		cmds[i].val = vs->mem_physical + vs->attribute_area_offset;
		cmds[i].cmd = MALI_VS_CMD_ATTRIBUTES_ADDRESS |
			(vs->attribute_area_size << 11);
		i++;

		cmds[i].val = vs->mem_physical + vs->varying_area_offset;
		cmds[i].cmd = MALI_VS_CMD_VARYINGS_ADDRESS |
			(vs->varying_area_size << 11);
		i++;
	}

	cmds[i].val = 0x00000003; /* always 3 */
	cmds[i].cmd = 0x10000041;
	i++;

	cmds[i].val = (draw->vertex_count << 24);
	cmds[i].cmd = 0x00000000;
	i++;

	cmds[i].val = 0x00000000;
	cmds[i].cmd = 0x60000000;
	i++;

	cmds[i].val = MALI_VS_CMD_ARRAYS_SEMAPHORE_END;
	cmds[i].cmd = MALI_VS_CMD_ARRAYS_SEMAPHORE;
	i++;

	/* update our size so we can set the gp job properly */
	state->vs_commands_count = i * sizeof(struct mali_cmd);
}

void
vs_info_finalize(struct premali_state *state, struct vs_info *info)
{
	int i;

	if (state->type == PREMALI_TYPE_MALI200) {
		for (i = 0; i < info->attribute_count; i++) {
			info->common->attributes[i].physical = info->attributes[i]->physical;
			info->common->attributes[i].size =
				((info->attributes[i]->component_size *
				  info->attributes[i]->component_count) << 11) |
				(info->attributes[i]->component_count - 1);
		}

		for (i = 0; i < info->varying_count; i++) {
			info->common->varyings[i].physical = info->varyings[i]->physical;
			info->common->varyings[i].size = (8 << 11) |
				(info->varyings[i]->component_count *
				 info->varyings[i]->component_size - 1);
		}

		/* fix up gl_Position */
		i--;
		info->common->varyings[i].size = (16 << 11) | (1 - 1) | 0x20;

	} else if (state->type == PREMALI_TYPE_MALI400) {
		for (i = 0; i < info->attribute_count; i++) {
			info->attribute_area[i].physical = info->attributes[i]->physical;
			info->attribute_area[i].size =
				((info->attributes[i]->component_size *
				  info->attributes[i]->component_count) << 11) |
				(info->attributes[i]->component_count - 1);
		}

		for (i = 0; i < info->varying_count; i++) {
			info->varying_area[i].physical = info->varyings[i]->physical;
			info->varying_area[i].size = (8 << 11) |
				(info->varyings[i]->component_count *
				 info->varyings[i]->component_size - 1);
		}

		/* fix up gl_Position */
		i--;
		info->varying_area[i].size = (16 << 11) | (1 - 1) | 0x20;
	}
}

void
plbu_commands_draw_add(struct premali_state *state, struct draw_info *draw)
{
	struct plbu_info *info = draw->plbu;
	struct vs_info *vs = draw->vs;
	struct mali_cmd *cmds = state->plbu_commands;
	int i = state->plbu_commands_count;

	/*
	 *
	 */
	cmds[i].val = MALI_PLBU_CMD_ARRAYS_SEMAPHORE_BEGIN;
	cmds[i].cmd = MALI_PLBU_CMD_ARRAYS_SEMAPHORE;
	i++;

	cmds[i].val = 0x00002200;
	cmds[i].cmd = MALI_PLBU_CMD_PRIMITIVE_SETUP;
	i++;

	cmds[i].val = info->mem_physical + info->render_state_offset;
	cmds[i].cmd = MALI_PLBU_CMD_RSW_VERTEX_ARRAY;
	cmds[i].cmd |= vs->varyings[vs->varying_count - 1]->physical >> 4;
	i++;

	cmds[i].val = (draw->vertex_count << 24); // | start;
	cmds[i].cmd = ((draw->draw_mode & 0x1F) << 16) | (draw->vertex_count >> 8);
	i++;

	cmds[i].val = MALI_PLBU_CMD_ARRAYS_SEMAPHORE_END;
	cmds[i].cmd = MALI_PLBU_CMD_ARRAYS_SEMAPHORE;
	i++;

	/* update our size so we can set the gp job properly */
	state->plbu_commands_count = i;
}

void
plbu_commands_finish(struct premali_state *state)
{
	struct mali_cmd *cmds = state->plbu_commands;
	int i = state->plbu_commands_count;

	/*
	 * Some inter-frame communication apparently.
	 */
#if 0
	cmds[i].val = 0x400e41c0;
	cmds[i].cmd = 0xa0000103;
	i++;

	cmds[i].val = 0x400e41c4;
	cmds[i].cmd = 0xa0000104;
	i++;

	cmds[i].val = 0x400e41c8;
	cmds[i].cmd = 0xa0000107;
	i++;

	cmds[i].val = 0x400e41cc;
	cmds[i].cmd = 0xa0000108;
	i++;

	cmds[i].val = 0x400e41d0;
	cmds[i].cmd = 0xa0000105;
	i++;

	cmds[i].val = 0x400e41d4;
	cmds[i].cmd = 0xa0000106;
	i++;
#endif

	cmds[i].val = 0x00000000;
	cmds[i].cmd = 0xd0000000;
	i++;

	cmds[i].val = 0x00000000;
	cmds[i].cmd = 0xd0000000;
	i++;

	cmds[i].val = 0;
	cmds[i].cmd = MALI_PLBU_CMD_END;
	i++;

	/* update our size so we can set the gp job properly */
	state->plbu_commands_count = i;
}


struct plbu_info *
plbu_info_create(void *address, int physical, int size)
{
	struct plbu_info *info;

	info = calloc(1, sizeof(struct plbu_info));
	if (!info) {
		printf("%s: Error allocating structure: %s\n",
		       __func__, strerror(errno));
		return NULL;
	}

	info->mem_address = address;
	info->mem_physical = physical;
	info->mem_size = size;

	/* leave the rest empty for now */

	if (info->mem_used > info->mem_size) {
		printf("%s: Not enough memory\n", __func__);
		free(info);
		return NULL;
	}

	return info;
}

int
plbu_info_attach_shader(struct plbu_info *info, unsigned int *shader, int size)
{
	int mem_size;

	if (info->shader != NULL) {
		printf("%s: shader already assigned\n", __func__);
		return -1;
	}

	mem_size = ALIGN(size * 4, 0x40);
	if (mem_size > (info->mem_size - info->mem_used)) {
		printf("%s: no more space\n", __func__);
		return -2;
	}

	info->shader = info->mem_address + info->mem_used;
	info->shader_offset = info->mem_used;
	info->shader_size = size;
	info->mem_used += mem_size;

	memcpy(info->shader, shader, 4 * size);

	return 0;
}

int
plbu_info_attach_uniforms(struct plbu_info *info, struct symbol **uniforms,
			  int count, int size)
{
	void *address;
	unsigned int *array;
	int i;

	info->uniform_array_offset = info->mem_used;
	info->uniform_array_size = 4;
	info->mem_used += 0x40;

	array = info->mem_address + info->uniform_array_offset;

	info->uniform_offset = info->mem_used;
	info->uniform_size = size;
	info->mem_used += ALIGN(size, 0x40);

	address = info->mem_address + info->uniform_offset;
	array[0] = info->mem_physical + info->uniform_offset;

	for (i = 0; i < count; i++) {
		struct symbol *symbol = uniforms[i];
		hfloat *halves = address +
			symbol->component_size * symbol->offset;
		float *fulls = symbol->data;

		for (i = 0; i < symbol->component_count; i++)
			halves[i] = float_to_hfloat(fulls[i]);
	}

	return 0;
}

int
plbu_info_render_state_create(struct plbu_info *info, struct vs_info *vs)
{
	struct render_state *state;
	int size, i;

	if (info->render_state) {
		printf("%s: render_state already assigned\n", __func__);
		return -1;
	}

	size = ALIGN(sizeof(struct render_state), 0x40);
	if (size > (info->mem_size - info->mem_used)) {
		printf("%s: no more space\n", __func__);
		return -2;
	}

	if (!info->shader) {
		printf("%s: no shader attached yet!\n", __func__);
		return -3;
	}

	info->render_state = info->mem_address + info->mem_used;
	info->render_state_offset = info->mem_used;
	info->render_state_size = size;
	info->mem_used += size;

	/* this bit still needs some figuring out :) */
	state = info->render_state;

	state->unknown00 = 0;
	state->unknown04 = 0;
	state->unknown08 = 0xfc3b1ad2;
	state->unknown0C = 0x3E;
	state->depth_range = 0xFFFF0000;
	state->unknown14 = 7;
	state->unknown18 = 7;
	state->unknown1C = 0;
	state->unknown20 = 0xF807;
	state->shader_address =
		(info->mem_physical + info->shader_offset) | info->shader_size;

	state->uniforms_address = 0;

	state->textures_address = 0;
	state->unknown34 = 0x300;
	state->unknown38 = 0x2000;

	if (vs->varying_count > 1) {
		state->varyings_address = vs->varyings[0]->physical;
		state->unknown34 |= 0x01;
		state->varying_types = 0;

		for (i = 0; i < (vs->varying_count - 1); i++) {
			if (i < 10)
				state->varying_types |= 2 << (3 * i);
			else if (i == 10) {
				state->varying_types |= 2 << 30;
				state->varyings_address |= 2 >> 2;
			} else if (i == 11)
				state->varyings_address |= 2 << 1;

		}
	}

	if (info->uniform_size) {
		state->uniforms_address =
			(int) info->mem_physical + info->uniform_array_offset;

		state->uniforms_address |=
			(ALIGN(info->uniform_size, 4) / 4) - 1;

		state->unknown34 |= 0x80;
		state->unknown38 |= 0x10000;
	}

	return 0;
}

int
premali_gp_job_start(struct premali_state *state)
{
	struct mali_gp_job_start *job;

	job = calloc(1, sizeof(struct mali_gp_job_start));

	state->gp_job = job;

	job->frame.vs_commands_start = state->vs_commands_physical;
	job->frame.vs_commands_end =
		state->vs_commands_physical + 8 * state->vs_commands_count;
	job->frame.plbu_commands_start = state->plbu_commands_physical;
	job->frame.plbu_commands_end =
		state->plbu_commands_physical + 8 * state->plbu_commands_count;
	job->frame.tile_heap_start = 0;
	job->frame.tile_heap_end = 0;

	return premali_gp_job_start_direct(state, job);
}

struct draw_info *
draw_create_new(struct premali_state *state, int offset, int size,
		int draw_mode, int vertex_start, int vertex_count)
{
	struct draw_info *draw = calloc(1, sizeof(struct draw_info));

	if (!draw)
		return NULL;

	draw->mem_address = state->mem_address + offset;
	draw->mem_physical = state->mem_physical + offset;
	draw->mem_used = 0;
	draw->mem_size = size;

	draw->draw_mode = draw_mode;
	draw->vertex_start = vertex_start;
	draw->vertex_count = vertex_count;


	draw->vs->mem_address = draw->mem_address;
	draw->vs->mem_physical = draw->mem_physical;
	draw->vs->mem_used = 0;
	draw->vs->mem_size = draw->mem_size / 2;

	vs_info_setup(state, draw);

	draw->plbu->mem_address = draw->mem_address + draw->mem_size / 2;
	draw->plbu->mem_physical = draw->mem_physical + draw->mem_size / 2;
	draw->plbu->mem_used = 0;
	draw->plbu->mem_size = draw->mem_size / 2;

	return draw;
}

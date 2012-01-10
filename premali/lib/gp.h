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

#ifndef PREMALI_GP_H
#define PREMALI_GP_H 1

struct gp_common_entry {
	unsigned int physical;
	int size; /* (element_size << 11) | (element_count - 1) */
};

struct gp_common {
	struct gp_common_entry attributes[0x10];
	struct gp_common_entry varyings[0x10];
};

struct vs_info {
	void *mem_address;
	int mem_physical;
	int mem_used;
	int mem_size;

	struct mali_cmd *commands;
	int commands_offset;
	int commands_size;

	struct gp_common *common;
	int common_offset;
	int common_size;

	struct symbol *uniforms[0x10];
	int uniform_count;
	int uniform_offset;
	int uniform_used;
	int uniform_size;

	struct symbol *attributes[0x10];
	int attribute_count;

	struct symbol *varyings[0x10];
	int varying_count;
	int varying_element_size;

	/* finishes off our varyings */
	int vertex_array_offset;
	int vertex_array_size;

	unsigned int *shader;
	int shader_offset;
	int shader_size;
};

struct vs_info *vs_info_create(void *address, int physical, int size);
int vs_info_attach_uniform(struct vs_info *info, struct symbol *uniform);
int vs_info_attach_standard_uniforms(struct vs_info *info, int width, int height);
int vs_info_attach_attribute(struct vs_info *info, struct symbol *attribute);
int vs_info_attach_varying(struct vs_info *info, struct symbol *varying);
int vs_info_attach_shader(struct vs_info *info, unsigned int *shader, int size);
void vs_info_finalize(struct vs_info *info);

struct plbu_info {
	void *mem_address;
	int mem_physical;
	int mem_used;
	int mem_size;

	struct mali_cmd *commands;
	int commands_offset;
	int commands_size;

	unsigned int *render_state;
	int render_state_offset;
	int render_state_size;

	unsigned int *shader;
	int shader_offset;
	int shader_size;
};

void plbu_commands_create(struct plbu_info *info, int width, int height,
			  struct plb *plb, struct vs_info *vs,
			  int draw_mode, int vertex_count);
struct plbu_info *plbu_info_create(void *address, int physical, int size);
int plbu_info_attach_shader(struct plbu_info *info, unsigned int *shader, int size);
int plbu_info_finalize(struct plbu_info *info, struct plb *plb, struct vs_info *vs,
		       int width, int height, int draw_mode, int vertex_count);

void gp_job_setup(_mali_uk_gp_start_job_s *gp_job, struct vs_info *vs,
		  struct plbu_info *plbu);

#endif /* PREMALI_GP_H */
/*
 * © Copyright 2017 Cafe Beverage. All rights reserved.
 * © Copyright 2017 The BiOpenly Community
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

#include "panwrap.h"
#include <mali-ioctl.h>
#include <mali-job.h>

static char *panwrap_job_type_name(enum mali_job_type type)
{
#define DEFINE_CASE(name) case JOB_TYPE_ ## name: return #name
	switch (type) {
	DEFINE_CASE(NULL);
	DEFINE_CASE(SET_VALUE);
	DEFINE_CASE(CACHE_FLUSH);
	DEFINE_CASE(COMPUTE);
	DEFINE_CASE(VERTEX);
	DEFINE_CASE(TILER);
	DEFINE_CASE(FUSED);
	DEFINE_CASE(FRAGMENT);
	case JOB_NOT_STARTED:
		return "NOT_STARTED";
	default:
		panwrap_log("Warning! Unknown job type %x\n", type);
		return "!?!?!?";
	}
#undef DEFINE_CASE
}

static char *panwrap_gl_mode_name(enum mali_gl_mode mode)
{
#define DEFINE_CASE(name) case MALI_ ## name: return #name
	switch(mode) {
	DEFINE_CASE(GL_POINTS);
	DEFINE_CASE(GL_LINES);
	DEFINE_CASE(GL_TRIANGLES);
	DEFINE_CASE(GL_TRIANGLE_STRIP);
	DEFINE_CASE(GL_TRIANGLE_FAN);
	default: return "!!! GL_UNKNOWN !!!";
	}
#undef DEFINE_CASE
}

void panwrap_decode_attributes(const struct panwrap_mapped_memory *mem,
			       mali_ptr addr)
{
	struct mali_vertex_tiler_attr *attr =
		panwrap_deref_gpu_mem(mem, addr, sizeof(*attr));
	float *buffer = panwrap_deref_gpu_mem(
	    NULL, attr->elements_upper << 2, attr->size);
	size_t vertex_count;
	size_t component_count;

	vertex_count = attr->size / attr->stride;
	component_count = attr->stride / sizeof(float);

	panwrap_log(MALI_PTR_FORMAT " (%x):\n",
		    attr->elements_upper << 2, attr->flags);

	panwrap_indent++;
	for (int row = 0; row < vertex_count; row++) {
		panwrap_log("<");
		for (int i = 0; i < component_count; i++)
			panwrap_log_cont("%f%s",
					 buffer[i],
					 i < component_count - 1 ? ", " : "");
		panwrap_log_cont(">\n");
	}
	panwrap_indent--;
}

void panwrap_decode_vertex_or_tiler_job(const struct mali_job_descriptor_header *h,
					const struct panwrap_mapped_memory *mem,
					mali_ptr payload)
{
	struct mali_payload_vertex_tiler *v =
		PANWRAP_PTR(mem, payload, typeof(*v));
	struct mali_shader_meta *meta;
	struct panwrap_mapped_memory *attr_mem;
	struct mali_vertex_tiler_attr_meta *attr_meta;
	mali_ptr meta_ptr = v->shader_upper << 4;
	mali_ptr p;

	/* From chai, no idea what this is for */
	if ((meta_ptr & 0xFFF00000) == 0x5AB00000) {
		panwrap_log("Job sabotaged\n");
	}

	attr_mem = panwrap_find_mapped_gpu_mem_containing(v->attribute_meta);

	panwrap_log("%s shader @ " MALI_PTR_FORMAT " (flags 0x%x)\n",
		    h->job_type == JOB_TYPE_VERTEX ? "Vertex" : "Fragment",
		    meta_ptr, v->flags);

	panwrap_log("Block #1:\n");
	panwrap_indent++;
	panwrap_log_hexdump(v->block1, sizeof(v->block1));
	panwrap_indent--;

	if (meta_ptr) {
		meta = panwrap_deref_gpu_mem(NULL, meta_ptr, sizeof(*meta));

		panwrap_log("Shader blob:\n");
		panwrap_indent++;
		panwrap_log_hexdump(
		    panwrap_deref_gpu_mem(NULL, meta->shader, 832), 832);
		panwrap_indent--;
	} else
		panwrap_log("<no shader>\n");

	if (v->attribute_meta) {
		panwrap_log("Attribute list:\n");
		panwrap_indent++;
		for (p = v->attribute_meta;
		     *PANWRAP_PTR(attr_mem, p, u64) != 0;
		     p += sizeof(u64)) {
			attr_meta = panwrap_deref_gpu_mem(attr_mem, p,
							  sizeof(*attr_mem));

			panwrap_log("%x:\n", attr_meta->index);
			panwrap_indent++;

			panwrap_log("flags = 0x%014" PRIx64 "\n",
				    attr_meta->flags);
			panwrap_decode_attributes(
			    NULL, v->attributes + attr_meta->index);

			panwrap_indent--;
		}
		panwrap_indent--;
	} else
		panwrap_log("<no attributes>\n");

	panwrap_log("Block #2:\n");
	panwrap_indent++;
	panwrap_log_hexdump(v->block2, sizeof(v->block2));
	panwrap_indent--;

	/*panwrap_assert_gpu_mem_zero(mem, */

	/*if (h->job_type == JOB_TYPE_TILER) {*/
		/*panwrap_log(*/
		    /*"Drawing in %s\n",*/
		    /*panwrap_gl_mode_name(*PANWRAP_PTR(NULL, v->block1[8], u8)));*/
	/*}*/
}

void panwrap_trace_hw_chain(mali_ptr jc_gpu_va)
{
	struct panwrap_mapped_memory *mem =
		panwrap_find_mapped_gpu_mem_containing(jc_gpu_va);
	struct mali_job_descriptor_header *h =
		panwrap_deref_gpu_mem(mem, jc_gpu_va, sizeof(*h));
	mali_ptr payload = jc_gpu_va + sizeof(*h);

	panwrap_log("%s job, %d-bit, status %X, incomplete %X\n",
		    panwrap_job_type_name(h->job_type),
		    h->job_descriptor_size ? 64 : 32,
		    h->exception_status,
		    h->first_incomplete_task);
	panwrap_log("fault %" PRIX64 ", barrier %d, index %hX\n",
		    h->fault_pointer,
		    h->job_barrier,
		    h->job_index);
	panwrap_log("dependencies (%hX, %hX)\n",
		    h->job_dependency_index_1,
		    h->job_dependency_index_2);

	panwrap_indent++;

	switch (h->job_type) {
	case JOB_TYPE_SET_VALUE:
		{
			struct mali_payload_set_value *s =
				panwrap_deref_gpu_mem(mem, payload, sizeof(*s));

			panwrap_log("set value -> %" PRIX64 " (%" PRIX64 ")\n",
				    s->out, s->unknown);
			break;
		}
	case JOB_TYPE_TILER:
	case JOB_TYPE_VERTEX:
		panwrap_decode_vertex_or_tiler_job(h, mem, payload);
		break;
	default:
		panwrap_log("Dumping payload " MALI_PTR_FORMAT ":\n",
			    payload);

		panwrap_indent++;
		panwrap_log_hexdump(panwrap_deref_gpu_mem(mem, payload, 256),
				    256);
		panwrap_indent--;
	}

	panwrap_indent--;
}

void panwrap_trace_atom(const struct mali_jd_atom_v2 *atom)
{
	if (atom->core_req & MALI_JD_REQ_SOFT_JOB) {
		/* We don't support decoding these, or need them just yet */
		panwrap_log("Soft job, cannot decode these yet!\n");
	} else {

	}
}

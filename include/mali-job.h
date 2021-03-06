/*
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

#ifndef __MALI_JOB_H__
#define __MALI_JOB_H__

#include <config.h>
#include <mali-ioctl.h>

#define MALI_PTR_BITS (sizeof(mali_ptr)*8)

#ifdef IS_64_BIT
#define PAD_PTR(p) p
#else
#define PAD_PTR(p) p; u32 :32
#endif

enum mali_job_type {
	JOB_NOT_STARTED	= 0,
	JOB_TYPE_NULL = 1,
	JOB_TYPE_SET_VALUE = 2,
	JOB_TYPE_CACHE_FLUSH = 3,
	JOB_TYPE_COMPUTE = 4,
	JOB_TYPE_VERTEX = 5,
	JOB_TYPE_TILER = 7,
	JOB_TYPE_FUSED = 8,
	JOB_TYPE_FRAGMENT = 9,
};

enum mali_gl_mode {
	MALI_GL_POINTS         = 0x01,
	MALI_GL_LINES          = 0x02,
	MALI_GL_TRIANGLES      = 0x08,
	MALI_GL_TRIANGLE_STRIP = 0x0A,
	MALI_GL_TRIANGLE_FAN   = 0x0C,
};

struct mali_shader_meta {
	PAD_PTR(mali_ptr shader);
	PAD_PTR(mali_ptr unknown1);
	PAD_PTR(mali_ptr unknown2);
};

/* FIXME: This might only concern fragment/vertex jobs? notes unclear */

struct mali_job_descriptor_header {
	u32 exception_status;
	u32 first_incomplete_task;
	u64 fault_pointer;
	u8 job_descriptor_size : 1;
	enum mali_job_type job_type : 7;
	u8 job_barrier : 1;
	u8 _reserved_01 : 1;
	u8 _reserved_1 : 1;
	u8 _reserved_02 : 1;
	u8 _reserved_03 : 1;
	u8 _reserved_2 : 1;
	u8 _reserved_04 : 1;
	u8 _reserved_05 : 1;
	u16 job_index;
	u16 job_dependency_index_1;
	u16 job_dependency_index_2;
	PAD_PTR(mali_ptr next_job);
} __attribute__((packed));

struct mali_payload_set_value {
	u64 out;
	u64 unknown;
} __attribute__((packed));

struct mali_vertex_tiler_attr {
	u8 flags : 2;
	mali_ptr elements_upper : MALI_PTR_BITS - 2;
	size_t stride;
	size_t size;
} __attribute__((packed));

struct mali_vertex_tiler_attr_meta {
	u8 index;
	u64 flags :56;
} __attribute__((packed));
ASSERT_SIZEOF_TYPE(struct mali_vertex_tiler_attr_meta,
		   sizeof(u64), sizeof(u64));

struct mali_payload_vertex_tiler {
	uint32_t block1[10];

	/* Lyude: no idea what the null's were supposed to represent here. */
	mali_ptr null0;
	mali_ptr _zeroes;
	mali_ptr unknown1; /* pointer */
	mali_ptr null1;
	mali_ptr null2;
	mali_ptr unknown2; /* pointer */
	u8 flags : 4;
	mali_ptr shader_upper : MALI_PTR_BITS - 4; /* struct shader_meta */
	mali_ptr attributes; /* struct attribute_buffer[] */
	mali_ptr attribute_meta; /* attribute_meta[] */
	mali_ptr unknown5; /* pointer */
	mali_ptr unknown6; /* pointer */
	mali_ptr nullForVertex;
	mali_ptr null4;
	mali_ptr fbd;	   /* struct tentative_fbd */
	mali_ptr unknown7; /* pointer */

	uint32_t block2[36];
} __attribute__((packed));
//ASSERT_SIZEOF_TYPE(struct mali_payload_vertex_tiler, 256, 256);

/* Originally from chai, which found it from mali_kase_reply.c */

#undef PAD_PTR

#endif /* __MALI_JOB_H__ */

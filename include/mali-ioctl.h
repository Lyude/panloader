/*
 * © Copyright 2017 The BiOpenly Community
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

/**
 * Definitions for all of the ioctls for the original open source bifrost GPU
 * kernel driver, written by ARM.
 */

#ifndef __MALI_IOCTL_H__
#define __MALI_IOCTL_H__

#include <panloader-util.h>

/**
 * Since these structs are passed to and from the kernel we need to make sure
 * that we get the size of each struct to match exactly what the kernel is
 * expecting. So, when editing this file make sure to add static asserts that
 * check each struct's size against the arg length you see in strace.
 */

enum mali_ioctl_mem_flags {
	/* IN */
	MALI_MEM_PROT_CPU_RD = (1U << 0),      /**< Read access CPU side */
	MALI_MEM_PROT_CPU_WR = (1U << 1),      /**< Write access CPU side */
	MALI_MEM_PROT_GPU_RD = (1U << 2),      /**< Read access GPU side */
	MALI_MEM_PROT_GPU_WR = (1U << 3),      /**< Write access GPU side */
	MALI_MEM_PROT_GPU_EX = (1U << 4),      /**< Execute allowed on the GPU
						    side */

	MALI_MEM_GROW_ON_GPF = (1U << 9),      /**< Grow backing store on GPU
						    Page Fault */

	MALI_MEM_COHERENT_SYSTEM = (1U << 10), /**< Page coherence Outer
						    shareable, if available */
	MALI_MEM_COHERENT_LOCAL = (1U << 11),  /**< Page coherence Inner
						    shareable */
	MALI_MEM_CACHED_CPU = (1U << 12),      /**< Should be cached on the
						    CPU */

	/* IN/OUT */
	MALI_MEM_SAME_VA = (1U << 13), /**< Must have same VA on both the GPU
					    and the CPU */
	/* OUT */
	MALI_MEM_NEED_MMAP = (1U << 14), /**< Must call mmap to acquire a GPU
					     address for the alloc */
	/* IN */
	MALI_MEM_COHERENT_SYSTEM_REQUIRED = (1U << 15), /**< Page coherence
					     Outer shareable, required. */
	MALI_MEM_SECURE = (1U << 16),          /**< Secure memory */
	MALI_MEM_DONT_NEED = (1U << 17),       /**< Not needed physical
						    memory */
	MALI_MEM_IMPORT_SHARED = (1U << 18),   /**< Must use shared CPU/GPU zone
						    (SAME_VA zone) but doesn't
						    require the addresses to
						    be the same */
};

/**
 * Header used by all ioctls
 */
union mali_ioctl_header {
	/* [in] The ID of the UK function being called */
	u32 id :32;
	/* [out] The return value of the UK function that was called */
	u32 rc :32;

	u64 :64;
} __attribute__((packed));
ASSERT_SIZEOF_TYPE(union mali_ioctl_header, 8);

struct mali_ioctl_get_version {
	union mali_ioctl_header header;
	u16 major; /* [out] */
	u16 minor; /* [out] */
	u32 :32;
} __attribute__((packed));
ASSERT_SIZEOF_TYPE(struct mali_ioctl_get_version, 16);

struct mali_ioctl_mem_alloc {
	union mali_ioctl_header header;
	/* [in] */
	u64 va_pages;
	u64 commit_pages;
	u64 extent;
	/* [in/out] */
	u64 flags;
	/* [out] */
	u64 gpu_va;
	u16 va_alignment;

	u32 :32;
	u16 :16;
} __attribute__((packed));
ASSERT_SIZEOF_TYPE(struct mali_ioctl_mem_alloc, 56);

struct mali_ioctl_mem_import {
	union mali_ioctl_header header;
	/* [in] */
	u64 phandle;
	u32 type;
	u32 :32;
	/* [in/out] */
	u64 flags;
	/* [out] */
	u64 gpu_va;
	u64 va_pages;
} __attribute__((packed));
/* FIXME: Size unconfirmed (haven't seen in a trace yet) */

struct mali_ioctl_mem_commit {
	union mali_ioctl_header header;
	/* [in] */
	u64 gpu_addr;
	u64 pages;
	/* [out] */
	u32 result_subcode;
	u32 :32;
} __attribute__((packed));
ASSERT_SIZEOF_TYPE(struct mali_ioctl_mem_commit, 32);

struct mali_ioctl_mem_query {
	union mali_ioctl_header header;
	/* [in] */
	u64 gpu_addr;
	enum {
		MALI_MEM_QUERY_COMMIT_SIZE = 1,
		MALI_MEM_QUERY_VA_SIZE     = 2,
		MALI_MEM_QUERY_FLAGS       = 3
	} query :32;
	u32 :32;
	/* [out] */
	u64 value;
} __attribute__((packed));
ASSERT_SIZEOF_TYPE(struct mali_ioctl_mem_query, 32);

struct mali_ioctl_mem_free {
	union mali_ioctl_header header;
	u64 gpu_addr; /* [in] */
} __attribute__((packed));
/* FIXME: Size unconfirmed (haven't seen in a trace yet) */

struct mali_ioctl_mem_flags_change {
	union mali_ioctl_header header;
	/* [in] */
	u64 gpu_va;
	u64 flags;
	u64 mask;
} __attribute__((packed));
/* FIXME: Size unconfirmed (haven't seen in a trace yet) */

struct mali_ioctl_mem_alias {
	union mali_ioctl_header header;
	/* [in/out] */
	u64 flags;
	/* [in] */
	u64 stride;
	u64 nents;
	u64 ai;
	/* [out] */
	u64 gpu_va;
	u64 va_pages;
} __attribute__((packed));

struct mali_ioctl_set_flags {
	union mali_ioctl_header header;
	u32 create_flags; /* [in] */
	u32 :32;
} __attribute__((packed));
ASSERT_SIZEOF_TYPE(struct mali_ioctl_set_flags, 16);

/* For ioctl's we haven't written decoding stuff for yet */
typedef struct {
	union mali_ioctl_header header;
} __ioctl_placeholder;

#define MALI_IOCTL_TYPE_BASE  0x80
#define MALI_IOCTL_TYPE_MAX   0x82
#define MALI_IOCTL_TYPE_COUNT (MALI_IOCTL_TYPE_MAX - MALI_IOCTL_TYPE_BASE + 1)

#define MALI_IOCTL_GET_VERSION             (_IOWR(0x80,  0, struct mali_ioctl_get_version))
#define MALI_IOCTL_MEM_ALLOC               (_IOWR(0x82,  0, struct mali_ioctl_mem_alloc))
#define MALI_IOCTL_MEM_IMPORT              (_IOWR(0x82,  1, struct mali_ioctl_mem_import))
#define MALI_IOCTL_MEM_COMMIT              (_IOWR(0x82,  2, struct mali_ioctl_mem_commit))
#define MALI_IOCTL_MEM_QUERY               (_IOWR(0x82,  3, struct mali_ioctl_mem_query))
#define MALI_IOCTL_MEM_FREE                (_IOWR(0x82,  4, struct mali_ioctl_mem_free))
#define MALI_IOCTL_MEM_FLAGS_CHANGE        (_IOWR(0x82,  5, struct mali_ioctl_mem_flags_change))
#define MALI_IOCTL_MEM_ALIAS               (_IOWR(0x82,  6, struct mali_ioctl_mem_alias))
#define MALI_IOCTL_SYNC                    (_IOWR(0x82,  8, __ioctl_placeholder))
#define MALI_IOCTL_POST_TERM               (_IOWR(0x82,  9, __ioctl_placeholder))
#define MALI_IOCTL_HWCNT_SETUP             (_IOWR(0x82, 10, __ioctl_placeholder))
#define MALI_IOCTL_HWCNT_DUMP              (_IOWR(0x82, 11, __ioctl_placeholder))
#define MALI_IOCTL_HWCNT_CLEAR             (_IOWR(0x82, 12, __ioctl_placeholder))
#define MALI_IOCTL_GPU_PROPS_REG_DUMP      (_IOWR(0x82, 14, __ioctl_placeholder))
#define MALI_IOCTL_FIND_CPU_OFFSET         (_IOWR(0x82, 15, __ioctl_placeholder))
#define MALI_IOCTL_GET_VERSION_NEW         (_IOWR(0x82, 16, struct mali_ioctl_get_version))
#define MALI_IOCTL_SET_FLAGS               (_IOWR(0x82, 18, struct mali_ioctl_set_flags))
#define MALI_IOCTL_SET_TEST_DATA           (_IOWR(0x82, 19, __ioctl_placeholder))
#define MALI_IOCTL_INJECT_ERROR            (_IOWR(0x82, 20, __ioctl_placeholder))
#define MALI_IOCTL_MODEL_CONTROL           (_IOWR(0x82, 21, __ioctl_placeholder))
#define MALI_IOCTL_KEEP_GPU_POWERED        (_IOWR(0x82, 22, __ioctl_placeholder))
#define MALI_IOCTL_FENCE_VALIDATE          (_IOWR(0x82, 23, __ioctl_placeholder))
#define MALI_IOCTL_STREAM_CREATE           (_IOWR(0x82, 24, __ioctl_placeholder))
#define MALI_IOCTL_GET_PROFILING_CONTROLS  (_IOWR(0x82, 25, __ioctl_placeholder))
#define MALI_IOCTL_SET_PROFILING_CONTROLS  (_IOWR(0x82, 26, __ioctl_placeholder))
#define MALI_IOCTL_DEBUGFS_MEM_PROFILE_ADD (_IOWR(0x82, 27, __ioctl_placeholder))
#define MALI_IOCTL_JOB_SUBMIT              (_IOWR(0x82, 28, __ioctl_placeholder))
#define MALI_IOCTL_DISJOINT_QUERY          (_IOWR(0x82, 29, __ioctl_placeholder))
#define MALI_IOCTL_GET_CONTEXT_ID          (_IOWR(0x82, 31, __ioctl_placeholder))
#define MALI_IOCTL_TLSTREAM_ACQUIRE_V10_4  (_IOWR(0x82, 32, __ioctl_placeholder))
#define MALI_IOCTL_TLSTREAM_TEST           (_IOWR(0x82, 33, __ioctl_placeholder))
#define MALI_IOCTL_TLSTREAM_STATS          (_IOWR(0x82, 34, __ioctl_placeholder))
#define MALI_IOCTL_TLSTREAM_FLUSH          (_IOWR(0x82, 35, __ioctl_placeholder))
#define MALI_IOCTL_HWCNT_READER_SETUP      (_IOWR(0x82, 36, __ioctl_placeholder))
#define MALI_IOCTL_SET_PRFCNT_VALUES       (_IOWR(0x82, 37, __ioctl_placeholder))
#define MALI_IOCTL_SOFT_EVENT_UPDATE       (_IOWR(0x82, 38, __ioctl_placeholder))
#define MALI_IOCTL_MEM_JIT_INIT            (_IOWR(0x82, 39, __ioctl_placeholder))
#define MALI_IOCTL_TLSTREAM_ACQUIRE        (_IOWR(0x82, 40, __ioctl_placeholder))

#endif /* __MALI_IOCTL_H__ */

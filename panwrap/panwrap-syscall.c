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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/ioctl.h>
#include <math.h>
#include <sys/mman.h>

#include <mali-ioctl.h>
#include <list.h>
#include "panwrap.h"

static pthread_mutex_t l = PTHREAD_MUTEX_INITIALIZER;

#define LOCK()   pthread_mutex_lock(&l)
#define UNLOCK() pthread_mutex_unlock(&l)

#define IOCTL_CASE(request) (_IOWR(_IOC_TYPE(request), _IOC_NR(request), \
				   _IOC_SIZE(request)))

struct ioctl_info {
	const char *name;
};

struct device_info {
	const char *name;
	const struct ioctl_info info[MALI_IOCTL_TYPE_COUNT][_IOC_NR(0xffffffff)];
};

typedef void* (mmap_func)(void *, size_t, int, int, int, off_t);
typedef int (open_func)(const char *, int flags, ...);

#define IOCTL_TYPE(type) [type - MALI_IOCTL_TYPE_BASE] =
#define IOCTL_INFO(n) [_IOC_NR(MALI_IOCTL_##n)] = { .name = #n }
static struct device_info mali_info = {
	.name = "mali",
	.info = {
		IOCTL_TYPE(0x80) {
			IOCTL_INFO(GET_VERSION),
		},
		IOCTL_TYPE(0x82) {
			IOCTL_INFO(MEM_ALLOC),
			IOCTL_INFO(MEM_IMPORT),
			IOCTL_INFO(MEM_COMMIT),
			IOCTL_INFO(MEM_QUERY),
			IOCTL_INFO(MEM_FREE),
			IOCTL_INFO(MEM_FLAGS_CHANGE),
			IOCTL_INFO(MEM_ALIAS),
			IOCTL_INFO(SYNC),
			IOCTL_INFO(POST_TERM),
			IOCTL_INFO(HWCNT_SETUP),
			IOCTL_INFO(HWCNT_DUMP),
			IOCTL_INFO(HWCNT_CLEAR),
			IOCTL_INFO(GPU_PROPS_REG_DUMP),
			IOCTL_INFO(FIND_CPU_OFFSET),
			IOCTL_INFO(GET_VERSION_NEW),
			IOCTL_INFO(SET_FLAGS),
			IOCTL_INFO(SET_TEST_DATA),
			IOCTL_INFO(INJECT_ERROR),
			IOCTL_INFO(MODEL_CONTROL),
			IOCTL_INFO(KEEP_GPU_POWERED),
			IOCTL_INFO(FENCE_VALIDATE),
			IOCTL_INFO(STREAM_CREATE),
			IOCTL_INFO(GET_PROFILING_CONTROLS),
			IOCTL_INFO(SET_PROFILING_CONTROLS),
			IOCTL_INFO(DEBUGFS_MEM_PROFILE_ADD),
			IOCTL_INFO(JOB_SUBMIT),
			IOCTL_INFO(DISJOINT_QUERY),
			IOCTL_INFO(GET_CONTEXT_ID),
			IOCTL_INFO(TLSTREAM_ACQUIRE_V10_4),
			IOCTL_INFO(TLSTREAM_TEST),
			IOCTL_INFO(TLSTREAM_STATS),
			IOCTL_INFO(TLSTREAM_FLUSH),
			IOCTL_INFO(HWCNT_READER_SETUP),
			IOCTL_INFO(SET_PRFCNT_VALUES),
			IOCTL_INFO(SOFT_EVENT_UPDATE),
			IOCTL_INFO(MEM_JIT_INIT),
			IOCTL_INFO(TLSTREAM_ACQUIRE),
		},
	},
};
#undef IOCTL_INFO
#undef IOCTL_TYPE

static inline const struct ioctl_info *
ioctl_get_info(unsigned long int request)
{
	return &mali_info.info[_IOC_TYPE(request) - MALI_IOCTL_TYPE_BASE]
	                      [_IOC_NR(request)];
}

static int mali_fd = 0;
static LIST_HEAD(allocations);
static LIST_HEAD(mmaps);

#define FLAG_INFO(flag) { MALI_MEM_##flag, #flag }
static const struct panwrap_flag_info mem_flag_info[] = {
	FLAG_INFO(PROT_CPU_RD),
	FLAG_INFO(PROT_CPU_WR),
	FLAG_INFO(PROT_GPU_RD),
	FLAG_INFO(PROT_GPU_WR),
	FLAG_INFO(PROT_GPU_EX),
	FLAG_INFO(GROW_ON_GPF),
	FLAG_INFO(COHERENT_SYSTEM),
	FLAG_INFO(COHERENT_LOCAL),
	FLAG_INFO(CACHED_CPU),
	FLAG_INFO(SAME_VA),
	FLAG_INFO(NEED_MMAP),
	FLAG_INFO(COHERENT_SYSTEM_REQUIRED),
	FLAG_INFO(SECURE),
	FLAG_INFO(DONT_NEED),
	FLAG_INFO(IMPORT_SHARED),
	{}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_JD_REQ_##flag, #flag }
static const struct panwrap_flag_info jd_req_flag_info[] = {
	FLAG_INFO(FS),
	FLAG_INFO(CS),
	FLAG_INFO(T),
	FLAG_INFO(CF),
	FLAG_INFO(V),
	FLAG_INFO(FS_AFBC),
	FLAG_INFO(EVENT_COALESCE),
	FLAG_INFO(COHERENT_GROUP),
	FLAG_INFO(PERMON),
	FLAG_INFO(EXTERNAL_RESOURCES),
	FLAG_INFO(ONLY_COMPUTE),
	FLAG_INFO(SPECIFIC_COHERENT_GROUP),
	FLAG_INFO(EVENT_ONLY_ON_FAILURE),
	FLAG_INFO(EVENT_NEVER),
	FLAG_INFO(SKIP_CACHE_START),
	FLAG_INFO(SKIP_CACHE_END),
	{}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { flag, #flag }
static const struct panwrap_flag_info external_resources_access_flag_info[] = {
	FLAG_INFO(MALI_EXT_RES_ACCESS_SHARED),
	FLAG_INFO(MALI_EXT_RES_ACCESS_EXCLUSIVE),
	{}
};

static const struct panwrap_flag_info mali_jd_dep_type_flag_info[] = {
	FLAG_INFO(MALI_JD_DEP_TYPE_DATA),
	FLAG_INFO(MALI_JD_DEP_TYPE_ORDER),
	{}
};
#undef FLAG_INFO

static inline const char *
ioctl_decode_coherency_mode(enum mali_ioctl_coherency_mode mode)
{
	switch (mode) {
	case COHERENCY_ACE_LITE: return "ACE_LITE";
	case COHERENCY_ACE:      return "ACE";
	case COHERENCY_NONE:     return "None";
	default:                 return "???";
	}
}

static inline const char *
ioctl_decode_jd_prio(mali_jd_prio prio)
{
	switch (prio) {
	case MALI_JD_PRIO_LOW:    return "Low";
	case MALI_JD_PRIO_MEDIUM: return "Medium";
	case MALI_JD_PRIO_HIGH:   return "High";
	default:                  return "???";
	}
}

/*
 * Decodes the jd_core_req flags and their real meanings
 * See mali_kbase_jd.c
 */
static inline const char *
ioctl_get_job_type_from_jd_core_req(mali_jd_core_req req)
{
	if (req & MALI_JD_REQ_SOFT_JOB)
		return "Soft job";
	if (req & MALI_JD_REQ_ONLY_COMPUTE)
		return "Compute Shader Job";

	switch (req & (MALI_JD_REQ_FS | MALI_JD_REQ_CS | MALI_JD_REQ_T)) {
	case MALI_JD_REQ_DEP:
		return "Dependency only job";
	case MALI_JD_REQ_FS:
		return "Fragment shader job";
	case MALI_JD_REQ_CS:
		return "Vertex/Geometry shader job";
	case MALI_JD_REQ_T:
		return "Tiler job";
	case (MALI_JD_REQ_FS | MALI_JD_REQ_CS):
		return "Fragment shader + vertex/geometry shader job";
	case (MALI_JD_REQ_FS | MALI_JD_REQ_T):
		return "Fragment shader + tiler job";
	case (MALI_JD_REQ_CS | MALI_JD_REQ_T):
		return "Vertex/geometry shader job + tiler job";
	case (MALI_JD_REQ_FS | MALI_JD_REQ_CS | MALI_JD_REQ_T):
		return "Fragment shader + vertex/geometry shader job + tiler job";
	}

	return "???";
}

#define SOFT_FLAG(flag)                                  \
	case MALI_JD_REQ_SOFT_##flag:                    \
		panwrap_log_cont("%s)", "SOFT_" #flag); \
		break
/* Decodes the actual jd_core_req flags, but not their meanings */
static inline void
ioctl_log_decoded_jd_core_req(mali_jd_core_req req)
{
	if (req & MALI_JD_REQ_SOFT_JOB) {
		panwrap_log_cont("0x%010x (", req);

		switch (req) {
		SOFT_FLAG(DUMP_CPU_GPU_TIME);
		SOFT_FLAG(FENCE_TRIGGER);
		SOFT_FLAG(FENCE_WAIT);
		SOFT_FLAG(REPLAY);
		SOFT_FLAG(EVENT_WAIT);
		SOFT_FLAG(EVENT_SET);
		SOFT_FLAG(EVENT_RESET);
		SOFT_FLAG(DEBUG_COPY);
		SOFT_FLAG(JIT_ALLOC);
		SOFT_FLAG(JIT_FREE);
		SOFT_FLAG(EXT_RES_MAP);
		SOFT_FLAG(EXT_RES_UNMAP);
		default: panwrap_log_cont("???" ")"); break;
		}
	} else {
		panwrap_log_decoded_flags(jd_req_flag_info, req);
	}
}
#undef SOFT_FLAG

static void
ioctl_decode_pre_mem_alloc(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_alloc *args = ptr;

	panwrap_log("va_pages = %" PRId64 "\n", args->va_pages);
	panwrap_log("commit_pages = %" PRId64 "\n", args->commit_pages);
	panwrap_log("extent = 0x%" PRIx64 "\n", args->extent);

	panwrap_log("flags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
}

static void
ioctl_decode_pre_mem_import(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_import *args = ptr;
	const char *type;

	switch (args->type) {
	case MALI_MEM_IMPORT_TYPE_UMP:         type = "UMP"; break;
	case MALI_MEM_IMPORT_TYPE_UMM:         type = "UMM"; break;
	case MALI_MEM_IMPORT_TYPE_USER_BUFFER: type = "User buffer"; break;
	default:                               type = "Invalid"; break;
	}

	panwrap_log("phandle = 0x%" PRIx64 "\n", args->phandle);
	panwrap_log("type = %d (%s)\n", args->type, type);

	panwrap_log("flags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
}

static void
ioctl_decode_pre_mem_commit(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_commit *args = ptr;

	panwrap_log("gpu_addr = " MALI_PTR_FORMAT "\n", args->gpu_addr);
	panwrap_log("pages = %" PRId64 "\n", args->pages);
}

static void
ioctl_decode_pre_mem_query(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_query *args = ptr;
	char *query_name;

	switch (args->query) {
	case MALI_MEM_QUERY_COMMIT_SIZE: query_name = "Commit size"; break;
	case MALI_MEM_QUERY_VA_SIZE:     query_name = "VA size"; break;
	case MALI_MEM_QUERY_FLAGS:       query_name = "Flags"; break;
	default:                         query_name = "???"; break;
	}

	panwrap_log("gpu_addr = " MALI_PTR_FORMAT "\n", args->gpu_addr);
	panwrap_log("query = %d (%s)\n", args->query, query_name);
}

static void
ioctl_decode_pre_mem_free(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_free *args = ptr;

	panwrap_log("gpu_addr = " MALI_PTR_FORMAT "\n", args->gpu_addr);
}

static void
ioctl_decode_pre_mem_flags_change(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_flags_change *args = ptr;

	panwrap_log("gpu_va = " MALI_PTR_FORMAT "\n", args->gpu_va);
	panwrap_log("flags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
	panwrap_log("mask = 0x%" PRIx64 "\n", args->mask);
}

static void
ioctl_decode_pre_mem_alias(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_alias *args = ptr;

	panwrap_log("flags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
	panwrap_log("stride = %" PRId64 "\n", args->stride);
	panwrap_log("nents = %" PRId64 "\n", args->nents);
	panwrap_log("ai = 0x%" PRIx64 "\n", args->ai);
}

static inline void
ioctl_decode_pre_sync(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_sync *args = ptr;
	const char *type;
	struct panwrap_mapped_memory *mem =
		panwrap_find_mapped_gpu_mem(args->handle);

	switch (args->type) {
	case MALI_SYNC_TO_DEVICE: type = "device <- CPU"; break;
	case MALI_SYNC_TO_CPU:    type = "device -> CPU"; break;
	default:                  type = "???"; break;
	}

	if (mem) {
		panwrap_log("handle = " MALI_PTR_FORMAT " (end=" MALI_PTR_FORMAT ", len=%zu)\n",
			    args->handle,
			    (mali_ptr)(args->handle + mem->length),
			    mem->length);
		panwrap_log("user_addr = %p - %p (offset=%zu)\n",
			    args->user_addr, args->user_addr + args->size,
			    args->user_addr - mem->addr);
	} else {
		panwrap_log("ERROR! Unknown handle specified\n");
		panwrap_log("handle = " MALI_PTR_FORMAT "\n", args->handle);
		panwrap_log("user_addr = %p - %p\n",
			    args->user_addr, args->user_addr + args->size);
	}
	panwrap_log("size = %" PRId64 "\n", args->size);
	panwrap_log("type = %d (%s)\n", args->type, type);

	if (args->type == MALI_SYNC_TO_DEVICE) {
		panwrap_log("Dumping memory being synced to device:\n");
		panwrap_indent++;
		panwrap_log_hexdump(args->user_addr, args->size);
		panwrap_indent--;
	}
}

static void
ioctl_decode_pre_set_flags(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_set_flags *args = ptr;

	panwrap_log("create_flags = %08x\n", args->create_flags);
}

static inline void
ioctl_decode_pre_stream_create(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_stream_create *args = ptr;

	panwrap_log("name = %s\n", args->name);
}

static inline void
ioctl_decode_pre_job_submit(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_job_submit *args = ptr;
	const struct mali_jd_atom_v2 *atoms = args->addr;

	panwrap_log("addr = %p\n", args->addr);
	panwrap_log("nr_atoms = %d\n", args->nr_atoms);
	panwrap_log("stride = %d\n", args->stride);

	/* The stride should be equivalent to the length of the structure,
	 * if it isn't then it's possible we're somehow tracing one of the
	 * legacy job formats
	 */
	if (args->stride != sizeof(*atoms)) {
		panwrap_log("SIZE MISMATCH (stride should be %zd, was %d)\n",
			    sizeof(*atoms), args->stride);
		panwrap_log("Cannot dump atoms :(, maybe it's a legacy job format?\n");
		return;
	}

	panwrap_log("Atoms:\n");
	panwrap_indent++;
	for (int i = 0; i < args->nr_atoms; i++) {
		const struct mali_jd_atom_v2 *a = &atoms[i];

		panwrap_log("jc = " MALI_PTR_FORMAT "\n", a->jc);
		panwrap_indent++;

		panwrap_log("Decoding job chain:\n");
		panwrap_indent++;
		panwrap_trace_hw_chain(a->jc);
		panwrap_indent--;

		panwrap_log("udata = [0x%" PRIx64 ", 0x%" PRIx64 "]\n",
			    a->udata.blob[0], a->udata.blob[1]);
		panwrap_log("nr_ext_res = %d\n", a->nr_ext_res);

		if (a->ext_res_list) {
			panwrap_log("text_res_list.count = %" PRId64 "\n",
				    a->ext_res_list->count);
			panwrap_log("External resources:\n");

			panwrap_indent++;
			for (int j = 0; j < a->nr_ext_res; j++)
			{
				panwrap_log("");
				panwrap_log_decoded_flags(
					external_resources_access_flag_info,
					a->ext_res_list[j].ext_resource[0]);
				panwrap_log_cont("\n");
			}
			panwrap_indent--;
		} else {
			panwrap_log("<no external resources>\n");
		}

		panwrap_log("compat_core_req = 0x%x\n", a->compat_core_req);

		panwrap_log("Pre-dependencies:\n");
		panwrap_indent++;
		for (int j = 0; j < ARRAY_SIZE(a->pre_dep); j++) {
			panwrap_log("atom_id = %d flags == ",
				    a->pre_dep[i].atom_id);
			panwrap_log_decoded_flags(
			    mali_jd_dep_type_flag_info,
			    a->pre_dep[i].dependency_type);
			panwrap_log_cont("\n");
		}
		panwrap_indent--;

		panwrap_log("atom_number = %d\n", a->atom_number);
		panwrap_log("prio = %d (%s)\n",
			    a->prio, ioctl_decode_jd_prio(a->prio));
		panwrap_log("device_nr = %d\n", a->device_nr);

		panwrap_log("Job type = %s\n",
			    ioctl_get_job_type_from_jd_core_req(a->core_req));
		panwrap_log("core_req = ");
		ioctl_log_decoded_jd_core_req(a->core_req);
		panwrap_log_cont("\n");

		panwrap_indent--;
	}
	panwrap_indent--;
}

static void
ioctl_decode_pre(unsigned long int request, void *ptr)
{
	switch (IOCTL_CASE(request)) {
	case IOCTL_CASE(MALI_IOCTL_MEM_ALLOC):
		ioctl_decode_pre_mem_alloc(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_IMPORT):
		ioctl_decode_pre_mem_import(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_COMMIT):
		ioctl_decode_pre_mem_commit(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_QUERY):
		ioctl_decode_pre_mem_query(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_FREE):
		ioctl_decode_pre_mem_free(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_FLAGS_CHANGE):
		ioctl_decode_pre_mem_flags_change(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_ALIAS):
		ioctl_decode_pre_mem_alias(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_SYNC):
		ioctl_decode_pre_sync(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_SET_FLAGS):
		ioctl_decode_pre_set_flags(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_STREAM_CREATE):
		ioctl_decode_pre_stream_create(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_JOB_SUBMIT):
		ioctl_decode_pre_job_submit(request, ptr);
		break;
	default:
		break;
	}
}

static void
ioctl_decode_post_get_version(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_get_version *args = ptr;

	panwrap_log("major = %3d\n", args->major);
	panwrap_log("minor = %3d\n", args->minor);
}

static void
ioctl_decode_post_mem_alloc(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_alloc *args = ptr;

	panwrap_log("gpu_va = " MALI_PTR_FORMAT "\n", args->gpu_va);
	panwrap_log("va_alignment = %d\n", args->va_alignment);

	panwrap_track_allocation(args->gpu_va, args->flags);
}

static void
ioctl_decode_post_mem_import(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_import *args = ptr;

	panwrap_log("gpu_va = " MALI_PTR_FORMAT "\n", args->gpu_va);
	panwrap_log("va_pages = %" PRId64 "\n", args->va_pages);
	panwrap_log("flags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
}

static void
ioctl_decode_post_mem_commit(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_commit *args = ptr;

	panwrap_log("result_subcode = %d\n", args->result_subcode);
}

static void
ioctl_decode_post_mem_query(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_query *args = ptr;

	panwrap_log("value = 0x%" PRIx64 "\n", args->value);
}

static void
ioctl_decode_post_mem_alias(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_alias *args = ptr;

	panwrap_log("gpu_va = " MALI_PTR_FORMAT "\n", args->gpu_va);
	panwrap_log("va_pages = %" PRId64 "\n", args->va_pages);
}

static void inline
ioctl_decode_post_sync(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_sync *args = ptr;

	if (args->type != MALI_SYNC_TO_CPU)
		return;

	panwrap_log("Dumping memory from device:\n");
	panwrap_indent++;
	panwrap_log_hexdump_trimmed(args->user_addr, args->size);
	panwrap_indent--;
}

static void
ioctl_decode_post_gpu_props_reg_dump(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_gpu_props_reg_dump *args = ptr;
	const char *implementation;

	switch (args->thread.impl_tech) {
	case MALI_GPU_IMPLEMENTATION_UNKNOWN: implementation = "Unknown"; break;
	case MALI_GPU_IMPLEMENTATION_SILICON: implementation = "Silicon"; break;
	case MALI_GPU_IMPLEMENTATION_FPGA:    implementation = "FPGA"; break;
	case MALI_GPU_IMPLEMENTATION_SW:      implementation = "Software"; break;
	}

	panwrap_log("core:\n");
	panwrap_indent++;
	panwrap_log("Product ID: %d\n", args->core.product_id);
	panwrap_log("Version status: %d\n", args->core.version_status);
	panwrap_log("Minor revision: %d\n", args->core.minor_revision);
	panwrap_log("Major revision: %d\n", args->core.major_revision);
	panwrap_log("GPU speed (?): %dMHz\n", args->core.gpu_speed_mhz);
	panwrap_log("GPU frequencies (?): %dKHz-%dKHz\n",
		    args->core.gpu_freq_khz_min, args->core.gpu_freq_khz_max);
	panwrap_log("Shader program counter size: %.lf MB\n",
		    pow(2, args->core.log2_program_counter_size) / 1024 / 1024);

	panwrap_log("Texture features:\n");
	panwrap_indent++;
	for (int i = 0; i < ARRAY_SIZE(args->core.texture_features); i++)
		panwrap_log("%010x\n", args->core.texture_features[i]);
	panwrap_indent--;

	panwrap_log("Available memory: %" PRId64 " bytes\n",
		    args->core.gpu_available_memory_size);
	panwrap_indent--;

	panwrap_log("L2 cache:\n");
	panwrap_indent++;
	panwrap_log("Line size: %.lf (bytes, words?)\n",
		    pow(2, args->l2.log2_line_size));
	panwrap_log("Cache size: %.lf KB\n",
		    pow(2, args->l2.log2_cache_size) / 1024);
	panwrap_log("L2 slice count: %d\n", args->l2.num_l2_slices);
	panwrap_indent--;

	panwrap_log("Tiler:\n");
	panwrap_indent++;
	panwrap_log("Binary size: %d bytes\n",
		    args->tiler.bin_size_bytes);
	panwrap_log("Max active levels: %d\n",
		    args->tiler.max_active_levels);
	panwrap_indent--;

	panwrap_log("Threads:\n");
	panwrap_indent++;
	panwrap_log("Max threads: %d\n", args->thread.max_threads);
	panwrap_log("Max threads per workgroup: %d\n",
		    args->thread.max_workgroup_size);
	panwrap_log("Max threads allowed for synchronizing on simple barrier: %d\n",
		    args->thread.max_barrier_size);
	panwrap_log("Max registers available per-core: %d\n",
		    args->thread.max_registers);
	panwrap_log("Max tasks that can be sent to a core before blocking: %d\n",
		    args->thread.max_task_queue);
	panwrap_log("Max allowed thread group split value: %d\n",
		    args->thread.max_thread_group_split);
	panwrap_log("Implementation type: %d (%s)\n",
		    args->thread.impl_tech, implementation);
	panwrap_indent--;

	panwrap_log("Raw props:\n");

	panwrap_indent++;

	panwrap_log("Shader present? %s\n", YES_NO(args->raw.shader_present));
	panwrap_log("Tiler present? %s\n", YES_NO(args->raw.tiler_present));
	panwrap_log("L2 present? %s\n", YES_NO(args->raw.l2_present));
	panwrap_log("Stack present? %s\n", YES_NO(args->raw.stack_present));
	panwrap_log("L2 features: 0x%010x\n", args->raw.l2_features);
	panwrap_log("Suspend size: %d\n", args->raw.suspend_size);
	panwrap_log("Memory features: 0x%010x\n", args->raw.mem_features);
	panwrap_log("MMU features: 0x%010x\n", args->raw.mmu_features);
	panwrap_log("AS (what is this?) present? %s\n",
		    YES_NO(args->raw.as_present));

	panwrap_log("JS (what is this?) present? %s\n",
		    YES_NO(args->raw.js_present));
	panwrap_log("JS features:\n");

	panwrap_indent++;
	for (int i = 0; i < ARRAY_SIZE(args->raw.js_features); i++)
		panwrap_log("\t\t\t%010x\n", args->raw.js_features[i]);
	panwrap_indent--;

	panwrap_log("Tiler features: %010x\n", args->raw.tiler_features);

	panwrap_log("GPU ID: 0x%x\n", args->raw.gpu_id);
	panwrap_log("Thread features: 0x%x\n", args->raw.thread_features);
	panwrap_log("Coherency mode: 0x%x (%s)\n",
		    args->raw.coherency_mode,
		    ioctl_decode_coherency_mode(args->raw.coherency_mode));

	panwrap_indent--;

	panwrap_log("Coherency info:\n");
	panwrap_indent++;
	panwrap_log("Number of groups: %d\n", args->coherency_info.num_groups);
	panwrap_log("Number of core groups (coherent or not): %d\n",
		    args->coherency_info.num_core_groups);
	panwrap_log("Features: 0x%x\n", args->coherency_info.coherency);
	panwrap_log("Groups:\n");
	panwrap_indent++;
	for (int i = 0; i < args->coherency_info.num_groups; i++) {
		panwrap_log("- Core mask: %010" PRIx64 "\n",
			    args->coherency_info.group[i].core_mask);
		panwrap_log("  Number of cores: %d\n",
			    args->coherency_info.group[i].num_cores);
	}
	panwrap_indent--;
	panwrap_indent--;
}

static inline void
ioctl_decode_post_stream_create(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_stream_create *args = ptr;

	panwrap_log("fd = %d\n", args->fd);
}

static inline void
ioctl_decode_post_get_context_id(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_get_context_id *args = ptr;

	panwrap_log("id = 0x%" PRIx64 "\n", args->id);
}

static void
ioctl_decode_post(unsigned long int request, void *ptr)
{
	switch (IOCTL_CASE(request)) {
	case IOCTL_CASE(MALI_IOCTL_GET_VERSION):
	case IOCTL_CASE(MALI_IOCTL_GET_VERSION_NEW):
		ioctl_decode_post_get_version(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_ALLOC):
		ioctl_decode_post_mem_alloc(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_IMPORT):
		ioctl_decode_post_mem_import(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_COMMIT):
		ioctl_decode_post_mem_commit(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_QUERY):
		ioctl_decode_post_mem_query(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_ALIAS):
		ioctl_decode_post_mem_alias(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_SYNC):
		ioctl_decode_post_sync(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_GPU_PROPS_REG_DUMP):
		ioctl_decode_post_gpu_props_reg_dump(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_STREAM_CREATE):
		ioctl_decode_post_stream_create(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_GET_CONTEXT_ID):
		ioctl_decode_post_get_context_id(request, ptr);
		break;
	default:
		break;
	}
}

/**
 * Overriden libc functions start here
 */
static inline int
panwrap_open_wrap(open_func *func, const char *path, int flags, va_list args)
{
	mode_t mode = 0;
	int ret;

	if (flags & O_CREAT) {
		mode = (mode_t) va_arg(args, int);
		ret = func(path, flags, mode);
	} else {
		ret = func(path, flags);
	}

	LOCK();
	if (ret != -1) {
		if (strcmp(path, "/dev/mali0") == 0) {
			panwrap_log("/dev/mali0 fd == %d\n", ret);
			mali_fd = ret;
		} else if (strstr(path, "/dev/")) {
			panwrap_log("Unknown device %s opened at fd %d\n",
				    path, ret);
		}
	}
	UNLOCK();

	return ret;
}

int
open(const char *path, int flags, ...)
{
	PROLOG(open);
	va_list args;
	va_start(args, flags);
	int o = panwrap_open_wrap(orig_open, path, flags, args);
	va_end(args);
	return o;
}

int
open64(const char *path, int flags, ...)
{
	PROLOG(open64);
	va_list args;
	va_start(args, flags);
	int o = panwrap_open_wrap(orig_open64, path, flags, args);
	va_end(args);
	return o;
}

int
close(int fd)
{
	PROLOG(close);

        /* Intentionally racy: prevents us from trying to hold the global mutex
         * in calls from system libraries */
        if (fd <= 0 || !mali_fd || fd != mali_fd)
                return orig_close(fd);

	LOCK();
	if (mali_fd && fd == mali_fd) {
		panwrap_log("/dev/mali0 closed\n");
		mali_fd = 0;
	}
	UNLOCK();

	return orig_close(fd);
}

/* XXX: Android has a messed up ioctl signature */
int ioctl(int fd, int request, ...)
{
	const char *name;
	union mali_ioctl_header *header;
	PROLOG(ioctl);
	int ioc_size = _IOC_SIZE(request);
	int ret;
	uint32_t func;
	void *ptr;

	if (ioc_size) {
		va_list args;

		va_start(args, request);
		ptr = va_arg(args, void *);
		va_end(args);
	} else {
		ptr = NULL;
	}

	if (fd && fd != mali_fd)
		return orig_ioctl(fd, request, ptr);

	LOCK();
	panwrap_freeze_time();
	name = ioctl_get_info(request)->name ?: "???";
	header = ptr;

	if (!ptr) { /* All valid mali ioctl's should have a specified arg */
		panwrap_log("<%-20s> (%02d) (%08x), has no arguments? Cannot decode :(\n",
			    name, _IOC_NR(request), request);

		panwrap_unfreeze_time();
		ret = orig_ioctl(fd, request, ptr);
		panwrap_freeze_time();

		panwrap_indent++;
		panwrap_log("= %02d\n", ret);
		panwrap_indent--;
		goto out;
	}

	func = header->id;
	panwrap_log("<%-20s> (%02d) (%08x) (%04d) (%03d)\n",
		    name, _IOC_NR(request), request, _IOC_SIZE(request), func);

	panwrap_indent++;

	ioctl_decode_pre(request, ptr);

	panwrap_unfreeze_time();
	ret = orig_ioctl(fd, request, ptr);
	panwrap_freeze_time();

	panwrap_log("= %02d, %02d\n",
		    ret, header->rc);
	ioctl_decode_post(request, ptr);

	panwrap_indent--;

out:
	panwrap_unfreeze_time();
	UNLOCK();
	return ret;
}

static void inline *panwrap_mmap_wrap(mmap_func *func,
				      void *addr, size_t length, int prot,
				      int flags, int fd, off_t offset)
{
	void *ret;

	if (!mali_fd || fd != mali_fd)
		return func(addr, length, prot, flags, fd, offset);

	LOCK();
	ret = func(addr, length, prot, flags, fd, offset);

	panwrap_freeze_time();
	/* offset == gpu_va */
	panwrap_track_mmap(offset, ret, length, prot, flags);
	panwrap_unfreeze_time();

	UNLOCK();
	return ret;
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd,
	     off_t offset)
{
	PROLOG(mmap64);

	return panwrap_mmap_wrap(orig_mmap64, addr, length, prot, flags, fd,
				 offset);
}

#ifdef IS_MMAP64_SEPERATE_SYMBOL
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
#ifdef IS_64_BIT
	PROLOG(mmap);

	return panwrap_mmap_wrap(orig_mmap, addr, length, prot, flags, fd,
				 offset);
#else
	return mmap64(addr, length, prot, flags, fd, (loff_t) offset);
#endif
}
#endif

int munmap(void *addr, size_t length)
{
	int ret;
	struct panwrap_mapped_memory *mem;
	PROLOG(munmap);

	LOCK();
	ret = orig_munmap(addr, length);

	panwrap_freeze_time();

	mem = panwrap_find_mapped_mem(addr);
	if (!mem)
		goto out;

	/* Was it memory mapped from the GPU? */
	if (mem->gpu_va)
		panwrap_log("Unmapped GPU memory " MALI_PTR_FORMAT "@%p\n",
			    mem->gpu_va, mem->addr);
	else
		panwrap_log("Unmapped unknown memory %p\n",
			    mem->addr);

	list_del(&mem->node);
	free(mem);
out:
	panwrap_unfreeze_time();
	UNLOCK();
	return ret;
}

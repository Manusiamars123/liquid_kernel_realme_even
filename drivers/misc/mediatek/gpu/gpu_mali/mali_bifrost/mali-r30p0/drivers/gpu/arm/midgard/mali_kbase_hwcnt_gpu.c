// SPDX-License-Identifier: GPL-2.0
/*
 *
 * (C) COPYRIGHT 2018-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include "mali_kbase_hwcnt_gpu.h"
#include "mali_kbase_hwcnt_types.h"

#include <linux/bug.h>
#include <linux/err.h>

#define KBASE_HWCNT_V5_BLOCK_TYPE_COUNT 4
#define KBASE_HWCNT_V5_HEADERS_PER_BLOCK 4
#define KBASE_HWCNT_V5_COUNTERS_PER_BLOCK 60
#define KBASE_HWCNT_V5_VALUES_PER_BLOCK \
	(KBASE_HWCNT_V5_HEADERS_PER_BLOCK + KBASE_HWCNT_V5_COUNTERS_PER_BLOCK)
/* Index of the PRFCNT_EN header into a V5 counter block */
#define KBASE_HWCNT_V5_PRFCNT_EN_HEADER 2

static void kbasep_get_fe_block_type(u64 *dst, enum kbase_hwcnt_set counter_set,
				     bool is_csf)
{
	switch (counter_set) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
		if (is_csf) {
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2;
		} else {
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_UNDEFINED;
		}
		break;
	case KBASE_HWCNT_SET_TERTIARY:
		if (is_csf) {
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3;
		} else {
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_UNDEFINED;
		}
		break;
	default:
		WARN_ON(true);
	}
}

static void kbasep_get_tiler_block_type(u64 *dst,
					enum kbase_hwcnt_set counter_set)
{
	switch (counter_set) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
	case KBASE_HWCNT_SET_TERTIARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_UNDEFINED;
		break;
	default:
		WARN_ON(true);
	}
}

static void kbasep_get_sc_block_type(u64 *dst, enum kbase_hwcnt_set counter_set,
				     bool is_csf)
{
	switch (counter_set) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2;
		break;
	case KBASE_HWCNT_SET_TERTIARY:
		if (is_csf) {
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3;
		} else {
			*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_UNDEFINED;
		}
		break;
	default:
		WARN_ON(true);
	}
}

static void kbasep_get_memsys_block_type(u64 *dst,
					 enum kbase_hwcnt_set counter_set)
{
	switch (counter_set) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2;
		break;
	case KBASE_HWCNT_SET_TERTIARY:
		*dst = KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_UNDEFINED;
		break;
	default:
		WARN_ON(true);
	}
}

/**
 * kbasep_hwcnt_backend_gpu_metadata_create() - Create hardware counter metadata
 *                                              for the GPU.
 * @gpu_info:      Non-NULL pointer to hwcnt info for current GPU.
 * @is_csf:        true for CSF GPU, otherwise false.
 * @counter_set:   The performance counter set to use.
 * @metadata:      Non-NULL pointer to where created metadata is stored
 *                 on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_gpu_metadata_create(
	const struct kbase_hwcnt_gpu_info *gpu_info, const bool is_csf,
	enum kbase_hwcnt_set counter_set,
	const struct kbase_hwcnt_metadata **metadata)
{
	struct kbase_hwcnt_description desc;
	struct kbase_hwcnt_group_description group;
	struct kbase_hwcnt_block_description
		blks[KBASE_HWCNT_V5_BLOCK_TYPE_COUNT];
	size_t non_sc_block_count;
	size_t sc_block_count;

	WARN_ON(!gpu_info);
	WARN_ON(!metadata);

	/* Calculate number of block instances that aren't shader cores */
	non_sc_block_count = 2 + gpu_info->l2_count;
	/* Calculate number of block instances that are shader cores */
	sc_block_count = fls64(gpu_info->core_mask);

	/*
	 * A system can have up to 64 shader cores, but the 64-bit
	 * availability mask can't physically represent that many cores as well
	 * as the other hardware blocks.
	 * Error out if there are more blocks than our implementation can
	 * support.
	 */
	if ((sc_block_count + non_sc_block_count) > KBASE_HWCNT_AVAIL_MASK_BITS)
		return -EINVAL;

	/* One Front End block */
	kbasep_get_fe_block_type(&blks[0].type, counter_set, is_csf);
	blks[0].inst_cnt = 1;
	blks[0].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[0].ctr_cnt = KBASE_HWCNT_V5_COUNTERS_PER_BLOCK;

	/* One Tiler block */
	kbasep_get_tiler_block_type(&blks[1].type, counter_set);
	blks[1].inst_cnt = 1;
	blks[1].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[1].ctr_cnt = KBASE_HWCNT_V5_COUNTERS_PER_BLOCK;

	/* l2_count memsys blks */
	kbasep_get_memsys_block_type(&blks[2].type, counter_set);
	blks[2].inst_cnt = gpu_info->l2_count;
	blks[2].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[2].ctr_cnt = KBASE_HWCNT_V5_COUNTERS_PER_BLOCK;

	/*
	 * There are as many shader cores in the system as there are bits set in
	 * the core mask. However, the dump buffer memory requirements need to
	 * take into account the fact that the core mask may be non-contiguous.
	 *
	 * For example, a system with a core mask of 0b1011 has the same dump
	 * buffer memory requirements as a system with 0b1111, but requires more
	 * memory than a system with 0b0111. However, core 2 of the system with
	 * 0b1011 doesn't physically exist, and the dump buffer memory that
	 * accounts for that core will never be written to when we do a counter
	 * dump.
	 *
	 * We find the core mask's last set bit to determine the memory
	 * requirements, and embed the core mask into the availability mask so
	 * we can determine later which shader cores physically exist.
	 */
	kbasep_get_sc_block_type(&blks[3].type, counter_set, is_csf);
	blks[3].inst_cnt = sc_block_count;
	blks[3].hdr_cnt = KBASE_HWCNT_V5_HEADERS_PER_BLOCK;
	blks[3].ctr_cnt = KBASE_HWCNT_V5_COUNTERS_PER_BLOCK;

	WARN_ON(KBASE_HWCNT_V5_BLOCK_TYPE_COUNT != 4);

	group.type = KBASE_HWCNT_GPU_GROUP_TYPE_V5;
	group.blk_cnt = KBASE_HWCNT_V5_BLOCK_TYPE_COUNT;
	group.blks = blks;

	desc.grp_cnt = 1;
	desc.grps = &group;
	desc.clk_cnt = gpu_info->clk_cnt;

	/* The JM, Tiler, and L2s are always available, and are before cores */
	desc.avail_mask = (1ull << non_sc_block_count) - 1;
	/* Embed the core mask directly in the availability mask */
	desc.avail_mask |= (gpu_info->core_mask << non_sc_block_count);

	return kbase_hwcnt_metadata_create(&desc, metadata);
}

/**
 * kbasep_hwcnt_backend_jm_dump_bytes() - Get the raw dump buffer size for the
 *                                        GPU.
 * @gpu_info: Non-NULL pointer to hwcnt info for the GPU.
 *
 * Return: Size of buffer the GPU needs to perform a counter dump.
 */
static size_t
kbasep_hwcnt_backend_jm_dump_bytes(const struct kbase_hwcnt_gpu_info *gpu_info)
{
	WARN_ON(!gpu_info);

	return (2 + gpu_info->l2_count + fls64(gpu_info->core_mask)) *
	       KBASE_HWCNT_V5_VALUES_PER_BLOCK * KBASE_HWCNT_VALUE_BYTES;
}

int kbase_hwcnt_jm_metadata_create(
	const struct kbase_hwcnt_gpu_info *gpu_info,
	enum kbase_hwcnt_set counter_set,
	const struct kbase_hwcnt_metadata **out_metadata,
	size_t *out_dump_bytes)
{
	int errcode;
	const struct kbase_hwcnt_metadata *metadata;
	size_t dump_bytes;

	if (!gpu_info || !out_metadata || !out_dump_bytes)
		return -EINVAL;

	dump_bytes = kbasep_hwcnt_backend_jm_dump_bytes(gpu_info);
	errcode = kbasep_hwcnt_backend_gpu_metadata_create(
		gpu_info, false, counter_set, &metadata);
	if (errcode)
		return errcode;

	/*
	 * Dump abstraction size should be exactly the same size and layout as
	 * the physical dump size, for backwards compatibility.
	 */
	WARN_ON(dump_bytes != metadata->dump_buf_bytes);

	*out_metadata = metadata;
	*out_dump_bytes = dump_bytes;

	return 0;
}

void kbase_hwcnt_jm_metadata_destroy(
	const struct kbase_hwcnt_metadata *metadata)
{
	if (!metadata)
		return;

	kbase_hwcnt_metadata_destroy(metadata);
}

int kbase_hwcnt_csf_metadata_create(
	const struct kbase_hwcnt_gpu_info *gpu_info,
	enum kbase_hwcnt_set counter_set,
	const struct kbase_hwcnt_metadata **out_metadata)
{
	int errcode;
	const struct kbase_hwcnt_metadata *metadata;

	if (!gpu_info || !out_metadata)
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_gpu_metadata_create(
		gpu_info, true, counter_set, &metadata);
	if (errcode)
		return errcode;

	*out_metadata = metadata;

	return 0;
}

void kbase_hwcnt_csf_metadata_destroy(
	const struct kbase_hwcnt_metadata *metadata)
{
	if (!metadata)
		return;

	kbase_hwcnt_metadata_destroy(metadata);
}

static bool is_block_type_shader(
	const u64 grp_type,
	const u64 blk_type,
	const size_t blk)
{
	bool is_shader = false;

	/* Warn on unknown group type */
	if (WARN_ON(grp_type != KBASE_HWCNT_GPU_GROUP_TYPE_V5))
		return false;

	if (blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC ||
	    blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2 ||
	    blk_type == KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3)
		is_shader = true;

	return is_shader;
}

int kbase_hwcnt_jm_dump_get(struct kbase_hwcnt_dump_buffer *dst, void *src,
			    const struct kbase_hwcnt_enable_map *dst_enable_map,
			    u64 pm_core_mask, bool accumulate)
{
	const struct kbase_hwcnt_metadata *metadata;
	const u32 *dump_src;
	size_t src_offset, grp, blk, blk_inst;
	u64 core_mask = pm_core_mask;

	if (!dst || !src || !dst_enable_map ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	metadata = dst->metadata;
	dump_src = (const u32 *)src;
	src_offset = 0;

	kbase_hwcnt_metadata_for_each_block(
		metadata, grp, blk, blk_inst) {
		const size_t hdr_cnt =
			kbase_hwcnt_metadata_block_headers_count(
				metadata, grp, blk);
		const size_t ctr_cnt =
			kbase_hwcnt_metadata_block_counters_count(
				metadata, grp, blk);
		const u64 blk_type = kbase_hwcnt_metadata_block_type(
			metadata, grp, blk);
		const bool is_shader_core = is_block_type_shader(
			kbase_hwcnt_metadata_group_type(metadata, grp),
			blk_type, blk);

		/* Early out if no values in the dest block are enabled */
		if (kbase_hwcnt_enable_map_block_enabled(
			dst_enable_map, grp, blk, blk_inst)) {
			u32 *dst_blk = kbase_hwcnt_dump_buffer_block_instance(
				dst, grp, blk, blk_inst);
			const u32 *src_blk = dump_src + src_offset;

			if (!is_shader_core || (core_mask & 1)) {
				if (accumulate) {
					kbase_hwcnt_dump_buffer_block_accumulate(
						dst_blk, src_blk, hdr_cnt,
						ctr_cnt);
				} else {
					kbase_hwcnt_dump_buffer_block_copy(
						dst_blk, src_blk,
						(hdr_cnt + ctr_cnt));
				}
			} else if (!accumulate) {
				kbase_hwcnt_dump_buffer_block_zero(
					dst_blk, (hdr_cnt + ctr_cnt));
			}
		}

		src_offset += (hdr_cnt + ctr_cnt);
		if (is_shader_core)
			core_mask = core_mask >> 1;
	}

	return 0;
}

int kbase_hwcnt_csf_dump_get(
	struct kbase_hwcnt_dump_buffer *dst, void *src,
	const struct kbase_hwcnt_enable_map *dst_enable_map,
	bool accumulate)
{
	const struct kbase_hwcnt_metadata *metadata;
	const u32 *dump_src;
	size_t src_offset, grp, blk, blk_inst;

	if (!dst || !src || !dst_enable_map ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	metadata = dst->metadata;
	dump_src = (const u32 *)src;
	src_offset = 0;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		const size_t hdr_cnt = kbase_hwcnt_metadata_block_headers_count(
			metadata, grp, blk);
		const size_t ctr_cnt =
			kbase_hwcnt_metadata_block_counters_count(metadata, grp,
								  blk);

		/* Early out if no values in the dest block are enabled */
		if (kbase_hwcnt_enable_map_block_enabled(dst_enable_map, grp,
							 blk, blk_inst)) {
			u32 *dst_blk = kbase_hwcnt_dump_buffer_block_instance(
				dst, grp, blk, blk_inst);
			const u32 *src_blk = dump_src + src_offset;

			if (accumulate) {
				kbase_hwcnt_dump_buffer_block_accumulate(
					dst_blk, src_blk, hdr_cnt, ctr_cnt);
			} else {
				kbase_hwcnt_dump_buffer_block_copy(
					dst_blk, src_blk, (hdr_cnt + ctr_cnt));
			}
		}

		src_offset += (hdr_cnt + ctr_cnt);
	}

	return 0;
}

/**
 * kbasep_hwcnt_backend_gpu_block_map_to_physical() - Convert from a block
 *                                                    enable map abstraction to
 *                                                    a physical block enable
 *                                                    map.
 * @lo: Low 64 bits of block enable map abstraction.
 * @hi: High 64 bits of block enable map abstraction.
 *
 * The abstraction uses 128 bits to enable 128 block values, whereas the
 * physical uses just 32 bits, as bit n enables values [n*4, n*4+3].
 * Therefore, this conversion is lossy.
 *
 * Return: 32-bit physical block enable map.
 */
static inline u32 kbasep_hwcnt_backend_gpu_block_map_to_physical(
	u64 lo,
	u64 hi)
{
	u32 phys = 0;
	u64 dwords[2] = {lo, hi};
	size_t dword_idx;

	for (dword_idx = 0; dword_idx < 2; dword_idx++) {
		const u64 dword = dwords[dword_idx];
		u16 packed = 0;

		size_t hword_bit;

		for (hword_bit = 0; hword_bit < 16; hword_bit++) {
			const size_t dword_bit = hword_bit * 4;
			const u16 mask =
				((dword >> (dword_bit + 0)) & 0x1) |
				((dword >> (dword_bit + 1)) & 0x1) |
				((dword >> (dword_bit + 2)) & 0x1) |
				((dword >> (dword_bit + 3)) & 0x1);
			packed |= (mask << hword_bit);
		}
		phys |= ((u32)packed) << (16 * dword_idx);
	}
	return phys;
}

/**
 * kbasep_hwcnt_backend_gpu_block_map_from_physical() - Convert from a physical
 *                                                      block enable map to a
 *                                                      block enable map
 *                                                      abstraction.
 * @phys: Physical 32-bit block enable map
 * @lo:   Non-NULL pointer to where low 64 bits of block enable map abstraction
 *        will be stored.
 * @hi:   Non-NULL pointer to where high 64 bits of block enable map abstraction
 *        will be stored.
 */
static inline void kbasep_hwcnt_backend_gpu_block_map_from_physical(
	u32 phys,
	u64 *lo,
	u64 *hi)
{
	u64 dwords[2] = {0, 0};

	size_t dword_idx;

	for (dword_idx = 0; dword_idx < 2; dword_idx++) {
		const u16 packed = phys >> (16 * dword_idx);
		u64 dword = 0;

		size_t hword_bit;

		for (hword_bit = 0; hword_bit < 16; hword_bit++) {
			const size_t dword_bit = hword_bit * 4;
			const u64 mask = (packed >> (hword_bit)) & 0x1;

			dword |= mask << (dword_bit + 0);
			dword |= mask << (dword_bit + 1);
			dword |= mask << (dword_bit + 2);
			dword |= mask << (dword_bit + 3);
		}
		dwords[dword_idx] = dword;
	}
	*lo = dwords[0];
	*hi = dwords[1];
}

void kbase_hwcnt_gpu_enable_map_to_physical(
	struct kbase_hwcnt_physical_enable_map *dst,
	const struct kbase_hwcnt_enable_map *src)
{
	const struct kbase_hwcnt_metadata *metadata;

	u64 fe_bm = 0;
	u64 shader_bm = 0;
	u64 tiler_bm = 0;
	u64 mmu_l2_bm = 0;

	size_t grp, blk, blk_inst;

	if (WARN_ON(!src) || WARN_ON(!dst))
		return;

	metadata = src->metadata;

	kbase_hwcnt_metadata_for_each_block(
		metadata, grp, blk, blk_inst) {
		const u64 grp_type = kbase_hwcnt_metadata_group_type(
			metadata, grp);
		const u64 blk_type = kbase_hwcnt_metadata_block_type(
			metadata, grp, blk);
		const size_t blk_val_cnt =
			kbase_hwcnt_metadata_block_values_count(
				metadata, grp, blk);
		const u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			src, grp, blk, blk_inst);

		if ((enum kbase_hwcnt_gpu_group_type)grp_type ==
		    KBASE_HWCNT_GPU_GROUP_TYPE_V5) {
			WARN_ON(blk_val_cnt != KBASE_HWCNT_V5_VALUES_PER_BLOCK);
			switch ((enum kbase_hwcnt_gpu_v5_block_type)blk_type) {
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_UNDEFINED:
				/* Nothing to do in this case. */
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3:
				fe_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:
				tiler_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3:
				shader_bm |= *blk_map;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2:
				mmu_l2_bm |= *blk_map;
				break;
			default:
				WARN_ON(true);
			}
		} else {
			WARN_ON(true);
		}
	}

	dst->fe_bm =
		kbasep_hwcnt_backend_gpu_block_map_to_physical(fe_bm, 0);
	dst->shader_bm =
		kbasep_hwcnt_backend_gpu_block_map_to_physical(shader_bm, 0);
	dst->tiler_bm =
		kbasep_hwcnt_backend_gpu_block_map_to_physical(tiler_bm, 0);
	dst->mmu_l2_bm =
		kbasep_hwcnt_backend_gpu_block_map_to_physical(mmu_l2_bm, 0);
}

void kbase_hwcnt_gpu_set_to_physical(enum kbase_hwcnt_physical_set *dst,
				     enum kbase_hwcnt_set src)
{
	switch (src) {
	case KBASE_HWCNT_SET_PRIMARY:
		*dst = KBASE_HWCNT_PHYSICAL_SET_PRIMARY;
		break;
	case KBASE_HWCNT_SET_SECONDARY:
		*dst = KBASE_HWCNT_PHYSICAL_SET_SECONDARY;
		break;
	case KBASE_HWCNT_SET_TERTIARY:
		*dst = KBASE_HWCNT_PHYSICAL_SET_TERTIARY;
		break;
	default:
		WARN_ON(true);
	}
}

void kbase_hwcnt_gpu_enable_map_from_physical(
	struct kbase_hwcnt_enable_map *dst,
	const struct kbase_hwcnt_physical_enable_map *src)
{
	const struct kbase_hwcnt_metadata *metadata;

	u64 ignored_hi;
	u64 fe_bm;
	u64 shader_bm;
	u64 tiler_bm;
	u64 mmu_l2_bm;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!src) || WARN_ON(!dst))
		return;

	metadata = dst->metadata;

	kbasep_hwcnt_backend_gpu_block_map_from_physical(
		src->fe_bm, &fe_bm, &ignored_hi);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(
		src->shader_bm, &shader_bm, &ignored_hi);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(
		src->tiler_bm, &tiler_bm, &ignored_hi);
	kbasep_hwcnt_backend_gpu_block_map_from_physical(
		src->mmu_l2_bm, &mmu_l2_bm, &ignored_hi);

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		const u64 grp_type = kbase_hwcnt_metadata_group_type(
			metadata, grp);
		const u64 blk_type = kbase_hwcnt_metadata_block_type(
			metadata, grp, blk);
		const size_t blk_val_cnt =
			kbase_hwcnt_metadata_block_values_count(
				metadata, grp, blk);
		u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			dst, grp, blk, blk_inst);

		if ((enum kbase_hwcnt_gpu_group_type)grp_type ==
		    KBASE_HWCNT_GPU_GROUP_TYPE_V5) {
			WARN_ON(blk_val_cnt != KBASE_HWCNT_V5_VALUES_PER_BLOCK);
			switch ((enum kbase_hwcnt_gpu_v5_block_type)blk_type) {
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_UNDEFINED:
				/* Nothing to do in this case. */
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE2:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_FE3:
				*blk_map = fe_bm;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_TILER:
				*blk_map = tiler_bm;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC2:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_SC3:
				*blk_map = shader_bm;
				break;
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS:
			case KBASE_HWCNT_GPU_V5_BLOCK_TYPE_PERF_MEMSYS2:
				*blk_map = mmu_l2_bm;
				break;
			default:
				WARN_ON(true);
			}
		} else {
			WARN_ON(true);
		}
	}
}

void kbase_hwcnt_gpu_patch_dump_headers(
	struct kbase_hwcnt_dump_buffer *buf,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	const struct kbase_hwcnt_metadata *metadata;
	size_t grp, blk, blk_inst;

	if (WARN_ON(!buf) || WARN_ON(!enable_map) ||
	    WARN_ON(buf->metadata != enable_map->metadata))
		return;

	metadata = buf->metadata;

	kbase_hwcnt_metadata_for_each_block(metadata, grp, blk, blk_inst) {
		const u64 grp_type =
			kbase_hwcnt_metadata_group_type(metadata, grp);
		u32 *buf_blk = kbase_hwcnt_dump_buffer_block_instance(
			buf, grp, blk, blk_inst);
		const u64 *blk_map = kbase_hwcnt_enable_map_block_instance(
			enable_map, grp, blk, blk_inst);
		const u32 prfcnt_en =
			kbasep_hwcnt_backend_gpu_block_map_to_physical(
				blk_map[0], 0);

		if ((enum kbase_hwcnt_gpu_group_type)grp_type ==
		    KBASE_HWCNT_GPU_GROUP_TYPE_V5) {
			buf_blk[KBASE_HWCNT_V5_PRFCNT_EN_HEADER] = prfcnt_en;
		} else {
			WARN_ON(true);
		}
	}
}

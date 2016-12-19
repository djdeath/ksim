/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "ksim.h"

static void
dispatch_vs(struct vf_buffer *buffer, __m256i mask)
{
	struct reg *grf = &buffer->t.grf[0];
	uint32_t g;

	if (!gt.vs.enable)
		return;

	/* Not sure what we should make this. */
	uint32_t fftid = 0;

	buffer->t.mask = _mm256_movemask_ps((__m256) mask);
	buffer->t.mask_q1 = mask;

	/* Fixed function header */
	grf[0] = (struct reg) {
		.ud = {
			/* R0.0 - R0.2: MBZ */
			0,
			0,
			0,
			/* R0.3: per-thread scratch space, sampler ptr */
			gt.vs.sampler_state_address |
			gt.vs.scratch_size,
			/* R0.4: binding table pointer */
			gt.vs.binding_table_address,
			/* R0.5: fftid, scratch offset */
			gt.vs.scratch_pointer | fftid,
			/* R0.6: thread id */
			gt.vs.tid++ & 0xffffff,
			/* R0.7: Reserved */
			0,
		}
	};

	grf[1].ireg = buffer->vue_handles.ireg;

	g = load_constants(&buffer->t, &gt.vs.curbe, gt.vs.urb_start_grf);

	/* SIMD8 VS payload */
	__m256i *src = &buffer->data[gt.vs.vue_read_offset * 2 * 4].ireg;
	__m256i *dst = &grf[g].ireg;
	for (uint32_t i = 0; i < gt.vs.vue_read_length * 2 * 4; i++)
		dst[i] = src[i];

	if (gt.vs.statistics)
		gt.vs_invocation_count++;

	gt.vs.avx_shader(&buffer->t);
}

static void
validate_vf_state(void)
{
	uint32_t vb_used, b;
	uint64_t range;

	/* Make sure vue is big enough to hold all vertex elements */
	ksim_assert(gt.vf.ve_count * 16 <= gt.vs.urb.size);

	vb_used = 0;
	for (uint32_t i = 0; i < gt.vf.ve_count; i++) {
		ksim_assert((1 << gt.vf.ve[i].vb) & gt.vf.vb_valid);
		ksim_assert(valid_vertex_format(gt.vf.ve[i].format));
		if (gt.vf.ve[i].valid)
			vb_used |= 1 << gt.vf.ve[i].vb;
	}

	/* Check all VEs reference valid VBs. */
	ksim_assert((vb_used & gt.vf.vb_valid) == vb_used);

	for_each_bit(b, vb_used) {
		gt.vf.vb[b].data = map_gtt_offset(gt.vf.vb[b].address, &range);
		ksim_assert(gt.vf.vb[b].size <= range);
	}

	/* Check that SGVs are written within bounds */
	ksim_assert(gt.vf.iid_element * 16 < gt.vs.urb.size);
	ksim_assert(gt.vf.vid_element * 16 < gt.vs.urb.size);
}

static void
dump_sf_clip_viewport(void)
{
	const float *vp = gt.sf.viewport;

	spam("viewport matrix:\n");
	for (uint32_t i = 0; i < 6; i++)
		spam("  %20.4f\n", vp[i]);
	spam("guardband: %f,%f - %f,%f\n",
	     gt.sf.guardband.x0, gt.sf.guardband.y0,
	     gt.sf.guardband.x1, gt.sf.guardband.y1);
	spam("depth viewport: %f-%f\n",
	     gt.cc.viewport[0], gt.cc.viewport[1]);
}

enum vue_flag {
	VUE_FLAG_CLIP = 1
};

static void
setup_prim(struct value **vue_in, uint32_t parity)
{
	struct value *vue[3];
	uint32_t provoking;

	for (int i = 0; i < 3; i++) {
		if (vue_in[i][0].u[0] & VUE_FLAG_CLIP)
			return;
	}

	switch (gt.ia.topology) {
	case _3DPRIM_TRILIST:
	case _3DPRIM_TRISTRIP:
		provoking = gt.sf.tri_strip_provoking;
		break;
	case _3DPRIM_TRIFAN:
		provoking = gt.sf.tri_fan_provoking;
		break;
	case _3DPRIM_POLYGON:
	case _3DPRIM_QUADLIST:
	case _3DPRIM_QUADSTRIP:
	default:
		provoking = 0;
		break;
	case _3DPRIM_RECTLIST:
		/* The documentation requires a specific vertex
		 * ordering, but the hw doesn't actually care.  Our
		 * rasterizer does though, so rotate vertices to make
		 * sure the first to edges are axis parallel. */
		if (vue_in[0][1].vec4.x != vue_in[1][1].vec4.x &&
		    vue_in[0][1].vec4.y != vue_in[1][1].vec4.y) {
			ksim_warn("invalid rect list vertex order\n");
			provoking = 1;
		} else if (vue_in[1][1].vec4.x != vue_in[2][1].vec4.x &&
			   vue_in[1][1].vec4.y != vue_in[2][1].vec4.y) {
			ksim_warn("invalid rect list vertex order\n");
			provoking = 2;
		} else {
			provoking = 0;
		}
		break;
	}

	static const int indices[5] = { 0, 1, 2, 0, 1 };
	vue[0] = vue_in[indices[provoking]];
	vue[1] = vue_in[indices[provoking + 1 + parity]];
	vue[2] = vue_in[indices[provoking + 2 - parity]];

	rasterize_primitive(vue);
}

static void
transform_and_queue_vues(struct value **vue, uint32_t mask)
{
	const float *vp = gt.sf.viewport;
	float m00, m11, m22, m30, m31, m32;
	struct rectanglef clip;
	uint32_t i;

	if (gt.sf.viewport_transform_enable) {
		m00 = vp[0];
		m11 = vp[1];
		m22 = vp[2];
		m30 = vp[3];
		m31 = vp[4];
		m32 = vp[5];
	}

	if (gt.clip.guardband_clip_test_enable) {
		clip = gt.sf.guardband;
	} else {
		clip.x0 = -1;
		clip.y0 = -1;
		clip.x1 = 1;
		clip.y1 = 1;
	}

	for_each_bit(i, mask) {
		struct vec4 *pos = &vue[i][1].vec4;
		if (!gt.clip.perspective_divide_disable) {
			float inv_w = 1.0f / pos->w;
			pos->x *= inv_w;
			pos->y *= inv_w;
			pos->z *= inv_w;
			pos->w = inv_w;
		}

		if (gt.clip.guardband_clip_test_enable ||
		    gt.clip.viewport_clip_test_enable) {
			const struct vec4 v = vue[i][1].vec4;
			if (v.x < clip.x0 || clip.x1 < v.x ||
			    v.y < clip.y0 || clip.y1 < v.y || v.z > 1.0f) {
				vue[i][0].u[0] = VUE_FLAG_CLIP;
			} else {
				vue[i][0].u[0] = 0;
			}
		}

		if (gt.sf.viewport_transform_enable) {
			pos->x = m00 * pos->x + m30;
			pos->y = m11 * pos->y + m31;
			pos->z = m22 * pos->z + m32;
		}

		gt.ia.queue.vue[gt.ia.queue.head++ & 15] = vue[i];
	}

	ksim_assert(gt.ia.queue.head - gt.ia.queue.tail < 16);
}

static void
assemble_primitives()
{
	struct value *vue[3];
	uint32_t tail = gt.ia.queue.tail;

	switch (gt.ia.topology) {
	case _3DPRIM_TRILIST:
		while (gt.ia.queue.head - tail >= 3) {
			vue[0] = gt.ia.queue.vue[(tail + 0) & 15];
			vue[1] = gt.ia.queue.vue[(tail + 1) & 15];
			vue[2] = gt.ia.queue.vue[(tail + 2) & 15];
			setup_prim(vue, 0);
			tail += 3;
			gt.ia_primitives_count++;
		}
		break;

	case _3DPRIM_TRISTRIP:
		while (gt.ia.queue.head - tail >= 3) {
			vue[0] = gt.ia.queue.vue[(tail + 0) & 15];
			vue[1] = gt.ia.queue.vue[(tail + 1) & 15];
			vue[2] = gt.ia.queue.vue[(tail + 2) & 15];
			setup_prim(vue, gt.ia.tristrip_parity);
			tail += 1;
			gt.ia.tristrip_parity = 1 - gt.ia.tristrip_parity;
			gt.ia_primitives_count++;
		}
		break;

	case _3DPRIM_POLYGON:
	case _3DPRIM_TRIFAN:
		if (gt.ia.trifan_first_vertex == NULL) {
			/* We always have at least one vertex
			 * when we get, so this is safe. */
			assert(gt.ia.queue.head - tail >= 1);
			gt.ia.trifan_first_vertex = gt.ia.queue.vue[tail & 15];
			/* Bump the queue tail now so we don't free
			 * the vue below */
			gt.ia.queue.tail++;
			tail++;
			gt.ia_primitives_count++;
		}

		while (gt.ia.queue.head - tail >= 2) {
			vue[0] = gt.ia.trifan_first_vertex;
			vue[1] = gt.ia.queue.vue[(tail + 0) & 15];
			vue[2] = gt.ia.queue.vue[(tail + 1) & 15];
			setup_prim(vue, gt.ia.tristrip_parity);
			tail += 1;
			gt.ia_primitives_count++;
		}
		break;
	case _3DPRIM_QUADLIST:
		while (gt.ia.queue.head - tail >= 4) {
			vue[0] = gt.ia.queue.vue[(tail + 3) & 15];
			vue[1] = gt.ia.queue.vue[(tail + 0) & 15];
			vue[2] = gt.ia.queue.vue[(tail + 1) & 15];
			setup_prim(vue, 0);
			vue[0] = gt.ia.queue.vue[(tail + 3) & 15];
			vue[1] = gt.ia.queue.vue[(tail + 1) & 15];
			vue[2] = gt.ia.queue.vue[(tail + 2) & 15];
			setup_prim(vue, 0);
			tail += 4;
			gt.ia_primitives_count++;
		}
		break;
	case _3DPRIM_QUADSTRIP:
		while (gt.ia.queue.head - tail >= 4) {
			vue[0] = gt.ia.queue.vue[(tail + 3) & 15];
			vue[1] = gt.ia.queue.vue[(tail + 0) & 15];
			vue[2] = gt.ia.queue.vue[(tail + 1) & 15];
			setup_prim(vue, 0);
			vue[0] = gt.ia.queue.vue[(tail + 3) & 15];
			vue[1] = gt.ia.queue.vue[(tail + 2) & 15];
			vue[2] = gt.ia.queue.vue[(tail + 0) & 15];
			setup_prim(vue, 0);
			tail += 2;
			gt.ia_primitives_count++;
		}
		break;

	case _3DPRIM_RECTLIST:
		while (gt.ia.queue.head - tail >= 3) {
			vue[0] = gt.ia.queue.vue[(tail + 0) & 15];
			vue[1] = gt.ia.queue.vue[(tail + 1) & 15];
			vue[2] = gt.ia.queue.vue[(tail + 2) & 15];
			setup_prim(vue, 0);
			tail += 3;
		}
		break;

	default:
		stub("topology %d", gt.ia.topology);
		tail = gt.ia.queue.head;
		break;
	}

	while (tail - gt.ia.queue.tail > 0) {
		struct value *vue =
			gt.ia.queue.vue[gt.ia.queue.tail++ & 15];
		free_urb_entry(&gt.vs.urb, vue);
	}
}

void
reset_ia_state(void)
{
	if (gt.ia.trifan_first_vertex) {
		free_urb_entry(&gt.vs.urb, gt.ia.trifan_first_vertex);
		gt.ia.trifan_first_vertex = NULL;
	}

	while (gt.ia.queue.head - gt.ia.queue.tail > 0) {
		struct value *vue =
			gt.ia.queue.vue[gt.ia.queue.tail++ & 15];
		free_urb_entry(&gt.vs.urb, vue);
	}

	gt.ia.queue.head = 0;
	gt.ia.queue.tail = 0;
	gt.ia.tristrip_parity = 0;
	gt.ia.trifan_first_vertex = NULL;
}

static void
fetch_vertices(struct vf_buffer *buffer, uint32_t iid, __m256i vid, __m256i mask)
{
	const __m256i zero = _mm256_setzero_si256();
	uint32_t a = 0;

	const struct reg m = { .ireg = mask };
	for (uint32_t c = 0; c < 8; c++) {
		if (m.d[c] >= 0)
			continue;
		void *entry = alloc_urb_entry(&gt.vs.urb);
		buffer->vue_handles.ud[c] = urb_entry_to_handle(entry);
	}

	for (uint32_t i = 0; i < gt.vf.ve_count; i++) {
		struct ve *ve = &gt.vf.ve[i];
		struct vb *vb = &gt.vf.vb[ve->vb];
		__m256i index;

		if (!gt.vf.ve[i].valid)
			continue;

		if (gt.vf.ve[i].instancing) {
			index = _mm256_set1_epi32(gt.prim.start_instance + iid / gt.vf.ve[i].step_rate);
		} else if (gt.prim.access_type == RANDOM) {
			uint64_t range;
			void *ib = map_gtt_offset(gt.vf.ib.address, &range);

			index = _mm256_add_epi32(_mm256_set1_epi32(gt.prim.start_vertex), vid);

			switch (gt.vf.ib.format) {
				/* FIXME: INDEX_BYTE and INDEX_WORD
				 * can read outside the index
				 * buffer. */
			case INDEX_BYTE:
				index = _mm256_mask_i32gather_epi32(zero, ib, index, mask, 1);
				index = _mm256_and_si256(index, _mm256_set1_epi32(0xff));
				index = _mm256_add_epi32(_mm256_set1_epi32(gt.prim.base_vertex), index);
				break;
			case INDEX_WORD:
				index = _mm256_mask_i32gather_epi32(zero, ib, index, mask, 2);
				index = _mm256_and_si256(index, _mm256_set1_epi32(0xffff));
				index = _mm256_add_epi32(_mm256_set1_epi32(gt.prim.base_vertex), index);
				break;
			case INDEX_DWORD:
				index = _mm256_mask_i32gather_epi32(zero, ib, index, mask, 4);
				index = _mm256_add_epi32(_mm256_set1_epi32(gt.prim.base_vertex), index);
				break;
			}
		} else {
			index = _mm256_add_epi32(_mm256_set1_epi32(gt.prim.start_vertex), vid);
		}

		__m256i offset =
			_mm256_add_epi32( _mm256_mullo_epi32(index, _mm256_set1_epi32(vb->pitch)),
					  _mm256_set1_epi32(ve->offset));

		load_format_simd8(vb->data, ve->format, offset, mask, &buffer->data[a]);

		for (uint32_t c = 0; c < 4; c++) {
			switch (ve->cc[c]) {
			case VFCOMP_STORE_0:
				buffer->data[a + c].ireg = _mm256_setzero_si256();
				break;
			case VFCOMP_STORE_1_FP:
				buffer->data[a + c].reg = _mm256_set1_ps(1.0f);
				break;
			case VFCOMP_STORE_1_INT:
				buffer->data[a + c].ireg = _mm256_set1_epi32(1);
				break;
			}
		}

		a += 4;

		/* edgeflag */
	}

	/* 3DSTATE_VF_SGVS */
	if (gt.vf.iid_enable) {
		a = gt.vf.iid_element * 4 + gt.vf.iid_component;
		buffer->data[a].ireg = _mm256_set1_epi32(iid);
	}
	if (gt.vf.vid_enable) {
		a = gt.vf.vid_element * 4 + gt.vf.vid_component;
		buffer->data[a].ireg = vid;
	}

	if (trace_mask & TRACE_VF) {
		struct reg v = { .ireg = vid };
		ksim_trace(TRACE_VF, "Loaded vue for idd=%d, vid=[", iid);
		for (uint32_t c = 0; c < 8; c++)
			ksim_trace(TRACE_VF, " %d", v.ud[c]);
		ksim_trace(TRACE_VF, " ]\n");

		uint32_t count = gt.vf.ve_count;
		if (gt.vf.iid_element + 1 > count)
			count = gt.vf.iid_element + 1;
		if (gt.vf.vid_element + 1 > count)
			count = gt.vf.vid_element + 1;
		for (uint32_t i = 0; i < count * 4; i++) {
			ksim_trace(TRACE_VF, "    ");
			for (uint32_t c = 0; c < 8; c++)
				ksim_trace(TRACE_VF, "  %8.2f", buffer->data[i].f[c]);
			ksim_trace(TRACE_VF, "\n");
		}
	}
}

static void
flush_to_vues(struct vf_buffer *buffer, struct value **vues, __m256i mask)
{
	/* Transpose the SIMD8 vf_buffer back into individual VUEs */
	const struct reg m = { .ireg = mask };
	for (uint32_t c = 0; c < 8; c++) {
		if (m.d[c] >= 0)
			continue;

		__m256i *vue = urb_handle_to_entry(buffer->vue_handles.ud[c]);
		__m256i offsets = (__m256i) (__v8si) { 0, 8, 16, 24, 32, 40, 48, 56 };
		for (uint32_t i = 0; i < gt.vs.urb.size / 32; i++)
			vue[i] = _mm256_i32gather_epi32(&buffer->data[i * 8].d[c], offsets, 4);

		vues[c] = (struct value *) vue;
 	}
}

void
dispatch_primitive(void)
{
	validate_vf_state();

	validate_urb_state();

	if (gt.sf.viewport_transform_enable)
		dump_sf_clip_viewport();

	ksim_assert(gt.vs.simd8 || !gt.vs.enable);

	prepare_shaders();

	gt.depth.write_enable =
		gt.depth.write_enable0 && gt.depth.write_enable1;

	/* Configure csr to round toward zero to make vcvtps2dq match
	 * the GEN EU behavior when converting from float to int. This
	 * may disagree with the rounding mode programmed in
	 * 3DSTATE_PS etc, which only affects rounding of internal
	 * intermediate float results. */
	const uint32_t csr_default =
		_MM_MASK_INVALID |
		_MM_MASK_DENORM |
		_MM_MASK_DIV_ZERO |
		_MM_MASK_OVERFLOW |
		_MM_MASK_UNDERFLOW |
		_MM_MASK_INEXACT |
		_MM_ROUND_TOWARD_ZERO;

	_mm_setcsr(csr_default);

	struct vf_buffer buffer;
	static const struct reg range = { .d = {  0, 1, 2, 3, 4, 5, 6, 7 } };
	for (uint32_t iid = 0; iid < gt.prim.instance_count; iid++) {
		for (uint32_t i = 0; i < gt.prim.vertex_count; i += 8) {
			__m256i vid = _mm256_add_epi32(range.ireg, _mm256_set1_epi32(i));
			__m256i mask = _mm256_sub_epi32(range.ireg, _mm256_set1_epi32(gt.prim.vertex_count - i));
			struct value *vues[8];
			uint32_t dwmask = _mm256_movemask_ps((__m256) mask);

			fetch_vertices(&buffer, iid, vid, mask);
			dispatch_vs(&buffer, mask);
			flush_to_vues(&buffer, vues, mask);
			transform_and_queue_vues(vues, dwmask);
			assemble_primitives();
		}

		reset_ia_state();
	}

	if (gt.vf.statistics)
		gt.ia_vertices_count +=
			gt.prim.vertex_count * gt.prim.instance_count;

	wm_flush();
}

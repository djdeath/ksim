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

struct free_urb {
	uint32_t next;
};

static void *
alloc_urb_entry(struct urb *urb)
{
	struct free_urb *f;
	void *p;

	if (urb->free_list != URB_EMPTY) {
		f = p = urb->data + urb->free_list;
		urb->free_list = f->next;
	} else {
		ksim_assert(urb->count < urb->total);
		p = urb->data + urb->size * urb->count++;
	}

	ksim_assert(p >= urb->data && p < urb->data + urb->total * urb->size);
	ksim_assert(p >= (void *) gt.urb && p < (void *) gt.urb + sizeof(gt.urb));

	return p;
}

static void
free_urb_entry(struct urb* urb, void *entry)
{
	struct free_urb *f = entry;

	f->next = urb->free_list;
	urb->free_list = entry - urb->data;
}

static inline int32_t
fp_as_int32(float f)
{
	return (union { float f; int32_t i; }) { .f = f }.i;
}

static int32_t
store_component(uint32_t cc, int32_t src)
{
	switch (cc) {
	case VFCOMP_NOSTORE:
		return 77; /* shouldn't matter */
	case VFCOMP_STORE_SRC:
		return src;
	case VFCOMP_STORE_0:
		return 0;
	case VFCOMP_STORE_1_FP:
		return fp_as_int32(1.0f);
	case VFCOMP_STORE_1_INT:
		return 1;
	case VFCOMP_STORE_PID:
		return 0; /* what's pid again? */
	default:
		ksim_warn("illegal component control: %d\n", cc);
		return 0;
	}
}

static struct value *
fetch_vertex(uint32_t instance_id, uint32_t vertex_id)
{
	struct value *vue;
	struct value v;

	vue = alloc_urb_entry(&gt.vs.urb);
	for (uint32_t i = 0; i < gt.vf.ve_count; i++) {
		struct ve *ve = &gt.vf.ve[i];
		ksim_assert((1 << ve->vb) & gt.vf.vb_valid);
		struct vb *vb = &gt.vf.vb[ve->vb];

		if (!gt.vf.ve[i].valid)
			continue;

		uint32_t index;
		if (gt.vf.ve[i].instancing) {
			index = gt.prim.start_instance + instance_id / gt.vf.ve[i].step_rate;
		} else if (gt.prim.access_type == RANDOM) {
			uint64_t range;
			void *ib = map_gtt_offset(gt.vf.ib.address, &range);

			index = gt.prim.start_vertex + vertex_id;

			switch (gt.vf.ib.format) {
			case INDEX_BYTE:
				index = ((uint8_t *) ib)[index] + gt.prim.base_vertex;
				break;
			case INDEX_WORD:
				index = ((uint16_t *) ib)[index] + gt.prim.base_vertex;
				break;
			case INDEX_DWORD:
				index = ((uint32_t *) ib)[index] + gt.prim.base_vertex;
				break;
			}
		} else {
			index = gt.prim.start_vertex + vertex_id;
		}

		uint32_t offset = index * vb->pitch + ve->offset;
		if (offset + format_size(ve->format) > vb->size) {
			ksim_trace(TRACE_WARN, "vertex element %d overflows vertex buffer %d\n",
				   i, ve->vb);
			v = vec4(0, 0, 0, 0);
		} else {
			v = fetch_format(vb->data + offset, ve->format);
		}

		for (uint32_t c = 0; c < 4; c++)
			vue[i].v[c] = store_component(ve->cc[c], v.v[c]);

		/* edgeflag */
	}

	/* 3DSTATE_VF_SGVS */
	if (gt.vf.iid_enable && gt.vf.vid_enable)
		ksim_assert(gt.vf.iid_element != gt.vf.vid_element ||
			    gt.vf.iid_component != gt.vf.vid_component);

	if (gt.vf.iid_enable)
		vue[gt.vf.iid_element].v[gt.vf.iid_component] = instance_id;
	if (gt.vf.vid_enable)
		vue[gt.vf.vid_element].v[gt.vf.vid_component] = vertex_id;

	if (trace_mask & TRACE_VF) {
		ksim_trace(TRACE_VF, "Loaded vue for vid=%d, iid=%d:\n",
			   vertex_id, instance_id);
		uint32_t count = gt.vf.ve_count;
		if (gt.vf.iid_element + 1 > count)
			count = gt.vf.iid_element + 1;
		if (gt.vf.vid_element + 1 > count)
			count = gt.vf.vid_element + 1;
		for (uint32_t i = 0; i < count; i++)
			ksim_trace(TRACE_VF, "    %8.2f  %8.2f  %8.2f  %8.2f\n",
				   vue[i].f[0], vue[i].f[1], vue[i].f[2], vue[i].f[3]);
	}

	return vue;
}

static void
dispatch_vs(struct value **vue, uint32_t mask)
{
	struct thread t;
	uint32_t g, c;

	if (!gt.vs.enable)
		return;

	assert(gt.vs.simd8);

	/* Not sure what we should make this. */
	uint32_t fftid = 0;

	t.mask = mask;
	/* Fixed function header */
	t.grf[0] = (struct reg) {
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

	for_each_bit(c, mask)
		t.grf[1].ud[c] = urb_entry_to_handle(vue[c]);

	g = load_constants(&t, &gt.vs.curbe, gt.vs.urb_start_grf);

	/* SIMD8 VS payload */
	for (uint32_t i = 0; i < gt.vs.vue_read_length * 2; i++) {
		for_each_bit(c, mask) {
			for (uint32_t j = 0; j < 4; j++)
				t.grf[g + j].ud[c] = vue[c][gt.vs.vue_read_offset * 2 + i].v[j];
		}
		g += 4;
	}

	if (gt.vs.statistics)
		gt.vs_invocation_count++;

	dispatch_shader(gt.vs.avx_shader, &t);
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
validate_urb_state(void)
{
	struct urb *all_urbs[] = {
		&gt.vs.urb,
		&gt.hs.urb,
		&gt.ds.urb,
		&gt.gs.urb,
	}, *u, *v;

	/* Validate that the URB allocations are properly sized and
	 * don't overlap
	 */

	for (uint32_t i = 0; i < ARRAY_LENGTH(all_urbs); i++) {
		u = all_urbs[i];
		char *ustart = u->data;
		char *uend = ustart + u->total * u->size;
		ksim_assert(gt.urb <= ustart && uend <= gt.urb + sizeof(gt.urb));

		for (uint32_t j = i + 1; j < ARRAY_LENGTH(all_urbs); j++) {
			v = all_urbs[j];
			char *vstart = v->data;
			char *vend = v->data + v->total * v->size;
			ksim_assert(vend <= ustart || uend <= vstart);
		}
	}

	/* If we're doing SIMD8 vs dispatch, we need at least 8 VUEs,
	 * but the BDW hw limit is even higher: 64. */
	ksim_assert(64 <= gt.vs.urb.total && gt.vs.urb.total <= 2560);

}

static void
dump_sf_clip_viewport(void)
{
	uint64_t range;
	float *p = map_gtt_offset(gt.sf.viewport_pointer, &range);

	spam("gt.sf.viewport_pointer: 0x%08x, (w/o dyn base: 0x%08x)\n",
	     gt.sf.viewport_pointer,
	     gt.sf.viewport_pointer - gt.dynamic_state_base_address);

	ksim_assert(range >= 14 * sizeof(*p));

	spam("sf_clip viewport: %08x\n", gt.sf.viewport_pointer);
	for (uint32_t i = 0; i < 14; i++)
		spam("  %20.4f\n", p[i]);
}

static void
setup_prim(struct value **vue, uint32_t parity)
{
	struct primitive prim;
	uint32_t provoking;

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
	}

	static const int indices[5] = { 0, 1, 2, 0, 1 };
	prim.vue[0] = vue[indices[provoking]];
	prim.vue[1] = vue[indices[provoking + 1 + parity]];
	prim.vue[2] = vue[indices[provoking + 2 - parity]];

	rasterize_primitive(&prim);
}

static void
transform_and_queue_vues(struct value **vue, int count)
{
	uint64_t range;
	const float *vp = map_gtt_offset(gt.sf.viewport_pointer, &range);
	ksim_assert(range >= 14 * sizeof(vp[0]));

	float m00 = vp[0];
	float m11 = vp[1];
	float m22 = vp[2];
	float m30 = vp[3];
	float m31 = vp[4];
	float m32 = vp[5];

	for (int i = 0; i < count; i++) {
		struct vec4 *pos = &vue[i][1].vec4;
		if (!gt.clip.perspective_divide_disable) {
			float inv_w = 1.0f / pos->w;
			pos->x *= inv_w;
			pos->y *= inv_w;
			pos->z *= inv_w;
			pos->w = 1;
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

void
dispatch_primitive(void)
{
	uint32_t i = 0;
	struct value *vue[8];
	uint32_t iid, vid;

	validate_vf_state();

	validate_urb_state();

	if (gt.sf.viewport_transform_enable)
		dump_sf_clip_viewport();

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

	for (iid = 0; iid < gt.prim.instance_count; iid++) {
		for (vid = 0; vid < gt.prim.vertex_count; vid++) {
			vue[i++] = fetch_vertex(iid, vid);
			if (gt.vf.statistics)
				gt.ia_vertices_count++;
			if (i == 8) {
				dispatch_vs(vue, 255);
				transform_and_queue_vues(vue, i);
				assemble_primitives(vue, i);
				i = 0;
			}
		}
		if (i > 0) {
			dispatch_vs(vue, (1 << i) - 1);
			transform_and_queue_vues(vue, i);
			assemble_primitives(vue, i);
			i = 0;
		}

		reset_ia_state();
	}

	wm_flush();
}

/*
 * Copyright © 2016 Intel Corporation
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
#include "kir.h"

static void
dispatch_group(uint32_t x, uint32_t y, uint32_t z)
{
	/* Not sure what we should make this. */
	const uint32_t fftid = 0 & 0x1ff;
	const uint32_t gpgpu_dispatch = 1 << 9;
	const uint32_t urb_handle = 0;
	const uint32_t stack_size = 0;
	struct thread t;

	t.grf[0] = (struct reg) {
		.ud = {
			/* R0.0: URB handle and SLM index */
			urb_handle,
			/* R0.1: Thread group ID x */
			x,
			/* R0.2: Barrier ID and enable bits */
			0, /* FIXME */
			/* R0.3: per-thread scratch space, sampler ptr */
			gt.compute.sampler_state_address |
			gt.compute.scratch_size,
			/* R0.4: binding table pointer */
			gt.compute.binding_table_address | stack_size,
			/* R0.5: fftid, scratch offset */
			gt.compute.scratch_pointer | gpgpu_dispatch | fftid,
			/* R0.6: Thread group ID Y */
			y,
			/* R0.7: Thread group ID Z */
			z,
		}
	};

	ksim_assert(gt.compute.depth == 1);
	ksim_assert(gt.compute.height == 1);

	static const struct reg shift = { .d = {  23, 22, 21, 20, 19, 18, 17, 16 } };
	__m256i right_mask = _mm256_set1_epi32(gt.compute.right_mask);
	right_mask = _mm256_sllv_epi32(right_mask, shift.ireg);
	__m256i right_mask_q1 = _mm256_srai_epi32(right_mask, 31);
	right_mask = _mm256_slli_epi32(right_mask, 8);
	__m256i right_mask_q0 = _mm256_srai_epi32(right_mask, 31);

	struct reg *src = gt.compute.curbe_data;
	uint32_t size_in_regs = gt.compute.curbe_data_length / 32;
	for (uint32_t i = 0; i < gt.compute.width; i++) {
		struct reg *dst = &t.grf[1];
		for (uint32_t j = 0; j < size_in_regs; j++)
			dst[i].ireg = src[i].ireg;

		if (i < gt.compute.width - 1) {
			t.mask_q1 = _mm256_set1_epi32(-1);
			t.mask_q2 = _mm256_set1_epi32(-1);
		} else {
			t.mask_q1 = right_mask_q0;
			t.mask_q2 = right_mask_q1;
		}

		gt.compute.avx_shader(&t);

		src += size_in_regs;
	}
}

static void
compile_cs(void)
{
	struct kir_program prog;

	kir_program_init(&prog, gt.compute.binding_table_address,
			 gt.compute.sampler_state_address);

	kir_program_emit_shader(&prog, gt.compute.ksp);

	kir_program_add_insn(&prog, kir_eot);

	gt.compute.avx_shader = kir_program_finish(&prog);
}

void
dispatch_compute(void)
{
	reset_shader_pool();
	compile_cs();

	/* FIXME: Any compute statistics that we need to maintain? */

	/* We use this little awkward goto to resume with the given
	 * start values. x and y are supposed start from start_x and
	 * start_y but revert back to 0 once they reach end_x and
	 * end_y. */
	uint32_t x = gt.compute.start_x;
	uint32_t y = gt.compute.start_y;
	uint32_t z = gt.compute.start_z;
	goto resume;

	for (; z < gt.compute.end_z; z++) {
		for (y = 0; y < gt.compute.end_y; y++) {
			for (x = 0; x < gt.compute.end_x; x++) {
			resume:
				dispatch_group(x, y, z);
			}
		}
	}
}

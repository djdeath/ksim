#include <stdint.h>
#include <stdbool.h>
#include <signal.h>

#include "ksim.h"

struct gt gt;

static inline uint32_t
field(uint32_t value, int start, int end)
{
	uint32_t mask;

	mask = ~0U >> (31 - end + start);

	return (value >> start) & mask;
}

static inline uint64_t
get_u64(uint32_t *p)
{
	return p[0] | ((uint64_t) p[1] << 32);
}

static void
illegal_opcode(void)
{
	ksim_assert(!"illegal opcode");
}

static void
unhandled_command(uint32_t *p)
{
	ksim_trace(TRACE_CS, "unhandled command\n");
}

/* MI commands */

static void
handle_mi_noop(uint32_t *p)
{
	ksim_trace(TRACE_CS, "MI_NOOP\n");
}

static void
handle_mi_batch_buffer_end(uint32_t *p)
{
	ksim_trace(TRACE_CS, "MI_BATCH_BUFFER_END\n");
}

static void
handle_mi_math(uint32_t *p)
{
	ksim_trace(TRACE_CS, "MI_MATH\n");
}

#define GEN7_3DPRIM_END_OFFSET          0x2420
#define GEN7_3DPRIM_START_VERTEX        0x2430
#define GEN7_3DPRIM_VERTEX_COUNT        0x2434
#define GEN7_3DPRIM_INSTANCE_COUNT      0x2438
#define GEN7_3DPRIM_START_INSTANCE      0x243C
#define GEN7_3DPRIM_BASE_VERTEX         0x2440
#define GPGPU_DISPATCHDIMX		0x2500
#define GPGPU_DISPATCHDIMY		0x2504
#define GPGPU_DISPATCHDIMZ		0x2508

static void
write_register(uint32_t reg, uint32_t value)
{
	switch (reg) {
	case GEN7_3DPRIM_END_OFFSET:
		break;
	case GEN7_3DPRIM_START_VERTEX:
		gt.prim.start_vertex = value;
		break;
	case GEN7_3DPRIM_VERTEX_COUNT:
		gt.prim.vertex_count = value;
		break;
	case GEN7_3DPRIM_INSTANCE_COUNT:
		gt.prim.instance_count = value;
		break;
	case GEN7_3DPRIM_START_INSTANCE:
		gt.prim.start_instance = value;
		break;
	case GEN7_3DPRIM_BASE_VERTEX:
		gt.prim.base_vertex = value;
		break;
	case GPGPU_DISPATCHDIMX:
		gt.dispatch.dimx = value;
		break;
	case GPGPU_DISPATCHDIMY:
		gt.dispatch.dimy = value;
		break;
	case GPGPU_DISPATCHDIMZ:
		gt.dispatch.dimz = value;
		break;
	}
}

static void
handle_mi_load_register_imm(uint32_t *p)
{
	ksim_trace(TRACE_CS, "MI_LOAD_REGISTER_IMM\n");

	write_register(p[1], p[2]);
}

static void
handle_mi_load_register_mem(uint32_t *p)
{
	uint64_t address = get_u64(&p[2]);
	uint64_t range;
	uint32_t *value = map_gtt_offset(address, &range);

	ksim_trace(TRACE_CS, "MI_LOAD_REGISTER_MEM\n");

	ksim_assert(range >= sizeof(*value));
	write_register(p[1], *value);
}

static void
handle_mi_atomic(uint32_t *p)
{
	ksim_trace(TRACE_CS, "MI_ATOMIC\n");
}

static void
handle_mi_batch_buffer_start(uint32_t *p)
{
	ksim_trace(TRACE_CS, "MI_BATCH_BUFFER_START\n");
}

typedef void (*command_handler_t)(uint32_t *);

static const command_handler_t mi_commands[] = {
	[ 0] = handle_mi_noop,
	[10] = handle_mi_batch_buffer_end,
	[26] = handle_mi_math,
	[34] = handle_mi_load_register_imm,
	[41] = handle_mi_load_register_mem,
	[47] = handle_mi_atomic,
	[49] = handle_mi_batch_buffer_start
};

static void
handle_state_base_address(uint32_t *p)
{
	const uint64_t mask = ~0xfff;

	ksim_trace(TRACE_CS, "STATE_BASE_ADDRESS\n");

	if (field(p[1], 0, 0))
		gt.general_state_base_address = get_u64(&p[1]) & mask;
	if (field(p[4], 0, 0))
		gt.surface_state_base_address = get_u64(&p[4]) & mask;
	if (field(p[6], 0, 0))
		gt.dynamic_state_base_address = get_u64(&p[6]) & mask;
	if (field(p[8], 0, 0))
		gt.indirect_object_base_address = get_u64(&p[8]) & mask;
	if (field(p[10], 0, 0))
		gt.instruction_base_address = get_u64(&p[10]) & mask;

	if (field(p[12], 0, 0))
		gt.general_state_buffer_size = p[12] & mask;
	if (field(p[13], 0, 0))
		gt.dynamic_state_buffer_size = p[13] & mask;
	if (field(p[14], 0, 0))
		gt.indirect_object_buffer_size = p[14] & mask;
	if (field(p[15], 0, 0))
		gt.general_instruction_size = p[15] & mask;
}

static void
handle_state_sip(uint32_t *p)
{
	ksim_trace(TRACE_CS, "STATE_SIP\n");

	gt.sip_address = get_u64(&p[1]);
}

static void
handle_swtess_base_address(uint32_t *p)
{
	ksim_trace(TRACE_CS, "SWTESS_BASE_ADDRESS\n");
}

static void
handle_gpgpu_csr_base_address(uint32_t *p)
{
	ksim_trace(TRACE_CS, "GPGPU_CSR_BASE_ADDRESS\n");
}

static const command_handler_t nonpipelined_common_commands[] = {
	[ 1] = handle_state_base_address,
	[ 2] = handle_state_sip,
	[ 3] = handle_swtess_base_address,
	[ 4] = handle_gpgpu_csr_base_address,
};

static command_handler_t
get_common_command(uint32_t *p)
{
	uint32_t h = p[0];
	uint32_t opcode = field(h, 24, 26);
	uint32_t subopcode = field(h, 16, 23);

	if (opcode == 0) { /* pipelined common state */
		/* STATE_PREFETCH subopcode  3 */
		return NULL;
	} else if (opcode == 1) {
		return nonpipelined_common_commands[subopcode];
	} else {
		return NULL;
	}
}

static void
handle_pipeline_select(uint32_t *p)
{
	ksim_trace(TRACE_CS, "PIPELINE_SELECT\n");

	uint32_t pipeline = field(p[0], 0, 1);

	ksim_assert(pipeline == _3D);
}

static void
handle_3dstate_vf_statistics(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_VF_STATISTICS\n");

	gt.vf.statistics = field(p[0], 0, 0);
}

static command_handler_t
get_dword_command(uint32_t *p)
{
	uint32_t h = p[0];
	uint32_t opcode = field(h, 24, 26);
	uint32_t subopcode = field(h, 16, 23);

	/* opcode 0 is pipelined, 1 is non-pipelined. */
	if (opcode == 0 && subopcode == 11) {
		return handle_3dstate_vf_statistics;
	} else if (opcode == 1 && subopcode == 4) {
		return handle_pipeline_select;
	} else {
		return NULL;
	}

}

/* Pipelined 3dstate commands */

static void
handle_3dstate_clear_params(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CLEAR_PARAMS\n");
}

static void
handle_3dstate_depth_buffer(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_DEPTH_BUFFER\n");
}

static void
handle_3dstate_stencil_buffer(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_STENCIL_BUFFER\n");
}

static void
handle_3dstate_hier_depth_buffer(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_HIER_DEPTH_BUFFER\n");
}

static void
handle_3dstate_vertex_buffers(uint32_t *p)
{
	uint32_t h = p[0];
	uint32_t length = field(h, 0, 7) + 2;

	ksim_trace(TRACE_CS, "3DSTATE_VERTEX_BUFFERS\n");
	ksim_assert((length - 1) % 4 == 0);

	for (uint32_t i = 1; i < length; i += 4) {
		uint32_t vb = field(p[i], 26, 31);
		bool modify_address = field(p[i], 14, 14);
		gt.vf.vb[vb].pitch = field(p[i], 0, 11);
		if (modify_address)
			gt.vf.vb[vb].address = * (uint64_t *) &p[i + 1];
		gt.vf.vb[vb].size = p[i + 3];
		gt.vf.vb_valid |= 1 << vb;
	}
}

static void
handle_3dstate_vertex_elements(uint32_t *p)
{
	uint32_t h = p[0];
	uint32_t length = field(h, 0, 7) + 2;

	ksim_trace(TRACE_CS, "3DSTATE_VERTEX_ELEMENTS\n");
	ksim_assert((length - 1) % 2 == 0);

	for (uint32_t i = 1, n = 0; i < length; i += 2, n++) {
		gt.vf.ve[n].vb = field(p[i], 26, 31);
		gt.vf.ve[n].valid = field(p[i], 25, 25);
		gt.vf.ve[n].format = field(p[i], 16, 24);
		gt.vf.ve[n].edgeflag = field(p[i], 15, 15);
		gt.vf.ve[n].offset = field(p[i], 0, 11);
		gt.vf.ve[n].cc[0] = field(p[i + 1], 28, 30);
		gt.vf.ve[n].cc[1] = field(p[i + 1], 24, 26);
		gt.vf.ve[n].cc[2] = field(p[i + 1], 20, 22);
		gt.vf.ve[n].cc[3] = field(p[i + 1], 16, 18);
	}

	gt.vf.ve_count = (length - 1) / 2;
}

static void
handle_3dstate_index_buffer(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_INDEX_BUFFER\n");

	gt.vf.ib.format = field(p[1], 8, 9);
	gt.vf.ib.address = get_u64(&p[2]);
	gt.vf.ib.size = p[4];
}

static void
handle_3dstate_vf(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_VF\n");

	gt.vf.cut_index = field(p[1], 0, 31);
}

static void
handle_3dstate_multisample(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_MULTISAMPLE\n");
}

static void
handle_3dstate_cc_state_pointers(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CC_STATE_POINTERS\n");
}

static void
handle_3dstate_scissor_state_pointers(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SCISSOR_STATE_POINTERS\n");
}

static void
handle_3dstate_vs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_VS\n");

	gt.vs.ksp = get_u64(&p[1]);

	gt.vs.single_dispatch = field(p[3], 31, 31);
	gt.vs.vector_mask = field(p[3], 30, 30);
	gt.vs.binding_table_entry_count = field(p[3], 18, 25);
	gt.vs.priority = field(p[3], 17, 17);
	gt.vs.alternate_fp = field(p[3], 16, 16);
	gt.vs.opcode_exception = field(p[3], 13, 13);
	gt.vs.access_uav = field(p[3], 12, 12);
	gt.vs.sw_exception = field(p[3], 7, 7);

	gt.vs.scratch_pointer = get_u64(&p[4]) & ~1023;
	gt.vs.scratch_size = field(p[4], 0, 3);

	gt.vs.urb_start_grf = field(p[6], 20, 24);
	gt.vs.vue_read_length = field(p[6], 11, 16);
	gt.vs.vue_read_offset = field(p[6], 4, 9);

	gt.vs.statistics = field(p[7], 10, 10);
	gt.vs.simd8 = field(p[7], 2, 2);
	gt.vs.enable = field(p[7], 0, 0);
}

static void
handle_3dstate_gs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_GS\n");
}

static void
handle_3dstate_clip(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CLIP\n");
}

static void
handle_3dstate_sf(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SF\n");
}

static void
handle_3dstate_wm(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_WM\n");
}

static void
fill_curbe(struct curbe *c, uint32_t *p)
{
	c->buffer[0].length = field(p[1], 0, 15);
	c->buffer[1].length = field(p[1], 16, 31);
	c->buffer[2].length = field(p[2], 0, 15);
	c->buffer[3].length = field(p[2], 16, 31);

	c->buffer[0].address = get_u64(&p[3]);
	c->buffer[1].address = get_u64(&p[5]);
	c->buffer[2].address = get_u64(&p[7]);
	c->buffer[3].address = get_u64(&p[9]);
}

static void
handle_3dstate_constant_vs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CONSTANT_VS\n");

	fill_curbe(&gt.vs.curbe, p);
}

static void
handle_3dstate_constant_gs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CONSTANT_GS\n");

	fill_curbe(&gt.gs.curbe, p);
}

static void
handle_3dstate_constant_ps(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CONSTANT_PS\n");

	fill_curbe(&gt.ps.curbe, p);
}

static void
handle_3dstate_sample_mask(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLE_MASK\n");
}

static void
handle_3dstate_constant_hs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CONSTANT_HS\n");

	fill_curbe(&gt.hs.curbe, p);
}

static void
handle_3dstate_constant_ds(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CONSTANT_DS\n");

	fill_curbe(&gt.ds.curbe, p);
}

static void
handle_3dstate_hs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_HS\n");
}

static void
handle_3dstate_te(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_TE\n");
}

static void
handle_3dstate_ds(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_DS\n");
}

static void
handle_3dstate_steamout(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_STEAMOUT\n");
}

static void
handle_3dstate_sbe(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SBE\n");
}

static void
handle_3dstate_ps(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_PS\n");
}

static void
handle_3dstate_viewport_state_pointer_sf_clip(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_VIEWPORT_STATE_POINTER_SF_CLIP\n");

	/* The driver is required to reemit dynamic indirect state
	 * packets (viewports and such) after emitting
	 * STATE_BASE_ADDRESS, which sounds like the dynamic state
	 * base address is used by the command streamer. */

	gt.sf.viewport_pointer = gt.dynamic_state_base_address + p[1];
}

static void
handle_3dstate_viewport_state_pointer_cc(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_VIEWPORT_STATE_POINTER_CC\n");

	gt.cc.viewport_pointer = gt.dynamic_state_base_address + p[1];
}

static void
handle_3dstate_blend_state_pointers(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BLEND_STATE_POINTERS\n");
}

static void
handle_3dstate_binding_table_pointers_vs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_POINTERS_VS\n");

	gt.vs.binding_table_address = p[1];
}

static void
handle_3dstate_binding_table_pointers_hs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_POINTERS_HS\n");

	gt.hs.binding_table_address = p[1];
}

static void
handle_3dstate_binding_table_pointers_ds(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_POINTERS_DS\n");

	gt.ds.binding_table_address = p[1];
}

static void
handle_3dstate_binding_table_pointers_gs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_POINTERS_GS\n");

	gt.gs.binding_table_address = p[1];
}

static void
handle_3dstate_binding_table_pointers_ps(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_POINTERS_PS\n");

	gt.ps.binding_table_address = p[1];
}

static void
handle_3dstate_sampler_state_pointers_vs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLER_STATE_POINTERS_VS\n");

	gt.vs.sampler_state_address = p[1];
}

static void
handle_3dstate_sampler_state_pointers_hs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLER_STATE_POINTERS_HS\n");

	gt.hs.sampler_state_address = p[1];
}

static void
handle_3dstate_sampler_state_pointers_ds(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLER_STATE_POINTERS_DS\n");

	gt.ds.sampler_state_address = p[1];
}

static void
handle_3dstate_sampler_state_pointers_gs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLER_STATE_POINTERS_GS\n");

	gt.gs.sampler_state_address = p[1];
}

static void
handle_3dstate_sampler_state_pointers_ps(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLER_STATE_POINTERS_PS\n");

	gt.ps.sampler_state_address = p[1];
}

static void
set_urb_allocation(struct urb *urb, uint32_t *p)
{
	const uint32_t chunk_size_bytes = 8192;

	urb->data = gt.urb + field(p[1], 25, 31) * chunk_size_bytes;
	urb->size = (field(p[1], 16, 24) + 1) * 64;
	urb->total = field(p[1], 0, 15);

	urb->free_list = 1;
	urb->count = 0;
}

static void
handle_3dstate_urb_vs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_URB_VS\n");

	set_urb_allocation(&gt.vs.urb, p);
	ksim_trace(TRACE_CS, "vs urb: start=%d, size=%d, total=%d\n",
		   gt.vs.urb.data - (void *) gt.urb,
		   gt.vs.urb.size, gt.vs.urb.total);
}

static void
handle_3dstate_urb_hs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_URB_HS\n");

	set_urb_allocation(&gt.hs.urb, p);
}

static void
handle_3dstate_urb_ds(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_URB_DS\n");

	set_urb_allocation(&gt.ds.urb, p);
}

static void
handle_3dstate_urb_gs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_URB_GS\n");

	set_urb_allocation(&gt.gs.urb, p);
}

static void
handle_gather_constant_vs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "GATHER_CONSTANT_VS\n");
}

static void
handle_gather_constant_gs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "GATHER_CONSTANT_GS\n");
}

static void
handle_gather_constant_hs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "GATHER_CONSTANT_HS\n");
}

static void
handle_gather_constant_ds(uint32_t *p)
{
	ksim_trace(TRACE_CS, "GATHER_CONSTANT_DS\n");
}

static void
handle_gather_constant_ps(uint32_t *p)
{
	ksim_trace(TRACE_CS, "GATHER_CONSTANT_PS\n");
}

static void
handle_3dstate_binding_table_edit_vs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_EDIT_VS\n");
}

static void
handle_3dstate_binding_table_edit_gs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_EDIT_GS\n");
}

static void
handle_3dstate_binding_table_edit_hs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_EDIT_HS\n");
}

static void
handle_3dstate_binding_table_edit_ds(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_EDIT_DS\n");
}

static void
handle_3dstate_binding_table_edit_ps(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_EDIT_PS\n");
}

static void
handle_3dstate_vf_instancing(uint32_t *p)
{
	uint32_t ve = field(p[1], 0, 5);

	ksim_trace(TRACE_CS, "3DSTATE_VF_INSTANCING\n");

	gt.vf.ve[ve].instancing = field(p[1], 8, 8);
	gt.vf.ve[ve].step_rate = field(p[1], 0, 5);
}

static void
handle_3dstate_vf_sgvs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_VF_SGVS\n");

	gt.vf.iid_enable = field(p[1], 31, 31);
	gt.vf.iid_component = field(p[1], 29, 30);
	gt.vf.iid_element = field(p[1], 16, 21);

	gt.vf.vid_enable = field(p[1], 15, 15);
	gt.vf.vid_component = field(p[1], 13, 14);
	gt.vf.vid_element = field(p[1], 0, 5);
}

static void
handle_3dstate_vf_topology(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_VF_TOPOLOGY\n");

	gt.vf.topology = field(p[1], 0, 5);
}

static void
handle_3dstate_wm_chromakey(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_WM_CHROMAKEY\n");
}

static void
handle_3dstate_ps_blend(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_PS_BLEND\n");
}

static void
handle_3dstate_wm_depth_stencil(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_WM_DEPTH_STENCIL\n");
}

static void
handle_3dstate_ps_extra(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_PS_EXTRA\n");
}

static void
handle_3dstate_raster(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_RASTER\n");
}

static void
handle_3dstate_sbe_swiz(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SBE_SWIZ\n");
}

static void
handle_3dstate_wm_hz_op(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_WM_HZ_OP\n");
}

static const command_handler_t pipelined_3dstate_commands[] = {
	[ 4] = handle_3dstate_clear_params,
	[ 5] = handle_3dstate_depth_buffer,
	[ 6] = handle_3dstate_stencil_buffer,
	[ 7] = handle_3dstate_hier_depth_buffer,
	[ 8] = handle_3dstate_vertex_buffers,
	[ 9] = handle_3dstate_vertex_elements,
	[10] = handle_3dstate_index_buffer,

	[12] = handle_3dstate_vf,
	[13] = handle_3dstate_multisample,
	[14] = handle_3dstate_cc_state_pointers,
	[15] = handle_3dstate_scissor_state_pointers,
	[16] = handle_3dstate_vs,
	[17] = handle_3dstate_gs,
	[18] = handle_3dstate_clip,
	[19] = handle_3dstate_sf,
	[20] = handle_3dstate_wm,

	[21] = handle_3dstate_constant_vs,
	[22] = handle_3dstate_constant_gs,
	[23] = handle_3dstate_constant_ps,
	[24] = handle_3dstate_sample_mask,
	[25] = handle_3dstate_constant_hs,
	[26] = handle_3dstate_constant_ds,

	[27] = handle_3dstate_hs,
	[28] = handle_3dstate_te,
	[29] = handle_3dstate_ds,
	[30] = handle_3dstate_steamout,
	[31] = handle_3dstate_sbe,
	[32] = handle_3dstate_ps,

	[33] = handle_3dstate_viewport_state_pointer_sf_clip,
	[35] = handle_3dstate_viewport_state_pointer_cc,
	[36] = handle_3dstate_blend_state_pointers,

	[38] = handle_3dstate_binding_table_pointers_vs,
	[39] = handle_3dstate_binding_table_pointers_hs,
	[40] = handle_3dstate_binding_table_pointers_ds,
	[41] = handle_3dstate_binding_table_pointers_gs,
	[42] = handle_3dstate_binding_table_pointers_ps,

	[43] = handle_3dstate_sampler_state_pointers_vs,
	[44] = handle_3dstate_sampler_state_pointers_hs,
	[45] = handle_3dstate_sampler_state_pointers_ds,
	[46] = handle_3dstate_sampler_state_pointers_gs,
	[47] = handle_3dstate_sampler_state_pointers_ps,

	[48] = handle_3dstate_urb_vs,
	[49] = handle_3dstate_urb_hs,
	[50] = handle_3dstate_urb_ds,
	[51] = handle_3dstate_urb_gs,

	[52] = handle_gather_constant_vs,
	[53] = handle_gather_constant_gs,
	[54] = handle_gather_constant_hs,
	[55] = handle_gather_constant_ds,
	[56] = handle_gather_constant_ps,

	[67] = handle_3dstate_binding_table_edit_vs,
	[68] = handle_3dstate_binding_table_edit_gs,
	[69] = handle_3dstate_binding_table_edit_hs,
	[70] = handle_3dstate_binding_table_edit_ds,
	[71] = handle_3dstate_binding_table_edit_ps,
	[73] = handle_3dstate_vf_instancing,
	[74] = handle_3dstate_vf_sgvs,
	[75] = handle_3dstate_vf_topology,
	[76] = handle_3dstate_wm_chromakey,
	[77] = handle_3dstate_ps_blend,
	[78] = handle_3dstate_wm_depth_stencil,
	[79] = handle_3dstate_ps_extra,
	[80] = handle_3dstate_raster,
	[81] = handle_3dstate_sbe_swiz,
	[82] = handle_3dstate_wm_hz_op,
};

/* Non-pipelined 3dstate commands */

static void
fill_curbe_alloc(struct curbe *c, uint32_t *p)
{
	c->size = field(p[1], 0, 5) * 1024;
}

static void
handle_3dstate_drawing_rectangle(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_DRAWING_RECTANGLE\n");
}

static void
handle_3dstate_sampler_palette_load0(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLER_PALETTE_LOAD0\n");
}

static void
handle_3dstate_chroma_key(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_CHROMA_KEY\n");
}

static void
handle_3dstate_poly_stipple_offset(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_POLY_STIPPLE_OFFSET\n");
}

static void
handle_3dstate_poly_stipple_pattern(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_POLY_STIPPLE_PATTERN\n");
}

static void
handle_3dstate_line_stipple(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_LINE_STIPPLE\n");
}

static void
handle_3dstate_aa_line_parameters(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_AA_LINE_PARAMETERS\n");
}

static void
handle_3dstate_sampler_palette_load1(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLER_PALETTE_LOAD1\n");
}

static void
handle_3dstate_monofilter_size(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_MONOFILTER_SIZE\n");
}

static void
handle_3dstate_push_constant_alloc_vs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_PUSH_CONSTANT_ALLOC_VS\n");

	fill_curbe_alloc(&gt.vs.curbe, p);
}

static void
handle_3dstate_push_constant_alloc_hs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_PUSH_CONSTANT_ALLOC_HS\n");

	fill_curbe_alloc(&gt.hs.curbe, p);
}

static void
handle_3dstate_push_constant_alloc_ds(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_PUSH_CONSTANT_ALLOC_DS\n");

	fill_curbe_alloc(&gt.ds.curbe, p);
}

static void
handle_3dstate_push_constant_alloc_gs(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_PUSH_CONSTANT_ALLOC_GS\n");

	fill_curbe_alloc(&gt.gs.curbe, p);
}

static void
handle_3dstate_push_constant_alloc_ps(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_PUSH_CONSTANT_ALLOC_PS\n");

	fill_curbe_alloc(&gt.ps.curbe, p);
}

static void
handle_3dstate_so_decl_list(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SO_DECL_LIST\n");
}

static void
handle_3dstate_so_buffer(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SO_BUFFER\n");
}

static void
handle_3dstate_binding_table_pool_alloc(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_BINDING_TABLE_POOL_ALLOC\n");
}

static void
handle_3dstate_gather_pool_alloc(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_GATHER_POOL_ALLOC\n");
}

static void
handle_3dstate_sample_pattern(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DSTATE_SAMPLE_PATTERN\n");
}

static const command_handler_t nonpipelined_3dstate_commands[] = {
	[ 0] = handle_3dstate_drawing_rectangle,
	[ 2] = handle_3dstate_sampler_palette_load0,
	[ 4] = handle_3dstate_chroma_key,
	[ 6] = handle_3dstate_poly_stipple_offset,
	[ 7] = handle_3dstate_poly_stipple_pattern,
	[ 8] = handle_3dstate_line_stipple,
	[10] = handle_3dstate_aa_line_parameters,
	[12] = handle_3dstate_sampler_palette_load1,
	[17] = handle_3dstate_monofilter_size,
	[18] = handle_3dstate_push_constant_alloc_vs,
	[19] = handle_3dstate_push_constant_alloc_hs,
	[20] = handle_3dstate_push_constant_alloc_ds,
	[21] = handle_3dstate_push_constant_alloc_gs,
	[22] = handle_3dstate_push_constant_alloc_ps,
	[23] = handle_3dstate_so_decl_list,
	[24] = handle_3dstate_so_buffer,
	[25] = handle_3dstate_binding_table_pool_alloc,
	[26] = handle_3dstate_gather_pool_alloc,
	[28] = handle_3dstate_sample_pattern,
};

static void
handle_pipe_control(uint32_t *p)
{
	ksim_trace(TRACE_CS, "PIPE_CONTROL\n");
}

static void
handle_3dprimitive(uint32_t *p)
{
	ksim_trace(TRACE_CS, "3DPRIMITIVE\n");

	bool indirect = field(p[0], 10, 10);

	gt.prim.predicate = field(p[0], 8, 8);
	gt.prim.end_offset = field(p[1], 9, 9);
	gt.prim.access_type = field(p[1], 8, 8);

	if (!indirect) {
		gt.prim.vertex_count = p[2];
		gt.prim.start_vertex = p[3];
		gt.prim.instance_count = p[4];
		gt.prim.start_instance = p[5];
		gt.prim.base_vertex = p[6];
	}

	dispatch_primitive();
}

static command_handler_t
get_3dstate_command(uint32_t *p)
{
	uint32_t h = p[0];
	uint32_t opcode = field(h, 24, 26);
	uint32_t subopcode = field(h, 16, 23);
	command_handler_t handler;

	switch (opcode) {
	case 0:
		return pipelined_3dstate_commands[subopcode];
	case 1:
		return nonpipelined_3dstate_commands[subopcode];
	case 2:
		if (subopcode == 0)
			return handle_pipe_control;
		else
			return NULL;
	case 3:
		if (subopcode == 0)
			return handle_3dprimitive;
		else
			return NULL;
	default:
		return NULL;
	}
}

void
start_batch_buffer(uint64_t address)
{
	bool done = false;
	uint64_t range;
	uint32_t *base, *p;
	command_handler_t handler;

	gt.curbe_dynamic_state_base = true;
	base = map_gtt_offset(address, &range);

	p = base;
	while (!done) {
		uint32_t h = p[0];
		uint32_t type = field(h, 29, 31);
		uint32_t length;

		ksim_assert(p + 4 < base + range);

		switch (type) {
		case 0: /* MI */ {
			uint32_t opcode = field(h, 23, 28);
			handler = mi_commands[opcode];
			if (opcode == 10) /* bb end */
				done = true;
			if (opcode < 16)
				length = 1;
			else
				length = field(h, 0, 7) + 2;
			break;
		}

		case 1:
		case 2: /* ? */
			ksim_assert(!"unknown command type");
			break;

		case 3: /* Render */ {
			uint32_t subtype = field(h, 27, 28);
			switch (subtype) {
			case 0:
				handler = get_common_command(p);
				length = field(h, 0, 7) + 2;
				break;
			case 1:
				handler = get_dword_command(p);
				length = 1;
				break;
			case 2:
				ksim_assert(!"unknown render command subtype");
				length = 2;
				break;
			case 3:
				handler = get_3dstate_command(p);
				length = field(h, 0, 7) + 2;
				break;
			}
			break;
		}
		}

		ksim_assert(p + length < base + range);
		if (handler)
			handler(p);
		else
			unhandled_command(p);
		p += length;
	}
}

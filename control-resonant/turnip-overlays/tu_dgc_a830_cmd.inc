/* SPDX-License-Identifier: MIT
 * A8xx GPU-generated draw stream integration.
 * Compiled INSIDE tu_cmd_buffer.cc (it needs static draw/renderpass helpers).
 *
 * Only INDEX_BUFFER + 4 PUSH CONSTANT DWORDS + DRAW_INDEXED is implemented.
 * Do not advertise generic Vulkan DGC until all required DGC semantics
 * and stage/property requirements are implemented and validated.
 */
#include "tu_dgc_a830.h"

static bool
tu_dgc_a830_supported_layout(const struct tu_dgc_layout *layout)
{
   const struct vk_indirect_command_layout *l = &layout->vk;
   uint32_t required = BITFIELD_BIT(MESA_VK_DGC_IB) |
                       BITFIELD_BIT(MESA_VK_DGC_PC) |
                       BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED);
   return l->dgc_info == required && l->n_pc_layouts == 1 &&
          l->pc_layouts[0].size_B == 16 &&
          l->pc_layouts[0].dst_offset_B % 4 == 0 &&
          l->index_mode_is_dx && l->stride >= 52 &&
          l->stride <= 2048 &&
          l->index_src_offset_B <= l->stride - 16 &&
          l->pc_layouts[0].src_offset_B <= l->stride - 16 &&
          l->draw_src_offset_B <= l->stride - 20 &&
          !(l->usage & VK_INDIRECT_COMMANDS_LAYOUT_USAGE_UNORDERED_SEQUENCES_BIT_EXT);
}

template <chip CHIP>
static bool
tu_dgc_a830_set_pipeline_registers(struct tu_cmd_buffer *state_cmd,
                                    struct tu_dgc_a830_params *params,
                                    const struct tu_dgc_layout *layout)
{
   uint32_t root_word = layout->vk.pc_layouts[0].dst_offset_B / 4;
   const struct tu_push_constant_range *pc =
       &state_cmd->state.program.shared_consts;

   if (pc->type == IR3_PUSH_CONSTS_SHARED_PREAMBLE) {
      params->push_mode = 0;
      params->shared_constant_register = SP_SHARED_CONSTANT_GFX_REG(CHIP, root_word).reg;
   } else if (pc->type == IR3_PUSH_CONSTS_SHARED) {
      params->push_mode = 1;
      params->load_state6_opcode = CP_LOAD_STATE6;
      params->load_state6_control =
          CP_LOAD_STATE6_0_DST_OFF(root_word) |
          CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
          CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
          CP_LOAD_STATE6_0_STATE_BLOCK(SB6_UAV) |
          CP_LOAD_STATE6_0_NUM_UNIT(4);
   } else {
      return false;
   }

   uint32_t old_index_size = state_cmd->state.index_size;
   state_cmd->state.index_size = INDEX4_SIZE_16_BIT;
   params->draw_initiator_u16 = tu_draw_initiator(state_cmd, DI_SRC_SEL_DMA);
   state_cmd->state.index_size = INDEX4_SIZE_32_BIT;
   params->draw_initiator_u32 = tu_draw_initiator(state_cmd, DI_SRC_SEL_DMA);
   state_cmd->state.index_size = old_index_size;

   params->draw_packet_opcode = CP_DRAW_INDIRECT_MULTI;
   params->draw_opcode_and_vs_offset =
       A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDEXED) |
       A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(state_cmd));
   return true;
}

/* Record a compute dispatch that writes complete PM4 command words to
 * GPU-writeable command stream memory (not an indirect-buffer child).
 */
template <chip CHIP>
static VkResult
tu_dgc_a830_record_dispatch(struct tu_cmd_buffer *cmd,
                            const struct tu_dgc_a830_params *params)
{
   VkResult vr = tu_dgc_a830_init(cmd->device);
   if (vr != VK_SUCCESS)
      return vr;

   VK_FROM_HANDLE(tu_pipeline, pipeline, cmd->device->dgc_a830_pipeline);
   struct tu_shader *previous_shader = cmd->state.shaders[MESA_SHADER_COMPUTE];
   struct tu_draw_state previous_load = cmd->state.compute_load_state;
   uint32_t previous_push[ARRAY_SIZE(cmd->push_constants)];
   memcpy(previous_push, cmd->push_constants, sizeof(previous_push));

   cmd->state.shaders[MESA_SHADER_COMPUTE] =
       pipeline->shaders[MESA_SHADER_COMPUTE];
   cmd->state.compute_load_state = pipeline->load_state;
   tu_cs_emit_state_ib(&cmd->cs,
                       pipeline->shaders[MESA_SHADER_COMPUTE]->state);
   memcpy(cmd->push_constants, params, sizeof(*params));
   cmd->state.dirty |= TU_CMD_DIRTY_SHADER_CONSTS |
                       TU_CMD_DIRTY_COMPUTE_DESC_SETS;

   struct tu_dispatch_info info = {};
   info.blocks[0] = DIV_ROUND_UP(params->active_sequences, 64u);
   info.blocks[1] = 1;
   info.blocks[2] = 1;
   tu_dispatch<CHIP>(cmd, &info);

   /* Restore application compute state; the graphics pipeline remains bound. */
   memcpy(cmd->push_constants, previous_push, sizeof(previous_push));
   cmd->state.shaders[MESA_SHADER_COMPUTE] = previous_shader;
   cmd->state.compute_load_state = previous_load;
   if (previous_shader)
      tu_cs_emit_state_ib(&cmd->cs, previous_shader->state);
   cmd->state.dirty |= TU_CMD_DIRTY_SHADER_CONSTS |
                       TU_CMD_DIRTY_COMPUTE_DESC_SETS;

   /* Shader -> Command Processor synchronization.  Match the A8xx mesh
    * emulation barrier: CP_WAIT_MEM_WRITES by itself only orders CP writes;
    * compute L1/L2 storage writes must be CLEANED before CP can fetch PM4. */
   tu_cs_emit_wfi(&cmd->cs);
   tu_emit_event_write<CHIP>(cmd, &cmd->cs, FD_CACHE_CLEAN);
   tu_emit_event_write<CHIP>(cmd, &cmd->cs, FD_CACHE_INVALIDATE);
   tu_cs_emit_wfi(&cmd->cs);
   tu_cs_emit_pkt7(&cmd->cs, CP_WAIT_FOR_ME, 0);
   return tu_cs_get_status(&cmd->cs);
}

template <chip CHIP>
static void
tu_dgc_a830_record(struct tu_cmd_buffer *cmd,
                   struct tu_cmd_buffer *state_cmd,
                   const VkGeneratedCommandsInfoEXT *generated,
                   uint64_t output,
                   uint32_t start_seq, uint32_t count, bool preprocessed)
{
   VK_FROM_HANDLE(tu_dgc_layout, layout, generated->indirectCommandsLayout);
   if (!layout || !tu_dgc_a830_supported_layout(layout)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   struct tu_dgc_a830_params params = {};
   params.indirect_address = generated->indirectAddress;
   params.sequence_count_address = generated->sequenceCountAddress;
   params.output_pm4_address = output;
   params.maximum_count = generated->maxSequenceCount;
   params.indirect_stride = layout->vk.stride;
   params.index_token_offset = layout->vk.index_src_offset_B;
   params.constants_token_offset = layout->vk.pc_layouts[0].src_offset_B;
   params.draw_token_offset = layout->vk.draw_src_offset_B;
   params.first_sequence = start_seq;
   params.active_sequences = count;
   params.mode = preprocessed ? 1u : 0u;
   params.preprocess_source_address = generated->preprocessAddress;

   if (!preprocessed &&
       !tu_dgc_a830_set_pipeline_registers<CHIP>(state_cmd, &params, layout)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   VkResult result = tu_dgc_a830_record_dispatch<CHIP>(cmd, &params);
   if (result != VK_SUCCESS)
      vk_command_buffer_set_error(&cmd->vk, result);
}

/* A standalone mesh draw DGC token can use the A8xx native mesh
 * indirect path.  The mesh emulation's existing compute setup supports
 * dynamic sequence counts, so no graphics functionality is disabled.
 */
static bool
tu_dgc_a830_mesh_layout(const struct tu_dgc_layout *layout)
{
   const struct vk_indirect_command_layout *l = &layout->vk;
   return l->dgc_info == BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH) &&
          l->draw_src_offset_B <= l->stride &&
          l->stride - l->draw_src_offset_B >= 12u;
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdPreprocessGeneratedCommandsEXT(
    VkCommandBuffer commandBuffer,
    const VkGeneratedCommandsInfoEXT *generated,
    VkCommandBuffer stateCommandBuffer)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_cmd_buffer, state, stateCommandBuffer);
   if (!generated) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }
   VK_FROM_HANDLE(tu_dgc_layout, dgc_layout, generated->indirectCommandsLayout);
   if (dgc_layout && tu_dgc_a830_mesh_layout(dgc_layout))
      return; // Mesh path consumes the indirect stream without a PM4 intermediate.
   if (!generated->preprocessAddress ||
       generated->preprocessSize < (uint64_t)generated->maxSequenceCount * 80) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }
   for (uint32_t start=0; start<generated->maxSequenceCount;) {
      uint32_t n = MIN2(512u, generated->maxSequenceCount-start);
      tu_dgc_a830_record<CHIP>(cmd, state, generated,
                               generated->preprocessAddress + (uint64_t)start*80u,
                               start, n, false);
      if (tu_cs_get_status(&cmd->cs) != VK_SUCCESS) return;
      start += n;
   }
}
TU_GENX(tu_CmdPreprocessGeneratedCommandsEXT);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdExecuteGeneratedCommandsEXT(
    VkCommandBuffer commandBuffer,
    VkBool32 isPreprocessed,
    const VkGeneratedCommandsInfoEXT *generated)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_dgc_layout, layout, generated->indirectCommandsLayout);
   if (layout && tu_dgc_a830_mesh_layout(layout)) {
      struct tu_mesh_draw mesh = {
         .indirect = generated->indirectAddress + layout->vk.draw_src_offset_B,
         .count = generated->sequenceCountAddress,
         .draw_count = generated->maxSequenceCount,
         .stride = (uint32_t)layout->vk.stride,
      };
      if (mesh.draw_count)
         tu_mesh_draw<CHIP>(cmd, &mesh);
      return;
   }
   if (!layout || !tu_dgc_a830_supported_layout(layout) ||
       !cmd->state.pass) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   /* Use the same hardware indexed indirect path as vkCmdDrawIndexedIndirect,
    * so vertexOffset and firstInstance reach the driver's VS parameters.
    */
   tu6_emit_empty_vs_params<CHIP>(cmd);
   if (cmd->device->physical_device->info->props.indirect_draw_wfm_quirk)
      draw_wfm(cmd);
   tu6_draw_common<CHIP>(cmd, &cmd->draw_cs, true, 0);

   for (uint32_t start=0; start<generated->maxSequenceCount;) {
      uint32_t n = MIN2(512u, generated->maxSequenceCount - start);
      struct tu_cs *stream = &cmd->draw_cs;
      bool old_writable = stream->writeable;
      tu_cs_set_writeable(stream, true);
      VkResult result = tu_cs_reserve_space(stream, n*20u);
      if (result != VK_SUCCESS) {
         tu_cs_set_writeable(stream, old_writable);
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }
      uint64_t output = tu_cs_get_cur_iova(stream);
      for (uint32_t word=0; word<n*20u; ++word)
         tu_cs_emit(stream, pm4_pkt7_hdr(CP_NOP, 0));
      tu_cs_set_writeable(stream, old_writable);

      tu_dgc_a830_record<CHIP>(cmd, cmd, generated, output, start, n,
                                isPreprocessed != VK_FALSE);
      if (tu_cs_get_status(&cmd->cs) != VK_SUCCESS) return;
      start += n;
   }
   trace_end_draw(&cmd->rp_trace, &cmd->draw_cs);
}
TU_GENX(tu_CmdExecuteGeneratedCommandsEXT);

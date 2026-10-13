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

static bool
tu_dgc_a830_basic_draw_layout(const struct tu_dgc_layout *layout)
{
   const struct vk_indirect_command_layout *l=&layout->vk;
   const bool indexed=l->dgc_info == BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED);
   const bool basic=l->dgc_info == BITFIELD_BIT(MESA_VK_DGC_DRAW);
   if (!(indexed || basic) || l->stride>2048u) return false;
   const uint32_t size=indexed?20u:16u;
   return l->stride>=size && l->draw_src_offset_B<=l->stride-size;
}

template <chip CHIP>
static void
tu_dgc_a830_execute_basic_draw(struct tu_cmd_buffer *cmd,
                                const VkGeneratedCommandsInfoEXT *generated,
                                const struct tu_dgc_layout *layout)
{
   const bool indexed = layout->vk.dgc_info ==
                        BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED);
   struct tu_cs *cs=&cmd->draw_cs;
   tu6_emit_empty_vs_params<CHIP>(cmd);
   if (generated->sequenceCountAddress ||
       cmd->device->physical_device->info->props.indirect_draw_wfm_quirk)
      draw_wfm(cmd);
   tu6_draw_common<CHIP>(cmd, cs, indexed, 0u);

   const bool counted=generated->sequenceCountAddress != 0;
   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI,
                   indexed ? (counted ? 11u : 9u) : (counted ? 8u : 6u));
   tu_cs_emit(cs, tu_draw_initiator(cmd,
                  indexed ? DI_SRC_SEL_DMA : DI_SRC_SEL_AUTO_INDEX));
   const unsigned op=indexed
       ? (counted ? INDIRECT_OP_INDIRECT_COUNT_INDEXED : INDIRECT_OP_INDEXED)
       : (counted ? INDIRECT_OP_INDIRECT_COUNT : INDIRECT_OP_NORMAL);
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(op) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, generated->maxSequenceCount);
   if (indexed) {
      tu_cs_emit_qw(cs, cmd->state.index_va);
      tu_cs_emit(cs, cmd->state.max_index_count);
   }
   tu_cs_emit_qw(cs, generated->indirectAddress+
                        layout->vk.draw_src_offset_B);
   if (counted)
      tu_cs_emit_qw(cs, generated->sequenceCountAddress);
   tu_cs_emit(cs, (uint32_t)layout->vk.stride);
   trace_end_draw(&cmd->rp_trace, cs);
}

static bool
tu_dgc_a830_dispatch_layout(const struct tu_dgc_layout *layout)
{
   const struct vk_indirect_command_layout *l = &layout->vk;
   return l->dgc_info == BITFIELD_BIT(MESA_VK_DGC_DISPATCH) &&
          l->stride >= 12u && l->stride <= 2048u &&
          l->dispatch_src_offset_B <= l->stride - 12u;
}

template <chip CHIP>
static void
tu_dgc_a830_preprocess_dispatch(struct tu_cmd_buffer *cmd,
                                const VkGeneratedCommandsInfoEXT *generated,
                                const struct tu_dgc_layout *layout)
{
   if (!generated->preprocessAddress ||
       generated->preprocessSize < (uint64_t)generated->maxSequenceCount * 80u ||
       generated->maxSequenceCount > 4096u) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }
   for (uint32_t start=0;start<generated->maxSequenceCount;) {
      uint32_t n=MIN2(512u, generated->maxSequenceCount-start);
      struct tu_dgc_a830_params params = {};
      params.indirect_address=generated->indirectAddress;
      params.sequence_count_address=generated->sequenceCountAddress;
      params.output_pm4_address=generated->preprocessAddress+(uint64_t)start*80u;
      params.maximum_count=generated->maxSequenceCount;
      params.indirect_stride=layout->vk.stride;
      params.draw_token_offset=layout->vk.dispatch_src_offset_B;
      params.first_sequence=start;
      params.active_sequences=n;
      params.mode=2u;
      VkResult err=tu_dgc_a830_record_dispatch<CHIP>(cmd,&params);
      if(err!=VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd->vk,err);
         return;
      }
      start+=n;
   }
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
   if (dgc_layout && (tu_dgc_a830_mesh_layout(dgc_layout) ||
                      tu_dgc_a830_basic_draw_layout(dgc_layout)))
      return; // Native indirect paths need no PM4 preprocessing.
   if (dgc_layout && tu_dgc_a830_dispatch_layout(dgc_layout)) {
      tu_dgc_a830_preprocess_dispatch<CHIP>(cmd,generated,dgc_layout);
      return;
   }
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
   if (layout && tu_dgc_a830_basic_draw_layout(layout)) {
      tu_dgc_a830_execute_basic_draw<CHIP>(cmd,generated,layout);
      return;
   }
   if (layout && tu_dgc_a830_dispatch_layout(layout)) {
      if (!isPreprocessed)
         tu_dgc_a830_preprocess_dispatch<CHIP>(cmd,generated,layout);
      if (tu_cs_get_status(&cmd->cs) != VK_SUCCESS)
         return;
      for (uint32_t i=0;i<generated->maxSequenceCount;++i) {
         struct tu_dispatch_info d = {};
         d.indirect=generated->preprocessAddress+(uint64_t)i*80u;
         tu_dispatch<CHIP>(cmd,&d);
      }
      return;
   }
   if (layout && tu_dgc_a830_mesh_layout(layout)) {
      /* Native VK_EXT_mesh_shader code takes VkBuffer handles, while
       * Vulkan DGC provides raw device addresses.  The temporary wrappers
       * are used only during command recording, never retained by the GPU.
       */
      if (!generated->maxSequenceCount)
         return;
      struct tu_buffer indirect_buffer = {};
      indirect_buffer.vk.device_address = generated->indirectAddress;
      indirect_buffer.vk.size = generated->indirectAddressSize;
      VkBuffer ib = tu_buffer_to_handle(&indirect_buffer);
      auto vk = &cmd->device->vk.dispatch_table;
      if (generated->sequenceCountAddress) {
         struct tu_buffer count_buffer = {};
         count_buffer.vk.device_address = generated->sequenceCountAddress;
         count_buffer.vk.size = sizeof(uint32_t);
         vk->CmdDrawMeshTasksIndirectCountEXT(commandBuffer, ib,
             layout->vk.draw_src_offset_B,
             tu_buffer_to_handle(&count_buffer), 0,
             generated->maxSequenceCount, (uint32_t)layout->vk.stride);
      } else {
         vk->CmdDrawMeshTasksIndirectEXT(commandBuffer, ib,
             layout->vk.draw_src_offset_B,
             generated->maxSequenceCount, (uint32_t)layout->vk.stride);
      }
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

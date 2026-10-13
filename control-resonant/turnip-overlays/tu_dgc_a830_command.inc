/* SPDX-License-Identifier: MIT
 * Experimental Turnip DGC A8xx implementation. Included after tu6_draw_common
 * and before tu_CmdDrawIndexedIndirect so it can reuse native draw state
 * emission, visibility and VS parameter helpers.
 */
template <chip CHIP>
static bool
tu_dgc_a830_record_preprocess(struct tu_cmd_buffer *cmd,
                               const VkGeneratedCommandsInfoEXT *info,
                               struct tu_cmd_buffer *state_cmd)
{
   VK_FROM_HANDLE(tu_dgc_layout, layout, info->indirectCommandsLayout);
   if (CHIP != A8XX || !layout || info->maxSequenceCount > UINT32_MAX/80u) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return false;
   }
   const auto *l = &layout->vk;
   const uint32_t needed = BITFIELD_BIT(MESA_VK_DGC_IB) |
                           BITFIELD_BIT(MESA_VK_DGC_PC) |
                           BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED);
   if (l->dgc_info != needed || l->n_pc_layouts != 1 ||
       l->pc_layouts[0].size_B != 16 || !l->index_mode_is_dx ||
       l->stride < 52 || l->stride > 2048) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return false;
   }

   if (info->preprocessSize < (uint64_t)info->maxSequenceCount * 80) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return false;
   }

   const auto &constants = state_cmd->state.program.shared_consts;
   const uint32_t offset_dw = l->pc_layouts[0].dst_offset_B / 4;
   if (offset_dw < constants.lo_dwords ||
       offset_dw + 4 > constants.lo_dwords + constants.dwords ||
       (constants.type != IR3_PUSH_CONSTS_SHARED &&
        constants.type != IR3_PUSH_CONSTS_SHARED_PREAMBLE)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return false;
   }

   tu_dgc_a830_params p = {};
   p.indirect_address = info->indirectAddress;
   p.output_pm4_address = info->preprocessAddress;
   p.sequence_count_address = info->sequenceCountAddress;
   p.maximum_count = info->maxSequenceCount;
   p.indirect_stride = l->stride;
   p.index_token_offset = l->index_src_offset_B;
   p.constants_token_offset = l->pc_layouts[0].src_offset_B;
   p.draw_token_offset = l->draw_src_offset_B;
   p.shared_constant_register = SP_SHARED_CONSTANT_GFX_REG(CHIP, offset_dw).reg;
   p.push_mode = constants.type == IR3_PUSH_CONSTS_SHARED ? 1u : 0u;
   p.load_state6_opcode = CP_LOAD_STATE6;
   p.load_state6_control =
       CP_LOAD_STATE6_0_DST_OFF(offset_dw) |
       CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
       CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
       CP_LOAD_STATE6_0_STATE_BLOCK(SB6_UAV) |
       CP_LOAD_STATE6_0_NUM_UNIT(4);

   // A shader can only use the bound GPU pipeline's program state to
   // determine primitive topology and VS driver parameter offsets.
   uint32_t initiator = tu_draw_initiator(state_cmd, DI_SRC_SEL_DMA);
   initiator &= ~CP_DRAW_INDX_OFFSET_0_INDEX_SIZE(3);
   p.draw_initiator_u16 = initiator |
       CP_DRAW_INDX_OFFSET_0_INDEX_SIZE(INDEX4_SIZE_16_BIT);
   p.draw_initiator_u32 = initiator |
       CP_DRAW_INDX_OFFSET_0_INDEX_SIZE(INDEX4_SIZE_32_BIT);
   p.draw_packet_opcode = CP_DRAW_INDIRECT_MULTI;
   p.draw_opcode_and_vs_offset =
       A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDEXED) |
       A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(state_cmd));

   tu_dgc_a830_generate(cmd, &p);
   return tu_cs_get_status(&cmd->cs) == VK_SUCCESS;
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer,
                                     const VkGeneratedCommandsInfoEXT *info,
                                     VkCommandBuffer stateCommandBuffer)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_cmd_buffer, state_cmd, stateCommandBuffer);
   tu_dgc_a830_record_preprocess<CHIP>(cmd, info, state_cmd);
}
TU_GENX(tu_CmdPreprocessGeneratedCommandsEXT);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer,
                                  VkBool32 isPreprocessed,
                                  const VkGeneratedCommandsInfoEXT *info)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   if (!isPreprocessed &&
       !tu_dgc_a830_record_preprocess<CHIP>(cmd, info, cmd))
      return;

   // Reuse the same state setup as a native indexed indirect draw.
   // The generated IB contains an ordered sequence of CP_LOAD_STATE6 / PM4
   // push constants followed by CP_DRAW_INDIRECT_MULTI.
   tu6_emit_empty_vs_params<CHIP>(cmd);
   if (cmd->device->physical_device->info->props.indirect_draw_wfm_quirk)
      draw_wfm(cmd);
   tu6_draw_common<CHIP>(cmd, &cmd->draw_cs, true, 0);

   constexpr uint32_t chunk = 4096;
   for (uint32_t i=0; i < info->maxSequenceCount; i+=chunk) {
      const uint32_t n = MIN2(chunk, info->maxSequenceCount-i);
      tu_cs_emit_pkt7(&cmd->draw_cs, CP_INDIRECT_BUFFER, 3);
      tu_cs_emit_qw(&cmd->draw_cs, info->preprocessAddress +
                   (uint64_t)i * 80u);
      tu_cs_emit(&cmd->draw_cs, n * 20u);
   }
   trace_end_draw(&cmd->rp_trace, &cmd->draw_cs);
}
TU_GENX(tu_CmdExecuteGeneratedCommandsEXT);

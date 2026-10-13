/* SPDX-License-Identifier: MIT
 * Work-in-progress Vulkan EXT DGC layout support for A8xx Turnip.
 */
#ifndef TU_DGC_A830_H
#define TU_DGC_A830_H
#include "vk_device_generated_commands.h"
#include "tu_device.h"

struct tu_dgc_layout {
   struct vk_indirect_command_layout vk;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_dgc_layout, vk.base,
                               VkIndirectCommandsLayoutEXT,
                               VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT)
struct tu_dgc_a830_params {
   uint64_t indirect_address;
   uint64_t output_pm4_address;
   uint64_t sequence_count_address;
   uint32_t maximum_count;
   uint32_t indirect_stride;
   uint32_t index_token_offset;
   uint32_t constants_token_offset;
   uint32_t draw_token_offset;
   uint32_t shared_constant_register;
   uint32_t vertex_offset_register;
   uint32_t draw_initiator_u16;
   uint32_t draw_initiator_u32;
   uint32_t push_mode;
   uint32_t load_state6_opcode;
   uint32_t load_state6_control;
   uint32_t draw_packet_opcode;
   uint32_t draw_opcode_and_vs_offset;
};
static_assert(sizeof(tu_dgc_a830_params) == 80, "scalar GLSL push layout");

VkResult tu_dgc_a830_init(struct tu_device *device);
void tu_dgc_a830_finish(struct tu_device *device);
#endif

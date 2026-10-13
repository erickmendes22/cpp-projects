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
#endif

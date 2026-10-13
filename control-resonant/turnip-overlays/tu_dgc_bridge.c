/* SPDX-License-Identifier: MIT
 * C-linkage trampoline for Mesa's vk_indirect_command_layout helpers.
 * Turnip compiles its Vulkan implementation as C++ while vk runtime is C.
 */
#include "vk_device_generated_commands.h"
#include "vk_device.h"

void *
tu_dgc_bridge_create(struct vk_device *device,
                     const VkIndirectCommandsLayoutCreateInfoEXT *ci,
                     const VkAllocationCallbacks *alloc, size_t size)
{
   return vk_indirect_command_layout_create(device, ci, alloc, size);
}

void
tu_dgc_bridge_destroy(struct vk_device *device,
                      const VkAllocationCallbacks *alloc,
                      struct vk_indirect_command_layout *layout)
{
   vk_indirect_command_layout_destroy(device, alloc, layout);
}

/* SPDX-License-Identifier: MIT
 * A8xx DGC work-in-progress: Vulkan command layout object and requirements.
 * Do NOT advertise VK_EXT_device_generated_commands until the GPU dispatcher
 * implementation is complete.
 */
#include "tu_dgc_a830.h"
#include "tu_common.h"
#include "vk_alloc.h"
#include "vk_device.h"
#include "vk_device_generated_commands.h"
#include "vk_util.h"

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateIndirectCommandsLayoutEXT(VkDevice _device,
                                   const VkIndirectCommandsLayoutCreateInfoEXT *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkIndirectCommandsLayoutEXT *pLayout)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_dgc_layout *layout = (struct tu_dgc_layout *)
      vk_indirect_command_layout_create(&device->vk, pCreateInfo,
                                        pAllocator, sizeof(*layout));
   if (!layout)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   *pLayout = tu_dgc_layout_to_handle(layout);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyIndirectCommandsLayoutEXT(VkDevice _device,
                                    VkIndirectCommandsLayoutEXT _layout,
                                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_dgc_layout, layout, _layout);
   if (layout)
      vk_indirect_command_layout_destroy(&device->vk, pAllocator, &layout->vk);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetGeneratedCommandsMemoryRequirementsEXT(
    VkDevice _device,
    const VkGeneratedCommandsMemoryRequirementsInfoEXT *pInfo,
    VkMemoryRequirements2 *pRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_dgc_layout, layout, pInfo->indirectCommandsLayout);
   /* Reserved PM4 stream for fixed-layout indexed draws: 64 B per command.
    * The future dispatcher must not emit more words than this.
    */
   const uint64_t cmd_size = (uint64_t)pInfo->maxSequenceCount * 64u;
   const uint32_t count = device->physical_device->memory.type_count;
   pRequirements->memoryRequirements.size = align64(cmd_size, 256u);
   pRequirements->memoryRequirements.alignment = 256u;
   pRequirements->memoryRequirements.memoryTypeBits =
      count >= 32 ? UINT32_MAX : ((1u << count) - 1u);
   (void)layout;
}

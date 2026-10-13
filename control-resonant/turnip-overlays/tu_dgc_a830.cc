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

extern "C" {
void *tu_dgc_bridge_create(struct vk_device *,
                          const VkIndirectCommandsLayoutCreateInfoEXT *,
                          const VkAllocationCallbacks *, size_t);
void tu_dgc_bridge_destroy(struct vk_device *,
                           const VkAllocationCallbacks *,
                           struct vk_indirect_command_layout *);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateIndirectCommandsLayoutEXT(VkDevice _device,
                                   const VkIndirectCommandsLayoutCreateInfoEXT *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkIndirectCommandsLayoutEXT *pLayout)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_dgc_layout *layout = (struct tu_dgc_layout *)
      tu_dgc_bridge_create(&device->vk, pCreateInfo,
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
      tu_dgc_bridge_destroy(&device->vk, pAllocator, &layout->vk);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetGeneratedCommandsMemoryRequirementsEXT(
    VkDevice _device,
    const VkGeneratedCommandsMemoryRequirementsInfoEXT *pInfo,
    VkMemoryRequirements2 *pRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_dgc_layout, layout, pInfo->indirectCommandsLayout);
   /* Reserved PM4 stream for fixed-layout indexed draws: 80 B per command.
    * The future dispatcher must not emit more words than this.
    */
   const uint64_t cmd_size = (uint64_t)pInfo->maxSequenceCount * 80u;
   const uint32_t count = device->physical_device->memory.type_count;
   pRequirements->memoryRequirements.size = align64(cmd_size, 256u);
   pRequirements->memoryRequirements.alignment = 256u;
   pRequirements->memoryRequirements.memoryTypeBits =
      count >= 32 ? UINT32_MAX : ((1u << count) - 1u);
   (void)layout;
}

/* Compiled from dgc_pm4_layout.comp in the Android builder. */
#include "tu_dgc_a830_spv.h"

VkResult
tu_dgc_a830_init(struct tu_device *device)
{
   if (device->dgc_a830_pipeline)
      return VK_SUCCESS;

   mtx_lock(&device->mutex);
   if (device->dgc_a830_pipeline) {
      mtx_unlock(&device->mutex);
      return VK_SUCCESS;
   }

   VkDevice vdev = tu_device_to_handle(device);
   const struct vk_device_dispatch_table *vk = &device->vk.dispatch_table;

   VkShaderModule module = VK_NULL_HANDLE;
   VkPipelineLayout layout = VK_NULL_HANDLE;
   VkPipeline pipeline = VK_NULL_HANDLE;
   VkPushConstantRange range = {};
   VkPipelineLayoutCreateInfo layout_info = {};
   VkComputePipelineCreateInfo pipeline_info = {};

   VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(tu_dgc_a830_spv),
      .pCode = tu_dgc_a830_spv,
   };
   VkResult result = vk->CreateShaderModule(vdev, &module_info, nullptr, &module);
   if (result != VK_SUCCESS)
      goto done;

   range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
   range.offset = 0;
   range.size = sizeof(struct tu_dgc_a830_params);
   layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   layout_info.pushConstantRangeCount = 1;
   layout_info.pPushConstantRanges = &range;
   result = vk->CreatePipelineLayout(vdev, &layout_info, nullptr, &layout);
   if (result != VK_SUCCESS)
      goto done;

   pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
   pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
   pipeline_info.stage.module = module;
   pipeline_info.stage.pName = "main";
   pipeline_info.layout = layout;
   result = vk->CreateComputePipelines(vdev, VK_NULL_HANDLE, 1,
                                       &pipeline_info, nullptr, &pipeline);
   if (result == VK_SUCCESS) {
      device->dgc_a830_pipeline = pipeline;
      device->dgc_a830_pipeline_layout = layout;
   }

done:
   if (module)
      vk->DestroyShaderModule(vdev, module, nullptr);
   if (result != VK_SUCCESS && layout)
      vk->DestroyPipelineLayout(vdev, layout, nullptr);
   mtx_unlock(&device->mutex);
   return result;
}

void
tu_dgc_a830_finish(struct tu_device *device)
{
   VkDevice vdev = tu_device_to_handle(device);
   const struct vk_device_dispatch_table *vk = &device->vk.dispatch_table;
   if (device->dgc_a830_pipeline)
      vk->DestroyPipeline(vdev, device->dgc_a830_pipeline, nullptr);
   if (device->dgc_a830_pipeline_layout)
      vk->DestroyPipelineLayout(vdev, device->dgc_a830_pipeline_layout, nullptr);
   device->dgc_a830_pipeline = VK_NULL_HANDLE;
   device->dgc_a830_pipeline_layout = VK_NULL_HANDLE;
}

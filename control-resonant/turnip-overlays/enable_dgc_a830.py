#!/usr/bin/env python3
"""Expose the experimental A8xx Vulkan DGC subset after runtime implementation is built.

This is not Vulkan-conformant DGC for all token combinations.  Keep this in
an isolated experimental driver; do not upstream/ship as generic Turnip.
"""
from pathlib import Path

p = Path("mesa/src/freedreno/vulkan/tu_device.cc")
text = p.read_text()

def once(needle: str, repl: str):
    global text
    count = text.count(needle)
    if count != 1:
        raise RuntimeError(f"Expected one occurrence: {needle[:70]} (found {count})")
    text = text.replace(needle, repl, 1)

once(
    "      .EXT_descriptor_buffer = true,",
    "      .EXT_descriptor_buffer = true,\n"
    "      .EXT_device_generated_commands = fd_dev_gen(&device->dev_id) == 8,"
)
once(
    "   /* VK_EXT_device_address_binding_report */",
    "   /* Experimental VK_EXT_device_generated_commands on A8xx. */\n"
    "   features->deviceGeneratedCommands = fd_dev_gen(&pdevice->dev_id) == 8;\n\n"
    "   /* VK_EXT_device_address_binding_report */"
)
once(
    "   props->maxDrawIndirectCount = UINT32_MAX;",
    """   props->maxDrawIndirectCount = UINT32_MAX;

   /* Experimental A8xx DGC: fixed graphics PSO, indexed indirect draw,
    * per-sequence INDEX_BUFFER + 4 DWORD graphics push constants.
    * Token combinations not supported by the driver return an error.
    * This is a Winlator compatibility experiment, not Vulkan CTS coverage.
    */
   if (fd_dev_gen(&pdevice->dev_id) == 8) {
      props->maxIndirectPipelineCount = 1;
      props->maxIndirectShaderObjectCount = 1;
      props->maxIndirectSequenceCount = 1048576;
      props->maxIndirectCommandsTokenCount = 3;
      props->maxIndirectCommandsTokenOffset = 2047;
      props->maxIndirectCommandsIndirectStride = 2048;
      props->supportedIndirectCommandsInputModes =
         VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT;
      props->supportedIndirectCommandsShaderStages =
         VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
      if (pdevice->vk.supported_features.meshShader)
         props->supportedIndirectCommandsShaderStages |=
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;
      props->supportedIndirectCommandsShaderStagesPipelineBinding = 0;
      props->supportedIndirectCommandsShaderStagesShaderBinding = 0;
      props->deviceGeneratedCommandsTransformFeedback = false;
      props->deviceGeneratedCommandsMultiDrawIndirectCount = false;
   }
"""
)
p.write_text(text)
print("Applied experimental A8xx Vulkan DGC extension, feature and limits")

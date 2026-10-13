#!/usr/bin/env python3
"""Enable the *experimental* A830 DGC path after integrating implementation.

This is not a generic conformance-certified Vulkan DGC implementation.
Keep original Turnip available and do not publish as a driver fix until
runtime and graphics validation pass.
"""
from pathlib import Path

p = Path("mesa/src/freedreno/vulkan/tu_device.cc")
s = p.read_text()

def insert(anchor, addition):
    global s
    if s.count(anchor) != 1:
        raise RuntimeError(f"Expected exactly one anchor: {anchor}")
    s = s.replace(anchor, anchor + addition, 1)

insert("      .EXT_descriptor_buffer = true,\n",
       "      .EXT_device_generated_commands = fd_dev_gen(&device->dev_id) == 8,\n")

insert("   features->descriptorBufferPushDescriptors = true;\n",
       "\n   /* Experimental GPU-translated DGC for A830 Vulkan/Winlator. */\n"
       "   features->deviceGeneratedCommands =\n"
       "       fd_dev_gen(&pdevice->dev_id) == 8;\n"
       "   features->dynamicGeneratedPipelineLayout = false;\n")

insert("   props->maxPushConstantsSize = MAX_PUSH_CONSTANTS_SIZE;\n",
       """
   /* Experimental DGC token layouts: index + 4 push constants + draw,
    * indirect mesh tasks, indirect dispatch, or basic indirect draw. */
   props->maxIndirectPipelineCount = 1;
   props->maxIndirectShaderObjectCount = 1;
   props->maxIndirectSequenceCount = 4096;
   props->maxIndirectCommandsTokenCount = 3;
   props->maxIndirectCommandsTokenOffset = 2047;
   props->maxIndirectCommandsIndirectStride = 2048;
   props->supportedIndirectCommandsInputModes =
       VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT;
   props->supportedIndirectCommandsShaderStages =
       VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT |
       VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;
   props->supportedIndirectCommandsShaderStagesPipelineBinding = 0;
   props->supportedIndirectCommandsShaderStagesShaderBinding = 0;
   props->deviceGeneratedCommandsTransformFeedback = false;
   props->deviceGeneratedCommandsMultiDrawIndirectCount = false;
""")
p.write_text(s)

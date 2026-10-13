// SPDX-License-Identifier: MIT
// Runtime smoke test of the EXACT GPU PM4 generator used by experimental A830.
// Runs on Mesa Lavapipe Vulkan (does not claim Adreno hardware validation).
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(VkResult code, const char *where) {
    if (code != VK_SUCCESS)
        throw std::runtime_error(std::string(where) + ": " + std::to_string(code));
}
static void require(bool v, const char *what) {
    if (!v) throw std::runtime_error(what);
}
static void w32(uint8_t *p, uint32_t v) { std::memcpy(p, &v, 4); }
static void w64(uint8_t *p, uint64_t v) { std::memcpy(p, &v, 8); }
static uint32_t r32(const uint8_t *p) { uint32_t v; std::memcpy(&v,p,4); return v; }

struct alignas(8) Parameters {
    uint64_t indirect_address, output_pm4_address, sequence_count_address;
    uint32_t maximum_count, indirect_stride, index_token_offset, constants_token_offset;
    uint32_t draw_token_offset, shared_constant_register, unused_vertex_offset_register;
    uint32_t draw_initiator_u16, draw_initiator_u32, push_mode;
    uint32_t load_state6_opcode, load_state6_control, draw_packet_opcode;
    uint32_t draw_opcode_and_vs_offset;
    uint64_t preprocess_source_address;
    uint32_t first_sequence, active_sequences, mode;
};
static_assert(sizeof(Parameters) == 104);

static uint32_t parity(uint32_t x) {
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4;
    return ((~0x6996u) >> (x & 15u)) & 1u;
}
static uint32_t pkt4(uint32_t reg, uint32_t n) {
    return 0x40000000u | n | (parity(n)<<7) |
           ((reg & 0x3ffffu)<<8) | (parity(reg)<<27);
}
static uint32_t pkt7(uint32_t op, uint32_t n) {
    return 0x70000000u | n | (parity(n)<<15) |
           ((op & 0x7fu)<<16) | (parity(op)<<23);
}

int main(int argc, char **argv) try {
    require(argc==2, "usage: gpu_dgc_shader_smoke <shader.spv>");
    std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
    require(bool(file), "SPIR-V input not found");
    auto bytes=file.tellg();
    require(bytes>0 && (bytes%4)==0, "invalid SPIR-V size");
    std::vector<uint32_t> spirv(size_t(bytes)/4u);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(spirv.data()), bytes);
    require(bool(file), "failed to read SPIR-V");

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName="CONTROL DGC shader smoke test";
    app.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo=&app;
    VkInstance instance=VK_NULL_HANDLE;
    check(vkCreateInstance(&ii,nullptr,&instance),"instance");
    uint32_t count=0;
    check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"enumerate count");
    require(count, "no Vulkan device (Lavapipe unavailable)");
    std::vector<VkPhysicalDevice> physicals(count);
    check(vkEnumeratePhysicalDevices(instance,&count,physicals.data()),"enumerate");
    VkPhysicalDevice physical=physicals[0];
    uint32_t families=0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,nullptr);
    std::vector<VkQueueFamilyProperties> props(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,props.data());
    uint32_t computeFamily=UINT32_MAX;
    for (uint32_t i=0;i<families;i++)
        if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            computeFamily=i; break;
        }
    require(computeFamily!=UINT32_MAX,"no compute queue");

    VkPhysicalDeviceVulkan12Features support12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 support{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    support.pNext=&support12;
    vkGetPhysicalDeviceFeatures2(physical,&support);
    require(support12.bufferDeviceAddress && support12.scalarBlockLayout &&
            support.features.shaderInt64,
            "Vulkan device lacks BDA/scalarBlockLayout/shaderInt64");

    VkPhysicalDeviceVulkan12Features required12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    required12.bufferDeviceAddress=VK_TRUE;
    required12.scalarBlockLayout=VK_TRUE;
    float priority=1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex=computeFamily;
    qi.queueCount=1;
    qi.pQueuePriorities=&priority;
    VkPhysicalDeviceFeatures reqFeatures{};
    reqFeatures.shaderInt64=VK_TRUE;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext=&required12;
    di.queueCreateInfoCount=1;
    di.pQueueCreateInfos=&qi;
    di.pEnabledFeatures=&reqFeatures;
    VkDevice device=VK_NULL_HANDLE;
    check(vkCreateDevice(physical,&di,nullptr,&device),"create device");
    VkQueue queue=VK_NULL_HANDLE;
    vkGetDeviceQueue(device,computeFamily,0,&queue);

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size=4096;
    bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBuffer buffer=VK_NULL_HANDLE;
    check(vkCreateBuffer(device,&bi,nullptr,&buffer),"create buffer");
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(device,buffer,&mr);
    VkPhysicalDeviceMemoryProperties memprops{};
    vkGetPhysicalDeviceMemoryProperties(physical,&memprops);
    uint32_t memoryType=UINT32_MAX;
    for(uint32_t i=0;i<memprops.memoryTypeCount;i++)
        if ((mr.memoryTypeBits & (1u<<i)) &&
            (memprops.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memoryType=i;break;
        }
    require(memoryType!=UINT32_MAX, "need host-coherent Vulkan memory");
    VkMemoryAllocateFlagsInfo maFlags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    maFlags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.pNext=&maFlags;
    mai.allocationSize=mr.size;
    mai.memoryTypeIndex=memoryType;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    check(vkAllocateMemory(device,&mai,nullptr,&memory),"allocate BDA memory");
    check(vkBindBufferMemory(device,buffer,memory,0),"bind buffer");
    VkBufferDeviceAddressInfo bai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    bai.buffer=buffer;
    uint64_t gpuaddr=vkGetBufferDeviceAddress(device,&bai);
    require(gpuaddr!=0,"BDA is zero");
    void *mapped=nullptr;
    check(vkMapMemory(device,memory,0,4096,0,&mapped),"map memory");
    uint8_t *data=static_cast<uint8_t*>(mapped);
    std::memset(data,0,4096);
    // Fixed 52B layout: DXGI IndexView(16) + push constants(16) + DrawIndexed(20).
    w64(data,0x0000007f00100000ull);
    w32(data+8,800);
    w32(data+12,57);   // DXGI R16_UINT
    for(int i=0;i<4;i++)w32(data+16+4*i,0xabcde000u+i);
    w32(data+32,300); w32(data+36,2); w32(data+40,13);
    w32(data+44,uint32_t(-9)); w32(data+48,7);
    w32(data+128,1);    // GPU culling count=1: second sequence must be NOP

    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize=size_t(bytes);
    smci.pCode=spirv.data();
    VkShaderModule shader=VK_NULL_HANDLE;
    check(vkCreateShaderModule(device,&smci,nullptr,&shader),"shader module");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Parameters)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.pushConstantRangeCount=1;
    plci.pPushConstantRanges=&range;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    check(vkCreatePipelineLayout(device,&plci,nullptr,&layout),"pipeline layout");
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module=shader;
    cpci.stage.pName="main";
    cpci.layout=layout;
    VkPipeline pipeline=VK_NULL_HANDLE;
    check(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cpci,nullptr,&pipeline),
          "compute pipeline");

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex=computeFamily;
    VkCommandPool pool=VK_NULL_HANDLE;
    check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool),"create pool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool=pool;
    alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount=1;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device,&alloc,&cmd),"alloc CB");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(cmd,&begin),"begin CB");
    Parameters p{};
    p.indirect_address=gpuaddr;
    p.sequence_count_address=gpuaddr+128;
    p.output_pm4_address=gpuaddr+256;
    p.maximum_count=2;
    p.indirect_stride=52;
    p.index_token_offset=0;
    p.constants_token_offset=16;
    p.draw_token_offset=32;
    p.shared_constant_register=0x8100;
    p.draw_initiator_u16=0x1234;
    p.draw_initiator_u32=0x4321;
    p.draw_packet_opcode=0x2a;
    p.draw_opcode_and_vs_offset=0xdeadbeefu;
    p.first_sequence=0;
    p.active_sequences=2;
    p.push_mode=0;
    p.mode=0;
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);
    vkCmdDispatch(cmd,1,1,1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    check(vkEndCommandBuffer(cmd),"end CB");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount=1;
    submit.pCommandBuffers=&cmd;
    check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE),"queue submit");
    check(vkQueueWaitIdle(queue),"queue wait");

    const uint8_t *out=data+256;
    require(r32(out)==pkt4(0x8100,4),"wrong constant register packet");
    for(int i=0;i<4;i++)
        require(r32(out+4*(i+1))==0xabcde000u+uint32_t(i),
                "wrong object constant");
    require(r32(out+5*4)==pkt7(0x2a,9),"wrong indexed indirect opcode");
    require(r32(out+6*4)==0x1234,"wrong 16-bit index initiator");
    require(r32(out+7*4)==0xdeadbeefu,"wrong VS driver settings");
    require(r32(out+8*4)==1,"wrong valid sequence count");
    require(r32(out+9*4)==0x00100000u,"wrong index buffer address");
    require(r32(out+10*4)==0x7fu,"wrong index buffer upper address");
    require(r32(out+11*4)==400u,"wrong index buffer max count");
    require(r32(out+14*4)==20u,"wrong draw stride");
    require(r32(out+15*4)==pkt7(0x10,4),"wrong PM4 record padding");
    require(r32(out+80)==pkt7(0x10,19),"invalid culled sequence NOP");
    for(int i=1;i<20;i++)require(r32(out+80+4*i)==0,"invalid culled sequence data");

    // Second dispatch: explicit DGC preprocessing must copy the exact record.
    p.mode=1;
    p.preprocess_source_address=gpuaddr+256;
    p.output_pm4_address=gpuaddr+512;
    check(vkResetCommandPool(device,pool,0),"reset CB pool");
    check(vkBeginCommandBuffer(cmd,&begin),"begin copy CB");
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);
    vkCmdDispatch(cmd,1,1,1);
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    check(vkEndCommandBuffer(cmd),"end copy CB");
    check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE),"copy submit");
    check(vkQueueWaitIdle(queue),"copy wait");
    require(std::memcmp(data+256,data+512,160)==0,
            "preprocessed GPU PM4 copy mismatch");

    vkDestroyCommandPool(device,pool,nullptr);
    vkDestroyPipeline(device,pipeline,nullptr);
    vkDestroyPipelineLayout(device,layout,nullptr);
    vkDestroyShaderModule(device,shader,nullptr);
    vkUnmapMemory(device,memory);
    vkDestroyBuffer(device,buffer,nullptr);
    vkFreeMemory(device,memory,nullptr);
    vkDestroyDevice(device,nullptr);
    vkDestroyInstance(instance,nullptr);
    std::cout << "PASS: executed DGC shader on Vulkan; PM4 words, count/NOP, constants, preprocessed copy\\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << "FAIL: Vulkan DGC GPU smoke test: " << e.what() << "\\n";
    return 1;
}

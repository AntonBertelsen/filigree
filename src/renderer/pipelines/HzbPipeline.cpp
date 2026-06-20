#include "renderer/pipelines/HzbPipeline.hpp"
#include <iostream>
#include <stdexcept>
#include <array>

HzbPipeline::HzbPipeline(VulkanContext& context) : context(context) {
    createPipeline();
    createDescriptorPoolAndSets();
}

HzbPipeline::~HzbPipeline() {
    cleanup();
}

void HzbPipeline::recordDispatch(VkCommandBuffer cb, uint32_t frameIndex, uint32_t level, int32_t srcWidth, int32_t srcHeight, int32_t srcLevel) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);

    VkDescriptorSet ds = descriptorSets[frameIndex][level];
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &ds, 0, nullptr);

    HzbPushConstants pcs{};
    pcs.srcSize = glm::ivec2(srcWidth, srcHeight);
    pcs.srcLevel = srcLevel;
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HzbPushConstants), &pcs);

    // Calculate destination size
    uint32_t dstWidth = std::max(1u, 1024u >> level);
    uint32_t dstHeight = std::max(1u, 1024u >> level);

    uint32_t groupCountX = (dstWidth + 15) / 16;
    uint32_t groupCountY = (dstHeight + 15) / 16;

    vkCmdDispatch(cb, groupCountX, groupCountY, 1);
}

void HzbPipeline::createPipeline() {
    VkDevice device = context.getDevice();

    // 1. Create Descriptor Set Layout
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    
    // Binding 0: Input VisBuffer SSBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Input HZB Image (Storage Image, Read-Only)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Binding 2: Output HZB Image (Storage Image, Write-Only)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB descriptor set layout!");
    }

    // 2. Create Pipeline Layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(HzbPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB pipeline layout!");
    }

    // 3. Load HZB Compute Shader
    auto hzbShaderCode = VulkanContext::readFile(SHADERS_DIR "hzb.spv");
    VkShaderModule hzbShaderModule = context.createShaderModule(hzbShaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = hzbShaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage = shaderStageInfo;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB compute pipeline!");
    }

    vkDestroyShaderModule(device, hzbShaderModule, nullptr);
    std::cout << "Successfully created HZB compute pipeline!" << std::endl;
}

void HzbPipeline::createDescriptorPoolAndSets() {
    VkDevice device = context.getDevice();

    // 1. Create Descriptor Pool
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 22; // 2 frames * 11 levels
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 44; // 2 frames * 11 levels * 2 storage bindings

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 22;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB descriptor pool!");
    }

    // 2. Allocate Descriptor Sets
    std::vector<VkDescriptorSetLayout> layouts(22, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 22;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> allocatedSets(22);
    if (vkAllocateDescriptorSets(device, &allocInfo, allocatedSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate HZB descriptor sets!");
    }

    // Distribute sets
    for (int f = 0; f < 2; f++) {
        for (int l = 0; l < 11; l++) {
            descriptorSets[f][l] = allocatedSets[f * 11 + l];
        }
    }

    // 3. Create Sampler for Depth Texture
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &depthSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB depth sampler!");
    }
}

void HzbPipeline::updateDescriptorSets(
    VkBuffer visBufferSSBO,
    const std::array<std::array<VkImageView, 11>, 2>& hzbLevelImageViews
) {
    VkDevice device = context.getDevice();

    for (int f = 0; f < 2; f++) {
        for (int l = 0; l < 11; l++) {
            VkDescriptorBufferInfo visBufferInfo{};
            visBufferInfo.buffer = visBufferSSBO;
            visBufferInfo.offset = 0;
            visBufferInfo.range = VK_WHOLE_SIZE;

            // Input HZB view is Level L-1 (or Level 0 if L == 0 as a dummy binding)
            VkDescriptorImageInfo inHzbInfo{};
            inHzbInfo.imageView = hzbLevelImageViews[f][l > 0 ? l - 1 : 0];
            inHzbInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            // Output HZB view is Level L
            VkDescriptorImageInfo outHzbInfo{};
            outHzbInfo.imageView = hzbLevelImageViews[f][l];
            outHzbInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::array<VkWriteDescriptorSet, 3> writes{};

            // Binding 0: VisBuffer SSBO
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descriptorSets[f][l];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &visBufferInfo;

            // Binding 1: Input HZB level image
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descriptorSets[f][l];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &inHzbInfo;

            // Binding 2: Output HZB level image
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = descriptorSets[f][l];
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo = &outHzbInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }
}

void HzbPipeline::cleanup() {
    VkDevice device = context.getDevice();

    if (depthSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, depthSampler, nullptr);
        depthSampler = VK_NULL_HANDLE;
    }

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
}

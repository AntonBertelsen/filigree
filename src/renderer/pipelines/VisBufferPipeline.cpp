#include "renderer/pipelines/VisBufferPipeline.hpp"
#include "scene/MeshNode.hpp"

#include <iostream>
#include <stdexcept>
#include <array>

VisBufferPipeline::VisBufferPipeline(VulkanContext& context, VkDescriptorSetLayout cullDescriptorSetLayout) 
    : context(context), cullDescriptorSetLayout(cullDescriptorSetLayout) {
    createPipeline();
}

VisBufferPipeline::~VisBufferPipeline() {
    cleanup();
}

void VisBufferPipeline::bind(VkCommandBuffer cb, bool useDepthTested) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, useDepthTested ? graphicsPipelineDepthTested : graphicsPipelinePureUAV);
}

void VisBufferPipeline::pushConstants(VkCommandBuffer cb, const VisBufferPushConstants& pcs) {
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VisBufferPushConstants), &pcs);
}

void VisBufferPipeline::createPipeline() {
    VkDevice device = context.getDevice();

    // 1. Load Compiled SPIR-V shaders
    auto vertShaderCode = VulkanContext::readFile(SHADERS_DIR "visbuffer_vert.spv");
    auto fragShaderCodePureUAV = VulkanContext::readFile(SHADERS_DIR "visbuffer_frag.spv");
    auto fragShaderCodeDepthTested = VulkanContext::readFile(SHADERS_DIR "visbuffer_earlyz_frag.spv");

    VkShaderModule vertShaderModule = context.createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModulePureUAV = context.createShaderModule(fragShaderCodePureUAV);
    VkShaderModule fragShaderModuleDepthTested = context.createShaderModule(fragShaderCodeDepthTested);

    // 2. Configure Shared Vertex Input state to read MeshVertex attributes
    static VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(MeshVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(MeshVertex, pos);
    // Normal
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(MeshVertex, normal);
    // TexCoord
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(MeshVertex, texCoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 3. Configure Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 4. Configure Dynamic States (Viewport and Scissor)
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // 5. Configure Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 6. Configure Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 7. Configure Color Blending (no blending needed for integer outputs, 0 color attachments)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments = nullptr;

    // 8. Configure Pipeline Layout with Push Constants (Vertex and Fragment stages)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(VisBufferPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &cullDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VisBuffer pipeline layout!");
    }

    // 9. Configure Dynamic Rendering target formats (0 color attachments)
    VkFormat depthFormat = context.getDepthFormat();
    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount = 0;
    renderingCreateInfo.pColorAttachmentFormats = nullptr;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;

    // --- PIPELINE A: Pure UAV (No Depth Testing/Writing) ---
    VkPipelineShaderStageCreateInfo vertStageInfoA{};
    vertStageInfoA.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfoA.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfoA.module = vertShaderModule;
    vertStageInfoA.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfoA{};
    fragStageInfoA.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfoA.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfoA.module = fragShaderModulePureUAV;
    fragStageInfoA.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStagesA[] = {vertStageInfoA, fragStageInfoA};

    VkPipelineDepthStencilStateCreateInfo depthStencilA{};
    depthStencilA.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilA.depthTestEnable = VK_FALSE;
    depthStencilA.depthWriteEnable = VK_FALSE;
    depthStencilA.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencilA.depthBoundsTestEnable = VK_FALSE;
    depthStencilA.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfoA{};
    pipelineInfoA.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfoA.pNext = &renderingCreateInfo;
    pipelineInfoA.stageCount = 2;
    pipelineInfoA.pStages = shaderStagesA;
    pipelineInfoA.pVertexInputState = &vertexInputInfo;
    pipelineInfoA.pInputAssemblyState = &inputAssembly;
    pipelineInfoA.pViewportState = &viewportState;
    pipelineInfoA.pRasterizationState = &rasterizer;
    pipelineInfoA.pMultisampleState = &multisampling;
    pipelineInfoA.pDepthStencilState = &depthStencilA;
    pipelineInfoA.pColorBlendState = &colorBlending;
    pipelineInfoA.pDynamicState = &dynamicStateInfo;
    pipelineInfoA.layout = pipelineLayout;
    pipelineInfoA.renderPass = VK_NULL_HANDLE;
    pipelineInfoA.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfoA, nullptr, &graphicsPipelinePureUAV) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VisBuffer Pure UAV graphics pipeline!");
    }

    // --- PIPELINE B: Depth Tested (With Depth Testing/Writing & Early-Z shader) ---
    VkPipelineShaderStageCreateInfo vertStageInfoB{};
    vertStageInfoB.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfoB.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfoB.module = vertShaderModule;
    vertStageInfoB.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfoB{};
    fragStageInfoB.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfoB.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfoB.module = fragShaderModuleDepthTested;
    fragStageInfoB.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStagesB[] = {vertStageInfoB, fragStageInfoB};

    VkPipelineDepthStencilStateCreateInfo depthStencilB{};
    depthStencilB.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilB.depthTestEnable = VK_TRUE;
    depthStencilB.depthWriteEnable = VK_TRUE;
    depthStencilB.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencilB.depthBoundsTestEnable = VK_FALSE;
    depthStencilB.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfoB{};
    pipelineInfoB.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfoB.pNext = &renderingCreateInfo;
    pipelineInfoB.stageCount = 2;
    pipelineInfoB.pStages = shaderStagesB;
    pipelineInfoB.pVertexInputState = &vertexInputInfo;
    pipelineInfoB.pInputAssemblyState = &inputAssembly;
    pipelineInfoB.pViewportState = &viewportState;
    pipelineInfoB.pRasterizationState = &rasterizer;
    pipelineInfoB.pMultisampleState = &multisampling;
    pipelineInfoB.pDepthStencilState = &depthStencilB;
    pipelineInfoB.pColorBlendState = &colorBlending;
    pipelineInfoB.pDynamicState = &dynamicStateInfo;
    pipelineInfoB.layout = pipelineLayout;
    pipelineInfoB.renderPass = VK_NULL_HANDLE;
    pipelineInfoB.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfoB, nullptr, &graphicsPipelineDepthTested) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VisBuffer Depth Tested graphics pipeline!");
    }

    // Clean up shader modules
    vkDestroyShaderModule(device, fragShaderModulePureUAV, nullptr);
    vkDestroyShaderModule(device, fragShaderModuleDepthTested, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    std::cout << "Successfully created Vulkan graphics pipelines inside VisBufferPipeline!" << std::endl;
}

void VisBufferPipeline::cleanup() {
    VkDevice device = context.getDevice();
    if (graphicsPipelinePureUAV != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipelinePureUAV, nullptr);
        graphicsPipelinePureUAV = VK_NULL_HANDLE;
    }
    if (graphicsPipelineDepthTested != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipelineDepthTested, nullptr);
        graphicsPipelineDepthTested = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
}

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
    VkPipeline pipeline = context.isShaderInt64AtomicsSupported() ? 
        (useDepthTested ? graphicsPipelineDepthTested64 : graphicsPipelinePureUAV64) :
        (useDepthTested ? graphicsPipelineDepthTested32 : graphicsPipelinePureUAV32);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void VisBufferPipeline::pushConstants(VkCommandBuffer cb, const VisBufferPushConstants& pcs) {
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VisBufferPushConstants), &pcs);
}

void VisBufferPipeline::createPipeline() {
    VkDevice device = context.getDevice();
    bool support64Bit = context.isShaderInt64AtomicsSupported();

    // 1. Load Compiled SPIR-V shaders
    auto vertShaderCode = VulkanContext::readFile(SHADERS_DIR "visbuffer_vert.spv");
    VkShaderModule vertShaderModule = context.createShaderModule(vertShaderCode);

    VkShaderModule fragShaderModulePureUAV64 = VK_NULL_HANDLE;
    VkShaderModule fragShaderModuleDepthTested64 = VK_NULL_HANDLE;
    if (support64Bit) {
        auto fragShaderCodePureUAV = VulkanContext::readFile(SHADERS_DIR "visbuffer_frag.spv");
        auto fragShaderCodeDepthTested = VulkanContext::readFile(SHADERS_DIR "visbuffer_earlyz_frag.spv");
        fragShaderModulePureUAV64 = context.createShaderModule(fragShaderCodePureUAV);
        fragShaderModuleDepthTested64 = context.createShaderModule(fragShaderCodeDepthTested);
    }

    auto fragShaderCodePureUAV32 = VulkanContext::readFile(SHADERS_DIR "visbuffer_32bit_frag.spv");
    auto fragShaderCodeDepthTested32 = VulkanContext::readFile(SHADERS_DIR "visbuffer_earlyz_32bit_frag.spv");
    VkShaderModule fragShaderModulePureUAV32 = context.createShaderModule(fragShaderCodePureUAV32);
    VkShaderModule fragShaderModuleDepthTested32 = context.createShaderModule(fragShaderCodeDepthTested32);

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

    // --- PIPELINES SHARED SHADER STAGES CONFIGS ---
    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    // A: 64-Bit Pipelines
    if (support64Bit) {
        VkPipelineShaderStageCreateInfo fragStageInfoPure64{};
        fragStageInfoPure64.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfoPure64.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfoPure64.module = fragShaderModulePureUAV64;
        fragStageInfoPure64.pName = "main";

        VkPipelineShaderStageCreateInfo stagesPure64[] = {vertStageInfo, fragStageInfoPure64};

        VkPipelineDepthStencilStateCreateInfo depthStencilPure64{};
        depthStencilPure64.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilPure64.depthTestEnable = VK_FALSE;
        depthStencilPure64.depthWriteEnable = VK_FALSE;
        depthStencilPure64.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pipelineInfoPure64{};
        pipelineInfoPure64.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfoPure64.pNext = &renderingCreateInfo;
        pipelineInfoPure64.stageCount = 2;
        pipelineInfoPure64.pStages = stagesPure64;
        pipelineInfoPure64.pVertexInputState = &vertexInputInfo;
        pipelineInfoPure64.pInputAssemblyState = &inputAssembly;
        pipelineInfoPure64.pViewportState = &viewportState;
        pipelineInfoPure64.pRasterizationState = &rasterizer;
        pipelineInfoPure64.pMultisampleState = &multisampling;
        pipelineInfoPure64.pDepthStencilState = &depthStencilPure64;
        pipelineInfoPure64.pColorBlendState = &colorBlending;
        pipelineInfoPure64.pDynamicState = &dynamicStateInfo;
        pipelineInfoPure64.layout = pipelineLayout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfoPure64, nullptr, &graphicsPipelinePureUAV64) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create 64-bit VisBuffer Pure UAV graphics pipeline!");
        }

        VkPipelineShaderStageCreateInfo fragStageInfoTested64{};
        fragStageInfoTested64.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfoTested64.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfoTested64.module = fragShaderModuleDepthTested64;
        fragStageInfoTested64.pName = "main";

        VkPipelineShaderStageCreateInfo stagesTested64[] = {vertStageInfo, fragStageInfoTested64};

        VkPipelineDepthStencilStateCreateInfo depthStencilTested64{};
        depthStencilTested64.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilTested64.depthTestEnable = VK_TRUE;
        depthStencilTested64.depthWriteEnable = VK_TRUE;
        depthStencilTested64.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pipelineInfoTested64{};
        pipelineInfoTested64.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfoTested64.pNext = &renderingCreateInfo;
        pipelineInfoTested64.stageCount = 2;
        pipelineInfoTested64.pStages = stagesTested64;
        pipelineInfoTested64.pVertexInputState = &vertexInputInfo;
        pipelineInfoTested64.pInputAssemblyState = &inputAssembly;
        pipelineInfoTested64.pViewportState = &viewportState;
        pipelineInfoTested64.pRasterizationState = &rasterizer;
        pipelineInfoTested64.pMultisampleState = &multisampling;
        pipelineInfoTested64.pDepthStencilState = &depthStencilTested64;
        pipelineInfoTested64.pColorBlendState = &colorBlending;
        pipelineInfoTested64.pDynamicState = &dynamicStateInfo;
        pipelineInfoTested64.layout = pipelineLayout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfoTested64, nullptr, &graphicsPipelineDepthTested64) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create 64-bit VisBuffer Depth Tested graphics pipeline!");
        }
    }

    // B: 32-Bit Pipelines
    {
        VkPipelineShaderStageCreateInfo fragStageInfoPure32{};
        fragStageInfoPure32.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfoPure32.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfoPure32.module = fragShaderModulePureUAV32;
        fragStageInfoPure32.pName = "main";

        VkPipelineShaderStageCreateInfo stagesPure32[] = {vertStageInfo, fragStageInfoPure32};

        VkPipelineDepthStencilStateCreateInfo depthStencilPure32{};
        depthStencilPure32.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilPure32.depthTestEnable = VK_FALSE;
        depthStencilPure32.depthWriteEnable = VK_FALSE;
        depthStencilPure32.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pipelineInfoPure32{};
        pipelineInfoPure32.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfoPure32.pNext = &renderingCreateInfo;
        pipelineInfoPure32.stageCount = 2;
        pipelineInfoPure32.pStages = stagesPure32;
        pipelineInfoPure32.pVertexInputState = &vertexInputInfo;
        pipelineInfoPure32.pInputAssemblyState = &inputAssembly;
        pipelineInfoPure32.pViewportState = &viewportState;
        pipelineInfoPure32.pRasterizationState = &rasterizer;
        pipelineInfoPure32.pMultisampleState = &multisampling;
        pipelineInfoPure32.pDepthStencilState = &depthStencilPure32;
        pipelineInfoPure32.pColorBlendState = &colorBlending;
        pipelineInfoPure32.pDynamicState = &dynamicStateInfo;
        pipelineInfoPure32.layout = pipelineLayout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfoPure32, nullptr, &graphicsPipelinePureUAV32) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create 32-bit VisBuffer Pure UAV graphics pipeline!");
        }

        VkPipelineShaderStageCreateInfo fragStageInfoTested32{};
        fragStageInfoTested32.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfoTested32.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfoTested32.module = fragShaderModuleDepthTested32;
        fragStageInfoTested32.pName = "main";

        VkPipelineShaderStageCreateInfo stagesTested32[] = {vertStageInfo, fragStageInfoTested32};

        VkPipelineDepthStencilStateCreateInfo depthStencilTested32{};
        depthStencilTested32.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilTested32.depthTestEnable = VK_TRUE;
        depthStencilTested32.depthWriteEnable = VK_FALSE; // No writing to depth buffer in Pass 2!
        depthStencilTested32.depthCompareOp = VK_COMPARE_OP_EQUAL; // EQUAL check for Pass 2!

        VkGraphicsPipelineCreateInfo pipelineInfoTested32{};
        pipelineInfoTested32.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfoTested32.pNext = &renderingCreateInfo;
        pipelineInfoTested32.stageCount = 2;
        pipelineInfoTested32.pStages = stagesTested32;
        pipelineInfoTested32.pVertexInputState = &vertexInputInfo;
        pipelineInfoTested32.pInputAssemblyState = &inputAssembly;
        pipelineInfoTested32.pViewportState = &viewportState;
        pipelineInfoTested32.pRasterizationState = &rasterizer;
        pipelineInfoTested32.pMultisampleState = &multisampling;
        pipelineInfoTested32.pDepthStencilState = &depthStencilTested32;
        pipelineInfoTested32.pColorBlendState = &colorBlending;
        pipelineInfoTested32.pDynamicState = &dynamicStateInfo;
        pipelineInfoTested32.layout = pipelineLayout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfoTested32, nullptr, &graphicsPipelineDepthTested32) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create 32-bit VisBuffer Depth Tested graphics pipeline!");
        }
    }

    // C: Depth-Only pipeline (No Fragment Shader Stage)
    {
        VkPipelineDepthStencilStateCreateInfo depthStencilOnly{};
        depthStencilOnly.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilOnly.depthTestEnable = VK_TRUE;
        depthStencilOnly.depthWriteEnable = VK_TRUE;
        depthStencilOnly.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pipelineInfoOnly{};
        pipelineInfoOnly.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfoOnly.pNext = &renderingCreateInfo;
        pipelineInfoOnly.stageCount = 1; // Only vertex stage!
        pipelineInfoOnly.pStages = &vertStageInfo;
        pipelineInfoOnly.pVertexInputState = &vertexInputInfo;
        pipelineInfoOnly.pInputAssemblyState = &inputAssembly;
        pipelineInfoOnly.pViewportState = &viewportState;
        pipelineInfoOnly.pRasterizationState = &rasterizer;
        pipelineInfoOnly.pMultisampleState = &multisampling;
        pipelineInfoOnly.pDepthStencilState = &depthStencilOnly;
        pipelineInfoOnly.pColorBlendState = &colorBlending;
        pipelineInfoOnly.pDynamicState = &dynamicStateInfo;
        pipelineInfoOnly.layout = pipelineLayout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfoOnly, nullptr, &graphicsPipelineDepthOnly) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create VisBuffer Depth-Only graphics pipeline!");
        }
    }

    // Clean up shader modules
    if (fragShaderModulePureUAV64 != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragShaderModulePureUAV64, nullptr);
    if (fragShaderModuleDepthTested64 != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragShaderModuleDepthTested64, nullptr);
    vkDestroyShaderModule(device, fragShaderModulePureUAV32, nullptr);
    vkDestroyShaderModule(device, fragShaderModuleDepthTested32, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    std::cout << "Successfully created Vulkan graphics pipelines inside VisBufferPipeline!" << std::endl;
}

void VisBufferPipeline::cleanup() {
    VkDevice device = context.getDevice();
    if (graphicsPipelinePureUAV64 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipelinePureUAV64, nullptr);
        graphicsPipelinePureUAV64 = VK_NULL_HANDLE;
    }
    if (graphicsPipelineDepthTested64 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipelineDepthTested64, nullptr);
        graphicsPipelineDepthTested64 = VK_NULL_HANDLE;
    }
    if (graphicsPipelinePureUAV32 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipelinePureUAV32, nullptr);
        graphicsPipelinePureUAV32 = VK_NULL_HANDLE;
    }
    if (graphicsPipelineDepthTested32 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipelineDepthTested32, nullptr);
        graphicsPipelineDepthTested32 = VK_NULL_HANDLE;
    }
    if (graphicsPipelineDepthOnly != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipelineDepthOnly, nullptr);
        graphicsPipelineDepthOnly = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
}

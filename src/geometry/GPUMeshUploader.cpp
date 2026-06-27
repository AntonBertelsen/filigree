#include "geometry/GPUMeshUploader.hpp"
#include <stdexcept>
#include <array>
#include <cstring>
#include <iostream>

void GPUMeshUploader::uploadScene(
    VulkanContext& context,
    VkDescriptorPool descriptorPool,
    VkDescriptorSetLayout globalLayout,
    const std::vector<MeshletData>& meshletDatas,
    GPUScene& outScene,
    std::vector<GPUMesh>& outMeshes
) {
    VkDevice device = context.getDevice();

    std::vector<MeshVertex> globalVertices;
    std::vector<uint16_t> globalIndices;
    std::vector<uint32_t> globalTraditionalIndices;
    std::vector<MeshletBounds> globalBounds;
    std::vector<VkDrawIndexedIndirectCommand> globalInputCommands;

    outMeshes.resize(meshletDatas.size());

    for (size_t i = 0; i < meshletDatas.size(); ++i) {
        const auto& data = meshletDatas[i];
        auto& mesh = outMeshes[i];

        mesh.baseVertexOffset = static_cast<uint32_t>(globalVertices.size());
        mesh.baseIndexOffset = static_cast<uint32_t>(globalIndices.size());
        mesh.firstMeshletCommandOffset = static_cast<uint32_t>(globalInputCommands.size());
        mesh.clusterCount = data.clusterCount;

        // Append Nanite vertices
        globalVertices.insert(globalVertices.end(), data.flatVertices.begin(), data.flatVertices.end());
        // Append Nanite indices
        globalIndices.insert(globalIndices.end(), data.flatIndices.begin(), data.flatIndices.end());
        // Append Bounds
        globalBounds.insert(globalBounds.end(), data.boundsList.begin(), data.boundsList.end());

        // Append and adjust indirect commands
        for (auto cmd : data.indirectCommands) {
            cmd.firstIndex += mesh.baseIndexOffset;
            cmd.vertexOffset += static_cast<int32_t>(mesh.baseVertexOffset);
            globalInputCommands.push_back(cmd);
        }

        // Append traditional vertices
        mesh.traditionalVertexOffset = static_cast<uint32_t>(globalVertices.size());
        globalVertices.insert(globalVertices.end(), data.originalVertices.begin(), data.originalVertices.end());

        // Append traditional indices
        mesh.traditionalIndexOffset = static_cast<uint32_t>(globalTraditionalIndices.size());
        mesh.traditionalIndexCount = static_cast<uint32_t>(data.originalIndices.size());
        globalTraditionalIndices.insert(globalTraditionalIndices.end(), data.originalIndices.begin(), data.originalIndices.end());
    }

    auto uploadBufferHelper = [&](VkBuffer& outBuffer, VmaAllocation& outAllocation, VkDeviceSize size, VkBufferUsageFlags usage, const void* data) {
        if (size == 0) return;
        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;
        context.createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            stagingBuffer,
            stagingAllocation
        );

        void* mappedData = nullptr;
        vmaMapMemory(context.getAllocator(), stagingAllocation, &mappedData);
        memcpy(mappedData, data, size);
        vmaUnmapMemory(context.getAllocator(), stagingAllocation);

        context.createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            0,
            outBuffer,
            outAllocation
        );

        context.copyBuffer(stagingBuffer, outBuffer, size);
        vmaDestroyBuffer(context.getAllocator(), stagingBuffer, stagingAllocation);
    };

    // Upload geometry buffers
    uploadBufferHelper(outScene.vertexBuffer, outScene.vertexAllocation, globalVertices.size() * sizeof(MeshVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, globalVertices.data());
    uploadBufferHelper(outScene.indexBuffer, outScene.indexAllocation, globalIndices.size() * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, globalIndices.data());
    uploadBufferHelper(outScene.traditionalIndexBuffer, outScene.traditionalIndexAllocation, globalTraditionalIndices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, globalTraditionalIndices.data());
    uploadBufferHelper(outScene.boundsBuffer, outScene.boundsAllocation, globalBounds.size() * sizeof(MeshletBounds), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, globalBounds.data());
    uploadBufferHelper(outScene.inputCommandsBuffer, outScene.inputCommandsAllocation, globalInputCommands.size() * sizeof(VkDrawIndexedIndirectCommand), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, globalInputCommands.data());

    // Create HZB sampler
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
    samplerInfo.maxLod = 11.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &outScene.hzbSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB sampler in GPUMeshUploader!");
    }

    // Allocate Descriptor Sets
    std::array<VkDescriptorSetLayout, 2> layouts = { globalLayout, globalLayout };
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, outScene.globalDescriptorSets) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate global scene descriptor sets inside GPUMeshUploader!");
    }
}

void GPUMeshUploader::uploadSceneInstances(
    VulkanContext& context,
    GPUScene& scene,
    const std::vector<GPUMesh>& meshes,
    const std::vector<std::vector<glm::mat4>>& modelInstances
) {
    VmaAllocator allocator = context.getAllocator();

    std::vector<InstanceData> instances;
    std::vector<CullTask> cullTasks;
    std::vector<VkDrawIndexedIndirectCommand> traditionalCommands;

    for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
        if (meshIdx >= modelInstances.size()) continue;
        const auto& mesh = meshes[meshIdx];
        const auto& mats = modelInstances[meshIdx];

        for (const auto& mat : mats) {
            uint32_t globalInstIdx = static_cast<uint32_t>(instances.size());

            InstanceData instData{};
            instData.modelMatrix = mat;
            instData.baseVertexOffset = mesh.traditionalVertexOffset;
            instData.baseIndexOffset = mesh.traditionalIndexOffset;
            instData.firstMeshletCommandOffset = mesh.firstMeshletCommandOffset;
            instData.isNanite = 1;
            instances.push_back(instData);

            for (uint32_t c = 0; c < mesh.clusterCount; ++c) {
                CullTask task{};
                task.globalInstIdx = globalInstIdx;
                task.clustIdx = c;
                cullTasks.push_back(task);
            }

            // Traditional indirect draw command
            VkDrawIndexedIndirectCommand tradCmd{};
            tradCmd.indexCount = mesh.traditionalIndexCount;
            tradCmd.instanceCount = 1;
            tradCmd.firstIndex = mesh.traditionalIndexOffset;
            tradCmd.vertexOffset = static_cast<int32_t>(mesh.traditionalVertexOffset);
            tradCmd.firstInstance = globalInstIdx;
            traditionalCommands.push_back(tradCmd);
        }
    }

    scene.totalCullTasks = static_cast<uint32_t>(cullTasks.size());
    scene.totalInstances = static_cast<uint32_t>(instances.size());

    if (instances.empty()) return;

    VkDeviceSize instanceBufferSize = instances.size() * sizeof(InstanceData);
    VkDeviceSize cullTasksBufferSize = cullTasks.size() * sizeof(CullTask);
    VkDeviceSize indirectBufferSize = cullTasks.size() * sizeof(VkDrawIndexedIndirectCommand);
    VkDeviceSize visibilityBufferSize = cullTasks.size() * sizeof(uint32_t);
    VkDeviceSize tradIndirectBufferSize = traditionalCommands.size() * sizeof(VkDrawIndexedIndirectCommand);

    auto updateBufferData = [&](VkBuffer& buffer, VmaAllocation& allocation, VkDeviceSize size, VkBufferUsageFlags usage, const void* data, VmaAllocationCreateFlags flags) {
        if (buffer != VK_NULL_HANDLE) {
            VmaAllocationInfo info;
            vmaGetAllocationInfo(allocator, allocation, &info);
            if (info.size < size) {
                vmaDestroyBuffer(allocator, buffer, allocation);
                buffer = VK_NULL_HANDLE;
                allocation = VK_NULL_HANDLE;
            }
        }
        if (buffer == VK_NULL_HANDLE) {
            context.createBuffer(size, usage, VMA_MEMORY_USAGE_AUTO, flags, buffer, allocation);
        }
        void* mapped = nullptr;
        vmaMapMemory(allocator, allocation, &mapped);
        memcpy(mapped, data, size);
        vmaUnmapMemory(allocator, allocation);
    };

    auto resizeDeviceBuffer = [&](VkBuffer& buffer, VmaAllocation& allocation, VkDeviceSize size, VkBufferUsageFlags usage) {
        if (buffer != VK_NULL_HANDLE) {
            VmaAllocationInfo info;
            vmaGetAllocationInfo(allocator, allocation, &info);
            if (info.size < size) {
                vmaDestroyBuffer(allocator, buffer, allocation);
                buffer = VK_NULL_HANDLE;
                allocation = VK_NULL_HANDLE;
            }
        }
        if (buffer == VK_NULL_HANDLE) {
            context.createBuffer(size, usage, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, buffer, allocation);
        }
    };

    for (int i = 0; i < 2; i++) {
        updateBufferData(scene.instanceBuffer[i], scene.instanceAllocation[i], instanceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instances.data(), VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        updateBufferData(scene.cullTasksBuffer[i], scene.cullTasksAllocation[i], cullTasksBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, cullTasks.data(), VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        updateBufferData(scene.traditionalIndirectBuffer[i], scene.traditionalIndirectAllocation[i], tradIndirectBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, traditionalCommands.data(), VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        resizeDeviceBuffer(scene.culledIndirectBuffer[i], scene.culledIndirectAllocation[i], indirectBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        resizeDeviceBuffer(scene.culledSoftwareIndirectBuffer[i], scene.culledSoftwareIndirectAllocation[i], indirectBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        
        // drawCountBuffer: Alloc size is sizeof(uint32_t), so we can just update it
        uint32_t zero = 0;
        updateBufferData(scene.drawCountBuffer[i], scene.drawCountAllocation[i], sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &zero, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
        
        // Readback buffer: host-visible copy so the CPU can read the draw count after the fence
        // Used by the drawIndirectCount fallback path (macOS) to avoid submitting all totalCullTasks draws
        if (scene.drawCountReadbackBuffer[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context.getAllocator(), scene.drawCountReadbackBuffer[i], scene.drawCountReadbackAllocation[i]);
        }
        context.createBuffer(
            sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            scene.drawCountReadbackBuffer[i],
            scene.drawCountReadbackAllocation[i]
        );
        scene.cachedHwDrawCount[i] = scene.totalCullTasks;
        
        struct SoftwareDrawCountInit {
            uint32_t x = 0;
            uint32_t y = 1;
            uint32_t z = 1;
        } swInit;
        updateBufferData(scene.softwareDrawCountBuffer[i], scene.softwareDrawCountAllocation[i], sizeof(SoftwareDrawCountInit), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &swInit, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

        resizeDeviceBuffer(scene.visibilityBuffer[i], scene.visibilityAllocation[i], visibilityBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
}

void GPUMeshUploader::updateDescriptorSets(
    VulkanContext& context,
    GPUScene& scene,
    VkImageView hzbImageView0,
    VkImageView hzbImageView1,
    VkBuffer visBufferSSBO,
    VkBuffer depthBufferSSBO
) {
    VkDevice device = context.getDevice();

    for (int i = 0; i < 2; ++i) {
        if (scene.globalDescriptorSets[i] == VK_NULL_HANDLE) continue;

        VkDescriptorBufferInfo inputCommandsInfo{};
        inputCommandsInfo.buffer = scene.inputCommandsBuffer;
        inputCommandsInfo.offset = 0;
        inputCommandsInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo culledIndirectInfo{};
        culledIndirectInfo.buffer = scene.culledIndirectBuffer[i] != VK_NULL_HANDLE ? scene.culledIndirectBuffer[i] : scene.inputCommandsBuffer;
        culledIndirectInfo.offset = 0;
        culledIndirectInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo drawCountInfo{};
        drawCountInfo.buffer = scene.drawCountBuffer[i] != VK_NULL_HANDLE ? scene.drawCountBuffer[i] : scene.inputCommandsBuffer;
        drawCountInfo.offset = 0;
        drawCountInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo boundsInfo{};
        boundsInfo.buffer = scene.boundsBuffer;
        boundsInfo.offset = 0;
        boundsInfo.range = VK_WHOLE_SIZE;

        VkDescriptorImageInfo hzbImageInfo{};
        hzbImageInfo.sampler = scene.hzbSampler;
        hzbImageInfo.imageView = ((i + 1) % 2 == 0) ? hzbImageView0 : hzbImageView1;
        hzbImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo visibilityInfo{};
        visibilityInfo.buffer = scene.visibilityBuffer[i] != VK_NULL_HANDLE ? scene.visibilityBuffer[i] : scene.inputCommandsBuffer;
        visibilityInfo.offset = 0;
        visibilityInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = scene.instanceBuffer[i] != VK_NULL_HANDLE ? scene.instanceBuffer[i] : scene.inputCommandsBuffer;
        instanceInfo.offset = 0;
        instanceInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo cullTasksInfo{};
        cullTasksInfo.buffer = scene.cullTasksBuffer[i] != VK_NULL_HANDLE ? scene.cullTasksBuffer[i] : scene.inputCommandsBuffer;
        cullTasksInfo.offset = 0;
        cullTasksInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo vertexInfo{};
        vertexInfo.buffer = scene.vertexBuffer;
        vertexInfo.offset = 0;
        vertexInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo indexInfo{};
        indexInfo.buffer = scene.indexBuffer;
        indexInfo.offset = 0;
        indexInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo visBufferInfo{};
        visBufferInfo.buffer = visBufferSSBO != VK_NULL_HANDLE ? visBufferSSBO : scene.inputCommandsBuffer;
        visBufferInfo.offset = 0;
        visBufferInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo traditionalIndexInfo{};
        traditionalIndexInfo.buffer = scene.traditionalIndexBuffer;
        traditionalIndexInfo.offset = 0;
        traditionalIndexInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo softwareIndirectInfo{};
        softwareIndirectInfo.buffer = scene.culledSoftwareIndirectBuffer[i] != VK_NULL_HANDLE ? scene.culledSoftwareIndirectBuffer[i] : scene.inputCommandsBuffer;
        softwareIndirectInfo.offset = 0;
        softwareIndirectInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo softwareDrawCountInfo{};
        softwareDrawCountInfo.buffer = scene.softwareDrawCountBuffer[i] != VK_NULL_HANDLE ? scene.softwareDrawCountBuffer[i] : scene.inputCommandsBuffer;
        softwareDrawCountInfo.offset = 0;
        softwareDrawCountInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo depthBufferInfo{};
        depthBufferInfo.buffer = depthBufferSSBO != VK_NULL_HANDLE ? depthBufferSSBO : scene.inputCommandsBuffer;
        depthBufferInfo.offset = 0;
        depthBufferInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 15> writes{};
        
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = scene.globalDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &inputCommandsInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = scene.globalDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &culledIndirectInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = scene.globalDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &drawCountInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = scene.globalDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &boundsInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = scene.globalDescriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &hzbImageInfo;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = scene.globalDescriptorSets[i];
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo = &visibilityInfo;

        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = scene.globalDescriptorSets[i];
        writes[6].dstBinding = 6;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo = &instanceInfo;

        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = scene.globalDescriptorSets[i];
        writes[7].dstBinding = 7;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[7].descriptorCount = 1;
        writes[7].pBufferInfo = &cullTasksInfo;

        writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet = scene.globalDescriptorSets[i];
        writes[8].dstBinding = 8;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[8].descriptorCount = 1;
        writes[8].pBufferInfo = &vertexInfo;

        writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet = scene.globalDescriptorSets[i];
        writes[9].dstBinding = 9;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[9].descriptorCount = 1;
        writes[9].pBufferInfo = &indexInfo;

        writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet = scene.globalDescriptorSets[i];
        writes[10].dstBinding = 10;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[10].descriptorCount = 1;
        writes[10].pBufferInfo = &visBufferInfo;

        writes[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[11].dstSet = scene.globalDescriptorSets[i];
        writes[11].dstBinding = 11;
        writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[11].descriptorCount = 1;
        writes[11].pBufferInfo = &traditionalIndexInfo;

        writes[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[12].dstSet = scene.globalDescriptorSets[i];
        writes[12].dstBinding = 12;
        writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[12].descriptorCount = 1;
        writes[12].pBufferInfo = &softwareIndirectInfo;

        writes[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[13].dstSet = scene.globalDescriptorSets[i];
        writes[13].dstBinding = 13;
        writes[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[13].descriptorCount = 1;
        writes[13].pBufferInfo = &softwareDrawCountInfo;

        writes[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[14].dstSet = scene.globalDescriptorSets[i];
        writes[14].dstBinding = 14;
        writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[14].descriptorCount = 1;
        writes[14].pBufferInfo = &depthBufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

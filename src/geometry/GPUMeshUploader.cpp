#include "geometry/GPUMeshUploader.hpp"
#include <stdexcept>
#include <array>

void GPUMeshUploader::uploadMesh(
    VulkanContext& context,
    VkDescriptorPool descriptorPool,
    VkDescriptorSetLayout descriptorLayout,
    const MeshletData& data,
    GPUMesh& outMesh
) {
    VulkanContext& ctx = context;
    VkDevice device = context.getDevice();
    VmaAllocator allocator = context.getAllocator();

    outMesh.clusterCount = data.clusterCount;
    outMesh.totalIndexCount = static_cast<uint32_t>(data.flatIndices.size());

    // 1. Vertex Buffer
    VkDeviceSize vertexBufferSize = sizeof(MeshVertex) * data.flatVertices.size();
    
    VkBuffer stagingVertexBuffer;
    VmaAllocation stagingVertexAllocation;
    ctx.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        stagingVertexBuffer,
        stagingVertexAllocation
    );
    
    void* vertexData;
    vmaMapMemory(allocator, stagingVertexAllocation, &vertexData);
    memcpy(vertexData, data.flatVertices.data(), vertexBufferSize);
    vmaUnmapMemory(allocator, stagingVertexAllocation);
    
    ctx.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        outMesh.vertexBuffer,
        outMesh.vertexAllocation
    );
    
    ctx.copyBuffer(stagingVertexBuffer, outMesh.vertexBuffer, vertexBufferSize);
    vmaDestroyBuffer(allocator, stagingVertexBuffer, stagingVertexAllocation);
    
    // 2. Index Buffer
    VkDeviceSize indexBufferSize = sizeof(uint16_t) * data.flatIndices.size();
    
    VkBuffer stagingIndexBuffer;
    VmaAllocation stagingIndexAllocation;
    ctx.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        stagingIndexBuffer,
        stagingIndexAllocation
    );
    
    void* indexData;
    vmaMapMemory(allocator, stagingIndexAllocation, &indexData);
    memcpy(indexData, data.flatIndices.data(), indexBufferSize);
    vmaUnmapMemory(allocator, stagingIndexAllocation);
    
    ctx.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        outMesh.indexBuffer,
        outMesh.indexAllocation
    );
    
    ctx.copyBuffer(stagingIndexBuffer, outMesh.indexBuffer, indexBufferSize);
    vmaDestroyBuffer(allocator, stagingIndexBuffer, stagingIndexAllocation);
    
    // 3. Indirect Buffer
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndexedIndirectCommand) * data.indirectCommands.size();
    
    VkBuffer stagingIndirectBuffer;
    VmaAllocation stagingIndirectAllocation;
    ctx.createBuffer(
        indirectBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        stagingIndirectBuffer,
        stagingIndirectAllocation
    );
    
    void* indirectData;
    vmaMapMemory(allocator, stagingIndirectAllocation, &indirectData);
    memcpy(indirectData, data.indirectCommands.data(), indirectBufferSize);
    vmaUnmapMemory(allocator, stagingIndirectAllocation);
    
    ctx.createBuffer(
        indirectBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        outMesh.indirectBuffer,
        outMesh.indirectAllocation
    );
    
    ctx.copyBuffer(stagingIndirectBuffer, outMesh.indirectBuffer, indirectBufferSize);
    vmaDestroyBuffer(allocator, stagingIndirectBuffer, stagingIndirectAllocation);

    // 4. Bounds Buffer
    VkDeviceSize boundsBufferSize = sizeof(MeshletBounds) * data.boundsList.size();

    VkBuffer stagingBoundsBuffer;
    VmaAllocation stagingBoundsAllocation;
    ctx.createBuffer(
        boundsBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        stagingBoundsBuffer,
        stagingBoundsAllocation
    );

    void* boundsData;
    vmaMapMemory(allocator, stagingBoundsAllocation, &boundsData);
    memcpy(boundsData, data.boundsList.data(), boundsBufferSize);
    vmaUnmapMemory(allocator, stagingBoundsAllocation);

    ctx.createBuffer(
        boundsBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        outMesh.boundsBuffer,
        outMesh.boundsAllocation
    );

    ctx.copyBuffer(stagingBoundsBuffer, outMesh.boundsBuffer, boundsBufferSize);
    vmaDestroyBuffer(allocator, stagingBoundsBuffer, stagingBoundsAllocation);

    // 4.5. Traditional Rendering Buffers
    if (!data.originalVertices.empty()) {
        VkDeviceSize tradVertexBufferSize = sizeof(MeshVertex) * data.originalVertices.size();
        
        VkBuffer stagingTradVertexBuffer;
        VmaAllocation stagingTradVertexAllocation;
        ctx.createBuffer(
            tradVertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            stagingTradVertexBuffer,
            stagingTradVertexAllocation
        );
        
        void* tradVertexData;
        vmaMapMemory(allocator, stagingTradVertexAllocation, &tradVertexData);
        memcpy(tradVertexData, data.originalVertices.data(), tradVertexBufferSize);
        vmaUnmapMemory(allocator, stagingTradVertexAllocation);
        
        ctx.createBuffer(
            tradVertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            0,
            outMesh.traditionalVertexBuffer,
            outMesh.traditionalVertexAllocation
        );
        
        ctx.copyBuffer(stagingTradVertexBuffer, outMesh.traditionalVertexBuffer, tradVertexBufferSize);
        vmaDestroyBuffer(allocator, stagingTradVertexBuffer, stagingTradVertexAllocation);
    }

    if (!data.originalIndices.empty()) {
        outMesh.traditionalIndexCount = static_cast<uint32_t>(data.originalIndices.size());
        VkDeviceSize tradIndexBufferSize = sizeof(uint32_t) * data.originalIndices.size();
        
        VkBuffer stagingTradIndexBuffer;
        VmaAllocation stagingTradIndexAllocation;
        ctx.createBuffer(
            tradIndexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            stagingTradIndexBuffer,
            stagingTradIndexAllocation
        );
        
        void* tradIndexData;
        vmaMapMemory(allocator, stagingTradIndexAllocation, &tradIndexData);
        memcpy(tradIndexData, data.originalIndices.data(), tradIndexBufferSize);
        vmaUnmapMemory(allocator, stagingTradIndexAllocation);
        
        ctx.createBuffer(
            tradIndexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            0,
            outMesh.traditionalIndexBuffer,
            outMesh.traditionalIndexAllocation
        );
        
        ctx.copyBuffer(stagingTradIndexBuffer, outMesh.traditionalIndexBuffer, tradIndexBufferSize);
        vmaDestroyBuffer(allocator, stagingTradIndexBuffer, stagingTradIndexAllocation);
    }

    // 5. Allocate Culled Indirect Buffers, Draw Count Buffers, and Visibility Buffers (Double-Buffered)
    for (int i = 0; i < 2; ++i) {
        ctx.createBuffer(
            indirectBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            0,
            outMesh.culledIndirectBuffer[i],
            outMesh.culledIndirectAllocation[i]
        );

        ctx.createBuffer(
            sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            outMesh.drawCountBuffer[i],
            outMesh.drawCountAllocation[i]
        );

        ctx.createBuffer(
            data.clusterCount * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            0,
            outMesh.visibilityBuffer[i],
            outMesh.visibilityAllocation[i]
        );
    }

    // Create HZB sampler (Nearest filtering for direct conservative footprint queries)
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

    if (vkCreateSampler(device, &samplerInfo, nullptr, &outMesh.hzbSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HZB sampler in GPUMeshUploader!");
    }

    // 6. Write Descriptor Sets (Double-Buffered)
    std::array<VkDescriptorSetLayout, 2> layouts = { descriptorLayout, descriptorLayout };
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, outMesh.computeDescriptorSets) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate compute descriptor sets inside GPUMeshUploader!");
    }

    for (int i = 0; i < 2; ++i) {
        // Allocate Sets
    }
    // We already allocated them above!
    // Actually, `outMesh.computeDescriptorSets` is populated.
    
    updateDescriptorSets(context, outMesh);
}

void GPUMeshUploader::updateDescriptorSets(
    VulkanContext& context,
    GPUMesh& outMesh
) {
    VkDevice device = context.getDevice();

    for (int i = 0; i < 2; ++i) {
        VkDescriptorBufferInfo inputInfo{};
        inputInfo.buffer = outMesh.indirectBuffer;
        inputInfo.offset = 0;
        inputInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo outputInfo{};
        outputInfo.buffer = outMesh.culledIndirectBuffer[i];
        outputInfo.offset = 0;
        outputInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo countInfo{};
        countInfo.buffer = outMesh.drawCountBuffer[i];
        countInfo.offset = 0;
        countInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo boundsInfo{};
        boundsInfo.buffer = outMesh.boundsBuffer;
        boundsInfo.offset = 0;
        boundsInfo.range = VK_WHOLE_SIZE;

        // Source HZB view for frame i is the HZB image from the OTHER frame (i + 1) % 2
        VkDescriptorImageInfo hzbImageInfo{};
        hzbImageInfo.sampler = outMesh.hzbSampler;
        hzbImageInfo.imageView = context.getHzbImageView((i + 1) % 2);
        hzbImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo visibilityInfo{};
        visibilityInfo.buffer = outMesh.visibilityBuffer[i];
        visibilityInfo.offset = 0;
        visibilityInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 6> descriptorWrites{};
        
        // Binding 0: Input Commands
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = outMesh.computeDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &inputInfo;

        // Binding 1: Output Commands
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = outMesh.computeDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &outputInfo;

        // Binding 2: Draw Count
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = outMesh.computeDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &countInfo;

        // Binding 3: Meshlet Bounds
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = outMesh.computeDescriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &boundsInfo;

        // Binding 4: HZB Texture (Sampled Image)
        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = outMesh.computeDescriptorSets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pImageInfo = &hzbImageInfo;

        // Binding 5: Visibilities
        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = outMesh.computeDescriptorSets[i];
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].dstArrayElement = 0;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pBufferInfo = &visibilityInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

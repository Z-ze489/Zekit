#include "pch.h"

#include "ZeVkUIRenderer.h"
#include "ZeVulkanLoader.h"
#include "ZeVkShaderSpv.h"
#include "ZeFrameCollector.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <iostream>

namespace Vk {

    namespace {
        struct PushConstants {
            float   proj[16];
            int32_t isTex;
            int32_t isColor;
        };
        static_assert(sizeof(PushConstants) == 72,
            "push constant size must match the GLSL-side layout");

        void imageBarrier(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout from, VkImageLayout to,
                          VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = from;
            b.newLayout = to;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask = srcAccess;
            b.dstAccessMask = dstAccess;

            vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                0, nullptr, 0, nullptr, 1, &b);
        }

        bool ok(VkResult r, const char* what)
        {
            if (r == VK_SUCCESS) return true;
            std::cerr << "[VkUI] " << what << " failed, VkResult = " << static_cast<int>(r) << "\n";
            return false;
        }
    }

    VkUIRenderer::~VkUIRenderer() { Shutdown(); }

    bool VkUIRenderer::Init(VulkanContext& ctx)
    {
        if (m_pipeline != VK_NULL_HANDLE) return true;

        m_ctx = &ctx;
        if (!m_ctx->isReady()) {
            std::cerr << "[VkUI] VulkanContext is not ready; cannot create the UI renderer\n";
            m_ctx = nullptr;
            return false;
        }

        if (!createDescriptorSetLayout()) { Shutdown(); return false; }
        if (!createDescriptorPool())      { Shutdown(); return false; }
        if (!createPipelineLayout())      { Shutdown(); return false; }
        if (!createPipeline())            { Shutdown(); return false; }
        if (!createSampler())             { Shutdown(); return false; }

        m_frameSlotCount = m_ctx->framesInFlight();
        m_frames.resize(m_frameSlotCount);

        const uint8_t white[4] = { 255, 255, 255, 255 };
        m_dummyTex = CreateRGBA(1, 1, white, 4, false);
        if (m_dummyTex == 0) {
            std::cerr << "[VkUI] failed to create the dummy texture\n";
            Shutdown();
            return false;
        }

        return true;
    }

    void VkUIRenderer::Shutdown()
    {
        VkDevice dev = m_ctx ? m_ctx->device() : VK_NULL_HANDLE;

        if (dev != VK_NULL_HANDLE) {
            m_ctx->waitIdle();

            for (Texture& t : m_textures) {
                if (t.set) vkFreeDescriptorSets(dev, m_pool, 1, &t.set);
                if (t.view) vkDestroyImageView(dev, t.view, nullptr);
                if (t.image) vkDestroyImage(dev, t.image, nullptr);
                if (t.memory) vkFreeMemory(dev, t.memory, nullptr);
            }

            for (FrameBuffers& fb : m_frames) destroyFrameBuffers(fb);

            if (m_pipeline)       vkDestroyPipeline(dev, m_pipeline, nullptr);
            if (m_pipelineLayout) vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
            if (m_pool)           vkDestroyDescriptorPool(dev, m_pool, nullptr);
            if (m_setLayout)      vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
            if (m_sampler)        vkDestroySampler(dev, m_sampler, nullptr);
        }

        m_textures.clear();
        m_freeSlots.clear();
        m_dummyTex = 0;
        m_frames.clear();

        m_pipeline = VK_NULL_HANDLE;
        m_pipelineLayout = VK_NULL_HANDLE;
        m_pool = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        m_sampler = VK_NULL_HANDLE;
        m_ctx = nullptr;
    }

    bool VkUIRenderer::createDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings = &b;

        return ok(vkCreateDescriptorSetLayout(m_ctx->device(), &ci, nullptr, &m_setLayout),
                  "vkCreateDescriptorSetLayout");
    }

    bool VkUIRenderer::createDescriptorPool()
    {
        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        size.descriptorCount = kMaxTextures;

        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = kMaxTextures;
        ci.poolSizeCount = 1;
        ci.pPoolSizes = &size;

        return ok(vkCreateDescriptorPool(m_ctx->device(), &ci, nullptr, &m_pool),
                  "vkCreateDescriptorPool");
    }

    bool VkUIRenderer::createPipelineLayout()
    {
        VkPushConstantRange r{};
        r.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        r.offset = 0;
        r.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &m_setLayout;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &r;

        return ok(vkCreatePipelineLayout(m_ctx->device(), &ci, nullptr, &m_pipelineLayout),
                  "vkCreatePipelineLayout");
    }

    bool VkUIRenderer::createPipeline()
    {
        const VkDevice dev = m_ctx->device();

        VkShaderModule vertModule = VK_NULL_HANDLE;
        VkShaderModule fragModule = VK_NULL_HANDLE;

        VkShaderModuleCreateInfo smi{};
        smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smi.codeSize = Shader::kUIVertSpvBytes;
        smi.pCode = Shader::kUIVertSpv;
        if (!ok(vkCreateShaderModule(dev, &smi, nullptr, &vertModule), "vkCreateShaderModule(vert)"))
            return false;

        smi.codeSize = Shader::kUIFragSpvBytes;
        smi.pCode = Shader::kUIFragSpv;
        if (!ok(vkCreateShaderModule(dev, &smi, nullptr, &fragModule), "vkCreateShaderModule(frag)")) {
            vkDestroyShaderModule(dev, vertModule, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription bind{};
        bind.binding = 0;
        bind.stride = sizeof(Kit::UIVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[5]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Kit::UIVertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Kit::UIVertex, r) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Kit::UIVertex, u) };
        attrs[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Kit::UIVertex, cx) };
        attrs[4] = { 4, 0, VK_FORMAT_R32_SFLOAT,          offsetof(Kit::UIVertex, rad) };

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = 5;
        vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        ms.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;
        ds.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1;
        cbs.pAttachments = &cba;

        const VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &ds;
        ci.pColorBlendState = &cbs;
        ci.pDynamicState = &dyn;
        ci.layout = m_pipelineLayout;
        ci.renderPass = m_ctx->renderPass();
        ci.subpass = 0;

        const VkResult r = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, nullptr, &m_pipeline);

        vkDestroyShaderModule(dev, vertModule, nullptr);
        vkDestroyShaderModule(dev, fragModule, nullptr);

        return ok(r, "vkCreateGraphicsPipelines");
    }

    bool VkUIRenderer::createSampler()
    {
        VkSamplerCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter = VK_FILTER_LINEAR;
        ci.minFilter = VK_FILTER_LINEAR;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ci.minLod = 0.0f;
        ci.maxLod = 0.0f;
        ci.anisotropyEnable = VK_FALSE;
        ci.compareEnable = VK_FALSE;

        return ok(vkCreateSampler(m_ctx->device(), &ci, nullptr, &m_sampler),
                  "vkCreateSampler");
    }

    bool VkUIRenderer::ensureCapacity(FrameBuffers& fb, VkDeviceSize vertexBytes,
                                      VkDeviceSize indexBytes)
    {
        if (fb.vboCap >= vertexBytes && fb.iboCap >= indexBytes) return true;

        m_ctx->waitIdle();
        destroyFrameBuffers(fb);

        const VkDevice dev = m_ctx->device();

        const VkDeviceSize vbCap = vertexBytes ? vertexBytes + vertexBytes / 2 : 4096;
        const VkDeviceSize ibCap = indexBytes ? indexBytes + indexBytes / 2 : 4096;

        auto makeBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                              VkBuffer& buf, VkDeviceMemory& mem, uint8_t*& mapped) -> bool {
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = size;
            bi.usage = usage;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (!ok(vkCreateBuffer(dev, &bi, nullptr, &buf), "vkCreateBuffer")) return false;

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(dev, buf, &req);

            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            if (!ok(vkAllocateMemory(dev, &ai, nullptr, &mem), "vkAllocateMemory(buffer)")) return false;
            if (!ok(vkBindBufferMemory(dev, buf, mem, 0), "vkBindBufferMemory")) return false;

            if (!ok(vkMapMemory(dev, mem, 0, size, 0, (void**)&mapped), "vkMapMemory")) return false;
            return true;
        };

        if (!makeBuffer(vbCap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, fb.vbo, fb.vboMem, fb.vboMapped))
            return false;
        fb.vboCap = vbCap;

        if (!makeBuffer(ibCap, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, fb.ibo, fb.iboMem, fb.iboMapped))
            return false;
        fb.iboCap = ibCap;

        return true;
    }

    void VkUIRenderer::destroyFrameBuffers(FrameBuffers& fb)
    {
        const VkDevice dev = m_ctx ? m_ctx->device() : VK_NULL_HANDLE;
        if (dev == VK_NULL_HANDLE) return;

        if (fb.vboMapped) { vkUnmapMemory(dev, fb.vboMem); fb.vboMapped = nullptr; }
        if (fb.vbo) { vkDestroyBuffer(dev, fb.vbo, nullptr); fb.vbo = VK_NULL_HANDLE; }
        if (fb.vboMem) { vkFreeMemory(dev, fb.vboMem, nullptr); fb.vboMem = VK_NULL_HANDLE; }
        fb.vboCap = 0;

        if (fb.iboMapped) { vkUnmapMemory(dev, fb.iboMem); fb.iboMapped = nullptr; }
        if (fb.ibo) { vkDestroyBuffer(dev, fb.ibo, nullptr); fb.ibo = VK_NULL_HANDLE; }
        if (fb.iboMem) { vkFreeMemory(dev, fb.iboMem, nullptr); fb.iboMem = VK_NULL_HANDLE; }
        fb.iboCap = 0;
    }

    uint32_t VkUIRenderer::CreateGray(int w, int h, const uint8_t* pixels, int pitch)
    {
        return uploadTexture(w, h, pixels, pitch,
            VK_FORMAT_R8_UNORM, 1, false);
    }

    uint32_t VkUIRenderer::CreateRGBA(int w, int h, const uint8_t* pixels, int pitch, bool bgra)
    {
        return uploadTexture(w, h, pixels, pitch,
            VK_FORMAT_R8G8B8A8_UNORM, 4, bgra);
    }

    uint32_t VkUIRenderer::uploadTexture(int w, int h, const uint8_t* pixels, int pitch,
                                         VkFormat format, int bytesPerPixel, bool swapRedBlue)
    {
        if (!m_ctx || w <= 0 || h <= 0 || !pixels || bytesPerPixel <= 0) return 0;

        const VkDevice dev = m_ctx->device();

        const size_t rowBytes = static_cast<size_t>(w) * bytesPerPixel;
        std::vector<uint8_t> packed(rowBytes * static_cast<size_t>(h));

        for (int y = 0; y < h; ++y) {
            const uint8_t* src = pixels + static_cast<size_t>(y) * pitch;
            uint8_t* dst = packed.data() + static_cast<size_t>(y) * rowBytes;

            if (swapRedBlue && bytesPerPixel == 4) {
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
            else {
                std::memcpy(dst, src, rowBytes);
            }
        }

        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory imageMem = VK_NULL_HANDLE;

        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = format;
        ici.extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (!ok(vkCreateImage(dev, &ici, nullptr, &image), "vkCreateImage")) return 0;

        VkMemoryRequirements ireq{};
        vkGetImageMemoryRequirements(dev, image, &ireq);

        VkMemoryAllocateInfo imai{};
        imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        imai.allocationSize = ireq.size;
        imai.memoryTypeIndex = findMemoryType(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (!ok(vkAllocateMemory(dev, &imai, nullptr, &imageMem), "vkAllocateMemory(image)")) {
            vkDestroyImage(dev, image, nullptr);
            return 0;
        }
        vkBindImageMemory(dev, image, imageMem, 0);

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        uint8_t* stagingMapped = nullptr;

        {
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = packed.size();
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (!ok(vkCreateBuffer(dev, &bi, nullptr, &staging), "vkCreateBuffer(staging)")) {
                vkDestroyImage(dev, image, nullptr);
                vkFreeMemory(dev, imageMem, nullptr);
                return 0;
            }

            VkMemoryRequirements sreq{};
            vkGetBufferMemoryRequirements(dev, staging, &sreq);

            VkMemoryAllocateInfo sai{};
            sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            sai.allocationSize = sreq.size;
            sai.memoryTypeIndex = findMemoryType(sreq.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            if (!ok(vkAllocateMemory(dev, &sai, nullptr, &stagingMem), "vkAllocateMemory(staging)") ||
                !ok(vkBindBufferMemory(dev, staging, stagingMem, 0), "vkBindBufferMemory(staging)") ||
                !ok(vkMapMemory(dev, stagingMem, 0, packed.size(), 0, (void**)&stagingMapped),
                    "vkMapMemory(staging)")) {
                if (stagingMapped) vkUnmapMemory(dev, stagingMem);
                if (stagingMem) vkFreeMemory(dev, stagingMem, nullptr);
                if (staging) vkDestroyBuffer(dev, staging, nullptr);
                vkDestroyImage(dev, image, nullptr);
                vkFreeMemory(dev, imageMem, nullptr);
                return 0;
            }

            std::memcpy(stagingMapped, packed.data(), packed.size());
            vkUnmapMemory(dev, stagingMem);
        }

        runOneShot([&](VkCommandBuffer cmd) {
            imageBarrier(cmd, image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };

            vkCmdCopyBufferToImage(cmd, staging, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            imageBarrier(cmd, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        });

        vkDestroyBuffer(dev, staging, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);

        VkImageView view = VK_NULL_HANDLE;
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = format;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (!ok(vkCreateImageView(dev, &vci, nullptr, &view), "vkCreateImageView")) {
            vkDestroyImage(dev, image, nullptr);
            vkFreeMemory(dev, imageMem, nullptr);
            return 0;
        }

        const uint32_t handle = allocSlot();
        if (handle == 0) {
            vkDestroyImageView(dev, view, nullptr);
            vkDestroyImage(dev, image, nullptr);
            vkFreeMemory(dev, imageMem, nullptr);
            return 0;
        }

        Texture& t = m_textures[handle - 1];
        t.image = image;
        t.memory = imageMem;
        t.view = view;

        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = m_pool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &m_setLayout;
        if (!ok(vkAllocateDescriptorSets(dev, &dai, &t.set), "vkAllocateDescriptorSets")) {
            vkDestroyImageView(dev, view, nullptr);
            vkDestroyImage(dev, image, nullptr);
            vkFreeMemory(dev, imageMem, nullptr);
            t = Texture{};
            releaseSlot(handle);
            return 0;
        }

        VkDescriptorImageInfo dii{};
        dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dii.imageView = view;
        dii.sampler = m_sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = t.set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &dii;

        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

        return handle;
    }

    void VkUIRenderer::Destroy(uint32_t tex)
    {
        if (tex == 0 || !m_ctx || tex > m_textures.size()) return;

        Texture& t = m_textures[tex - 1];
        if (!t.occupied) return;

        m_ctx->waitIdle();

        const VkDevice dev = m_ctx->device();
        if (t.set) { vkFreeDescriptorSets(dev, m_pool, 1, &t.set); t.set = VK_NULL_HANDLE; }
        if (t.view) { vkDestroyImageView(dev, t.view, nullptr); t.view = VK_NULL_HANDLE; }
        if (t.image) { vkDestroyImage(dev, t.image, nullptr); t.image = VK_NULL_HANDLE; }
        if (t.memory) { vkFreeMemory(dev, t.memory, nullptr); t.memory = VK_NULL_HANDLE; }

        releaseSlot(tex);
    }

    uint32_t VkUIRenderer::allocSlot()
    {
        if (!m_freeSlots.empty()) {
            const uint32_t slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_textures[slot].occupied = true;
            return slot + 1;
        }

        if (m_textures.size() >= kMaxTextures) {
            if (!m_overflowWarned) {
                m_overflowWarned = true;
                std::cerr << "[VkUI] texture slot limit reached (" << kMaxTextures
                          << "); further textures will not be created (time for a glyph atlas)\n";
            }
            return 0;
        }

        m_textures.emplace_back();
        m_textures.back().occupied = true;
        return static_cast<uint32_t>(m_textures.size());
    }

    void VkUIRenderer::releaseSlot(uint32_t handle)
    {
        if (handle == 0 || handle > m_textures.size()) return;
        m_textures[handle - 1].occupied = false;
        m_freeSlots.push_back(handle - 1);
    }

    VkDescriptorSet VkUIRenderer::descriptorFor(uint32_t handle) const
    {
        if (handle != 0 && handle <= m_textures.size()) {
            const Texture& t = m_textures[handle - 1];
            if (t.occupied && t.set != VK_NULL_HANDLE) return t.set;
        }
        if (m_dummyTex != 0 && m_dummyTex <= m_textures.size()) {
            const Texture& d = m_textures[m_dummyTex - 1];
            if (d.set != VK_NULL_HANDLE) return d.set;
        }
        return VK_NULL_HANDLE;
    }

    int VkUIRenderer::Draw(const Kit::FrameCollector& fc, const VulkanContext::FrameInfo& frame)
    {
        if (m_pipeline == VK_NULL_HANDLE || !m_ctx) return 0;
        if (frame.frameSlot >= m_frames.size()) {
            std::cerr << "[VkUI] Draw: frameSlot out of range ("
                      << frame.frameSlot << " >= " << m_frames.size() << ")\n";
            return 0;
        }

        const auto& verts = fc.Vertices();
        const auto& idxs = fc.Indices();
        const auto& batches = fc.Batches();
        if (verts.empty() || idxs.empty() || batches.empty()) return 0;

        const VkDeviceSize vertBytes = verts.size() * sizeof(Kit::UIVertex);
        const VkDeviceSize idxBytes = idxs.size() * sizeof(uint32_t);

        FrameBuffers& fb = m_frames[frame.frameSlot];
        if (!ensureCapacity(fb, vertBytes, idxBytes)) return 0;

        std::memcpy(fb.vboMapped, verts.data(), static_cast<size_t>(vertBytes));
        std::memcpy(fb.iboMapped, idxs.data(), static_cast<size_t>(idxBytes));

        const VkCommandBuffer cmd = frame.cmd;
        const uint32_t W = frame.extent.width;
        const uint32_t H = frame.extent.height;
        if (W == 0 || H == 0) return 0;

        const uint32_t UW = (frame.uiExtent.width  > 0) ? frame.uiExtent.width  : W;
        const uint32_t UH = (frame.uiExtent.height > 0) ? frame.uiExtent.height : H;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.width = static_cast<float>(W);
        vp.height = static_cast<float>(H);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D sc{};
        sc.offset = { 0, 0 };
        sc.extent = { W, H };
        vkCmdSetScissor(cmd, 0, 1, &sc);

        VkDeviceSize vboOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &fb.vbo, &vboOffset);
        vkCmdBindIndexBuffer(cmd, fb.ibo, 0, VK_INDEX_TYPE_UINT32);

        const glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(UW),
                                          0.0f, static_cast<float>(UH),
                                          -1.0f, 1.0f);

        PushConstants pc{};
        std::memcpy(pc.proj, glm::value_ptr(proj), sizeof(pc.proj));

        int draws = 0;
        for (const auto& b : batches) {
            if (b.indexCount == 0) continue;

            pc.isTex = (b.texture != 0) ? 1 : 0;
            pc.isColor = (b.isColor ? 1 : 0);
            vkCmdPushConstants(cmd, m_pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(pc), &pc);

            const VkDescriptorSet set = (b.texture != 0) ? descriptorFor(b.texture)
                                                          : descriptorFor(m_dummyTex);
            if (set == VK_NULL_HANDLE) continue;

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                0, 1, &set, 0, nullptr);

            vkCmdDrawIndexed(cmd, b.indexCount, 1, b.indexOffset, 0, 0);
            ++draws;
        }

        return draws;
    }

    uint32_t VkUIRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const
    {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(m_ctx->physicalDevice(), &mp);

        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        }
        return 0;
    }

    void VkUIRenderer::runOneShot(const std::function<void(VkCommandBuffer)>& fn)
    {
        const VkDevice dev = m_ctx->device();

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = m_ctx->commandPool();
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (!ok(vkAllocateCommandBuffers(dev, &ai, &cmd), "vkAllocateCommandBuffers(oneshot)"))
            return;

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        fn(cmd);

        vkEndCommandBuffer(cmd);

        VkFence fence = VK_NULL_HANDLE;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(dev, &fi, nullptr, &fence);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(m_ctx->graphicsQueue(), 1, &si, fence);

        constexpr uint64_t kUploadTimeoutNs = 5ull * 1000 * 1000 * 1000;
        const VkResult waitRes = vkWaitForFences(dev, 1, &fence, VK_TRUE, kUploadTimeoutNs);
        if (waitRes != VK_SUCCESS) {
            std::cerr << "[VkUI] vkWaitForFences(staging upload) failed: "
                      << static_cast<int>(waitRes) << " (timeout or device lost)\n";

            vkDeviceWaitIdle(dev);
        }

        vkDestroyFence(dev, fence, nullptr);
        vkFreeCommandBuffers(dev, m_ctx->commandPool(), 1, &cmd);
    }

}

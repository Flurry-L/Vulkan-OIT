/*
* Vulkan Example - Order Independent Transparency rendering using linked lists
*
* Copyright by Sascha Willems - www.saschawillems.de
* Copyright by Daemyung Jang  - dm86.jang@gmail.com
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/
#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"
#include <fstream>
#include <memory>
#include <vector>
#include <shaderc/shaderc.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "stb_image.h"
#include "stb_image_write.h"

// #include "tiny_obj_loader.h"

#define NODE_COUNT 20

struct UISettings {
    int oitAlgorithm = 0;
    int optimization = 0;
    bool animateLight = false;
    bool onlyOpt32 = false;
    bool unrollBubble = false;
    float lightSpeed = 0.25f;
    float lightTimer = 0.0f;
    int OIT_LAYERS = 16;
    int MAX_FRAGMENT_COUNT = 128;
    float alpha = 0.5f;
    bool exportRequest = false;
} uiSettings;

class VulkanExample : public VulkanExampleBase {
public:
    std::vector<std::string> methods{"base", "BMA", "RBS_BMA", "iRBS_BMA", "RBS_only", "iRBS_only"};
    std::vector<std::string> oit_alg{"linked list", "atomic loop"};
    bool supportsShaderInt64{false};
    bool supportsShaderBufferInt64Atomics{false};
    struct {
        vkglTF::Model sphere;
        vkglTF::Model cube;
    } models;

    struct Node {
        glm::vec4 color;
        float depth{0.0f};
        uint32_t next{0};
    };

    struct {
        uint32_t count{0};
        uint32_t maxNodeCount{0};
    } geometrySBO;

    struct GeometryPass {
        VkRenderPass renderPass{VK_NULL_HANDLE};
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        vks::Buffer geometry;
        vks::Texture headIndex;
        vks::Buffer abuffer;
    } geometryPass;

    struct RenderPassUniformData {
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec4 lightPos;
        glm::ivec3 viewport; // (width, height, width * height)
    } renderPassUniformData;
    vks::Buffer renderPassUniformBuffer;

    struct ObjectData {
        glm::mat4 model;
        glm::vec4 color;
    };

    struct {
        VkDescriptorSetLayout geometry{VK_NULL_HANDLE};
        VkDescriptorSetLayout color{VK_NULL_HANDLE};
    } descriptorSetLayouts;

    struct {
        VkPipelineLayout geometry{VK_NULL_HANDLE};
        VkPipelineLayout color{VK_NULL_HANDLE};
    } pipelineLayouts;

    struct {
        VkPipeline geometry{VK_NULL_HANDLE};
        VkPipeline loop64{VK_NULL_HANDLE};
        VkPipeline kbuf_blend{VK_NULL_HANDLE};
        VkPipeline color{VK_NULL_HANDLE};
        VkPipeline rbs_color_128{VK_NULL_HANDLE};
        VkPipeline bma_color_128{VK_NULL_HANDLE};
        VkPipeline color_32{VK_NULL_HANDLE};
        VkPipeline color_16{VK_NULL_HANDLE};
        VkPipeline color_8{VK_NULL_HANDLE};
        VkPipeline color_4{VK_NULL_HANDLE};
        VkPipeline base_color_32{VK_NULL_HANDLE};
        VkPipeline bitonic_color{VK_NULL_HANDLE};
        VkPipeline rbs_only{VK_NULL_HANDLE};
        VkPipeline irbs_only{VK_NULL_HANDLE};
    } pipelines;

    struct {
        VkDescriptorSet geometry{VK_NULL_HANDLE};
        VkDescriptorSet color{VK_NULL_HANDLE};
    } descriptorSets;

    VkDeviceSize objectUniformBufferSize{0};

    static int clampIntValue(int value, int minValue, int maxValue) {
        if (value < minValue) {
            return minValue;
        }
        if (value > maxValue) {
            return maxValue;
        }
        return value;
    }

    std::string getOitShaderPath(const std::string &shader) const {
        return getShadersPath() + "oit/" + shader;
    }

    static std::string getDirectoryPath(const std::string &path) {
        const size_t splitPos = path.find_last_of("/\\");
        if (splitPos == std::string::npos) {
            return ".";
        }
        return path.substr(0, splitPos);
    }

    static std::string joinPath(const std::string &basePath, const std::string &name) {
        if (basePath.empty()) {
            return name;
        }
        const char lastChar = basePath.back();
        if ((lastChar == '/') || (lastChar == '\\')) {
            return basePath + name;
        }
        return basePath + "/" + name;
    }

    static bool readTextFile(const std::string &path, std::string *contents) {
        std::ifstream input(path, std::ios::in);
        if (!input.is_open()) {
            return false;
        }
        contents->assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        return true;
    }

    static shaderc_shader_kind getShaderKind(const std::string &shaderPath) {
        if (shaderPath.size() >= 5 && shaderPath.substr(shaderPath.size() - 5) == ".vert") {
            return shaderc_vertex_shader;
        }
        if (shaderPath.size() >= 5 && shaderPath.substr(shaderPath.size() - 5) == ".frag") {
            return shaderc_fragment_shader;
        }
        if (shaderPath.size() >= 5 && shaderPath.substr(shaderPath.size() - 5) == ".comp") {
            return shaderc_compute_shader;
        }
        if (shaderPath.size() >= 5 && shaderPath.substr(shaderPath.size() - 5) == ".geom") {
            return shaderc_geometry_shader;
        }
        if (shaderPath.size() >= 5 && shaderPath.substr(shaderPath.size() - 5) == ".tesc") {
            return shaderc_tess_control_shader;
        }
        if (shaderPath.size() >= 5 && shaderPath.substr(shaderPath.size() - 5) == ".tese") {
            return shaderc_tess_evaluation_shader;
        }
        return shaderc_glsl_infer_from_source;
    }

    static void addCompileMacro(shaderc::CompileOptions *options, const std::string &define) {
        const size_t splitPos = define.find('=');
        if (splitPos == std::string::npos) {
            options->AddMacroDefinition(define);
        } else {
            options->AddMacroDefinition(define.substr(0, splitPos), define.substr(splitPos + 1));
        }
    }

    class FileIncluder final : public shaderc::CompileOptions::IncluderInterface {
    public:
        explicit FileIncluder(std::string shaderRootPath) : shaderRoot(std::move(shaderRootPath)) {
        }

        shaderc_include_result *GetInclude(const char *requestedSource, shaderc_include_type type,
                                           const char *requestingSource, size_t) override {
            auto *storedResult = new StoredIncludeResult();
            auto *includeResult = new shaderc_include_result();

            const std::string requested = requestedSource ? requestedSource : "";
            const std::string requesting = requestingSource ? requestingSource : "";
            std::string includePath;

            if (type == shaderc_include_type_relative) {
                includePath = joinPath(getDirectoryPath(requesting), requested);
            } else {
                includePath = joinPath(shaderRoot, requested);
            }

            if (!readTextFile(includePath, &storedResult->content)) {
                includePath = joinPath(shaderRoot, requested);
                if (!readTextFile(includePath, &storedResult->content)) {
                    storedResult->content = "Failed to include shader file: " + requested;
                    storedResult->sourceName.clear();
                    includeResult->source_name = "";
                    includeResult->source_name_length = 0;
                    includeResult->content = storedResult->content.c_str();
                    includeResult->content_length = storedResult->content.size();
                    includeResult->user_data = storedResult;
                    return includeResult;
                }
            }

            storedResult->sourceName = includePath;
            includeResult->source_name = storedResult->sourceName.c_str();
            includeResult->source_name_length = storedResult->sourceName.size();
            includeResult->content = storedResult->content.c_str();
            includeResult->content_length = storedResult->content.size();
            includeResult->user_data = storedResult;
            return includeResult;
        }

        void ReleaseInclude(shaderc_include_result *data) override {
            delete static_cast<StoredIncludeResult *>(data->user_data);
            delete data;
        }

    private:
        struct StoredIncludeResult {
            std::string sourceName;
            std::string content;
        };

        std::string shaderRoot;
    };

    bool compileShader(const std::string &shader, const std::vector<std::string> &defines) {
        const std::string shaderPath = getOitShaderPath(shader);

        std::string shaderSource;
        if (!readTextFile(shaderPath, &shaderSource)) {
            std::cerr << "Failed to read shader source: " << shaderPath << std::endl;
            return false;
        }

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetGenerateDebugInfo();
        options.SetIncluder(std::make_unique<FileIncluder>(getOitShaderPath("")));
        for (const auto &define: defines) {
            addCompileMacro(&options, define);
        }

        const shaderc::SpvCompilationResult result =
                compiler.CompileGlslToSpv(shaderSource, getShaderKind(shaderPath), shaderPath.c_str(), "main", options);
        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            std::cerr << "Failed to compile shader: " << shader << "\n" << result.GetErrorMessage() << std::endl;
            return false;
        }

        std::ofstream output(shaderPath + ".spv", std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            std::cerr << "Failed to write SPIR-V output for shader: " << shader << std::endl;
            return false;
        }

        for (auto word = result.cbegin(); word != result.cend(); ++word) {
            output.write(reinterpret_cast<const char *>(&(*word)), sizeof(uint32_t));
        }

        return output.good();
    }

    bool compileShaders(const std::vector<std::string> &shaders, const std::vector<std::string> &defines = {}) {
        bool success = true;
        for (const auto &shader: shaders) {
            if (!compileShader(shader, defines)) {
                success = false;
            }
        }
        return success;
    }

    void compileLinkedListColorShader() {
        compileShaders({"color.frag"}, {"MAX_FRAGMENT_COUNT=" + std::to_string(uiSettings.MAX_FRAGMENT_COUNT)});
    }

    void compileAtomicLoopShaders() {
        compileShaders(
                {"loop64.vert", "loop64.frag", "kbuf_blend.frag", "kbuf_blend.vert"},
                {"OIT_LAYERS=" + std::to_string(uiSettings.OIT_LAYERS)});
    }

    void compileBubbleShaders() {
        compileShaders(
                {"rbs_only.frag", "rbs_color_128.frag"},
                {
                        "MAX_FRAGMENT_COUNT=" + std::to_string(uiSettings.MAX_FRAGMENT_COUNT),
                        "unroll=" + std::to_string(uiSettings.unrollBubble)
                });
    }

    void compileInitialShaders() {
        compileShaders({"color.frag"}, {"MAX_FRAGMENT_COUNT=128"});
        compileAtomicLoopShaders();
        compileShaders({"color.vert", "geometry.frag", "geometry.vert"}, {"MAX_FRAGMENT_COUNT=16"});
        compileShaders(
                {"rbs_color_128.frag", "bma_color_128.frag", "base_color_32.frag", "bitonic.frag", "irbs_only.frag",
                 "rbs_only.frag"},
                {"MAX_FRAGMENT_COUNT=128"});
        compileShaders({"base_color_32.frag"}, {"MAX_FRAGMENT_COUNT=32"});
        compileShaders({"color_4.frag", "color_8.frag", "color_16.frag", "color_32.frag"});
    }

    void destroyPipelines() {
        vkDestroyPipeline(device, pipelines.geometry, nullptr);
        vkDestroyPipeline(device, pipelines.loop64, nullptr);
        vkDestroyPipeline(device, pipelines.kbuf_blend, nullptr);
        vkDestroyPipeline(device, pipelines.color, nullptr);
        vkDestroyPipeline(device, pipelines.bitonic_color, nullptr);
        vkDestroyPipeline(device, pipelines.color_4, nullptr);
        vkDestroyPipeline(device, pipelines.color_8, nullptr);
        vkDestroyPipeline(device, pipelines.color_16, nullptr);
        vkDestroyPipeline(device, pipelines.color_32, nullptr);
        vkDestroyPipeline(device, pipelines.rbs_color_128, nullptr);
        vkDestroyPipeline(device, pipelines.bma_color_128, nullptr);
        vkDestroyPipeline(device, pipelines.base_color_32, nullptr);
        vkDestroyPipeline(device, pipelines.irbs_only, nullptr);
        vkDestroyPipeline(device, pipelines.rbs_only, nullptr);

        pipelines.geometry = VK_NULL_HANDLE;
        pipelines.loop64 = VK_NULL_HANDLE;
        pipelines.kbuf_blend = VK_NULL_HANDLE;
        pipelines.color = VK_NULL_HANDLE;
        pipelines.bitonic_color = VK_NULL_HANDLE;
        pipelines.color_4 = VK_NULL_HANDLE;
        pipelines.color_8 = VK_NULL_HANDLE;
        pipelines.color_16 = VK_NULL_HANDLE;
        pipelines.color_32 = VK_NULL_HANDLE;
        pipelines.rbs_color_128 = VK_NULL_HANDLE;
        pipelines.bma_color_128 = VK_NULL_HANDLE;
        pipelines.base_color_32 = VK_NULL_HANDLE;
        pipelines.irbs_only = VK_NULL_HANDLE;
        pipelines.rbs_only = VK_NULL_HANDLE;
    }

    bool drawIntControl(const char *label, int *value, int minValue, int maxValue, int step = 1) {
        bool changed = false;
        ImGui::PushID(label);

        ImGui::TextUnformatted(label);
        ImGui::PushItemWidth(-80.0f);
        changed |= ImGui::SliderInt("##slider", value, minValue, maxValue);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("-")) {
            *value -= step;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+")) {
            *value += step;
            changed = true;
        }

        const int clamped = clampIntValue(*value, minValue, maxValue);
        if (clamped != *value) {
            *value = clamped;
            changed = true;
        }

        ImGui::PopID();
        return changed;
    }

    VulkanExample() : VulkanExampleBase() {
        title = "Order independent transparency rendering";
        camera.type = Camera::CameraType::lookat;
        //camera.setPosition(glm::vec3(0.0f, 0.0f, -6.0f));
        //camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
        camera.setPosition(glm::vec3(1.685f, 0.46f, -7.69001f));
        camera.setRotation(glm::vec3(35.f, 1074.f, 0.f));
        camera.setPerspective(60.0f, (float) width / (float) height, 0.1f, 256.0f);
        requiresStencil = true;

        compileInitialShaders();
    }

    ~VulkanExample() {
        if (device) {
            destroyPipelines();
            vkDestroyPipelineLayout(device, pipelineLayouts.geometry, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayouts.color, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.geometry, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.color, nullptr);
            destroyGeometryPass();
            renderPassUniformBuffer.destroy();
        }
    }

    void getEnabledFeatures() override {
        // The linked lists are built in a fragment shader using atomic stores, so the sample won't work without that feature available
        if (deviceFeatures.fragmentStoresAndAtomics) {
            enabledFeatures.fragmentStoresAndAtomics = VK_TRUE;
        } else {
            vks::tools::exitFatal("Selected GPU does not support stores and atomic operations in the fragment stage",
                                  VK_ERROR_FEATURE_NOT_PRESENT);
        }

        supportsShaderInt64 = (deviceFeatures.shaderInt64 == VK_TRUE);
        if (supportsShaderInt64) {
            enabledFeatures.shaderInt64 = VK_TRUE;
        }

        VkPhysicalDeviceVulkan12Features vk12Features{};
        vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vk12Features;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
        supportsShaderBufferInt64Atomics = (vk12Features.shaderBufferInt64Atomics == VK_TRUE);

        if (!supportsShaderInt64) {
            methods = {"base", "BMA"};
            uiSettings.optimization = 0;
            std::cerr << "Warning: shaderInt64 is not supported on this GPU. Disabling RBS/iRBS variants." << std::endl;
        }

        if (!(supportsShaderInt64 && supportsShaderBufferInt64Atomics)) {
            oit_alg = {"linked list"};
            uiSettings.oitAlgorithm = 0;
            std::cerr << "Warning: 64-bit buffer atomics are not supported. Disabling atomic loop mode." << std::endl;
        }
    };

    void loadAssets() {
        const uint32_t glTFLoadingFlags =
                vkglTF::FileLoadingFlags::PreTransformVertices |
                vkglTF::FileLoadingFlags::PreMultiplyVertexColors |
                vkglTF::FileLoadingFlags::FlipY;
        models.cube.loadFromFile(getAssetPath() + "models/worldcar.gltf", vulkanDevice, queue, glTFLoadingFlags);
    }

    void prepareUniformBuffers() {
        // Create an uniform buffer for a render pass.
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &renderPassUniformBuffer,
                                                   sizeof(RenderPassUniformData), &renderPassUniformBuffer));
        updateUniformBuffers();
    }

    void createGeometryRenderPass() {
        VkAttachmentDescription attachments[1];
        attachments[0].flags = VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;
        attachments[0].format = depthFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthStencilAttachmentRef = {};
        depthStencilAttachmentRef.attachment = 0;
        depthStencilAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.pDepthStencilAttachment = &depthStencilAttachmentRef;
        VkRenderPassCreateInfo renderPassInfo = vks::initializers::renderPassCreateInfo();
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;

        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &geometryPass.renderPass));
    }

    void createGeometryFramebuffer() {
        VkImageView v_attachments[1];
        v_attachments[0] = depthStencil.view;
        VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
        fbufCreateInfo.renderPass = geometryPass.renderPass;
        fbufCreateInfo.attachmentCount = 1;
        fbufCreateInfo.pAttachments = v_attachments;
        fbufCreateInfo.width = width;
        fbufCreateInfo.height = height;
        fbufCreateInfo.layers = 1;

        VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &geometryPass.framebuffer));
    }

    void createGeometryCounterBuffer() {
        geometrySBO.count = 0;
        geometrySBO.maxNodeCount = NODE_COUNT * width * height;

        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &stagingBuffer,
                sizeof(geometrySBO),
                &geometrySBO));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &geometryPass.geometry,
                sizeof(geometrySBO)));

        vulkanDevice->copyBuffer(&stagingBuffer, &geometryPass.geometry, queue);
        stagingBuffer.destroy();
    }

    void createHeadIndexStorageImage() {
        geometryPass.headIndex.device = vulkanDevice;

        VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R32_UINT;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
#if (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
        // SRS - On macOS/iOS use linear tiling for atomic image access, see https://github.com/KhronosGroup/MoltenVK/issues/1027
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
#else
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
#endif
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

        VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &geometryPass.headIndex.image));

        geometryPass.headIndex.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, geometryPass.headIndex.image, &memReqs);

        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits,
                                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &geometryPass.headIndex.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, geometryPass.headIndex.image, geometryPass.headIndex.deviceMemory, 0));

        VkImageViewCreateInfo imageViewInfo = vks::initializers::imageViewCreateInfo();
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.format = VK_FORMAT_R32_UINT;
        imageViewInfo.flags = 0;
        imageViewInfo.image = geometryPass.headIndex.image;
        imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewInfo.subresourceRange.baseMipLevel = 0;
        imageViewInfo.subresourceRange.levelCount = 1;
        imageViewInfo.subresourceRange.baseArrayLayer = 0;
        imageViewInfo.subresourceRange.layerCount = 1;

        VK_CHECK_RESULT(vkCreateImageView(device, &imageViewInfo, nullptr, &geometryPass.headIndex.view));

        geometryPass.headIndex.width = width;
        geometryPass.headIndex.height = height;
        geometryPass.headIndex.mipLevels = 1;
        geometryPass.headIndex.layerCount = 1;
        geometryPass.headIndex.descriptor.imageView = geometryPass.headIndex.view;
        geometryPass.headIndex.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        geometryPass.headIndex.sampler = VK_NULL_HANDLE;
    }

    void createLinkedListStorageBuffer() {
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &geometryPass.abuffer,
                sizeof(Node) * geometrySBO.maxNodeCount));
    }

    void transitionHeadIndexStorageImageLayout() {
        VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        VkImageMemoryBarrier barrier = vks::initializers::imageMemoryBarrier();
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = geometryPass.headIndex.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);

        vulkanDevice->flushCommandBuffer(cmdBuf, queue, true);
    }

    void prepareGeometryPass() {
        createGeometryRenderPass();
        createGeometryFramebuffer();
        createGeometryCounterBuffer();
        createHeadIndexStorageImage();
        createLinkedListStorageBuffer();
        transitionHeadIndexStorageImageLayout();
    }

    void setupDescriptors() {
        // Pool
        std::vector<VkDescriptorPoolSize> poolSizes = {
                vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
                vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1),
                vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3),
                vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2),
        };
        VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

        // Layouts

        // Create a geometry descriptor set layout
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                // renderPassUniformData
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                              0),
                // AtomicSBO
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                // headIndexImage
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 2),
                // LinkedListSBO
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 3),
        };
        VkDescriptorSetLayoutCreateInfo descriptorLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(
                setLayoutBindings);
        VK_CHECK_RESULT(
                vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.geometry));

        // Create a color descriptor set layout
        setLayoutBindings = {
                // headIndexImage
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 0),
                // LinkedListSBO
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 1),
                // renderPassUniformData
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                              2),
        };
        descriptorLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.color));

        // Sets
        VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool,
                                                                                             &descriptorSetLayouts.geometry,
                                                                                             1);

        // Update a geometry descriptor set

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.geometry));

        std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
                // Binding 0: renderPassUniformData
                vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0,
                                                      &renderPassUniformBuffer.descriptor),
                // Binding 2: GeometrySBO
                vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                      &geometryPass.geometry.descriptor),
                // Binding 3: headIndexImage
                vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2,
                                                      &geometryPass.headIndex.descriptor),
                // Binding 4: LinkedListSBO
                vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3,
                                                      &geometryPass.abuffer.descriptor)
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0,
                               nullptr);

        // Update a color descriptor set
        allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.color, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.color));

        writeDescriptorSets = {
                // Binding 0: headIndexImage
                vks::initializers::writeDescriptorSet(descriptorSets.color, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0,
                                                      &geometryPass.headIndex.descriptor),
                // Binding 1: LinkedListSBO
                vks::initializers::writeDescriptorSet(descriptorSets.color, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                      &geometryPass.abuffer.descriptor),
                // Binding 2: renderPassUniformData
                vks::initializers::writeDescriptorSet(descriptorSets.color, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2,
                                                      &renderPassUniformBuffer.descriptor),
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0,
                               nullptr);
    }

    void preparePipelines() {
        // Layouts

        // Create a geometry pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(
                &descriptorSetLayouts.geometry, 1);
        // Static object data passed using push constants
        VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(ObjectData), 0);
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.geometry));

        // Create color pipeline layout
        pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.color, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.color));


        // Pipelines
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
        VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(
                VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
        VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(0,
                                                                                                                   nullptr);
        VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(
                VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
        VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
        VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(
                VK_SAMPLE_COUNT_1_BIT, 0);
        std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(
                dynamicStateEnables);
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.geometry,
                                                                                        geometryPass.renderPass);
        pipelineCI.pInputAssemblyState = &inputAssemblyState;
        pipelineCI.pRasterizationState = &rasterizationState;
        pipelineCI.pColorBlendState = &colorBlendState;
        pipelineCI.pMultisampleState = &multisampleState;
        pipelineCI.pViewportState = &viewportState;
        pipelineCI.pDepthStencilState = &depthStencilState;
        pipelineCI.pDynamicState = &dynamicState;
        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();
        pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState(
                {vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Color});

        // Create a geometry pipeline
        shaderStages[0] = loadShader(getShadersPath() + "oit/geometry.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getShadersPath() + "oit/geometry.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        //

        depthStencilState.stencilTestEnable = VK_TRUE;
        depthStencilState.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;
        depthStencilState.back.failOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        depthStencilState.back.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        depthStencilState.back.compareMask = 0xff;
        depthStencilState.back.writeMask = 0xff;
        depthStencilState.back.reference = 0xff;
        depthStencilState.front = depthStencilState.back;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.geometry));

        //  loop64 (requires int64 + int64 atomics support)
        if (supportsShaderInt64 && supportsShaderBufferInt64Atomics) {
            shaderStages[0] = loadShader(getShadersPath() + "oit/loop64.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
            shaderStages[1] = loadShader(getShadersPath() + "oit/loop64.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.loop64));
        }

        // Create color pipeline
        // stencil > 32
        VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(
                0xf, VK_FALSE);
        colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.color, renderPass);
        pipelineCI.pInputAssemblyState = &inputAssemblyState;
        pipelineCI.pRasterizationState = &rasterizationState;
        pipelineCI.pColorBlendState = &colorBlendState;
        pipelineCI.pMultisampleState = &multisampleState;
        pipelineCI.pViewportState = &viewportState;
        pipelineCI.pDepthStencilState = &depthStencilState;
        pipelineCI.pDynamicState = &dynamicState;
        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();
        pipelineCI.pVertexInputState = &vertexInputInfo;

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 32;
        depthStencilState.front = depthStencilState.back;


        shaderStages[0] = loadShader(getShadersPath() + "oit/color.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        if (supportsShaderInt64) {
            shaderStages[1] = loadShader(getShadersPath() + "oit/rbs_color_128.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            VK_CHECK_RESULT(
                    vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.rbs_color_128));
        }
        shaderStages[1] = loadShader(getShadersPath() + "oit/bma_color_128.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(
                vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.bma_color_128));

        // bitonic
        if (supportsShaderInt64) {
            shaderStages[1] = loadShader(getShadersPath() + "oit/bitonic.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            VK_CHECK_RESULT(
                    vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.bitonic_color));
        }

        // stencil > 16

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 16;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color_32.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color_32));

        // stencil > 8

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 8;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color_16.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color_16));

        // stencil > 4

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 4;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color_8.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color_8));

        // stencil > 0

        depthStencilState.back.compareOp = VK_COMPARE_OP_LESS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 0;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color_4.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color_4));

        // base 32
        shaderStages[1] = loadShader(getShadersPath() + "oit/base_color_32.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(
                vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.base_color_32));

        // baseline

        depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_ZERO;
        depthStencilState.back.reference = 0;
        depthStencilState.front = depthStencilState.back;

        shaderStages[1] = loadShader(getShadersPath() + "oit/color.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.color));

        if (supportsShaderInt64) {
            shaderStages[1] = loadShader(getShadersPath() + "oit/irbs_only.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.irbs_only));
            shaderStages[1] = loadShader(getShadersPath() + "oit/rbs_only.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.rbs_only));
        }


        // kbuf (requires int64 + int64 atomics support)
        if (supportsShaderInt64 && supportsShaderBufferInt64Atomics) {
            shaderStages[0] = loadShader(getShadersPath() + "oit/kbuf_blend.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
            shaderStages[1] = loadShader(getShadersPath() + "oit/kbuf_blend.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
            rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

            VK_CHECK_RESULT(
                    vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.kbuf_blend));
        }


    }

    void buildCommandBuffers() override {
        if (resized)
            return;
        if (oit_alg.empty() || methods.empty()) {
            return;
        }
        if (uiSettings.oitAlgorithm >= static_cast<int>(oit_alg.size())) {
            uiSettings.oitAlgorithm = 0;
        }
        if (uiSettings.optimization >= static_cast<int>(methods.size())) {
            uiSettings.optimization = 0;
        }

        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

        VkClearValue geometry_clearValues[1];
        geometry_clearValues[0].depthStencil = {1.f, 0};

        VkClearValue clearValues[2];
        clearValues[0].color = defaultClearColor;
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;

        VkViewport viewport = vks::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
        VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            // Update dynamic viewport state
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            // Update dynamic scissor state
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            VkClearColorValue clearColor;
            clearColor.uint32[0] = 0xffffffff;

            VkImageSubresourceRange subresRange = {};

            subresRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresRange.levelCount = 1;
            subresRange.layerCount = 1;

            vkCmdClearColorImage(drawCmdBuffers[i], geometryPass.headIndex.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor,
                                 1, &subresRange);

            // Clear previous geometry pass data
            vkCmdFillBuffer(drawCmdBuffers[i], geometryPass.geometry.buffer, 0, sizeof(uint32_t), 0);

            // We need a barrier to make sure all writes are finished before starting to write again
            VkMemoryBarrier memoryBarrier = vks::initializers::memoryBarrier();
            memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

            // Begin the geometry render pass
            renderPassBeginInfo.renderPass = geometryPass.renderPass;
            renderPassBeginInfo.framebuffer = geometryPass.framebuffer;
            renderPassBeginInfo.clearValueCount = 1;
            renderPassBeginInfo.pClearValues = geometry_clearValues;

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            if (oit_alg[uiSettings.oitAlgorithm] == "atomic loop" && pipelines.loop64 != VK_NULL_HANDLE) {
                uint32_t data = 0xFFFFFFFF;  // 32-bit value with all bits set to 1
                vkCmdFillBuffer(drawCmdBuffers[i], geometryPass.abuffer.buffer, 0, geometryPass.abuffer.size, data);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.loop64);
            } else {
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.geometry);
            }

            // Render the scene
            ObjectData objectData;

            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.geometry, 0, 1,
                                    &descriptorSets.geometry, 0, nullptr);

            models.cube.bindBuffers(drawCmdBuffers[i]);
            objectData.color = glm::vec4(1.0f, 1.0f, 1.0f, uiSettings.alpha);

            glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
            glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
            objectData.model = T * S;
            vkCmdPushConstants(drawCmdBuffers[i], pipelineLayouts.geometry,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectData),
                               &objectData);
            models.cube.draw(drawCmdBuffers[i]);


            vkCmdEndRenderPass(drawCmdBuffers[i]);

            // Make a pipeline barrier to guarantee the geometry pass is done
            vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);

            // We need a barrier to make sure all writes are finished before starting to write again
            memoryBarrier = vks::initializers::memoryBarrier();
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

            // Begin the color render pass
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = frameBuffers[i];
            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.color, 0, 1,
                                    &descriptorSets.color, 0, nullptr);
            if (oit_alg[uiSettings.oitAlgorithm] == "linked list") {
                if (methods[uiSettings.optimization] == "base") {
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color);
                    vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                } else if (methods[uiSettings.optimization] == "RBS_only") {
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rbs_only);
                    vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                } else if (methods[uiSettings.optimization] == "iRBS_only") {
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.irbs_only);
                    vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                }
                else {
                    if (methods[uiSettings.optimization] == "iRBS_BMA") {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bitonic_color);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    } else if (methods[uiSettings.optimization] == "RBS_BMA") {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rbs_color_128);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    } else if (methods[uiSettings.optimization] == "BMA") {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bma_color_128);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    }
                    if (uiSettings.onlyOpt32) {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.base_color_32);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    } else {
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color_32);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color_16);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color_8);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.color_4);
                        vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                    }
                }
            } else if (oit_alg[uiSettings.oitAlgorithm] == "atomic loop" && pipelines.kbuf_blend != VK_NULL_HANDLE) {
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.kbuf_blend);
                vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
            }

            if (!uiSettings.exportRequest) {
                drawUI(drawCmdBuffers[i]);
            }

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void updateUniformBuffers() {
        renderPassUniformData.projection = camera.matrices.perspective;
        renderPassUniformData.view = camera.matrices.view;
        renderPassUniformData.viewport = glm::ivec3(width, height, width * height);

        // light source
        if (uiSettings.animateLight) {
            uiSettings.lightTimer += frameTimer * uiSettings.lightSpeed;
            renderPassUniformData.lightPos.x = sin(glm::radians(uiSettings.lightTimer * 360.f)) * 15.0f;
            renderPassUniformData.lightPos.y = cos(glm::radians(uiSettings.lightTimer * 360.f)) * 15.0f;
        }

        VK_CHECK_RESULT(renderPassUniformBuffer.map());
        memcpy(renderPassUniformBuffer.mapped, &renderPassUniformData, sizeof(RenderPassUniformData));
        renderPassUniformBuffer.unmap();

    }

    void prepare() override {
        VulkanExampleBase::prepare();
        loadAssets();
        prepareUniformBuffers();
        prepareGeometryPass();
        setupDescriptors();
        preparePipelines();
        buildCommandBuffers();
        //updateUniformBuffers();
        prepared = true;
    }

    void draw() {
        VulkanExampleBase::prepareFrame();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

        if (uiSettings.exportRequest) {
            exportImage();
            uiSettings.exportRequest = false;
            UIOverlay.updated = true;
        }
        VulkanExampleBase::submitFrame();
    }

    void render() override {
        if (!prepared)
            return;
        updateUniformBuffers();
        draw();
    }

    void windowResized() override {
        destroyGeometryPass();
        prepareGeometryPass();
        vkResetDescriptorPool(device, descriptorPool, 0);
        setupDescriptors();
        resized = false;
        buildCommandBuffers();
    }

    void destroyGeometryPass() {
        vkDestroyRenderPass(device, geometryPass.renderPass, nullptr);
        vkDestroyFramebuffer(device, geometryPass.framebuffer, nullptr);
        geometryPass.geometry.destroy();
        geometryPass.headIndex.destroy();
        geometryPass.abuffer.destroy();
        geometryPass.renderPass = VK_NULL_HANDLE;
        geometryPass.framebuffer = VK_NULL_HANDLE;
    }

    void rePreparePipeline() {
        destroyPipelines();
        preparePipelines();
        UIOverlay.updated = true;
    }

    void exportImage() {
        // 获取当前帧缓冲的图像
        VkImage srcImage = swapChain.buffers[currentBuffer].image;

        // 创建临时的 VkImage 和 VkDeviceMemory
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = swapChain.colorFormat;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImage dstImage;
        vkCreateImage(device, &imageInfo, nullptr, &dstImage);

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, dstImage, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDeviceMemory dstImageMemory;
        vkAllocateMemory(device, &allocInfo, nullptr, &dstImageMemory);
        vkBindImageMemory(device, dstImage, dstImageMemory, 0);

        // 将当前帧缓冲的图像复制到临时图像中
        VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        VkImageMemoryBarrier srcBarrier = {};
        srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        srcBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.image = srcImage;
        srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        srcBarrier.subresourceRange.baseMipLevel = 0;
        srcBarrier.subresourceRange.levelCount = 1;
        srcBarrier.subresourceRange.baseArrayLayer = 0;
        srcBarrier.subresourceRange.layerCount = 1;
        srcBarrier.srcAccessMask = 0;
        srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &srcBarrier
        );

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = dstImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
        );

        VkImageCopy imageCopyRegion = {};
        imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.srcSubresource.layerCount = 1;
        imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.dstSubresource.layerCount = 1;
        imageCopyRegion.extent.width = width;
        imageCopyRegion.extent.height = height;
        imageCopyRegion.extent.depth = 1;

        vkCmdCopyImage(
                commandBuffer,
                srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &imageCopyRegion
        );

        srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcBarrier.dstAccessMask = 0;

        vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &srcBarrier
        );

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

        vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
        );

        vulkanDevice->flushCommandBuffer(commandBuffer, queue, true);

        // 将图像数据映射到主机内存并保存到文件
        void* data;
        vkMapMemory(device, dstImageMemory, 0, VK_WHOLE_SIZE, 0, &data);

        VkImageSubresource imageSubresource{};
        imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageSubresource.mipLevel = 0;
        imageSubresource.arrayLayer = 0;
        VkSubresourceLayout subresourceLayout{};
        vkGetImageSubresourceLayout(device, dstImage, &imageSubresource, &subresourceLayout);

        const uint8_t* mappedData = static_cast<const uint8_t*>(data) + subresourceLayout.offset;
        const bool swapBAndR = (swapChain.colorFormat == VK_FORMAT_B8G8R8A8_UNORM) ||
                               (swapChain.colorFormat == VK_FORMAT_B8G8R8A8_SRGB);

        std::vector<uint8_t> rgbaPixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* srcRow = mappedData + static_cast<size_t>(y) * subresourceLayout.rowPitch;
            uint8_t* dstRow = rgbaPixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t* src = srcRow + static_cast<size_t>(x) * 4u;
                uint8_t* dst = dstRow + static_cast<size_t>(x) * 4u;
                if (swapBAndR) {
                    dst[0] = src[2];
                    dst[1] = src[1];
                    dst[2] = src[0];
                    dst[3] = src[3];
                } else {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = src[3];
                }
            }
        }

        stbi_write_png("exported_image.png", width, height, 4, rgbaPixels.data(), static_cast<int>(width) * 4);
        vkUnmapMemory(device, dstImageMemory);

        // 清理资源
        vkDestroyImage(device, dstImage, nullptr);
        vkFreeMemory(device, dstImageMemory, nullptr);
    }

    void OnUpdateUIOverlay(vks::UIOverlay *overlay) override {
        if (oit_alg.empty() || methods.empty()) {
            return;
        }
        if (uiSettings.oitAlgorithm >= static_cast<int>(oit_alg.size())) {
            uiSettings.oitAlgorithm = 0;
        }
        if (uiSettings.optimization >= static_cast<int>(methods.size())) {
            uiSettings.optimization = 0;
        }

        if (overlay->header("Algorithm Settings")) {
            if (overlay->comboBox("Oit algorithm", &uiSettings.oitAlgorithm, oit_alg)) {
                buildCommandBuffers();
            }

            if (oit_alg[uiSettings.oitAlgorithm] == "linked list") {
                if (overlay->comboBox("optimization", &uiSettings.optimization, methods)) {
                    buildCommandBuffers();
                }
                if (drawIntControl("MAX_FRAGMENT_COUNT", &uiSettings.MAX_FRAGMENT_COUNT, 1, 128)) {
                    compileLinkedListColorShader();
                    if (uiSettings.unrollBubble) {
                        compileBubbleShaders();
                    }
                    rePreparePipeline();
                }

                if (overlay->checkBox("Only optimize 32+", &uiSettings.onlyOpt32)) {
                    buildCommandBuffers();
                }
                if (overlay->checkBox("Unroll Bubble Sort", &uiSettings.unrollBubble)) {
                    compileBubbleShaders();
                    rePreparePipeline();
                }

            } else if (oit_alg[uiSettings.oitAlgorithm] == "atomic loop") {
                if (drawIntControl("OIT_LAYERS", &uiSettings.OIT_LAYERS, 1, 32)) {
                    compileAtomicLoopShaders();
                    rePreparePipeline();
                }
            }

        }
        if (overlay->header("Render Pass Uniform Settings")) {
            if (overlay->sliderFloat("Alpha", &uiSettings.alpha, 0.0f, 1.0f)) {
                buildCommandBuffers();
            }
            overlay->checkBox("Animate light", &uiSettings.animateLight);
            overlay->inputFloat("Light speed", &uiSettings.lightSpeed, 0.1f, 2);  // 假设步长为0.1，精度为2位小数
            if (overlay->button("print camera")) {
                //const Camera::Matrices& m = camera.matrices;
                std::cout << "position\n";
                std::cout << camera.position.x << " " << camera.position.y << " " << camera.position.z << "\n";
                std::cout << "rotation\n";
                std::cout << camera.rotation.x << " " << camera.rotation.y << " " << camera.rotation.z << "\n";
                //std::cout << camera.fov << " " << camera.znear << " " << camera.zfar;
            }

            if (overlay->button("export picture")) {
                //exportImage();
                uiSettings.exportRequest = true;
            }
        }
    }

    void keyPressed(uint32_t keycode) override {
        if (keycode == KEY_SPACE) {
            UIOverlay.visible = !UIOverlay.visible;
            UIOverlay.updated = true;
        }
        VulkanExampleBase::keyPressed(keycode);
    }
};

VULKAN_EXAMPLE_MAIN()

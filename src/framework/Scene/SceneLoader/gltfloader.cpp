//
// Created by pc on 2023/8/27.
//
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "gltfloader.h"

#include "Common/Timer.h"
#include "Common/samplerCPP/ThreadPool.h"
#include "Core/Descriptor/DescriptorSet.h"
#include "Core/Buffer.h"
#include "Core/RenderContext.h"
#include "Core/math.h"

#include <ctpl_stl.h>
#include <set>

inline std::vector<uint8_t> convertUnderlyingDataStride(const std::vector<uint8_t>& src_data, uint32_t src_stride, uint32_t dst_stride) {
    auto elem_count = toUint32(src_data.size()) / src_stride;

    std::vector<uint8_t> result(elem_count * dst_stride);
    uint32_t             copy_stride = std::min(src_stride, dst_stride);

    for (uint32_t idxSrc = 0, idxDst = 0;
         idxSrc < src_data.size() && idxDst < result.size();
         idxSrc += src_stride, idxDst += dst_stride) {
        std::copy_n(src_data.begin() + idxSrc, copy_stride, result.begin() + idxDst);
        //   std::copy(src_data.begin() + idxSrc, src_data.begin() + idxSrc + src_stride, result.begin() + idxDst);
    }

    return result;
}

static std::unordered_map<VkFormat, uint32_t>    formatStrideMap    = {{VK_FORMAT_R8_UINT, 1}, {VK_FORMAT_R16_UINT, 2}, {VK_FORMAT_R32_UINT, 4}};
static std::unordered_map<VkFormat, VkIndexType> formatIndexTypeMap = {{VK_FORMAT_R16_UINT, VK_INDEX_TYPE_UINT16}, {VK_FORMAT_R32_UINT, VK_INDEX_TYPE_UINT32}};

static std::unordered_map<VkIndexType, uint32_t> indexStrideMap = {{VK_INDEX_TYPE_UINT16, 2}, {VK_INDEX_TYPE_UINT32, 4}};

inline std::vector<uint8_t> convertIndexData(const std::vector<uint8_t>& src_data, VkIndexType targetType, VkFormat format) {
    if (targetType == VK_INDEX_TYPE_NONE_KHR) {
        switch (format) {
            case VK_FORMAT_R8_UINT: {
                //  type = VK_INDEX_TYPE_UINT16;
                return convertUnderlyingDataStride(src_data, 1, 2);
            }
            case VK_FORMAT_R16_UINT:
                //    type = VK_INDEX_TYPE_UINT16;
                return src_data;
            case VK_FORMAT_R32_UINT:
                //    type = VK_INDEX_TYPE_UINT32;
                return src_data;
        }
    Default:
        LOGE("Unsupported index format {} ", uint32(format))
    } else {
        //type            = targetType;
        float srcStride = formatStrideMap.at(format);
        float dstStride = indexStrideMap.at(targetType);
        if (srcStride == dstStride) {
            return src_data;
        }
        return convertUnderlyingDataStride(src_data, srcStride, dstStride);
    }
}

inline VkFormat getAttributeFormat(const tinygltf::Model* model, uint32_t accessorId) {
    assert(accessorId < model->accessors.size());
    auto& accessor = model->accessors[accessorId];

    VkFormat format;

    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            static const std::map<int, VkFormat> mapped_format = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R8_SINT},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R8G8_SINT},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R8G8B8_SINT},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R8G8B8A8_SINT}};

            format = mapped_format.at(accessor.type);

            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            static const std::map<int, VkFormat> mapped_format = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R8_UINT},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R8G8_UINT},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R8G8B8_UINT},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R8G8B8A8_UINT}};

            static const std::map<int, VkFormat> mapped_format_normalize = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R8_UNORM},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R8G8_UNORM},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R8G8B8_UNORM},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R8G8B8A8_UNORM}};

            if (accessor.normalized) {
                format = mapped_format_normalize.at(accessor.type);
            } else {
                format = mapped_format.at(accessor.type);
            }

            break;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            static const std::map<int, VkFormat> mapped_format = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R8_SINT},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R8G8_SINT},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R8G8B8_SINT},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R8G8B8A8_SINT}};

            format = mapped_format.at(accessor.type);

            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            static const std::map<int, VkFormat> mapped_format = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R16_UINT},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R16G16_UINT},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R16G16B16_UINT},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R16G16B16A16_UINT}};

            static const std::map<int, VkFormat> mapped_format_normalize = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R16_UNORM},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R16G16_UNORM},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R16G16B16_UNORM},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R16G16B16A16_UNORM}};

            if (accessor.normalized) {
                format = mapped_format_normalize.at(accessor.type);
            } else {
                format = mapped_format.at(accessor.type);
            }

            break;
        }
        case TINYGLTF_COMPONENT_TYPE_INT: {
            static const std::map<int, VkFormat> mapped_format = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R32_SINT},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R32G32_SINT},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R32G32B32_SINT},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R32G32B32A32_SINT}};

            format = mapped_format.at(accessor.type);

            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            static const std::map<int, VkFormat> mapped_format = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R32_UINT},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R32G32_UINT},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R32G32B32_UINT},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R32G32B32A32_UINT}};

            format = mapped_format.at(accessor.type);

            break;
        }
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            static const std::map<int, VkFormat> mapped_format = {
                {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R32_SFLOAT},
                {TINYGLTF_TYPE_VEC2, VK_FORMAT_R32G32_SFLOAT},
                {TINYGLTF_TYPE_VEC3, VK_FORMAT_R32G32B32_SFLOAT},
                {TINYGLTF_TYPE_VEC4, VK_FORMAT_R32G32B32A32_SFLOAT}};

            format = mapped_format.at(accessor.type);

            break;
        }
        default: {
            format = VK_FORMAT_UNDEFINED;
            break;
        }
    }

    return format;
};

struct Skin;

struct Mesh {
    std::string name;
    Device&     device;

    struct
    {
        std::unique_ptr<Buffer>        buffer{nullptr};
        std::unique_ptr<DescriptorSet> descriptorSet{nullptr};
    } uniformBuffer;

    struct UniformBlock {
        glm::mat4 matrix;
        glm::mat4 jointMatrix[64]{};
        float     jointcount{0};
    } uniformBlock;

    Mesh(Device& device, glm::mat4 matrix);

    std::vector<std::unique_ptr<Primitive>> primitives;
};

struct Node {
    Node*              parent;
    uint32_t           index;
    std::vector<Node*> children;
    glm::mat4          matrix;
    std::string        name;
    Mesh*              mesh;
    Skin*              skin;
    int32_t            skinIndex = -1;
    glm::vec3          translation{};
    glm::vec3          scale{1.0f};
    glm::quat          rotation{};

    glm::mat4 localMatrix();

    glm::mat4 getMatrix();

    void update();

    ~Node();
};

struct Skin {
    std::string            name;
    Node*                  skeletonRoot = nullptr;
    std::vector<glm::mat4> inverseBindMatrices;
    std::vector<Node*>     joints;
};

struct AnimationChannel {
    enum PathType {
        TRANSLATION,
        ROTATION,
        SCALE
    };

    PathType path;
    Node*    node;
    uint32_t samplerIndex;
};

/*
    glTF animation sampler
*/
struct AnimationSampler {
    enum InterpolationType {
        LINEAR,
        STEP,
        CUBICSPLINE
    };

    InterpolationType      interpolation;
    std::vector<float>     inputs;
    std::vector<glm::vec4> outputsVec4;
};

/*
glTF animation
*/
struct Animation {
    std::string                   name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
    float                         start = std::numeric_limits<float>::max();
    float                         end   = std::numeric_limits<float>::min();
};

VkBufferUsageFlags getBufferUsageFlags(const SceneLoadingConfig& config) {
    VkBufferUsageFlags vkBufferUsageFlags{};
    if (config.bufferAddressAble) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (config.bufferForAccel) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (config.bufferForTransferSrc) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (config.bufferForTransferDst) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (config.bufferForStorage) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    return vkBufferUsageFlags;
}

struct GLTFLoadingImpl {
    std::vector<std::unique_ptr<Primitive>> primitives;

    SceneLoadingConfig config;

    Device& device;

    std::vector<std::unique_ptr<Texture>>                            textures;
    std::vector<SgLight>                                             lights;
    std::vector<GltfMaterial>                                        materials;
    std::unordered_map<const tinygltf::Node*, Transform>             modelTransforms;
    std::unordered_map<const tinygltf::Node*, const tinygltf::Node*> nodeParentMap;

    std::unordered_map<int, int> texIndexRemap;
    int                          cur_tex_index{0};

    std::vector<std::shared_ptr<Camera>> cameras;
    BBox sceneBBox;
    bool metallicRoughnessWorkflow;

    GLTFLoadingImpl(Device& device, const std::string& path, const SceneLoadingConfig& config, Scene* scene);
    ~GLTFLoadingImpl();

    const Texture* getTexture(uint32_t idx) const;
    void           loadImages(const std::filesystem::path& modelPath, std::shared_ptr<tinygltf::Model> model);
    void           loadNode(const tinygltf::Node& node, const tinygltf::Model& model);
    void           loadFromFile(const std::string& path);
    void           loadMaterials(tinygltf::Model& gltfModel);
    void           loadCameras(const tinygltf::Model& model);
    void           loadLights(const tinygltf::Model& model);
    int            requestTexture(int tex, const tinygltf::Model& model);

    void      process(const tinygltf::Model& model);
    void      processNode(const tinygltf::Node& node, const tinygltf::Model& model);
    Transform getTransform(const tinygltf::Node& node);
    void      GetModelTransforms(const tinygltf::Model& model);
    void      transformNodes(const std::vector<tinygltf::Node>& nodes, const tinygltf::Node& node);

    // std::unique_ptr<Buffer> sceneVertexBuffer{nullptr};

    //Scene attributes
    std::unordered_map<std::string, VertexAttribute>         vertexAttributes;
    std::unordered_map<std::string, std::unique_ptr<Buffer>> sceneVertexBuffer;
    std::unique_ptr<Buffer>                                  sceneIndexBuffer{nullptr};
    std::unique_ptr<Buffer>                                  sceneUniformBuffer{nullptr};
    std::unique_ptr<Buffer>                                  scenePrimitiveIdBuffer{nullptr};

    VkIndexType indexType{VK_INDEX_TYPE_UINT16};
    uint32_t    mVertexCount{0}, mIndexCount{0};

    struct Accessor {
        uint32_t vertexOffset;
        uint32_t accessor;
    };

    //Offset ——> accessor
    //accessor may repeat
    std::map<std::string, std::map<uint32_t, uint32_t>> gltfVertexAccessors;
    std::map<uint32_t, uint32_t>                        gltfIndexAccessors;
    std::map<uint32_t, uint32_t>                        gltfIndexToVertexOffset;

    std::map<std::uint32_t, std::vector<const tinygltf::Primitive*>> gltfPrimitives;

    Scene* sceneToLoad;
};

bool loadImageDataFunc(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData) {
    // KTX files will be handled by our own code
    auto imageEXT = FileUtils::getFileExt(image->uri);
    if (imageEXT == "ktx" || imageEXT == "dds") {
        //  if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx") {
        return true;
        //  }
    }

    return LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
}

void GLTFLoadingImpl::loadCameras(const tinygltf::Model& model) {
    for (auto& gltfCamera : model.cameras) {
        auto camera = std::make_shared<Camera>();
        camera->setPerspective(math::toDegrees(gltfCamera.perspective.yfov), gltfCamera.perspective.aspectRatio, gltfCamera.perspective.znear, gltfCamera.perspective.zfar);
        cameras.push_back(camera);
    }
    if (model.cameras.empty()) {
        LOGI("No camera found in gltf file, using default camera");
        auto camera = std::make_shared<Camera>();
        camera->setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 4000.0f);
        camera->getTransform()->setPosition({0.0f, 0.0f, 5.0f});
        camera->setScreenSize(1920,1080);
        cameras.push_back(camera);
    }
}

void GLTFLoadingImpl::loadLights(const tinygltf::Model& model) {
    if (!config.loadLight)
        return;
    if (model.extensions.contains("KHR_lights_punctual")) {
        auto& khrLightsPunctual = model.extensions.at("KHR_lights_punctual");
        auto  khrLights         = khrLightsPunctual.Get("lights");
        for (int i = 0; i < khrLights.Size(); i++) {
            auto& khr_light = khrLights.Get(i);

            SgLight sgLight;
            if (khr_light.Has("color")) {
                sgLight.lightProperties.color = glm::vec3(
                    static_cast<float>(khr_light.Get("color").Get(0).Get<double>()),
                    static_cast<float>(khr_light.Get("color").Get(1).Get<double>()),
                    static_cast<float>(khr_light.Get("color").Get(2).Get<double>()));
            }
            if (khr_light.Has("intensity")) {
                sgLight.lightProperties.intensity = static_cast<float>(khr_light.Get("intensity").Get<double>());
            }
            if (khr_light.Has("range")) {
                sgLight.lightProperties.range = static_cast<float>(khr_light.Get("range").Get<double>());
            }
            if (khr_light.Has("spot")) {
                sgLight.lightProperties.inner_cone_angle = static_cast<float>(khr_light.Get("spot").Get("innerConeAngle").Get<double>());
                sgLight.lightProperties.outer_cone_angle = static_cast<float>(khr_light.Get("spot").Get("outerConeAngle").Get<double>());
            }
            std::string lightType = khr_light.Get("type").Get<std::string>();
            if (lightType == "directional") {
                sgLight.type = LIGHT_TYPE::Directional;
            } else if (lightType == "point") {
                sgLight.type = LIGHT_TYPE::Point;
            } else if (lightType == "spot") {
                sgLight.type = LIGHT_TYPE::Spot;
            }
            sgLight.lightProperties.direction = vec4(0.0f, 0.0f, 1.0f, 0.0f);
            this->lights.push_back(sgLight);
        }
    }

    if(config.loadLight && lights.empty()) {
        LOGI("No light found in gltf file, using default light");
        SgLight sgLight;
        sgLight.lightProperties.color = glm::vec3(1.0f, 1.0f, 1.0f);
        sgLight.lightProperties.intensity = 1.0f;
        sgLight.lightProperties.position = sceneBBox.center()+vec3(0,sceneBBox.extent().y/5.f * 3,0);
        sgLight.lightProperties.direction = glm::normalize(glm::vec3(-1, -1, -1));
        sgLight.type = LIGHT_TYPE::Directional;
        this->lights.push_back(sgLight);
    }
}
int GLTFLoadingImpl::requestTexture(int tex, const tinygltf::Model& model) {
    if (tex == -1)
        return -1;
    auto image = model.textures[tex].source;
    if (texIndexRemap.contains(image))
        return texIndexRemap[image];
    texIndexRemap[image] = cur_tex_index++;
    return texIndexRemap[image];
}
void GLTFLoadingImpl::process(const tinygltf::Model& model) {
    GetModelTransforms(model);
    for (auto& node : model.nodes) {
        processNode(node, model);
    }

    if (primitives.size() > 100 && config.enableMergeDrawCalls) {
        sceneToLoad->setMergeDrawCall(true);
    }

    std::unordered_map<std::string, std::unique_ptr<Buffer>> stagingBuffers;
    for (auto attribute : vertexAttributes) {
        auto stagingBuffer = std::make_unique<Buffer>(device, attribute.second.stride * mVertexCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        for (auto accessor : gltfVertexAccessors.at(attribute.first)) {
            auto                        accessorId   = accessor.second;
            auto                        vertexOffset = accessor.first;
            const tinygltf::Accessor&   gltfAccessor = model.accessors[accessorId];
            const tinygltf::BufferView& view         = model.bufferViews[gltfAccessor.bufferView];
            auto                        startByte    = gltfAccessor.byteOffset + view.byteOffset;
            auto                        endByte      = startByte + gltfAccessor.ByteStride(view) * gltfAccessor.count;

            std::vector<uint8_t> data(model.buffers[view.buffer].data.begin() + startByte,
                                      model.buffers[view.buffer].data.begin() + endByte);
            stagingBuffer->uploadData(data.data(), data.size(), vertexOffset * attribute.second.stride);
        }
        stagingBuffers[attribute.first] = std::move(stagingBuffer);
    }

    auto                 size                    = mIndexCount * indexStrideMap.at(indexType);
    auto                 sceneIndexStagingBuffer = std::make_unique<Buffer>(device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    std::vector<uint8_t> indexData;
    for (auto accessor : gltfIndexAccessors) {
        auto                        accessorId   = accessor.second;
        auto                        indexOffset  = accessor.first;
        const tinygltf::Accessor&   gltfAccessor = model.accessors[accessorId];
        const tinygltf::BufferView& view         = model.bufferViews[gltfAccessor.bufferView];
        auto                        startByte    = gltfAccessor.byteOffset + view.byteOffset;
        auto                        endByte      = startByte + gltfAccessor.ByteStride(view) * gltfAccessor.count;
        std::vector<uint8_t>        data(model.buffers[view.buffer].data.begin() + startByte,
                                  model.buffers[view.buffer].data.begin() + endByte);
        indexData = convertIndexData(data, indexType, getAttributeFormat(&model, accessorId));

        if (sceneToLoad->getMergeDrawCall()) {
            uint32_t*           dataU32 = reinterpret_cast<uint32_t*>(indexData.data());
            std::vector<uint32> indexBuffer(dataU32, dataU32 + indexData.size() / 4);
            for (int i = indexOffset; i < indexOffset + indexData.size() / 4; i++) {
                dataU32[i - indexOffset] += gltfIndexToVertexOffset[indexOffset];
            }
        }

        sceneIndexStagingBuffer->uploadData(indexData.data(), indexData.size(), indexOffset * indexStrideMap.at(indexType));
    }

    auto commandBuffer = device.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true, VK_QUEUE_TRANSFER_BIT);

    sceneIndexBuffer = Buffer::FromBuffer(device, commandBuffer, *sceneIndexStagingBuffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | getBufferUsageFlags(config));
    for (auto& attribute : vertexAttributes) {
        // CommandBuffer tempBuffer = device.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true, VK_QUEUE_TRANSFER_BIT);
        sceneVertexBuffer[attribute.first] = Buffer::FromBuffer(device, commandBuffer, *stagingBuffers[attribute.first], VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | getBufferUsageFlags(config));
        // g_context->submit(tempBuffer);
    }

    std::unique_ptr<Buffer> stagingPrimitiveIDBuffer = nullptr;
    if (sceneToLoad->getMergeDrawCall()) {
        stagingPrimitiveIDBuffer = std::make_unique<Buffer>(device, sizeof(uint32_t) * sceneVertexBuffer[POSITION_ATTRIBUTE_NAME]->getSize() / sizeof(vec3), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        std::vector<uint> primitiveIDs(sceneVertexBuffer[POSITION_ATTRIBUTE_NAME]->getSize() / sizeof(vec3));
        int               id     = 0;
        int               offset = 0;
        for (const auto& primitive : primitives) {
            for (int i = 0; i < primitive->vertexCount; i++) {
                primitiveIDs[offset + i] = id;
            }
            id++;
            offset += primitive->vertexCount;
        }
        stagingPrimitiveIDBuffer->uploadData(primitiveIDs.data(), primitiveIDs.size() * sizeof(uint32_t), 0);
        scenePrimitiveIdBuffer = Buffer::FromBuffer(device, commandBuffer, *stagingPrimitiveIDBuffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | getBufferUsageFlags(config));
    } else {
        std::vector<int> primitiveIDs;
        int              id = 0;
        for (const auto& primitive : primitives) {
            primitiveIDs.push_back(id++);
        }
        stagingPrimitiveIDBuffer = std::make_unique<Buffer>(device, primitiveIDs.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, primitiveIDs.data());
        stagingPrimitiveIDBuffer->uploadData(primitiveIDs.data(), primitiveIDs.size() * sizeof(uint32_t), 0);
        scenePrimitiveIdBuffer = Buffer::FromBuffer(device, commandBuffer, *stagingPrimitiveIDBuffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | getBufferUsageFlags(config));
    }
    g_context->submit(commandBuffer);
}
Transform GLTFLoadingImpl::getTransform(const tinygltf::Node& node) {
    Transform transform;
    if (!node.matrix.empty()) {
        glm::mat4 matrix = glm::make_mat4x4(node.matrix.data());
        transform.setLocalToWorldMatrix(glm::scale(matrix, config.sceneScale));
        transform.setLocalToWorldMatrix(matrix);
    } else {
        vec3 translation = config.sceneTranslation;
        if (node.translation.size() == 3) {
            translation += glm::make_vec3(node.translation.data());
        }
        transform.setPosition(translation);

        glm::quat rotation = config.sceneRotation;
        if (node.rotation.size() == 4) {
            glm::quat rota = glm::make_quat(node.rotation.data());
            rotation       = rotation * rota;
        }
        transform.setRotation(rotation);

        glm::vec3 scale = config.sceneScale;
        if (node.scale.size() == 3) {
            scale *= glm::make_vec3(node.scale.data());
        }
        transform.setLocalScale(scale);
    }
    return transform;
}

void GLTFLoadingImpl::transformNodes(const std::vector<tinygltf::Node>& nodes, const tinygltf::Node& node) {
    for (auto child : node.children) {
        nodeParentMap[&nodes[child]] = &node;
        // modelTransforms[&nodes[child]].setParent(&modelTransforms[&node]);
        transformNodes(nodes, nodes[child]);
    }
}

void GLTFLoadingImpl::GetModelTransforms(const tinygltf::Model& model) {
    auto& nodes = model.nodes;
    for (const auto& node : nodes) {
        modelTransforms[&node] = getTransform(node);
    }
    // for (const auto& node : model.scenes[0].nodes) {
    for (const auto& node : model.nodes) {
        transformNodes(nodes, node);
    }

    for (auto& [node, parent] : nodeParentMap) {
        // auto & parent = nodeParentMap[node];
        glm::mat4 matrix = modelTransforms[node].getLocalToWorldMatrix();
        while (parent) {
            matrix = modelTransforms[parent].getLocalToWorldMatrix() * matrix;
            parent = nodeParentMap.count(parent) ? nodeParentMap[parent] : nullptr;
        }
        modelTransforms[node].setLocalToWorldMatrix(matrix);
    }
}

const tinygltf::Value* get_extension(const tinygltf::ExtensionMap& tinygltf_extensions, const std::string& extension) {
    auto it = tinygltf_extensions.find(extension);
    if (it != tinygltf_extensions.end()) {
        return &it->second;
    } else {
        return nullptr;
    }
}

void GLTFLoadingImpl::processNode(const tinygltf::Node& node, const tinygltf::Model& model) {
    if (node.mesh > -1) {
        const tinygltf::Mesh mesh = model.meshes[node.mesh];
        for (size_t j = 0; j < mesh.primitives.size(); j++) {
            const tinygltf::Primitive& primitive = mesh.primitives[j];
            if (primitive.indices < 0) {
                continue;
            }
            if (vertexAttributes.empty()) {
                for (const auto& attr : primitive.attributes) {
                    std::string attributeName = attr.first;
                    std::ranges::transform(attributeName.begin(), attributeName.end(), attributeName.begin(), ::tolower);
                    bool attr_need_to_load = config.requiredVertexAttribute.empty() || config.requiredVertexAttribute.contains(attributeName);
                    if (!attr_need_to_load) continue;

                    const tinygltf::Accessor&   accessor = model.accessors[attr.second];
                    const tinygltf::BufferView& view     = model.bufferViews[accessor.bufferView];
                    uint32_t                    stride   = accessor.ByteStride(view);

                    VertexAttribute attribute{};
                    attribute.format                = getAttributeFormat(&model, attr.second);
                    attribute.stride                = stride;
                    vertexAttributes[attributeName] = attribute;
                }
                auto indexFormat = getAttributeFormat(&model, primitive.indices);
                indexType        = config.indexType == VK_INDEX_TYPE_NONE_KHR ? formatIndexTypeMap.at(indexFormat) : config.indexType;
            }
            assert(primitive.attributes.find("POSITION") != primitive.attributes.end());
            const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find(
                                                                                            "POSITION")
                                                                        ->second];
            auto                      posMin      = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
            auto                      posMax      = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

            uint32_t primVertexCount = posAccessor.count;
            bool     loadNewVertex   = true;
            bool     loadNewIndex    = true;

            uint32_t vertexOffset{0}, indexOffset{0};
            for (const auto& attr : primitive.attributes) {
                std::string attributeName = attr.first;
                std::ranges::transform(attributeName.begin(), attributeName.end(), attributeName.begin(), ::tolower);
                auto& vertexAccessors                = gltfVertexAccessors[attributeName];
                bool  attr_need_to_load              = config.requiredVertexAttribute.empty() || config.requiredVertexAttribute.contains(attributeName);
                vertexOffset                         = mVertexCount;
                const tinygltf::Accessor&   accessor = model.accessors[attr.second];
                const tinygltf::BufferView& view     = model.bufferViews[accessor.bufferView];
                if (attr_need_to_load) {
                    vertexAccessors[mVertexCount] = attr.second;
                }
            }

            const tinygltf::Accessor& accessor               = model.accessors[primitive.indices];
            uint32_t                  curPrimitiveIndexCount = accessor.count;

            gltfIndexAccessors[mIndexCount]      = primitive.indices;
            gltfIndexToVertexOffset[mIndexCount] = mVertexCount;
            indexOffset                          = mIndexCount;

            uint32_t curIndexCount = loadNewIndex ? curPrimitiveIndexCount : gltfIndexAccessors[primitive.indices];
            uint     materialIdx   = primitive.material == -1 ? 0 : primitive.material;
            auto     newPrimitive  = std::make_unique<Primitive>(vertexOffset, indexOffset, primVertexCount, curPrimitiveIndexCount, materialIdx);
            if (loadNewVertex) mVertexCount += primVertexCount;
            if (loadNewIndex) mIndexCount += curPrimitiveIndexCount;

            LOGI("Primitive {} has {} vertices and {} indices {} {} material_idx", j, primVertexCount, curPrimitiveIndexCount, mIndexCount, primitive.material)
            auto transform          = modelTransforms[&node];
            auto min = transform.getLocalToWorldMatrix() * glm::vec4(posMin, 1.0f);
            auto max = transform.getLocalToWorldMatrix() * glm::vec4(posMax, 1.0f);
            auto tempMin = glm::vec3(std::min(min.x, max.x), std::min(min.y, max.y), std::min(min.z, max.z));
            auto tempMax = glm::vec3(std::max(min.x, max.x), std::max(min.y, max.y), std::max(min.z, max.z));
            newPrimitive->setDimensions(tempMin, tempMax);
            newPrimitive->transform = transform;
            sceneBBox.unite(newPrimitive->getDimensions());
            //     primitiveUniforms.push_back(newPrimitive->GetPerPrimitiveUniform());
            primitives.push_back(std::move(newPrimitive));
            gltfPrimitives[primitive.indices].emplace_back(&primitive);
        }
    }
    if (config.loadLight) {
        if (const auto extension = get_extension(node.extensions, "KHR_lights_punctual")) {
            int   light_index                            = extension->Get("light").Get<int>();
            auto& transform                              = modelTransforms[&node];
            lights[light_index].lightProperties.position = transform.getPosition();
        }
    }
}

void GLTFLoadingImpl::loadNode(const tinygltf::Node& node, const tinygltf::Model& model) {
    // Node with children
    if (node.children.size() > 0) {
        for (auto i = 0; i < node.children.size(); i++) {
            loadNode(model.nodes[node.children[i]], model);
        }
    }

    if (node.camera > -1 && !cameras.empty()) {
        auto& camera = cameras[node.camera];

        if (!node.translation.empty())
            camera->setTranslation(glm::make_vec3(node.translation.data()));

        //todo set rotation
        // glm::quat rotation;
        // std::ranges::transform(node.rotation.begin(),node.rotation.end(), glm::value_ptr(rotation), TypeCast<double, float>{});
    }

    // Node contains mesh data
    if (node.mesh > -1) {
        const tinygltf::Mesh mesh = model.meshes[node.mesh];
        for (size_t j = 0; j < mesh.primitives.size(); j++) {
            const tinygltf::Primitive& primitive = mesh.primitives[j];
            if (primitive.indices < 0) {
                continue;
            }
            uint32_t  vertexCount = 0;
            glm::vec3 posMin{};
            glm::vec3 posMax{};

            const tinygltf::Accessor& accessor   = model.accessors[primitive.indices];
            uint32_t                  indexCount = accessor.count;

            const auto& positionAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
            vertexCount                  = static_cast<uint32_t>(positionAccessor.count);

            auto materialIndex = primitive.material == -1 ? 0 : primitive.material;
            auto newPrimitive  = std::make_unique<Primitive>(0, 0, vertexCount, indexCount, materialIndex);
            // Vertices
            {
                for (const auto& attr : primitive.attributes) {
                    std::string attributeName = attr.first;
                    std::ranges::transform(attributeName.begin(), attributeName.end(), attributeName.begin(), ::tolower);

                    if (config.requiredVertexAttribute.empty() || config.requiredVertexAttribute.contains(attributeName)) {
                        const tinygltf::Accessor&   accessor = model.accessors[attr.second];
                        const tinygltf::BufferView& view     = model.bufferViews[accessor.bufferView];

                        auto startByte = accessor.byteOffset + view.byteOffset;
                        auto endByte   = startByte + accessor.ByteStride(view) * accessor.count;

                        std::vector<uint8_t> data(model.buffers[view.buffer].data.begin() + startByte,
                                                  model.buffers[view.buffer].data.begin() + endByte);

                        VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | getBufferUsageFlags(config);

                        auto buffer = std::make_unique<Buffer>(device, DATA_SIZE(data), bufferUsageFlags, VMA_MEMORY_USAGE_CPU_TO_GPU);
                        buffer->uploadData(data.data(), DATA_SIZE(data));

                        newPrimitive->setVertexBuffer(attributeName, buffer);

                        VertexAttribute attribute{};
                        attribute.format = getAttributeFormat(&model, attr.second);
                        attribute.stride = accessor.ByteStride(view);

                        newPrimitive->setVertxAttribute(attributeName, attribute);
                        newPrimitive->transform = modelTransforms[&node];
                    }
                }

                if (config.requiredVertexAttribute.empty() || config.requiredVertexAttribute.contains("indices")) {
                    const tinygltf::Accessor&   accessor = model.accessors[primitive.indices];
                    const tinygltf::BufferView& view     = model.bufferViews[accessor.bufferView];

                    auto startByte = accessor.byteOffset + view.byteOffset;
                    auto endByte   = startByte + accessor.ByteStride(view) * accessor.count;

                    std::vector<uint8_t> data(model.buffers[view.buffer].data.begin() + startByte,
                                              model.buffers[view.buffer].data.begin() + endByte);

                    data = convertIndexData(data, config.indexType, getAttributeFormat(&model, primitive.indices));
                    newPrimitive->setIndexType(config.indexType);

                    VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | getBufferUsageFlags(config);

                    auto indexBuffer = std::make_unique<Buffer>(device, DATA_SIZE(data), bufferUsageFlags, VMA_MEMORY_USAGE_CPU_TO_GPU);
                    indexBuffer->uploadData(data.data(), DATA_SIZE(data));
                    newPrimitive->setIndexBuffer(indexBuffer);
                }

                // Position attribute is required
                assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

                const tinygltf::Accessor&   posAccessor = model.accessors[primitive.attributes.find(
                                                                                                "POSITION")
                                                                            ->second];
                const tinygltf::BufferView& posView     = model.bufferViews[posAccessor.bufferView];

                posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
                posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

                vertexCount = static_cast<uint32_t>(posAccessor.count);
            }

            newPrimitive->setDimensions(posMin, posMax);

            auto matrix = getTransform(node).getLocalToWorldMatrix();

            if (config.bufferAddressAble) {
                auto transformBuffer = std::make_unique<Buffer>(device, sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU, &matrix);
                newPrimitive->setVertexBuffer("transform", transformBuffer);
            }

            // newPrimitive->vertexBuffers.emplace("transform", std::move(transformBuffer));

            PerPrimitiveUniform primitiveUniform{matrix};
            auto                primitiveUniformBuffer = std::make_unique<Buffer>(device, sizeof(PerPrimitiveUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU, &primitiveUniform);
            newPrimitive->setUniformBuffer(primitiveUniformBuffer);

            primitives.push_back(std::move(newPrimitive));
        }
    }
}

void GLTFLoadingImpl::loadFromFile(const std::string& path) {

    Timer timer;
    timer.start();

    std::shared_ptr<tinygltf::Model> gltfModel = std::make_shared<tinygltf::Model>();
    tinygltf::TinyGLTF               gltfContext;
    std::string                      error, warning;
    gltfContext.SetImageLoader(loadImageDataFunc, nullptr);
    bool fileLoaded = gltfContext.LoadASCIIFromFile(gltfModel.get(), &error, &warning, path);

    if (!fileLoaded) {
        LOGE("Could not load glTF file {} {} \"", path, error)
    }

    std::vector<uint32_t> indexData;
    loadMaterials(*gltfModel);
    loadImages(path, gltfModel);
    loadCameras(*gltfModel);
    const tinygltf::Scene& scene = gltfModel->scenes[gltfModel->defaultScene > -1 ? gltfModel->defaultScene : 0];

    if (config.bufferRate == BufferRate::PER_PRIMITIVE) {
        for (size_t i = 0; i < scene.nodes.size(); i++) {
            const tinygltf::Node node = gltfModel->nodes[scene.nodes[i]];
            loadNode(node, *gltfModel);
        }
    } else {
        process(*gltfModel);
    }
    loadLights(*gltfModel);
    std::vector<uint32_t>            primitiveIdxs;
    std::vector<PerPrimitiveUniform> primitiveUniforms;
    for (size_t i = 0; i < primitives.size(); i++) {
        primitiveIdxs.push_back(i);
        primitiveUniforms.push_back(primitives[i]->GetPerPrimitiveUniform());
    }

    sceneUniformBuffer = std::make_unique<Buffer>(device, sizeof(PerPrimitiveUniform) * primitiveUniforms.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, primitiveUniforms.data());

    if (cameras.empty()) {
        auto camera = std::make_shared<Camera>();
        camera->setPerspective(45.0f, 1.0f, 0.1f, 100.0f);
        cameras.push_back(camera);
    }

    for (auto extension : gltfModel->extensionsUsed) {
        if (extension == "KHR_materials_pbrSpecularGlossiness") {
            metallicRoughnessWorkflow = false;
        }
    }
    LOGI("{} loaded in {} s", path, timer.elapsed<Timer::Seconds>());
}

void GLTFLoadingImpl::loadMaterials(tinygltf::Model& gltfModel) {

    materials.reserve(gltfModel.materials.size());

    for (const auto& mat : gltfModel.materials) {
        GltfMaterial material;
        material.alphaCutoff = static_cast<float>(mat.alphaCutoff);
        material.alphaMode   = mat.alphaMode == "MASK" ? 1 : (mat.alphaMode == "BLEND" ? 2 : 0);
        if (mat.name.find("glass") != std::string::npos) {
            if (mat.name.find("red") == std::string::npos)
                material.alphaMode = 2;
        }
        LOGI("alpha mode {}", material.alphaMode);
        LOGI("mat name {}", mat.name);
        material.doubleSided              = mat.doubleSided ? 1 : 0;
        material.emissiveFactor           = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]);
        material.emissiveTexture          = requestTexture(mat.emissiveTexture.index, gltfModel);
        material.normalTexture            = requestTexture(mat.normalTexture.index, gltfModel);
        material.normalTextureScale       = static_cast<float>(mat.normalTexture.scale);
        material.occlusionTexture         = mat.occlusionTexture.index;
        material.occlusionTextureStrength = static_cast<float>(mat.occlusionTexture.strength);

        if (mat.extensions.find("KHR_materials_pbrSpecularGlossiness") == mat.extensions.end()) {
            auto& pbr                            = mat.pbrMetallicRoughness;
            material.pbrBaseColorFactor          = glm::vec4(pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
            material.pbrBaseColorTexture         = requestTexture(pbr.baseColorTexture.index, gltfModel);
            material.pbrMetallicFactor           = static_cast<float>(pbr.metallicFactor);
            material.pbrMetallicRoughnessTexture = requestTexture(pbr.metallicRoughnessTexture.index, gltfModel);
            material.pbrRoughnessFactor          = static_cast<float>(pbr.roughnessFactor);
        } else {
            auto ext = mat.extensions.find("KHR_materials_pbrSpecularGlossiness");
            if (ext != mat.extensions.end()) {
                auto& pbr                   = ext->second;
                material.pbrBaseColorFactor = glm::vec4(pbr.Get("diffuseFactor").Get(0).Get<double>(), pbr.Get("diffuseFactor").Get(1).Get<double>(), pbr.Get("diffuseFactor").Get(2).Get<double>(), pbr.Get("diffuseFactor").Get(3).Get<double>());
                if (pbr.Has("diffuseTexture"))
                    material.pbrBaseColorTexture = requestTexture(pbr.Get("diffuseTexture").Get("index").Get<int>(), gltfModel);
            }
        }
        if (material.alphaMode == 2) {
            material.pbrBaseColorFactor.w = 0.2f;
        }
        LOGI("base color factor {} {} {} {}", material.pbrBaseColorFactor.x, material.pbrBaseColorFactor.y, material.pbrBaseColorFactor.z, material.pbrBaseColorFactor.w);
        materials.emplace_back(material);
    }

    if(materials.empty()) {
        LOGI("No material found in gltf file, using default material");
        GltfMaterial material;
        material.alphaCutoff = 0.5f;
        material.alphaMode = 0;
        material.doubleSided = 0;
        material.emissiveFactor = glm::vec3(0.0f, 0.0f, 0.0f);
        material.emissiveTexture = -1;
        material.normalTexture = -1;
        material.normalTextureScale = 1.0f;
        material.occlusionTexture = -1;
        material.occlusionTextureStrength = 1.0f;
        material.pbrBaseColorFactor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        material.pbrBaseColorTexture = -1;
        material.pbrMetallicFactor = 0.0f;
        material.pbrMetallicRoughnessTexture = -1;
        material.pbrRoughnessFactor = 1.0f;
        materials.emplace_back(material);
    }
    //   materials.resize(gltfModel.materials.size());
}

GLTFLoadingImpl::~GLTFLoadingImpl() {
    // for (auto node : nodes)
    //     delete node;
}

void GLTFLoadingImpl::loadImages(const std::filesystem::path& modelPath, std::shared_ptr<tinygltf::Model> gltfModel) {
    auto parentDir = modelPath.parent_path();

    auto& thread_pool = ThreadPool::GetThreadPool();

    using TextureFuture = std::future<std::unique_ptr<Texture>>;

    std::vector<TextureFuture>* futures = new std::vector<TextureFuture>();

    std::vector<int> texIndexVec(texIndexRemap.size());
    for (auto& remap : texIndexRemap)
        texIndexVec[remap.second] = remap.first;
    for (const auto& remap : texIndexVec) {
        auto fut = thread_pool.push(
            [this, parentDir, temp_remap = remap, gltfModel](size_t) {
                auto& image = gltfModel->images[temp_remap];
                if (image.image.size() > 0)
                    return Texture::loadTextureFromMemoryWithoutInit(device, image.image, VkExtent3D{static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1});
                //return Texture::loadTextureFromMemory(device, image.image, VkExtent3D{static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1});
                else if (!image.uri.empty())
                    return Texture::loadTextureFromFileWitoutInit(device, parentDir.string() + "/" + image.uri);
                else {
                    LOGE("Image uri is empty and image data is empty!");
                    return std::unique_ptr<Texture>(nullptr);
                }
            });
        futures->emplace_back(std::move(fut));
        //  textures.emplace_back(futures->back().get());
    }

    auto reOrganize = thread_pool.push([this, futures](size_t) {
        for (auto& future : *futures)
            future.wait();
        for (int i = 0; i < futures->size(); i++) {
            textures.emplace_back(futures->operator[](i).get());
        }

        std::unordered_map<int, int> texIndexRemap;
        int                          validTextureCount = 0;
        for (int i = 0; i < textures.size(); i++) {
            if (textures[i] != nullptr) {
                texIndexRemap[i] = validTextureCount;
                validTextureCount++;
            } else {
                LOGI("Texture {} is nullptr", i)
            }
        }
        for (auto& material : materials) {
            if (material.pbrBaseColorTexture != -1) {
                material.pbrBaseColorTexture = texIndexRemap.contains(material.pbrBaseColorTexture) ? texIndexRemap[material.pbrBaseColorTexture] : -1;
            }
            if (material.pbrMetallicRoughnessTexture != -1) {
                material.pbrMetallicRoughnessTexture = texIndexRemap.contains(material.pbrMetallicRoughnessTexture) ? texIndexRemap[material.pbrMetallicRoughnessTexture] : -1;
            }
            if (material.normalTexture != -1) {
                material.normalTexture = texIndexRemap.contains(material.normalTexture) ? texIndexRemap[material.normalTexture] : -1;
            }
        }

        std::vector<std::unique_ptr<Texture>> remappedTextures;
        for (int i = 0; i < textures.size(); i++) {
            if (textures[i] != nullptr) {
                remappedTextures.emplace_back(std::move(textures[i]));
            }
        }

        textures = std::move(remappedTextures);
        Texture::initTexturesInOneSubmit(textures);

        sceneToLoad->setTextures(std::move(textures));

        sceneToLoad->getLoadCompleteInfo().SetTextureLoaded();
        if (sceneToLoad->getLoadCompleteInfo().GetSceneLoaded()) {
            delete this;
        }
        delete futures;
        return 1;
    });
}

template<class T, class Y>
struct TypeCast {
    Y operator()(T value) const noexcept {
        return static_cast<Y>(value);
    }
};

const Texture* GLTFLoadingImpl::getTexture(uint32_t index) const {
    if (index < textures.size()) {
        return textures[index].get();
    }
    return nullptr;
}

std::unique_ptr<Scene> GltfLoading::LoadSceneFromGLTFFile(Device& device, const std::string& path, const SceneLoadingConfig& config) {
    auto scene = std::make_unique<Scene>();
    auto model = new GLTFLoadingImpl(device, path, config, scene.get());

    scene->primitives = std::move(model->primitives);
    //scene->textures           = std::move(model->textures);
    scene->materials          = std::move(model->materials);
    scene->lights             = std::move(model->lights);
    scene->cameras            = std::move(model->cameras);
    scene->vertexAttributes   = std::move(model->vertexAttributes);
    scene->sceneVertexBuffer  = std::move(model->sceneVertexBuffer);
    scene->sceneIndexBuffer   = std::move(model->sceneIndexBuffer);
    scene->sceneUniformBuffer = std::move(model->sceneUniformBuffer);
    scene->indexType          = model->indexType;
    scene->primitiveIdBuffer  = std::move(model->scenePrimitiveIdBuffer);
    scene->bufferRate         = config.bufferRate;

    scene->setSceneBBox(model->sceneBBox);

    scene->getLoadCompleteInfo().SetGeometryLoaded();
    return scene;
}

GLTFLoadingImpl::GLTFLoadingImpl(Device& device, const std::string& path, const SceneLoadingConfig& config, Scene* scene) : device(device), config(config), sceneToLoad(scene) {
    loadFromFile(path);
}

glm::mat4 Node::localMatrix() {
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) *
           matrix;
}

glm::mat4 Node::getMatrix() {
    glm::mat4 m = localMatrix();
    Node*     p = parent;
    while (p) {
        m = p->localMatrix() * m;
        p = p->parent;
    }
    return m;
}

void Node::update() {
}

Node::~Node() {
    if (mesh) {
        delete mesh;
    }
    for (auto& child : children) {
        delete child;
    }
}

Mesh::Mesh(Device& device, glm::mat4 matrix) : device(device) {
}


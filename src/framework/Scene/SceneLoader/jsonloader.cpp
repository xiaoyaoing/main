#include "jsonloader.h"

#include "ComplexIor.hpp"
#include "LoaderHelper.h"
#include "ObjLoader.hpp"
#include "Core/BufferPool.h"
#include "Core/RenderContext.h"
#include "Core/math.h"
#include "Raytracing/commons.h"
#include <Scene/Compoments/Camera.h>

#include <Common/JsonUtil.h>
#include <tiny_obj_loader.h>

struct JsonLoader {
    JsonLoader(Device& device) : device(device) {}
    void    LoadSceneFromJsonFile(Device& device, const std::string& path, const SceneLoadingConfig& config);
    void    loadCamera();
    void    PreprocessMaterials();
    void    loadMaterials();
    void    loadPrimitives();
    void    Valiation();
    Json    sceneJson;
    Device& device;

    std::vector<PerPrimitiveUniform> primitiveUniforms;

    std::vector<std::unique_ptr<Primitive>> primitives;
    BBox sceneBBox;
    std::vector<std::unique_ptr<Texture>>   textures;
    std::vector<GltfMaterial>               materials;
    std::vector<RTMaterial>                 rtMaterials;
    std::vector<SgLight>                    lights;
    std::vector<std::shared_ptr<Camera>>    cameras;
    // std::vector<RTMaterial> rtMaterials;
    std::vector<Json>                                materialJsons;
    std::unordered_map<std::string, uint32_t>        materialIndexMap;
    std::unordered_map<std::string, VertexAttribute> vertexAttributes;

    std::unordered_map<std::string, std::unique_ptr<Buffer>> sceneVertexBuffer;
    std::unique_ptr<Buffer>                                  sceneIndexBuffer{nullptr};
    std::unique_ptr<Buffer>                                  sceneUniformBuffer{nullptr};
    std::unique_ptr<Buffer>                                  scenePrimitiveIdBuffer{nullptr};

    std::filesystem::path rootPath;
    SceneLoadingConfig    config;
};



VkBufferUsageFlags GetBufferUsageFlags(const SceneLoadingConfig& config, VkBufferUsageFlags flags) {
    VkBufferUsageFlags vkBufferUsageFlags = flags;
    if (config.bufferAddressAble) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (config.bufferForAccel) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (config.bufferForTransferSrc) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (config.bufferForTransferDst) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (config.bufferForStorage) vkBufferUsageFlags = vkBufferUsageFlags | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    return vkBufferUsageFlags;
}

void JsonLoader::LoadSceneFromJsonFile(Device& device, const std::string& path, const SceneLoadingConfig& config) {
    std::ifstream file(path);
    file >> sceneJson;
    rootPath     = std::filesystem::path(path).parent_path();
    this->config = config;
    PreprocessMaterials();
    loadCamera();
    loadPrimitives();
    loadMaterials();
}
void JsonLoader::loadCamera() {
    std::shared_ptr<Camera> camera     = std::make_shared<Camera>();
    Json                    cameraJson = sceneJson["camera"];
    auto                    transform  = GetOptional(cameraJson, "transform", Json());
    vec3                    lookAt     = GetOptional(transform, "look_at", vec3(0, 0, 0));
    vec3                    up         = GetOptional(transform, "up", vec3(0, 1, 0));
    camera->setTranslation(GetOptional(transform, "position", glm::vec3(0, 0, 0)));
    camera->lookAt(lookAt, up);
    vec2  resolution = GetOptional(cameraJson, "resolution", vec2(1920, 1080));
    float aspect     = resolution.x / resolution.y;
    //  camera->setRotation(GetOptional(transform, "rotation", glm::vec3(0.0f)));
    camera->setPerspective(GetOptional(cameraJson, "fov", 45.0f), aspect, GetOptional(cameraJson, "zNear", 0.1f), GetOptional(cameraJson, "zFar", 4000.0f));
    camera->setFlipY(true);

    camera->setScreenSize(int(resolution.x), int(resolution.y));
    cameras.push_back(camera);
}
void JsonLoader::PreprocessMaterials() {
    Json materialsJson = sceneJson["bsdfs"];
    for (auto& materialJson : materialsJson) {
        materialJsons.push_back(materialJson);
        if (materialJson.contains("name")) {
            materialIndexMap[materialJson["name"].get<std::string>()] = materialJsons.size() - 1;
        }
    }
    // for(auto & primitiveJson : sceneJson["primitives"]) {
    //     if(primitiveJson.contains("bsdf")) {
    //         auto &  materialJson = primitiveJson["bsdf"];
    //         if(ma)
    //     }
    // }
}

std::unordered_map<std::string, uint32_t> type2RTBSDFTYPE = {
    {"diffuse", RT_BSDF_TYPE_DIFFUSE},
    {"lambert", RT_BSDF_TYPE_DIFFUSE},
    {"specular", RT_BSDF_TYPE_MIRROR},
    {"conductor", RT_BSDF_TYPE_CONDUCTOR},
    {"rough_conductor", RT_BSDF_TYPE_CONDUCTOR},
    {"plastic", RT_BSDF_TYPE_DIFFUSE},
    {"rough_plastic", RT_BSDF_TYPE_PLASTIC},
    {"disney", RT_BSDF_TYPE_DISNEY},
    {"dielectric", RT_BSDF_TYPE_DIELECTRIC},
    {"rough_dielectric", RT_BSDF_TYPE_DIELECTRIC},
};

static inline float dielectricReflectance(float eta, float cosThetaI, float& cosThetaT) {
    if (cosThetaI < 0.0) {
        eta       = 1.0 / eta;
        cosThetaI = -cosThetaI;
    }
    float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);
    if (sinThetaTSq > 1.0) {
        cosThetaT = 0.0;
        return 1.0;
    }
    cosThetaT = std::sqrt(std::max(1.0 - sinThetaTSq, 0.0));

    float Rs = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float Rp = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);

    return (Rs * Rs + Rp * Rp) * 0.5;
}

static inline float dielectricReflectance(float eta, float cosThetaI) {
    float cosThetaT;
    return dielectricReflectance(eta, cosThetaI, cosThetaT);
}

static inline float diffuseReflectance(float eta, float sampleCount) {
    double diffuseFresnel = 0.0;
    float  fb             = dielectricReflectance(eta, 0.0f);
    for (int i = 1; i <= sampleCount; ++i) {
        float cosThetaSq = float(i) / sampleCount;
        float fa         = dielectricReflectance(eta, std::min(std::sqrt(cosThetaSq), 1.0f));
        diffuseFresnel += double(fa + fb) * (0.5 / sampleCount);
        fb = fa;
    }

    return float(diffuseFresnel);
}

void handleSpecifyMaterialAttribute(RTMaterial& rtMaterial, const Json& materialJson) {
    if (rtMaterial.bsdf_type == RT_BSDF_TYPE_CONDUCTOR) {
        auto material = GetOptional<std::string>(materialJson, "material", "Cu");
        ComplexIorList::lookup(material, rtMaterial.eta, rtMaterial.k);
    }
    if (rtMaterial.bsdf_type == RT_BSDF_TYPE_PLASTIC) {
        float m_ior         = GetOptional(materialJson, "ior", 1.3);
        float thickness     = GetOptional(materialJson, "thickness", 1);
        vec3  sigmaA        = GetOptional(materialJson, "sigma_a", vec3(0.f));
        vec3  _scaledSigmaA = thickness * sigmaA;

        float avgSigmaA             = (_scaledSigmaA.x + _scaledSigmaA.y + _scaledSigmaA.z) / 3.0f;
        rtMaterial.scaledSigmaA     = _scaledSigmaA;
        rtMaterial.ior              = m_ior;
        rtMaterial.avgTransmittance = std::exp(-2.0f * avgSigmaA);
        rtMaterial.diffuseFresnel   = diffuseReflectance(m_ior, 10000);
    }
    if (rtMaterial.bsdf_type == RT_BSDF_TYPE_DIELECTRIC) {
        rtMaterial.ior = GetOptional(materialJson, "ior", 1.3f);
    }
    if (rtMaterial.bsdf_type == RT_BSDF_TYPE_DISNEY) {
        rtMaterial.albedo = GetOptional(materialJson, "base_color", vec3(0.5f));
        rtMaterial.ior = GetOptional(materialJson, "ior", 1.3f);
        rtMaterial.metallic = GetOptional(materialJson, "metallic", 0.0f);
        rtMaterial.sheenTint = GetOptional(materialJson, "sheen_tint", 0.0f);
        rtMaterial.sheen = GetOptional(materialJson, "sheen", 0.0f);
        rtMaterial.clearCoat = GetOptional(materialJson, "clearCoat", 0.0f);
        rtMaterial.metallic = GetOptional(materialJson, "metallic", 0.0f);
        rtMaterial.specularTint = GetOptional(materialJson, "specular_tint", 0.0f);
        rtMaterial.anisotropic = GetOptional(materialJson, "anisotropic", 0.0f);
        rtMaterial.specularTransmission = GetOptional(materialJson, "specular_transmission", 0.0f);
        rtMaterial.clearCoatGloss = GetOptional(materialJson, "clear_coat_gloss", 0.0f);
        rtMaterial.subSurfaceFactor = GetOptional(materialJson, "subsurface", 0.0f);
    }
}
void JsonLoader::loadMaterials() {
    std::unordered_set<std::string> texture_paths;
    for (auto& materialJson : materialJsons) {
        RTMaterial rtMaterial;
        if (materialJson.contains("albedo") && materialJson["albedo"].is_string()) {
            texture_paths.insert(materialJson["albedo"].get<std::string>());
        }
        if (materialJson.contains("roughness") && materialJson["roughness"].is_string()) {
            texture_paths.insert(materialJson["roughness"].get<std::string>());
        }
    }
    //  std::vector<std::unique_ptr<Texture>> textures;
    std::unordered_map<std::string_view, int> texture_index;
    for (auto& texture_path : texture_paths) {
        textures.push_back(Texture::loadTextureFromFile(device, rootPath.string() + "/" + texture_path));
        texture_index[texture_path] = textures.size() - 1;
    }

    // textures.push_back(Texture::loadTextureFromFile(device, "E:/DM/dist/scenes/classroom/textures/blackboard.jpg"));

    // textures.clear();
    //
    // for (auto& texture_path : texture_paths) {
    //     textures.push_back(Texture::loadTextureFromFile(device, rootPath.string() + "/" + texture_path));
    //     texture_index[texture_path] = textures.size() - 1;
    // }
    //
    // texture_paths.clear();
    // texture_index.clear();

    for (auto& materialJson : materialJsons) {

        RTMaterial rtMaterial;
        if (materialJson.contains("albedo") && materialJson["albedo"].is_string()) {
            auto textureName      = materialJson["albedo"].get<std::string>();
            rtMaterial.texture_id = texture_index.contains(textureName) ? texture_index[textureName] : -1;
        } else {
            rtMaterial.texture_id = -1;
            rtMaterial.albedo     = GetOptional(materialJson, "albedo", vec3(0.5f));
        }
        if(materialJson.contains("roughness") && materialJson["roughness"].is_string()) {
            auto textureName      = materialJson["roughness"].get<std::string>();
            rtMaterial.roughness_texture_id = texture_index.contains(textureName) ? texture_index[textureName] : -1;
        } else {
            rtMaterial.roughness_texture_id = -1;
            rtMaterial.roughness            = GetOptional(materialJson, "roughness", 0.f);
        }
        if(materialJson.contains("name")) {
            LOGI("name : " + materialJson["name"].get<std::string>());
            if(materialJson["name"] == "Floor") {
                LOGI(rtMaterial.texture_id);
            }
        }
        rtMaterial.emissiveFactor = vec3(0);
        rtMaterial.bsdf_type      = type2RTBSDFTYPE[materialJson["type"].get<std::string>()];
        rtMaterial.roughness      = GetOptional(materialJson, "roughness", 0.0f);
        handleSpecifyMaterialAttribute(rtMaterial, materialJson);
        rtMaterials.push_back(rtMaterial);

        GltfMaterial material        = InitGltfMaterial();
        material.pbrBaseColorFactor  = glm::vec4(rtMaterial.albedo, 1);
        material.pbrBaseColorTexture = rtMaterial.texture_id;
        materials.push_back(material);
    }
    //textures.clear();
    //  static std::unique_ptr<Texture> t = std::move(textures[0]);
    //  delete t.get();
    // textures.clear();
}

template<typename T>
std::vector<T> getTFromGpuBuffer(Buffer& buffer) {
    Buffer        stagingBuffer(buffer.getDevice(), buffer.getSize(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    CommandBuffer commandBuffer = buffer.getDevice().createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    VkBufferCopy  copyRegion{0, 0, buffer.getSize()};
    vkCmdCopyBuffer(commandBuffer.getHandle(), buffer.getHandle(), stagingBuffer.getHandle(), 1, &copyRegion);
    g_context->submit(commandBuffer, true);
    return stagingBuffer.getData<T>();
}

static vec3 randomOrtho(const vec3& a) {
    vec3 res;
    if (std::abs(a.x) > std::abs(a.y))
        res = vec3(0.0f, 1.0f, 0.0f);
    else
        res = vec3(1.0f, 0.0f, 0.0f);
    return normalize(cross(a, res));
}

static void gramSchmidt(vec3& a, vec3& b, vec3& c) {
    a = normalize(a);
    b -= a * dot(a, b);
    if (length2(b) < 1e-5)
        b = randomOrtho(a);
    else
        b = normalize(b);

    c -= a * dot(a, c);
    c -= b * dot(b, c);
    if (length2(c) < 1e-5)
        c = cross(a, b);
    else
        c = normalize(c);
}

static inline vec3 mult(const mat4& a, const vec4 point) {
    return vec3(
        a[0][0] * point.x + a[0][1] * point.y + a[0][2] * point.z + a[0][3] * point.w,
        a[1][0] * point.x + a[1][1] * point.y + a[1][2] * point.z + a[1][3] * point.w,
        a[2][0] * point.x + a[2][1] * point.y + a[2][2] * point.z + a[2][3] * point.w);
}

static mat4 rotYXZ(const vec3& rot) {
    vec3  r   = rot * math::PI / 180.0f;
    float c[] = {std::cos(r.x), std::cos(r.y), std::cos(r.z)};
    float s[] = {std::sin(r.x), std::sin(r.y), std::sin(r.z)};

    return mat4(
        c[1] * c[2] - s[1] * s[0] * s[2], -c[1] * s[2] - s[1] * s[0] * c[2], -s[1] * c[0], 0.0f, c[0] * s[2], c[0] * c[2], -s[0], 0.0f, s[1] * c[2] + c[1] * s[0] * s[2], -s[1] * s[2] + c[1] * s[0] * c[2], c[1] * c[0], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
}

glm::mat4 mat4FromJson(const Json& json) {
    vec3 x(1.0f, 0.0f, 0.0f);
    vec3 y(0.0f, 1.0f, 0.0f);
    vec3 z(0.0f, 0.0f, 1.0f);

    vec3 pos = GetOptional(json, "position", vec3(0.0f));

    gramSchmidt(z, y, x);

    if (dot(cross(x, y), z) < 0.0f) {
        x = -x;
    }

    vec3 scale;
    if (json.contains("scale")) {
        scale = json["scale"];
        x *= scale.x;
        y *= scale.y;
        z *= scale.z;
    }

    vec3 rot;
    if (ContainsAndGet(json, "rotation", rot)) {
        mat4 tform = rotYXZ(rot);
        x          = mult(tform, vec4(x, 1.0));
        y          = mult(tform, vec4(y, 1.0));
        z          = mult(tform, vec4(z, 1.0));
    }
    return mat4(
        x[0], y[0], z[0], pos[0], x[1], y[1], z[1], pos[1], x[2], y[2], z[2], pos[2], 0.0f, 0.0f, 0.0f, 1.0f);
}

float getArea(const PrimitiveData& primitiveData, const Transform& transform) {
    const auto&                  position = primitiveData.buffers.at(POSITION_ATTRIBUTE_NAME);
    const std::vector<uint32_t>& indexs   = reinterpret_cast<const std::vector<uint32_t>&>(primitiveData.indexs);
    float                        area     = 0;
    for (size_t i = 0; i < indexs.size(); i += 3) {
        vec3 v0 = transform.getLocalToWorldMatrix() * vec4(reinterpret_cast<const glm::vec3*>(position.data())[indexs[i]], 1.0f);
        vec3 v1 = transform.getLocalToWorldMatrix() * vec4(reinterpret_cast<const glm::vec3*>(position.data())[indexs[i + 1]], 1.0f);
        vec3 v2 = transform.getLocalToWorldMatrix() * vec4(reinterpret_cast<const glm::vec3*>(position.data())[indexs[i + 2]], 1.0f);
        area += 0.5f * length(cross(v1 - v0, v2 - v0));
    }
    return area;
}

//when load primitives from type
//tlas build error
//can't find bug now
void JsonLoader::loadPrimitives() {
    const Json& primitivesJson = sceneJson["primitives"];

    uint32_t vertexOffset = 0;
    uint32_t indexOffset  = 0;
    //  uint32_t maxVertexCount = 0;
    //  uint32_t maxIndexCount  = 0;

    std::vector<std::unordered_map<std::string, BufferData>> vertexDatas;
    std::vector<BufferData>                                  indexBuffers;
    //   std::vector<Transform> transforms;
    std::vector<uint32_t> vertexOffsets, indexOffsets;

    vertexAttributes = {
        {POSITION_ATTRIBUTE_NAME, VertexAttribute{.format = VK_FORMAT_R32G32B32_SFLOAT, .stride = sizeof(glm::vec3)}},
        {NORMAL_ATTRIBUTE_NAME, VertexAttribute{.format = VK_FORMAT_R32G32B32_SFLOAT, .stride = sizeof(glm::vec3)}},
        {TEXCOORD_ATTRIBUTE_NAME, VertexAttribute{.format = VK_FORMAT_R32G32_SFLOAT, .stride = sizeof(glm::vec2)}},
    };

    for (auto& primitiveJson : primitivesJson) {

        std::unique_ptr<PrimitiveData> primitiveData = nullptr;
        if (primitiveJson.contains("file")) {
            primitiveData = PrimitiveLoader::loadPrimitive(rootPath.string() + "/" + primitiveJson["file"].get<std::string>());
        } else if (primitiveJson.contains("type")) {
            std::string type = primitiveJson["type"];
            if (type == "infinite_sphere") {
                mat4 matrix = mat4(1);
                if (primitiveJson.contains("transform")) {
                    matrix = mat4FromJson(primitiveJson["transform"]);
                }
                lights.push_back(SgLight{.type = LIGHT_TYPE::Sky, .lightProperties = {.texture_index = static_cast<uint32_t>(textures.size()), .world_matrix = matrix}});
                textures.push_back(Texture::loadTextureFromFile(device, rootPath.string() + "/" + primitiveJson["emission"].get<std::string>()));
            } else
                primitiveData = PrimitiveLoader::loadPrimitiveFromType(type);
        } else {
            LOGE("Primitive must have a type or a file")
        }
        if (primitiveData == nullptr) {
            LOGW("Failed to load primitive");
            continue;
        }

        Transform transform;
        if (primitiveJson.contains("transform")) {
            auto transformJson = primitiveJson["transform"];
            auto position      = GetOptional(transformJson, "position", glm::vec3(0.0f));
            transform.setPosition(position);

            auto scale = GetOptional(transformJson, "scale", glm::vec3(1.0f));
            transform.setLocalScale(scale);

            auto rotation = GetOptional(transformJson, "rotation", glm::vec3(0.0f));
            transform.setRotation(math::eulerYZXQuat(rotation));
            transform.setRotation(transpose(rotYXZ(rotation)));

            // transform.setLocalToWorldMatrix(transpose(mat4FromJson(transformJson)));
        }

        uint32_t lightIndex = -1;
        if (primitiveJson.contains("emission")) {
            vec3 color = primitiveJson["emission"];
            lights.push_back(SgLight{.type = LIGHT_TYPE::Area, .lightProperties = {.color = color, .prim_index = static_cast<uint32_t>(primitives.size())}});
            lightIndex = lights.size() - 1;
            LOGI("light color : {} {} {}", lights.back().lightProperties.color.x, lights.back().lightProperties.color.y, lights.back().lightProperties.color.z);
        }

        if (primitiveJson.contains("power")) {
            vec3 color = primitiveJson["power"];
            color /= getArea(*primitiveData, transform);
            //todo
            lights.push_back(SgLight{.type = LIGHT_TYPE::Area, .lightProperties = {.color = color, .prim_index = static_cast<uint32_t>(primitives.size())}});
            lightIndex = lights.size() - 1;
            LOGI("light color : {} {} {}", lights.back().lightProperties.color.x, lights.back().lightProperties.color.y, lights.back().lightProperties.color.z);

        }
        // auto primitiveData = PrimitiveLoader::loadPrimitive(rootPath.string()+"/"+primitiveJson["file"].get<std::string>());
        //
        uint32_t vertexCount = primitiveData->buffers.at(POSITION_ATTRIBUTE_NAME).size() / sizeof(glm::vec3);
        uint32_t indexCount  = primitiveData->indexs.size() / sizeof(uint32_t);

        //    maxVertexCount = std::max(maxVertexCount, vertexCount);
        //   maxIndexCount  = std::max(maxIndexCount, indexCount);

        uint32_t material_index;
        {
            auto& materialJson = primitiveJson["bsdf"];
            if (materialJson.is_string()) {
                CHECK_RESULT(materialIndexMap.contains(materialJson));
                material_index = materialIndexMap[materialJson.get<std::string>()];
            } else {
                materialJsons.emplace_back(std::move(materialJson));
                material_index = materialJsons.size() - 1;
            }
        }
        auto primitive = std::make_unique<Primitive>(vertexOffset, indexOffset, vertexCount, indexCount, material_index);

        vertexOffsets.push_back(vertexOffset);
        indexOffsets.push_back(indexOffset);

        vertexOffset += vertexCount;
        indexOffset += indexCount;

        indexBuffers.emplace_back(std::move(primitiveData->indexs));
        vertexDatas.push_back(std::move(primitiveData->buffers));


        primitive->lightIndex = lightIndex;
        auto bbox             = primitiveData->bbox;
        auto tempMin          = transform.getLocalToWorldMatrix() * glm::vec4(bbox.min(), 1.0f);
        auto tempMax          = transform.getLocalToWorldMatrix() * glm::vec4(bbox.max(), 1.0f);
        primitive->setDimensions(vec3(std::min(tempMin.x, tempMax.x), std::min(tempMin.y, tempMax.y), std::min(tempMin.z, tempMax.z)),
                                 vec3(std::max(tempMin.x, tempMax.x), std::max(tempMin.y, tempMax.y), std::max(tempMin.z, tempMax.z)));
        primitive->transform = transform;

        sceneBBox.unite(primitive->getDimensions());
        primitives.push_back(std::move(primitive));
    }
    std::unordered_map<std::string, std::unique_ptr<Buffer>> stagingVertexBuffers;
    auto                                                     stagingIndexBuffer = std::make_unique<Buffer>(device, indexOffset * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    for (auto& attri : vertexAttributes) {
        auto buffer = std::make_unique<Buffer>(device, attri.second.stride * vertexOffset, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        // sceneVertexBuffer[attri.first] = std::move(buffer);
        stagingVertexBuffers[attri.first] = std::move(buffer);
    }

    auto commandBuffer = device.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    // auto copyBuffer = [this, &commandBuffer, &stagingVertexBuffers,stagingIndexBuffer](Buffer & srcBuffer,Buffer& dstBuffer, uint32_t size, uint32_t offset) {
    //     VkBufferCopy copy{0, offset, size};
    //     vkCmdCopyBuffer(commandBuffer.getHandle(), srcBuffer.getHandle(), dstBuffer.getHandle(), 1, &copy);
    // };
    for (uint32_t i = 0; i < primitives.size(); i++) {
        for (auto& [name, bufferData] : vertexDatas[i]) {
            stagingVertexBuffers[name]->uploadData(bufferData.data(), bufferData.size(), vertexOffsets[i] * vertexAttributes[name].stride);
        }
        stagingIndexBuffer->uploadData(indexBuffers[i].data(), indexBuffers[i].size(), indexOffsets[i] * sizeof(uint32_t));
    }

    for (auto& [name, buffer] : stagingVertexBuffers) {
        sceneVertexBuffer[name] = Buffer::FromBuffer(device, commandBuffer, *buffer, GetBufferUsageFlags(config, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    }
    sceneIndexBuffer = Buffer::FromBuffer(device, commandBuffer, *stagingIndexBuffer, GetBufferUsageFlags(config, VK_BUFFER_USAGE_INDEX_BUFFER_BIT));

    g_context->submit(commandBuffer, true);


    {
        sceneUniformBuffer = std::make_unique<Buffer>(device, sizeof(PerPrimitiveUniform) * primitives.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        std::vector<PerPrimitiveUniform> uniforms;
        for (uint32_t i = 0; i < primitives.size(); i++) {
            PerPrimitiveUniform uniform;
            uniform.model         = primitives[i]->transform.getLocalToWorldMatrix();
            uniform.modelIT       = glm::transpose(glm::inverse(uniform.model));
            uniform.materialIndex = primitives[i]->materialIndex;
            uniforms.push_back(uniform);
        }
        sceneUniformBuffer->uploadData(uniforms.data(), uniforms.size() * sizeof(PerPrimitiveUniform));
    }
    {
        scenePrimitiveIdBuffer = std::make_unique<Buffer>(device, sizeof(uint32_t) * primitives.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        std::vector<uint32_t> primitiveIds;
        for (uint32_t i = 0; i < primitives.size(); i++) {
            primitiveIds.push_back(i);
        }
        scenePrimitiveIdBuffer->uploadData(primitiveIds.data(), primitiveIds.size() * sizeof(uint32_t));
    }
}
void JsonLoader::Valiation() {
    CHECK_RESULT(primitives.size() > 0);
    CHECK_RESULT(materials.size() > 0);
    CHECK_RESULT(lights.size() > 0);
    CHECK_RESULT(cameras.size() > 0);
    CHECK_RESULT(sceneVertexBuffer.size() > 0);
    CHECK_RESULT(sceneIndexBuffer != nullptr);
    CHECK_RESULT(sceneUniformBuffer != nullptr);
    CHECK_RESULT(scenePrimitiveIdBuffer != nullptr);
}

std::unique_ptr<Scene> Jsonloader::LoadSceneFromJsonFile(Device& device, const std::string& path, const SceneLoadingConfig& config) {
    JsonLoader loader(device);
    loader.LoadSceneFromJsonFile(device, path, config);

    auto scene               = std::make_unique<Scene>();
    scene->primitives        = std::move(loader.primitives);
    scene->textures          = std::move(loader.textures);
    scene->materials         = std::move(loader.materials);
    scene->rtMaterials       = std::move(loader.rtMaterials);
    scene->lights            = std::move(loader.lights);
    scene->cameras           = std::move(loader.cameras);
    scene->vertexAttributes  = std::move(loader.vertexAttributes);
    scene->sceneVertexBuffer = std::move(loader.sceneVertexBuffer);
    scene->sceneIndexBuffer  = std::move(loader.sceneIndexBuffer);
    scene->setSceneBBox(loader.sceneBBox);

    scene->sceneUniformBuffer = std::move(loader.sceneUniformBuffer);
    scene->indexType          = VK_INDEX_TYPE_UINT32;
    scene->primitiveIdBuffer  = std::move(loader.scenePrimitiveIdBuffer);

    scene->loadCompleteInfo->SetGeometryLoaded();
    scene->loadCompleteInfo->SetTextureLoaded();
    return scene;
}

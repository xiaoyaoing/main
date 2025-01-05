#include "RenderConfig.h"


using IntegratorKey = std::string;
static const IntegratorKey kPathIntegrator     = "path";
static const IntegratorKey kRestirDIIntegrator = "restir";
static const IntegratorKey kDDGIIntegrator     = "ddgi";
static const IntegratorKey kSurfelIntegrator   = "surfel";

static const std::unordered_map<EIntegraotrType, IntegratorKey> kIntegratorTypeToString = {
    {ePathTracing, kPathIntegrator},
    {eDDGI, kDDGIIntegrator},
    {eRestirDI, kRestirDIIntegrator},
    {eSurfelGI, kSurfelIntegrator}
};

std::string to_string(EIntegraotrType type) {
    return kIntegratorTypeToString.at(type);
}

std::string RenderConfig::getScenePath() const {
    return scenePath;
}

DDGIConfig RenderConfig::getDDGIConfig() const {
    return ddgiConfig;
}
PathTracingConfig RenderConfig::getPathTracingConfig() const {
    return pathTracingConfig;
}

SurfelConfig RenderConfig::getSurfelConfig() const {
    return {};
}

EIntegraotrType RenderConfig::getIntegratorType() const {
    return mIntegratorType;
}
void RenderConfig::getSceneLoadingConfig(SceneLoadingConfig& config) const {
    {
        config.sceneScale = GetOptional(json, "scene_scale", config.sceneScale);
        config.sceneRotation = GetOptional(json, "scene_rotation",config.sceneRotation);
        config.sceneTranslation = GetOptional(json, "scene_translation", config.sceneTranslation);
    }
}
int RenderConfig::getWindowWidth() const {
    return window_width;
}
int RenderConfig::getWindowHeight() const {
    return window_height;
}
std::vector<SgLight> RenderConfig::getLights() const {
    return lights;
}
RenderConfig::RenderConfig(const Json& json) {
    this->json              = json;
    init();
}
RenderConfig::RenderConfig(const std::string& path) {
    json = JsonUtil::fromFile(path);
    init();
}


static void loadLightsFromJsonPart(const Json& json, std::vector<SgLight>& lights) {
    for (const auto& lightJson : json) {
        SgLight light;
        // 解析光源类型
        std::string typeStr = lightJson["type"].get<std::string>();
        if (typeStr == "directional") {
            light.type = LIGHT_TYPE::Directional;
        } else if (typeStr == "point") {
            light.type = LIGHT_TYPE::Point;
        } else if (typeStr == "spot") {
            light.type = LIGHT_TYPE::Spot;
        } else if (typeStr == "area") {
            light.type = LIGHT_TYPE::Area;
        } else if (typeStr == "sky") {
            light.type = LIGHT_TYPE::Sky;
        } else {
            LOGE("Unknown light type: {}", typeStr);
            continue;
        }
        // 解析光源属性
        light.lightProperties.position = GetOptional(lightJson, "position", glm::vec3(0.0f));
        light.lightProperties.direction = GetOptional(lightJson, "direction", glm::vec3(0.0f, 0.0f, -1.0f));
        light.lightProperties.color = GetOptional(lightJson, "color", glm::vec3(1.0f));
        light.lightProperties.intensity = GetOptional(lightJson, "intensity", 1.0f);
        light.lightProperties.range = GetOptional(lightJson, "range", 0.0f);
        light.lightProperties.inner_cone_angle = GetOptional(lightJson, "inner_cone_angle", 0.0f);
        light.lightProperties.outer_cone_angle = GetOptional(lightJson, "outer_cone_angle", 0.0f);
        light.lightProperties.use_shadow = GetOptional(lightJson, "use_shadow", true);
        // 添加到光源列表
        lights.push_back(light);
    }
}


void RenderConfig::init() {
    auto integratorsTypeStr = json["integrators"];
    for (auto& integratorJson : integratorsTypeStr) {
        auto integratorTypeStr = integratorJson["type"].get<std::string>();
        if (integratorTypeStr == kPathIntegrator) {
            pathTracingConfig.min_depth = GetOptional(integratorJson, "min_depth", pathTracingConfig.min_depth);
            pathTracingConfig.max_depth = GetOptional(integratorJson, "max_depth", pathTracingConfig.max_depth);
        } else if (integratorTypeStr == kDDGIIntegrator) {
            ddgiConfig.rays_per_probe = GetOptional(integratorJson, "ray_per_probe", ddgiConfig.rays_per_probe);
            ddgiConfig.probe_distance = GetOptional(integratorJson, "probe_distance", ddgiConfig.probe_distance);
            ddgiConfig.normal_bias    = GetOptional(integratorJson, "normal_bias", ddgiConfig.normal_bias);
            ddgiConfig.view_bias      = GetOptional(integratorJson, "view_bias", ddgiConfig.view_bias);
            ddgiConfig.use_rt_gbuffer = GetOptional(integratorJson, "use_rt_gbuffer", ddgiConfig.use_rt_gbuffer);
            ddgiConfig.probe_start_position = GetOptional(integratorJson, "probe_start_position", ddgiConfig.probe_start_position);
            ddgiConfig.probe_counts = GetOptional(integratorJson, "probe_counts", ddgiConfig.probe_counts);
        } else if (integratorTypeStr == kRestirDIIntegrator) {
            // integratorType = eRestirDI;
        }
    }

    auto integrator     = json["integrator"];
    auto integratorType = integrator["type"].get<std::string>();
    if (integratorType == kPathIntegrator) {
        mIntegratorType = ePathTracing;
    } else if (integratorType == kDDGIIntegrator) {
        mIntegratorType = eDDGI;
    } else if (integratorType == kRestirDIIntegrator) {
        mIntegratorType = eRestirDI;
    }
    else if (integratorType == kSurfelIntegrator) {
		mIntegratorType = eSurfelGI;
	}

    {
        scenePath = json["scene_path"].get<std::string>();
        if(json.contains("lights"))
           loadLightsFromJsonPart(json["lights"], lights);
    }
}

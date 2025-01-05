//
// Created by 打工人 on 2023/3/19.
//

#include "Application.h"

#include "ctpl_stl.h"

#include <Core/Device/Instance.h>
#include "../Common/VkCommon.h"
#include "Common/Config.h"
#include "Common/ResourceCache.h"
#include "Common/TextureHelper.h"
#include "Gui/Gui.h"
#include "Core/RenderTarget.h"
#include "Core/Shader/Shader.h"
#include "Core/Texture.h"
#include "Core/math.h"
#include "Core/Shader/GlslCompiler.h"
#include "IO/ImageIO.h"
#include "PostProcess/PostProcess.h"
#include "RenderPasses/RenderPassBase.h"
#include "Scene/Compoments/Camera.h"
#include "Scene/SceneLoader/ObjLoader.hpp"
#include "Scene/SceneLoader/SceneLoaderInterface.h"
#include "Scene/SceneLoader/gltfloader.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <imgui.h>
#include "Core/SwapChain.h"
#include <volk.h>


/*
 * Initializes window dimensions and application name
 * Calls initWindow to create the application window
 */
Application::Application(const char* name,
                         uint32_t    width,
                         uint32_t    height,
                         RenderConfig   _config) : mWidth(width), mHeight(height), mAppName(name), config(_config) {
    initWindow(name, width, height);
}
Application::Application(const char* name,  std::string configPath) :  mAppName(name), config(configPath) {
    mWidth = config.getWindowWidth();
    mHeight = config.getWindowHeight();
    initWindow(name, mWidth, mHeight);
}

Application::~Application() {
    scene.reset();
    camera.reset();
    renderContext.reset();
    gui.reset();
    device.reset();
    _instance.reset();
    window.reset();
}

/**
 * @brief init window
 */
void Application::initWindow(const char* name, uint32_t width, uint32_t height) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window = std::make_unique<Window>(Window::WindowProp{name, {width, height}}, this);
}

/**
 * @brief Initialize Vulkan components:
 * - Initialize Vulkan loader
 * - Create Vulkan instance
 * - Create surface
 * - Select physical device
 * - Create logical device
 */
void Application::prepare() {

    initLogger();

    initVk();
    initGUI();

    TextureHelper::Initialize();
    RenderPtrManangr::Initalize();
    SamplerManager::Initialize(*device);

    camera = std::make_shared<Camera>();
    mPostProcessPass = std::make_unique<PostProcess>();
    mPostProcessPass->init();
}

/**
 * @brief Initialize Vulkan components:
 * - Initialize Vulkan loader
 * - Create Vulkan instance
 * - Create surface
 * - Select physical device
 * - Create logical device
 * - Create render context
 * - Create fence for synchronization
 */
void Application::initVk() {
    VK_CHECK_RESULT(volkInitialize());

    getRequiredInstanceExtensions();


    _instance = std::make_unique<Instance>(std::string("vulkanApp"), instanceExtensions, validationLayers);
    surface   = window->createSurface(*_instance);

    uint32_t physical_device_count{0};
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(_instance->getHandle(), &physical_device_count, nullptr));

    if (physical_device_count < 1) {
        throw std::runtime_error("Couldn't find a physical device that supports Vulkan.");
    }

    std::vector<VkPhysicalDevice> physical_devices;
    physical_devices.resize(physical_device_count);

    LOGI("Found {} physical device", physical_device_count);

    VK_CHECK_RESULT(
        vkEnumeratePhysicalDevices(_instance->getHandle(), &physical_device_count, physical_devices.data()));

    addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    addDeviceExtension(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
    addDeviceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    addDeviceExtension( VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physical_devices[0], &deviceProperties);
    LOGI("Device Name: {}", deviceProperties.deviceName)


    device = std::make_unique<Device>(physical_devices[0], surface, _instance->getHandle(), deviceExtensions);

    createRenderContext();

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK_RESULT(vkCreateFence(device->getHandle(), &fenceInfo, nullptr, &fence));
}

void Application::initGUI() {
    gui = std::make_unique<Gui>(*device);
    gui->prepareResoucrces(this);
}
void Application::initLogger() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink    = std::make_shared<spdlog::sinks::basic_file_sink_mt>(mAppName, true);
    spdlog::logger logger(mAppName, {console_sink, file_sink});
    spdlog::set_default_logger(std::make_shared<spdlog::logger>(logger));
}

void Application::getRequiredInstanceExtensions() {
    uint32_t     glfwExtensionsCount = 0;
    const char** glfwExtensions      = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
    for (uint32_t i = 0; i < glfwExtensionsCount; i++) {
        addInstanceExtension(glfwExtensions[i]);
    }
    if (enableValidationLayers)
        addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

void Application::createRenderContext() {
    renderContext = std::make_unique<RenderContext>(*device, surface, *window);
    g_context     = renderContext.get();
}

void Application::mainloop() {
    while (!glfwWindowShouldClose(window->getHandle())) {
        glfwPollEvents();
        update();
    }
}

/**
 * @brief Updates the application state.
 *
 * - Calculates the delta time since the last frame.
 * - Checks if the application is focused;
 * - Calls the onViewUpdated() method.
 * - Updates the scene and GUI.
 * - Begins a new frame in the render context.
 * - Waits for the fence to ensure synchronization.
 * - Resets the fence for the next frame.
 * ------------------------- Render Graph -------------------------
 * - Creates a render graph and imports the current hardware texture.
 * - Draws the frame and renders the post-process pass.
 * - Updates the list of current textures.
 * - Adds an image copy pass to the render graph.
 * - Handles saving the image if required.
 * - Adds the GUI pass to the render graph.
 * - Executes the render graph using the graphic command buffer.
 * ------------------------- Render Graph -------------------------
 * - Submits the command buffer and presents the frame.
 * - Resets the image save state.
 * - Updates the camera based on the delta time.
 */

void Application::update() {

    deltaTime = timer.tick<Timer::Seconds>();
    if (!m_focused)
        return;

    if (viewUpdated) {
        viewUpdated = false;
        onViewUpdated();
    }

    if(reloadShader) {
        device->getResourceCache().reloadShaders();
        reloadShader = false;
    }


    renderContext->beginFrame();
    updateGUI();

    vkWaitForFences(device->getHandle(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device->getHandle(), 1, &fence);

    RenderGraph graph(*device);
    graph.importTexture(RENDER_VIEW_PORT_IMAGE_NAME, &renderContext->getCurHwtexture());

    if (scene && scene->getLoadCompleteInfo().GetSceneLoaded()) {
        if(sceneFirstLoad){
            onSceneLoaded();
        }
        updateScene();
        drawFrame(graph);
        mPostProcessPass->render(graph);
    }

    mCurrentTextures = graph.getResourceNames(RenderResourceType::ETexture);
    graph.addImageCopyPass(graph.getBlackBoard().getHandle(mPresentTexture), graph.getBlackBoard().getHandle(RENDER_VIEW_PORT_IMAGE_NAME));
    handleSaveImage(graph);

    gui->addGuiPass(graph);

    graph.execute(renderContext->getGraphicCommandBuffer());

    renderContext->submitAndPresent(renderContext->getGraphicCommandBuffer(), fence);

    resetImageSave();

    camera->update(deltaTime);

    if (camera->moving()) {
        viewUpdated = true;
    }

    perFrameUpdate();


}
void Application::perFrameUpdate() {
}

void Application::updateScene() {
    if (sceneAsync != nullptr && sceneAsync->getLoadCompleteInfo().GetSceneLoaded() ) {
        scene = std::move(sceneAsync);
        onSceneLoaded();
        sceneAsync = nullptr;
    }
    if(view)
        view->perFrameUpdate();
}
/**
 * @brief Update GUI
 *
 *
 * Sets the display size and delta time for ImGui;
 * Starts a new frame;
 * Renders various GUI elements such as:
 * - frame time
 * - FPS
 * - file dialogs
 */
void Application::updateGUI() {
    if (!gui)
        return;

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = ImVec2(static_cast<float>(mWidth), static_cast<float>(mHeight));
    io.DeltaTime   = 0;

    gui->newFrame();

    ImGui::Begin("Basic", nullptr, ImGuiWindowFlags_NoMove);

    ImGui::Text("%.2f ms/frame ", 1000.f * deltaTime);
    ImGui::NextColumn();
    ImGui::Text(" %d fps", toUint32(1.f / deltaTime));
    ImGui::Checkbox("save png", &imageSave.savePng);
    ImGui::Checkbox("save exr", &imageSave.saveExr);
    ImGui::Checkbox("save camera config", &saveCamera);
    ImGui::Checkbox("reload shader", &reloadShader);

    auto file = gui->showFileDialog("Select gltf or json file", {".gltf", ".json"});

    if (file != "no file selected") {
        ctpl::thread_pool pool(1);
        pool.push([this, file](size_t) {
            LOGI("file selected: {}", file);
            sceneAsync = SceneLoaderInterface::LoadSceneFromFile(*device, file, sceneLoadingConfig);
        });
    }

    auto                     itemIter    = std::ranges::find(mCurrentTextures.begin(), mCurrentTextures.end(), mPresentTexture);
    int                      itemCurrent = itemIter - mCurrentTextures.begin();
    std::vector<const char*> currentTexturesCStr;
    std::ranges::transform(mCurrentTextures.begin(), mCurrentTextures.end(), std::back_inserter(currentTexturesCStr), [](const std::string& str) { return str.c_str(); });
    ImGui::Combo("RenderGraphTextures", &itemCurrent, currentTexturesCStr.data(), currentTexturesCStr.size());
    mPresentTexture = mCurrentTextures[itemCurrent];

    ImGui::End();
    ImGui::Separator();

    ImGui::Begin("camera", nullptr, ImGuiWindowFlags_NoMove);
    camera->onShowInEditor();
    ImGui::End();
    ImGui::Separator();

    ImGui::Begin("app sepcify", nullptr, ImGuiWindowFlags_NoMove);
    onUpdateGUI();
    ImGui::End();

    ImGui::Separator();

    mPostProcessPass->updateGui();
    if(view) view->updateGui();

    auto& texture = g_context->getCurHwtexture();

    ImGui::SameLine();

    ImGui::Begin("Render view port", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoMouseInputs);// Leave room for 1 line below us
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.f, 0.f});

    ImVec2 size;
    size.x   = ImGui::GetWindowWidth();
    size.y   = ImGui::GetWindowHeight();

    ImGui::Image(&texture.getVkImageView(), size, ImVec2(0, 0), ImVec2(1, 1));
    ImGui::PopStyleVar();
    ImGui::End();

    ImGui::Render();
    ImGui::EndFrame();

    if (gui->update() || gui->updated) {
        gui->updated = false;
    }
}

void Application::resetImageSave() {
    if (imageSave.savePng | imageSave.saveExr) {

        auto width  = g_context->getViewPortExtent().width;
        auto height = g_context->getViewPortExtent().height;

        std::shared_ptr<std::vector<uint8_t>> data   = std::make_shared<std::vector<uint8_t>>(imageSave.buffer->getSize());
        auto                                  mapped = imageSave.buffer->map();
        memcpy(data->data(), mapped, data->size());
        imageSave.buffer->unmap();

        auto sceneFolderPath = scene->getPath().parent_path().string();
        auto imagePath = sceneFolderPath + "/output";
        ImageIO::saveImage(imagePath.c_str(), data, width, height, 4, imageSave.savePng, imageSave.saveExr);

        imageSave.saveExr = false;
        imageSave.savePng = false;
    }

    if (saveCamera) {
        Config::GetInstance().CameraToConfig(*camera,scene->getName());
        Config::GetInstance().SaveConfig();
        saveCamera = false;
    }
}

void Application::handleSaveImage(RenderGraph& graph) {
    if (imageSave.saveExr | imageSave.savePng) {
        graph.addComputePass(
            "image to file ",
            [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
                RenderGraphHandle image;
                if(imageSave.savePng)
                    image = graph.getBlackBoard().getHandle(getLdrImageToSave());
                else {
                     image = graph.getBlackBoard().getHandle(getHdrImageToSave());
                }
                builder.readTexture(image, TextureUsage::TRANSFER_SRC);

                //Not really write to image. Avoid pass cut
                builder.writeTexture(image, TextureUsage::TRANSFER_SRC);
            },
            [&](RenderPassContext& context) {
                auto& swapchainImage = graph.getBlackBoard().getHwImage(imageSave.savePng ? getLdrImageToSave() : getHdrImageToSave());
                auto  width          = swapchainImage.getExtent2D().width;
                auto  height         = swapchainImage.getExtent2D().height;
                auto  stride = imageSave.savePng ? 4 : 16;
                if (imageSave.buffer == nullptr || imageSave.buffer->getSize() < width * height * stride) {
                    imageSave.buffer = std::make_unique<Buffer>(*device, swapchainImage.getExtent2D().width * swapchainImage.getExtent2D().height * stride, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
                }
                VkBufferImageCopy region = {.bufferOffset      = 0,
                                            .bufferRowLength   = 0,
                                            .bufferImageHeight = 0,
                                            .imageSubresource  = {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                  .mipLevel       = 0,
                                                                  .baseArrayLayer = 0,
                                                                  .layerCount     = 1},
                                            .imageOffset       = {0, 0, 0},
                                            .imageExtent       = {width, height, 1}};
                vkCmdCopyImageToBuffer(context.commandBuffer.getHandle(), swapchainImage.getVkImage().getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imageSave.buffer->getHandle(), 1, &region);
            });
    }
}

void Application::setFocused(bool focused) {
    m_focused = focused;
}

// void Application::loadScene(const std::string& path,const SceneLoadingConfig & config)
// {
//     scene = GltfLoading::LoadSceneFromGLTFFile(*device,FileUtils::getResourcePath(path));
//     camera = scene->getCameras()[0];
// }

void Application::inputEvent(const InputEvent& inputEvent) {
    if (gui) {
        if (gui->inputEvent(inputEvent))
            return;
    }

    auto source = inputEvent.getSource();
    if (source == EventSource::KeyBoard) {
        const auto& keyEvent = static_cast<const KeyInputEvent&>(inputEvent);
        auto        action   = keyEvent.getAction();
        auto        code     = keyEvent.getCode();

        switch (action) {
            case KeyAction::Down:
                switch (code) {
                    case KeyCode::W:
                        camera->keys.up = true;
                        break;
                    case KeyCode::S:
                        camera->keys.down = true;
                        break;
                    case KeyCode::A:
                        camera->keys.left = true;
                        break;
                    case KeyCode::D:
                        camera->keys.right = true;
                        break;
                    case KeyCode::Up:
                        camera->keys.up = true;
                        break;
                    case KeyCode::Down:
                        camera->keys.down = true;
                        break;
                    case KeyCode::Left:
                        camera->keys.left = true;
                        break;
                    case KeyCode::Right:
                        camera->keys.right = true;
                        break;
                    default:
                        break;
                }
                break;
            case KeyAction::Up:
                switch (code) {
                    case KeyCode::W:
                        camera->keys.up = false;
                        break;
                    case KeyCode::S:
                        camera->keys.down = false;
                        break;
                    case KeyCode::A:
                        camera->keys.left = false;
                        break;
                    case KeyCode::D:
                        camera->keys.right = false;
                        break;
                    case KeyCode::Up:
                        camera->keys.up = false;
                        break;
                    case KeyCode::Down:
                        camera->keys.down = false;
                        break;
                    case KeyCode::Left:
                        camera->keys.left = false;
                        break;
                    case KeyCode::Right:
                        camera->keys.right = false;
                        break;
                    default:
                        break;
                }
                break;
            case KeyAction::Repeat:
                break;
            case KeyAction::Unknown:
                break;
        }
    }
    if (source == EventSource::Mouse) {
        const auto& mouseEvent = static_cast<const MouseButtonInputEvent&>(inputEvent);
        handleMouseMove(mouseEvent.getPosX(), mouseEvent.getPosY());
        auto action = mouseEvent.getAction();
        auto button = mouseEvent.getButton();
        switch (action) {
            case MouseAction::Down:
                switch (button) {
                    case MouseButton::Left:
                        camera->mouseButtons.left = true;
                        break;
                    case MouseButton::Right:
                        camera->mouseButtons.right = true;
                        break;
                    case MouseButton::Middle:
                        camera->mouseButtons.middle = true;
                        break;
                    default:
                        break;
                }
                break;
            case MouseAction::Up:
                switch (button) {
                    case MouseButton::Left:
                        camera->mouseButtons.left = false;
                        break;
                    case MouseButton::Right:
                        camera->mouseButtons.right = false;
                        break;
                    case MouseButton::Middle:
                        camera->mouseButtons.middle = false;
                        break;
                    default:
                        break;
                }
                break;
            case MouseAction::Move:
                break;
            case MouseAction::Unknown:
                break;
        }
    } else if (source == EventSource::TouchScreen) {
        const auto& touchEvent = static_cast<const TouchInputEvent&>(inputEvent);

        if (touchEvent.getAction() == TouchAction::Down) {
            //   touchDown = true;
            touchPos.x                = static_cast<int32_t>(touchEvent.getPosX());
            touchPos.y                = static_cast<int32_t>(touchEvent.getPosY());
            mousePos.x                = touchEvent.getPosX();
            mousePos.y                = touchEvent.getPosY();
            camera->mouseButtons.left = true;
        } else if (touchEvent.getAction() == TouchAction::Up) {
            touchPos.x = static_cast<int32_t>(touchEvent.getPosX());
            touchPos.y = static_cast<int32_t>(touchEvent.getPosY());
            //   touchTimer = 0.0;
            //   touchDown = false;
            camera->keys.up           = false;
            camera->mouseButtons.left = false;
        } else if (touchEvent.getAction() == TouchAction::Move) {
            bool handled = false;
            if (gui) {
                ImGuiIO& io = ImGui::GetIO();
                handled     = io.WantCaptureMouse;
            }
            if (!handled) {
                int32_t eventX = static_cast<int32_t>(touchEvent.getPosX());
                int32_t eventY = static_cast<int32_t>(touchEvent.getPosY());

                float deltaX = static_cast<float>(touchPos.y - eventY) * rotationSpeed * 0.5f;
                float deltaY = static_cast<float>(touchPos.x - eventX) * rotationSpeed * 0.5f;

                camera->pitch(math::toRadians(deltaX));
                camera->rotateY(math::toRadians(deltaY));
                // camera->rotate(glm::vec3(deltaX, 0.0f, 0.0f));
                // camera->rotate(glm::vec3(0.0f, -deltaY, 0.0f));

                rotation.x += deltaX;
                rotation.y -= deltaY;

                // viewChanged();

                touchPos.x = eventX;
                touchPos.y = eventY;
            }
        }
    }
}

void Application::onResize(uint32_t width, uint32_t height) {
    this->mWidth  = width;
    this->mHeight = height;
}

void Application::loadScene(const std::string& path) {
    scene = SceneLoaderInterface::LoadSceneFromFile(*device, path, sceneLoadingConfig);
    //RuntimeSceneManager::addSponzaRestirLight(*scene);
}

void Application::onSceneLoaded() {
    if(!scene->getCameras().empty()) {
        camera = scene->getCameras()[0];
    }
    initView();
    Config::GetInstance().CameraFromConfig(*camera,scene->getName());
    sceneFirstLoad = false;


}

void Application::initView() {
    view = std::make_unique<View>(*device);
    if (scene->getCameras().empty()) {
        camera = std::make_shared<Camera>();
        camera->setPerspective(60.0f, (float)mWidth / (float)mHeight, 0.1f, 256.0f);
    }
    view->setScene(scene.get());
    view->setCamera(camera.get());

   // RenderPtrManangr::init();
    g_manager->putPtr("view", view.get());
}


void Application::handleMouseMove(float x, float y) {
    float dx      = static_cast<int32_t>(mousePos.x) - x;
    float dy      = static_cast<int32_t>(mousePos.y) - y;
    onMouseMove();

    if (camera->mouseButtons.right) {
        rotation.x += dy * 1.25f * rotationSpeed;
        rotation.y -= dx * 1.25f * rotationSpeed;
        //camera->rotate(glm::vec3(dy * camera->rotationSpeed, -dx * camera->rotationSpeed, 0.0f));
        camera->pitch(math::toRadians(dy * 0.1f));
        camera->rotateY(math::toRadians(0.1f * dx));
        viewUpdated = true;
    }
    if (camera->mouseButtons.left) {
        camera->translate(glm::vec3(-0.0f, 0.0f, dy * .005f));
        viewUpdated = true;
    }
    if (camera->mouseButtons.middle) {
        camera->translate(glm::vec3(-dx * 0.01f, -dy * 0.01f, 0.0f));
        viewUpdated = true;
    }
    mousePos = glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

void Application::onUpdateGUI() {
}

void Application::onMouseMove() {
}

void Application::onViewUpdated() {
}
void Application::preparePerViewData() {
}

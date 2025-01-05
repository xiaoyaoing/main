#pragma once

#include "Core/Buffer.h"
#include "Core/Device/Device.h"
#include "PlatForm/Window.h"
#include "Core/RenderContext.h"
#include "Scene/Compoments/Camera.h"
#include "Gui/InputEvent.h"
#include "Gui/Gui.h"

#include <vk_mem_alloc.h>
#include <vector>

#include "Common/RenderConfig.h"
#include "Common/Timer.h"
#include "Core/View.h"
#include "RenderPasses/RenderPassBase.h"
#include "Scene/SceneLoader/SceneLoadingConfig.h"

// #ifdef _WIN32
// #include <minwindef.h>
// #include <WinUser.h>
// #include <windef.h>
// #endif

#define EXAMPLE_MAIN                  \
    int main(int argc, char** argv) { \
        Example app;                  \
        app.prepare();                \
        app.mainloop();               \
        return 0;                     \
    }

#define MAIN(APP_NAME)        \
    int main(int argc, char** argv) { \
        APP_NAME app;             \
        app.prepare();                \
        app.mainloop();               \
        return 0;                     \
    }


class Application {
    /**
     * @brief Initializes the window for the application.
     */
    void initWindow(const char* name, uint32_t width, uint32_t height);

    /**
     * @brief Initializes the GUI for the application.
     */
    virtual void initGUI();

    void initLogger();

public:
    Application(const char* name, uint32_t width, uint32_t height,RenderConfig config = RenderConfig());
    Application(const char* name, std::string configPath);
    Application() : Application("Vulkan", 1920, 1080) {
    }
    virtual ~Application();

    virtual void prepare();
    virtual void inputEvent(const InputEvent& inputEvent);

    void         setFocused(bool focused);
    void         mainloop();
    void         onResize(uint32_t width, uint32_t height);
    void         initView();
    void         loadScene(const std::string& path);
    virtual void onSceneLoaded();

protected:
    inline void addDeviceExtension(const char* extension, bool optional = true) {
        deviceExtensions[extension] = optional;
    }
    inline void addInstanceExtension(const char* extension, bool optional = true) {
        instanceExtensions[extension] = optional;
    }

    virtual void update();
    virtual void perFrameUpdate();
    virtual void getRequiredInstanceExtensions();
    virtual void initVk();
    virtual void drawFrame(RenderGraph& rg) = 0;
    virtual void updateScene();
    virtual void onUpdateGUI();
    virtual void onMouseMove();
    virtual void onViewUpdated();
    virtual void preparePerViewData();
    virtual std::string getLdrImageToSave() {return RENDER_VIEW_PORT_IMAGE_NAME;}
    virtual std::string getHdrImageToSave() {return RENDER_VIEW_PORT_IMAGE_NAME;}

    void updateGUI();
    void createRenderContext();

    void handleSaveImage(RenderGraph& graph);
    void resetImageSave();
    //void loadScene(const std::string & path);
protected:
    VmaAllocator _allocator{};

    float deltaTime{0};

    Timer timer;

    std::unique_ptr<Instance>             _instance;
    std::unordered_map<const char*, bool> deviceExtensions;
    std::unordered_map<const char*, bool> instanceExtensions;
    std::vector<const char*>              validationLayers{"VK_LAYER_KHRONOS_validation"};
    std::unique_ptr<Window>               window{nullptr};
    std::unique_ptr<RenderContext>        renderContext{nullptr};
    std::unique_ptr<Device>               device{nullptr};
    std::shared_ptr<Camera>               camera;

    std::unique_ptr<Scene> scene;
    std::unique_ptr<Scene> sceneAsync{nullptr};
    std::unique_ptr<View>  view;
    std::unique_ptr<Gui>   gui;

    VkSurfaceKHR surface{};

    uint32_t  mWidth, mHeight;
    bool      m_focused{true};
    glm::vec2 mousePos;

    glm::vec2 touchPos;

    glm::vec3 rotation;

    bool viewUpdated{false};

    float rotationSpeed{1};

    bool               enableGui{true};
    uint32_t           frameCounter{0};
    SceneLoadingConfig sceneLoadingConfig;

    struct ImageSave {
        bool                    savePng = false;
        bool                    saveExr = false;
        std::unique_ptr<Buffer> buffer{nullptr};
    } imageSave;

    bool saveCamera{false};
    bool reloadShader{false};

    void handleMouseMove(float x, float y);

protected:
    bool sceneFirstLoad{true};
    RenderConfig config;

private:
    std::string               mPresentTexture = RENDER_VIEW_PORT_IMAGE_NAME;
    std::vector<std::string>  mCurrentTextures{RENDER_VIEW_PORT_IMAGE_NAME};
    const char*               mAppName;
    std::unique_ptr<PassBase> mPostProcessPass{};
    //Camera related  variable end
    VkFence   fence{VK_NULL_HANDLE};
#ifdef NOEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
};

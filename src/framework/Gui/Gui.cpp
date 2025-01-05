//
// Created by pc on 2023/8/23.
//

#include "Gui.h"
#include <utility>
#include "Common/VkCommon.h"
#include "Core/Shader/Shader.h"
#include "Core/Device/Device.h"
#include "App/Application.h"
#include "Common/ResourceCache.h"
#include <algorithm>
#include "imgui.h"
#include "imfilebrowser.h"

Gui::Gui(Device& device) : device(device) {
    // ImGui::CreateContext();
    // // Color scheme
    // ImGuiStyle& style                       = ImGui::GetStyle();
    // style.Colors[ImGuiCol_TitleBg]          = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    // style.Colors[ImGuiCol_TitleBgActive]    = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    // style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.0f, 0.0f, 0.0f, 0.1f);
    // style.Colors[ImGuiCol_MenuBarBg]        = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    // style.Colors[ImGuiCol_Header]           = ImVec4(0.8f, 0.0f, 0.0f, 0.4f);
    // style.Colors[ImGuiCol_HeaderActive]     = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    // style.Colors[ImGuiCol_HeaderHovered]    = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    // style.Colors[ImGuiCol_FrameBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
    // style.Colors[ImGuiCol_CheckMark]        = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
    // style.Colors[ImGuiCol_SliderGrab]       = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    // style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
    // style.Colors[ImGuiCol_FrameBgHovered]   = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
    // style.Colors[ImGuiCol_FrameBgActive]    = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
    // style.Colors[ImGuiCol_Button]           = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    // style.Colors[ImGuiCol_ButtonHovered]    = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
    // style.Colors[ImGuiCol_ButtonActive]     = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
    //
    // // Dimensions
    // ImGuiIO& io        = ImGui::GetIO();
    // io.FontGlobalScale = scale;
    //
    // setColorsDark();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;// Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // // Setup Platform/Renderer backends
    // ImGui_ImplGlfw_InitForVulkan(window, true);
    // ImGui_ImplVulkan_InitInfo init_info = {};
    // init_info.Instance = g_Instance;
    // init_info.PhysicalDevice = g_PhysicalDevice;
    // init_info.Device = g_Device;
    // init_info.QueueFamily = g_QueueFamily;
    // init_info.Queue = g_Queue;
    // init_info.PipelineCache = g_PipelineCache;
    // init_info.DescriptorPool = g_DescriptorPool;
    // init_info.Subpass = 0;
    // init_info.MinImageCount = g_MinImageCount;
    // init_info.ImageCount = wd->ImageCount;
    // init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    // init_info.Allocator = g_Allocator;
    // init_info.CheckVkResultFn = check_vk_result;
    // ImGui_ImplVulkan_Init(&init_info, wd->RenderPass);
    //   io.Fonts->AddFontFromFileTTF((FileUtils::getResourcePath() + "Roboto-Medium.ttf").c_str(), 16.0f);

    // Our state
    bool   show_demo_window    = true;
    bool   show_another_window = false;
    ImVec4 clear_color         = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    ShaderPipelineKey paths{"gui.vert", "gui.frag"};
    pipelineLayout = &device.getResourceCache().requestPipelineLayout(paths);

    // (optional) set browser properties
    fileDialog = new ImGui::FileBrowser();
    //fileDialog->SetTitle("Open scene");
    fileDialog->SetTypeFilters({".gltf", ".json"});

    iniFileName = FileUtils::getResourcePath() + "imgui.ini";
}
Gui::~Gui() {
    ImGui::SaveIniSettingsToDisk(FileUtils::getResourcePath("imgui.ini").c_str());
}

bool Gui::inputEvent(const InputEvent& input_event) {
    auto& io                 = ImGui::GetIO();
    auto  capture_move_event = false;

    if (input_event.getSource() == EventSource::KeyBoard) {

    } else if (input_event.getSource() == EventSource::Mouse) {
        const auto& mouse_button = static_cast<const MouseButtonInputEvent&>(input_event);

        io.MousePos = ImVec2{mouse_button.getPosX() * contentScaleFactor,
                             mouse_button.getPosY() * contentScaleFactor};

        auto button_id = static_cast<int>(mouse_button.getButton());

        if (mouse_button.getAction() == MouseAction::Down) {
            io.MouseDown[button_id] = true;
        } else if (mouse_button.getAction() == MouseAction::Up) {
            io.MouseDown[button_id] = false;
        } else if (mouse_button.getAction() == MouseAction::Move) {
            capture_move_event = io.WantCaptureMouse;
        }
        capture_move_event = io.WantCaptureMouse;

    } else if (input_event.getSource() == EventSource::TouchScreen) {
        const auto& touch_event = static_cast<const TouchInputEvent&>(input_event);

        io.MousePos = ImVec2{touch_event.getPosX(), touch_event.getPosY()};

        if (touch_event.getAction() == TouchAction::Down) {
            io.MouseDown[touch_event.getPointerId()] = true;
        } else if (touch_event.getAction() == TouchAction::Up) {
            io.MouseDown[touch_event.getPointerId()] = false;
        } else if (touch_event.getAction() == TouchAction::Move) {
            capture_move_event = io.WantCaptureMouse;
        }
    }
    return capture_move_event;
}
std::string Gui::showFileDialog(std::string title, std::vector<std::string> fileTypes) {
    // if (ImGui::Begin("dummy window")) {
    // open file dialog when user clicks this button
    if (ImGui::Button(title.c_str())) {
        fileDialog->Open();
        fileDialog->SetTitle(title);
        fileDialog->SetTypeFilters(fileTypes);
    }
    // }
    fileDialog->Display();

    if (fileDialog->HasSelected()) {
        std::string file = fileDialog->GetSelected().string();
        fileDialog->ClearSelected();
        return file;
    }
    return "no file selected";
}


void Gui::prepareResoucrces(Application* app) {
    fontTexture = Texture::loadTextureFromFile(device, FileUtils::getResourcePath() + "Roboto-Medium.ttf");

    vertexInputState.attributes = {
        vkCommon::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert,
                                                                                                        pos)),             // Location 0: Position
        vkCommon::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)),  // Location 1: UV
        vkCommon::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)),// Location 0: Color
    };

    vertexInputState.bindings = {
        vkCommon::initializers::vertexInputBindingDescription(0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX),
    };

    ColorBlendAttachmentState colorBlendAttachmentState;

    colorBlendAttachmentState.blendEnable = VK_TRUE;
    colorBlendAttachmentState.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachmentState.colorBlendOp        = VK_BLEND_OP_ADD;
    colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachmentState.alphaBlendOp        = VK_BLEND_OP_ADD;

    colorBlendState.attachments = {colorBlendAttachmentState};

    ImGui::GetIO().Fonts->SetTexID(&fontTexture->getImage().getVkImageView());

    ImGui::LoadIniSettingsFromDisk(iniFileName.c_str());
}

bool Gui::update() {
    ImDrawData* imDrawData = ImGui::GetDrawData();

    if (!imDrawData) {
        return false;
    };

    // Note: Alignment is done inside buffer creation
    VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize  = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

    if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
        return false;
    }

    // if (mvertexBuffer.size != vertexBufferSize ||
    //     mIndexBuffer.size != indexBufferSize) {
    if (mvertexBuffer.size != vertexBufferSize) {
        mvertexBuffer = g_context->allocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (mIndexBuffer.size != indexBufferSize)
        mIndexBuffer = g_context->allocateBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    uint64_t vtxOffset = mvertexBuffer.offset, idxOffset = mIndexBuffer.offset;
    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
        const ImDrawList* cmd_list = imDrawData->CmdLists[n];
        mvertexBuffer.buffer->uploadData(cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), vtxOffset);
        mIndexBuffer.buffer->uploadData(cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), idxOffset);
        vtxOffset += cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
        idxOffset += cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
    }
    return true;
}

void Gui::addGuiPass(RenderGraph& graph) {
    auto& renderContext = *g_context;
    graph.addGraphicPass(
        "Gui Pass",
        [&graph](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto renderOutput = graph.getBlackBoard()[RENDER_VIEW_PORT_IMAGE_NAME];
            auto swapChain    = graph.importTexture("swap_chain", &g_context->getSwapChainImage(), true);
            builder.readTexture(renderOutput, RenderGraphTexture::Usage::SAMPLEABLE);
            builder.writeTexture(swapChain, RenderGraphTexture::Usage::COLOR_ATTACHMENT);
            builder.declare(RenderGraphPassDescriptor({swapChain}, RenderGraphSubpassInfo{.outputAttachments = {swapChain}}));
        },
        [&renderContext, this](const RenderPassContext& context) {
            ImDrawData* imDrawData   = ImGui::GetDrawData();
            int32_t     vertexOffset = 0;
            int32_t     indexOffset  = 0;
            if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
                return;
            }
            const ImGuiIO& io = ImGui::GetIO();

            renderContext.getPipelineState().setPipelineLayout(*pipelineLayout).setVertexInputState(vertexInputState).setColorBlendState(colorBlendState).setRasterizationState({.cullMode = VK_CULL_MODE_NONE});

            pushConstBlock.scale        = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
            pushConstBlock.translate    = glm::vec2(-1.0f);
            pushConstBlock.flipViewPort = g_context->getFlipViewport();

            renderContext.bindPushConstants(pushConstBlock);

            // auto & renderOutput = context.renderGraph.getBlackBoard().getImageView(RENDER_VIEW_PORT_IMAGE_NAME);

            std::vector<const Buffer*> vertexBuffers = {mvertexBuffer.buffer};

            context.commandBuffer.bindVertexBuffer(vertexBuffers, {mvertexBuffer.offset});
            context.commandBuffer.bindIndicesBuffer(*mIndexBuffer.buffer, mIndexBuffer.offset);

            for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
                const ImDrawList* cmd_list = imDrawData->CmdLists[i];
                for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {

                    const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];

                    auto texture = static_cast<ImageView*>(pcmd->GetTexID());
                    auto & renderOutput = context.renderGraph.getBlackBoard().getImageView(RENDER_VIEW_PORT_IMAGE_NAME);
                    renderContext.bindImageSampler(0, *texture, fontTexture->getSampler());
                    renderContext.flushAndDrawIndexed(
                        context.commandBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
                    indexOffset += pcmd->ElemCount;
                }
                vertexOffset += cmd_list->VtxBuffer.Size;
            }
        });
}
void Gui::setColorsDark() {
    auto& colors = ImGui::GetStyle().Colors;

    colors[ImGuiCol_Text]                  = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.01f, 0.01f, 0.01f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.11f, 0.11f, 0.14f, 0.92f);
    colors[ImGuiCol_Border]                = ImVec4(0.08f, 0.07f, 0.10f, 0.50f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.05f, 0.05f, 0.05f, 0.39f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.15f, 0.15f, 0.15f, 0.39f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.36f, 0.36f, 0.36f, 0.39f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.01f, 0.00f, 0.03f, 0.83f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.02f, 0.01f, 0.06f, 0.83f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.02f, 0.01f, 0.05f, 0.83f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.00f, 0.00f, 0.02f, 0.80f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.04f, 0.08f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.43f, 0.26f, 0.73f, 0.60f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.55f, 0.39f, 0.81f, 0.60f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.69f, 0.60f, 0.82f, 0.60f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.91f, 0.76f, 0.09f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.41f, 0.39f, 0.80f, 0.60f);
    colors[ImGuiCol_Button]                = ImVec4(0.18f, 0.04f, 0.39f, 0.62f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.27f, 0.13f, 0.49f, 0.62f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.45f, 0.25f, 0.75f, 0.62f);
    colors[ImGuiCol_Header]                = ImVec4(0.05f, 0.03f, 0.12f, 0.62f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.10f, 0.06f, 0.22f, 0.62f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.12f, 0.06f, 0.30f, 0.62f);
    colors[ImGuiCol_Separator]             = ImVec4(0.13f, 0.11f, 0.17f, 0.68f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.17f, 0.16f, 0.21f, 0.68f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.22f, 0.21f, 0.25f, 0.68f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.78f, 0.82f, 1.00f, 0.60f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.78f, 0.82f, 1.00f, 0.90f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.09f, 0.04f, 0.14f, 0.45f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.13f, 0.08f, 0.18f, 0.45f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.20f, 0.16f, 0.25f, 0.45f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.14f, 0.07f, 0.42f, 0.45f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.20f, 0.13f, 0.45f, 0.45f);
    colors[ImGuiCol_DockingPreview]        = ImVec4(0.52f, 0.40f, 0.90f, 0.31f);
    colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.15f, 0.07f, 0.42f, 0.45f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.09f, 0.04f, 0.30f, 0.45f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.14f, 0.34f, 0.45f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.00f, 0.00f, 1.00f, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(0.45f, 0.45f, 0.90f, 0.80f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

    auto& style             = ImGui::GetStyle();
    style.TabRounding       = 4;
    style.ScrollbarRounding = 9;
    style.WindowRounding    = 7;
    style.GrabRounding      = 3;
    style.FrameRounding     = 3;
    style.PopupRounding     = 4;
    style.ChildRounding     = 4;
}

bool Gui::checkBox(const char* caption, bool* value) {
    bool res = ImGui::Checkbox(caption, value);
    if (res) { updated = true; };
    return res;
}

void Gui::text(const char* formatstr, ...) {
    va_list args;
    va_start(args, formatstr);
    ImGui::TextV(formatstr, args);
    va_end(args);
}
void Gui::newFrame() {
    ImGuiIO& io = ImGui::GetIO();

    // Setup display size (every frame to accommodate for window resizing)
    // int w, h;
    // int display_w, display_h;
    // io.DisplaySize = ImVec2((float)w, (float)h);
    // if (w > 0 && h > 0)
    //     io.DisplayFramebufferScale = ImVec2((float)display_w / (float)w, (float)display_h / (float)h);

    // Setup time step
    double current_time = glfwGetTime();
    io.DeltaTime        = mTime > 0.0 ? (float)(current_time - mTime) : (float)(1.0f / 60.0f);
    mTime               = current_time;

    ImGui::NewFrame();
}

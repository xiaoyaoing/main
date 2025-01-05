#pragma once

#include <RenderGraph/RenderGraphId.h>
#include <unordered_map>
#include <string>

class Buffer;
class Image;
class ImageView;
class SgImage;

class RenderGraph;

#define REGISTER_RENDER_GRAPH_TEXTURE(name) static const std::string name = #name;
REGISTER_RENDER_GRAPH_TEXTURE(RENDER_VIEW_PORT_IMAGE_NAME)
REGISTER_RENDER_GRAPH_TEXTURE(RT_IMAGE_NAME)
REGISTER_RENDER_GRAPH_TEXTURE(NORMAL_RG)
REGISTER_RENDER_GRAPH_TEXTURE(ALBEDO_RG)
REGISTER_RENDER_GRAPH_TEXTURE(UV_RG)
REGISTER_RENDER_GRAPH_TEXTURE(VBUFFER_RG)
REGISTER_RENDER_GRAPH_TEXTURE(EMISSION_RG)


static const std::string DEPTH_IMAGE_NAME = "_DEPTH_IMAGE_NAME_";

class Blackboard {
    using Container = std::unordered_map<
        std::string,
        RenderGraphHandle>;

public:
    Blackboard(const RenderGraph& graph) noexcept;
    ~Blackboard() noexcept;

    RenderGraphHandle& operator[](const std::string& name) noexcept;
    void               put(const std::string& name, RenderGraphHandle handle) noexcept;

    RenderGraphHandle getHandle(const std::string& name) const noexcept;
    Image&            getImage(const std::string& name) const noexcept;
    const ImageView&  getImageView(const std::string& name) const noexcept;
    const SgImage&    getHwImage(const std::string& name) const noexcept;
    const Buffer&     getBuffer(const std::string& name) const noexcept;
    bool              contains(const std::string& name) const noexcept;

    void remove(const std::string& name) noexcept;

private:
    // RenderGraphHandle getHandle(const std::string & name) const noexcept;
    Container          mMap;
    const RenderGraph& graph;
};

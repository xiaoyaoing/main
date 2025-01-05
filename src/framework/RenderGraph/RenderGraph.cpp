#include "RenderGraph.h"
#include "Core/CommandBuffer.h"
#include "Core/Pipeline.h"
#include "Core/Texture.h"
#include "Scene/SceneLoader/gltfloader.h"

#include <stack>

// void RenderGraph::Builder::read(VirtualResource* resource, PassNode* node)
// {
// }
//
// void RenderGraph::Builder::write(VirtualResource* resource, PassNode* node)
// {
// }

void RenderGraph::Builder::declare(const RenderGraphPassDescriptor& desc) {
    auto rNode = static_cast<GraphicsPassNode*>(node);
    rNode->declareRenderPass(desc);

    for (const auto& subpass : desc.subpasses) {
        for (auto inputAtt : subpass.inputAttachments) {
            readTexture(inputAtt);
        }
        for (auto outputAtt : subpass.outputAttachments) {
            writeTexture(outputAtt);
        }
    }
}

RenderGraph::RenderGraph(Device& device) : device(device) {
    mBlackBoard = std::make_unique<Blackboard>(*this);
}

RenderGraph::Builder& RenderGraph::Builder::readTexture(RenderGraphHandle input, RenderGraphTexture::Usage usage) {
    auto texture = renderGraph.getTexture(input);
    if (usage == RenderGraphTexture::Usage::NONE)
        usage = texture->isDepthStencilTexture() ? RenderGraphTexture::Usage::DEPTH_READ_ONLY : RenderGraphTexture::Usage::READ_ONLY;
    renderGraph.edges.emplace_back(Edge{.pass = node, .resource = texture, .usage = static_cast<uint16_t>(usage), .read = true});
    return *this;
}

RenderGraph::Builder& RenderGraph::Builder::writeTexture(RenderGraphHandle output, RenderGraphTexture::Usage usage) {
    auto texture = renderGraph.getTexture(output);
    if (usage == RenderGraphTexture::Usage::NONE)
        usage = texture->isDepthStencilTexture() ? RenderGraphTexture::Usage::DEPTH_ATTACHMENT : RenderGraphTexture::Usage::COLOR_ATTACHMENT;
    renderGraph.edges.emplace_back(Edge{.pass = node, .resource = texture, .usage = static_cast<uint16_t>(usage), .read = false});
    return *this;
}


RenderGraph::Builder& RenderGraph::Builder::writeTexture(const std::string& name, RenderGraphTexture::Usage usage) {
    auto handle = renderGraph.getBlackBoard().getHandle(name);
    return writeTexture(handle, usage);
}
RenderGraph::Builder& RenderGraph::Builder::readTexture(const std::string& name, RenderGraphTexture::Usage usage) {
    auto handle = renderGraph.getBlackBoard().getHandle(name);
    return readTexture(handle, usage);
}
RenderGraph::Builder& RenderGraph::Builder::readAndWriteTexture(RenderGraphHandle input, RenderGraphTexture::Usage usage) {
    readTexture(input, usage);
    writeTexture(input, usage);
    return *this;
}
RenderGraph::Builder& RenderGraph::Builder::readTextures(const std::vector<RenderGraphHandle>& inputs, RenderGraphTexture::Usage usage) {
    for (auto& input : inputs) {
        readTexture(input, usage);
    }
    return *this;
}
RenderGraph::Builder& RenderGraph::Builder::writeTextures(const std::vector<RenderGraphHandle>& output, RenderGraphTexture::Usage usage) {
    for (auto& out : output) {
        writeTexture(out, usage);
    }
    return *this;
}

RenderGraph::Builder& RenderGraph::Builder::readBuffer(RenderGraphHandle input, RenderGraphBuffer::Usage usage) {
    auto buffer = renderGraph.getBuffer(input);
    renderGraph.edges.emplace_back(Edge{.pass = node, .resource = buffer, .usage = static_cast<uint16_t>(usage), .read = true});
    return *this;

}

RenderGraph::Builder& RenderGraph::Builder::writeBuffer(RenderGraphHandle output, RenderGraphBuffer::Usage usage) {
    auto buffer = renderGraph.getBuffer(output);
    renderGraph.edges.emplace_back(Edge{.pass = node, .resource = buffer, .usage = static_cast<uint16_t>(usage), .read = false});
    return *this;
}

RenderGraphHandle RenderGraph::createBuffer(const std::string& name, const RenderGraphBuffer::Descriptor& desc) {
    auto buffer = new RenderGraphBuffer(name, desc);
    return addBuffer(buffer);
}

RenderGraphHandle RenderGraph::importBuffer(const std::string& name, Buffer* hwBuffer) {
    auto buffer = new RenderGraphBuffer(name, hwBuffer);
    return addBuffer(buffer);
}

RenderGraphTexture* RenderGraph::getTexture(RenderGraphHandle handle) const {
    auto resource = getResource(handle);
    CHECK_RESULT(resource->getType() == RenderResourceType::ETexture);
    return static_cast<RenderGraphTexture*>(resource);
}

RenderGraphBuffer* RenderGraph::getBuffer(RenderGraphHandle handle) const {
    auto resource = getResource(handle);
    CHECK_RESULT(resource->getType() == RenderResourceType::EBuffer);
    return static_cast<RenderGraphBuffer*>(resource);
}

RenderGraphHandle RenderGraph::addBuffer(RenderGraphBuffer* buffer) {
    const RenderGraphHandle handle(mResources.size());
    buffer->handle = handle;
    mResources.push_back(buffer);
    mBlackBoard->put(buffer->getName(), handle);
    return handle;
}

RenderGraphHandle RenderGraph::createTexture(const std::string& name, const RenderGraphTexture::Descriptor& desc) {
    if (mBlackBoard->contains(name)) {
        LOGI("Texture with name {} already exists in render graph", name);
        return mBlackBoard->getHandle(name);
    }
    auto texture = new RenderGraphTexture(name, desc);
    return addTexture(texture);
}

std::vector<RenderGraphNode*> RenderGraph::getInComingNodes(RenderGraphNode* node) const {
    if (auto resource = dynamic_cast<ResourceNode*>(node)) {
        std::vector<Edge> inComingEdges{};
        std::ranges::copy_if(edges.begin(), edges.end(), std::back_insert_iterator(inComingEdges), [resource](const Edge& edge) { return edge.inComing(resource); });
        std::vector<RenderGraphNode*> results{};
        std::ranges::transform(inComingEdges.begin(), inComingEdges.end(), std::back_insert_iterator(results), [](const Edge& edge) {
            return edge.pass;
        });
        return results;
    }
    if (auto pass = dynamic_cast<PassNode*>(node)) {
        std::vector<Edge> inComingEdges{};
        std::ranges::copy_if(edges.begin(), edges.end(), std::back_insert_iterator(inComingEdges), [pass](const Edge& edge) { return edge.inComing(pass); });
        std::vector<RenderGraphNode*> results{};
        std::ranges::transform(inComingEdges.begin(), inComingEdges.end(), std::back_insert_iterator(results), [](const Edge& edge) {
            return edge.resource;
        });
        return results;
    }
    LOGE("Node Invalid")
}

std::vector<RenderGraphNode*> RenderGraph::getOutComingNodes(RenderGraphNode* node) const {
    if (auto resource = dynamic_cast<ResourceNode*>(node)) {
        std::vector<Edge> outComingEdges{};
        std::ranges::copy_if(edges.begin(), edges.end(), std::back_insert_iterator(outComingEdges), [resource](const Edge& edge) { return edge.outComing(resource); });
        std::vector<RenderGraphNode*> results{};
        std::ranges::transform(outComingEdges.begin(), outComingEdges.end(), std::back_insert_iterator(results), [](const Edge& edge) {
            return edge.pass;
        });
        return results;
    }
    if (auto pass = dynamic_cast<PassNode*>(node)) {
        std::vector<Edge> outComingEdges{};
        std::ranges::copy_if(edges.begin(), edges.end(), std::back_insert_iterator(outComingEdges), [pass](const Edge& edge) { return edge.outComing(pass); });
        std::vector<RenderGraphNode*> results{};
        std::ranges::transform(outComingEdges.begin(), outComingEdges.end(), std::back_insert_iterator(results), [](const Edge& edge) {
            return edge.resource;
        });
        return results;
    }
    LOGE("Node Invalid")
}

std::vector<const RenderGraph::Edge*> RenderGraph::getEdges(RenderGraphNode* node) const {
    std::vector<const Edge*> results{};

    if (auto resource = dynamic_cast<ResourceNode*>(node)) {
        for (const auto& edge : edges) {
            if (edge.resource == resource)
                results.push_back(&edge);
        }
    }
    if (auto pass = dynamic_cast<PassNode*>(node)) {
        for (const auto& edge : edges) {
            if (edge.pass == pass)
                results.push_back(&edge);
        }
    }
    return results;
}

bool RenderGraph::getCutUnUsedResources() const {
    return cutUnUsedResources;
}
void RenderGraph::setCutUnUsedResources(const bool cut_un_used_resources) {
    cutUnUsedResources = cut_un_used_resources;
}
RenderGraphHandle RenderGraph::addTexture(RenderGraphTexture* texture) {
    if (mBlackBoard->contains(texture->getName())) {
        LOGE("Texture with name %s already exists in render graph", texture->getName());
    }
    const RenderGraphHandle handle(mResources.size());
    if (texture->getName() == DEPTH_IMAGE_NAME)
        texture->addRef();
    mBlackBoard->put(texture->getName(), handle);
    texture->handle = handle;
    mResources.push_back(texture);
    return handle;
}

bool RenderGraph::isWrite(RenderGraphHandle handle, const GraphicsPassNode* passNode) const {
    for (const auto& edge : edges) {
        if (edge.pass == passNode && edge.resource == getResource(handle)) {
            if (!edge.read)
                return true;
        }
    }
    return false;
}

bool RenderGraph::isRead(RenderGraphHandle handle, const GraphicsPassNode* passNode) const {
    for (const auto& edge : edges) {
        if (edge.pass == passNode && edge.resource == getResource(handle)) {
            if (edge.read)
                return true;
        }
    }
    return false;
}

void RenderGraph::setUp() {
}

ResourceNode* RenderGraph::getResource(RenderGraphHandle handle) const {
    return mResources[handle.index];
}

void RenderGraph::addGraphicPass(const std::string& name, const GraphicSetup& setup, GraphicsExecute&& execute) {

    GraphicRenderGraphPass* pass = new GraphicRenderGraphPass(std::move(execute));
    auto                    node = new GraphicsPassNode(*this, name, pass);
    mPassNodes.emplace_back(node);
    Builder builder(node, *this);
    setup(builder, pass->getData());
}

void RenderGraph::addComputePass(const std::string& name, const ComputeSetUp& setup, ComputeExecute&& execute) {
    ComputeRenderGraphPass* pass = new ComputeRenderGraphPass(std::move(execute));
    auto                    node = new ComputePassNode(*this, name, pass);
    mPassNodes.emplace_back(node);
    Builder builder(node, *this);
    setup(builder, pass->getData());
}

void RenderGraph::addRaytracingPass(const std::string& name, const RayTracingSetup& setup, RaytracingExecute&& execute) {
    RaytracingRenderGraphPass* pass = new RaytracingRenderGraphPass(std::move(execute));
    auto                       node = new RayTracingPassNode(*this, name, pass);
    mPassNodes.emplace_back(node);
    Builder builder(node, *this);
    node->addRef();
    setup(builder, pass->getData());
}

void RenderGraph::addImageCopyPass(RenderGraphHandle src, RenderGraphHandle dst) {
    if (src == dst)
        return;
    auto node = new ImageCopyPassNode(src, dst);
    mPassNodes.emplace_back(node);
    Builder builder(node, *this);
    builder.readTexture(src, RenderGraphTexture::Usage::TRANSFER_SRC);
    builder.writeTexture(dst, RenderGraphTexture::Usage::TRANSFER_DST);
}
void RenderGraph::setOutput(RenderGraphHandle resource) {
    getResource(resource)->addRef();
}

void RenderGraph::compile() {
    //first cull Graph

    for (auto edge : edges) {
        if (edge.read)
            edge.resource->addRef();
        else
            edge.pass->addRef();
    }

    if (cutUnUsedResources) {
        std::stack<RenderGraphNode*> stack;
        for (const auto& node : mResources)
            if (needToCutResource(node))
                stack.push(node);
        for (const auto& node : mPassNodes)
            if (node->getRefCount() == 0 || !node->active())
                stack.push(node);
        while (!stack.empty()) {
            const auto node = stack.top();
            stack.pop();
            for (auto inComingNode : getInComingNodes(node)) {
                if (--inComingNode->refCount == 0)
                    stack.push(inComingNode);
            }
        }
    }

    mActivePassNodesEnd = std::stable_partition(this->mPassNodes.begin(), mPassNodes.end(), [](const auto& passNode) {
        return passNode->active();
    });

    for (auto pass = mActivePassNodesEnd; pass != mPassNodes.end(); pass++) {
        LOGI("Pass {0} is not active", (*pass)->getName())
    }

    auto       first = mPassNodes.begin();
    const auto last  = mActivePassNodesEnd;
    while (first != last) {
        PassNode* const passNode = *first;
        first++;
        auto inResources  = getInComingNodes(passNode);
        auto outResources = getOutComingNodes(passNode);

        for (const auto inResource : inResources) {
            const auto resource = dynamic_cast<ResourceNode*>(inResource);
            assert(resource);
            resource->first = resource->first ? resource->first : passNode;
            resource->last  = passNode;
            //   passNode->addTextureUsage(static_cast<const RenderGraphTexture*>(inResource), );
        }
        for (const auto outResource : outResources) {
            const auto resource = dynamic_cast<ResourceNode*>(outResource);
            assert(resource);
            resource->first = resource->first ? resource->first : passNode;
            resource->last  = passNode;
            //passNode->addTextureUsage(static_cast<const RenderGraphTexture*>(inResource), texture->usage);
        }

        for (const auto edge : getEdges(passNode)) {
            passNode->addResourceUsage(edge->resource->handle, edge->usage);
        }
    }

    for (const auto& resource : mResources) {
        if (!needToCutResource(resource)) {
            if (resource->first)
                resource->first->devirtualize.push_back(resource);
            if (resource->last)
                resource->last->destroy.push_back(resource);
        } else {
            auto incoming  = getInComingNodes(resource);
            auto outComing = getOutComingNodes(resource);
            LOGI("Resource {0} is not used", resource->getName());
        }
    }

    for (const auto& edge : edges) {
        edge.resource->resourceUsage = edge.resource->resourceUsage | edge.usage;
    }

    for (const auto& node : mResources) {
        if (needToCutResource(node))
            node->destroy();
    }
}
void RenderGraph::clearPass() {
    for (const auto& passNode : mPassNodes)
        passNode->setActive(false);
}

Device& RenderGraph::getDevice() const {
    return device;
}
std::vector<std::string> RenderGraph::getResourceNames(RenderResourceType type) const {
    std::vector<std::string> names{};
    for (const auto& resource : mResources) {
        if (any(resource->getType() & type))
            names.push_back(resource->getName());
    }
    return names;
}

std::vector<std::string> RenderGraph::getPasseNames(RenderPassType type) const {

    std::vector<std::string> names{};
    for (const auto& passNode : mPassNodes) {
        if (any(passNode->getType() & type))
            names.push_back(passNode->getName());
    }
    return names;
}
bool RenderGraph::needToCutResource(ResourceNode* resourceNode) const {
    return resourceNode->getRefCount() == 0 && cutUnUsedResources;
}

void RenderGraph::setDebugBarrierInfo(bool _debugBarrierInfo) {
    debugBarrierInfo = _debugBarrierInfo;
}

bool RenderGraph::getDebugBarrierInfo() const {
    return debugBarrierInfo;
}

void RenderGraph::execute(CommandBuffer& commandBuffer) {

    // DebugUtils::CmdInsertLabel(commandBuffer, "RenderGraph")

    //todo handle compile
    compile();

    auto first = mPassNodes.begin();
    while (first != mActivePassNodesEnd) {

        const auto pass = *first;

        first++;

        //
        for (const auto& resource : pass->devirtualize) {
            resource->devirtualize();
            //getBlackBoard().put(resource->getName(), resource->handle);
        }
        pass->resolveResourceUsages(*this, commandBuffer);
        pass->execute(*this, commandBuffer);

        ResourceBarrierInfo barrierInfo;

        for (const auto& resourceNode : pass->destroy) {
            auto firstPass = resourceNode->first;
            auto state = firstPass->getResourceUsage(resourceNode->handle);
            auto curState = resourceStateTracker.getResourceState(resourceNode->handle);
            unsigned short srcUsage;
            RenderPassType srcState;
            resourceNode->resloveUsage(barrierInfo,std::get<1>(curState),state,pass->getType(),firstPass->getType());
        }


        auto depdencyInfo = barrierInfo.GetVkDependencyInfo();
        vkCmdPipelineBarrier2(commandBuffer.getHandle(),&depdencyInfo);

        for (const auto& resource : pass->destroy) {
            resource->destroy();
        }
    }
}

Blackboard& RenderGraph::getBlackBoard() const {
    return *mBlackBoard;
}

RenderGraphHandle RenderGraph::importTexture(const std::string& name, SgImage* hwTexture, bool addRef) {
    auto texture = new RenderGraphTexture(name, hwTexture);
    DebugUtils::SetObjectName(device.getHandle(),reinterpret_cast<uint64_t>(texture->getHwTexture()->getVkImage().getHandle()),VK_OBJECT_TYPE_IMAGE,name);
    if (addRef) texture->addRef();
    return addTexture(texture);
}

// RenderGraphHandle RenderGraph::Builder::readTexture(RenderGraphHandle input,
//                                                     RenderGraphTexture::Usage usage)
// {
//     node->addTextureUsage(input, usage);
//     return input;
// }
//
// RenderGraphHandle RenderGraph::Builder::writeTexture(RenderGraphHandle output,
//                                                      RenderGraphTexture::Usage usage)
// {
//     node->addTextureUsage(output, usage);
//     return output;
// }

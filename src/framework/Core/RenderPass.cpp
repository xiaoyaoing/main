#include "RenderPass.h"
#include "Core/Device/Device.h"
#include "RenderTarget.h"
#include "Images/ImageUtil.h"

struct AttachmentReference {
    uint32_t     attachment;
    VulkanLayout layout;
};

VulkanLayout getVulkanLayout(VkImageLayout layout, VkFormat format) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VulkanLayout::UNDEFINED;
        case VK_IMAGE_LAYOUT_GENERAL:
            return isDepthOrStencilFormat(format) ? VulkanLayout::READ_WRITE : VulkanLayout::DEPTH_SAMPLER;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VulkanLayout::READ_ONLY;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VulkanLayout::TRANSFER_SRC;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VulkanLayout::TRANSFER_DST;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VulkanLayout::DEPTH_ATTACHMENT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VulkanLayout::DEPTH_READ_ONLY;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return VulkanLayout::PRESENT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VulkanLayout::COLOR_ATTACHMENT;
    }

    // 默认情况，可以根据需要返回特定的默认值或抛出异常
    return VulkanLayout::UNDEFINED;
}

void setAttachmentLayouts(std::vector<VkSubpassDescription>     subpassDescs,
                          std::vector<VkAttachmentDescription>& attachments) {
    for (const auto& subpass : subpassDescs) {
        for (int i = 0; i < subpass.colorAttachmentCount; i++) {
            auto& reference = subpass.pColorAttachments[i];
            if (attachments[reference.attachment].initialLayout == ImageUtil::getVkImageLayout(VulkanLayout::UNDEFINED)) {
                attachments[reference.attachment].initialLayout = reference.layout;
            }
        }

        for (int i = 0; i < subpass.inputAttachmentCount; i++) {
            auto& reference = subpass.pInputAttachments[i];
            if (attachments[reference.attachment].initialLayout == ImageUtil::getVkImageLayout(VulkanLayout::UNDEFINED)) {
                attachments[reference.attachment].initialLayout = reference.layout;
            }
        }

        if (subpass.pDepthStencilAttachment) {
            auto& reference = *subpass.pDepthStencilAttachment;
            if (attachments[reference.attachment].initialLayout == ImageUtil::getVkImageLayout(VulkanLayout::UNDEFINED)) {
                attachments[reference.attachment].initialLayout = reference.layout;
            }
        }

        if (subpass.pResolveAttachments) {
            for (int i = 0; i < subpass.colorAttachmentCount; i++) {
                auto& reference = subpass.pResolveAttachments[i];
                if (attachments[reference.attachment].initialLayout == ImageUtil::getVkImageLayout(VulkanLayout::UNDEFINED)) {
                    attachments[reference.attachment].initialLayout = reference.layout;
                }
            }
        }
    }

    for (const auto& subpass : subpassDescs) {
        for (int i = 0; i < subpass.colorAttachmentCount; i++) {
            auto& reference = subpass.pColorAttachments[i];
            if (attachments[reference.attachment].finalLayout == ImageUtil::getVkImageLayout(VulkanLayout::UNDEFINED)) {
                attachments[reference.attachment].finalLayout = reference.layout;
            }
        }

        for (int i = 0; i < subpass.inputAttachmentCount; i++) {
            auto& reference = subpass.pInputAttachments[i];
            if (attachments[reference.attachment].finalLayout == ImageUtil::getVkImageLayout(VulkanLayout::UNDEFINED)) {
                attachments[reference.attachment].finalLayout = reference.layout;
            }
        }

        if (subpass.pDepthStencilAttachment) {
            auto& reference = *subpass.pDepthStencilAttachment;
            if (attachments[reference.attachment].finalLayout == ImageUtil::getVkImageLayout(VulkanLayout::UNDEFINED)) {
                attachments[reference.attachment].finalLayout = reference.layout;
            }
        }

        if (subpass.pResolveAttachments) {
            for (int i = 0; i < subpass.colorAttachmentCount; i++) {
                auto& reference = subpass.pResolveAttachments[i];
                if (attachments[reference.attachment].finalLayout == ImageUtil::getVkImageLayout(VulkanLayout::UNDEFINED)) {
                    attachments[reference.attachment].finalLayout = reference.layout;
                }
            }
        }
    }

    {
        auto& subpass = subpassDescs.back();

        for (size_t k = 0U; k < subpass.colorAttachmentCount; ++k) {
            const auto& reference                         = subpass.pColorAttachments[k];
            attachments[reference.attachment].finalLayout = reference.layout;
        }

        for (size_t k = 0U; k < subpass.inputAttachmentCount; ++k) {
            const auto& reference                         = subpass.pInputAttachments[k];
            attachments[reference.attachment].finalLayout = reference.layout;

            if (isDepthOrStencilFormat(attachments[reference.attachment].format)) {
                subpass.pDepthStencilAttachment = nullptr;
            }
        }

        if (subpass.pDepthStencilAttachment) {
            const auto& reference                         = *subpass.pDepthStencilAttachment;
            attachments[reference.attachment].finalLayout = reference.layout;
        }

        if (subpass.pResolveAttachments) {
            for (size_t k = 0U; k < subpass.colorAttachmentCount; ++k) {
                const auto& reference                         = subpass.pResolveAttachments[k];
                attachments[reference.attachment].finalLayout = reference.layout;
            }
        }
    }
}

std::vector<VkSubpassDependency> getSubpassDependency(const size_t subpassCount) {
    std::vector<VkSubpassDependency> dependencies(subpassCount - 1);

    if (subpassCount > 1) {
        for (uint32_t i = 0; i < (dependencies.size()); ++i) {
            // Transition input attachments from color attachment to shader read
            dependencies[i].srcSubpass      = i;
            dependencies[i].dstSubpass      = i + 1;
            dependencies[i].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[i].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependencies[i].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[i].dstAccessMask   = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
            dependencies[i].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        }
    }

    return dependencies;
}

RenderPass::~RenderPass() {
    vkDestroyRenderPass(device.getHandle(), _pass, nullptr);
}

RenderPass::RenderPass(RenderPass&& other) : _pass(other._pass), device(other.device),
                                             colorOutputCount(other.colorOutputCount),
                                             subpassInputLayouts(other.subpassInputLayouts),
                                             attachmentFinalLayouts(other.attachmentFinalLayouts) {
    other._pass = VK_NULL_HANDLE;
}

RenderPass::RenderPass(Device& device, const std::vector<Attachment>& attachments, const std::vector<SubpassInfo>& subpasses)
    : device(device) {
    std::vector<VkAttachmentDescription> attachmentDescriptions;
    //对于一个渲染pass来说 attachment说明了这个渲染过程中需要用的image资源
    for (int i = 0; i < attachments.size(); i++) {
        VkAttachmentDescription attachmentDescription{};
        attachmentDescription.format        = attachments[i].format;
        attachmentDescription.samples       = attachments[i].samples;
        attachmentDescription.initialLayout = ImageUtil::getVkImageLayout(attachments[i].initial_layout);
        //todo fix this
        if (attachments[i].final_layout == VulkanLayout::UNDEFINED)
            attachmentDescription.finalLayout = isDepthOrStencilFormat(attachmentDescription.format) ? ImageUtil::getVkImageLayout(VulkanLayout::DEPTH_ATTACHMENT) : ImageUtil::getVkImageLayout(VulkanLayout::COLOR_ATTACHMENT);
        else
            attachmentDescription.finalLayout = ImageUtil::getVkImageLayout(attachments[i].final_layout);
        attachmentDescription.loadOp  = attachments[i].loadOp;
        attachmentDescription.storeOp = attachments[i].storeOp;
        attachmentDescription.loadOp  = attachments[i].loadOp;
        attachmentDescription.storeOp = attachments[i].storeOp;

        attachmentDescriptions.push_back(attachmentDescription);
    }

    auto subpass_count = std::max<size_t>(1, subpasses.size());

    subpassInputLayouts.resize(subpass_count);
    std::vector<std::vector<VkAttachmentReference>> inputAttachments{subpass_count};
    std::vector<std::vector<VkAttachmentReference>> colorAttachments{subpass_count};
    std::vector<std::vector<VkAttachmentReference>> depthStencilAttachments{subpass_count};
    std::vector<std::vector<VkAttachmentReference>> colorResolveAttachments{subpass_count};
    std::vector<std::vector<VkAttachmentReference>> depthResolveAttachments{subpass_count};
    std::vector<VkSubpassDescription>               subpassDescriptions;
    subpassDescriptions.reserve(subpass_count);
    if (subpasses.empty()) {
        VkSubpassDescription subpass_description{};
        subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        uint32_t default_depth_stencilAttachment{VK_ATTACHMENT_UNUSED};

        for (uint32_t k = 0U; k < (attachmentDescriptions.size()); ++k) {
            if (isDepthOrStencilFormat(attachments[k].format)) {
                if (default_depth_stencilAttachment == VK_ATTACHMENT_UNUSED) {
                    default_depth_stencilAttachment = k;
                }
                continue;
            }
            VkAttachmentReference reference{k, ImageUtil::getVkImageLayout(VulkanLayout::COLOR_ATTACHMENT)};
            colorAttachments[0].push_back(reference);
        }

        subpass_description.pColorAttachments    = colorAttachments[0].data();
        subpass_description.colorAttachmentCount = toUint32(colorAttachments[0].size());

        if (default_depth_stencilAttachment != VK_ATTACHMENT_UNUSED) {
            VkAttachmentReference reference{default_depth_stencilAttachment, ImageUtil::getVkImageLayout(VulkanLayout::DEPTH_ATTACHMENT)};

            depthStencilAttachments[0].push_back(reference);

            subpass_description.pDepthStencilAttachment = depthStencilAttachments[0].data();
        }

        subpassDescriptions.push_back(subpass_description);
    } else {
        for (int i = 0; i < subpasses.size(); i++) {
            auto& subPass = subpasses[i];
            for (auto& inputAttachment : subPass.inputAttachments) {
                auto defaultLayout = isDepthOrStencilFormat(attachmentDescriptions[inputAttachment].format) ? VulkanLayout::DEPTH_READ_ONLY : VulkanLayout::READ_ONLY;
                // : VK_IMAGE_LAYOUT_GENERAL;
                // auto initialLayout = ImageUtil::getVkImageLayout()
                auto initialLayout   = defaultLayout;
                auto initialLayVkout = ImageUtil::getVkImageLayout(initialLayout);
                // initialLayout = VK_IMAGE_LAYOUT_GENERAL; //todo fix this

                inputAttachments[i].emplace_back(VkAttachmentReference{inputAttachment, initialLayVkout});
                subpassInputLayouts[i].emplace_back(std::pair{inputAttachment, defaultLayout});
            }

            for (auto& outputAttachment : subPass.outputAttachments) {
                if (!isDepthOrStencilFormat(attachmentDescriptions[outputAttachment].format)) {
                    auto                  initialLayout = ImageUtil::getVkImageLayout(ImageUtil::chooseVulkanLayout(attachments[outputAttachment].initial_layout, VulkanLayout::COLOR_ATTACHMENT));
                    VkAttachmentReference ref{outputAttachment, initialLayout};
                    colorAttachments[i].push_back(ref);
                }
            }

            for (auto& colorResolveAttachment : subPass.colorResolveAttachments) {
                VkAttachmentReference ref{colorResolveAttachment, ImageUtil::getVkImageLayout(attachments[colorResolveAttachment].initial_layout)};
                colorResolveAttachments[i].push_back(ref);
            }
            // 如果有深度测试
            if (!subPass.disableDepthStencilAttachment) {
                auto it = std::find_if(attachments.begin(), attachments.end(), [](const Attachment attachment) {
                    return isDepthOrStencilFormat(attachment.format);
                });
                if (it != attachments.end()) {

                    uint32_t iDepthStencil = std::distance(attachments.begin(), it);

                    bool depthAsOutput = std::find(subPass.outputAttachments.begin(), subPass.outputAttachments.end(), iDepthStencil) != subPass.outputAttachments.end();
                    auto initialLayout = ImageUtil::getVkImageLayout(depthAsOutput ? VulkanLayout::DEPTH_ATTACHMENT : VulkanLayout::DEPTH_READ_ONLY);
                    depthStencilAttachments[i].push_back(VkAttachmentReference{iDepthStencil, initialLayout});
                }
            }

            VkSubpassDescription subpassDescription{};

            subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

            subpassDescription.inputAttachmentCount = inputAttachments[i].size();
            subpassDescription.pInputAttachments    = inputAttachments[i].empty() ? nullptr : inputAttachments[i].data();

            subpassDescription.colorAttachmentCount = colorAttachments[i].size();
            subpassDescription.pColorAttachments    = colorAttachments[i].empty() ? nullptr : colorAttachments[i].data();

            subpassDescription.pResolveAttachments = colorResolveAttachments[i].empty() ? nullptr : colorResolveAttachments[i].data();

            subpassDescription.pDepthStencilAttachment = nullptr;

            if (!depthStencilAttachments[i].empty()) {
                subpassDescription.pDepthStencilAttachment = depthStencilAttachments[i].data();
            }

            subpassDescriptions.push_back(subpassDescription);
        }
    }


    
    setAttachmentLayouts(subpassDescriptions, attachmentDescriptions);

    for (int i = 0; i < subpass_count; i++) {
        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.inputAttachmentCount = inputAttachments[i].size();
        subpassDescription.pInputAttachments    = inputAttachments[i].empty() ? nullptr : inputAttachments[i].data();
    }

    colorOutputCount.reserve(subpass_count);
    for (size_t i = 0; i < subpass_count; i++) {
        colorOutputCount.push_back(toUint32(colorAttachments[i].size()));
    }

    auto subpassDependencies = getSubpassDependency(subpass_count);

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachmentDescriptions.size();
    renderPassInfo.pAttachments    = attachmentDescriptions.data();
    renderPassInfo.subpassCount    = subpassDescriptions.size();
    renderPassInfo.pSubpasses      = subpassDescriptions.data();
    renderPassInfo.dependencyCount = subpassDependencies.size();
    renderPassInfo.pDependencies   = subpassDependencies.data();

    for (const auto& attachmentDesc : attachmentDescriptions) {
        attachmentFinalLayouts.push_back(getVulkanLayout(attachmentDesc.finalLayout, attachmentDesc.format));
    }

    VK_CHECK_RESULT(vkCreateRenderPass(device.getHandle(), &renderPassInfo, nullptr, &_pass))
}

const uint32_t RenderPass::getColorOutputCount(uint32_t subpass_index) const {
    return colorOutputCount[subpass_index];
}

const std::vector<VulkanLayout>& RenderPass::getAttachmentFinalLayouts() const
{
    return attachmentFinalLayouts;
}

#include <array>
#include <iostream>     // std::cout
#include <fstream>      // std::ifstream


#ifdef WIN64
#else
#include <unistd.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "vkapp.h"

#include "app.h"

 
template <class integral>
constexpr integral align_up(integral x, size_t a) noexcept
{
    return integral((x + (integral(a) - 1)) & ~integral(a - 1));
}

#define GLM_FORCE_RADIANS
#define GLM_SWIZZLE
#include <glm/glm.hpp>

VkApp::VkApp(App* _app) : app(_app)
{
    createInstance(app->doApiDump);
    assert (m_instance);
    createPhysicalDevice(); // i.e. the GPU
    chooseQueueIndex();
    createDevice();
    getCommandQueue();

    loadExtensions();

    getSurface();
    createCommandPool();
    
    createSwapchain();
    createDepthResource();
    createPostRenderPass();
    createPostFrameBuffers();

    createScBuffer();
    createPostDescriptor();
    createPostPipeline();

    #ifdef GUI
    initGUI();
    #endif
    
    myloadModel("models/living_room.obj", glm::mat4());

    createMatrixBuffer();
    createObjDescriptionBuffer();
    createScanlineRenderPass();
    createScDescriptorSet();
    createScPipeline();
    
    createRtBuffers();
    // createDenoiseBuffer();

    
    // createStuff();

    

    // //init ray tracing capabilities
    initRayTracing();
    createRtAccelerationStructure();
    createRtDescriptorSet();
    createRtPipeline();
    createRtShaderBindingTable();

    // createDenoiseDescriptorSet();
    // createDenoiseCompPipeline();

}

void VkApp::drawFrame()
{        
    prepareFrame();
    
    //VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    //beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    //vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    m_commandBuffer.begin(beginInfo);
    {   // Extra indent for recording commands into m_commandBuffer
        updateCameraBuffer();
        
        // Draw scene
         if (useRaytracer) {
             raytrace();
             //denoise(); 
         }
         else {
             rasterize(); 
         }
        
        postProcess(); //  tone mapper and output to swapchain image.
        
        // vkEndCommandBuffer(m_commandBuffer);
        m_commandBuffer.end();
    }   // Done recording;  Execute!
    
        submitFrame();  // Submit for display
}

VkAccessFlags accessFlagsForImageLayout(VkImageLayout layout)
{
    switch(layout)
        {
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return VK_ACCESS_HOST_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        default:
            return VkAccessFlags();
        }
}

VkPipelineStageFlags pipelineStageForLayout(VkImageLayout layout)
{
    switch(layout)
        {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;  // Allow queue other than graphic
            // return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;  // Allow queue other than graphic
            // return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return VK_PIPELINE_STAGE_HOST_BIT;
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        default:
            return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }
}

//-------------------------------------------------------------------------------------------------
// Post processing pass: tone mapper, UI
void VkApp::postProcess()
{

    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color = vk::ClearColorValue(std::array<float, 4>({ { 1.0f, 1.0f, 1.0f, 1.0f } }));
    clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

    vk::RenderPassBeginInfo renderPassBeginInfo(
        m_postRenderPass, m_framebuffers[m_swapchainIndex], 
        vk::Rect2D(vk::Offset2D(0, 0), VkExtent2D(windowSize)), clearValues);

    m_commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
    {   // extra indent for renderpass commands
        //VkViewport viewport{0.0f, 0.0f,
        //    static_cast<float>(windowSize.width), static_cast<float>(windowSize.height),
        //    0.0f, 1.0f};
        //vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
        //
        //VkRect2D scissor{{0, 0}, {windowSize.width, windowSize.height}};
        //vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);

        auto aspectRatio = static_cast<float>(windowSize.width)
            / static_cast<float>(windowSize.height);
        //vkCmdPushConstants(m_commandBuffer, m_postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        //                   sizeof(float), &aspectRatio);
        m_commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_postPipeline);
        //vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        //                       m_postPipelineLayout, 0, 0, nullptr, 0, nullptr);
        m_commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_postPipelineLayout, 0, 1, &m_postDesc.descSet, 0, nullptr);
        // Weird! This draws 3 vertices but with no vertices/triangles buffers bound in.
        // Hint: The vertex shader fabricates vertices from gl_VertexIndex
        m_commandBuffer.draw(3, 1, 0, 0);

#ifdef GUI
        ImGui::Render();  // Rendering UI
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_commandBuffer);
#endif
    }
    m_commandBuffer.endRenderPass();
}


VkCommandBuffer VkApp::createTempCmdBuffer()
{
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandBufferCount = 1;
    allocateInfo.commandPool        = m_cmdPool;
    allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(m_device, &allocateInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    return cmdBuffer;
}

vk::CommandBuffer VkApp::createTempCppCmdBuffer() {
    vk::CommandBuffer cmdBuffer = m_device.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo(m_cmdPool, vk::CommandBufferLevel::ePrimary, 1)).front();
    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuffer.begin(beginInfo);

    return cmdBuffer;
}

void VkApp::submitTempCmdBuffer(VkCommandBuffer cmdBuffer)
{
    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmdBuffer;
    vkQueueSubmit(m_queue, 1, &submitInfo, {});
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmdBuffer);
}

void VkApp::submitTemptCppCmdBuffer(vk::CommandBuffer cmdBuffer) {
    cmdBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBufferCount(1);
    submitInfo.setPCommandBuffers(&cmdBuffer);
    m_queue.submit(1, &submitInfo, {});
    m_queue.waitIdle();
    m_device.freeCommandBuffers(m_cmdPool, 1, &cmdBuffer);
}

void VkApp::prepareFrame()
{
    // Acquire the next image from the swap chain --> m_swapchainIndex
    //VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_readSemaphore,
    //                                        (VkFence)VK_NULL_HANDLE, &m_swapchainIndex);
    m_device.acquireNextImageKHR(m_swapchain, UINT64_MAX, m_readSemaphore, 
        (VkFence)VK_NULL_HANDLE, &m_swapchainIndex);

    // Check if window has been resized -- or other(??) swapchain specific event
    //if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    //    recreateSizedResources(VkExtent2D(windowSize)); }

    // Use a fence to wait until the command buffer has finished execution before using it again
    while (vk::Result::eTimeout == m_device.waitForFences(1, &m_waitFence, VK_TRUE, 1'000'000))
        {}
}

void VkApp::submitFrame()
{
    m_device.resetFences(1, &m_waitFence);

    // Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
    const vk::PipelineStageFlags waitStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    // The submit info structure specifies a command buffer queue submission batch
    vk::SubmitInfo submitInfo(1, &m_readSemaphore, 
        &waitStageMask,
        1, &m_commandBuffer, 
        1, &m_writtenSemaphore);
    m_queue.submit(1, &submitInfo, m_waitFence);

    vk::PresentInfoKHR presentInfo(1, &m_writtenSemaphore,
        1, &m_swapchain, &m_swapchainIndex);
    m_queue.presentKHR(presentInfo);
}


vk::ShaderModule VkApp::createShaderModule(std::string code)
{
    vk::ShaderModuleCreateInfo sm_createInfo(vk::ShaderModuleCreateFlags(),
        code.size(), (uint32_t*)code.data(), nullptr);

    return m_device.createShaderModule(sm_createInfo);
}

VkPipelineShaderStageCreateInfo VkApp::createShaderStageInfo(const std::string&    code,
                                                                   VkShaderStageFlagBits stage,
                                                                   const char* entryPoint)
{
    VkPipelineShaderStageCreateInfo shaderStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStage.stage  = stage;
    shaderStage.module = createShaderModule(code);
    shaderStage.pName  = entryPoint;
    return shaderStage;
}

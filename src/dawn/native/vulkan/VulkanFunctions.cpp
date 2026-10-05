// Copyright 2017 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/dawn/native/vulkan/VulkanFunctions.h"

#include <string>
#include <utility>

#include "src/dawn/common/DynamicLib.h"
#include "src/dawn/native/vulkan/VulkanInfo.h"

namespace dawn::native::vulkan {

#if DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5)
extern "C" {
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
}
#endif  // DAWN_PLATFORM_IS(SWITCH)

namespace {

#if DAWN_NO_SANITIZE_VK_FN

template <typename F>
struct AsVkNoSanitizeFn;

// SwiftShader does not export function pointer type information.
// So, when fuzzing with UBSAN, fuzzers break whenever calling
// a vk* function since it thinks the type of the function pointer
// does not match. Context: crbug.com/1296934.

// Workaround this problem by proxying through a std::function
// in UBSAN builds. The std::function delegates to a Call method
// which does the same cast of the function pointer type, however
// the Call method is tagged with
// `__attribute__((no_sanitize("function")))` to silence the error.
template <typename R, typename... Args>
struct AsVkNoSanitizeFn<R(VKAPI_PTR*)(Args...)> {
    auto operator()(void(VKAPI_PTR* addr)()) {
        return [addr](Args&&... args) -> R { return Call(addr, std::forward<Args>(args)...); };
    }

  private:
    __attribute__((no_sanitize("function"))) static R Call(void(VKAPI_PTR* addr)(),
                                                           Args&&... args) {
        return reinterpret_cast<R(VKAPI_PTR*)(Args...)>(addr)(std::forward<Args>(args)...);
    }
};
template <typename F>
auto AsVkFn(void(VKAPI_PTR* addr)()) {
    return AsVkNoSanitizeFn<F>{}(addr);
}

#else

template <typename F>
F AsVkFn(void(VKAPI_PTR* addr)()) {
    return reinterpret_cast<F>(addr);
}

#endif

}  // anonymous namespace

#define GET_GLOBAL_PROC(name)                                                        \
    do {                                                                             \
        name = AsVkFn<PFN_vk##name>(GetInstanceProcAddr(nullptr, "vk" #name));       \
        if (name == nullptr) {                                                       \
            return DAWN_INTERNAL_ERROR(std::string("Couldn't get proc vk") + #name); \
        }                                                                            \
    } while (0)

MaybeError VulkanFunctions::LoadGlobalProcs(const DynamicLib& vulkanLib) {
#if DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5)
    (void)vulkanLib;
    GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr"));
    if (GetInstanceProcAddr == nullptr) {
        GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(&vk_icdGetInstanceProcAddr);
    }
#else
    if (!vulkanLib.GetProc(&GetInstanceProcAddr, "vkGetInstanceProcAddr")) {
        return DAWN_INTERNAL_ERROR("Couldn't get vkGetInstanceProcAddr");
    }
#endif  // DAWN_PLATFORM_IS(SWITCH)

    GET_GLOBAL_PROC(CreateInstance);
    GET_GLOBAL_PROC(EnumerateInstanceExtensionProperties);
    GET_GLOBAL_PROC(EnumerateInstanceLayerProperties);

    // Is not available in Vulkan 1.0, so allow nullptr
    EnumerateInstanceVersion = AsVkFn<PFN_vkEnumerateInstanceVersion>(
        GetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));

    return {};
}

#define GET_INSTANCE_PROC_NO_ERROR_BASE(name, procName) \
    name = AsVkFn<PFN_vk##name>(GetInstanceProcAddr(instance, "vk" #procName))
#define GET_INSTANCE_PROC_BASE(name, procName)                                           \
    do {                                                                                 \
        GET_INSTANCE_PROC_NO_ERROR_BASE(name, procName);                                 \
        if (name == nullptr) {                                                           \
            return DAWN_INTERNAL_ERROR(std::string("Couldn't get proc vk") + #procName); \
        }                                                                                \
    } while (0)

#define GET_INSTANCE_PROC(name) GET_INSTANCE_PROC_BASE(name, name)
#define GET_INSTANCE_PROC_NO_ERROR(name) GET_INSTANCE_PROC_NO_ERROR_BASE(name, name)

MaybeError VulkanFunctions::LoadInstanceProcs(VkInstance instance,
                                              const VulkanGlobalInfo& globalInfo) {
    // Dawn requires at least Vulkan 1.1
    DAWN_ASSERT(globalInfo.apiVersion >= VK_API_VERSION_1_1);

    // Load this proc first so that we can destroy the instance even if some other
    // GET_INSTANCE_PROC fails
    GET_INSTANCE_PROC(DestroyInstance);

    GET_INSTANCE_PROC(CreateDevice);
    GET_INSTANCE_PROC(DestroyDevice);
    GET_INSTANCE_PROC(EnumerateDeviceExtensionProperties);
    GET_INSTANCE_PROC(EnumerateDeviceLayerProperties);
    GET_INSTANCE_PROC(EnumeratePhysicalDevices);
    GET_INSTANCE_PROC(GetDeviceProcAddr);
    GET_INSTANCE_PROC(GetPhysicalDeviceFeatures);
    GET_INSTANCE_PROC(GetPhysicalDeviceFormatProperties);
    GET_INSTANCE_PROC(GetPhysicalDeviceImageFormatProperties);
    GET_INSTANCE_PROC(GetPhysicalDeviceMemoryProperties);
    GET_INSTANCE_PROC(GetPhysicalDeviceProperties);
    GET_INSTANCE_PROC(GetPhysicalDeviceQueueFamilyProperties);
    GET_INSTANCE_PROC(GetPhysicalDeviceSparseImageFormatProperties);

    // Promoted in 1.1
    GET_INSTANCE_PROC(EnumeratePhysicalDeviceGroups);
    GET_INSTANCE_PROC(GetPhysicalDeviceExternalBufferProperties);
    GET_INSTANCE_PROC(GetPhysicalDeviceExternalFenceProperties);
    GET_INSTANCE_PROC(GetPhysicalDeviceExternalSemaphoreProperties);
    GET_INSTANCE_PROC(GetPhysicalDeviceFeatures2);
    GET_INSTANCE_PROC(GetPhysicalDeviceFormatProperties2);
    GET_INSTANCE_PROC(GetPhysicalDeviceImageFormatProperties2);
    GET_INSTANCE_PROC(GetPhysicalDeviceMemoryProperties2);
    GET_INSTANCE_PROC(GetPhysicalDeviceProperties2);
    GET_INSTANCE_PROC(GetPhysicalDeviceQueueFamilyProperties2);
    GET_INSTANCE_PROC(GetPhysicalDeviceSparseImageFormatProperties2);

    if (globalInfo.HasExt(InstanceExt::DebugUtils)) {
        GET_INSTANCE_PROC(CmdBeginDebugUtilsLabelEXT);
        GET_INSTANCE_PROC(CmdEndDebugUtilsLabelEXT);
        GET_INSTANCE_PROC(CmdInsertDebugUtilsLabelEXT);
        GET_INSTANCE_PROC(CreateDebugUtilsMessengerEXT);
        GET_INSTANCE_PROC(DestroyDebugUtilsMessengerEXT);
        GET_INSTANCE_PROC(QueueBeginDebugUtilsLabelEXT);
        GET_INSTANCE_PROC(QueueEndDebugUtilsLabelEXT);
        GET_INSTANCE_PROC(QueueInsertDebugUtilsLabelEXT);
        GET_INSTANCE_PROC(SetDebugUtilsObjectNameEXT);
        GET_INSTANCE_PROC(SetDebugUtilsObjectTagEXT);
        GET_INSTANCE_PROC(SubmitDebugUtilsMessageEXT);
    }

    if (globalInfo.HasExt(InstanceExt::Display)) {
        GET_INSTANCE_PROC(GetPhysicalDeviceDisplayPropertiesKHR);
        GET_INSTANCE_PROC(GetDisplayModePropertiesKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceDisplayPlanePropertiesKHR);
        GET_INSTANCE_PROC(GetDisplayPlaneSupportedDisplaysKHR);
        GET_INSTANCE_PROC(GetDisplayPlaneCapabilitiesKHR);
        GET_INSTANCE_PROC(CreateDisplayPlaneSurfaceKHR);
    }

    if (globalInfo.HasExt(InstanceExt::Surface)) {
        GET_INSTANCE_PROC(DestroySurfaceKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceSurfaceSupportKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceSurfaceCapabilitiesKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceSurfaceFormatsKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceSurfacePresentModesKHR);
    }

#if defined(VK_USE_PLATFORM_FUCHSIA)
    if (globalInfo.HasExt(InstanceExt::FuchsiaImagePipeSurface)) {
        GET_INSTANCE_PROC(CreateImagePipeSurfaceFUCHSIA);
    }
#endif  // defined(VK_USE_PLATFORM_FUCHSIA)

#if defined(DAWN_ENABLE_BACKEND_METAL)
    if (globalInfo.HasExt(InstanceExt::MetalSurface)) {
        GET_INSTANCE_PROC(CreateMetalSurfaceEXT);
    }
#endif  // defined(DAWN_ENABLE_BACKEND_METAL)

#if defined(DAWN_USE_WAYLAND)
    if (globalInfo.HasExt(InstanceExt::WaylandSurface)) {
        GET_INSTANCE_PROC(CreateWaylandSurfaceKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceWaylandPresentationSupportKHR);
    }
#endif  // defined(DAWN_USE_WAYLAND)

#if DAWN_PLATFORM_IS(WINDOWS)
    if (globalInfo.HasExt(InstanceExt::Win32Surface)) {
        GET_INSTANCE_PROC(CreateWin32SurfaceKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceWin32PresentationSupportKHR);
    }
#endif  // DAWN_PLATFORM_IS(WINDOWS)

#if DAWN_PLATFORM_IS(ANDROID)
    if (globalInfo.HasExt(InstanceExt::AndroidSurface)) {
        GET_INSTANCE_PROC(CreateAndroidSurfaceKHR);
    }
#endif  // DAWN_PLATFORM_IS(ANDROID)

#if defined(DAWN_USE_X11)
    if (globalInfo.HasExt(InstanceExt::XlibSurface)) {
        GET_INSTANCE_PROC(CreateXlibSurfaceKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceXlibPresentationSupportKHR);
    }
    if (globalInfo.HasExt(InstanceExt::XcbSurface)) {
        GET_INSTANCE_PROC(CreateXcbSurfaceKHR);
        GET_INSTANCE_PROC(GetPhysicalDeviceXcbPresentationSupportKHR);
    }
#endif  // defined(DAWN_USE_X11)

#if defined(VK_USE_PLATFORM_VI_NN)
    if (globalInfo.HasExt(InstanceExt::ViSurface)) {
        GET_INSTANCE_PROC(CreateViSurfaceNN);
    }
#endif  // defined(VK_USE_PLATFORM_VI_NN)

    // Some device extensions expose instance procs to query information from the vkPhysicalDevice.
    // Always try loading them as we don't know yet what extensions are available on the device.
    // The more proper solution to load them only if the extension is available would require
    // unnecessary roundtrips between info gathering and proc loading, and this approach is well
    // specified as well.

    // VK_KHR_cooperative_matrix
    GET_INSTANCE_PROC_NO_ERROR(GetPhysicalDeviceCooperativeMatrixPropertiesKHR);

    return {};
}

#define GET_DEVICE_PROC(name)                                                        \
    do {                                                                             \
        name = AsVkFn<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #name));          \
        if (name == nullptr) {                                                       \
            return DAWN_INTERNAL_ERROR(std::string("Couldn't get proc vk") + #name); \
        }                                                                            \
    } while (0)

#define GET_DEVICE_PROC_ALIAS(name, alias)                                                   \
    do {                                                                                      \
        name = AsVkFn<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #name));                   \
        if (name == nullptr) {                                                                \
            name = AsVkFn<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #alias));              \
        }                                                                                     \
        if (name == nullptr) {                                                                \
            return DAWN_INTERNAL_ERROR(std::string("Couldn't get proc vk") + #name + " or vk" + \
                                       #alias);                                               \
        }                                                                                     \
    } while (0)

#define GET_DEVICE_PROC_ALIAS_NO_ERROR(name, alias)                             \
    do {                                                                        \
        name = AsVkFn<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #name));     \
        if (name == nullptr) {                                                  \
            name = AsVkFn<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #alias)); \
        }                                                                       \
    } while (0)

MaybeError VulkanFunctions::LoadDeviceProcs(VkInstance instance,
                                            VkDevice device,
                                            const VulkanDeviceInfo& deviceInfo) {
    // Vulkan 1.0
    GET_DEVICE_PROC(AllocateCommandBuffers);
    GET_DEVICE_PROC(AllocateDescriptorSets);
    GET_DEVICE_PROC(AllocateMemory);
    GET_DEVICE_PROC(BeginCommandBuffer);
    GET_DEVICE_PROC(BindBufferMemory);
    GET_DEVICE_PROC(BindImageMemory);
    GET_DEVICE_PROC(CmdBeginQuery);
    GET_DEVICE_PROC(CmdBeginRenderPass);
    GET_DEVICE_PROC(CmdBindDescriptorSets);
    GET_DEVICE_PROC(CmdBindIndexBuffer);
    GET_DEVICE_PROC(CmdBindPipeline);
    GET_DEVICE_PROC(CmdBindVertexBuffers);
    GET_DEVICE_PROC(CmdBlitImage);
    GET_DEVICE_PROC(CmdClearAttachments);
    GET_DEVICE_PROC(CmdClearColorImage);
    GET_DEVICE_PROC(CmdClearDepthStencilImage);
    GET_DEVICE_PROC(CmdCopyBuffer);
    GET_DEVICE_PROC(CmdCopyBufferToImage);
    GET_DEVICE_PROC(CmdCopyImage);
    GET_DEVICE_PROC(CmdCopyImageToBuffer);
    GET_DEVICE_PROC(CmdCopyQueryPoolResults);
    GET_DEVICE_PROC(CmdDispatch);
    GET_DEVICE_PROC(CmdDispatchIndirect);
    GET_DEVICE_PROC(CmdDraw);
    GET_DEVICE_PROC(CmdDrawIndexed);
    GET_DEVICE_PROC(CmdDrawIndexedIndirect);
    GET_DEVICE_PROC(CmdDrawIndirect);
    GET_DEVICE_PROC(CmdEndQuery);
    GET_DEVICE_PROC(CmdEndRenderPass);
    GET_DEVICE_PROC(CmdExecuteCommands);
    GET_DEVICE_PROC(CmdFillBuffer);
    GET_DEVICE_PROC(CmdNextSubpass);
    GET_DEVICE_PROC(CmdPipelineBarrier);
    GET_DEVICE_PROC(CmdPushConstants);
    GET_DEVICE_PROC(CmdResetEvent);
    GET_DEVICE_PROC(CmdResetQueryPool);
    GET_DEVICE_PROC(CmdResolveImage);
    GET_DEVICE_PROC(CmdSetBlendConstants);
    GET_DEVICE_PROC(CmdSetDepthBias);
    GET_DEVICE_PROC(CmdSetDepthBounds);
    GET_DEVICE_PROC(CmdSetEvent);
    GET_DEVICE_PROC(CmdSetLineWidth);
    GET_DEVICE_PROC(CmdSetScissor);
    GET_DEVICE_PROC(CmdSetStencilCompareMask);
    GET_DEVICE_PROC(CmdSetStencilReference);
    GET_DEVICE_PROC(CmdSetStencilWriteMask);
    GET_DEVICE_PROC(CmdSetViewport);
    GET_DEVICE_PROC(CmdUpdateBuffer);
    GET_DEVICE_PROC(CmdWaitEvents);
    GET_DEVICE_PROC(CmdWriteTimestamp);
    GET_DEVICE_PROC(CreateBuffer);
    GET_DEVICE_PROC(CreateBufferView);
    GET_DEVICE_PROC(CreateCommandPool);
    GET_DEVICE_PROC(CreateComputePipelines);
    GET_DEVICE_PROC(CreateDescriptorPool);
    GET_DEVICE_PROC(CreateDescriptorSetLayout);
    GET_DEVICE_PROC(CreateEvent);
    GET_DEVICE_PROC(CreateFence);
    GET_DEVICE_PROC(CreateFramebuffer);
    GET_DEVICE_PROC(CreateGraphicsPipelines);
    GET_DEVICE_PROC(CreateImage);
    GET_DEVICE_PROC(CreateImageView);
    GET_DEVICE_PROC(CreatePipelineCache);
    GET_DEVICE_PROC(CreatePipelineLayout);
    GET_DEVICE_PROC(CreateQueryPool);
    GET_DEVICE_PROC(CreateRenderPass);
    GET_DEVICE_PROC(CreateSampler);
    GET_DEVICE_PROC(CreateSemaphore);
    GET_DEVICE_PROC(CreateShaderModule);
    GET_DEVICE_PROC(DestroyBuffer);
    GET_DEVICE_PROC(DestroyBufferView);
    GET_DEVICE_PROC(DestroyCommandPool);
    GET_DEVICE_PROC(DestroyDescriptorPool);
    GET_DEVICE_PROC(DestroyDescriptorSetLayout);
    GET_DEVICE_PROC(DestroyEvent);
    GET_DEVICE_PROC(DestroyFence);
    GET_DEVICE_PROC(DestroyFramebuffer);
    GET_DEVICE_PROC(DestroyImage);
    GET_DEVICE_PROC(DestroyImageView);
    GET_DEVICE_PROC(DestroyPipeline);
    GET_DEVICE_PROC(DestroyPipelineCache);
    GET_DEVICE_PROC(DestroyPipelineLayout);
    GET_DEVICE_PROC(DestroyQueryPool);
    GET_DEVICE_PROC(DestroyRenderPass);
    GET_DEVICE_PROC(DestroySampler);
    GET_DEVICE_PROC(DestroySemaphore);
    GET_DEVICE_PROC(DestroyShaderModule);
    GET_DEVICE_PROC(DeviceWaitIdle);
    GET_DEVICE_PROC(EndCommandBuffer);
    GET_DEVICE_PROC(FlushMappedMemoryRanges);
    GET_DEVICE_PROC(FreeCommandBuffers);
    GET_DEVICE_PROC(FreeDescriptorSets);
    GET_DEVICE_PROC(FreeMemory);
    GET_DEVICE_PROC(GetBufferMemoryRequirements);
    GET_DEVICE_PROC(GetDeviceMemoryCommitment);
    GET_DEVICE_PROC(GetDeviceQueue);
    GET_DEVICE_PROC(GetEventStatus);
    GET_DEVICE_PROC(GetFenceStatus);
    GET_DEVICE_PROC(GetImageMemoryRequirements);
    GET_DEVICE_PROC(GetImageSparseMemoryRequirements);
    GET_DEVICE_PROC(GetImageSubresourceLayout);
    GET_DEVICE_PROC(GetPipelineCacheData);
    GET_DEVICE_PROC(GetQueryPoolResults);
    GET_DEVICE_PROC(GetRenderAreaGranularity);
    GET_DEVICE_PROC(InvalidateMappedMemoryRanges);
    GET_DEVICE_PROC(MapMemory);
    GET_DEVICE_PROC(MergePipelineCaches);
    GET_DEVICE_PROC(QueueBindSparse);
    GET_DEVICE_PROC(QueueSubmit);
    GET_DEVICE_PROC(QueueWaitIdle);
    GET_DEVICE_PROC(ResetCommandBuffer);
    GET_DEVICE_PROC(ResetCommandPool);
    GET_DEVICE_PROC(ResetDescriptorPool);
    GET_DEVICE_PROC(ResetEvent);
    GET_DEVICE_PROC(ResetFences);
    GET_DEVICE_PROC(SetEvent);
    GET_DEVICE_PROC(UnmapMemory);
    GET_DEVICE_PROC(UpdateDescriptorSets);
    GET_DEVICE_PROC(WaitForFences);

    // Promoted in 1.1
    GET_DEVICE_PROC_ALIAS(BindBufferMemory2, BindBufferMemory2KHR);
    GET_DEVICE_PROC_ALIAS(BindImageMemory2, BindImageMemory2KHR);
    GET_DEVICE_PROC_ALIAS(CmdDispatchBase, CmdDispatchBaseKHR);
    GET_DEVICE_PROC_ALIAS(CmdSetDeviceMask, CmdSetDeviceMaskKHR);
#if DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5)
    GET_DEVICE_PROC_ALIAS_NO_ERROR(CreateDescriptorUpdateTemplate,
                                   CreateDescriptorUpdateTemplateKHR);
#else
    GET_DEVICE_PROC_ALIAS(CreateDescriptorUpdateTemplate, CreateDescriptorUpdateTemplateKHR);
#endif
#if DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5)
    GET_DEVICE_PROC_ALIAS_NO_ERROR(CreateSamplerYcbcrConversion,
                                   CreateSamplerYcbcrConversionKHR);
#else
    GET_DEVICE_PROC_ALIAS(CreateSamplerYcbcrConversion, CreateSamplerYcbcrConversionKHR);
#endif
#if DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5)
    GET_DEVICE_PROC_ALIAS_NO_ERROR(DestroyDescriptorUpdateTemplate,
                                   DestroyDescriptorUpdateTemplateKHR);
#else
    GET_DEVICE_PROC_ALIAS(DestroyDescriptorUpdateTemplate, DestroyDescriptorUpdateTemplateKHR);
#endif
#if DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5)
    GET_DEVICE_PROC_ALIAS_NO_ERROR(DestroySamplerYcbcrConversion,
                                   DestroySamplerYcbcrConversionKHR);
#else
    GET_DEVICE_PROC_ALIAS(DestroySamplerYcbcrConversion, DestroySamplerYcbcrConversionKHR);
#endif
    GET_DEVICE_PROC_ALIAS(GetBufferMemoryRequirements2, GetBufferMemoryRequirements2KHR);
    GET_DEVICE_PROC_ALIAS(GetDescriptorSetLayoutSupport, GetDescriptorSetLayoutSupportKHR);
    GET_DEVICE_PROC_ALIAS(GetDeviceGroupPeerMemoryFeatures, GetDeviceGroupPeerMemoryFeaturesKHR);
    GET_DEVICE_PROC(GetDeviceQueue2);
    GET_DEVICE_PROC_ALIAS(GetImageMemoryRequirements2, GetImageMemoryRequirements2KHR);
    GET_DEVICE_PROC_ALIAS(GetImageSparseMemoryRequirements2, GetImageSparseMemoryRequirements2KHR);
    GET_DEVICE_PROC_ALIAS(TrimCommandPool, TrimCommandPoolKHR);
#if DAWN_PLATFORM_IS(SWITCH) || DAWN_PLATFORM_IS(PS5)
    GET_DEVICE_PROC_ALIAS_NO_ERROR(UpdateDescriptorSetWithTemplate,
                                   UpdateDescriptorSetWithTemplateKHR);
#else
    GET_DEVICE_PROC_ALIAS(UpdateDescriptorSetWithTemplate, UpdateDescriptorSetWithTemplateKHR);
#endif

    // Promoted in 1.2
    if (deviceInfo.HasExt(DeviceExt::DrawIndirectCount)) {
        GET_DEVICE_PROC(CmdDrawIndirectCountKHR);
        GET_DEVICE_PROC(CmdDrawIndexedIndirectCountKHR);
    }

    if (deviceInfo.HasExt(DeviceExt::CreateRenderPass2)) {
        GET_DEVICE_PROC(CreateRenderPass2KHR);
    }

    // Promoted in 1.3
    if (deviceInfo.HasExt(DeviceExt::DynamicRendering)) {
        GET_DEVICE_PROC(CmdBeginRenderingKHR);
        GET_DEVICE_PROC(CmdEndRenderingKHR);
    }

    if (deviceInfo.HasExt(DeviceExt::ExtendedDynamicState)) {
        GET_DEVICE_PROC(CmdSetCullModeEXT);
        GET_DEVICE_PROC(CmdSetDepthCompareOpEXT);
        GET_DEVICE_PROC(CmdSetDepthTestEnableEXT);
        GET_DEVICE_PROC(CmdSetDepthWriteEnableEXT);
        GET_DEVICE_PROC(CmdSetFrontFaceEXT);
        GET_DEVICE_PROC(CmdSetPrimitiveTopologyEXT);
        GET_DEVICE_PROC(CmdSetStencilOpEXT);
        GET_DEVICE_PROC(CmdSetStencilTestEnableEXT);
    }

    // Not promoted to core in any version
    if (deviceInfo.HasExt(DeviceExt::ExternalMemoryFD)) {
        GET_DEVICE_PROC(GetMemoryFdKHR);
        GET_DEVICE_PROC(GetMemoryFdPropertiesKHR);
    }

    if (deviceInfo.HasExt(DeviceExt::ExternalMemoryHost)) {
        GET_DEVICE_PROC(GetMemoryHostPointerPropertiesEXT);
    }

    if (deviceInfo.HasExt(DeviceExt::ExternalSemaphoreFD)) {
        GET_DEVICE_PROC(ImportSemaphoreFdKHR);
        GET_DEVICE_PROC(GetSemaphoreFdKHR);
    }

    if (deviceInfo.HasExt(DeviceExt::Swapchain)) {
        GET_DEVICE_PROC(CreateSwapchainKHR);
        GET_DEVICE_PROC(DestroySwapchainKHR);
        GET_DEVICE_PROC(GetSwapchainImagesKHR);
        GET_DEVICE_PROC(AcquireNextImageKHR);
        GET_DEVICE_PROC(QueuePresentKHR);
    }

#if defined(VK_USE_PLATFORM_FUCHSIA)
    if (deviceInfo.HasExt(DeviceExt::ExternalMemoryZirconHandle)) {
        GET_DEVICE_PROC(GetMemoryZirconHandleFUCHSIA);
        GET_DEVICE_PROC(GetMemoryZirconHandlePropertiesFUCHSIA);
    }

    if (deviceInfo.HasExt(DeviceExt::ExternalSemaphoreZirconHandle)) {
        GET_DEVICE_PROC(ImportSemaphoreZirconHandleFUCHSIA);
        GET_DEVICE_PROC(GetSemaphoreZirconHandleFUCHSIA);
    }
#endif

#if DAWN_PLATFORM_IS(ANDROID)
    if (deviceInfo.HasExt(DeviceExt::ExternalMemoryAndroidHardwareBuffer)) {
        GET_DEVICE_PROC(GetAndroidHardwareBufferPropertiesANDROID);
        GET_DEVICE_PROC(GetMemoryAndroidHardwareBufferANDROID);
    }
#endif  // DAWN_PLATFORM_IS(ANDROID)

    return {};
}

}  // namespace dawn::native::vulkan

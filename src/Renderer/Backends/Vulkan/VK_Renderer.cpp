#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include "Core/Application.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Nova::Core::Renderer::Backends::Vulkan {

	static void CheckVkResult(VkResult err) {
		if (err == VK_SUCCESS) return;
		NV_LOG_ERROR((std::string("Vulkan error : ") + std::to_string((int)err)).c_str());
	}

	static bool FindQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t& outGraphics, uint32_t& outPresent) {
		outGraphics = UINT32_MAX;
		outPresent = UINT32_MAX;

		uint32_t qCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
		if (qCount == 0) return false;

		std::vector<VkQueueFamilyProperties> qProps(qCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, qProps.data());

		for (uint32_t i = 0; i < qCount; i++) {
			if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && outGraphics == UINT32_MAX) {
				outGraphics = i;
			}

			VkBool32 presentSupport = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
			if (presentSupport && outPresent == UINT32_MAX) {
				outPresent = i;
			}
		}

		return outGraphics != UINT32_MAX && outPresent != UINT32_MAX;
	}

	bool VK_Renderer::Create() {
		NV_LOG_INFO("Creating Vulkan renderer...");

		if (!CreateInstance()) return false;
		if (!CreateSurface()) return false;
		if (!PickPhysicalDevice()) return false;
		if (!CreateDevice()) return false;

		NV_LOG_INFO("Vulkan renderer created successfully.");
		return true;
	}

	void VK_Renderer::Destroy() {
		if (m_Device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(m_Device);
			vkDestroyDevice(m_Device, nullptr);
			m_Device = VK_NULL_HANDLE;
		}

		if (m_Surface != VK_NULL_HANDLE) {
			SDL_Vulkan_DestroySurface(m_Instance, m_Surface, nullptr);
			m_Surface = VK_NULL_HANDLE;
		}

		if (m_Instance != VK_NULL_HANDLE) {
			vkDestroyInstance(m_Instance, nullptr);
			m_Instance = VK_NULL_HANDLE;
		}

		NV_LOG_INFO("Vulkan renderer destroyed.");
	}

	bool VK_Renderer::Resize(int w, int h) { return true; }

	void VK_Renderer::Update(float dt) {}

	void VK_Renderer::BeginFrame() {}

	void VK_Renderer::Render() {}

	void VK_Renderer::EndFrame() {}

	bool VK_Renderer::CreateInstance() {
		SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
		if (!window) {
			NV_LOG_INFO("CreateInstance failed: SDL window is null.");
			return false;
		}

		Uint32 extCount = 0;
		const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
		if (!sdlExts || extCount == 0) {
			NV_LOG_ERROR((std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError()).c_str());
			return false;
		}

		std::vector<const char*> extensions(sdlExts, sdlExts + extCount);

		NV_LOG_INFO((std::string("SDL requires ") + std::to_string(extCount) + " instance extensions:").c_str());
		for (const char* e : extensions) {
			NV_LOG_INFO((std::string("  - ") + e).c_str());
		}

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Nova";
		appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.pEngineName = "Nova Core";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		VkInstanceCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		ci.pApplicationInfo = &appInfo;
		ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		ci.ppEnabledExtensionNames = extensions.data();
		ci.enabledLayerCount = 0;
		ci.ppEnabledLayerNames = nullptr;

		VkResult res = vkCreateInstance(&ci, nullptr, &m_Instance);
		CheckVkResult(res);

		NV_LOG_INFO("Vulkan instance created.");
		return true;
	}

	bool VK_Renderer::CreateSurface() {
		SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
		if (!window) {
			NV_LOG_INFO("CreateSurface failed: SDL window is null.");
			return false;
		}

		if (!SDL_Vulkan_CreateSurface(window, m_Instance, nullptr, &m_Surface)) {
			NV_LOG_ERROR((std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError()).c_str());
			return false;
		}

		NV_LOG_INFO("Vulkan surface created.");
		return true;
	}

	bool VK_Renderer::PickPhysicalDevice() {
		uint32_t deviceCount = 0;
		VkResult res = vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
		CheckVkResult(res);

		std::vector<VkPhysicalDevice> devices(deviceCount);
		res = vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());
		CheckVkResult(res);

		for (VkPhysicalDevice dev : devices) {
			uint32_t g = UINT32_MAX, p = UINT32_MAX;
			if (!FindQueueFamilies(dev, m_Surface, g, p)) {
				continue;
			}

			if (!HasDeviceExtension(dev, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
				continue;
			}

			m_PhysicalDevice = dev;

			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);

			NV_LOG_INFO((std::string("Selected GPU: ") + props.deviceName).c_str());
			LogDeviceExtensions(m_PhysicalDevice);
			return true;
		}

		NV_LOG_ERROR("No suitable Vulkan physical devices fount (graphics + present + swapchain).");
		return false;
	}

	bool VK_Renderer::CreateDevice() {
		uint32_t graphicsIndex = UINT32_MAX;
		uint32_t presentIndex = UINT32_MAX;
		if (!FindQueueFamilies(m_PhysicalDevice, m_Surface, graphicsIndex, presentIndex)) {
			NV_LOG_ERROR("CreateDevice failed: could not find required queue families");
			return false;
		}

		const float priority = 1.0f;

		std::vector<VkDeviceQueueCreateInfo> qcis;
		qcis.reserve(2);

		VkDeviceQueueCreateInfo qci{};
		qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		qci.queueFamilyIndex = graphicsIndex;
		qci.queueCount = 1;
		qci.pQueuePriorities = &priority;
		qcis.push_back(qci);

		if (presentIndex != graphicsIndex) {
			VkDeviceQueueCreateInfo pqci{};
			pqci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			pqci.queueFamilyIndex = presentIndex;
			pqci.queueCount = 1;
			pqci.pQueuePriorities = &priority;
			qcis.push_back(pqci);
		}

		std::vector<const char*> enabledDeviceExts;
		enabledDeviceExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

		VkPhysicalDeviceFeatures features{};

		VkDeviceCreateInfo dci{};
		dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		dci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
		dci.pQueueCreateInfos = qcis.data();
		dci.pEnabledFeatures = &features;
		dci.enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExts.size());
		dci.ppEnabledExtensionNames = enabledDeviceExts.data();

		VkResult res = vkCreateDevice(m_PhysicalDevice, &dci, nullptr, &m_Device);
		CheckVkResult(res);

		NV_LOG_INFO("Vulkan logical device created.");
		return true;
	}

} // namespace Nova::Core::Renderer::Backends::Vulkan
/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "EditorBackendBindings.h"

#include <Logger.h>
#include <SDL.h>
#include <imgui/imgui.h>

#ifdef TK_VULKAN
  #include <RenderSystem.h>
  #include <ToolKit.h>
  #include <Vulkan/VulkanBackend.h>
  #include <Vulkan/VulkanContext.h>
  #include <Vulkan/VulkanSwapchain.h>
  #include <imgui/backends/imgui_impl_vulkan.h>
  #include <vulkan/vulkan.h>

  #include <SDL_vulkan.h>
#else
  #include <imgui/backends/imgui_impl_opengl3.h>
#endif

#include <imgui/backends/imgui_impl_sdl2.h>

namespace ToolKit
{
  namespace Editor
  {
    namespace EditorBackendBindings
    {

      uint32_t GetSDLWindowFlags()
      {
#ifdef TK_VULKAN
        return SDL_WINDOW_VULKAN;
#else
        return SDL_WINDOW_OPENGL;
#endif
      }

      void PrepareWindowAttributes()
      {
#ifndef TK_VULKAN
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

  #ifdef TK_DEBUG
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
  #endif

        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
#endif
      }

      void* CreateGraphicsContext(SDL_Window* window)
      {
#ifdef TK_VULKAN
        (void) window;
        return nullptr;
#else
        SDL_GLContext ctx = SDL_GL_CreateContext(window);
        if (ctx != nullptr)
        {
          SDL_GL_MakeCurrent(window, ctx);
        }
        return (void*) ctx;
#endif
      }

      void DestroyGraphicsContext(void* context)
      {
#ifdef TK_VULKAN
        (void) context;
#else
        if (context != nullptr)
        {
          SDL_GL_DeleteContext((SDL_GLContext) context);
        }
#endif
      }

      void FillBackendInitParams(IGraphicsBackend::BackendInitParams& params, SDL_Window* window)
      {
#ifdef TK_VULKAN
        params.windowHandle   = window;
        params.getProcAddress = nullptr;

        uint32_t extCount     = 0;
        if (SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr) != SDL_TRUE)
        {
          TK_ERR("SDL_Vulkan_GetInstanceExtensions (count) failed: %s", SDL_GetError());
          return;
        }
        params.vkInstanceExtensions.resize(extCount);
        if (SDL_Vulkan_GetInstanceExtensions(window, &extCount, params.vkInstanceExtensions.data()) != SDL_TRUE)
        {
          TK_ERR("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
          params.vkInstanceExtensions.clear();
          return;
        }

        params.vkCreateSurface = [window](void* instance) -> uint64
        {
          VkSurfaceKHR surface = VK_NULL_HANDLE;
          if (SDL_Vulkan_CreateSurface(window, (VkInstance) instance, &surface) != SDL_TRUE)
          {
            TK_ERR("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
            return 0;
          }
          return (uint64) surface;
        };
#else
        (void) window;
        params.getProcAddress = (void*) SDL_GL_GetProcAddress;
        params.windowHandle   = nullptr;
#endif
      }

      bool IsBackbufferSrgb()
      {
#ifdef TK_VULKAN
        // Swapchain will be created with an sRGB format in Stage 1c — report true up-front.
        return true;
#else
        int flag = 0;
        SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &flag);
        return flag == 1;
#endif
      }

      void SetSwapInterval(int interval)
      {
#ifdef TK_VULKAN
        (void) interval;
        // Vulkan present mode will be picked when the swapchain is created (Stage 1c).
#else
        SDL_GL_SetSwapInterval(interval);
#endif
      }

      void PresentBackbuffer(SDL_Window* window)
      {
#ifdef TK_VULKAN
        (void) window;
        if (auto* rsys = GetRenderSystem())
        {
          // Skip RenderSystem::Present (it would recurse through the present callback we installed).
          // Go straight to the backend so the submit + vkQueuePresentKHR runs exactly once per frame.
          if (auto* backend = rsys->GetBackend())
          {
            backend->Present();
          }
        }
#else
        SDL_GL_SwapWindow(window);
#endif
      }

      void InitImGui(SDL_Window* window, void* context)
      {
#ifdef TK_VULKAN
        (void) context;
        ImGui_ImplSDL2_InitForVulkan(window);

        auto* backend = static_cast<VulkanBackend*>(GetRenderSystem()->GetBackend());
        if (backend == nullptr || backend->GetContext() == nullptr || backend->GetSwapchain() == nullptr)
        {
          TK_ERR("InitImGui: VulkanBackend/context/swapchain not ready");
          return;
        }
        VulkanContext* ctx     = backend->GetContext();
        VulkanSwapchain* swap  = backend->GetSwapchain();

        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion                      = VK_API_VERSION_1_3;
        info.Instance                        = ctx->GetInstance();
        info.PhysicalDevice                  = ctx->GetPhysicalDevice();
        info.Device                          = ctx->GetDevice();
        info.QueueFamily                     = ctx->GetGraphicsQueueFamily();
        info.Queue                           = ctx->GetGraphicsQueue();
        info.DescriptorPool                  = ctx->GetSharedDescriptorPool();
        info.MinImageCount                   = swap->GetMinImageCount();
        info.ImageCount                      = swap->GetImageCount();
        info.PipelineInfoMain.RenderPass     = swap->GetRenderPass();
        info.PipelineInfoMain.Subpass        = 0;
        info.PipelineInfoMain.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
        info.CheckVkResultFn                 = [](VkResult r)
        {
          if (r != VK_SUCCESS)
          {
            TK_ERR("[ImGui-VK] VkResult %d", (int) r);
          }
        };

        if (!ImGui_ImplVulkan_Init(&info))
        {
          TK_ERR("ImGui_ImplVulkan_Init failed");
        }
#else
        ImGui_ImplSDL2_InitForOpenGL(window, context);
        ImGui_ImplOpenGL3_Init("#version 300 es");
#endif
      }

      void ShutdownImGui()
      {
#ifdef TK_VULKAN
        if (auto* rsys = GetRenderSystem())
        {
          if (auto* backend = static_cast<VulkanBackend*>(rsys->GetBackend()))
          {
            if (auto* ctx = backend->GetContext())
            {
              // ImGui holds references to descriptor sets / device objects — flush first.
              vkDeviceWaitIdle(ctx->GetDevice());
            }
          }
        }
        ImGui_ImplVulkan_Shutdown();
#else
        ImGui_ImplOpenGL3_Shutdown();
#endif
      }

      void ImGuiNewFrame()
      {
#ifdef TK_VULKAN
        ImGui_ImplVulkan_NewFrame();
#else
        ImGui_ImplOpenGL3_NewFrame();
#endif
      }

      void ImGuiRenderDrawData()
      {
#ifdef TK_VULKAN
        auto* backend = static_cast<VulkanBackend*>(GetRenderSystem()->GetBackend());
        if (backend == nullptr)
        {
          return;
        }
        VkCommandBuffer cb = backend->GetCurrentCommandBuffer();
        if (cb != VK_NULL_HANDLE)
        {
          ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
        }
#else
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
      }

      void MakeContextCurrent(SDL_Window* window, void* context)
      {
#ifdef TK_VULKAN
        (void) window;
        (void) context;
#else
        SDL_GL_MakeCurrent(window, (SDL_GLContext) context);
#endif
      }

    } // namespace EditorBackendBindings
  } // namespace Editor
} // namespace ToolKit

/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../IGraphicsBackend.h"
#include "../Shader.h"
#include "VulkanBindings.h"

#include <vulkan/vulkan.h>

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

typedef struct VkCommandBuffer_T* VkCommandBuffer;

namespace ToolKit
{

  class VulkanContext;
  class VulkanSwapchain;

  class TK_API VulkanBackend : public IGraphicsBackend
  {
   public:
    VulkanBackend();
    ~VulkanBackend() override;

    VulkanContext* GetContext() { return m_context.get(); }

    VulkanSwapchain* GetSwapchain() { return m_swapchain.get(); }

    class VulkanPipelineCache* GetPipelineCache() { return m_pipelineCache.get(); }

    /** Command buffer being recorded this frame, or VK_NULL_HANDLE between frames. */
    VkCommandBuffer GetCurrentCommandBuffer() const;

    void InitBackend(const BackendInitParams& params) override;

    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;

    void StartPass(const PassDesc& desc) override;
    void FinishPass() override;

    void SetViewport(uint x, uint y, uint w, uint h) override;
    void SetScissor(uint x, uint y, uint w, uint h) override;

    void ClearBuffer(GraphicBitFields fields, const Vec4& color) override;
    void ClearColorBuffer(const Vec4& color) override;

    void BindPipeline(const GpuProgramPtr& program, const RenderState* state) override;

    void SubmitPerDrawData(const void* data, size_t size) override;
    void BindTexture(ubyte slot, TexturePtr tex) override;
    void BindUniformBuffer(const String& name, UniformBuffer* ub) override;

    void Draw(const DrawDesc& desc) override;

    void ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments) override;
    void CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields) override;
    void BlitToScreen(FramebufferPtr src) override;

    void StartTimerQuery() override;
    void EndTimerQuery() override;
    void GetElapsedTime(float& cpu, float& gpu) override;

    void CreateTexture(Texture* tex) override;
    void DestroyTexture(Texture* tex) override;
    void ApplyTextureSettings(Texture* tex) override;
    void SetTextureSwizzleAlpha(Texture* tex, bool swizzleToOne, bool setLastBindBack = false) override;
    void GenerateMipmaps(Texture* tex) override;
    void UpdateTextureRegion(Texture* tex, const void* data) override;
    void SetTextureMaxMipLevel(Texture* tex, int maxLevel) override;
    void AllocateCubemapMipStorage(Texture* tex) override;
    void CopyCubemapFaceFromFramebuffer(Texture* cubemap,
                                        int face,
                                        int mip,
                                        int width,
                                        int height,
                                        Framebuffer* readFb,
                                        Framebuffer* writeFb) override;

    void CreateMesh(Mesh* mesh) override;
    void DestroyMesh(Mesh* mesh) override;

    void CreateUniformBuffer(UniformBuffer* ub, uint64 size) override;
    void DestroyUniformBuffer(UniformBuffer* ub) override;
    void UpdateUniformBuffer(UniformBuffer* ub, const void* data, uint64 size) override;

    GpuResourceDataPtr CreateShader(Shader* shader, const String& source) override;
    void DestroyShader(GpuResourceData* shaderData) override;

    void CreateGpuProgram(GpuProgram* program, const struct ShaderResourceBinding* bindings, int bindingCount) override;
    void DestroyGpuProgram(GpuProgram* program) override;
    int GetUniformLocation(GpuProgram* program, const char* name) override;

    void CreateFramebuffer(Framebuffer* fb) override;
    void DestroyFramebuffer(Framebuffer* fb) override;
    void AttachColorTarget(Framebuffer* fb, RenderTargetPtr rt, int attachment, int mip, int layer, int face) override;
    void DetachColorTarget(Framebuffer* fb, int attachment) override;
    void AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt) override;
    void DetachDepthTarget(Framebuffer* fb) override;

    void SetUniform4f(int location, const Vec4& value) override;
    String GetBackendRendererString() override;
    int GetMaxArrayTextureLayers() override;
    void SetSrgbAutoEncoding(bool enable) override;
    void Finish() override;
    void SetDefaultClearColor(const Vec4& color) override;
    bool ValidateBackbufferSrgbEncoding() override;
    void EnableScissorTest(bool enable) override;
    void ReadPixels(int x, int y, int w, int h, GraphicTypes format, GraphicTypes type, void* data) override;
    void UpdateTextureSubRegion(Texture* tex, int x, int y, int w, int h, const void* data) override;
    void PushDebugGroup(StringView name) override;
    void PopDebugGroup() override;
    bool SupportsFloatTextureLinearFilter() override;
    bool IsDepthClampSupported() override;
    void* GetNativeTextureHandle(Texture* tex) override;
    void SetDebugLabel(Texture* tex) override;
    void SetDebugLabel(Framebuffer* fb) override;

    /** Defers a destruction lambda until the current frame's deleter bucket drains (i.e., until
        the fence covering the in-flight cb has retired). Capture by value any device/handle/
        shared_ptr the lambda needs. */
    void DeferDelete(std::function<void()> fn);

   private:
    std::unique_ptr<VulkanContext> m_context;
    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::unique_ptr<class VulkanPipelineCache> m_pipelineCache;
    bool m_frameStarted                      = false;
    bool m_needsRecreate                     = false;
    Vec4 m_clearColor                        = Vec4(0.4f, 0.0f, 0.4f, 1.0f);

    /** Active offscreen framebuffer between StartPass and FinishPass; null otherwise. */
    struct VulkanFramebuffer* m_activePassFb = nullptr;

    /** Last PassDesc supplied to StartPass; reused by Draw's lazy RP open. */
    PassDesc m_pendingPassDesc {};

    /** True while a render pass instance is open (gates resolve/blit which must run outside RP). */
    bool m_rpActive                         = false;

    /** True between BindPipeline success and the next FinishPass. */
    bool m_pipelineBound                    = false;

    /** Cached by BindPipeline; the pipeline is built lazily in Draw because the desc needs the
        vertex layout that only arrives with DrawDesc. */
    struct VulkanGpuProgram* m_boundProgram = nullptr;
    RenderState m_boundState {};

    /** UBO slot count for fixed-array indexing (covers GL slots 3..10 plus headroom). */
    static constexpr int kMaxUboSlots = 16;

    struct ShadowState
    {
      /** Per binding slot; null = unbound (dummy fallback at flush time). */
      std::array<TexturePtr, VulkanBindings::kTextureBindingCount> boundTextures {};

      /** UBO bindings indexed by GL slot. */
      std::array<UniformBuffer*, kMaxUboSlots> boundUniforms {};

      /** True after SubmitPerDrawData has run this draw cycle. */
      bool perDrawSubmitted = false;

      /** Payload size of the latest SubmitPerDrawData — drives the dynamic UBO range field. */
      uint64_t perDrawSize  = 0;

      /** Any binding changed since the last flush. */
      bool dirty            = false;

      void Reset()
      {
        for (auto& t : boundTextures)
        {
          t.reset();
        }
        for (UniformBuffer*& u : boundUniforms)
        {
          u = nullptr;
        }
        perDrawSubmitted = false;
        perDrawSize      = 0;
        dirty            = false;
      }
    };

    ShadowState m_shadow;

    /** Per-frame descriptor set cache. Key = state hash, value = the set allocated for that
        state. Cleared in BeginFrame alongside the pool reset. */
    struct DescriptorCacheEntry
    {
      uint64_t hash       = 0;
      VkDescriptorSet set = VK_NULL_HANDLE;
    };

    std::array<std::vector<DescriptorCacheEntry>, 2> m_descriptorCache {};

    /** MRU shortcut for FlushDescriptorState — material-sorted scenes hit it repeatedly. */
    VkDescriptorSet m_lastFlushedSet              = VK_NULL_HANDLE;
    struct VulkanGpuProgram* m_lastFlushedProgram = nullptr;

    // Timer query. Pool holds (start, end) timestamps; one cycle in flight at a time. Result is
    // polled non-blocking next BeginFrame, so multiple frames may pass between cycles.
    VkQueryPool m_timestampPool                   = VK_NULL_HANDLE;
    bool m_timerSupported                         = false;
    bool m_timerQueryActive                       = false;
    bool m_timerQueryWaiting                      = false;
    float m_timestampPeriodNs                     = 1.0f; //!< vkPhysicalDeviceLimits::timestampPeriod, cached.
    float m_cpuStartMs                            = 0.0f;
    float m_cpuTimeMs                             = 1.0f; //!< Non-zero default so Stats' 1000/x FPS doesn't /0.
    float m_gpuTimeMs                             = 1.0f;

    /** Resolves bound program's resources against m_shadow / m_globalUboRegistry, hashes, and
        returns the cached or freshly written descriptor set. */
    VkDescriptorSet FlushDescriptorState();

    /** Global (non-per-draw) UBO handles indexed by GL slot. Fallback source when no explicit
        BindUniformBuffer overrides the slot. */
    struct GlobalUboEntry
    {
      VkBuffer handle = VK_NULL_HANDLE;
      uint64_t size   = 0;
    };

    std::array<GlobalUboEntry, kMaxUboSlots> m_globalUboRegistry {};

    /** Dummy textures filling declared-but-unbound slots in descriptor sets. */
    std::shared_ptr<struct VulkanTexture> m_dummyTexture;
    std::shared_ptr<struct VulkanTexture> m_dummyCubeTexture;
    std::shared_ptr<struct VulkanTexture> m_dummy2DArrayTexture;
    void CreateDummyTexture();

    /** dynamicOffsets[0] for the next Draw's vkCmdBindDescriptorSets. Set by SubmitPerDrawData;
        reset by BeginFrame and BindPipeline. */
    uint32_t m_currentDynamicOffset = 0;

    /** Cached viewport/scissor — re-issued onto the new cb after FlushAndResetRing. */
    struct CachedRect2D
    {
      uint32_t x = 0, y = 0, w = 0, h = 0;
      bool valid = false;
    };

    CachedRect2D m_cachedViewport {};
    CachedRect2D m_cachedScissor {};

    /** Recovery for per-draw ring overflow: flush in-progress cb, wait, reset pools/cache/ring,
        re-issue dynamic state onto the new cb. Shadow state preserved. */
    void FlushAndResetRing();

    /** Per-FIF deletion buckets. Bucket N is appended to during frame N and drained at the start
        of frame N + FRAMES_IN_FLIGHT, by which point the slot's fence has retired. */
    std::vector<std::vector<std::function<void()>>> m_pendingDeleters;
    uint m_deleterSlot = 0;

    /** First BeginFrame must skip the drain — init-time DeferDelete'd entries live in this slot
        but haven't been consumed by any cb yet; draining now would destroy resources the
        pending init barriers/copies are about to touch. */
    bool m_firstFrame  = true;

    void DrainDeleterBucket(uint slot);
    void DrainAllDeleters();

    /** Drops every cached RP variant + VkFramebuffer for @p fbData; evicts dependent pipelines. */
    void EvictFramebufferCache(struct VulkanFramebuffer* fbData);

    /** Builds a VkRenderPass variant for @p clearBits and stores it in fbData. */
    bool BuildRpVariant(struct VulkanFramebuffer* fbData, GraphicBitFields clearBits);

    /** Builds fbData->framebuffer from current attachment views against fbData->renderPass. */
    bool BuildOffscreenFramebuffer(struct VulkanFramebuffer* fbData);

    /** RP variant lookup with build-on-miss. */
    bool EnsureRpForClearBits(struct VulkanFramebuffer* fbData, GraphicBitFields clearBits);

    /** Closes the offscreen RP if open, updates attachment layouts to RP finalLayouts, and
        drains EnqueueGpuWork parked while the RP was open. */
    void CloseOffscreenRenderPassIfOpen(VkCommandBuffer cb);
  };

  IGraphicsBackend* CreateGraphicsBackend();

} // namespace ToolKit

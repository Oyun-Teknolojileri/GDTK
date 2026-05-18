/*
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

// Forward declare so we don't drag vulkan.h into every translation unit that includes this header.
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

    /** The command buffer being recorded this frame, or VK_NULL_HANDLE between frames. */
    VkCommandBuffer GetCurrentCommandBuffer() const;

    // Backend initialization
    void InitBackend(const BackendInitParams& params) override;

    // Frame lifecycle
    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;

    // Pass boundary
    void StartPass(const PassDesc& desc) override;
    void FinishPass() override;

    // Viewport / scissor
    void SetViewport(uint x, uint y, uint w, uint h) override;
    void SetScissor(uint x, uint y, uint w, uint h) override;

    // Clear
    void ClearBuffer(GraphicBitFields fields, const Vec4& color) override;
    void ClearColorBuffer(const Vec4& color) override;

    // Pipeline / program binding
    void BindPipeline(const GpuProgramPtr& program, const RenderState* state) override;

    // Per-draw resource binding
    void SubmitPerDrawData(const void* data, size_t size) override;
    void BindTexture(ubyte slot, TexturePtr tex) override;
    void BindUniformBuffer(const String& name, UniformBuffer* ub) override;

    // Geometry draw
    void Draw(const DrawDesc& desc) override;

    // Utility / blit
    void ResolveFramebuffer(FramebufferPtr src, FramebufferPtr dst, const IntArray& attachments) override;
    void CopyFramebuffer(FramebufferPtr src, FramebufferPtr dst, GraphicBitFields fields) override;
    void BlitToScreen(FramebufferPtr src) override;

    // Timer queries
    void StartTimerQuery() override;
    void EndTimerQuery() override;
    void GetElapsedTime(float& cpu, float& gpu) override;

    // Texture resource management 
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

    // Mesh resource management
    void CreateMesh(Mesh* mesh) override;
    void DestroyMesh(Mesh* mesh) override;

    // UniformBuffer resource management
    void CreateUniformBuffer(UniformBuffer* ub, uint64 size) override;
    void DestroyUniformBuffer(UniformBuffer* ub) override;
    void UpdateUniformBuffer(UniformBuffer* ub, const void* data, uint64 size) override;

    // Shader resource management
    GpuResourceDataPtr CreateShader(Shader* shader, const String& source) override;
    void DestroyShader(GpuResourceData* shaderData) override;

    // GpuProgram resource management
    void CreateGpuProgram(GpuProgram* program, struct GlobalGpuBuffers* buffers) override;
    void DestroyGpuProgram(GpuProgram* program) override;
    int GetUniformLocation(GpuProgram* program, const char* name) override;

    // Framebuffer resource management
    void CreateFramebuffer(Framebuffer* fb) override;
    void DestroyFramebuffer(Framebuffer* fb) override;
    void AttachColorTarget(Framebuffer* fb, RenderTargetPtr rt, int attachment, int mip, int layer, int face) override;
    void DetachColorTarget(Framebuffer* fb, int attachment) override;
    void AttachDepthTarget(Framebuffer* fb, DepthTexturePtr dt) override;
    void DetachDepthTarget(Framebuffer* fb) override;

    // Custom uniforms and renderer utility
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

    /**
     * Defers a destruction lambda until the GPU has finished with whatever the lambda releases.
     * The lambda is captured into the current frame's bucket and fired on the next BeginFrame
     * after that bucket's fence has been waited on (i.e., GPU is guaranteed done with the cmd
     * buffer that may still reference the resource). Used to retire VkRenderPass/Framebuffer/
     * ImageView/Sampler/Image instances that get replaced or destroyed mid-frame.
     *
     * Capture by value anything the lambda needs (device handle, allocator, shared_ptr to a
     * GpuResourceData, etc.) — the lambda outlives the original resource owner.
     */
    void DeferDelete(std::function<void()> fn);

   private:
    std::unique_ptr<VulkanContext> m_context;
    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::unique_ptr<class VulkanPipelineCache> m_pipelineCache;
    bool m_frameStarted  = false;
    bool m_needsRecreate = false;
    Vec4 m_clearColor    = Vec4(0.4f, 0.0f, 0.4f, 1.0f); // Purple — easy to spot in stage 1e tests.

    /** Non-null between StartPass and FinishPass when an offscreen framebuffer is the active
        pass target. nullptr when no pass is configured or the swapchain pass is pending. */
    struct VulkanFramebuffer* m_activePassFb = nullptr;

    /** Stores the last PassDesc supplied to StartPass; consumed by Draw (offscreen path) to
        reconstruct the VkRenderPassBeginInfo for the per-draw render pass instance. */
    PassDesc m_pendingPassDesc{};

    /** True only while an offscreen render pass instance is actively recording inside a Draw
        call. Used to guard operations that are illegal inside a render pass (resolve, blit). */
    bool m_rpActive = false;

    /** True between BindPipeline() success and the next FinishPass(). Draw() bails when false so
        that Stage 7a can land before BindPipeline (Stage 7c) without recording bare draws into
        the cmd buffer (validation: "no pipeline bound"). Reset on FinishPass / new StartPass. */
    bool m_pipelineBound = false;

    /** Cached by BindPipeline, consumed by Draw. The pipeline can't be built at BindPipeline
        time because VulkanPipelineDesc requires the vertex layout, which only arrives with
        DrawDesc. So we stash the program + state and build/lookup the pipeline at Draw time
        when every required field is known. */
    struct VulkanGpuProgram* m_boundProgram = nullptr;
    RenderState m_boundState{};

    /** Pending descriptor bindings recorded by engine-side BindTexture / BindUniformBuffer /
        SubmitPerDrawData calls between BindPipeline and Draw. FlushDescriptorState (called from
        Draw) consumes this to allocate-or-reuse a descriptor set via the cache. Reset on
        BindPipeline so each pipeline binding starts from a clean slate. */
    struct ShadowState
    {
      /** TexturePtr per binding slot. nullptr = unbound (filled with dummy at flush time). */
      std::array<TexturePtr, VulkanBindings::kTextureBindingCount> boundTextures{};

      /** UBO bindings keyed by GL slot. Engine code currently routes UBOs through
          UpdateUniformBuffer (slot-keyed), so we mirror that key for now; named binding via
          BindUniformBuffer overrides whatever the slot map holds. */
      std::unordered_map<int, UniformBuffer*> boundUniforms;

      /** True after SubmitPerDrawData has appended to the per-draw ring this draw cycle. */
      bool perDrawSubmitted = false;

      /** Payload size of the most recent SubmitPerDrawData call. Drives the descriptor write's
          range field for the per-draw dynamic UBO. */
      uint64_t perDrawSize  = 0;

      /** Set whenever any binding changes. Cleared by FlushDescriptorState after consume. */
      bool dirty            = false;

      void Reset()
      {
        for (auto& t : boundTextures)
        {
          t.reset();
        }
        boundUniforms.clear();
        perDrawSubmitted = false;
        perDrawSize      = 0;
        dirty            = false;
      }
    };
    ShadowState m_shadow;

    /** Per-frame descriptor set cache. Key = state hash (active binding handles); value =
        the VkDescriptorSet allocated and written for that exact state. Cleared at BeginFrame
        when ResetFrameDescriptorPool releases all sets allocated last cycle. */
    struct DescriptorCacheEntry
    {
      uint64_t hash    = 0;
      VkDescriptorSet set = VK_NULL_HANDLE;
    };
    std::array<std::vector<DescriptorCacheEntry>, 2> m_descriptorCache{}; // sized to FRAMES_IN_FLIGHT

    // Timer query state. Mirrors GLBackend's single-cycle gating: one in-flight query at a time.
    // The pool holds 2 timestamps (start, end); a fresh cycle resets the pool and writes start at
    // PreRender of the first render path, then end at PostRender. The result is polled in
    // EndTimerQuery via non-blocking GetQueryPoolResults, so multiple frames may pass before the
    // next cycle begins — matches GL's m_timerQueryWaiting behavior.
    VkQueryPool m_timestampPool   = VK_NULL_HANDLE;
    bool m_timerSupported         = false;
    bool m_timerQueryActive       = false;
    bool m_timerQueryWaiting      = false;
    float m_timestampPeriodNs     = 1.0f; //!< vkPhysicalDeviceLimits::timestampPeriod, cached.
    float m_cpuStartMs            = 0.0f;
    float m_cpuTimeMs             = 1.0f; //!< Defaults to 1.0 so Stats' `1000/x` FPS calc doesn't divide by zero.
    float m_gpuTimeMs             = 1.0f;

    /** Resolves the bound program's resource declarations against m_shadow / m_globalUboRegistry,
        hashes the active handles, and returns the cached (or freshly allocated + written)
        descriptor set for the current frame. Returns VK_NULL_HANDLE if no descriptor work is
        possible (no pipeline / no swapchain / pool exhausted). */
    VkDescriptorSet FlushDescriptorState();

    /** Registry of global (non-per-draw) UBO VkBuffer handles, keyed by GL slot.
        Populated by UpdateUniformBuffer for every slot != 6 (the per-draw dynamic slot).
        FlushDescriptorState reads this as the fallback UBO source when the shader's declared
        UBO slot is not overridden by an explicit BindUniformBuffer call. */
    struct GlobalUboEntry { VkBuffer handle = VK_NULL_HANDLE; uint64_t size = 0; };
    std::unordered_map<int, GlobalUboEntry> m_globalUboRegistry; // GL slot -> entry

    /** Dummy texture initialized in InitBackend to fill unused texture slots in descriptor sets. */
    std::shared_ptr<struct VulkanTexture> m_dummyTexture;
    std::shared_ptr<struct VulkanTexture> m_dummyCubeTexture;
    std::shared_ptr<struct VulkanTexture> m_dummy2DArrayTexture;
    void CreateDummyTexture();

    /** Per-draw UBO ring offset for the next Draw's vkCmdBindDescriptorSets dynamicOffsets[0]
        (Stage 7d-4b). SubmitPerDrawData stores the slot offset here; if no SubmitPerDrawData
        runs before a Draw, this stays at 0 and the shader simply reads whatever is at the start
        of the ring \u2014 stub shaders don't, so it's a safe no-op. Reset by BeginFrame and
        BindPipeline. */
    uint32_t m_currentDynamicOffset        = 0;

    /** Cached dynamic viewport/scissor state, refreshed by SetViewport / SetScissor and
        re-issued onto the new command buffer by FlushAndResetRing. Vulkan dynamic state lives
        on the VkCommandBuffer; mid-frame cmd buffer resets lose it, so we keep the latest
        values here to restore after a flush+reset cycle. */
    struct CachedRect2D
    {
      uint32_t x = 0, y = 0, w = 0, h = 0;
      bool valid = false;
    };
    CachedRect2D m_cachedViewport{};
    CachedRect2D m_cachedScissor{};

    /** Recovery path for per-draw ring overflow. Submits the in-progress cmd buffer, waits for
        the GPU to drain, resets the descriptor pool / descriptor cache / ring head, and
        re-issues cached dynamic state onto the freshly begun cmd buffer. CPU-side shadow
        state (bound textures/UBOs/program) is left intact so the upcoming Draw rebuilds its
        descriptor set from current bindings. Called from SubmitPerDrawData when
        AllocatePerDrawSlot fails. */
    void FlushAndResetRing();

    /**
     * Per-frame-in-flight deletion buckets. Sized to VulkanSwapchain::FRAMES_IN_FLIGHT in the
     * constructor. Bucket index N is appended to during frame N (and between frames before the
     * next BeginFrame), and drained at the start of frame N + FRAMES_IN_FLIGHT — at which point
     * the corresponding fence has been waited on, so any cmd-buffer reference held over from
     * that earlier frame is guaranteed retired.
     */
    std::vector<std::vector<std::function<void()>>> m_pendingDeleters;
    /** Slot in @ref m_pendingDeleters that DeferDelete writes to. Always points at the swapchain
        slot of the current (or next pending) frame. */
    uint m_deleterSlot = 0;
    /** Tracks whether BeginFrame has run at least once. The first BeginFrame must skip
        DrainDeleterBucket: at that point the slot's fence has never gated a real submission, so
        anything DeferDelete'd during init (e.g. textures created and immediately destroyed by
        loader code) sits in the slot waiting to be drained. Draining it before
        FlushPendingGpuWork would vkDestroyImage handles that the pending init barriers/copies
        are about to use. By skipping the first drain, those entries stay in the slot until the
        next time it becomes current (frame N + FRAMES_IN_FLIGHT), by which point this frame's
        cb has retired and it is genuinely safe to drain. */
    bool m_firstFrame  = true;

    /** Run + clear all deleters in a single bucket. Safe to call with an out-of-range index. */
    void DrainDeleterBucket(uint slot);
    /** Run + clear every bucket. Used on shutdown after vkDeviceWaitIdle. */
    void DrainAllDeleters();

    /** Drops every cached RP variant + the VkFramebuffer for @p fbData. Used when attachments
        change (fbData->dirty) — old handles get deferred-deleted and their pipelines evicted
        from the pipeline cache. Leaves fbData->renderPass / framebuffer at VK_NULL_HANDLE. */
    void EvictFramebufferCache(struct VulkanFramebuffer* fbData);

    /** Builds a single VkRenderPass variant for @p clearBits and stores it in the next free
        slot of fbData->rpVariants. Sets fbData->renderPass to the new RP. Does NOT touch
        fbData->framebuffer. Returns false on allocation / vkCreateRenderPass failure. */
    bool BuildRpVariant(struct VulkanFramebuffer* fbData, GraphicBitFields clearBits);

    /** Builds fbData->framebuffer using fbData->renderPass + the current attachment views.
        Caller guarantees fbData->renderPass is valid. Returns false on vkCreateFramebuffer
        failure. */
    bool BuildOffscreenFramebuffer(struct VulkanFramebuffer* fbData);

    /** Looks up the RP variant matching @p clearBits in fbData->rpVariants; on miss, builds
        a new variant. Sets fbData->renderPass to the matching/new RP. Returns false if a
        build was needed and failed. */
    bool EnsureRpForClearBits(struct VulkanFramebuffer* fbData, GraphicBitFields clearBits);
  };

  /** Factory function called by RenderSystem::CreateBackend(). */
  IGraphicsBackend* CreateGraphicsBackend();

} // namespace ToolKit
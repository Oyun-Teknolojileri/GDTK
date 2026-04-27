/*
 /*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "../IGraphicsBackend.h"

#include <vulkan/vulkan.h>

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
  class VulkanTestPipeline;

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
    void BeginPass(const PassDesc& desc) override;
    void EndPass() override;

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
    void* GetNativeTextureHandle(Texture* tex) override;
    void SetDebugLabel(Texture* tex) override;
    void SetDebugLabel(Framebuffer* fb) override;

    /**
     * Stage 2b scaffold: emits the fullscreen test triangle into the currently-active offscreen
     * pass. No-op if no offscreen pass is recording. Removed once real meshes (Stage 3) replace it.
     */
    void DrawTestTriangle();

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
    std::unique_ptr<VulkanTestPipeline> m_testPipeline; // Stage 2b scaffold; removed in Stage 3.
    bool m_frameStarted  = false;
    bool m_needsRecreate = false;
    Vec4 m_clearColor    = Vec4(0.4f, 0.0f, 0.4f, 1.0f); // Purple — easy to spot in stage 1e tests.

    /** Non-null while an offscreen render pass is recording on the current command buffer.
        nullptr when either no pass is active or the swapchain pass is the active one (the
        swapchain tracks its own pass-active flag). */
    struct VulkanFramebuffer* m_activePassFb = nullptr;

    /** True between BindPipeline() success and the next EndPass(). Draw() bails when false so
        that Stage 7a can land before BindPipeline (Stage 7c) without recording bare draws into
        the cmd buffer (validation: "no pipeline bound"). Reset on EndPass / new BeginPass. */
    bool m_pipelineBound = false;

    /** Cached by BindPipeline, consumed by Draw. The pipeline can't be built at BindPipeline
        time because VulkanPipelineDesc requires the vertex layout, which only arrives with
        DrawDesc. So we stash the program + state and build/lookup the pipeline at Draw time
        when every required field is known. */
    struct VulkanGpuProgram* m_boundProgram = nullptr;
    RenderState m_boundState{};

    /** Active per-draw descriptor set (Stage 7d-4). Lazily allocated from the per-frame pool on
        the first BindTexture / SubmitPerDrawData call following a BindPipeline; reused by every
        write within the same draw cycle so multiple BindTexture calls fold into a single set.
        Draw binds it via vkCmdBindDescriptorSets and then null-flips it so the next draw starts
        a fresh allocation \u2014 Vulkan forbids modifying a set that may still be referenced by an
        earlier vkCmdDraw. */
    VkDescriptorSet m_currentDescriptorSet = VK_NULL_HANDLE;

    /** Registry of global (non-per-draw) UBO VkBuffer handles, keyed by GL slot.
        Populated by UpdateUniformBuffer for every slot != 6 (the per-draw dynamic slot).
        When a fresh descriptor set is allocated, WriteGlobalUbosToSet writes every registered
        entry so the shader always has valid descriptors for Camera, GraphicConsts, lights, etc. */
    struct GlobalUboEntry { VkBuffer handle = VK_NULL_HANDLE; uint64_t size = 0; };
    std::unordered_map<int, GlobalUboEntry> m_globalUboRegistry; // GL slot -> entry

    /** Writes all registered global UBOs into @p set immediately after allocation. */
    void WriteGlobalUbosToSet(VkDescriptorSet set);

    /** Per-draw UBO ring offset for the next Draw's vkCmdBindDescriptorSets dynamicOffsets[0]
        (Stage 7d-4b). SubmitPerDrawData stores the slot offset here; if no SubmitPerDrawData
        runs before a Draw, this stays at 0 and the shader simply reads whatever is at the start
        of the ring \u2014 stub shaders don't, so it's a safe no-op. Reset by BeginFrame and
        BindPipeline. */
    uint32_t m_currentDynamicOffset        = 0;

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

    /** Run + clear all deleters in a single bucket. Safe to call with an out-of-range index. */
    void DrainDeleterBucket(uint slot);
    /** Run + clear every bucket. Used on shutdown after vkDeviceWaitIdle. */
    void DrainAllDeleters();

    /** Lazy-builds (or rebuilds when @p fbData->dirty) the VkRenderPass + VkFramebuffer for an
        offscreen target sized from fbData->width/height with the current attachment views.
        Returns true on success; on failure leaves both handles VK_NULL_HANDLE. */
    bool BuildOffscreenRenderPass(const struct PassDesc& desc, struct VulkanFramebuffer* fbData);
  };

  /** Factory function called by RenderSystem::CreateBackend(). */
  IGraphicsBackend* CreateGraphicsBackend();

} // namespace ToolKit
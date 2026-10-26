/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "ForwardSceneRenderPath.h"

#include "GradientSky.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Scene.h"
#include "Shader.h"
#include "Stats.h"

#include "DebugNew.h"

namespace ToolKit
{
  namespace
  {
    /** Blocks per side of the depth grid. This is HiZBlockSize in hizDepthGridFrag.shader, which does the
     *  reduction: the two have to agree or the grid the CPU indexes is not the grid the shader wrote. */
    constexpr int DepthGridBlockSize = 8;

    /** Grid reads happen every this many frames. A blocking read per frame costs more than it saves. */
    constexpr uint DepthGridReadInterval = 4;

    /** How many frames in a row a light or a mesh has to look hidden before it is dropped. */
    constexpr int OcclusionStreakToCull = 3;

    /** NDC depths closer than this count as sitting on the near plane, which no volume can be hidden
     *  behind: a corner that lands on it has no depth of its own to compare. */
    constexpr float OcclusionNearEpsilon = 1e-4f;

    /** The fraction of its own magnitude a view space depth read out of the pre process G buffer is
     *  allowed to be wrong by. That buffer is RGBA16F, and a half float carries about eleven bits of
     *  mantissa, so a stored distance of d can be off by roughly d / 2048. The grid comparison gives up
     *  that much so a rounding error cannot turn a volume that is in front of the geometry into one that
     *  is behind it. */
    constexpr float LinearDepthSlack = 5e-4f;
  } // namespace

  ForwardSceneRenderPath::ForwardSceneRenderPath()
  {
    m_shadowPass            = MakeNewPtr<ShadowPass>();
    m_forwardRenderPass     = MakeNewPtr<ForwardRenderPass>();
    m_skyPass               = MakeNewPtr<CubeMapPass>();
    m_forwardPreProcessPass = MakeNewPtr<ForwardPreProcessPass>();
    m_ssaoPass              = MakeNewPtr<SSAOPass>();
    m_ssrPass               = MakeNewPtr<SsrPass>();
    m_bloomPass             = MakeNewPtr<BloomPass>();
    m_dofPass               = MakeNewPtr<DoFPass>();
    m_gammaTonemapFxaaPass  = MakeNewPtr<GammaTonemapFxaaPass>();
    m_resolvedFramebuffer   = MakeNewPtr<Framebuffer>("ForwardResolveFB");
  }

  ForwardSceneRenderPath::~ForwardSceneRenderPath()
  {
    m_shadowPass            = nullptr;
    m_forwardRenderPass     = nullptr;
    m_skyPass               = nullptr;
    m_ssaoPass              = nullptr;
    m_ssrPass               = nullptr;
    m_forwardPreProcessPass = nullptr;
    m_bloomPass             = nullptr;
    m_dofPass               = nullptr;
    m_gammaTonemapFxaaPass  = nullptr;
  }

  void ForwardSceneRenderPath::Render(Renderer* renderer)
  {
    TK_PROFILE_FUNCTION();

    // The frame starts with a clean depth buffer. It has to be cleared here rather than by the composite
    // pass at the end of the frame, which no longer clears the depth. Every pass of a frame assumes it
    // starts from a clear buffer - the forward pass skips its own depth clear whenever a pre process pass
    // exists, so a camera that moved would otherwise be testing this frame's geometry against last
    // frame's - and the stencil comes along because the composite pass used to clear it in the same call.
    if (m_params.MainFramebuffer != nullptr)
    {
      renderer->SetFramebuffer(m_params.MainFramebuffer, GraphicBitFields::DepthStencilBits);
    }

    PreRender(renderer);

    m_passArray.clear();

    // Shadow pass
    renderer->SetShadowAtlas(Cast<Texture>(m_shadowPass->GetShadowAtlas()));
    m_passArray.push_back(m_shadowPass);

    // Forward Pre Process Pass
    if (RequiresForwardPreProcessPass())
    {
      m_passArray.push_back(m_forwardPreProcessPass);
    }

    // SSAO pass
    if (m_params.postProcessSettings->GetSSAOEnabledVal())
    {
      m_passArray.push_back(m_ssaoPass);
    }

    // Draw sky pass
    renderer->m_sky = m_sky;
    if (m_drawSky)
    {
      m_passArray.push_back(m_skyPass);
    }

    // Forward pass
    m_passArray.push_back(m_forwardRenderPass);

    // Screen space reflections. Runs on the forward shaded color, right before the post process
    // chain, so a reflection miss simply keeps the sky / environment volume IBL the forward pass
    // already wrote. The g buffer it marches against comes from the pre process pass.
    if (m_params.postProcessSettings->GetSSREnabledVal())
    {
      m_passArray.push_back(m_ssrPass);
    }

    // Bloom pass
    if (m_params.postProcessSettings->GetBloomEnabledVal())
    {
      m_passArray.push_back(m_bloomPass);
    }

    // Depth of field pass
    if (m_params.postProcessSettings->GetDepthOfFieldEnabledVal())
    {
      m_passArray.push_back(m_dofPass);
    }

    if (m_gammaTonemapFxaaPass->IsEnabled())
    {
      m_passArray.push_back(m_gammaTonemapFxaaPass);
    }

    RenderPath::Render(renderer);

    renderer->SetShadowAtlas(nullptr);

    // The grid is built at the end of the frame, not at the start of the next one, so the depth it reduces
    // is this frame's own and nothing has to keep that depth alive across a frame boundary. The editor
    // draws its gizmos and billboards into the same depth buffer after this path returns, and a gizmo pass
    // that clears depth used to destroy the depths the next frame's grid needed; taking them here, before
    // any of that runs, removes the reason to save and restore the buffer around the editor's drawing.
    BuildDepthGrid(renderer);

    // The grid pass leaves its own target bound and its viewport at the grid's size, which is not what the
    // rest of the frame expects - the editor draws its overlays into the scene's framebuffer right after
    // this returns. Nothing is cleared here, the scene's color is still the picture.
    if (m_params.MainFramebuffer != nullptr)
    {
      renderer->SetFramebuffer(m_params.MainFramebuffer, GraphicBitFields::None);
    }

    // Remember the camera this frame used: next frame's volumes are projected into the view the depths
    // belong to, and a camera that moved is what makes the grid in hand stale.
    StoreOcclusionCamera();

    PostRender(renderer);
  }

  //////////////////////////////////////////
  // Depth grid occlusion culling

  void ForwardSceneRenderPath::BuildDepthGrid(Renderer* renderer)
  {
    // Read by the next frame, which then re-reads the grid instead of waiting for the interval.
    const bool sceneChanged = m_sceneChangedLastFrame;
    m_sceneChangedLastFrame = false;

    if (!GetEngineSettings().m_graphics->GetHiZCullingVal())
    {
      InvalidateDepthGrid();
      return;
    }

    // Which depth the grid reduces, and both sources answer the same question. The pre process pass, when
    // the post processing settings ask for it, has already written the frame's view-space depth into the
    // blue channel of its G buffer and resolved it for SSAO / SSR / DoF, so reading it needs no copy and no
    // extra target: that is the source this prefers. Without the pass, the forward pass's own depth
    // attachment is what is left, and while MSAA is on that attachment is a renderbuffer which has to be
    // resolved into a single sample copy first.
    FramebufferPtr depthSource = nullptr;
    TexturePtr depthBuffer     = nullptr;

    if (RequiresForwardPreProcessPass() && m_forwardPreProcessPass != nullptr)
    {
      TexturePtr gBuffer = m_forwardPreProcessPass->m_normalDepthRt;
      if (gBuffer != nullptr && gBuffer->IsMultiSampled())
      {
        gBuffer = gBuffer->GetResolvedTexture();
      }

      if (gBuffer != nullptr && gBuffer->m_width > 0 && gBuffer->m_height > 0)
      {
        depthBuffer = gBuffer;
      }
    }

    if (depthBuffer == nullptr)
    {
      depthSource = m_forwardRenderPass && m_forwardRenderPass->m_params.FrameBuffer
                        ? m_forwardRenderPass->m_params.FrameBuffer
                        : m_params.MainFramebuffer;

      depthBuffer = depthSource ? depthSource->GetDepthTexture() : nullptr;
      if (depthBuffer == nullptr || depthBuffer->m_width == 0 || depthBuffer->m_height == 0)
      {
        InvalidateDepthGrid();
        return;
      }
    }

    // The two sources do not answer in the same units: the G buffer holds a view space distance, the depth
    // attachment whatever encoding the projection wrote. A grid built from one says nothing about the
    // other, so a change of source - SSAO or SSR switched on or off in the middle of a session - drops the
    // grid and the streaks instead of comparing one unit against the other.
    const bool linearDepth = depthSource == nullptr;
    if (linearDepth != m_occlusionLinearDepth)
    {
      m_occlusionLinearDepth = linearDepth;
      InvalidateDepthGrid();
    }

    // The interval is the steady state only. The depths in the grid were written for a view of a scene that
    // has not changed since, so an old copy describes the picture exactly as well as a fresh one would. A
    // frame that did change something - a camera that moved, so the blocks no longer name the same
    // geometry, or a scene edit, so something is now in front of what it was behind - is what makes the
    // copy in hand worthless, and that is what is read here. Everything below, the resolve included, is
    // skipped on the frames between two reads.
    bool viewMoved = sceneChanged;
    if (Camera* camera = m_params.Cam.get())
    {
      if (camera->GetIdVal() != m_occlusionCameraId)
      {
        // The grid was reduced from another camera's depths. Reading now means it becomes usable one
        // frame after the switch instead of up to a read interval later.
        viewMoved = true;
      }
      else
      {
        const float* currentView = glm::value_ptr(camera->GetProjectViewMatrix());
        const float* gridView    = glm::value_ptr(m_occlusionViewProj);
        for (int i = 0; i < 16 && !viewMoved; ++i)
        {
          const float scale = glm::max(1.0f, glm::abs(gridView[i]));
          viewMoved         = glm::abs(currentView[i] - gridView[i]) > 1e-6f * scale;
        }
      }
    }

    // InvalidateDepthGrid resets the counter, so a grid that was dropped is rebuilt on the next frame
    // rather than up to an interval later.
    const bool readNow = viewMoved || (m_depthGridFrame % DepthGridReadInterval) == 0;
    m_depthGridFrame++;

    if (!readNow)
    {
      return;
    }

    // A multisampled depth attachment cannot be read in place. It is a renderbuffer, because neither
    // GL ES 3.0 nor WebGL 2 has a multisample sampler to fetch it with, so the frame's depth is resolved
    // into a single sample depth texture first: one depth blit, which the hardware does, and the grid
    // reduces that copy exactly as it reduces a single sample frame. The blit is one to one, because a
    // multisampled source can be resolved but not scaled.
    //
    // The resolve picks one sample per pixel, so the copy is the picture the frame shows at pixel
    // centers rather than every sample of it. That can differ from the per sample depths only inside a
    // pixel that a silhouette crosses, which costs at most a fraction of one pixel of an edge - the
    // reason it is acceptable where a nearest downscale of the whole buffer would not be: that one lost
    // eight by eight blocks of depth at once, not one pixel's worth.
    const bool msaaDepth = depthSource != nullptr && depthBuffer->IsMultiSampled();

    // Reported when the source changes rather than per read: which depth the grid reduces decides both what
    // it costs and whether a multisampled frame can be culled at all, and a line every interval would bury
    // the rest.
    const int depthSourceKind = linearDepth ? 2 : (msaaDepth ? 1 : 0);
    if (depthSourceKind != m_loggedDepthSource)
    {
      m_loggedDepthSource = depthSourceKind;

      if (linearDepth)
      {
        TK_LOG("HiZ culling: the grid reduces the pre process pass's view-space depth (%dx%d), no copy.",
               depthBuffer->m_width,
               depthBuffer->m_height);
      }
      else
      {
        TK_LOG("HiZ culling: the frame's depth is %s (%dx%d), the grid reduces %s.",
               msaaDepth ? "multisampled" : "single sample",
               depthBuffer->m_width,
               depthBuffer->m_height,
               msaaDepth ? "a single sample resolve of it" : "it directly");
      }
    }

    if (msaaDepth)
    {
      depthBuffer = ResolveDepthBuffer(renderer, depthSource.get(), depthBuffer);
      if (depthBuffer == nullptr)
      {
        InvalidateDepthGrid();
        return;
      }
    }

    const UVec2 gridSize(glm::max(depthBuffer->m_width / DepthGridBlockSize, 1),
                         glm::max(depthBuffer->m_height / DepthGridBlockSize, 1));

    if (m_depthGridQuad == nullptr)
    {
      m_depthGridShader = GetShaderManager()->Create<Shader>(ShaderPath("hizDepthGridFrag.shader", true));

      // Compiled here rather than on the first program build, because the channel the reduction reads is
      // picked by a define and a define can only be set on an initialized shader.
      m_depthGridShader->Init();

      m_depthGridQuad = MakeNewPtr<FullQuadPass>();
      m_depthGridFb   = MakeNewPtr<Framebuffer>("DepthGridFB");
      m_depthGridRt   = MakeNewPtr<RenderTarget>("DepthGridRT");

      // The quad is drawn from here rather than from the pass array, so nothing else hands it a
      // renderer, and its own PreRender would build the program through a null one.
      m_depthGridQuad->SetRenderer(renderer);
    }

    if (m_depthGridSize != gridSize)
    {
      // One float per block, thirty two bits wide. The depth read back here is compared against a
      // depth taken from the projection, and on a scene with a far plane in the thousands an eight bit
      // channel puts both of them inside one quantization step, which is a decision made by rounding.
      TextureSettings settings;
      settings.WarpS          = GraphicTypes::UVClampToEdge;
      settings.WarpT          = GraphicTypes::UVClampToEdge;
      settings.InternalFormat = GraphicTypes::FormatRGBA32F;
      settings.Format         = GraphicTypes::FormatRGBA;
      settings.Type           = GraphicTypes::TypeFloat;
      settings.MinFilter      = GraphicTypes::SampleNearest;
      settings.MagFilter      = GraphicTypes::SampleNearest;

      m_depthGridRt->Settings(settings);
      m_depthGridRt->ReconstructIfNeeded(gridSize.x, gridSize.y);

      // No depth attachment on the grid itself: the full quad writes every texel and never tests one.
      m_depthGridFb->ReconstructIfNeeded({(int) gridSize.x, (int) gridSize.y, false, false});
      m_depthGridFb->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_depthGridRt);

      m_depthGridSize  = gridSize;
      m_depthGridValid = false;
    }

    // The quad's PreRender installs the material's fragment shader and builds the program from it, so
    // the grid shader goes on the material: a shader that only sits in the requirements is not what
    // ends up compiled. The depth buffer is then bound by slot, which ApplyRequirements resolves
    // after the program is up, when a slot can still turn into a sampler.
    //
    // The define comes before the shader is installed: it picks the channel the reduction reads, and the
    // program is built from the variant the shader holds at that moment. Both variants are compiled
    // together on first use, so switching source does not stall on a runtime compile.
    m_depthGridShader->SetDefine("HiZLinearDepth", linearDepth ? "1" : "0");

    m_depthGridQuad->m_params.frameBuffer      = m_depthGridFb;
    m_depthGridQuad->m_params.clearFrameBuffer = GraphicBitFields::None;
    m_depthGridQuad->SetFragmentShader(m_depthGridShader, renderer);

    // The quad material's default diffuse texture sits on slot 0, the slot this shader wants for the
    // depth, and the quad's own requirement merge pushes it back in after the depth is bound.
    if (m_depthGridQuad->m_material != nullptr)
    {
      m_depthGridQuad->m_material->SetDiffuseTextureVal(nullptr);
    }

    PassRequirements& reqs = m_depthGridQuad->GetRequirements();
    reqs                   = PassRequirements();
    reqs.fragmentShader    = m_depthGridShader;
    reqs.textures[0]       = depthBuffer;

    m_depthGridQuad->PreRender();
    m_depthGridQuad->Render();

    // Read while the pass is still open: the read takes the pass's own target, and closing a pass
    // with no discard bits set is what a read after it would have to work around.
    const size_t count = size_t(gridSize.x) * size_t(gridSize.y);
    if (m_depthGridScratch.size() != count * 4)
    {
      m_depthGridScratch.resize(count * 4);
    }
    if (m_depthGrid.size() != count)
    {
      m_depthGrid.resize(count, 0.0f);
    }

    renderer->GetBackend()->ReadPixels(0,
                                       0,
                                       (int) gridSize.x,
                                       (int) gridSize.y,
                                       GraphicTypes::FormatRGBA,
                                       GraphicTypes::TypeFloat,
                                       m_depthGridScratch.data());

    // A real depth buffer cannot be zero everywhere - the nearest surface a frame can show sits at the
    // near plane, which is half the depth range on a zero to one projection - so an all zero read means
    // the grid never arrived. Culling against it would call every block nearer than every light and drop
    // all of them, which is exactly what a broken read looks like from the outside.
    bool anyDepth = false;
    for (size_t i = 0; i < count; ++i)
    {
      m_depthGrid[i] = m_depthGridScratch[i * 4 + 0];
      anyDepth       = anyDepth || (m_depthGrid[i] > 0.0f);
    }
    m_depthGridValid = anyDepth;

    // One line per transition, because an empty grid is silent otherwise: it turns culling off in the
    // frame that reads it, and nothing else in the log would say so.
    if (!anyDepth && !m_loggedEmptyGrid)
    {
      m_loggedEmptyGrid = true;
      TK_LOG("HiZ culling: the depth grid read came back empty, nothing is culled until it has depths.");
    }
    else if (anyDepth && m_loggedEmptyGrid)
    {
      m_loggedEmptyGrid = false;
      TK_LOG("HiZ culling: the depth grid has depths again.");
    }

    m_depthGridQuad->PostRender();
  }

  TexturePtr ForwardSceneRenderPath::ResolveDepthBuffer(Renderer* renderer,
                                                        Framebuffer* depthSource,
                                                        TexturePtr depthBuffer)
  {
    if (renderer == nullptr || depthSource == nullptr || depthBuffer == nullptr)
    {
      return nullptr;
    }

    if (m_depthResolveFb == nullptr)
    {
      m_depthResolveFb = MakeNewPtr<Framebuffer>("DepthResolveFB");
    }

    // The destination of a depth blit has to carry the same depth format as the source - a depth
    // attachment that carries stencil is a different format - and it has to be single sample, which is
    // what the resolve is for. The size is the source's, because a multisampled source can be resolved
    // but not scaled.
    DepthTexture* srcDepth = depthBuffer->As<DepthTexture>();
    const bool stencil     = srcDepth != nullptr && srcDepth->m_stencil;
    const UVec2 size(depthBuffer->m_width, depthBuffer->m_height);

    // The target is rebuilt only when the frame it copies changes size or depth format. The check is
    // made on those two values rather than by handing the settings struct back to ReconstructIfNeeded,
    // which compares it with memcmp: the struct has padding that its constructor does not write, so two
    // equal settings can compare unequal and rebuild the attachment on every read.
    if (m_depthResolveSize != size || m_depthResolveStencil != stencil)
    {
      FramebufferSettings settings;
      settings.width           = size.x;
      settings.height          = size.y;
      settings.depthStencil    = stencil;
      settings.useDefaultDepth = true;

      m_depthResolveFb->ReconstructIfNeeded(settings);

      m_depthResolveSize    = size;
      m_depthResolveStencil = stencil;
    }

    // Cleared to the far plane before the copy. A blit that a driver refuses then leaves the grid with
    // one value everywhere instead of with whatever the allocation happened to hold: the whole buffer
    // reads as "behind everything", no volume is found in front of it, and the frame is drawn as if
    // culling were off, which is the only acceptable outcome for a resolve that did not happen.
    renderer->SetFramebuffer(m_depthResolveFb, GraphicBitFields::DepthStencilBits);

    renderer->GetBackend()->BlitDepth(depthSource, m_depthResolveFb.get());

    return m_depthResolveFb->GetDepthTexture();
  }

  void ForwardSceneRenderPath::StoreOcclusionCamera()
  {
    Camera* camera = m_params.Cam.get();
    if (camera == nullptr)
    {
      return;
    }

    m_occlusionViewProj   = camera->GetProjectViewMatrix();
    m_occlusionView       = camera->GetViewMatrix();
    m_occlusionProjection = camera->GetProjectionMatrix();
    m_occlusionNear       = camera->Near();
    m_occlusionFar        = camera->Far();

    // Which way the depth buffer runs. The grid keeps whatever the projection wrote, and a reversed
    // range counts nearer as larger, so the comparison against a light's depth has to know the
    // direction instead of assuming larger means farther. Far has to be read before the two clips are
    // projected, or the first call measures against the previous camera's far plane.
    const Vec4 nearClip      = m_occlusionProjection * Vec4(0.0f, 0.0f, -m_occlusionNear, 1.0f);
    const Vec4 farClip       = m_occlusionProjection * Vec4(0.0f, 0.0f, -m_occlusionFar, 1.0f);
    const float nearDepth    = (nearClip.z / nearClip.w) * 0.5f + 0.5f;
    const float farDepth     = (farClip.z / farClip.w) * 0.5f + 0.5f;
    m_occlusionReversedDepth = nearDepth > farDepth;

    // The depths the next frame reduces were written by this camera.
    m_occlusionCameraId = camera->GetIdVal();
  }

  void ForwardSceneRenderPath::InvalidateDepthGrid()
  {
    m_depthGridValid = false;
    m_lightOcclusionStreak.clear();
    ResetEntityOcclusionStreaks();

    // A dropped grid is rebuilt on the next frame instead of up to a read interval later: the read is
    // what brings a new one, and there is nothing to cull with until it happens.
    m_depthGridFrame = 0;
  }

  bool ForwardSceneRenderPath::OcclusionCullingReady()
  {
    if (!GetEngineSettings().m_graphics->GetHiZCullingVal())
    {
      m_depthGridValid = false;
      return false;
    }

    Camera* camera     = m_params.Cam.get();
    const uint64 camId = camera ? camera->GetIdVal() : 0;

    // The grid in hand was reduced from another view's depths, so it proves nothing about this one.
    // A camera that just changed therefore gets a frame with no culling, and the streaks start over:
    // otherwise the first frames of a new view are culled against the old view's geometry.
    if (camId != m_occlusionCameraId)
    {
      InvalidateDepthGrid();
      return false;
    }

    return m_depthGridValid && m_depthGridSize.x > 0 && m_depthGridSize.y > 0;
  }

  bool ForwardSceneRenderPath::IsOccludedByGrid(const Vec3& boxMin, const Vec3& boxMax, float nearestDepth) const
  {
    if (!m_depthGridValid || m_depthGridSize.x == 0 || m_depthGridSize.y == 0)
    {
      return false;
    }

    const int gridW = int(m_depthGridSize.x);
    const int gridH = int(m_depthGridSize.y);

    float ndcMinX = 1e30f;
    float ndcMaxX = -1e30f;
    float ndcMinY = 1e30f;
    float ndcMaxY = -1e30f;

    for (int corner = 0; corner < 8; ++corner)
    {
      const Vec3 p((corner & 1) ? boxMax.x : boxMin.x,
                   (corner & 2) ? boxMax.y : boxMin.y,
                   (corner & 4) ? boxMax.z : boxMin.z);

      const Vec4 clip = m_occlusionViewProj * Vec4(p, 1.0f);

      // A corner at or behind the camera has no projection, and the part of the box that is behind
      // the near plane is not what the frame drew. Giving up is the only safe answer.
      if (clip.w <= OcclusionNearEpsilon)
      {
        return false;
      }

      const Vec3 ndc = Vec3(clip) / clip.w;

      ndcMinX = glm::min(ndcMinX, ndc.x);
      ndcMaxX = glm::max(ndcMaxX, ndc.x);
      ndcMinY = glm::min(ndcMinY, ndc.y);
      ndcMaxY = glm::max(ndcMaxY, ndc.y);
    }

    // A volume that starts behind the near plane has nothing in the frame to be compared against.
    if (nearestDepth <= m_occlusionNear)
    {
      return false;
    }

    // The volume's nearest point in the same units the grid holds. The G buffer already stores a view
    // space distance, so it is the number itself; the depth attachment needs the projection, which rather
    // than inverting near and far is what keeps this right for a zero to one depth range, a reversed one
    // and an orthographic camera.
    float volumeDepth = nearestDepth;
    if (!m_occlusionLinearDepth)
    {
      const Vec4 volumeClip = m_occlusionProjection * Vec4(0.0f, 0.0f, -nearestDepth, 1.0f);
      volumeDepth           = (volumeClip.z / volumeClip.w) * 0.5f + 0.5f;
    }
    else
    {
      // The G buffer holds its depth as a half float, so a stored value can be off by about a two
      // thousandth of its own magnitude. The comparison gives up that much, which keeps a rounding error
      // from turning a volume that is only just in front of the geometry into one that is behind it.
      volumeDepth *= 1.0f - LinearDepthSlack;
    }

    // Only the part of the footprint that is on screen is tested. Off screen there is no depth to
    // compare against and no pixel of the volume that could be shown, so the overlap is all that has
    // anything to prove; a footprint that is entirely outside is left to the frustum test.
    const float u0 = glm::clamp(ndcMinX * 0.5f + 0.5f, 0.0f, 1.0f);
    const float u1 = glm::clamp(ndcMaxX * 0.5f + 0.5f, 0.0f, 1.0f);
    const float v0 = glm::clamp(ndcMinY * 0.5f + 0.5f, 0.0f, 1.0f);
    const float v1 = glm::clamp(ndcMaxY * 0.5f + 0.5f, 0.0f, 1.0f);

    if (u1 <= u0 || v1 <= v0)
    {
      return false;
    }

    const int gx0 = glm::clamp(int(u0 * float(gridW)), 0, gridW - 1);
    const int gx1 = glm::clamp(int(u1 * float(gridW)), 0, gridW - 1);
    const int gy0 = glm::clamp(int(v0 * float(gridH)), 0, gridH - 1);
    const int gy1 = glm::clamp(int(v1 * float(gridH)), 0, gridH - 1);

    for (int y = gy0; y <= gy1; ++y)
    {
      for (int x = gx0; x <= gx1; ++x)
      {
        // The block holds the farthest depth it covers, so "in front of the volume" means every
        // sample under it is nearer than the volume's nearest point. One block that reaches past
        // the volume means part of that block is beside or behind it, and the volume can still
        // reach the picture through it. A view space distance counts up with distance like an ordinary
        // depth range does; only the depth attachment can be reversed.
        const float depth = m_depthGrid[size_t(y) * size_t(gridW) + size_t(x)];

        const bool inFront = m_occlusionLinearDepth
                                 ? (depth < volumeDepth)
                                 : (m_occlusionReversedDepth ? (depth > volumeDepth) : (depth < volumeDepth));
        if (!inFront)
        {
          return false;
        }
      }
    }

    return true;
  }

  bool ForwardSceneRenderPath::IsOccludedByGrid(const BoundingBox& box) const
  {
    // Only the box's own nearest point is used here: unlike a light, whose box encloses a smaller
    // sphere, the box is the volume the caller is asking about.
    float nearestDepth = std::numeric_limits<float>::max();
    for (int corner = 0; corner < 8; ++corner)
    {
      const Vec3 p((corner & 1) ? box.max.x : box.min.x,
                   (corner & 2) ? box.max.y : box.min.y,
                   (corner & 4) ? box.max.z : box.min.z);

      nearestDepth = glm::min(nearestDepth, -(m_occlusionView * Vec4(p, 1.0f)).z);
    }

    return IsOccludedByGrid(box.min, box.max, nearestDepth);
  }

  void ForwardSceneRenderPath::ResetEntityOcclusionStreaks()
  {
    for (auto& [entity, slot] : m_jobRanges)
    {
      slot.occlusionStreak = 0;
    }
  }

  void ForwardSceneRenderPath::CullOccludedLights(LightRawPtrArray& lights)
  {
    if (!OcclusionCullingReady())
    {
      return;
    }

    Camera* camera = m_params.Cam.get();
    const uint64 camId = camera ? camera->GetIdVal() : 0;

    int culled            = 0;
    const int totalLights = (int) lights.size();

    LightRawPtrArray kept;
    kept.reserve(lights.size());

    std::vector<uint64> seenIds;
    seenIds.reserve(lights.size());

    for (Light* light : lights)
    {
      if (light->GetLightType() == Light::LightType::Directional)
      {
        kept.push_back(light);
        continue;
      }

      const Vec3 centre  = light->m_node->GetTranslation(TransformationSpace::TS_WORLD);
      const float radius = light->AffectDistance();

      // The light's volume as a world box, projected to find the blocks it can reach on screen. A box is
      // a superset of the sphere the light really occupies, so the footprint it produces can only be too
      // large - never too small.
      const Vec3 boxMin = centre - Vec3(radius);
      const Vec3 boxMax = centre + Vec3(radius);

      // The nearest point of the volume itself, and not of its box. A box's nearest corner sits a radius
      // times the square root of three from the centre - well over a radius nearer the camera than the
      // sphere it encloses - and a light that looked that much deeper had the geometry standing in front
      // of it fail to count, which is exactly a light that stays lit when it should not.
      const float centreDepth  = -(m_occlusionView * Vec4(centre, 1.0f)).z;
      const float nearestDepth = centreDepth - radius;

      const bool occluded      = IsOccludedByGrid(boxMin, boxMax, nearestDepth);

      const uint64 lightId = light->GetIdVal();
      seenIds.push_back(lightId);

      int& streak = m_lightOcclusionStreak[lightId];
      if (occluded)
      {
        streak++;
      }
      else
      {
        streak = 0;
      }

      if (streak >= OcclusionStreakToCull)
      {
        culled++;
        continue;
      }

      kept.push_back(light);
    }

    lights.swap(kept);

    // A light removed from the scene would otherwise keep its counter for the life of the process.
    if (m_lightOcclusionStreak.size() > seenIds.size())
    {
      for (auto itr = m_lightOcclusionStreak.begin(); itr != m_lightOcclusionStreak.end();)
      {
        if (std::find(seenIds.begin(), seenIds.end(), itr->first) == seenIds.end())
        {
          itr = m_lightOcclusionStreak.erase(itr);
        }
        else
        {
          ++itr;
        }
      }
    }

    Stats::AddStat(FrameStatType::HiZCulledLight, (uint64) culled);

    // Reported when the decision changes rather than every frame: a camera that holds still has nothing
    // new to say, and one that moves would bury the rest of the log.
    if (culled != m_loggedCullCount || camId != m_loggedCullCameraId)
    {
      m_loggedCullCount    = culled;
      m_loggedCullCameraId = camId;

      TK_LOG("HiZ culling: camera '%s' dropped %d of %d local lights.",
             camera ? camera->GetNameVal().c_str() : "?",
             culled,
             totalLights);
    }
  }

  void ForwardSceneRenderPath::CullOccludedEntities(EntityRawPtrArray& entities)
  {
    if (!OcclusionCullingReady())
    {
      return;
    }

    const int totalEntities = (int) entities.size();

    EntityRawPtrArray kept;
    kept.reserve(entities.size());

    int culled = 0;
    int hidden = 0;

    for (Entity* ntt : entities)
    {
      if (ntt == nullptr)
      {
        continue;
      }

      // The editor's grid is a helper drawn over the scene, not something the scene's depths describe.
      if (m_params.grid != nullptr && ntt == m_params.grid.get())
      {
        kept.push_back(ntt);
        continue;
      }

      const auto slotItr = m_jobRanges.find(ntt);
      if (slotItr == m_jobRanges.end() || !ntt->IsVisible())
      {
        // Nothing cached to test against, or nothing this frame draws anyway.
        kept.push_back(ntt);
        continue;
      }

      JobSlot& slot = slotItr->second;

      // A skinned mesh deforms and an animated one can be driven without the entity moving, so the box
      // the test projects is not the geometry that would be drawn.
      if (m_animatedJobEntities.find(ntt) != m_animatedJobEntities.end())
      {
        slot.occlusionStreak = 0;
        kept.push_back(ntt);
        continue;
      }

      // A shader material can place its vertices wherever its own code says, so neither is its box the
      // geometry on screen. The default and the PBR materials are the ones this test is sound for.
      bool customShaded = false;
      for (uint i = slot.begin; i < slot.end && !customShaded; ++i)
      {
        Material* material = m_cachedJobs[i].Material;
        customShaded       = material == nullptr || material->IsShaderMaterial();
      }

      const BoundingBox& box = ntt->GetBoundingBox(true);
      if (customShaded || !box.IsValid() || !IsOccludedByGrid(box))
      {
        slot.occlusionStreak = 0;
        kept.push_back(ntt);
        continue;
      }

      hidden++;

      // The grid is a frame behind, so a single frame that looks hidden is not enough to take a mesh
      // out of the frame. Only a volume that kept its whole footprint behind the drawn depths for
      // OcclusionStreakToCull frames in a row is dropped.
      if (++slot.occlusionStreak >= OcclusionStreakToCull)
      {
        culled++;
        continue;
      }

      kept.push_back(ntt);
    }

    entities.swap(kept);

    Stats::AddStat(FrameStatType::HiZCulledEntity, (uint64) culled);

    if (culled != m_loggedCulledEntityCount)
    {
      m_loggedCulledEntityCount = culled;

      TK_LOG("HiZ culling: %d of %d visible entities found hidden, %d of them dropped.",
             hidden,
             totalEntities,
             culled);
    }
  }

  void ForwardSceneRenderPath::PreRender(Renderer* renderer)
  {
    TK_PROFILE_FUNCTION();

    RenderPath::PreRender(renderer);

    SetPassParams(renderer);

    // Init / ReInit forward pre process.
    if (RequiresForwardPreProcessPass())
    {
      FramebufferSettings settings = m_params.MainFramebuffer->GetSettings();
      m_forwardPreProcessPass->InitBuffers(settings.width, settings.height, settings.msaaCount);
    }

    // Apply multi-sample resolve if needed.
    m_forwardRenderPass->m_params.resolveFrameBuffer = nullptr;
    if (m_params.MainFramebuffer->IsMultiSampled())
    {
      FramebufferSettings settings = m_params.MainFramebuffer->GetSettings();
      settings.msaaCount           = MsaaSampleCount::x0;

      m_resolvedFramebuffer->ReconstructIfNeeded(settings);
      m_forwardRenderPass->m_params.resolveFrameBuffer = m_resolvedFramebuffer;

      m_bloomPass->m_params.FrameBuffer                = m_resolvedFramebuffer;
      m_gammaTonemapFxaaPass->m_params.frameBuffer     = m_resolvedFramebuffer;
    }
  }

  void ForwardSceneRenderPath::PostRender(Renderer* renderer)
  {
    TK_PROFILE_FUNCTION();

    RenderPath::PostRender(renderer);
  }

  void ForwardSceneRenderPath::SetPassParams(Renderer* renderer)
  {
    TK_PROFILE_FUNCTION();

    Frustum frustum;
    EntityRawPtrArray entities;
    {
      TK_PROFILE_SCOPE("SetPassParams: cull");
      frustum  = ExtractFrustum(m_params.Cam->GetProjectViewMatrix(), false);
      entities = m_params.Scene->m_aabbTree.VolumeQuery(frustum);
    }

    if (m_params.grid != nullptr)
    {
      entities.push_back(m_params.grid.get());
    }

    LightRawPtrArray lights;
    if (m_params.overrideLights.empty())
    {
      // Select non culled scene lights.
      MoveByType(entities, lights);

      // A light that is being edited belongs in the frame even when the camera does not see it. The
      // list above is what the camera frustum returned, so a light edited off screen never reached the
      // GPU: its cache entry was never packed, and the new value only appeared once something else
      // happened to bring the light into view.
      for (Light* light : m_params.Scene->GetLights())
      {
        if (light != nullptr && light->IsCacheItemDirty() &&
            std::find(lights.begin(), lights.end(), light) == lights.end())
        {
          lights.push_back(light);
        }
      }

      // Collect directional lights.
      const LightRawPtrArray& directionalLights = m_params.Scene->GetDirectionalLights();
      renderer->SetDirectionalLights(directionalLights);
      m_forwardRenderPass->m_params.activeDirectionalLightCount = (int) directionalLights.size();
      for (Light* light : directionalLights)
      {
        lights.push_back(light);
      }

      // At this point directional lights may be added twice, due to ones coming from frustum cull.
      auto dirLightEndItr =
          std::partition(lights.begin(),
                         lights.end(),
                         [](Light* light) -> bool { return light->GetLightType() == Light::LightType::Directional; });

      // Make sure there is no duplicate for directionals.
      RemoveDuplicates(lights, lights.begin(), dirLightEndItr);
    }
    else
    {
      // or use override lights.
      for (LightPtr light : m_params.overrideLights)
      {
        lights.push_back(light.get());
      }

      LightRawPtrArray directionalLights                        = ToRawPtrArray(m_params.overrideLights);
      m_forwardRenderPass->m_params.activeDirectionalLightCount = (int) directionalLights.size();
      renderer->SetDirectionalLights(directionalLights);
    }

    int dirEndIndx = RenderJobProcessor::PreSortLights(lights);

    // Directional lights keep the front, everything else is ordered by id. The atlas hands out slots
    // in list order, so a list whose order comes out of the threaded scene query differently on two
    // frames makes the shadows jump between slots. The id order is stable for the lifetime of the
    // session, which is all the atlas needs.
    std::sort(lights.begin() + dirEndIndx,
              lights.end(),
              [](Light* a, Light* b) -> bool { return a->GetIdVal() < b->GetIdVal(); });

    // Lights the depth grid proves are behind what is already drawn: dropped from the frame entirely, so
    // the shadow pass neither gives them an atlas slot nor renders a map for them.
    CullOccludedLights(lights);

    // An edited light is packed for the GPU here, before the passes, instead of waiting for a draw
    // that happens to use it. The light buffers used to be refreshed only from the per draw path, so a
    // light edited while nothing it reaches was on screen kept the old value in the GPU buffer, and
    // coming into view did not correct it.
    renderer->UploadDirtyLights(lights);

    // A different set of lights means the assignment the objects carry was built from a list that no
    // longer exists. The editor switching between its own lights and the scene ones is the case that
    // shows it: every object keeps the list it was given in the other mode, so no light reaches it
    // until something unrelated happens to invalidate it.
    uint64 lightSetSignature = 1469598103934665603ull;
    for (Light* light : lights)
    {
      lightSetSignature =
          (lightSetSignature ^ uint64(reinterpret_cast<uintptr_t>(light))) * 1099511628211ull;
    }

    const bool lightSetChanged = lightSetSignature != m_lightSetSignature;
    m_lightSetSignature        = lightSetSignature;

    const EnvironmentComponentPtrArray& environments = m_params.Scene->GetEnvironmentVolumes();

    {
      TK_PROFILE_SCOPE("SetPassParams: create jobs");

      // Only structural changes rebuild the cache. Light assignment is not part of it: which lights
      // reach an object depends on where the lights are, so it is recomputed when they move below.
      //
      // A replaced material counts as structural even though the scene itself barely changed: a cached
      // job carries the material pointer it was built with, so without folding the mesh material revision
      // in, the substituted material shows in the editor while the objects keep drawing the old one.
      const uint64 version =
          TKMixHash(m_params.Scene->GetCacheVersion(), Material::GetAssignmentRevision());
      const bool rebuild = version != m_jobCacheVersion;
      bool rebuiltRanges = false;

      if (rebuild)
      {
        TK_PROFILE_SCOPE("SetPassParams: rebuild jobs");

        // Built from every drawable entity, not from the visible ones: the camera only decides
        // which of these survive the filter below, so the list can outlive a camera move.
        EntityRawPtrArray allEntities = ToEntityRawPtrArray(m_params.Scene->GetEntities());
        m_cachedJobs.clear();
        RenderJobProcessor::CreateRenderJobs(m_cachedJobs,
                                             allEntities,
                                             false,
                                             dirEndIndx,
                                             lights,
                                             environments);

        m_jobCacheVersion = version;

        // Entity to job range, so a frame only has to walk the entities the AABB tree reports
        // instead of testing every cached job against the frustum.
        m_jobRanges.clear();
        for (uint i = 0; i < (uint) m_cachedJobs.size();)
        {
          const Entity* owner = m_cachedJobs[i].Entity;
          uint end            = i + 1;
          while (end < (uint) m_cachedJobs.size() && m_cachedJobs[end].Entity == owner)
          {
            end++;
          }

          if (owner != nullptr)
          {
            m_jobRanges[owner] = {owner->GetSpatialGeneration(),
                                  owner->GetLightAssignmentGeneration(),
                                  i,
                                  end};
          }
          i = end;
        }

        // A skinned mesh deforms without its entity moving, and an animated one can be driven
        // without a spatial invalidation, so neither is something the two counters can see. They are
        // listed here and their ranges are rebuilt every frame, which keeps the cache useful for the
        // static surroundings of an animated character instead of turning it off for the whole scene.
        m_animatedJobEntities.clear();
        for (const RenderJob& job : m_cachedJobs)
        {
          if (job.Mesh->IsSkinned() || job.animData.currentAnimation != nullptr)
          {
            if (job.Entity != nullptr)
            {
              m_animatedJobEntities.insert(job.Entity);
            }
          }
        }
      }
      else
      {
        // The scene is unchanged structurally, but individual entities may have moved. Only their
        // job ranges are rebuilt, which is what keeps a scene with a few animated objects cached.
        m_dirtyEntities.clear();
        m_lightDirtyEntities.clear();
        for (const auto& [entity, slot] : m_jobRanges)
        {
          if (entity->GetSpatialGeneration() != slot.generation ||
              m_animatedJobEntities.find(entity) != m_animatedJobEntities.end())
          {
            m_dirtyEntities.push_back(const_cast<Entity*>(entity));
          }
          else if (lightSetChanged || entity->GetLightAssignmentGeneration() != slot.lightGeneration)
          {
            // A light moved over it, or the whole light set changed under it: its own transform and
            // mesh did not change, so only the light list of its jobs is refreshed.
            m_lightDirtyEntities.push_back(const_cast<Entity*>(entity));
          }
        }

        // A range that was rebuilt is an entity that moved, and moving geometry is what makes the grid
        // read by the next frame stale. The light only lists are not: a light does not change depth.
        rebuiltRanges = !m_dirtyEntities.empty();

        for (Entity* entity : m_lightDirtyEntities)
        {
          const auto slotItr = m_jobRanges.find(entity);
          if (slotItr == m_jobRanges.end() || slotItr->second.begin >= m_cachedJobs.size())
          {
            continue;
          }

          JobSlot& slot       = slotItr->second;
          RenderJob& firstJob = m_cachedJobs[slot.begin];
          firstJob.lights.clear();
          RenderJobProcessor::AssignLight(firstJob, lights, dirEndIndx);
          for (uint sub = slot.begin + 1; sub < slot.end; sub++)
          {
            m_cachedJobs[sub].lights = firstJob.lights;
          }

          slot.lightGeneration = entity->GetLightAssignmentGeneration();
        }

        if (!m_dirtyEntities.empty())
        {
          // A large share of the scene moving is cheaper as one rebuild than as a scatter of
          // range copies, and a submesh count change moves the layout anyway.
          const bool tooMany = m_dirtyEntities.size() * 4 > m_jobRanges.size();

          bool layoutChanged = tooMany;
          if (!tooMany)
          {
            TK_PROFILE_SCOPE("SetPassParams: rebuild dirty jobs");

            m_dirtyJobs.clear();
            RenderJobProcessor::CreateRenderJobs(m_dirtyJobs,
                                                 m_dirtyEntities,
                                                 false,
                                                 dirEndIndx,
                                                 lights,
                                                 environments);

            // CreateRenderJobs filters its input, so m_dirtyEntities now holds only the entities
            // that produced jobs, in the same order. Walk both and patch the ranges.
            uint job = 0;
            for (Entity* entity : m_dirtyEntities)
            {
              const auto slotItr = m_jobRanges.find(entity);
              if (slotItr == m_jobRanges.end())
              {
                continue;
              }

              JobSlot& slot      = slotItr->second;
              const uint expected = slot.end - slot.begin;
              uint count          = 0;
              while (job + count < (uint) m_dirtyJobs.size() && m_dirtyJobs[job + count].Entity == entity)
              {
                count++;
              }

              if (count != expected)
              {
                layoutChanged = true;
                break;
              }

              for (uint k = 0; k < count; k++)
              {
                m_cachedJobs[slot.begin + k] = m_dirtyJobs[job + k];
              }

              slot.generation = entity->GetSpatialGeneration();

              // The entity moved, or its geometry changed: whatever the grid proved about where it
              // used to be says nothing about it now, so the count starts over.
              slot.occlusionStreak = 0;

              job += count;
            }
          }

          if (layoutChanged)
          {
            // Fall back to a full rebuild by invalidating the version and recursing through the
            // next frame: simpler than duplicating the build here, and rare.
            m_jobCacheVersion = ~0ull;
          }
        }
      }

      // The next frame re-reads the grid instead of waiting for the read interval. The depth the grid
      // reduces is a frame old to begin with, and anything that moved since the last read - an entity
      // that just did, or the occluder in front of it - is a picture the CPU copy no longer describes.
      // Without this a mesh behind a wall that moved away stays dropped for the rest of the interval.
      m_sceneChangedLastFrame = rebuild || rebuiltRanges;

      m_renderData.jobs.clear();
      if (m_cachedJobs.empty())
      {
        RenderJobProcessor::CreateRenderJobs(m_renderData.jobs, entities, false, dirEndIndx, lights, environments);
      }
      else
      {
        // Entities the grid proves are behind what this frame can already show are taken out before
        // their jobs are copied, so an occluded entity costs neither a copied job nor a draw.
        CullOccludedEntities(entities);

        // The visible jobs are copied out of the cache. Two things make this affordable: the
        // visible entities come from the AABB tree query instead of a frustum test per job (which
        // measured four times slower for 6800 jobs), and the destination keeps its capacity,
        // including the per job light vectors, so no allocation happens after the first frames.
        if (m_renderData.jobs.capacity() < m_cachedJobs.size())
        {
          m_renderData.jobs.reserve(m_cachedJobs.size());
        }

        TK_PROFILE_SCOPE("SetPassParams: copy visible jobs");

        for (Entity* ntt : entities)
        {
          const auto range = m_jobRanges.find(ntt);
          if (range == m_jobRanges.end())
          {
            continue;
          }

          if (!ntt->IsVisible())
          {
            continue;
          }

          // Light and environment assignment needs no work here: an entity whose job range is
          // rebuilt runs through CreateRenderJobs, which reassigns both. A light that moves
          // invalidates the entities around it, so they arrive with a new generation.

          for (uint i = range->second.begin; i < range->second.end; i++)
          {
            const RenderJob& job = m_cachedJobs[i];
            const size_t index   = m_renderData.jobs.size();
            if (index < m_renderData.jobs.capacity())
            {
              m_renderData.jobs.resize(index + 1);
              m_renderData.jobs[index] = job;
            }
            else
            {
              m_renderData.jobs.push_back(job);
            }
          }
        }
      }
    }

    m_shadowPass->m_params.scene      = m_params.Scene;
    m_shadowPass->m_params.viewCamera = m_params.Cam;
    m_shadowPass->m_params.lights     = lights;

    // The shadow pass keeps its caster jobs between frames, so it has to hear about the frames where
    // an entity changed without the scene version moving: when the cache rebuilt some ranges (a
    // transform, a light that moved) or when it could not be used at all.
    m_shadowPass->m_params.castersDirty = !m_dirtyEntities.empty();

    {
      TK_PROFILE_SCOPE("SetPassParams: partition");
      RenderJobProcessor::SeperateRenderData(m_renderData, true);
      RenderJobProcessor::SortByMaterial(m_renderData);
    }

    // Set CubeMapPass for sky. The sky is re-read unconditionally: `m_sky` lives as long as the
    // render path, so leaving it untouched would keep the IBL of a sky that the scene no longer
    // has (scene switch, deleted Sky entity) alive and lighting everything.
    m_drawSky = false;
    m_sky     = m_params.Scene->GetSky();
    if (m_sky)
    {
      m_sky->Init();
      if (m_drawSky = m_sky->GetDrawSkyVal())
      {
        if (m_sky->IsReadyToRender())
        {
          m_skyPass->m_params.FrameBuffer = m_params.MainFramebuffer;
          m_skyPass->m_params.Cam         = m_params.Cam;
          m_skyPass->m_params.Transform   = m_sky->m_node->GetTransform();
          m_skyPass->m_params.Material    = m_sky->GetSkyboxMaterial();

          if (m_sky->IsA<GradientSky>())
          {
            GradientSky* gSky               = static_cast<GradientSky*>(m_sky.get());
            m_skyPass->m_params.onPreRender = [renderer, gSky, this]()
            {
              if (!renderer->m_gradientSkyboxBufferInitialized)
              {
                renderer->m_gradientSkyboxBuffer.Init(7);
                renderer->m_gradientSkyboxBufferInitialized = true;
              }
              renderer->m_gradientSkyboxBuffer.m_data.topColor    = Vec4(gSky->GetTopColorVal(), 1.0f);
              renderer->m_gradientSkyboxBuffer.m_data.middleColor = Vec4(gSky->GetMiddleColorVal(), 1.0f);
              renderer->m_gradientSkyboxBuffer.m_data.bottomColor = Vec4(gSky->GetBottomColorVal(), 1.0f);
              renderer->m_gradientSkyboxBuffer.m_data.exponentAndPad =
                  Vec4(gSky->GetGradientExponentVal(), 0.0f, 0.0f, 0.0f);
              renderer->m_gradientSkyboxBuffer.Invalidate();
              renderer->m_gradientSkyboxBuffer.Map();

              // CubeMapPass::Render now drives ApplyRequirements, so populating
              // customUbos[7] is enough - the descriptor set picks up our VkBuffer
              // without needing a manual BindUniformBuffer here.
              m_skyPass->GetRequirements().customUbos[7] = &renderer->m_gradientSkyboxBuffer.GetBuffer();
            };
          }
        }
        else
        {
          m_drawSky = false;
        }
      }
    }

    m_forwardRenderPass->m_params.renderData        = &m_renderData;
    m_forwardRenderPass->m_params.Cam               = m_params.Cam;
    m_forwardRenderPass->m_params.FrameBuffer       = m_params.MainFramebuffer;
    m_forwardRenderPass->m_params.clearBuffer       = GraphicBitFields::None;

    // The forward pass throws its depth away when it ends, which is the right default: nothing else was
    // reading it. The occlusion grid does, at the end of the frame, so it is kept while the test is on.
    m_forwardRenderPass->m_params.invalidateDepthBuffer = !GetEngineSettings().m_graphics->GetHiZCullingVal();

    PostProcessingSettingsPtr pps                   = m_params.postProcessSettings;
    m_forwardRenderPass->m_params.SsaoTexture       = pps->GetSSAOEnabledVal() ? m_ssaoPass->m_ssaoTexture : nullptr;

    bool forwardPreProcessExist                     = RequiresForwardPreProcessPass();
    m_forwardRenderPass->m_params.hasForwardPrePass = forwardPreProcessExist;

    if (m_drawSky) // Sky pass will clear frame buffer.
    {
      if (forwardPreProcessExist) // We need to keep depth buffer for early Z pass.
      {
        m_skyPass->m_params.clearBuffer = GraphicBitFields::ColorBits;
      }
      else // Otherwise clear all.
      {
        m_skyPass->m_params.clearBuffer = GraphicBitFields::AllBits;
      }
    }
    else // Forward pass will clear frame buffer.
    {
      if (forwardPreProcessExist)
      {
        m_forwardRenderPass->m_params.clearBuffer = GraphicBitFields::ColorBits;
      }
      else
      {
        m_forwardRenderPass->m_params.clearBuffer = GraphicBitFields::AllBits;
      }
    }

    m_forwardPreProcessPass->m_params       = m_forwardRenderPass->m_params;

    m_ssaoPass->m_params.GNormalDepthBuffer = m_forwardPreProcessPass->m_normalDepthRt;
    m_ssaoPass->m_params.Cam                = m_params.Cam;
    m_ssaoPass->m_params.Radius             = pps->GetSSAORadiusVal();
    m_ssaoPass->m_params.spread             = pps->GetSSAOSpreadVal();
    m_ssaoPass->m_params.Bias               = pps->GetSSAOBiasVal();
    m_ssaoPass->m_params.KernelSize         = pps->GetSSAOKernelSizeVal().GetValue<int>();
    m_ssaoPass->m_params.HalfResolution     = pps->GetSSAOHalfResolutionVal();

    m_bloomPass->m_params.FrameBuffer       = m_params.MainFramebuffer;
    m_bloomPass->m_params.intensity         = pps->GetBloomIntensityVal();
    m_bloomPass->m_params.minThreshold      = pps->GetBloomThresholdVal();
    m_bloomPass->m_params.iterationCount    = pps->GetBloomIterationCountVal();

    // DoF runs after the forward pass's MSAA resolve, sitting on the same single-sample chain
    // as Bloom and GammaTonemap. Feeding it the MSAA color RT directly would (a) trip
    // VUID-RuntimeSpirv-samples-08725 (sampler2D fed an MSAA image) and (b) leave its output on
    // the MSAA surface while subsequent passes read from the resolved one - i.e. DoF's effect
    // would silently drop on the floor.
    FramebufferPtr dofSourceFb =
        m_params.MainFramebuffer->IsMultiSampled() ? m_resolvedFramebuffer : m_params.MainFramebuffer;
    m_dofPass->m_params.ColorRt     = dofSourceFb->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);

    m_dofPass->m_params.DepthRt     = m_forwardPreProcessPass->m_normalDepthRt;
    m_dofPass->m_params.focusPoint  = pps->GetFocusPointVal();
    m_dofPass->m_params.focusScale  = pps->GetFocusScaleVal();
    m_dofPass->m_params.blurQuality = pps->ParamDofBlurQuality().GetEnum<DoFQuality>();

    // SSR composites into the same single sample color chain DoF and Bloom read from, and marches
    // the rays against the pre process g buffer (normal + linear depth + roughness).
    m_ssrPass->m_params.ColorRt              = dofSourceFb->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
    m_ssrPass->m_params.GNormalDepthBuffer   = m_forwardPreProcessPass->m_normalDepthRt;
    m_ssrPass->m_params.Cam                  = m_params.Cam;
    m_ssrPass->m_params.Intensity            = pps->GetSSRIntensityVal();
    m_ssrPass->m_params.MaxDistance          = pps->GetSSRMaxDistanceVal();
    m_ssrPass->m_params.Thickness            = pps->GetSSRThicknessVal();
    m_ssrPass->m_params.StepCount            = pps->GetSSRStepCountVal();
    m_ssrPass->m_params.RoughnessCutoff      = pps->GetSSRRoughnessCutoffVal();
    m_ssrPass->m_params.DebugView            = pps->GetSSRDebugViewVal();
    m_ssrPass->m_params.DebugViewMode        = pps->GetSSRDebugViewModeVal();

    // Post Process Pass
    bool gammaNeeded                = GetRenderSystem()->IsGammaCorrectionNeeded();
    m_gammaTonemapFxaaPass->m_params.enableGammaCorrection = pps->GetGammaCorrectionEnabledVal() && gammaNeeded;
    m_gammaTonemapFxaaPass->m_params.enableFxaa            = pps->GetFXAAEnabledVal();
    m_gammaTonemapFxaaPass->m_params.enableTonemapping     = pps->GetTonemappingEnabledVal();
    // GammaTonemapFxaa writes the final composited color. When MSAA is on, the rest of the
    // post chain (Bloom, DoF) operates on the resolved (single-sample) buffer, so this pass
    // must read from MainFramebuffer (where the forward pass wrote) and write to the
    // resolved framebuffer that the editor will copy back. Don't override to MainFramebuffer
    // when MSAA is on - that would clobber the resolve and leave editor with pre-tonemap colors.
    m_gammaTonemapFxaaPass->m_params.frameBuffer           = m_params.MainFramebuffer;
    m_gammaTonemapFxaaPass->m_params.tonemapMethod         = pps->GetTonemapperModeVal().GetEnum<TonemapMethod>();
    m_gammaTonemapFxaaPass->m_params.gamma                 = pps->GetGammaVal();

    FramebufferSettings fbs                                = m_params.MainFramebuffer->GetSettings();
    m_gammaTonemapFxaaPass->m_params.screenSize            = Vec2(fbs.width, fbs.height);

    // Final override: when MSAA is on, the post-process chain (Bloom + GammaTonemapFxaa) must
    // operate on the resolved single-sample framebuffer, NOT the MSAA one. The earlier
    // `m_params.MainFramebuffer` assignments below are the source-of-truth defaults; the
    // MSAA branch in PreRender sets the resolve target, and we re-apply it here so the
    // later assignments don't clobber it.
    if (m_params.MainFramebuffer->IsMultiSampled())
    {
      m_bloomPass->m_params.FrameBuffer            = m_resolvedFramebuffer;
      m_gammaTonemapFxaaPass->m_params.frameBuffer = m_resolvedFramebuffer;
    }
  }

  bool ForwardSceneRenderPath::RequiresForwardPreProcessPass()
  {
    bool ssaoEnabled = m_params.postProcessSettings->GetSSAOEnabledVal();
    bool dofEnabled  = m_params.postProcessSettings->GetDepthOfFieldEnabledVal();
    bool ssrEnabled  = m_params.postProcessSettings->GetSSREnabledVal();
    return ssaoEnabled || dofEnabled || ssrEnabled;
  }

} // namespace ToolKit

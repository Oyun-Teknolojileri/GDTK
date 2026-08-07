/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "EnvironmentComponent.h"
#include "RenderState.h"
#include "Renderer.h"
#include "UniformBuffer.h"

#include <unordered_map>

namespace ToolKit
{

  typedef std::shared_ptr<class Pass> PassPtr;
  typedef std::vector<PassPtr> PassPtrArray;

  /**
   * Declarative description of every piece of GPU state a pass needs before its draw call.
   * Passes populate this struct in Setup() (or end of PreRender) and Renderer::ApplyRequirements
   * binds everything in the correct order:
   *   1. shader defines / fragment shader
   *   2. program create + bind
   *   3. framebuffer + clear
   *   4. pass RenderState
   *   5. custom UBOs (pass-specific UBOs)
   *   6. textures — semantic names are resolved to slots AFTER the program is bound, so
   *      "program not yet bound" silent no-ops become impossible.
   *
   * Render() itself becomes a pure draw call. No texture/program/state mutation inside.
   */
  struct TK_API PassRequirements
  {
    /** Optional: a specific fragment shader. If null, the program's existing fragment is used. */
    ShaderPtr fragmentShader = nullptr;
    /** Optional: a specific vertex shader. If null, the program's existing vertex is used
     *  (and if no program is provided either, the default fullQuadVert.shader is used). */
    ShaderPtr vertexShader   = nullptr;
    /** Optional: a pre-built program. If null, the manager creates one from the pass's
     *  vertex + fragment shaders. */
    GpuProgramPtr program    = nullptr;

    /** Slot → texture bindings. Use this when you know the slot number directly. */
    std::unordered_map<int, TexturePtr> textures;
    /** Semantic name (e.g. "s_normalDepth") → texture. Resolved to slot after program bind.
     *  Use this for new code — it's the safe path that makes the SSAO bug class extinct. */
    std::unordered_map<String, TexturePtr> semanticTextures;

    /** Slot → uniform buffer. Pass-specific UBOs (DoF, Bloom, SSAO, GaussBlur) go here. */
    std::unordered_map<int, UniformBuffer*> customUbos;

    /** Shader defines applied to the fragment shader before program creation. */
    std::unordered_map<String, String> defines;

    /** Passive pipeline state (depth test/write, depth func, stencil, color mask, etc.). */
    RenderState passState;

    /** Framebuffer / viewport / clear. */
    FramebufferPtr frameBuffer = nullptr;
    GraphicBitFields clearBits = GraphicBitFields::None;
    Vec4 clearColor            = Vec4(0.0f);

    /** Optional: scissor rect. If scissorEnabled, SetScissor is called. */
    bool scissorEnabled        = false;
    UVec4 scissor              = UVec4(0, 0, 0, 0);
  };

  /** Base Pass class. */
  class TK_API Pass
  {
   public:
    Pass(StringView name);
    virtual ~Pass();

    /**
     * Pure draw call. Subclasses should NOT mutate GPU state here — populate
     * m_requirements in Setup()/PreRender() and let ApplyRequirements do the binding.
     */
    virtual void Render() = 0;

    /** CPU-side prep. Subclasses do most work here: build framebuffers, compute UBOs,
     *  decide what textures/UBOs to bind, set defines, etc. Ends by populating m_requirements. */
    virtual void PreRender();

    /** Cleanup. */
    virtual void PostRender();

    /** Run a sub-pass. Same PreRender → Render → PostRender lifecycle, with this pass as renderer owner. */
    void RenderSubPass(const PassPtr& pass);

    /** Apply m_requirements to the renderer. Called automatically by RenderPath::Render just
     *  before the pass's Render() runs. Subclasses can call it manually if they need finer
     *  control (e.g. SSAO running calc + blur with different state between draws). */
    void ApplyRequirements(Renderer* renderer);

    /** Subclass hook: extend m_requirements with whatever it needs. Default impl is in Pass.cpp. */
    virtual void GatherRequirements(PassRequirements& reqs);

    /** Public accessor for the requirements. Lets a parent pass inject custom state into
     *  a sub-pass without exposing internals. */
    PassRequirements& GetRequirements() { return m_requirements; }

    const PassRequirements& GetRequirements() const { return m_requirements; }

    /** Public accessor for m_program. SetFragmentShader assigns it; outer passes that drive
     *  a sub-pass's draw can read it to fill in PassRequirements::program explicitly. */
    GpuProgramPtr GetProgram() const { return m_program; }

    void SetProgram(const GpuProgramPtr& prog) { m_program = prog; }

    Renderer* GetRenderer();
    void SetRenderer(Renderer* renderer);

    /** Label that appears in the gpu profile / debug applications(RenderDoc etc...). */
    void SetName(StringView name) { m_name = name; }

   protected:
    GpuProgramPtr m_program = nullptr; //!< Program used to draw objects with in the pass.
    StringView m_name; //!< Label that appears in the gpu profile / debug applications (RenderDoc etc...).

    /** Declarative description of what this pass needs at draw time. Populated in
     *  PreRender()/Setup() and consumed by ApplyRequirements. */
    PassRequirements m_requirements;

   private:
    Renderer* m_renderer = nullptr;
  };

  /** This struct holds all the data required to make a drawcall. */
  struct RenderJob
  {
    Entity* Entity                                   = nullptr; //!< Entity that this job is created from.
    Mesh* Mesh                                       = nullptr; //!< Mesh to render.
    Material* Material                               = nullptr; //!< Material to render job with.
    EnvironmentComponent* EnvironmentVolume          = nullptr; //!< EnvironmentVolume effecting this entity, if any.
    EnvironmentComponent* SecondaryEnvironmentVolume = nullptr; //!< Secondary env volume for IBL blending.
    bool ShadowCaster                                = true;    //!< Account in shadow map construction.
    bool frustumCulled                               = false;   //!< States that the job is culled by a camera.
    bool requireCullFlip = false; //!< Negative determinant in transform requires cull side flip.

    BoundingBox BoundingBox; //!< World space bounding box.
    Mat4 WorldTransform;     //!< World transform of the entity.
    AnimData animData;       //!< Animation data of render job.

    LightRawPtrArray lights;
  };

  typedef RenderJobArray::iterator RenderJobItr;

  /**
   * Singular render data that contains all the rendering information for a frame.
   * When first culled than separated by a render job processor, the indexes become valid.
   * Partition structure
   * 0 Culled                 : jobs.begin to deferredJobsStartIndex
   * 1 Deferred Opaque        : deferredJobsStartIndex to deferredAlphaMaskedJobsStartIndex
   * 2 Deferred Alpha Masked  : deferredAlphaMaskedJobsStartIndex to forwardOpaqueStartIndex
   * 3 Forward Opaque         : forwardOpaqueStartIndex to forwardAlphaMaskedJobsStartIndex
   * 4 Forward Alpha Masked   : forwardAlphaMaskedJobsStartIndex to forwardTranslucentStartIndex
   * 5 Forward Translucent    : forwardTranslucentStartIndex to jobs.end
   */
  struct RenderData
  {
    RenderJobArray jobs;

    int deferredJobsStartIndex            = 0; //!< Beginning of deferred jobs. Before this, culled jobs resides.
    int deferredAlphaMaskedJobsStartIndex = 0; //<! Beginning of deferred render alpha masked jobs.
    int forwardOpaqueStartIndex           = 0; //!< Beginning of forward opaque jobs.
    int forwardAlphaMaskedJobsStartIndex  = 0; //!< Beginning of forward render alpha masked jobs.
    int forwardTranslucentStartIndex      = 0; //!< Beginning of forward translucent jobs.

    RenderJobItr GetDefferedBegin()
    {
      assert(deferredJobsStartIndex != -1 && "Accessing forward only data.");
      return jobs.begin() + deferredJobsStartIndex;
    }

    RenderJobItr GetForwardOpaqueBegin() { return jobs.begin() + forwardOpaqueStartIndex; }

    RenderJobItr GetForwardTranslucentBegin() { return jobs.begin() + forwardTranslucentStartIndex; }

    RenderJobItr GetDeferredAlphaMaskedBegin() { return jobs.begin() + deferredAlphaMaskedJobsStartIndex; }

    RenderJobItr GetForwardAlphaMaskedBegin() { return jobs.begin() + forwardAlphaMaskedJobsStartIndex; }
  };

  class TK_API RenderJobProcessor
  {
   public:
    /**
     * Constructs all render jobs from entities.
     * @param jobArray is the array of constructed jobs.
     * @param entities are the entities to construct render jobs for.
     * @param lights are the list of lights to consider. Lights must be presorted before sending them to this function.
     * @param environments are the environment volumes to consider.
     * @param ingnoreVisibility when set true, construct jobs for entities that has visibility set to false.
     */
    static void CreateRenderJobs(RenderJobArray& jobArray,
                                 EntityRawPtrArray& entities,
                                 bool ignoreVisibility                            = false,
                                 int dirLightEndIndex                             = 0,
                                 const LightRawPtrArray& lights                   = {},
                                 const EnvironmentComponentPtrArray& environments = {});

    static void CreateRenderJobs(RenderJobArray& jobArray, EntityPtr entity);

    /**
     * Separate jobs such that job array starts with culled jobs, than deferred jobs, than forward opaque and
     * translucent jobs.
     * For example, all jobs between these iterators are the deferred jobs.
     * RenderData::GetDefferedBegin() and RenderData::GetForwardOpaqueBegin()
     */
    static void SeperateRenderData(RenderData& renderData, bool forwardOnly);

    /** Assign all lights affecting the job. */
    static void AssignLight(RenderJob& job, const LightRawPtrArray& lights, int startIndex);

    /** Assign environment to each job. If job is under influence of many environment, picks the smallest volume. */
    static void AssignEnvironment(RenderJob& job, const EnvironmentComponentPtrArray& environments);

    /**
     * Makes sure that first elements are directional lights.
     * @param lights are the lights to sort.
     * @returns The index where the non directional lights starts.
     */
    static int PreSortLights(LightRawPtrArray& lights);

    /** Sort entities by distance(from boundary center) in ascending order to camera. Accounts for isometric camera. */
    static void SortByDistanceToCamera(RenderJobItr begin, RenderJobItr end, const CameraPtr& cam);

    /** Sort render jobs based on materials. */
    static void SortByMaterial(RenderData& renderData);

    /**
     * Calculates the standard deviation and mean of the given RenderJobArray
     * based on world position of the RenderJobs.
     * @param rjVec is array containing jobs.
     * @param stdev is the output of calculated standard deviation.
     * @param mean is the calculated mean position.
     */
    static void CalculateStdev(const RenderJobArray& rjVec, float& stdev, Vec3& mean);

    /**
     * Decides if the given RenderJob is an outlier based on its world position.
     * @param rj is the RenderJob to decide if its outlier.
     * @param sigma is the threshold sigma to accept as outlier or not.
     */
    static bool IsOutlier(const RenderJob& rj, float sigma, const float stdev, const Vec3& mean);
  };

} // namespace ToolKit

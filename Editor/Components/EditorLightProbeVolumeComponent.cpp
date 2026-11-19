/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "EditorLightProbeVolumeComponent.h"

#include <Entity.h>
#include <LightProbe.h>
#include <Scene.h>
#include <Threads.h>
#include <ToolKit.h>

#include <DebugNew.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>

namespace ToolKit
{
  namespace Editor
  {
    namespace
    {
      /** printf into a String, for the one line status the inspector shows. */
      String Format(const char* format, ...)
      {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return String(buffer);
      }
    } // namespace

    TKDefineClass(EditorLightProbeVolumeComponent, LightProbeVolumeComponent);

    EditorLightProbeVolumeComponent::EditorLightProbeVolumeComponent() {}

    EditorLightProbeVolumeComponent::~EditorLightProbeVolumeComponent()
    {
      // The bake thread holds a Scene pointer and writes the status the destructor is about to
      // free, so it has to be stopped and joined here rather than left to finish on its own.
      m_cancel = true;
      if (m_bakeThread.joinable())
      {
        m_bakeThread.join();
      }
    }

    ComponentPtr EditorLightProbeVolumeComponent::Copy(EntityPtr ntt) { return Super::Copy(ntt); }

    void EditorLightProbeVolumeComponent::ParameterConstructor()
    {
      Super::ParameterConstructor();

      std::weak_ptr<EditorLightProbeVolumeComponent> weakSelf =
          Self<EditorLightProbeVolumeComponent>();

      Bake_Define(
          [weakSelf]() -> void
          {
            if (EditorLightProbeVolumeComponentPtr self = weakSelf.lock())
            {
              self->RequestBake();
            }
          },
          LightProbeVolumeCategory.Name,
          LightProbeVolumeCategory.Priority,
          true,
          true);
    }

    void EditorLightProbeVolumeComponent::RequestBake()
    {
      if (m_baking.load())
      {
        return;
      }

      EntityPtr owner = OwnerEntity();
      if (owner == nullptr)
      {
        return;
      }

      ScenePtr scene = owner->m_scene.lock();
      if (scene == nullptr)
      {
        return;
      }

      // The bake traces world space radiance, so it gets the world box. A rotated node is the one
      // case this does not represent exactly: the grid is traced axis aligned around the world
      // bounds while the shader maps it back through the node's inverse transform.
      const BoundingBox box       = GetBoundingBox();
      const UVec3 resolution      = ComputeBakeResolution();
      const String relativeFile   = ResolveBakeFile();
      const bool absoluteBakeFile = std::filesystem::path(relativeFile).is_absolute();
      const String absoluteFile =
          absoluteBakeFile ? relativeFile : ConcatPaths({ResourcePath(), relativeFile});

      LightProbeBake::BakeRequest request;
      request.scene      = scene.get();
      request.boxMin     = Vec3(box.min);
      request.boxMax     = Vec3(box.max);
      request.resolution = resolution;
      request.samples    = GetBakeSamplesVal();
      request.bounces    = GetBakeBouncesVal();
      request.seed       = (uint) glm::max(GetBakeSeedVal(), 0);
      request.denoiseIterations = glm::max(GetBakeDenoiseVal(), 0);

      // L1: the runtime reads four coefficients, and a quadratic bake measured no visible
      // difference on top of them for a 2.25x larger file.
      request.quality    = ProbeVolumeQuality::L1;
      request.cancel     = &m_cancel;
      request.onProgress = [this](float progress) -> void { m_progress = progress; };

      // The snapshot is taken here, on the frame thread: BuildBakeScene reads engine objects, and
      // doing that from the bake thread raced the frame loop - the coefficients came back as NaN,
      // which the shader interpolates across the whole volume and the GI disappears. Only the
      // tracing runs on the thread.
      auto snapshot = std::make_shared<LightProbeBake::BakeScene>();
      String snapshotError;
      if (!LightProbeBake::BuildBakeScene(scene.get(), *snapshot, snapshotError))
      {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status = "Bake failed: " + snapshotError;
        return;
      }

      {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status = Format("Baking %ux%ux%u probes, %d samples, %d bounces...",
                                resolution.x,
                                resolution.y,
                                resolution.z,
                                request.samples,
                                request.bounces);
      }

      m_progress = 0.0f;
      m_cancel   = false;
      m_baking   = true;

      // Joined by Update on the main thread, or by the destructor when the component dies first.
      m_bakeThread = std::thread(
          [this, snapshot, request, absoluteFile, resolution]() -> void
          {
            LightProbeBake::BakeResult result;
            const bool baked = LightProbeBake::TraceBakeScene(*snapshot, request, result);

            bool saved = false;
            if (baked)
            {
              saved = SaveProbeVolume(absoluteFile, result.data);
            }

            std::lock_guard<std::mutex> lock(m_statusMutex);

            if (m_cancel.load())
            {
              m_status = "Bake cancelled.";
            }
            else if (!baked)
            {
              m_status = "Bake failed: " + result.error;
            }
            else if (!saved)
            {
              m_status = "Bake finished but " + absoluteFile + " could not be written.";
            }
            else
            {
              m_status = Format("Baked %ux%ux%u probes in %.1f s (%u buried).",
                                      resolution.x,
                                      resolution.y,
                                      resolution.z,
                                      result.seconds,
                                      result.buriedProbes);
            }

            m_bakeSucceeded = baked && saved;
            m_baking        = false;
          });

    }

    void EditorLightProbeVolumeComponent::RequestCancelBake()
    {
      if (m_baking.load())
      {
        m_cancel = true;
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status = "Cancelling, stopping after the probe in flight...";
      }
    }

    String EditorLightProbeVolumeComponent::GetBakeStatus() const
    {
      std::lock_guard<std::mutex> lock(m_statusMutex);
      return m_status;
    }

    void EditorLightProbeVolumeComponent::Update()
    {
      // The entity mirrors these values into its own parameter group; a bake writes BakeFile and
      // the box edit tool writes Size and PositionOffset straight onto the component.
      if (EntityPtr owner = OwnerEntity())
      {
        if (LightProbe* probe = owner->As<LightProbe>())
        {
          probe->SyncFromComponent();
        }
      }

      if (m_baking.load())
      {
        return;
      }

      if (!m_bakeThread.joinable())
      {
        // The thread has not finished yet, or there is no bake to finish.
        return;
      }

      m_bakeThread.join();

      if (!m_bakeSucceeded)
      {
        return;
      }

      // Setting the path is what a hand authored volume does, and it runs the component's own
      // reload path. A re-bake of the same file does not change the value, and the setter would
      // not fire, so the load is called directly in that case - once, either way.
      const String bakeFile = ResolveBakeFile();
      if (GetBakeFileVal() != bakeFile)
      {
        SetBakeFileVal(bakeFile);
      }
      else
      {
        LoadBake();
      }
    }

  } // namespace Editor
} // namespace ToolKit

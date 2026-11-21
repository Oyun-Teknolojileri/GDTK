/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file EditorLightProbeVolumeComponent.h
 *
 * The editor half of a light probe volume: a Bake button that runs the CPU path tracer off the
 * frame thread, reports progress, and can be cancelled.
 *
 * The engine component stays data and upload only. The bake itself lives in
 * `Modules/LightProbeBake`, which the engine must not depend on, so the button and the worker
 * thread live here and the editor registers this subclass over the engine type.
 */

#include <LightProbeVolumeComponent.h>

#include <LightProbeBake.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace ToolKit
{
  namespace Editor
  {
    class TK_EDITOR_API EditorLightProbeVolumeComponent : public LightProbeVolumeComponent
    {
     public:
      TKDeclareClass(EditorLightProbeVolumeComponent, LightProbeVolumeComponent);

      EditorLightProbeVolumeComponent();
      ~EditorLightProbeVolumeComponent() override;

      ComponentPtr Copy(EntityPtr ntt) override;

      /** Per frame tick from Scene::Update: joins a finished bake and loads its result. */
      void Update() override;

      /**
       * Starts a bake on its own thread. No-op while a bake runs or when the component has no
       * scene. The snapshot of the scene is taken on that thread too, so the scene must not be
       * edited while a bake runs; the viewport keeps rendering beside it.
       */
      void RequestBake() override;

      /** Asks the running bake to stop. It stops after the probe in flight. */
      void RequestCancelBake() override;

      bool IsBaking() const override { return m_baking.load(); }

      /** Fractions of the probes traced, 0 to 1. */
      float GetBakeProgress() const override { return m_progress.load(); }

      /** One line the inspector shows under the button. */
      String GetBakeStatus() const override;

     protected:
      void ParameterConstructor() override;

     private:
      void FinishBake();

      std::thread m_bakeThread;
      mutable std::mutex m_statusMutex;

      std::atomic<bool> m_baking {false};
      std::atomic<bool> m_cancel {false};
      std::atomic<float> m_progress {0.0f};

      // Written by the worker, read on the main thread once m_baking turns false.
      bool m_bakeSucceeded   = false;
      String m_bakeFile      = "";
      String m_status        = "Not baked.";

     public:
      TKDeclareParam(VariantCallback, Bake);

      /**
       * Draws the volume's sample points in the viewport.
       *
       * An editor view toggle, so it lives on the editor half and not on the engine component or
       * the entity: nothing in the renderer or the bake reads it, and a shipped game has no
       * viewport to draw it in.
       */
      TKDeclareParam(bool, VisualiseProbe);

      /** Radius of one sample point marker, in world units. Only the view above reads it. */
      TKDeclareParam(float, VisualiseProbeSize);
    };

    typedef std::shared_ptr<EditorLightProbeVolumeComponent> EditorLightProbeVolumeComponentPtr;
  } // namespace Editor
} // namespace ToolKit

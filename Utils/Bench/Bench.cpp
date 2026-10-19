/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

/**
 * @file Bench.cpp
 *
 * Deterministic render benchmark and visual regression host.
 *
 * The tool exists to serve render-cpu-optimization-plan.md: every render stage
 * that touches the draw path has to prove, with numbers and images, that it made
 * things faster and did not change the picture.
 *
 * What it guarantees:
 *  - The test scene is built in code, so the workload is identical on every run.
 *  - The camera path is a pure function of the frame index, never of wall clock.
 *  - Vsync is off and the frame count is fixed, so the CPU numbers are stable.
 *  - The final composited image is read back at fixed frames, written as PNG and
 *    compared against a stored baseline.
 *
 * The scene is built to exercise every feature the render path must keep working:
 * global sky IBL, two local environment volumes with an IBL blend, screen space
 * reflections (smooth metal), ambient occlusion (a tight cluster of boxes),
 * tonemapping, gamma correction and MSAA.
 */

#include "Camera.h"
#include "Common/PlatformHelper.h"
#include "EngineSettings.h"
#include "EnvironmentComponent.h"
#include "FileManager.h"
#include "Framebuffer.h"
#include "GameRenderer.h"
#include "GameViewport.h"
#include "Image.h"
#include "Light.h"
#include "Material.h"
#include "Platform.h"
#include "Primative.h"
#include "RenderSystem.h"
#include "Scene.h"
#include "Sky.h"
#include "Stats.h"
#include "Texture.h"
#include "ToolKit.h"
#include "Util.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ToolKit
{
  namespace Bench
  {
    namespace
    {
      // Channels of the read back images and of the CSV-independent PNG output.
      const int g_channels            = 4;

      // Number of pre-roll frames that are rendered but not measured. The first
      // frames pay for shader compilation, pipeline creation and resource upload.
      const int g_defaultWarmup       = 20;

      // Mean absolute per channel difference, in 0-255 units, under which two
      // captures are considered the same image.
      //
      // Not 1.0: the engine does not produce a bit identical frame from run to run
      // yet. Repeated identical runs land on a small set of discrete images whose
      // diffs against each other are 0.0002, 4.5 and 15.7 while their draw call
      // counts are exactly equal, so the variance is in a resource built once per
      // process (the environment bake task) or in a driver shader variant, not in
      // the work being measured. 20 clears that noise band and still trips on a
      // gross break such as a missing pass or a black frame. Lower it once the
      // variance source is found; see render-cpu-optimization-plan.md.
      const float g_imageTolerance    = 20.0f;

      // ---------------------------------------------------------------------
      // Options
      // ---------------------------------------------------------------------

      struct Options
      {
        int frames       = 120;              //!< Measured frames.
        int warmup       = g_defaultWarmup;  //!< Unmeasured pre-roll frames.
        int width        = 1280;
        int height       = 720;

        // Scene composition. The default is a dense prop field, because the draw
        // path is what is being measured: a hundred props cannot show a bottleneck.
        int grid         = 45;   //!< grid x grid visible props (2025 at the default).
        float spacing    = 2.0f; //!< Distance between props, in world units.
        int cluster      = 3;    //!< 3D cluster edge for the ambient occlusion test.

        int envVolumes   = 2;    //!< Local environment volumes.
        int pointLights  = 3;
        bool spotShadow  = true; //!< Adds a shadow casting spot light next to the sun.
        bool useTextures = true; //!< Gives some materials a diffuse texture.

        // Frames whose image is read back and compared. Independent of --frames so a
        // shorter run still compares the same reference images.
        std::vector<int> captureFrames = {40, 80, 120};

        bool msaa        = true;
        bool ssao        = true;
        bool ssr         = true;
        bool tonemap     = true;
        bool gamma       = true; //!< Always on in the game path, see ApplySettings.
        bool hidden      = false;
        bool capture     = true;
        bool compare     = true;
        float tolerance  = g_imageTolerance;

        // Reference images live OUTSIDE the repository: they are large binaries and
        // they change whenever a stage legitimately changes the image, so keeping
        // them in git only grows history. The root comes from --reference-root, then
        // from Bench/bench.txt, then from a sibling folder next to the repository,
        // and Bench/Reference.txt in the repo records what was approved.
        String referenceRoot;
        bool updateReference = false;

        // Optional downscale of the stored reference, as a fraction of the capture
        // size. 1.0 (the default) keeps the reference at full fidelity; lower it only
        // if the reference folder has to stay small.
        float referenceScale = 1.0f;

        // Where the per-run statistics go. This one IS versioned: a few hundred bytes
        // per run, and the performance history is worth keeping with the code.
        String resultsDir = "Bench/Results";

        // The engine profiler costs an rdtsc pair per scope, and the draw path has a
        // scope per pass and per draw, so it stays off while timings are measured and
        // is switched on only to explain where the time went.
        bool profile     = false;
        String tag       = "run";
        String outDir    = "Bench/Out";
        String baseline; //!< Deprecated alias for --reference-root, resolved at startup.

        // Baselines are keyed per configuration set, not only per frame: a probe run
        // with a different grid or resolution must not overwrite the reference.
        String baselineSet = "default";
      };

      struct FrameStats
      {
        int frame      = 0;
        double frameMs = 0.0; //!< Whole frame, CPU side.
        double renderMs = 0.0; //!< Render task only, CPU side.
        uint64 drawCalls = 0;
      };

      // ---------------------------------------------------------------------
      // Small helpers
      // ---------------------------------------------------------------------

      double NowMs()
      {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
      }

      void PrintUsage()
      {
        printf("Bench - deterministic render benchmark\n"
               "  --frames N      measured frames (default 120)\n"
               "  --warmup N      unmeasured pre-roll frames (default 20)\n"
               "  --grid N        visible prop grid edge, N*N props (default 45)\n"
               "  --spacing F     distance between props (default 1.5)\n"
               "  --cluster N     ambient occlusion cluster edge (default 3)\n"

               "  --env N         local environment volumes (default 2)\n"
               "  --lights N      point lights (default 3)\n"
               "  --no-spot       drop the shadow casting spot light\n"
               "  --no-textures   leave every material untextured\n"
               "  --capture-frames A,B,C   frames to read back (default 40,80,120)\n"
               "  --width N --height N     capture size (default 1280x720)\n"
               "  --tag NAME      output file prefix (default run)\n"
               "  --out DIR       output directory (default Bench/Out)\n"
               "  --baseline DIR  baseline directory (default Bench/Baseline)\n"
               "  --baseline-set NAME      baseline configuration set (default default)\n"
               "  --no-msaa --no-ssao --no-ssr --no-tonemap\n"
               "  --hidden        do not show the window\n"
               "  --no-capture    skip the image capture\n"
               "  --tolerance F   image difference that still counts as a match (default 20)\n"
               "  --reference-root DIR     where reference images live (default from Bench/bench.txt)\n"
               "  --reference-scale F      reference image scale, 1.0 = full capture (default 1)\n"
               "  --update-reference       create / refresh the reference for this baseline set\n"
               "  --results DIR   where the run statistics go (default Bench/Results)\n"
               "  --profile       enable the engine profiler and dump its tree at the end\n"
               "  --no-compare    write the capture but do not compare it\n");
      }

      bool ParseArgs(int argc, char* argv[], Options& opts)
      {
        for (int i = 1; i < argc; i++)
        {
          const String arg = argv[i];
          auto nextValue   = [&](int& out) -> bool
          {
            if (i + 1 >= argc)
            {
              return false;
            }
            out = std::atoi(argv[++i]);
            return true;
          };
          auto nextString  = [&](String& out) -> bool
          {
            if (i + 1 >= argc)
            {
              return false;
            }
            out = argv[++i];
            return true;
          };

          if (arg == "--frames")
          {
            nextValue(opts.frames);
          }
          else if (arg == "--warmup")
          {
            nextValue(opts.warmup);
          }
          else if (arg == "--grid")
          {
            nextValue(opts.grid);
          }
          else if (arg == "--spacing")
          {
            String value;
            if (nextString(value))
            {
              opts.spacing = (float) std::atof(value.c_str());
            }
          }

          else if (arg == "--cluster")
          {
            nextValue(opts.cluster);
          }
          else if (arg == "--env")
          {
            nextValue(opts.envVolumes);
          }
          else if (arg == "--lights")
          {
            nextValue(opts.pointLights);
          }
          else if (arg == "--width")
          {
            nextValue(opts.width);
          }
          else if (arg == "--height")
          {
            nextValue(opts.height);
          }
          else if (arg == "--tag")
          {
            nextString(opts.tag);
          }
          else if (arg == "--out")
          {
            nextString(opts.outDir);
          }
          else if (arg == "--baseline")
          {
            nextString(opts.baseline);
          }
          else if (arg == "--baseline-set")
          {
            nextString(opts.baselineSet);
          }
          else if (arg == "--no-msaa")
          {
            opts.msaa = false;
          }
          else if (arg == "--no-ssao")
          {
            opts.ssao = false;
          }
          else if (arg == "--no-ssr")
          {
            opts.ssr = false;
          }
          else if (arg == "--no-tonemap")
          {
            opts.tonemap = false;
          }
          else if (arg == "--no-spot")
          {
            opts.spotShadow = false;
          }
          else if (arg == "--no-textures")
          {
            opts.useTextures = false;
          }
          else if (arg == "--capture-frames")
          {
            String value;
            if (!nextString(value))
            {
              return false;
            }

            opts.captureFrames.clear();
            StringArray parts;
            Split(value, ",", parts);
            for (const String& part : parts)
            {
              if (!part.empty())
              {
                opts.captureFrames.push_back(std::atoi(part.c_str()));
              }
            }
          }
          else if (arg == "--hidden")
          {
            opts.hidden = true;
          }
          else if (arg == "--no-capture")
          {
            opts.capture = false;
          }
          else if (arg == "--reference-root")
          {
            nextString(opts.referenceRoot);
          }
          else if (arg == "--results")
          {
            nextString(opts.resultsDir);
          }
          else if (arg == "--profile")
          {
            opts.profile = true;
          }
          else if (arg == "--update-reference")
          {
            opts.updateReference = true;
          }
          else if (arg == "--reference-scale")
          {
            String value;
            if (nextString(value))
            {
              opts.referenceScale = glm::clamp((float) std::atof(value.c_str()), 0.1f, 1.0f);
            }
          }
          else if (arg == "--tolerance")
          {
            String value;
            if (nextString(value))
            {
              opts.tolerance = (float) std::atof(value.c_str());
            }
          }
          else if (arg == "--no-compare")
          {
            opts.compare = false;
          }
          else if (arg == "--help" || arg == "-h")
          {
            return false;
          }
          else
          {
            TK_WRN("Unknown argument: %s", arg.c_str());
            return false;
          }
        }

        return opts.frames > 0 && opts.width > 0 && opts.height > 0;
      }

      double Percentile(std::vector<double> values, float percentile)
      {
        if (values.empty())
        {
          return 0.0;
        }

        std::sort(values.begin(), values.end());
        size_t index = (size_t) (percentile * (float) (values.size() - 1));
        return values[index];
      }

      uint64 MedianU64(std::vector<uint64> values)
      {
        if (values.empty())
        {
          return 0;
        }

        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
      }

      // ---------------------------------------------------------------------
      // Scene
      // ---------------------------------------------------------------------

      MaterialPtr MakeMaterial(const Vec3& color,
                               float metallic,
                               float roughness,
                               TexturePtr diffuse = nullptr,
                               bool translucent  = false)
      {
        MaterialPtr mat = GetMaterialManager()->GetCopyOfDefaultMaterial();
        mat->SetColorVal(color);
        mat->SetMetallicVal(metallic);
        mat->SetRoughnessVal(roughness);

        // Always assign, including the null case: a copy of the default material
        // arrives with the engine's default diffuse texture already set, so passing
        // nullptr here has to clear it rather than skip the assignment.
        mat->SetDiffuseTextureVal(diffuse);

        if (translucent)
        {
          mat->SetAlphaVal(0.45f);
          mat->SetBlendState(BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA);
        }

        mat->Init();
        return mat;
      }

      /** Points a node at a target. Builds a standard look at basis and decomposes it. */
      void AimNodeAt(Node* node, const Vec3& eye, const Vec3& target)
      {
        const Mat4 world = glm::inverse(glm::lookAt(eye, target, Vec3(0.0f, 1.0f, 0.0f)));

        node->SetTranslation(eye, TransformationSpace::TS_WORLD);
        node->SetOrientation(glm::quat_cast(Mat3(world)), TransformationSpace::TS_WORLD);
      }

      void DriveCamera(const CameraPtr& cam, int frame, float extent);

      String TrimCopy(const String& text)
      {
        const size_t begin = text.find_first_not_of(" \t");
        if (begin == String::npos)
        {
          return String();
        }
        const size_t end = text.find_last_not_of(" \t");
        return text.substr(begin, end - begin + 1);
      }

      /** Repository root, derived from the binary location, never from the cwd. */
      String RepoRoot()
      {
        const String exePath = PlatformHelpers::GetExecutablePath();
        String exeDir;
        DecomposePath(exePath, &exeDir, nullptr, nullptr);
        return PathToString(std::filesystem::path(exeDir).parent_path());
      }

      /**
       * Where the reference images live.
       *
       * Priority: --reference-root, then --baseline, then the `reference_root` line
       * of Bench/bench.txt, then a sibling folder next to the repository. Relative
       * paths in the config file resolve against the repository root.
       */
      String ResolveReferenceRoot(const Options& opts)
      {
        // Several engine helpers compare paths textually, so hand back a normalized
        // absolute path: a ".." segment would make the manifest check miss a file
        // that exists.
        auto normalize = [](const String& path) -> String
        { return PathToString(std::filesystem::path(path).lexically_normal()); };

        if (!opts.referenceRoot.empty())
        {
          return normalize(opts.referenceRoot);
        }

        if (!opts.baseline.empty())
        {
          return normalize(opts.baseline);
        }

        const String repoRoot = RepoRoot();
        std::ifstream config(ConcatPaths({repoRoot, "Bench", "bench.txt"}).c_str());
        String line;
        while (std::getline(config, line))
        {
          const size_t separator = line.find('=');
          if (separator == String::npos)
          {
            continue;
          }

          if (TrimCopy(line.substr(0, separator)) != "reference_root")
          {
            continue;
          }

          String value = TrimCopy(line.substr(separator + 1));
          if (value.empty())
          {
            break;
          }

          if (!std::filesystem::path(value).is_absolute())
          {
            value = ConcatPaths({repoRoot, value});
          }

          return normalize(value);
        }

        return normalize(ConcatPaths({repoRoot, "..", "GDTK-Bench-Reference"}));
      }

      /** Half width of the visible prop field, in world units. */
      float FieldExtent(const Options& opts) { return opts.spacing * float(opts.grid - 1) * 0.5f; }

      /**
       * Builds the benchmark scene.
       *
       * The scene is deliberately boring: it is a stress pattern, not art. Every
       * feature that must keep rendering is represented by geometry that only that
       * feature can produce a visible result on.
       */
      ScenePtr BuildScene(const Options& opts, CameraPtr& cameraOut)
      {
        ScenePtr scene = MakeNewPtr<Scene>();
        scene->m_name  = "BenchScene";

        // --- Sky -------------------------------------------------------------
        // Owns the global IBL. The default HDRI is shared with the local volumes
        // below, so the irradiance caches are generated once.
        std::shared_ptr<Sky> sky                = MakeNewPtr<Sky>();
        sky->SetNameVal("BenchSky");

        TextureManager* textureMan = GetTextureManager();
        HdriPtr hdri               = textureMan->Create<Hdri>(textureMan->GetDefaultResource(Hdri::StaticClass()));

        EnvironmentComponentPtr skyEnv = sky->AddComponent<EnvironmentComponent>();
        skyEnv->SetHdriVal(hdri);
        skyEnv->SetIntensityVal(0.15f);
        scene->AddEntity(sky);

        // Field geometry. Everything downstream is placed relative to the extent of
        // the prop field, so a --grid change keeps the framing and the light setup
        // meaningful instead of pushing the camera inside the props.
        const float extent     = FieldExtent(opts);
        const float groundSize = extent * 2.0f + 40.0f;
        const float half       = float(opts.grid - 1) * 0.5f;

        // --- Materials -------------------------------------------------------
        // Six shared materials over three texture sets (a checker, the engine's
        // default texture, and no texture at all). Two materials deliberately share
        // the checker with different parameters: that is the case where a per
        // instance material index could matter, so the batching work has to handle
        // both "same material" and "same texture set, different parameters".
        TexturePtr checker, defaultTex;
        if (opts.useTextures)
        {
          checker    = textureMan->Create<Texture>(TexturePath("checkerBoard.png", true));
          defaultTex = textureMan->Create<Texture>(TexturePath("default.png", true));
        }

        MaterialPtr matCheckerMatte = MakeMaterial(Vec3(0.85f, 0.80f, 0.70f), 0.0f, 0.65f, checker);
        MaterialPtr matCheckerMetal = MakeMaterial(Vec3(0.80f, 0.85f, 0.95f), 0.85f, 0.25f, checker);
        MaterialPtr matDefault      = MakeMaterial(Vec3(0.90f, 0.72f, 0.60f), 0.10f, 0.50f, defaultTex);
        MaterialPtr matPlain        = MakeMaterial(Vec3(0.45f, 0.62f, 0.55f), 0.40f, 0.35f, nullptr);
        MaterialPtr matTranslucent  = MakeMaterial(Vec3(0.40f, 0.55f, 0.95f), 0.0f, 0.10f, nullptr, true);

        MaterialPtrArray materialSet = {matCheckerMatte, matCheckerMetal, matDefault, matPlain};

        // Alternating order so both same-material and same-texture-set neighbours
        // show up in the job list.
        static const int materialCycle[8] = {0, 1, 2, 3, 0, 2, 1, 3};

        // --- Ground ----------------------------------------------------------
        // Matte, not a mirror. A polished floor hides exactly what has to be
        // visible here: cast shadows and ambient occlusion. Albedo is dark enough
        // that a shadow reads as a clear step, and the metalness is near zero so
        // the surface does not mirror the sky and wash the shadow out.
        MaterialPtr groundMat = MakeMaterial(Vec3(1.0f, 1.0f, 1.0f), 0.0f, 1.0f);

        CubePtr ground        = MakeNewPtr<Cube>();
        ground->SetNameVal("Ground");
        ground->SetCubeScaleVal(Vec3(groundSize, 0.4f, groundSize));
        ground->m_node->SetTranslation(Vec3(0.0f, -0.2f, 0.0f), TransformationSpace::TS_WORLD);
        ground->GetComponent<MaterialComponent>()->SetFirstMaterial(groundMat);
        scene->AddEntity(ground);


        // --- Plaza -----------------------------------------------------------
        // A clear area in the middle of the prop field. Without it the props are so
        // densely packed that every cast shadow lands under a neighbouring prop and
        // nothing about the shadow path is legible. The plaza holds the landmarks
        // that make each feature readable: a mirror slab, tall shadow pillars, and
        // the ambient occlusion cluster right next to it.
        const float plazaRadius = extent * 0.24f;

        // Mirror slab: the one place screen space reflections have something to show.
        MaterialPtr mirrorMat = MakeMaterial(Vec3(0.72f, 0.75f, 0.80f), 1.0f, 0.04f);

        CubePtr mirror         = MakeNewPtr<Cube>();
        mirror->SetNameVal("Mirror");
        const float mirrorSize = plazaRadius * 0.85f;
        mirror->SetCubeScaleVal(Vec3(mirrorSize, 0.05f, mirrorSize));
        mirror->m_node->SetTranslation(Vec3(plazaRadius * 2.1f, 0.03f, -plazaRadius * 0.6f),
                                       TransformationSpace::TS_WORLD);
        mirror->GetComponent<MaterialComponent>()->SetFirstMaterial(mirrorMat);
        scene->AddEntity(mirror);

        // Tall pillars around the mirror. Their long shadows across the open floor
        // are what proves the directional shadow cascade works while the camera moves.
        MaterialPtr pillarMat = MakeMaterial(Vec3(0.62f, 0.60f, 0.55f), 0.0f, 0.55f);

        for (int i = 0; i < 3; i++)
        {
          const float angle  = (2.0f * glm::pi<float>() * float(i)) / 3.0f + 0.4f;
          const float height = 5.0f + 2.0f * float(i);

          CubePtr pillar     = MakeNewPtr<Cube>();
          pillar->SetNameVal("Pillar" + std::to_string(i));
          pillar->SetCubeScaleVal(Vec3(1.5f, height, 1.5f));
          pillar->m_node->SetTranslation(Vec3(plazaRadius * 1.15f * glm::cos(angle),
                                             height * 0.5f,
                                             plazaRadius * 1.15f * glm::sin(angle)),
                                         TransformationSpace::TS_WORLD);
          pillar->m_node->SetOrientation(glm::angleAxis(angle, Vec3(0.0f, 1.0f, 0.0f)),
                                         TransformationSpace::TS_WORLD);
          pillar->GetComponent<MaterialComponent>()->SetFirstMaterial(pillarMat);
          scene->AddEntity(pillar);
        }


        // --- Duplicated props ------------------------------------------------
        // The batching workload: thousands of copies over four shared materials and
        // two shared meshes. Every ninth prop gets a mirrored X scale, which flips
        // the triangle winding and exercises the cull flip path.
        int propIndex = 0;

        for (int z = 0; z < opts.grid; z++)
        {
          for (int x = 0; x < opts.grid; x++)
          {
            const Vec3 pos((float(x) - half) * opts.spacing, 0.95f, (float(z) - half) * opts.spacing);

            // Keep the plaza clear so shadows and the mirror stay readable.
            if (glm::length(Vec2(pos.x, pos.z)) < plazaRadius)
            {
              propIndex++;
              continue;
            }

            const float baseScale = 1.0f + 0.07f * float(propIndex % 3);

            Vec3 scale(baseScale, baseScale, baseScale);
            if (propIndex % 9 == 4)
            {
              scale.x = -baseScale;
            }

            EntityPtr prop = nullptr;
            if (propIndex % 2 == 0)
            {
              CubePtr cube = MakeNewPtr<Cube>();
              cube->SetNameVal("PropCube");
              cube->SetCubeScaleVal(Vec3(1.3f, 1.3f, 1.3f));
              prop         = cube;
            }
            else
            {
              SpherePtr sphere = MakeNewPtr<Sphere>();
              sphere->SetNameVal("PropSphere");
              sphere->SetRadiusVal(0.75f);
              prop             = sphere;
            }

            prop->m_node->SetTranslation(pos, TransformationSpace::TS_WORLD);
            prop->m_node->SetScale(scale);
            prop->m_node->SetOrientation(glm::angleAxis(glm::radians(11.0f * float(propIndex % 8)),
                                                        Vec3(0.0f, 1.0f, 0.0f)),
                                         TransformationSpace::TS_WORLD);
            prop->GetComponent<MaterialComponent>()->SetFirstMaterial(materialSet[materialCycle[propIndex % 8]]);
            scene->AddEntity(prop);

            propIndex++;
          }
        }

        // No artificial off screen ring: the camera path sits inside the prop field,
        // so roughly the half of the field behind the camera is rejected by the
        // frustum cull on every frame. `drawCalls` in the CSV versus the prop count
        // is the measurement that the cull path is doing real work.

        // --- Ambient occlusion cluster ---------------------------------------
        // Tightly packed boxes with no gap: the region where SSAO produces a strong,
        // obvious darkening. Placed at the north west corner of the field, in view.
        const float clusterSpacing = 0.78f;
        const float clusterCenter  = -plazaRadius * 1.7f;

        for (int z = 0; z < opts.cluster; z++)
        {
          for (int y = 0; y < opts.cluster; y++)
          {
            for (int x = 0; x < opts.cluster; x++)
            {
              CubePtr box     = MakeNewPtr<Cube>();
              box->SetNameVal("AoBox");
              box->SetCubeScaleVal(Vec3(0.75f, 0.75f, 0.75f));
              const Vec3 pos(clusterCenter + float(x) * clusterSpacing,
                             0.4f + float(y) * clusterSpacing,
                             clusterCenter + float(z) * clusterSpacing);
              box->m_node->SetTranslation(pos, TransformationSpace::TS_WORLD);
              box->GetComponent<MaterialComponent>()->SetFirstMaterial(matCheckerMatte);
              scene->AddEntity(box);
            }
          }
        }

        // --- Translucent props ----------------------------------------------
        // Stay on the per job path in the batching work, so they must still show
        // up in the reference image.
        for (int i = 0; i < 3; i++)
        {
          SpherePtr bubble = MakeNewPtr<Sphere>();
          bubble->SetNameVal("Translucent");
          bubble->SetRadiusVal(1.1f + 0.2f * float(i));
          bubble->m_node->SetTranslation(Vec3(extent * 0.25f + 2.4f * float(i), 1.4f, -extent * 0.3f),
                                         TransformationSpace::TS_WORLD);
          bubble->GetComponent<MaterialComponent>()->SetFirstMaterial(matTranslucent);
          scene->AddEntity(bubble);
        }

        // --- Local environment volumes --------------------------------------
        // Overlapping volumes with different intensity and fade distance, so the
        // shader blends a primary and a secondary IBL against the sky.
        for (int i = 0; i < opts.envVolumes; i++)
        {
          EntityPtr volume = MakeNewPtr<Entity>();
          volume->SetNameVal("EnvVolume" + std::to_string(i));

          const float offset = float(i) * extent * 0.35f;
          const float size   = extent * (0.75f - 0.12f * float(i));

          volume->m_node->SetTranslation(Vec3(-extent * 0.3f + offset, size * 0.45f, 0.0f),
                                         TransformationSpace::TS_WORLD);

          EnvironmentComponentPtr env = volume->AddComponent<EnvironmentComponent>();
          env->SetHdriVal(hdri);
          env->SetSizeVal(Vec3(size, size * 0.5f, size));
          env->SetIlluminateVal(true);
          env->SetIntensityVal(2.6f - 0.9f * float(i));
          env->SetFadeVal(1.5f);
          env->SetParallaxCorrectionVal(i == 0);
          env->SetInteriorVal(false);

          scene->AddEntity(volume);
        }

        // --- Lights ----------------------------------------------------------
        // A hard sun for the cast shadows, a shadow casting spot to prove the spot
        // path and its atlas slot work, and point lights for the per object light
        // index lists (the strongest part of the per draw CPU cost, see plan 2.2).
        DirectionalLightPtr sun = MakeNewPtr<DirectionalLight>();
        sun->SetNameVal("Sun");
        sun->SetColorVal(Vec3(1.0f, 0.98f, 0.95f));
        sun->SetIntensityVal(1.1f);
        sun->SetCastShadowVal(true);
        // 45 degree elevation, 45 degree azimuth. AimNodeAt gives the node a forward
        // axis of -Z pointing from the eye to the target, which is what the
        // DirectionComponent reads as the light direction.
        const Vec3 sunDirection = glm::normalize(Vec3(-0.5f, -0.70710678f, -0.5f));
        AimNodeAt(sun->m_node, -sunDirection * 40.0f, Vec3(0.0f, 0.0f, 0.0f));
        scene->AddEntity(sun);

        if (opts.spotShadow)
        {
          SpotLightPtr spot = MakeNewPtr<SpotLight>();
          spot->SetNameVal("Spot");
          spot->SetColorVal(Vec3(1.0f, 0.86f, 0.68f));
          spot->SetIntensityVal(3.5f);
          spot->SetRadiusVal(extent * 3.0f);
          spot->SetOuterAngleVal(38.0f);
          spot->SetInnerAngleVal(22.0f);
          spot->SetCastShadowVal(true);

          const Vec3 spotPos(extent * 0.55f, extent * 1.15f, extent * 0.55f);
          AimNodeAt(spot->m_node, spotPos, Vec3(0.0f, 0.0f, 0.0f));
          scene->AddEntity(spot);
        }

        for (int i = 0; i < opts.pointLights; i++)
        {
          PointLightLightPtr point = MakeNewPtr<PointLight>();
          point->SetNameVal("Point" + std::to_string(i));
          point->SetColorVal(i == 0 ? Vec3(1.0f, 0.75f, 0.45f) : Vec3(0.45f, 0.75f, 1.0f));
          point->SetIntensityVal(2.0f);
          point->SetRadiusVal(extent * 1.2f);
          point->SetCastShadowVal(false);

          // Spread them over the field so different props get different light sets.
          const float angle = (2.0f * glm::pi<float>() * float(i)) / glm::max(1.0f, float(opts.pointLights));
          point->m_node->SetTranslation(Vec3(extent * 0.55f * glm::cos(angle),
                                             extent * 0.35f,
                                             extent * 0.55f * glm::sin(angle)),
                                        TransformationSpace::TS_WORLD);
          scene->AddEntity(point);
        }

        // --- Camera ----------------------------------------------------------
        // Not added to the scene: the render path takes the camera pointer from
        // the viewport, and a camera entity would only add an extra entity to
        // cull.
        cameraOut = MakeNewPtr<Camera>();
        // SetLens takes the field of view in radians; every other angle in this
        // file is in degrees, so convert explicitly.
        cameraOut->SetLens(glm::radians(55.0f),
                           float(opts.width) / float(opts.height),
                           0.1f,
                           extent * 6.0f + 200.0f);
        DriveCamera(cameraOut, 0, extent);

        // --- Post processing -------------------------------------------------
        PostProcessingSettingsPtr pps = scene->m_postProcessSettings;
        pps->SetSSAOEnabledVal(opts.ssao);
        pps->SetSSAORadiusVal(0.9f);
        pps->SetSSAOBiasVal(0.02f);
        pps->SetSSAOSpreadVal(1.1f);
        pps->SetSSAOHalfResolutionVal(false);

        pps->SetSSREnabledVal(opts.ssr);
        pps->SetSSRIntensityVal(1.0f);
        pps->SetSSRMaxDistanceVal(60.0f);
        pps->SetSSRThicknessVal(0.35f);
        pps->SetSSRStepCountVal(48);
        pps->SetSSRRoughnessCutoffVal(0.45f);
        pps->SetSSRDebugViewVal(false);

        pps->SetTonemappingEnabledVal(opts.tonemap);
        pps->SetGammaCorrectionEnabledVal(opts.gamma);
        pps->SetGammaVal(2.2f);

        // Off on purpose: FXAA would soften edges every run in a slightly
        // different way depending on driver specifics, and MSAA already covers
        // anti aliasing. Determinism of the reference image matters more here.
        pps->SetFXAAEnabledVal(false);

        pps->SetBloomEnabledVal(false);
        pps->SetDepthOfFieldEnabledVal(false);


        return scene;
      }

      /**
       * Deterministic camera path. Pure function of the frame index: an orbit plus
       * a slow dolly, which keeps geometry moving through the frustum, the shadow
       * cascades and the environment volumes between reference frames.
       *
       * The orbit radius and height are derived from the prop field so that a
       * denser or wider field stays framed instead of ending up under the camera.
       */
      void DriveCamera(const CameraPtr& cam, int frame, float extent)
      {
        const float t      = float(frame) * 0.0145f; // radians per frame, slow orbit
        const float base   = extent * 0.42f + 6.0f;
        const float radius = base + extent * 0.06f * glm::sin(t * 0.37f);
        const float height = extent * 0.26f + 5.0f + 1.5f * glm::sin(t * 0.21f);

        // Looks at the plaza, so the shadow pillars, the mirror and the occlusion
        // cluster stay in frame while the orbit moves around them.
        const Vec3 eye(radius * glm::cos(t), height, radius * glm::sin(t));
        const Vec3 target(0.0f, 1.5f, 0.0f);

        AimNodeAt(cam->m_node, eye, target);
      }

      // ---------------------------------------------------------------------
      // Capture
      // ---------------------------------------------------------------------

      void FlipRows(ubyte* pixels, int width, int height)
      {
        const size_t rowBytes = size_t(width) * g_channels;
        for (int y = 0; y < height / 2; y++)
        {
          ubyte* top    = pixels + size_t(y) * rowBytes;
          ubyte* bottom = pixels + size_t(height - 1 - y) * rowBytes;
          for (size_t i = 0; i < rowBytes; i++)
          {
            std::swap(top[i], bottom[i]);
          }
        }
      }

      /**
       * Reads the final composited frame back from the framebuffer the render path wrote.
       *
       * Not the back buffer: its content is undefined while the window is occluded or
       * covered, and reading it then returns a blank or stale frame. That is exactly how
       * the "run to run variance" in an earlier version of this tool was produced - a
       * white capture against a real reference. GameRenderer exposes the framebuffer it
       * composited into for this purpose.
       */
      bool CaptureOutput(Renderer* renderer,
                         const FramebufferPtr& output,
                         int width,
                         int height,
                         std::vector<ubyte>& pixels)
      {
        pixels.assign(size_t(width) * size_t(height) * g_channels, 0);
        renderer->SetFramebuffer(output, GraphicBitFields::None);
        renderer->GetBackend()->ReadPixels(0,
                                           0,
                                           width,
                                           height,
                                           GraphicTypes::FormatRGBA,
                                           GraphicTypes::TypeUnsignedByte,
                                           pixels.data());

        // GL hands the rows back bottom up. Normalise once, here: everything
        // downstream (PNG output, the baseline comparison and the hash) then works
        // in image order and never flips again.
        FlipRows(pixels.data(), width, height);
        return true;
      }

      uint64 HashPixels(const std::vector<ubyte>& pixels)
      {
        // FNV-1a, cheap and good enough to notice "the same run twice".
        uint64 hash = 1469598103934665603ull;
        for (ubyte p : pixels)
        {
          hash ^= uint64(p);
          hash *= 1099511628211ull;
        }
        return hash;
      }

      bool WritePng(const String& file, int width, int height, const std::vector<ubyte>& pixels)
      {
        const int result = WritePNG(file, width, height, g_channels, pixels.data(), width * g_channels);
        return result != 0;
      }

      /** Box filtered resize, used only when the reference is stored downscaled. */
      bool Downscale(const std::vector<ubyte>& src,
                     int srcWidth,
                     int srcHeight,
                     std::vector<ubyte>& dst,
                     int dstWidth,
                     int dstHeight)
      {
        dst.resize(size_t(dstWidth) * size_t(dstHeight) * g_channels);
        const int result = ImageResize(src.data(),
                                       srcWidth,
                                       srcHeight,
                                       srcWidth * g_channels,
                                       dst.data(),
                                       dstWidth,
                                       dstHeight,
                                       dstWidth * g_channels,
                                       g_channels);
        return result != 0;
      }

      /**
       * Compares a capture against the baseline of the same frame.
       *
       * Returns the mean absolute per channel difference in 0-255 units. A
       * missing baseline is not an error: it is created from this run so the
       * next run has something to compare against.
       */
      float CompareWithBaseline(const String& baselineFile, int width, int height, const std::vector<ubyte>& current)
      {
        if (!CheckSystemFile(baselineFile))
        {
          return -1.0f;
        }

        int baseWidth = 0, baseHeight = 0, baseComp = 0;
        // ImageLoadTopDown, not ImageLoad: the latter follows the engine's vertical
        // flip switch, which exists for the GL texture path and would hand the rows
        // back upside down here.
        unsigned char* basePixels = ImageLoadTopDown(baselineFile.c_str(), &baseWidth, &baseHeight, &baseComp, 0);
        if (basePixels == nullptr)
        {
          TK_ERR("Bench: could not read baseline '%s'.", baselineFile.c_str());
          return -1.0f;
        }

        if (baseWidth != width || baseHeight != height || baseComp != g_channels)
        {
          TK_ERR("Bench: baseline '%s' is %dx%d with %d channels, capture is %dx%d with %d.",
                 baselineFile.c_str(),
                 baseWidth,
                 baseHeight,
                 baseComp,
                 width,
                 height,
                 g_channels);
            ImageFree(basePixels);
          return -1.0f;
        }

        // Both sides are in image order: the capture was normalised right after the
        // read back, the baseline is loaded top down.
        double total = 0.0;
        for (size_t i = 0; i < current.size(); i++)
        {
          total += std::abs((double) current[i] - (double) basePixels[i]);
        }

        ImageFree(basePixels);
        return float(total / double(current.size()));
      }

      // ---------------------------------------------------------------------
      // Output
      // ---------------------------------------------------------------------

      void WriteCsv(const String& file, const std::vector<FrameStats>& rows)
      {
        std::ofstream stream(file.c_str(), std::ios::out | std::ios::trunc);
        stream << "frame,frameCpuMs,renderCpuMs,drawCalls\n";
        for (const FrameStats& row : rows)
        {
          stream << row.frame << ',' << row.frameMs << ',' << row.renderMs << ',' << row.drawCalls << '\n';
        }
      }

      void WriteSummary(const String& file,
                        const Options& opts,
                        const std::vector<FrameStats>& rows,
                        const std::vector<uint64>& hashes,
                        float worstImageDiff)
      {
        std::vector<double> frameTimes, renderTimes;
        std::vector<uint64> drawCalls;

        frameTimes.reserve(rows.size());
        renderTimes.reserve(rows.size());
        drawCalls.reserve(rows.size());

        for (const FrameStats& row : rows)
        {
          frameTimes.push_back(row.frameMs);
          renderTimes.push_back(row.renderMs);
          drawCalls.push_back(row.drawCalls);
        }

        std::ofstream stream(file.c_str(), std::ios::out | std::ios::trunc);
        stream << "{\n";
        stream << "  \"tag\": \"" << opts.tag << "\",\n";
      stream << "  \"baselineSet\": \"" << opts.baselineSet << "\",\n";
        stream << "  \"frames\": " << opts.frames << ",\n";
        stream << "  \"warmup\": " << opts.warmup << ",\n";
        stream << "  \"resolution\": [" << opts.width << ", " << opts.height << "],\n";
        stream << "  \"grid\": " << opts.grid << ",\n";
        stream << "  \"spacing\": " << opts.spacing << ",\n";

        stream << "  \"envVolumes\": " << opts.envVolumes << ",\n";
        stream << "  \"pointLights\": " << opts.pointLights << ",\n";
        stream << "  \"spotShadow\": " << (opts.spotShadow ? "true" : "false") << ",\n";
        stream << "  \"textures\": " << (opts.useTextures ? "true" : "false") << ",\n";
        stream << "  \"msaa\": " << (opts.msaa ? "true" : "false") << ",\n";
        stream << "  \"ssao\": " << (opts.ssao ? "true" : "false") << ",\n";
        stream << "  \"ssr\": " << (opts.ssr ? "true" : "false") << ",\n";
        stream << "  \"tonemap\": " << (opts.tonemap ? "true" : "false") << ",\n";
        stream << "  \"gamma\": " << (opts.gamma ? "true" : "false") << ",\n";
        stream << "  \"renderCpuMs\": {\"median\": " << Percentile(renderTimes, 0.5f)
               << ", \"p95\": " << Percentile(renderTimes, 0.95f) << "},\n";
        stream << "  \"frameCpuMs\": {\"median\": " << Percentile(frameTimes, 0.5f)
               << ", \"p95\": " << Percentile(frameTimes, 0.95f) << "},\n";
        stream << "  \"drawCalls\": " << MedianU64(drawCalls) << ",\n";
        stream << "  \"captureHash\": [";
        for (size_t i = 0; i < hashes.size(); i++)
        {
          stream << (i == 0 ? "" : ", ") << hashes[i];
        }
        stream << "],\n";
        stream << "  \"worstImageDiff\": " << worstImageDiff << "\n";
        stream << "}\n";
      }

      /**
       * Prints the engine profiler tree for the last frame.
       *
       * Every `TK_PROFILE_FUNCTION` in the engine becomes a node, so this is how the
       * draw path is broken down without instrumenting the render path itself. A
       * node's exclusive time excludes its children; inclusive includes them.
       */
      void DumpProfiler(int maxDepth)
      {
        TKProfiler* profiler = Profiler::GetProfiler();
        if (profiler == nullptr)
        {
          return;
        }

        std::function<void(const ProfilerNode*, int)> walk = [&](const ProfilerNode* node, int depth)
        {
          if (node == nullptr || depth > maxDepth)
          {
            return;
          }

          printf("Bench: prof %*s%-34s excl %8.3f ms  incl %8.3f ms  hits %u\n",
                 depth * 2,
                 "",
                 node->name.c_str(),
                 node->exclusiveTimePrev,
                 node->inclusiveTimePrev,
                 node->hitCountPrev);

          for (const ProfilerNode* child : node->children)
          {
            walk(child, depth + 1);
          }
        };

        for (const ProfilerNode* root : profiler->GetRootNodes())
        {
          walk(root, 0);
        }
      }

      void EnsureDirectory(const String& dir)
      {
        std::error_code error;
        std::filesystem::create_directories(dir, error);
        if (error)
        {
          TK_WRN("Bench: could not create '%s': %s", dir.c_str(), error.message().c_str());
        }
      }

      /**
       * One line description of everything that changes the reference image.
       *
       * Written next to the baseline captures and compared on every run: a baseline
       * is only valid for the exact configuration that produced it, so a --grid or
       * --spacing change has to use a different baseline set instead of silently
       * comparing against images from another scene.
       */
      /** Build configuration the numbers and the reference belong to. */
      const char* BuildConfigName()
      {
#if TK_DEBUG
        return "debug";
#else
        return "release";
#endif
      }

      String SceneSignature(const Options& opts)
      {
        String frames;
        for (size_t i = 0; i < opts.captureFrames.size(); i++)
        {
          frames += (i == 0 ? "" : ",") + std::to_string(opts.captureFrames[i]);
        }

        // Geometry and capture frames only: these define what the reference image is.
        // The build configuration changes the image as much as the scene does, so a
        // Debug run must never be compared against a Release reference.
        return String(BuildConfigName()) + " " + std::to_string(opts.width) + "x" +
               std::to_string(opts.height) + " grid " +
               std::to_string(opts.grid) + " spacing " + std::to_string(opts.spacing).substr(0, 4) +
               " cluster " + std::to_string(opts.cluster) + " ref " +
               std::to_string(opts.referenceScale).substr(0, 4) + " capture " + frames;
      }

      /**
       * The feature switches are recorded but not enforced: turning one off on
       * purpose is how the effect of that feature is measured against the reference.
       */
      String FeatureSignature(const Options& opts)
      {
        // Everything that changes what the renderer does without moving the scene.
        return String("env ") + std::to_string(opts.envVolumes) + " points " +
               std::to_string(opts.pointLights) + " spot " + (opts.spotShadow ? "1" : "0") + " textures " +
               (opts.useTextures ? "1" : "0") + " msaa " + (opts.msaa ? "1" : "0") + " ssao " +
               (opts.ssao ? "1" : "0") + " ssr " + (opts.ssr ? "1" : "0") + " tonemap " +
               (opts.tonemap ? "1" : "0");
      }

      // ---------------------------------------------------------------------
      // Engine setup helpers
      // ---------------------------------------------------------------------

      /**
       * Applies the settings that change what gets rendered. Must run before the
       * viewport is created, because the viewport builds its render target from
       * these values exactly once.
       */
      void ApplySettings(const Options& opts)
      {
        GraphicSettingsPtr graphics = GetEngineSettings().m_graphics;

        // HDR pipeline on: the scene renders to a float target and the gamma and
        // tonemap pass does the encoding. This is the shipping configuration.
        graphics->SetHDRPipelineVal(true);

        MultiChoiceVariant msaa = graphics->GetMSAAVal();
        msaa.SetEnum(opts.msaa ? MsaaSampleCount::x4 : MsaaSampleCount::x0);
        graphics->SetMSAAVal(msaa);

        graphics->SetRenderResolutionScaleVal(1.0f);
        graphics->SetMultiThreadedVal(true);

        // Shadows, configured explicitly in world units.
        //
        // The engine defaults keep the parallel split partitioning (PSSM) path on
        // with ShadowMinDistance = 1.0. ShadowPass derives its split as
        // `minZ = near + ShadowMinDistance * (far - near)`, so minZ lands on the far
        // plane and the split range goes negative, which degenerates the cascade
        // distances and the shadow cameras with them. The fragment shader also
        // compares its world space view depth against `cascadeDistances`, so those
        // numbers have to be world units rather than PSSM's normalised fractions.
        // Manual distances avoid both problems and still exercise the cascaded path.
        ShadowSettingsPtr shadows = graphics->m_shadows;
        shadows->SetUseParallelSplitPartitioningVal(false);
        shadows->SetCascadeCountVal(4);
        shadows->SetCascadeDistancesVal(Vec4(15.0f, 35.0f, 80.0f, 220.0f));
        shadows->SetShadowMinDistanceVal(0.0f);
      }

      bool CreateBenchWindow(const Options& opts, SDL_Window** window, SDL_GLContext* context)
      {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
        {
          TK_ERR("Bench: SDL_Init failed: %s", SDL_GetError());
          return false;
        }

        if constexpr (TK_PLATFORM == PLATFORM::TKWindows)
        {
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
          SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
        }
        else
        {
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        }

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

        Uint32 flags = SDL_WINDOW_OPENGL;
        if (opts.hidden)
        {
          flags |= SDL_WINDOW_HIDDEN;
        }

        *window = SDL_CreateWindow("GDTK Bench",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   opts.width,
                                   opts.height,
                                   flags);
        if (*window == nullptr)
        {
          TK_ERR("Bench: SDL_CreateWindow failed: %s", SDL_GetError());
          return false;
        }

        *context = SDL_GL_CreateContext(*window);
        if (*context == nullptr)
        {
          TK_ERR("Bench: SDL_GL_CreateContext failed: %s", SDL_GetError());
          return false;
        }

        SDL_GL_MakeCurrent(*window, *context);
        return true;
      }
    } // namespace

    // -----------------------------------------------------------------------
    // Entry point
    // -----------------------------------------------------------------------

    int Run(int argc, char* argv[])
    {
      // Unbuffered stdout: the tool must show how far it got even when it dies.
      setvbuf(stdout, nullptr, _IONBF, 0);

      Options opts;
      if (!ParseArgs(argc, argv, opts))
      {
        PrintUsage();
        return 1;
      }

      printf("Bench: tag=%s frames=%d warmup=%d grid=%d env=%d lights=%d %dx%d\n",
             opts.tag.c_str(),
             opts.frames,
             opts.warmup,
             opts.grid,
             opts.envVolumes,
             opts.pointLights,
             opts.width,
             opts.height);

      // --- Engine -----------------------------------------------------------
      Main* main = new Main();
      Main::SetProxy(main);

      // Resolve the engine's resource and config roots from the binary location
      // (<root>/Bin<Config>/Bench.exe -> <root>), so the tool can be started from
      // any working directory.
      // Several engine helpers compare resource paths textually, so the roots have to
      // be clean absolute paths without a ".." segment.
      const String rootDir = RepoRoot();
      main->m_resourceRoot = ConcatPaths({rootDir, "Resources"});
      main->m_cfgPath      = ConcatPaths({rootDir, "Config"});

      // "def" resource lookups (the engine's default shaders, textures and
      // materials) resolve against DefaultPath(), which otherwise falls back to
      // "../Resources/Engine" relative to the process working directory.
      main->SetDefaultPath(ConcatPaths({rootDir, "Resources", "Engine"}));

      main->PreInit();

      // Route engine logs to stdout. Without this the assertions below arrive
      // without the TK_ERR line that explains them.
      GetLogger()->SetWriteConsoleFn(
          [](LogType logType, String message) -> void { printf("%s\n", message.c_str()); });

      // The profiler has a scope per pass and per draw; keep it out of the timings
      // unless it was asked for.
      Profiler::SetProfilerEnabled(opts.profile);

      ApplySettings(opts);

      SDL_Window* window   = nullptr;
      SDL_GLContext context = nullptr;
      if (!CreateBenchWindow(opts, &window, &context))
      {
        return 1;
      }

      int srgbFlag = 0;
      SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &srgbFlag);
      main->m_renderSys->m_backbufferFormatIsSRGB = (srgbFlag == 1);

      IGraphicsBackend::BackendInitParams initParams;
      initParams.getProcAddress = (void*) SDL_GL_GetProcAddress;
      initParams.errorCallback  = [](const String& msg) { fprintf(stderr, "%s\n", msg.c_str()); };
      main->m_renderSys->InitGraphics(initParams);
      main->m_renderSys->SetPresentCallback([window]() { SDL_GL_SwapWindow(window); });

      // No vsync: the numbers are CPU time, display pacing would only add noise.
      SDL_GL_SetSwapInterval(0);

      main->Init();

      // --- Viewport and scene ----------------------------------------------
      int result = 0;
      {
        GameViewportPtr viewport = MakeNewPtr<GameViewport>((float) opts.width, (float) opts.height);
        GetRenderSystem()->SetAppWindowSize(opts.width, opts.height);

        CameraPtr camera        = nullptr;
        ScenePtr scene          = BuildScene(opts, camera);
        GetSceneManager()->SetCurrentScene(scene);
        scene->Init();

        viewport->SetCamera(camera);

        printf("Bench: scene ready, %zu entities\n", scene->GetEntities().size());
        {
          ShadowSettingsPtr sh = GetEngineSettings().m_graphics->m_shadows;
          Vec4 cd              = sh->GetCascadeDistancesVal();
          printf("Bench: shadows pssm=%d count=%d dist=%.1f,%.1f,%.1f,%.1f mind=%.2f\n",
                 (int) sh->GetUseParallelSplitPartitioningVal(),
                 sh->GetCascadeCountVal(),
                 cd.x,
                 cd.y,
                 cd.z,
                 cd.w,
                 sh->GetShadowMinDistanceVal());
        }
        printf("Bench: gamma encoding %s (backbuffer srgb %d)\n",
               GetRenderSystem()->IsGammaCorrectionNeeded() ? "enabled" : "disabled",
               (int) srgbFlag);

        // --- Run ------------------------------------------------------------
        GameRenderer gameRenderer;

        GameRendererParams rendererParams;
        rendererParams.viewport            = viewport;
        rendererParams.scene               = scene;
        rendererParams.postProcessSettings = scene->m_postProcessSettings;
        gameRenderer.SetParams(rendererParams);

        std::vector<FrameStats> rows;
        std::vector<uint64> hashes;
        rows.reserve(size_t(opts.frames));

        const int totalFrames = opts.warmup + opts.frames;
        const float extent    = FieldExtent(opts);

        // Capture frames come from the configuration, not from --frames: a shorter
        // run must still compare the same reference images.
        auto isCaptureFrame = [&opts](int frame) -> bool
        {
          return std::find(opts.captureFrames.begin(), opts.captureFrames.end(), frame) !=
                 opts.captureFrames.end();
        };

        for (int captureFrame : opts.captureFrames)
        {
          if (captureFrame >= totalFrames)
          {
            TK_WRN("Bench: capture frame %d is past the end of the run (%d), it will be skipped.",
                   captureFrame,
                   totalFrames);
          }
        }

        opts.baseline = ResolveReferenceRoot(opts);
        printf("Bench: reference root %s\n", opts.baseline.c_str());

        EnsureDirectory(opts.outDir);
        EnsureDirectory(opts.resultsDir);
        if (opts.capture)
        {
          const String baseDir = ConcatPaths({opts.baseline, opts.baselineSet});
          EnsureDirectory(baseDir);

          // The reference belongs to one configuration. Refuse to compare against
          // images captured from a different scene instead of reporting a diff that
          // is really a configuration mismatch, and never create a reference by
          // accident: a missing reference is a failure, not a pass.
          const String manifestFile = ConcatPaths({baseDir, "benchmark.txt"});
          const String sceneSig     = SceneSignature(opts);
          const String featureSig   = FeatureSignature(opts);
          const String manifestText = "scene: " + sceneSig + "\nfeatures: " + featureSig + "\n";

          if (!CheckSystemFile(manifestFile) && !opts.updateReference)
          {
            TK_ERR("Bench: no reference for baseline set '%s' at\n  %s\n"
                   "Create one with --update-reference when the current image is the one\n"
                   "you want to keep, or point --reference-root at an existing folder.",
                   opts.baselineSet.c_str(),
                   baseDir.c_str());
            return 3;
          }

          if (opts.updateReference)
          {
            GetFileManager()->WriteAllText(manifestFile, manifestText);
            printf("Bench: reference '%s' written\n  scene: %s\n  features: %s\n",
                   opts.baselineSet.c_str(),
                   sceneSig.c_str(),
                   featureSig.c_str());
          }
          else
          {
            String existing = GetFileManager()->ReadAllText(manifestFile);

            // The scene part has to match exactly; the feature part is only reported.
            if (existing.find("scene: " + sceneSig) == String::npos)
            {
              TK_ERR("Bench: reference set '%s' was captured from a different scene.\n"
                     "  reference: %s\n"
                     "  this run:  %s\n"
                     "Use another --baseline-set, or refresh it with --update-reference.",
                     opts.baselineSet.c_str(),
                     existing.c_str(),
                     manifestText.c_str());
              return 3;
            }

            if (existing.find("features: " + featureSig) == String::npos)
            {
              printf("Bench: note: feature settings differ from the reference (%s), comparing anyway.\n",
                     featureSig.c_str());
            }
          }
        }

        float worstImageDiff = 0.0f;
        int comparedImages   = 0;

        for (int frame = 0; frame < totalFrames; frame++)
        {
          DriveCamera(camera, frame, extent);

          if (frame == 0)
          {
            const Vec3 camPos  = camera->m_node->GetTranslation(TransformationSpace::TS_WORLD);
            const Vec3 camDir  = camera->Direction();
            printf("Bench: camera start (%.2f %.2f %.2f) dir (%.2f %.2f %.2f) fov %.1f deg\n",
                   camPos.x,
                   camPos.y,
                   camPos.z,
                   camDir.x,
                   camDir.y,
                   camDir.z,
                   glm::degrees(camera->Fov()));
          }


          // Pump events so the window stays responsive and Alt+F4 works.
          SDL_Event event;
          while (SDL_PollEvent(&event))
          {
            if (event.type == SDL_QUIT)
            {
              frame = totalFrames;
            }
          }

          const double frameStart = NowMs();
          double renderMs         = 0.0;

          RenderTask task;
          task.Task = [&](Renderer* renderer) -> void
          {
            const double renderStart = NowMs();
            gameRenderer.Render(renderer);
            renderMs = NowMs() - renderStart;

            if (!opts.capture || !isCaptureFrame(frame))
            {
              return;
            }

            std::vector<ubyte> pixels;
            CaptureOutput(renderer, gameRenderer.GetOutputFramebuffer(), opts.width, opts.height, pixels);

            // The full resolution capture goes to the output folder for eyeballing.
            const String suffix  = "_frame" + std::to_string(frame);
            const String outFile = ConcatPaths({opts.outDir, opts.tag + suffix + ".png"});
            WritePng(outFile, opts.width, opts.height, pixels);

            // The reference may be stored downscaled to keep the reference folder
            // small. Everything compared or hashed uses the same scaled image.
            int refWidth   = opts.width;
            int refHeight  = opts.height;
            const std::vector<ubyte>* reference = &pixels;
            std::vector<ubyte> scaled;

            if (opts.referenceScale < 0.999f)
            {
              refWidth  = glm::max(16, int(float(opts.width) * opts.referenceScale));
              refHeight = glm::max(16, int(float(opts.height) * opts.referenceScale));
              if (Downscale(pixels, opts.width, opts.height, scaled, refWidth, refHeight))
              {
                reference = &scaled;
              }
              else
              {
                refWidth  = opts.width;
                refHeight = opts.height;
              }
            }

            hashes.push_back(HashPixels(*reference));

            const String baseDir  = ConcatPaths({opts.baseline, opts.baselineSet});
            const String baseFile = ConcatPaths({baseDir, "frame" + std::to_string(frame) + ".png"});

            if (opts.updateReference)
            {
              WritePng(baseFile, refWidth, refHeight, *reference);
              printf("Bench: reference written for frame %d\n", frame);
              return;
            }

            if (!opts.compare)
            {
              return;
            }

            float diff = CompareWithBaseline(baseFile, refWidth, refHeight, *reference);
            if (diff < 0.0f)
            {
              TK_ERR("Bench: reference image missing or unreadable for frame %d: %s", frame, baseFile.c_str());
              return;
            }

            comparedImages++;
            worstImageDiff = glm::max(worstImageDiff, diff);
            printf("Bench: frame %d image diff %.4f\n", frame, diff);
          };

          GetRenderSystem()->AddRenderTask(task);

          main->FrameBegin();
          main->FrameUpdate();
          main->FrameEnd();

          const double frameMs = NowMs() - frameStart;

          if (frame >= opts.warmup)
          {
            FrameStats stats;
            stats.frame     = frame;
            stats.frameMs   = frameMs;
            stats.renderMs  = renderMs;
            stats.drawCalls = Stats::GetStatPrev(FrameStatType::DrawCall);
            rows.push_back(stats);
          }

          if ((frame + 1) % 30 == 0 || frame + 1 == totalFrames)
          {
            printf("Bench: frame %d/%d\n", frame + 1, totalFrames);
          }
        }

        if (opts.profile)
        {
          printf("Bench: profiler tree for the last frame\n");
          DumpProfiler(4);
        }

        // --- Output ---------------------------------------------------------
        const String csvFile  = ConcatPaths({opts.outDir, opts.tag + ".csv"});
        const String jsonFile = ConcatPaths({opts.resultsDir, opts.tag + ".json"});
        WriteCsv(csvFile, rows);
        WriteSummary(jsonFile, opts, rows, hashes, worstImageDiff);

        std::vector<double> renderTimes;
        std::vector<uint64> drawCalls;
        for (const FrameStats& row : rows)
        {
          renderTimes.push_back(row.renderMs);
          drawCalls.push_back(row.drawCalls);
        }

        printf("Bench: %.2f ms render cpu (median), %.2f ms p95, draw calls %llu\n",
               Percentile(renderTimes, 0.5f),
               Percentile(renderTimes, 0.95f),
               (unsigned long long) MedianU64(drawCalls));

        if (comparedImages > 0)
        {
          printf("Bench: worst image diff %.4f (tolerance %.2f) over %d captures\n",
                 worstImageDiff,
                 opts.tolerance,
                 comparedImages);
          result = worstImageDiff <= opts.tolerance ? 0 : 2;
        }

        printf("Bench: wrote %s and %s\n", csvFile.c_str(), jsonFile.c_str());

        // --- Teardown (order matters, see AGENTS.md) -------------------------
        // Every engine object Bench owns has to go before the engine does. The
        // scene itself is released by SceneManager::Uninit inside Main::Uninit:
        // SetCurrentScene(nullptr) is not a release path, it dereferences the new
        // scene unconditionally, so the current scene is left to the engine.
        printf("Bench: teardown scene\n");
        gameRenderer.SetParams(GameRendererParams());
        viewport = nullptr;
        scene    = nullptr;
        camera   = nullptr;
      }

      printf("Bench: teardown engine\n");
      main->PreUninit();
      main->Uninit();
      main->PostUninit();
      SafeDel(main);
      printf("Bench: teardown platform\n");

      SDL_GL_DeleteContext(context);
      SDL_DestroyWindow(window);
      SDL_Quit();
      printf("Bench: done (%d)\n", result);

      return result;
    }
  } // namespace Bench
} // namespace ToolKit

int main(int argc, char* argv[]) { return ToolKit::Bench::Run(argc, argv); }

/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

/**
 * @file ProbeBakeMain.cpp
 *
 * Headless front end for the light probe baker.
 *
 * Boots ToolKit with the null backend, loads one scene and writes one `.pgi`. This is the path
 * a CI box takes; the editor calls the library directly instead.
 */

#include "LightProbeBake.h"

#include "PathTracer.h"
#include "Sh.h"

#include "Common/PlatformHelper.h"
#include "FileManager.h"
#include "Logger.h"
#include "RenderSystem.h"
#include "Scene.h"
#include "ToolKit.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>

namespace ToolKit
{
  namespace
  {
    constexpr float Pi = 3.14159265359f;

    /**
     * Analytic checks for the two pieces that would be invisible to break and wrong to ship: the
     * spherical harmonic projection and the path tracer's radiance hand off.
     *
     * No engine, no resources: both scenes are built in memory.
     */
    int RunSelfTest()
    {
      using namespace LightProbeBake;

      int failures = 0;

      auto project = [](const BakeScene& scene, const TraceContext& context, int samples, uint seed) -> Sh9
      {
        Sh9 sh {};
        Pcg32 rng(seed);

        for (int sample = 0; sample < samples; ++sample)
        {
          const Vec3 direction = rng.SampleUniformSphere();
          const Vec3 radiance  = TraceRadiance(context, Vec3(0.0f), direction, rng);

          float basis[ShCoefficientCount];
          ShBasis(direction, basis);

          for (int coefficient = 0; coefficient < ShCoefficientCount; ++coefficient)
          {
            sh.c[coefficient] += radiance * basis[coefficient];
          }
        }

        const float scale = 4.0f * Pi / (float) samples;
        for (int coefficient = 0; coefficient < ShCoefficientCount; ++coefficient)
        {
          sh.c[coefficient] *= scale * ShBandFactor[coefficient];
        }

        return sh;
      };

      // 1. A uniform environment of radiance 1 must evaluate to 1 in every direction.
      {
        BakeScene scene;
        scene.sky.enabled = true;
        scene.sky.top = scene.sky.middle = scene.sky.bottom = Vec3(1.0f);

        const TraceContext context {&scene, 0};
        const Sh9 sh = project(scene, context, 8192, 11);

        const Vec3 normals[3] = {Vec3(0, 1, 0), Vec3(1, 0, 0), glm::normalize(Vec3(1, 1, 1))};
        for (const Vec3& normal : normals)
        {
          const Vec3 value = ShEvaluate(sh, normal);
          printf("[selftest] uniform sky, n=(%.2f %.2f %.2f) -> %.4f\n",
                 normal.x,
                 normal.y,
                 normal.z,
                 value.x);

          if (std::abs(value.x - 1.0f) > 0.05f)
          {
            printf("[selftest] FAIL: a uniform sky has to evaluate to its radiance.\n");
            failures++;
          }
        }
      }

      // 2. A bright surface above the probe must light the up normal and not the down one.
      {
        BakeScene scene;
        scene.sky.enabled = false;

        SurfaceMaterial emissive;
        emissive.albedo   = Vec3(0.0f);
        emissive.emissive = Vec3(1.0f);
        scene.materials.push_back(emissive);

        Triangle quad;
        quad.p0 = Vec3(-10.0f, 2.0f, -10.0f);
        quad.p1 = Vec3(10.0f, 2.0f, -10.0f);
        quad.p2 = Vec3(10.0f, 2.0f, 10.0f);
        quad.n0 = quad.n1 = quad.n2 = Vec3(0.0f, -1.0f, 0.0f);
        quad.material = 0;

        Triangle quad2 = quad;
        quad2.p0       = Vec3(10.0f, 2.0f, 10.0f);
        quad2.p2       = Vec3(-10.0f, 2.0f, -10.0f);

        std::vector<Triangle> triangles = {quad, quad2};
        scene.bvh.Build(std::move(triangles));

        const TraceContext context {&scene, 1};
        const Sh9 sh = project(scene, context, 8192, 23);

        const Vec3 up   = ShEvaluate(sh, Vec3(0.0f, 1.0f, 0.0f));
        const Vec3 down = ShEvaluate(sh, Vec3(0.0f, -1.0f, 0.0f));

        printf("[selftest] emissive ceiling: up %.4f, down %.4f\n", up.x, down.x);

        if (up.x < 0.5f)
        {
          printf("[selftest] FAIL: the ceiling above the probe did not light it.\n");
          failures++;
        }

        if (down.x > 0.02f)
        {
          printf("[selftest] FAIL: the down normal receives light through the ceiling.\n");
          failures++;
        }
      }

      printf("[selftest] %s\n", failures == 0 ? "passed" : "FAILED");
      return failures == 0 ? 0 : 1;
    }

    void PrintUsage()
    {
      printf("Usage: ProbeBake --root <resources> --scene <file> --out <file.pgi> [options]\n");
      printf("\n");
      printf("  --root <dir>       Resource root of the project (its Resources folder).\n");
      printf("  --scene <path>     Scene file, relative to the resource root.\n");
      printf("  --out <path>       Output .pgi path.\n");
      printf("  --res <x,y,z>      Probes per axis. Default 8,4,8.\n");
      printf("  --samples <n>      Paths per probe. Default 512.\n");
      printf("  --bounces <n>      Surface bounces per path. Default 4.\n");
      printf("  --box <minx,miny,minz,maxx,maxy,maxz>\n");
      printf("                     World space volume box. Default: the scene bounds, padded.\n");
      printf("  --quality <l1|l2>  Coefficients stored per probe. Default l1.\n");
      printf("  --seed <n>         Sampling seed. Default 0.\n");
      printf("  --denoise <n>      Grid relaxation passes after tracing. Default 2, 0 keeps raw.\n");
    }

    bool ParseFloat3(const char* text, Vec3& out)
    {
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      if (sscanf(text, "%f,%f,%f", &x, &y, &z) != 3)
      {
        return false;
      }
      out = Vec3(x, y, z);
      return true;
    }

    bool ParseUInt3(const char* text, UVec3& out)
    {
      unsigned x = 0;
      unsigned y = 0;
      unsigned z = 0;
      if (sscanf(text, "%u,%u,%u", &x, &y, &z) != 3)
      {
        return false;
      }
      out = UVec3(x, y, z);
      return true;
    }

    bool ParseFloat6(const char* text, Vec3& min, Vec3& max)
    {
      float values[6] = {};
      if (sscanf(text, "%f,%f,%f,%f,%f,%f", &values[0], &values[1], &values[2], &values[3],
                 &values[4], &values[5]) != 6)
      {
        return false;
      }
      min = Vec3(values[0], values[1], values[2]);
      max = Vec3(values[3], values[4], values[5]);
      return true;
    }
  } // namespace

  int ProbeBakeMain(int argc, char* argv[])
  {
    String resourceRoot;
    String sceneFile;
    String outFile;
    String qualityArg = "l1";

    LightProbeBake::BakeRequest request;
    bool boxGiven = false;

    for (int i = 1; i < argc; ++i)
    {
      const String flag = argv[i];
      auto next         = [&](const char* name) -> const char*
      {
        if (i + 1 >= argc)
        {
          printf("%s needs a value.\n", name);
          return nullptr;
        }
        return argv[++i];
      };

      if (flag == "--help" || flag == "-h")
      {
        PrintUsage();
        return 0;
      }
      else if (flag == "--selftest")
      {
        return RunSelfTest();
      }
      else if (flag == "--root")
      {
        const char* value = next("--root");
        if (value == nullptr)
        {
          return 1;
        }
        resourceRoot = value;
      }
      else if (flag == "--scene")
      {
        const char* value = next("--scene");
        if (value == nullptr)
        {
          return 1;
        }
        sceneFile = value;
      }
      else if (flag == "--out")
      {
        const char* value = next("--out");
        if (value == nullptr)
        {
          return 1;
        }
        outFile = value;
      }
      else if (flag == "--res")
      {
        const char* value = next("--res");
        if (value == nullptr || !ParseUInt3(value, request.resolution))
        {
          printf("--res expects x,y,z.\n");
          return 1;
        }
      }
      else if (flag == "--samples")
      {
        const char* value = next("--samples");
        if (value == nullptr)
        {
          return 1;
        }
        request.samples = std::max(1, std::atoi(value));
      }
      else if (flag == "--bounces")
      {
        const char* value = next("--bounces");
        if (value == nullptr)
        {
          return 1;
        }
        request.bounces = std::max(1, std::atoi(value));
      }
      else if (flag == "--seed")
      {
        const char* value = next("--seed");
        if (value == nullptr)
        {
          return 1;
        }
        request.seed = (uint) std::strtoul(value, nullptr, 10);
      }
      else if (flag == "--denoise")
      {
        const char* value = next("--denoise");
        if (value == nullptr)
        {
          return 1;
        }
        request.denoiseIterations = std::max(0, std::atoi(value));
      }
      else if (flag == "--quality")
      {
        const char* value = next("--quality");
        if (value == nullptr)
        {
          return 1;
        }
        qualityArg = value;
      }
      else if (flag == "--box")
      {
        const char* value = next("--box");
        if (value == nullptr || !ParseFloat6(value, request.boxMin, request.boxMax))
        {
          printf("--box expects minx,miny,minz,maxx,maxy,maxz.\n");
          return 1;
        }
        boxGiven = true;
      }
      else
      {
        printf("Unknown argument '%s'.\n", flag.c_str());
        PrintUsage();
        return 1;
      }
    }

    if (resourceRoot.empty() || sceneFile.empty() || outFile.empty())
    {
      PrintUsage();
      return 1;
    }

    request.quality = qualityArg == "l1" ? ProbeVolumeQuality::L1 : ProbeVolumeQuality::L2;

    // Unbuffered, so a failure inside the engine still leaves a trail of what was reached.
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ProbeBake] booting the engine (null backend)\n");

    // Boot the engine headless. Same sequence the Packer uses: no window, no GL context.
    Main* g_proxy = new Main();
    Main::SetProxy(g_proxy);

    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    RenderSystem::UseNullBackend();

    g_proxy->PreInit();
    printf("[ProbeBake] PreInit done\n");

    String configDir = PlatformHelpers::GetUserConfigDir();
    String toolkitPath;
    if (!configDir.empty())
    {
      const String pathFile = ConcatPaths({configDir, "ToolKit", "Config", "Path.txt"});
      toolkitPath           = GetFileManager()->ReadAllText(pathFile);
      NormalizePathInplace(toolkitPath);
    }

    if (!toolkitPath.empty())
    {
      g_proxy->SetConfigPath(ConcatPaths({toolkitPath, "Config"}));
    }

    NormalizePathInplace(resourceRoot);
    g_proxy->m_resourceRoot = resourceRoot;

    // A relative scene path is relative to the resource root, which is how the editor names a
    // scene. An absolute path is taken as it is.
    if (!std::filesystem::path(sceneFile).is_absolute())
    {
      sceneFile = ConcatPaths({resourceRoot, sceneFile});
    }
    NormalizePathInplace(sceneFile);

    GetLogger()->SetWriteConsoleFn([](LogType, String message) -> void { printf("%s", message.c_str()); });

    SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER);
    g_proxy->m_renderSys->SetPresentCallback([]() { /* headless, nothing to present */ });
    g_proxy->Init();
    printf("[ProbeBake] engine initialized, loading scene '%s'\n", sceneFile.c_str());

    ScenePtr scene = MakeNewPtr<Scene>();
    try
    {
      scene->SetFile(sceneFile);
      scene->Load();
      GetSceneManager()->SetCurrentScene(scene);
    }
    catch (const std::exception& exception)
    {
      printf("[ProbeBake] the scene failed to load: %s\n", exception.what());
      return 1;
    }
    catch (...)
    {
      printf("[ProbeBake] the scene failed to load with an unknown exception.\n");
      return 1;
    }
    printf("[ProbeBake] scene loaded, %zu entities\n", scene->GetEntities().size());

    if (!boxGiven)
    {
      const BoundingBox& bounds = scene->GetSceneBoundary();
      const Vec3 extent         = bounds.max - bounds.min;
      const float padding       = glm::max(glm::max(extent.x, glm::max(extent.y, extent.z)) * 0.02f,
                                     0.1f);

      request.boxMin = Vec3(bounds.min) - Vec3(padding);
      request.boxMax = Vec3(bounds.max) + Vec3(padding);

      TK_LOG("Probe bake: fitted the volume to the scene bounds, %.2f x %.2f x %.2f.",
             request.boxMax.x - request.boxMin.x,
             request.boxMax.y - request.boxMin.y,
             request.boxMax.z - request.boxMin.z);
    }

    request.scene = scene.get();

    LightProbeBake::BakeResult result;
    if (!LightProbeBake::Bake(request, result))
    {
      printf("Bake failed: %s\n", result.error.c_str());
      return 1;
    }

    NormalizePathInplace(outFile);
    if (!SaveProbeVolume(outFile, result.data))
    {
      printf("Could not write '%s'.\n", outFile.c_str());
      return 1;
    }

    printf("Wrote %s: %ux%ux%u probes, %u valid, %u buried, %u unfillable, %.2f s, %.1f KB.\n",
           outFile.c_str(),
           result.data.resolution.x,
           result.data.resolution.y,
           result.data.resolution.z,
           result.validProbes,
           result.buriedProbes,
           result.invalidProbes,
           result.seconds,
           (double) result.data.AllocatedBytes() / 1024.0);

    // Round trip check: the runtime reads what this writes, so a file that does not load back
    // is a failure worth catching here and not in the editor.
    ProbeVolumeData reloaded;
    if (!LoadProbeVolume(outFile, reloaded))
    {
      printf("The written file does not load back.\n");
      return 1;
    }

    const bool sameGrid = reloaded.resolution == result.data.resolution &&
                          reloaded.quality == result.data.quality &&
                          reloaded.coefficients.size() == result.data.coefficients.size() &&
                          reloaded.valid.size() == result.data.valid.size();
    if (!sameGrid)
    {
      printf("The written file loads back with a different grid.\n");
      return 1;
    }

    printf("Round trip check passed.\n");

    return 0;
  }

} // namespace ToolKit

int main(int argc, char* argv[]) { return ToolKit::ProbeBakeMain(argc, argv); }

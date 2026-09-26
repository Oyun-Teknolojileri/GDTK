/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "ProjectThumbnail.h"

#include "App.h"
#include "ConsoleWindow.h"
#include "EditorTypes.h"
#include "EditorViewport.h"

#include <EngineSettings.h>
#include <Framebuffer.h>
#include <GammaTonemapFxaaPass.h>
#include <Image.h>
#include <RenderSystem.h>
#include <Renderer.h>
#include <ToolKit.h>
#include <Util.h>
#include <Workspace.h>

#include <cstring>
#include <memory>
#include <vector>

namespace ToolKit
{
  namespace Editor
  {
    namespace
    {
      /** File name the launcher looks for in a project folder. */
      const String g_thumbnailFileName = "thumbnail.png";

      /** Channel count of the images the read back and the PNG use. */
      const int g_channels             = 4;

      /** Flips the rows of an RGBA image in place: GL hands the pixels back bottom up. */
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
       * Copies the center square of an RGBA image into a buffer of its own, so a thumbnail of a
       * viewport that is wider than it is tall is cropped instead of squashed.
       */
      std::vector<ubyte> CenterSquare(const std::vector<ubyte>& pixels, int width, int height, int& side)
      {
        side                 = glm::min(width, height);
        const int x0         = (width - side) / 2;
        const int y0         = (height - side) / 2;

        const size_t rowSize = size_t(side) * g_channels;
        std::vector<ubyte> square(size_t(side) * rowSize);

        for (int y = 0; y < side; y++)
        {
          std::memcpy(square.data() + size_t(y) * rowSize,
                      pixels.data() + (size_t(y + y0) * size_t(width) + size_t(x0)) * g_channels,
                      rowSize);
        }

        return square;
      }

      /** A framebuffer that carries one image, which is all a read back needs. */
      FramebufferPtr MakeReadFramebuffer(const RenderTargetPtr& image)
      {
        FramebufferSettings settings;
        settings.width           = image->m_width;
        settings.height          = image->m_height;
        settings.useDefaultDepth = false;

        FramebufferPtr buffer    = MakeNewPtr<Framebuffer>(settings, "ProjectThumbnailReadFB");
        buffer->Init();
        buffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, image);

        return buffer;
      }

      /**
       * Gamma encodes an image on the GPU and returns the texture that holds the result. The input
       * is left alone: the pass works in place on the color attachment of the framebuffer it is
       * given, and the viewport image is still on screen, so the pass gets a copy of it.
       *
       * Only gamma runs. The scene render path has already tonemapped, and a thumbnail has no use
       * for FXAA on top of a downscale to ProjectThumbnailSize.
       */
      RenderTargetPtr GammaEncode(Renderer* renderer, const RenderTargetPtr& source, float gamma)
      {
        RenderTargetPtr encoded = MakeNewPtr<RenderTarget>();
        encoded->ReconstructIfNeeded(source->m_width, source->m_height, &source->Settings());
        renderer->CopyTexture(source, encoded);

        FramebufferPtr buffer                = MakeReadFramebuffer(encoded);

        GammaTonemapFxaaPassPtr pass         = MakeNewPtr<GammaTonemapFxaaPass>();
        pass->m_params.frameBuffer           = buffer;
        pass->m_params.enableGammaCorrection = true;
        pass->m_params.enableTonemapping     = false;
        pass->m_params.enableFxaa            = false;
        pass->m_params.gamma                 = gamma;
        pass->m_params.screenSize            = Vec2(float(encoded->m_width), float(encoded->m_height));

        // The three calls RenderPath::Render makes for every pass it runs.
        pass->SetRenderer(renderer);
        pass->PreRender();
        pass->Render();
        pass->PostRender();

        return encoded;
      }
    } // namespace

    void SaveProjectThumbnail()
    {
      App* app = GetApp();
      if (app == nullptr || !app->IsWorkspaceSane(true, true))
      {
        return;
      }

      // Without a project there is no thumbnail to write: the file belongs to the project folder.
      if (app->m_workspace->GetActiveProject().name.empty())
      {
        TK_ERR("SaveThumbnail: no project is open.");
        return;
      }

      if (TKVulkan != 0)
      {
        // VulkanBackend::ReadPixels is a stub, so there are no pixels to write the file from.
        TK_ERR("SaveThumbnail: reading a viewport back is not implemented for the Vulkan backend.");
        return;
      }

      EditorViewportPtr viewport = app->GetActiveViewport();
      if (viewport == nullptr)
      {
        viewport = app->GetViewport(g_3dViewport);
      }

      if (viewport == nullptr || viewport->m_renderTarget == nullptr)
      {
        TK_ERR("SaveThumbnail: no viewport to take the image from.");
        return;
      }

      // The last image the viewport was drawn into. A multisampled viewport is shown through its
      // resolved texture (see EditorViewport::GetImGuiTextureId), so that is the one to read.
      RenderTargetPtr source = viewport->m_renderTarget;
      if (source->IsMultiSampled())
      {
        RenderTargetPtr resolved = Cast<RenderTarget>(source->GetResolvedTexture());
        if (resolved == nullptr)
        {
          TK_ERR("SaveThumbnail: the viewport image is not resolved yet, run it again.");
          return;
        }

        source = resolved;
      }

      const int width  = source->m_width;
      const int height = source->m_height;
      const String projectFolder =
          ConcatPaths({app->m_workspace->GetActiveWorkspace(), app->m_workspace->GetActiveProject().name});
      const String file      = ConcatPaths({projectFolder, g_thumbnailFileName});

      // Shared with the callback: the task fills it, the callback turns it into a file.
      auto pixels            = std::make_shared<std::vector<ubyte>>(size_t(width) * size_t(height) * g_channels);

      // When the backbuffer is not an sRGB capable format the editor leaves gamma out of the scene
      // render path and lets the ImGui backend encode the whole frame (see EditorRenderer::Render),
      // so the viewport image is linear and the read back pixels need that encoding before they
      // become a file a PNG reader shows.
      const bool gammaNeeded = GetRenderSystem()->IsGammaCorrectionNeeded();
      const float gamma      = GetEngineSettings().m_postProcessing->GetGammaVal();

      RenderTask task;
      task.Task = [source, pixels, width, height, gammaNeeded, gamma](Renderer* renderer) -> void
      {
        RenderTargetPtr image     = gammaNeeded ? GammaEncode(renderer, source, gamma) : source;

        // Reading goes through the framebuffer binding and the texture the engine rendered into is
        // not attached to one at this point, so it gets a framebuffer of its own for the read.
        FramebufferPtr readBuffer = MakeReadFramebuffer(image);

        FramebufferPtr previous   = renderer->GetFrameBuffer();
        renderer->SetFramebuffer(readBuffer, GraphicBitFields::None);
        renderer->GetBackend()
            ->ReadPixels(0, 0, width, height, GraphicTypes::FormatRGBA, GraphicTypes::TypeUnsignedByte, pixels->data());
        renderer->SetFramebuffer(previous, GraphicBitFields::None);
      };

      task.Callback = [pixels, file, width, height]() -> void
      {
        // GL hands the pixels back bottom up, the PNG wants the top row first.
        FlipRows(pixels->data(), width, height);

        // The editor shows the viewport opaque (EditorRenderer fills the alpha channel), so the
        // thumbnail is opaque too instead of blending with the launcher's card.
        for (size_t i = 3; i < pixels->size(); i += g_channels)
        {
          (*pixels)[i] = 255;
        }

        int side                  = 0;
        std::vector<ubyte> square = CenterSquare(*pixels, width, height, side);
        pixels->clear();
        pixels->shrink_to_fit();

        std::vector<ubyte> thumbnail(size_t(ProjectThumbnailSize) * size_t(ProjectThumbnailSize) * g_channels);
        const int scaled = ImageResize(square.data(),
                                       side,
                                       side,
                                       side * g_channels,
                                       thumbnail.data(),
                                       ProjectThumbnailSize,
                                       ProjectThumbnailSize,
                                       ProjectThumbnailSize * g_channels,
                                       g_channels);
        if (scaled == 0)
        {
          TK_ERR("SaveThumbnail: scaling the viewport image failed.");
          return;
        }

        if (WritePNG(file,
                     ProjectThumbnailSize,
                     ProjectThumbnailSize,
                     g_channels,
                     thumbnail.data(),
                     ProjectThumbnailSize * g_channels) == 0)
        {
          TK_ERR("SaveThumbnail: writing '%s' failed.", file.c_str());
          return;
        }

        TK_LOG("Project thumbnail saved: %s", file.c_str());
      };

      // Low priority: the viewport renders in the high queue of the same frame, so the image read
      // here is the one drawn for this frame, and the file is written right after the read.
      task.Priority = RenderTaskPriority::Low;
      GetRenderSystem()->AddRenderTask(task);

      if (ConsoleWindowPtr console = app->GetConsole())
      {
        console->AddLog("Saving the project thumbnail: " + file, LogType::Memo);
      }
    }

  } // namespace Editor
} // namespace ToolKit

/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Image.h"
#include "Logger.h"
#include "Types.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>

namespace ToolKit
{

  /**
   * Cross-platform splash screen that runs on its own thread.
   * Uses plain SDL window surface (no OpenGL/Vulkan) so it can be shown
   * before the graphics context is ready.
   *
   * Usage: Include this header in excatly one cpp file and use SplashScreen class
   * Most suitable place is using this on main.cpp
   */
  class SplashScreen
  {
   public:
    inline SplashScreen(const String& splashFile, const String& fontFile, const String& infoText);
    inline ~SplashScreen();

    inline void Show(int width, int height);
    inline void SetInfoText(const String& text);
    inline void SetProgress(float percent);
    inline void Hide();
    inline bool IsVisible() const;

   private:
    inline void Run();
    inline void TriggerRedraw();

    inline bool LoadFont(const String& path);
    inline void DrawText(SDL_Surface* surf,
                         const String& text,
                         int x,
                         int y,
                         float fontSize,
                         ubyte r,
                         ubyte g,
                         ubyte b);

    std::thread m_thread;
    std::atomic<bool> m_running {false};
    std::atomic<bool> m_shouldHide {false};
    std::atomic<float> m_progress {0.0f};

    String m_splashFile;
    String m_fontFile;
    String m_infoFixedText;
    int m_width  = 0;
    int m_height = 0;

    // Image data loaded on the caller thread to avoid stbi global state races
    std::vector<ubyte> m_splashPixels;
    int m_imgW = 0;
    int m_imgH = 0;

    String m_infoText;
    mutable std::mutex m_infoMutex;

    SDL_Window* m_window = nullptr;

    std::vector<ubyte> m_fontData;
    stbtt_fontinfo* m_font = nullptr;
    bool m_fontLoaded      = false;
  };

  inline SplashScreen::SplashScreen(const String& splashFile, const String& fontFile, const String& infoText)
      : m_splashFile(splashFile), m_fontFile(fontFile), m_infoFixedText(infoText)
  {
  }

  inline SplashScreen::~SplashScreen() { Hide(); }

  inline void SplashScreen::Show(int width, int height)
  {
    if (m_running)
      return;

    // Load image on the calling thread to avoid stbi global state races
    int comp      = 0;
    ubyte* pixels = ImageLoad(m_splashFile.c_str(), &m_imgW, &m_imgH, &comp, 4);
    if (!pixels)
    {
      TK_ERR("SplashScreen: Failed to load image: %s", m_splashFile.c_str());
      return;
    }

    m_splashPixels.resize(m_imgW * m_imgH * 4);
    memcpy(m_splashPixels.data(), pixels, m_splashPixels.size());
    ImageFree(pixels);

    m_width      = width;
    m_height     = height;
    m_shouldHide = false;
    m_progress   = 0.0f;
    m_running    = true;

    m_thread     = std::thread(&SplashScreen::Run, this);
  }

  inline void SplashScreen::SetInfoText(const String& text)
  {
    {
      std::lock_guard<std::mutex> lock(m_infoMutex);
      m_infoText = text;
    }
    TriggerRedraw();
  }

  inline void SplashScreen::SetProgress(float percent)
  {
    float p = percent;
    if (p < 0.0f)
      p = 0.0f;
    if (p > 100.0f)
      p = 100.0f;
    m_progress = p;
    TriggerRedraw();
  }

  inline void SplashScreen::Hide()
  {
    if (!m_running)
    {
      return;
    }

    m_shouldHide = true;
    TriggerRedraw();

    if (m_thread.joinable())
    {
      m_thread.join();
    }
  }

  inline bool SplashScreen::IsVisible() const { return m_running && !m_shouldHide; }

  inline void SplashScreen::TriggerRedraw()
  {
    SDL_Event e  = {};
    e.type       = SDL_USEREVENT;
    e.user.code  = 0;
    e.user.data1 = this;
    SDL_PushEvent(&e);
  }

  inline bool SplashScreen::LoadFont(const String& path)
  {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
      return false;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    m_fontData.resize(size);
    fread(m_fontData.data(), 1, size, fp);
    fclose(fp);

    if (!m_font)
      m_font = new stbtt_fontinfo();

    if (!stbtt_InitFont(m_font, m_fontData.data(), stbtt_GetFontOffsetForIndex(m_fontData.data(), 0)))
    {
      m_fontData.clear();
      return false;
    }

    m_fontLoaded = true;
    return true;
  }

  inline void SplashScreen::DrawText(SDL_Surface* surf,
                                     const String& text,
                                     int x,
                                     int y,
                                     float fontSize,
                                     ubyte r,
                                     ubyte g,
                                     ubyte b)
  {
    if (!m_fontLoaded || text.empty() || !surf)
      return;

    float scale = stbtt_ScaleForPixelHeight(m_font, fontSize);

    int cursorX = x;
    for (size_t i = 0; i < text.size(); ++i)
    {
      int c = text[i];
      int w, h, xoff, yoff;
      unsigned char* bitmap = stbtt_GetCodepointBitmap(m_font, scale, scale, c, &w, &h, &xoff, &yoff);
      if (bitmap)
      {
        int startX = cursorX + xoff;
        int startY = y + yoff;

        for (int row = 0; row < h; ++row)
        {
          for (int col = 0; col < w; ++col)
          {
            unsigned char alpha = bitmap[row * w + col];
            if (alpha == 0)
              continue;

            int px = startX + col;
            int py = startY + row;
            if (px < 0 || px >= surf->w || py < 0 || py >= surf->h)
              continue;

            Uint32* pixel = (Uint32*) ((Uint8*) surf->pixels + py * surf->pitch + px * 4);
            ubyte pr, pg, pb, pa;
            SDL_GetRGBA(*pixel, surf->format, &pr, &pg, &pb, &pa);

            float a  = alpha / 255.0f;
            ubyte nr = (ubyte) (r * a + pr * (1.0f - a));
            ubyte ng = (ubyte) (g * a + pg * (1.0f - a));
            ubyte nb = (ubyte) (b * a + pb * (1.0f - a));

            *pixel   = SDL_MapRGBA(surf->format, nr, ng, nb, 255);
          }
        }
        stbtt_FreeBitmap(bitmap, nullptr);
      }

      int advance, lsb;
      stbtt_GetCodepointHMetrics(m_font, c, &advance, &lsb);
      cursorX += (int) (advance * scale);

      if (i + 1 < text.size())
      {
        int kern  = stbtt_GetCodepointKernAdvance(m_font, c, text[i + 1]);
        cursorX  += (int) (kern * scale);
      }
    }
  }

  inline void SplashScreen::Run()
  {
    m_window = SDL_CreateWindow("ToolKit",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                m_width,
                                m_height,
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR);

    if (!m_window)
    {
      TK_ERR("SplashScreen: Failed to create window: %s", SDL_GetError());
      m_running = false;
      return;
    }

    if (m_splashPixels.empty())
    {
      TK_ERR("SplashScreen: No image data available");
      SDL_DestroyWindow(m_window);
      m_window  = nullptr;
      m_running = false;
      return;
    }

    SDL_Surface* imgSurf = SDL_CreateRGBSurfaceWithFormatFrom(m_splashPixels.data(),
                                                              m_imgW,
                                                              m_imgH,
                                                              32,
                                                              m_imgW * 4,
                                                              SDL_PIXELFORMAT_RGBA32);
    if (!imgSurf)
    {
      TK_ERR("SplashScreen: Failed to create surface: %s", SDL_GetError());
      SDL_DestroyWindow(m_window);
      m_window  = nullptr;
      m_running = false;
      return;
    }

    SDL_Surface* winSurf = SDL_GetWindowSurface(m_window);
    if (!winSurf)
    {
      TK_ERR("SplashScreen: Failed to get window surface: %s", SDL_GetError());
      SDL_FreeSurface(imgSurf);
      SDL_DestroyWindow(m_window);
      m_window  = nullptr;
      m_running = false;
      return;
    }

    // Load font if provided
    if (!m_fontLoaded && !m_fontFile.empty())
    {
      LoadFont(m_fontFile);
    }

    while (m_running && !m_shouldHide)
    {
      SDL_Event e;
      while (SDL_PollEvent(&e))
      {
        if (e.type == SDL_QUIT)
        {
          m_shouldHide = true;
        }
      }

      // Dark background
      SDL_FillRect(winSurf, nullptr, SDL_MapRGB(winSurf->format, 20, 20, 20));

      // Center image
      SDL_Rect dstRect;
      dstRect.w = m_imgW;
      dstRect.h = m_imgH;
      dstRect.x = (m_width - m_imgW) / 2;
      dstRect.y = (m_height - m_imgH) / 2;
      SDL_BlitSurface(imgSurf, nullptr, winSurf, &dstRect);

      // Progress bar
      float progress = m_progress.load();
      if (progress > 0.0f)
      {
        const int barH = 6;
        const int padX = 40;
        const int barY = m_height - 30;

        SDL_Rect bg    = {padX, barY, m_width - padX * 2, barH};
        SDL_FillRect(winSurf, &bg, SDL_MapRGB(winSurf->format, 40, 40, 40));

        int fillW = (int) ((bg.w - 2) * (progress / 100.0f));
        if (fillW < 0)
          fillW = 0;
        SDL_Rect fg = {padX + 1, barY + 1, fillW, barH - 2};
        SDL_FillRect(winSurf, &fg, SDL_MapRGB(winSurf->format, 0, 180, 90));
      }

      // Info text
      {
        std::lock_guard<std::mutex> lock(m_infoMutex);
        if (!m_infoText.empty())
        {
          SDL_SetWindowTitle(m_window, m_infoText.c_str());

          if (m_fontLoaded)
          {
            float fontSize = 16.0f;
            float scale    = stbtt_ScaleForPixelHeight(m_font, fontSize);

            int ascent, descent, lineGap;
            stbtt_GetFontVMetrics(m_font, &ascent, &descent, &lineGap);
            int baseline  = (int) (ascent * scale);

            int textWidth = 0;
            for (size_t i = 0; i < m_infoText.size(); ++i)
            {
              int c = m_infoText[i];
              int advance, lsb;
              stbtt_GetCodepointHMetrics(m_font, c, &advance, &lsb);
              textWidth += (int) (advance * scale);
              if (i + 1 < m_infoText.size())
              {
                int kern   = stbtt_GetCodepointKernAdvance(m_font, c, m_infoText[i + 1]);
                textWidth += (int) (kern * scale);
              }
            }

            int textX = (m_width - textWidth) / 2;
            int textY = m_height - 48; // just above progress bar

            DrawText(winSurf, m_infoText, textX, textY - baseline, fontSize, 255, 255, 255);
          }
        }
      }

      // Fixed info text at the bottom
      if (m_fontLoaded && !m_infoFixedText.empty())
      {
        float fontSize = 16.0f;
        float scale    = stbtt_ScaleForPixelHeight(m_font, fontSize);

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(m_font, &ascent, &descent, &lineGap);
        int baseline  = (int) (ascent * scale);

        int textWidth = 0;
        for (size_t i = 0; i < m_infoFixedText.size(); ++i)
        {
          int c = m_infoFixedText[i];
          int advance, lsb;
          stbtt_GetCodepointHMetrics(m_font, c, &advance, &lsb);
          textWidth += (int) (advance * scale);
          if (i + 1 < m_infoFixedText.size())
          {
            int kern   = stbtt_GetCodepointKernAdvance(m_font, c, m_infoFixedText[i + 1]);
            textWidth += (int) (kern * scale);
          }
        }

        int textX = 40;                       // left aligned with progress bar padding
        int textY = m_height - 40 + baseline; // flush under progress bar

        DrawText(winSurf, m_infoFixedText, textX, textY - baseline, fontSize, 160, 160, 160);
      }

      SDL_UpdateWindowSurface(m_window);
      SDL_Delay(33); // ~30 FPS is enough for splash
    }

    SDL_FreeSurface(imgSurf);
    SDL_DestroyWindow(m_window);
    m_window  = nullptr;
    m_running = false;

    delete m_font;
    m_font       = nullptr;
    m_fontLoaded = false;
    m_fontData.clear();
    m_splashPixels.clear();
    m_imgW = 0;
    m_imgH = 0;
  }

} // namespace ToolKit

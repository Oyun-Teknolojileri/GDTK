/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "EngineSettings.h"
#include "Game.h"
#include "Logger.h"
#include "SDL.h"
#include "Util.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <iostream>

// Info, not debug: a phone sets its own minimum log priority, and an OEM image that pins it to Info
// drops every DEBUG line, which took the whole TK_LOG stream with it (measured on a TECNO CL7,
// Android 16, where log.tag and persist.log.tag are both "I"). Info clears that floor; the engine's
// own level filter (SetLogLevel) is a separate gate and still applies.
#define ANDROID_LOG(format, ...) __android_log_print(ANDROID_LOG_INFO, "TK_LOG", format, ##__VA_ARGS__)

#define PLATFORM_SDL_FLAGS       (SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN)

#define TK_PLATFORM              PLATFORM::TKAndroid

namespace ToolKit
{

  AAssetManager* assetManager = nullptr;

  /**
   * Copies one asset into the app's data folder. Returns true when the file is in place afterwards,
   * whether it was copied now or already there.
   *
   * The copy streams in 1 MB chunks. It used to read the whole asset into one buffer, which was fine
   * for the 15 MB pak this started with and would not be for a resource pak of a gigabyte: a 1:1
   * memory cost on a phone that cannot pay it. AAsset_read also returns short reads like read(2), so
   * the loop is what makes a large copy complete instead of silently short.
   */
  inline bool CopyFileToDataPath(String& internalDataPath, const String& file, bool overwrite = false)
  {
    String destinationPath = ConcatPaths({internalDataPath, file});

    // An existing file is normally kept: the pak is large and copying it on every start would be a
    // visible stall. The settings are different: they change with every build, and a stale one from
    // an older format parses to an empty document, so the caller asks for those to be refreshed.
    if (!overwrite && access(destinationPath.c_str(), F_OK) != -1)
    {
      ANDROID_LOG("File %s already exists, skipping copy.\n", file.c_str());
      return true;
    }

    AAsset* asset = AAssetManager_open(assetManager, file.c_str(), 0);

    if (!asset)
    {
      ANDROID_LOG("Cannot open %s!\n", file.c_str());
      return false;
    }

    FILE* fileHandle = fopen(destinationPath.c_str(), "wb");
    if (!fileHandle)
    {
      ANDROID_LOG("Cannot create file %s!\n", destinationPath.c_str());
      AAsset_close(asset);
      return false;
    }

    constexpr size_t ChunkSize = 1 << 20;
    std::vector<char> buffer(ChunkSize);
    off_t remaining = AAsset_getLength(asset);
    bool complete   = true;

    while (remaining > 0)
    {
      const off_t want = remaining < (off_t) ChunkSize ? remaining : (off_t) ChunkSize;
      const int read   = AAsset_read(asset, buffer.data(), (size_t) want);
      if (read <= 0)
      {
        complete = false;
        break;
      }

      if (fwrite(buffer.data(), 1, (size_t) read, fileHandle) != (size_t) read)
      {
        complete = false;
        break;
      }

      remaining -= read;
    }

    fclose(fileHandle);
    AAsset_close(asset);

    if (!complete)
    {
      // A half written file would be worse than none: the caller's fingerprint check would think the
      // pak is current and never copy it again.
      ANDROID_LOG("Copy of %s did not complete, removing it.\n", file.c_str());
      unlink(destinationPath.c_str());
      return false;
    }

    ANDROID_LOG("File %s copied successfully.\n", file.c_str());
    return true;
  }

  /**
   * Reads a whole asset as text, or returns an empty string when it is not in the APK.
   */
  inline String ReadAssetText(const String& file)
  {
    AAsset* asset = AAssetManager_open(assetManager, file.c_str(), 0);
    if (!asset)
    {
      return "";
    }

    const off_t size = AAsset_getLength(asset);
    String text;
    if (size > 0)
    {
      text.resize((size_t) size);
      AAsset_read(asset, &text[0], size);
    }
    AAsset_close(asset);

    // The file is a single token; a trailing newline would make two equal fingerprints compare
    // unequal.
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
    {
      text.pop_back();
    }

    return text;
  }

  /**
   * Unpacks the resource pak when the APK ships a different one than the copy already on the device.
   *
   * Skipping the copy on "the file exists" pins the first pak a package name ever wrote: every later
   * publish ships a new pak in the assets and the app keeps rendering the old scene. The pak carries
   * no self describing version, so the Packer writes <pak>.hash - the fingerprint of the resource
   * tree it packed - and ships it next to the pak; the unpacked copy keeps the fingerprint it came
   * from. Comparing two 16 character strings costs nothing next to a 15 MB copy, and hashing the pak
   * at every start would cost more than the copy saves.
   */
  inline void CopyPakToDataPath(String& internalDataPath)
  {
    const String pakFile     = TKResourcePak;
    const String assetHash   = ReadAssetText(pakFile + ".hash");
    const String destination = ConcatPaths({internalDataPath, pakFile});
    const String hashPath    = ConcatPaths({internalDataPath, pakFile + ".hash"});

    String installedHash;
    if (FILE* hashFile = fopen(hashPath.c_str(), "r"))
    {
      char buffer[64] = {};
      if (fgets(buffer, sizeof(buffer), hashFile) != nullptr)
      {
        installedHash = String(buffer);
      }
      fclose(hashFile);
      while (!installedHash.empty() &&
             (installedHash.back() == '\n' || installedHash.back() == '\r' || installedHash.back() == ' '))
      {
        installedHash.pop_back();
      }
    }

    const bool havePak  = access(destination.c_str(), F_OK) != -1;
    const bool upToDate = havePak && !assetHash.empty() && installedHash == assetHash;
    if (upToDate)
    {
      ANDROID_LOG("Pak %s is current (%s), skipping copy.\n", pakFile.c_str(), assetHash.c_str());
      return;
    }

    ANDROID_LOG("Pak %s is stale or missing (have '%s', shipped '%s'), copying.\n",
                pakFile.c_str(),
                installedHash.c_str(),
                assetHash.c_str());

    // Only after a copy that completed: a fingerprint without the pak it describes - or beside a
    // half written one - would make every later start believe it is current.
    const bool copied = CopyFileToDataPath(internalDataPath, pakFile, true);
    if (copied && !assetHash.empty())
    {
      if (FILE* hashFile = fopen(hashPath.c_str(), "w"))
      {
        fwrite(assetHash.c_str(), 1, assetHash.size(), hashFile);
        fclose(hashFile);
      }
    }
  }

  // copies all of the engine assets to internal data folder if already not copied
  inline void CopyAllAssetsToDataPath(String& internalDataPath)
  {
    mkdir(ConcatPaths({internalDataPath, "Resources"}).c_str(), 0777);
    mkdir(ConcatPaths({internalDataPath, "Config"}).c_str(), 0777);

    CopyPakToDataPath(internalDataPath);
    CopyFileToDataPath(internalDataPath, ConcatPaths({"Config", "Engine.settings"}), true);
  }

  inline void PlatformPreInit(Main* g_proxy)
  {
    String internalPath     = SDL_AndroidGetInternalStoragePath();

    g_proxy->m_resourceRoot = ConcatPaths({internalPath, "Resources"});
    g_proxy->m_cfgPath      = ConcatPaths({internalPath, "Config"});

    // Set log function
    GetLogger()->SetPlatformConsoleFn([](LogType lt, String ms) -> void { ANDROID_LOG("%s", ms.c_str()); });

    CopyAllAssetsToDataPath(internalPath);
  }

  inline void PlatformMainLoop(bool* running, void (*TK_Loop)())
  {
    while (*running)
    {
      TK_Loop();
    }
  }

  inline void PlatformAdjustEngineSettings(int availableWidth, int availableHeight, EngineSettings* engineSettings)
  {
    engineSettings->m_window->SetWidthVal((uint) availableWidth);
    engineSettings->m_window->SetHeightVal((uint) availableHeight);
  }

} // namespace ToolKit

extern "C"
{
  JNIEXPORT void JNICALL Java_com_otyazilim_toolkit_ToolKitActivity_load(JNIEnv* env, jclass clazz, jobject mgr)
  {
    ToolKit::assetManager = AAssetManager_fromJava(env, mgr);
    if (ToolKit::assetManager == nullptr)
    {
      __android_log_print(ANDROID_LOG_ERROR, "ToolKit_Android", "error loading asset manager");
    }
    else
    {
      __android_log_print(ANDROID_LOG_VERBOSE, "ToolKit_Android", "Asset manager loaded successfully");
    }
  }
}

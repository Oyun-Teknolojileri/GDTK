/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file GLProgramCache.h Android only: keeps linked GL programs on disk across starts.
 *
 * Compiling and linking the engine's programs is the slowest part of the first frames on a device:
 * on the release build of the city scene it is around half a minute on an emulator, and a real
 * phone spends seconds on it every start. GL has no shader cache of its own, but a linked program
 * can be read back and restored (`GL_OES_get_program_binary`), which skips the whole compile.
 *
 * Desktop and web do not use this: their boot is not dominated by shader compilation, and WebGL has
 * no program binaries at all.
 *
 * The format is driver specific, so a file carries the driver identity it was written on and is
 * rejected when that changes; the key covers the shader sources, so a shader edit misses on its own.
 */

#include <Types.h>

namespace ToolKit
{


  /**
   * Hashes the driver identity a binary was produced on: vendor, renderer and version strings.
   * A binary from another driver, or another version of the same driver, is not loadable.
   */
  uint64 HashDriverIdentity();

  /** Returns the file a program is cached in, creating the cache directory when it is missing. */
  String ProgramCachePath(uint64 key);

  /**
   * Reads a cached program.
   * @param key is the hash of the program's sources.
   * @param format receives the GL binary format the driver reported when the program was saved.
   * @param binary receives the bytes of the program.
   * @return True when a binary for this key and driver exists.
   */
  bool ReadProgramBinary(uint64 key, unsigned int& format, std::vector<unsigned char>& binary);

  /**
   * Writes a program to the cache. An existing file for the same key is left alone: the key already
   * identifies the sources, so rewriting it would only repeat identical work.
   */
  void WriteProgramBinary(uint64 key,
                          unsigned int format,
                          const void* binary,
                          size_t binarySize);

  /**
   * Appends a line to <ShaderCache>/report.txt. A phone's logcat does not always carry the app's
   * lines, so the cache records what it did next to the binaries, where adb can pull it.
   */
  void ReportProgramCacheEvent(const char* message);

  /** Removes every cached program. Called when the shaders are reloaded. */
  void ClearProgramCache();

} // namespace ToolKit

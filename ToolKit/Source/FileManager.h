/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "TextureCompression.h"
#include "Types.h"

namespace ToolKit
{

  /**
   * Maintain access to resources. This class can work either on Pak files, the resources zipped in "MinResources.pak"
   * or files that resides in Resources folder of the current project. Any access to resources should use FileManager in
   * the Main to keep its cross platform capabilities effective in all platforms.
   */
  class TK_API FileManager
  {
   public:
    /** What the pack step did to the textures it met, for the tool's summary line. */
    struct PackCompressionStats
    {
      int Encoded     = 0;
      int Failed      = 0;
      uint64 InputBytes  = 0;
      uint64 OutputBytes = 0;
    };

    FileManager();  //!< Empty constructor.
    ~FileManager(); //!< Closes pak file if any open.

    /** Returns an xml file. */
    XmlFilePtr GetXmlFile(const String& filePath);

    /** Returns an image raw data and its properties in the arguments. Used in Texture::Load to create resource. */
    uint8* GetImageFile(const String& filePath, int* x, int* y, int* comp, int reqComp);

    /** Returns a hdri image raw data and its properties in the arguments. Used in Texture::Load to create resource. */
    float* GetHdriFile(const String& filePath, int* x, int* y, int* comp, int reqComp);

    /**
     * Reads a resource as raw bytes: from the open pak when there is one, from disk otherwise.
     *
     * The image accessors above hand the file straight to stb_image, which cannot help with a
     * container the engine parses itself (KTX2). Going through one byte reader keeps the pak and the
     * loose-file paths identical for those.
     *
     * @param filePath Resource path, absolute or relative to the resource root.
     * @param out Receives the file contents. Untouched when the function returns false.
     * @return False when the file is missing on both the pak and the disk.
     */
    bool ReadFileBuffer(const String& filePath, std::vector<ubyte>& out);

    /** Returns a decoded audio file or null if no decoder found. Used in Audio::Load to create resource. */
    SoundBuffer GetAudioFile(const String& filePath);

    /**
     * Pack all the resources for the project.
     * Does this by opening all scene and layer files in resource folder.
     * Than accumulate all resources in all managers. Finally creates a zip file from the collected resources.
     * Produced zip file is called "MinResources.pak"
     * If extra files other than automatically collected ones are needed, the function looks for a text file
     * "ExtraFiles.txt" each line in this file is added to the pack as well.
     * All files must be in the Resources folder of the project.
     */
    int PackResources();

    /**
     * Turns on texture compression for the next PackResources call.
     *
     * Every image the pack meets that is not already a KTX2 container (and not HDRI, and not one of
     * the engine's own resources) is encoded and stored under its own entry name, so the material
     * references inside the pak keep pointing at it. The runtime sniffs the container, not the
     * extension, which is what makes that safe.
     */
    void SetPackCompressionOptions(const TextureCompressionOptions& options);

    /** Packs images untouched. The pre KTX2 behaviour, used by Packer's --no-compress. */
    void DisablePackCompression();

    /** What the last pack did to the textures it met. */
    PackCompressionStats GetPackCompressionStats() const;

    bool CheckFileFromResources(const String& path); //!< Checks the given file in first resource path than in pak file.
    void GetRelativeResourcesPath(String& path);     //!< Converts the path, relative to the Resources folder.

    void CloseZipFile(); //!< Closes the pak file if its open.
    bool CheckPakFile(); //!< Returns true if workspace contains pak file.

    String ReadAllText(const String& file); //!< Read the text content of the file. Zip file not supported.
    void WriteAllText(const String& file, const String& text); //!< Write the text to the file. Zip file not supported.

    /** Creates a folder in the resource path. */
    void CreateResourceFolder(const String& folder);

   private:
    typedef std::variant<XmlFilePtr, uint8*, float*, SoundBuffer> FileDataType;

    enum class FileType
    {
      Xml,
      ImageUint8,
      ImageFloat,
      Audio
    };

    struct ImageFileInfo
    {
      String& filePath;
      int* x;
      int* y;
      int* comp;
      int reqComp;
    };

    FileDataType GetFile(FileType fileType, ImageFileInfo& fileInfo);
    void LoadAllScenes(const String& path);
    void GetAllUsedResourcePaths();

    bool ZipPack(const String& zipName);
    bool AddFileToZip(ZipFile zfile, const char* filename);

    void GetAllPaths(const String& path);
    void GetExtraFilePaths();

    XmlFilePtr ReadXmlFileFromZip(ZipFile zfile, const String& relativePath, const char* path);
    uint8* ReadImageFileFromZip(ZipFile zfile, const String& relativePath, ImageFileInfo& fileInfo);
    float* ReadHdriFileFromZip(ZipFile zfile, const String& relativePath, ImageFileInfo& fileInfo);

    /** Reads the file from the zip and returns it as buffer pointer and set the buffer size. */
    ubyte* ReadFileBufferFromZip(ZipFile zfile, const String& relativePath, uint& bufferSize);

    XmlFilePtr CreateXmlFileFromZip(ZipFile zfile, const String& filename, uint filesize);
    uint8* CreateImageFileFromZip(ZipFile zfile, uint filesize, ImageFileInfo& fileInfo);
    float* CreateHdriFileFromZip(ZipFile zfile, uint filesize, ImageFileInfo& fileInfo);

    void GenerateOffsetTableForPakFiles();
    bool IsFileInPak(const String& filename);

    /** True when the pack step should encode this file instead of storing it as it is. */
    bool ShouldCompressForPack(const String& filename);

    /** Size on disk, for the pack summary. */
    uint64 GetFileSize(const String& filename);

   private:
    StringSet m_allPaths;
    std::unordered_map<String, std::pair<uint64, uint>> m_zipFilesOffsetTable;
    bool m_offsetTableCreated = false;
    ZipFile m_zfile           = nullptr;

    /** Pack step texture compression (see SetPackCompressionOptions). */
    TextureCompressionOptions m_packCompression;
    bool m_compressTexturesInPack = false;
    PackCompressionStats m_packStats;

   public:
    bool m_ignorePakFile = false;
  };
} // namespace ToolKit

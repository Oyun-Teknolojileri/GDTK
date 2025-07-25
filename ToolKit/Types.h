/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

/**
 * @file Forward declerations for frequenty used common ToolKit classes and
 * relatled structures.
 */

#include "Platform.h"

// GLM
#ifndef TK_GLM
  #define GLM_FORCE_QUAT_DATA_XYZW
  #define GLM_FORCE_CTOR_INIT
  #define GLM_ENABLE_EXPERIMENTAL
  #define GLM_FORCE_ALIGNED_GENTYPES
  #define GLM_FORCE_INTRINSICS
#endif

#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/scalar_relational.hpp>

// RapidXml
#include <RapidXml/rapidxml_ext.h>
#include <RapidXml/rapidxml_utils.hpp>

// STL
#include <assert.h>

#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#define SafeDel(ptr)                                                                                                   \
  {                                                                                                                    \
    delete ptr;                                                                                                        \
    ptr = nullptr;                                                                                                     \
  }
#define SafeDelArray(ptr)                                                                                              \
  {                                                                                                                    \
    delete[] ptr;                                                                                                      \
    ptr = nullptr;                                                                                                     \
  }

#define TKStringify(x) #x
#define TKToString(x)  TKStringify(x)
#define TKLoc          __FILE__ ":" TKToString(__LINE__)

// std template types require dll interface.
#pragma warning(disable : 4251)

/// @cond
namespace rapidxml
{
  template <class Ch>
  class xml_document;
  template <class Ch>
  class xml_node;
  template <class Ch>
  class xml_attribute;
  template <class Ch>
  class file;
} // namespace rapidxml

/// @endcond

namespace ToolKit
{

  // Primitive types.
  typedef char byte;
  typedef unsigned char ubyte;
  typedef std::vector<byte> ByteArray;
  typedef uint8_t uint8;
  typedef uint16_t uint16;
  typedef uint32_t uint;
  typedef uint64_t uint64;
  typedef int32_t int32;
  typedef int64_t int64;
  typedef float Real; // Floating point type with adjustable precision. Can be set as double or float.
  typedef uint64 ObjectId;
  typedef std::vector<ObjectId> IDArray;
  typedef const int16_t SignalId;
  typedef std::string String;
  typedef std::string_view StringView;
  typedef std::vector<String> StringArray;
  typedef std::set<std::string> StringSet;
  typedef glm::ivec2 IVec2;
  typedef glm::vec2 Vec2;
  typedef std::vector<Vec2> Vec2Array;
  typedef glm::vec3 Vec3;
  typedef std::vector<Vec3> Vec3Array;
  typedef glm::vec4 Vec4;
  typedef std::vector<Vec4> Vec4Array;
  typedef glm::ivec4 IVec4;
  typedef glm::uvec4 UVec4;
  typedef glm::uvec2 UVec2;
  typedef glm::mat4 Mat4;
  typedef std::vector<Mat4> Mat4Array;
  typedef glm::mat3 Mat3;
  typedef glm::quat Quaternion;
  typedef std::vector<int> IntArray;
  typedef std::vector<uint> UIntArray;
  typedef std::vector<bool> BoolArray;
  typedef std::vector<struct VariantCategory> VariantCategoryArray;
  typedef std::vector<struct RenderJob> RenderJobArray;
  typedef void* SoundBuffer; //!< Internal sound buffer object used for decoding / loading audio.
  typedef void* ZipFile;
  typedef std::mutex Mutex;
  typedef std::lock_guard<std::mutex> LockGuard;

  // Resource types.
  typedef std::shared_ptr<class Animation> AnimationPtr;
  typedef std::shared_ptr<class Material> MaterialPtr;
  typedef std::weak_ptr<class Material> MaterialWeakPtr;
  typedef std::vector<MaterialPtr> MaterialPtrArray;
  typedef std::shared_ptr<class CubeMap> CubeMapPtr;
  typedef std::shared_ptr<class Texture> TexturePtr;
  typedef std::shared_ptr<class DataTexture> DataTexturePtr;
  typedef std::shared_ptr<class DepthTexture> DepthTexturePtr;
  typedef std::shared_ptr<class Hdri> HdriPtr;
  typedef std::shared_ptr<class RenderTarget> RenderTargetPtr;
  typedef std::shared_ptr<class Framebuffer> FramebufferPtr;
  typedef std::shared_ptr<class SpriteSheet> SpriteSheetPtr;
  typedef std::shared_ptr<class Mesh> MeshPtr;
  typedef std::shared_ptr<class Skeleton> SkeletonPtr;
  typedef std::shared_ptr<class SkeletonComponent> SkeletonComponentPtr;
  typedef std::shared_ptr<class DynamicBoneMap> DynamicBoneMapPtr;
  typedef std::shared_ptr<class Shader> ShaderPtr;
  typedef std::vector<ShaderPtr> ShaderPtrArray;
  typedef std::shared_ptr<class GpuProgram> GpuProgramPtr;
  typedef std::shared_ptr<class SkinMesh> SkinMeshPtr;
  typedef std::shared_ptr<class Scene> ScenePtr;
  typedef std::weak_ptr<class Scene> SceneWeakPtr;
  typedef std::vector<MeshPtr> MeshPtrArray;
  typedef std::vector<class Mesh*> MeshRawPtrArray;
  typedef std::shared_ptr<class AnimRecord> AnimRecordPtr;
  typedef std::unordered_map<String, AnimRecordPtr> AnimRecordPtrMap;
  typedef class AnimRecord* AnimRecordRawPtr;
  typedef std::vector<AnimRecordRawPtr> AnimRecordRawPtrArray;
  typedef std::vector<AnimRecordPtr> AnimRecordPtrArray;
  typedef std::shared_ptr<class ForwardSceneRenderPath> SceneRenderPathPtr;

  // Entity types.
  typedef std::shared_ptr<class AudioSource> AudioSourcePtr;
  typedef std::shared_ptr<class Entity> EntityPtr;
  typedef std::weak_ptr<class Entity> EntityWeakPtr;
  typedef std::vector<EntityPtr> EntityPtrArray;
  typedef std::vector<class Entity*> EntityRawPtrArray;
  typedef std::vector<class Light*> LightRawPtrArray;
  typedef std::shared_ptr<class Light> LightPtr;
  typedef std::vector<LightPtr> LightPtrArray;
  typedef std::weak_ptr<Light> LightWeakPtr;
  typedef std::vector<LightWeakPtr> LightWeakPtrArray;
  typedef std::vector<class DirectionalLight*> DirectionalLightRawPtrArray;
  typedef std::vector<class SpotLight*> SpotLightRawPtrArray;
  typedef std::vector<class PointLight*> PointLightRawPtrArray;
  typedef std::vector<class Node*> NodeRawPtrArray;
  typedef std::vector<class Vertex> VertexArray;
  typedef std::vector<class Face> FaceArray;
  typedef std::vector<class ParameterVariant> ParameterVariantArray;
  typedef std::vector<class ParameterVariant*> ParameterVariantRawPtrArray;
  typedef std::shared_ptr<class LineBatch> LineBatchPtr;
  typedef std::vector<class LineBatch*> LineBatchRawPtrArray;
  typedef std::vector<LineBatchPtr> LineBatchPtrArray;
  typedef std::shared_ptr<class SkyBase> SkyBasePtr;
  typedef std::weak_ptr<class SkyBase> SkyBaseWeakPtr;
  typedef std::shared_ptr<class Sky> SkyPtr;
  typedef std::weak_ptr<class Sky> SkyWeakPtr;
  typedef std::shared_ptr<class GradientSky> GradientSkyPtr;
  typedef std::weak_ptr<class GradientSky> GradientSkyWeakPtr;
  typedef std::shared_ptr<class Prefab> PrefabPtr;
  typedef std::vector<PrefabPtr> PrefabPtrArray;
  typedef std::vector<class Prefab*> PrefabRawPtrArray;
  typedef std::shared_ptr<class Canvas> CanvasPtr;
  typedef std::shared_ptr<class Billboard> BillboardPtr;
  typedef std::shared_ptr<class Camera> CameraPtr;
  typedef std::vector<CameraPtr> CameraPtrArray;
  typedef std::shared_ptr<class Surface> SurfacePtr;
  typedef std::shared_ptr<class Dpad> DpadPtr;
  typedef std::shared_ptr<class GammaTonemapFxaaPass> GammaTonemapFxaaPassPtr;

  // Xml types.
  typedef rapidxml::xml_node<char> XmlNode;
  typedef rapidxml::xml_attribute<char> XmlAttribute;
  typedef rapidxml::xml_document<char> XmlDocument;
  typedef std::shared_ptr<XmlDocument> XmlDocumentPtr;
  typedef rapidxml::file<char> XmlFile;
  typedef std::shared_ptr<XmlFile> XmlFilePtr;

  struct XmlDocBundle
  {
    XmlDocumentPtr doc;
    XmlFilePtr file;
  };

  // System Types
  typedef std::vector<class Event*> EventPool;
  typedef std::shared_ptr<class Viewport> ViewportPtr;
  typedef std::vector<ViewportPtr> ViewportPtrArray;

  // File system variable types
  typedef std::filesystem::path Path;

  // Callbacks.
  typedef std::function<void(class Event*, EntityPtr)> SurfaceEventCallback;
  typedef std::function<void(const String&)> GlReportCallback;

  // Math Vector decelerations.
  static const Vec3 ZERO    = Vec3(0.0f);
  static const Vec3 X_AXIS  = Vec3(1.0f, 0.0f, 0.0f);
  static const Vec3 Y_AXIS  = Vec3(0.0f, 1.0f, 0.0f);
  static const Vec3 Z_AXIS  = Vec3(0.0f, 0.0f, 1.0f);
  static const Vec3 XY_AXIS = Vec3(1.0f, 1.0f, 0.0f);
  static const Vec3 YZ_AXIS = Vec3(0.0f, 1.0f, 1.0f);
  static const Vec3 ZX_AXIS = Vec3(1.0f, 0.0f, 1.0f);
  static const Vec3 AXIS[6] = {X_AXIS, Y_AXIS, Z_AXIS, XY_AXIS, YZ_AXIS, ZX_AXIS};
  struct BoundingBox;

  // Supported file formats.
  static const String FBX(".fbx");
  static const String GLTF(".gltf");
  static const String GLB(".glb");
  static const String OBJ(".obj");
  static const String PNG(".png");
  static const String JPG(".jpg");
  static const String JPEG(".jpeg");
  static const String TGA(".tga");
  static const String BMP(".bmp");
  static const String PSD(".psd");
  static const String HDR(".hdr");
  static const String WAW(".waw");
  static const String MP3(".mp3");

  // Local formats.
  static const String SCENE(".scene");
  static const String MESH(".mesh");
  static const String ANIM(".anim");
  static const String SKINMESH(".skinMesh");
  static const String SKELETON(".skeleton");
  static const String MATERIAL(".material");
  static const String SHADER(".shader");
  static const String AUDIO(".wav");
  static const String LAYER(".layer");

  static const ObjectId NullHandle    = 0;  //!< Used for uninitialized handles.
  static const ObjectId InvalidHandle = -1; //!< Used for invalid handles.

  // Xml file IO.
  static const String XmlEntityElement("E");
  static const String XmlEntityIdAttr("i");
  static const String XmlParentEntityIdAttr("pi");
  static const String XmlEntityNameAttr("n");
  static const String XmlEntityTagAttr("ta");
  static const String XmlEntityTypeAttr("t");
  static const String XmlEntityVisAttr("vi");
  static const String XmlEntityTrLockAttr("lc");
  static const String XmlSceneElement("S");
  static const String XmlParamterElement("P");
  static const String XmlParamterValAttr("v");
  static const String XmlParamterTypeAttr("t");
  static const String XmlParamBlockElement("PB");
  static const String XmlMeshElement("M");
  static const String XmlFileAttr("f");
  static const String XmlNodeElement("N");
  static const String XmlNodeIdAttr("i");
  static const String XmlNodeParentIdAttr("pi");
  static const String XmlNodeInheritScaleAttr("is");
  static const String XmlTranslateElement("T");
  static const String XmlRotateElement("R");
  static const String XmlScaleElement("S");
  static const String XmlResRefElement("ResourceRef");
  static const String XmlComponent("Component");
  static const StringView XmlNodeSettings("Settings");
  static const StringView XmlNodeName("name");

  // V > 0.4.4
  static const StringView XmlObjectClassAttr("Cl");
  static const StringView XmlObjectElement("Ob");
  static const StringView XmlObjectIdAttr("i");
  static const StringView XmlComponentArrayElement("Ca");
  static const StringView XmlVersion("version");

  enum class AxisLabel
  {
    None = -1, // Order matters. Don't change.
    X    = 0,
    Y    = 1,
    Z    = 2,
    // Mod3 gives plane normal .
    YZ   = 3, // YZ(3) % 3 = X(0)
    ZX   = 4, // ZX(4) % 3 = Y(1)
    XY   = 5, // XY(5) % 3 = Z(2)
    // Don't apply mod3
    XYZ  = 6
  };

  enum class DirectionLabel
  {
    None = -1,
    N,
    S,
    E,
    W,
    SE,
    SW,
    NE,
    NW,
    CENTER
  };

  constexpr float TK_FLT_MAX = std::numeric_limits<float>::max();
  constexpr float TK_FLT_MIN = std::numeric_limits<float>::min();
  constexpr int TK_INT_MAX   = std::numeric_limits<int>::max();
  constexpr int TK_UINT_MAX  = std::numeric_limits<unsigned int>::max();

  // Graphics Api Type Overrides.
  enum class GraphicTypes
  {
    VertexShader               = 0x8B31,
    FragmentShader             = 0x8B30,
    UVRepeat                   = 0x2901,
    UVClampToEdge              = 0x812F,
    UVClampToBorder            = 0x812D,
    SampleNearest              = 0x2600,
    SampleLinear               = 0x2601,
    SampleNearestMipmapNearest = 0x2700,
    SampleLinearMipmapLinear   = 0x2703,
    SampleLinearMipmapNearest  = 0x2701,
    FormatRed                  = 0x1903,
    FormatR8                   = 0x8229,
    FormatRG                   = 0x8227,
    FormatRG8                  = 0x822B,
    FormatRGB                  = 0x1907,
    FormatRGB8                 = 0x8051,
    FormatRGBA8                = 0x8058,
    FormatRGBA                 = 0x1908,
    FormatR16F                 = 0x822A,
    FormatR32F                 = 0x822E,
    FormatRG16F                = 0x822F,
    FormatRG32F                = 0x8230,
    FormatRGB16F               = 0x881B,
    FormatRGBA16F              = 0x881A,
    FormatRGB32F               = 0x8815,
    FormatRGBA32F              = 0x8814,
    FormatR16SNorm             = 0x8F98,
    FormatSRGB8_A8             = 0x8C43,
    FormatDepth24              = 0x81A6,
    FormatDepth24Stencil8      = 0x88F0,
    ColorAttachment0           = 0x8CE0,
    DepthAttachment            = 0x8D00,
    TypeFloat                  = 0x1406,
    TypeUnsignedByte           = 0x1401,
    Target2D                   = 0x0DE1,
    TargetCubeMap              = 0x8513,
    Target2DArray              = 0x8C1A
  };

  enum class GpuResourceType
  {
    Texture      = 0x1702, // GL_TEXTURE
    Buffer       = 0x82E0, // GL_BUFFER
    Shader       = 0x82E1, // GL_SHADER
    Program      = 0x82E2, // GL_PROGRAM
    FrameBuffer  = 0x8D40, // GL_FRAMEBUFFER
    RenderBuffer = 0x8D41  // GL_RENDERBUFFER
  };

  inline int BytesOfFormat(GraphicTypes type)
  {
    switch (type)
    {
    case GraphicTypes::FormatRed:
    case GraphicTypes::FormatR8:
      return 1;
    case GraphicTypes::FormatRG:
    case GraphicTypes::FormatRG8:
    case GraphicTypes::FormatR16F:
    case GraphicTypes::FormatR16SNorm:
      return 2;
    case GraphicTypes::FormatRGB:
    case GraphicTypes::FormatRGB8:
      return 3;
    case GraphicTypes::FormatRGBA:
    case GraphicTypes::FormatRGBA8:
    case GraphicTypes::FormatR32F:
    case GraphicTypes::FormatRG16F:
    case GraphicTypes::FormatSRGB8_A8:
      return 4;
    case GraphicTypes::FormatRG32F:
    case GraphicTypes::FormatRGBA16F:
      return 8;
    case GraphicTypes::FormatRGB16F:
      return 6;
    case GraphicTypes::FormatRGB32F:
      return 12;
    case GraphicTypes::FormatRGBA32F:
      return 16;
    default:
      assert(false && "Unsupported pixel format");
      return 1;
    }
  }

  // String defines.
  static const String TKBrdfLutTexture        = "GLOBAL_BRDF_LUT_TEXTURE";
  static const String TKIrradianceCacheFolder = "EnvCacheMap";
  static const String TKDefaultGradientSky    = "DefaultGradientSky";
  static const String TKDefaultHdri           = "DefaultHDRI";
  static const String TKDefaultImage          = "default.png";
  static const String TKResourcePak           = "MinResources.pak";

  // Version defines.
  static const String TKVersionStr            = "v0.4.9";
  static const String TKV044                  = "v0.4.4";
  static const String TKV045                  = "v0.4.5";
  static const String TKV046                  = "v0.4.6";
  static const String TKV047                  = "v0.4.7";
  static const String TKV048                  = "v0.4.8";
  static const String TKV049                  = "v0.4.9";

} // namespace ToolKit

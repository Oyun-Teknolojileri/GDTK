/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include <Animation.h>
#include <Common/Win32Utils.h>
#include <DirectionComponent.h>
#include <Image.h>
#include <Material.h>
#include <MaterialComponent.h>
#include <Mesh.h>
#include <MeshComponent.h>
#include <RenderSystem.h>
#include <SDL.h>
#include <Scene.h>
#include <Texture.h>
#include <ToolKit.h>
#include <Types.h>
#include <Util.h>
#include <assert.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/Importer.hpp>
#include <assimp/pbrmaterial.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <iostream>

using std::cout;
using std::endl;
using std::fstream;
using std::ifstream;
using std::ios;
using std::ofstream;
using std::string;
using std::to_string;
using std::unordered_map;
using std::vector;
namespace fs = std::filesystem;

template <typename GLMtype, typename AiType>
GLMtype convertAssimpColorToGlm(AiType source)
{
  GLMtype color = {};
  for (glm::length_t i = 0; i < GLMtype::length(); i++)
  {
    color[i] = source[i];
  }
  return color;
}

char GetPathSeparator()
{
  static char sep    = '/';
  const wchar_t pref = fs::path::preferred_separator;
  wcstombs(&sep, &pref, 1);
  return sep;
}

void NormalizePath(string& path)
{
  fs::path patify = path;
  path            = patify.lexically_normal().u8string();
}

void TrunckToFileName(string& fullPath)
{
  fs::path patify = fullPath;
  fullPath        = patify.filename().u8string();
}

namespace ToolKit
{
  Vec3 toVec3(aiVector3f vec)
  {
    Vec3 gv;
    gv.x = vec.x;
    gv.y = vec.y;
    gv.z = vec.z;
    return gv;
  }

  // Right handed row major to Right handed column major.
  Mat4 toMat4(aiMatrix4x4 mat)
  {
    Mat4 gm;
    gm[0][0] = mat.a1;
    gm[0][1] = mat.a2;
    gm[0][2] = mat.a3;
    gm[0][3] = mat.a4;

    gm[1][0] = mat.b1;
    gm[1][1] = mat.b2;
    gm[1][2] = mat.b3;
    gm[1][3] = mat.b4;

    gm[2][0] = mat.c1;
    gm[2][1] = mat.c2;
    gm[2][2] = mat.c3;
    gm[2][3] = mat.c4;

    gm[3][0] = mat.d1;
    gm[3][1] = mat.d2;
    gm[3][2] = mat.d3;
    gm[3][3] = mat.d4;

    return gm;
  }

  class BoneNode
  {
   public:
    BoneNode() {}

    BoneNode(aiNode* node, unsigned int index)
    {
      boneIndex = index;
      boneNode  = node;
    }

    aiNode* boneNode       = nullptr;
    aiBone* bone           = nullptr;
    unsigned int boneIndex = 0;
  };

  vector<string> g_usedFiles;

  bool IsUsed(const string& file) { return find(g_usedFiles.begin(), g_usedFiles.end(), file) == g_usedFiles.end(); }

  void AddToUsedFiles(const string& file)
  {
    // Add unique.
    if (IsUsed(file))
    {
      g_usedFiles.push_back(file);
    }
  }

  void ClearForbidden(std::string& str)
  {
    const std::string forbiddenChars = "\\/:?\"<>|";
    std::replace_if(
        str.begin(),
        str.end(),
        [&forbiddenChars](char c) { return std::string::npos != forbiddenChars.find(c); },
        ' ');
  }

  unordered_map<string, BoneNode> g_skeletonMap;
  SkeletonPtr g_skeleton;
  bool isSkeletonEntityCreated = false;
  const aiScene* g_scene       = nullptr;

  void Decompose(string& fullPath, string& path, string& name)
  {
    NormalizePath(fullPath);
    path     = "";

    size_t i = fullPath.find_last_of(GetPathSeparator());
    if (i != string::npos)
    {
      path = fullPath.substr(0, fullPath.find_last_of(GetPathSeparator()) + 1);
    }

    name = fullPath.substr(fullPath.find_last_of(GetPathSeparator()) + 1);
    name = name.substr(0, name.find_last_of('.'));
  }

  void DecomposeAssimpMatrix(aiMatrix4x4 transform, Vec3* t, Quaternion* r, Vec3* s)
  {
    aiVector3D aiT, aiS;
    aiQuaternion aiR;
    transform.Decompose(aiS, aiR, aiT);

    *t = Vec3(aiT.x, aiT.y, aiT.z);
    *r = Quaternion(aiR.x, aiR.y, aiR.z, aiR.w);
    *s = Vec3(aiS.x, aiS.y, aiS.z);
  }

  string GetEmbeddedTextureName(const aiTexture* texture, int i)
  {
    string name = texture->mFilename.C_Str();
    if (name.empty())
    {
      // Some glb files doesn't contain any file name for embedded textures.
      // So we add one to help importer.
      name = "@" + std::to_string(i);
    }

    NormalizePath(name);
    name = name + "." + texture->achFormatHint;

    return name;
  }

  string GetMaterialName(aiMaterial* material, unsigned int indx)
  {
    string name = material->GetName().C_Str();
    if (name.empty())
    {
      name = "emb" + to_string(indx);
    }

    return name;
  }

  string GetMaterialName(aiMesh* mesh)
  {
    aiString matName;
    g_scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, matName);
    return GetMaterialName(g_scene->mMaterials[mesh->mMaterialIndex], mesh->mMaterialIndex);
  }

  std::vector<MaterialPtr> tMaterials;

  template <typename T>
  void CreateFileAndSerializeObject(T* objectToSerialize, const String& filePath)
  {
    objectToSerialize->SetFile(filePath);
    objectToSerialize->Save(false);
  }

  const float g_desiredFps = 30.0f;
  const float g_animEps    = 0.001f;
  String g_currentExt;

  // Interpolator functions Begin
  // Range checks added by OTSoftware.
  // https://github.com/triplepointfive/ogldev/blob/master/tutorial39/mesh.cpp

  bool EpsilonLessEqual(float a, float b, float epsilon)
  {
    // Return true if a is less than b or if they are approximately equal
    return (a < b) || glm::epsilonEqual(a, b, epsilon);
  }

  int GetMax(int a, int b) { return a > b ? a : b; }

  int GetMax(int a, int b, int c) { return GetMax(a, GetMax(b, c)); }

  uint FindPosition(float AnimationTime, const aiNodeAnim* pNodeAnim)
  {
    for (uint i = 0; i < pNodeAnim->mNumPositionKeys - 1; i++)
    {
      if (EpsilonLessEqual(AnimationTime, (float) pNodeAnim->mPositionKeys[i + 1].mTime, g_animEps))
      {
        return i;
      }
    }

    return GetMax(0, pNodeAnim->mNumPositionKeys - 2);
  }

  uint FindRotation(float AnimationTime, const aiNodeAnim* pNodeAnim)
  {
    assert(pNodeAnim->mNumRotationKeys > 0);

    for (uint i = 0; i < pNodeAnim->mNumRotationKeys - 1; i++)
    {
      if (EpsilonLessEqual(AnimationTime, (float) (pNodeAnim->mRotationKeys[i + 1].mTime), g_animEps))
      {
        return i;
      }
    }

    return GetMax(0, pNodeAnim->mNumPositionKeys - 2);
  }

  uint FindScaling(float AnimationTime, const aiNodeAnim* pNodeAnim)
  {
    assert(pNodeAnim->mNumScalingKeys > 0);

    for (uint i = 0; i < pNodeAnim->mNumScalingKeys - 1; i++)
    {
      if (EpsilonLessEqual(AnimationTime, (float) (pNodeAnim->mScalingKeys[i + 1].mTime), g_animEps))
      {
        return i;
      }
    }

    return GetMax(0, pNodeAnim->mNumPositionKeys - 2);
  }

  void CalcInterpolatedPosition(aiVector3D& Out, float AnimationTime, const aiNodeAnim* pNodeAnim)
  {
    if (pNodeAnim->mNumPositionKeys == 1)
    {
      Out = pNodeAnim->mPositionKeys[0].mValue;
      return;
    }

    uint PositionIndex     = FindPosition(AnimationTime, pNodeAnim);
    uint NextPositionIndex = (PositionIndex + 1);
    assert(NextPositionIndex < pNodeAnim->mNumPositionKeys);

    float DeltaTime =
        (float) (pNodeAnim->mPositionKeys[NextPositionIndex].mTime - pNodeAnim->mPositionKeys[PositionIndex].mTime);

    float Factor = (AnimationTime - (float) (pNodeAnim->mPositionKeys[PositionIndex].mTime)) / DeltaTime;

    glm::clamp(Factor, 0.0f, 1.0f);

    const aiVector3D& Start = pNodeAnim->mPositionKeys[PositionIndex].mValue;
    const aiVector3D& End   = pNodeAnim->mPositionKeys[NextPositionIndex].mValue;
    aiVector3D Delta        = End - Start;
    Out                     = Start + Factor * Delta;
  }

  void CalcInterpolatedRotation(aiQuaternion& Out, float AnimationTime, const aiNodeAnim* pNodeAnim)
  {
    // we need at least two values to interpolate...
    if (pNodeAnim->mNumRotationKeys == 1)
    {
      Out = pNodeAnim->mRotationKeys[0].mValue;
      return;
    }

    uint RotationIndex     = FindRotation(AnimationTime, pNodeAnim);
    uint NextRotationIndex = (RotationIndex + 1);
    assert(NextRotationIndex < pNodeAnim->mNumRotationKeys);

    float DeltaTime =
        (float) (pNodeAnim->mRotationKeys[NextRotationIndex].mTime - pNodeAnim->mRotationKeys[RotationIndex].mTime);

    float Factor = (AnimationTime - (float) (pNodeAnim->mRotationKeys[RotationIndex].mTime)) / DeltaTime;

    glm::clamp(Factor, 0.0f, 1.0f);

    const aiQuaternion& StartRotationQ = pNodeAnim->mRotationKeys[RotationIndex].mValue;
    const aiQuaternion& EndRotationQ   = pNodeAnim->mRotationKeys[NextRotationIndex].mValue;
    aiQuaternion::Interpolate(Out, StartRotationQ, EndRotationQ, Factor);
    Out = Out.Normalize();
  }

  void CalcInterpolatedScaling(aiVector3D& Out, float AnimationTime, const aiNodeAnim* pNodeAnim)
  {
    if (pNodeAnim->mNumScalingKeys == 1)
    {
      Out = pNodeAnim->mScalingKeys[0].mValue;
      return;
    }

    uint ScalingIndex     = FindScaling(AnimationTime, pNodeAnim);
    uint NextScalingIndex = (ScalingIndex + 1);
    assert(NextScalingIndex < pNodeAnim->mNumScalingKeys);

    float DeltaTime =
        (float) (pNodeAnim->mScalingKeys[NextScalingIndex].mTime - pNodeAnim->mScalingKeys[ScalingIndex].mTime);

    float Factor = (AnimationTime - (float) (pNodeAnim->mScalingKeys[ScalingIndex].mTime)) / DeltaTime;

    glm::clamp(Factor, 0.0f, 1.0f);

    const aiVector3D& Start = pNodeAnim->mScalingKeys[ScalingIndex].mValue;
    const aiVector3D& End   = pNodeAnim->mScalingKeys[NextScalingIndex].mValue;
    aiVector3D Delta        = End - Start;
    Out                     = Start + Factor * Delta;
  }

  // Interpolator functions END

  void ImportAnimation(const string& file)
  {
    if (!g_scene->HasAnimations())
    {
      return;
    }

    for (uint i = 0; i < g_scene->mNumAnimations; i++)
    {
      aiAnimation* anim = g_scene->mAnimations[i];
      std::string animName(anim->mName.C_Str());
      string animFilePath = file;
      replace(animName.begin(), animName.end(), '.', '_');
      replace(animName.begin(), animName.end(), '|', '_');
      animFilePath += animName + ".anim";
      AddToUsedFiles(animFilePath);
      AnimationPtr tAnim = MakeNewPtr<Animation>();

      double fps         = anim->mTicksPerSecond == 0 ? g_desiredFps : anim->mTicksPerSecond;
      double duration    = anim->mDuration / fps;
      uint frameCount    = (uint) ceil(duration * g_desiredFps);

      // Used to normalize animation start time.
      int cr, ct, cs, cmax;
      cr = ct = cs = cmax = 0;

      for (uint chIndx = 0; chIndx < anim->mNumChannels; chIndx++)
      {
        KeyArray keys;
        aiNodeAnim* nodeAnim = anim->mChannels[chIndx];
        for (uint frame = 1; frame < frameCount; frame++)
        {
          float timeInTicks = (frame / g_desiredFps) * (float) anim->mTicksPerSecond;

          aiVector3D t;
          if (
              // Timer is not yet reach the animation begin. Skip frames.
              // Happens when there aren't keys at the beginning of the
              // animation.
              EpsilonLessEqual(timeInTicks, (float) nodeAnim->mPositionKeys[0].mTime, g_animEps))
          {
            continue;
          }
          else
          {
            CalcInterpolatedPosition(t, timeInTicks, nodeAnim);
            ct++;
          }

          aiQuaternion r;
          if (EpsilonLessEqual(timeInTicks, (float) (nodeAnim->mRotationKeys[0].mTime), g_animEps))
          {
            continue;
          }
          else
          {
            CalcInterpolatedRotation(r, timeInTicks, nodeAnim);
            cr++;
          }

          aiVector3D s;
          if (EpsilonLessEqual(timeInTicks, (float) (nodeAnim->mScalingKeys[0].mTime), g_animEps))
          {
            continue;
          }
          else
          {
            CalcInterpolatedScaling(s, timeInTicks, nodeAnim);
            cs++;
          }

          Key tKey;
          tKey.m_frame    = frame;
          tKey.m_position = Vec3(t.x, t.y, t.z);
          tKey.m_rotation = Quaternion(r.x, r.y, r.z, r.w);
          tKey.m_scale    = Vec3(s.x, s.y, s.z);
          keys.push_back(tKey);
        }

        cmax = GetMax(cr, ct, cs);
        cr = ct = cs = 0;
        tAnim->m_keys.insert(std::make_pair(nodeAnim->mNodeName.C_Str(), keys));
      }

      // Recalculate duration. May be misleading due to shifted animations.
      tAnim->m_duration = (float) (cmax / g_desiredFps);
      tAnim->m_fps      = (float) (g_desiredFps);

      CreateFileAndSerializeObject(tAnim.get(), animFilePath);
    }
  }

  void ImportMaterial(const string& filePath, const string& origin)
  {
    fs::path pathOrg              = fs::path(origin).parent_path();

    auto textureFindAndCreateFunc = [filePath, pathOrg](aiTextureType textureAssimpType,
                                                        aiMaterial* material) -> TexturePtr
    {
      int texCount = material->GetTextureCount(textureAssimpType);
      TexturePtr tTexture;
      if (texCount > 0)
      {
        aiString texture;
        material->GetTexture(textureAssimpType, 0, &texture);

        string tName  = texture.C_Str();
        bool embedded = false;
        if (!tName.empty() && tName[0] == '*') // Embedded texture.
        {
          embedded        = true;
          string indxPart = tName.substr(1);
          uint tIndx      = atoi(indxPart.c_str());
          if (g_scene->mNumTextures > tIndx)
          {
            aiTexture* t = g_scene->mTextures[tIndx];
            tName        = GetEmbeddedTextureName(t, tIndx);
          }
        }

        string fileName = tName;
        TrunckToFileName(fileName);
        string textPath = fs::path(filePath + fileName).lexically_normal().u8string();

        if (!embedded && !std::filesystem::exists(textPath))
        {
          // Try copying texture.
          fs::path fullPath = pathOrg;
          fullPath.append(tName);
          fullPath = fullPath.lexically_normal();

          ifstream isGoodFile;
          isGoodFile.open(fullPath, ios::binary | ios::in);
          if (isGoodFile.good())
          {
            fs::path target = fs::path(textPath);
            if (target.has_parent_path())
            {
              fs::path dir = target.parent_path();
              if (!fs::exists(dir))
              {
                fs::create_directories(dir);
              }
            }

            fs::copy(fullPath, target, fs::copy_options::overwrite_existing);
          }
          isGoodFile.close();
        }

        AddToUsedFiles(textPath);
        tTexture = MakeNewPtr<Texture>();
        tTexture->SetFile(textPath);
      }
      return tTexture;
    };

    for (uint i = 0; i < g_scene->mNumMaterials; i++)
    {
      aiMaterial* material  = g_scene->mMaterials[i];
      string name           = GetMaterialName(material, i);
      string writePath      = filePath + name + MATERIAL;
      MaterialPtr tMaterial = MakeNewPtr<Material>();

      TexturePtr diffuse    = textureFindAndCreateFunc(aiTextureType_DIFFUSE, material);
      if (diffuse)
      {
        tMaterial->SetDiffuseTextureVal(diffuse);
      }

      TexturePtr emissive = textureFindAndCreateFunc(aiTextureType_EMISSIVE, material);
      if (emissive)
      {
        tMaterial->SetEmissiveTextureVal(emissive);
      }

      aiColor3D emissiveColor;
      if (material->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveColor) == aiReturn_SUCCESS)
      {
        tMaterial->SetEmissiveColorVal(convertAssimpColorToGlm<Vec3>(emissiveColor));
      }

      TexturePtr metallicRoughness = textureFindAndCreateFunc(aiTextureType_UNKNOWN, material);
      if (metallicRoughness)
      {
        tMaterial->SetMetallicRoughnessTextureVal(metallicRoughness);
      }

      float metalness, roughness;
      if (material->Get(AI_MATKEY_METALLIC_FACTOR, metalness) == aiReturn_SUCCESS)
      {
        tMaterial->SetMetallicVal(metalness);
      }
      if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == aiReturn_SUCCESS)
      {
        tMaterial->SetRoughnessVal(roughness);
      }

      TexturePtr normal = textureFindAndCreateFunc(aiTextureType_NORMALS, material);
      if (normal)
      {
        tMaterial->SetNormalTextureVal(normal);
      }

      // There are various ways to get alpha value in Assimp, try each step
      // until succeed
      float transparency = 1.0f;
      if (material->Get(AI_MATKEY_TRANSPARENCYFACTOR, transparency) != aiReturn_SUCCESS)
      {
        if (material->Get(AI_MATKEY_OPACITY, transparency) != aiReturn_SUCCESS)
        {
          material->Get(AI_MATKEY_COLOR_TRANSPARENT, transparency);
        }
      }
      tMaterial->SetAlphaVal(transparency);

      aiBlendMode blendFunc = aiBlendMode_Default;
      if (material->Get(AI_MATKEY_BLEND_FUNC, blendFunc) == aiReturn_SUCCESS)
      {
        if (blendFunc == aiBlendMode_Default)
        {
          tMaterial->GetRenderState()->blendFunction = BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;
        }
        else
        {
          tMaterial->GetRenderState()->blendFunction = BlendFunction::ONE_TO_ONE;
        }
      }
      else if (transparency != 1.0f)
      {
        tMaterial->GetRenderState()->blendFunction = BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;
      }

      material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, tMaterial->GetRenderState()->alphaMaskTreshold);

      tMaterial->SetFile(writePath);
      CreateFileAndSerializeObject(tMaterial.get(), writePath);
      AddToUsedFiles(writePath);
      tMaterials.push_back(tMaterial);
    }
  }

  // Creates a ToolKit mesh by reading the aiMesh
  // @param mainMesh: Pointer of the mesh
  template <typename convertType>
  void ConvertMesh(aiMesh* mesh, convertType tMesh)
  {
    assert(mesh->mNumVertices && "Mesh has no vertices!");

    // Skin data
    unordered_map<int, vector<std::pair<int, float>>> skinData;
    if constexpr (std::is_same<convertType, SkinMeshPtr>::value)
    {
      for (unsigned int i = 0; i < mesh->mNumBones; i++)
      {
        aiBone* bone = mesh->mBones[i];
        assert(g_skeletonMap.find(bone->mName.C_Str()) != g_skeletonMap.end());
        BoneNode bn = g_skeletonMap[bone->mName.C_Str()];
        for (unsigned int j = 0; j < bone->mNumWeights; j++)
        {
          aiVertexWeight vw = bone->mWeights[j];
          skinData[vw.mVertexId].push_back(std::pair<int, float>(bn.boneIndex, vw.mWeight));
        }
      }
      tMesh->m_skeleton = g_skeleton;
    }

    tMesh->m_clientSideVertices.resize(mesh->mNumVertices);
    for (uint vIndex = 0; vIndex < mesh->mNumVertices; vIndex++)
    {
      auto& v = tMesh->m_clientSideVertices[vIndex];
      v.pos   = Vec3(mesh->mVertices[vIndex].x, mesh->mVertices[vIndex].y, mesh->mVertices[vIndex].z);

      if (mesh->HasNormals())
      {
        v.norm = Vec3(mesh->mNormals[vIndex].x, mesh->mNormals[vIndex].y, mesh->mNormals[vIndex].z);
      }

      // Does the mesh contain texture coordinates?
      if (mesh->HasTextureCoords(0))
      {
        v.tex.x = mesh->mTextureCoords[0][vIndex].x;
        v.tex.y = mesh->mTextureCoords[0][vIndex].y;
      }

      if (mesh->HasTangentsAndBitangents())
      {
        v.btan = Vec3(mesh->mBitangents[vIndex].x, mesh->mBitangents[vIndex].y, mesh->mBitangents[vIndex].z);
      }

      if constexpr (std::is_same<convertType, SkinMeshPtr>::value)
      {
        if (!skinData.empty() && skinData.find(vIndex) != skinData.end())
        {
          for (int j = 0; j < 4; j++)
          {
            if (j >= (int) (skinData[vIndex].size()))
            {
              skinData[vIndex].push_back(std::pair<int, float>(0, 0.0f));
            }
          }

          for (ubyte i = 0; i < 4; i++)
          {
            v.bones[i]   = (float) skinData[vIndex][i].first;
            v.weights[i] = skinData[vIndex][i].second;
          }
        }
      }
    }

    tMesh->m_clientSideIndices.resize(mesh->mNumFaces * 3);
    for (unsigned int face_i = 0; face_i < mesh->mNumFaces; face_i++)
    {
      aiFace face = mesh->mFaces[face_i];
      assert(face.mNumIndices == 3);
      for (ubyte i = 0; i < 3; i++)
      {
        tMesh->m_clientSideIndices[(face_i * 3) + i] = face.mIndices[i];
      }
    }

    tMesh->m_loaded      = true;
    tMesh->m_vertexCount = (int) (tMesh->m_clientSideVertices.size());
    tMesh->m_indexCount  = (int) (tMesh->m_clientSideIndices.size());
    tMesh->m_material    = tMaterials[mesh->mMaterialIndex];
    for (ubyte i = 0; i < 3; i++)
    {
      tMesh->m_boundingBox.min[i] = mesh->mAABB.mMin[i];
      tMesh->m_boundingBox.max[i] = mesh->mAABB.mMax[i];
    }
  }

  std::unordered_map<aiMesh*, MeshPtr> g_meshes;
  SkinMeshPtr mainSkinMesh;

  void ImportMeshes(string& filePath)
  {
    string path, name;
    Decompose(filePath, path, name);
    mainSkinMesh = nullptr;

    // Skinned meshes will be merged because they're using the same skeleton
    // (Only one skeleton is imported)
    for (uint MeshIndx = 0; MeshIndx < g_scene->mNumMeshes; MeshIndx++)
    {
      aiMesh* aMesh = g_scene->mMeshes[MeshIndx];
      if (aMesh->HasBones())
      {
        SkinMeshPtr skinMesh = MakeNewPtr<SkinMesh>();
        ConvertMesh(aMesh, skinMesh);
        if (mainSkinMesh)
        {
          mainSkinMesh->m_subMeshes.push_back(skinMesh);
        }
        else
        {
          mainSkinMesh = skinMesh;
        }
      }
      else
      {
        MeshPtr mesh = MakeNewPtr<Mesh>();
        ConvertMesh(aMesh, mesh);

        // Better to use scene node name
        string fileName  = "";
        aiNode* meshNode = g_scene->mRootNode->FindNode(aMesh->mName);
        if (meshNode)
        {
          fileName = std::string(meshNode->mName.C_Str());
        }
        else
        {
          fileName = aMesh->mName.C_Str();
        }
        ClearForbidden(fileName);
        String meshPath = path + fileName + MESH;

        Assimp::DefaultLogger::get()->info("file name: ", meshPath);

        mesh->SetFile(meshPath);
        AddToUsedFiles(meshPath);
        g_meshes[aMesh] = mesh;
        CreateFileAndSerializeObject(mesh.get(), meshPath);
      }
    }
    if (mainSkinMesh)
    {
      ClearForbidden(name);
      String skinMeshPath = path + name + SKINMESH;
      mainSkinMesh->SetFile(skinMeshPath);

      AddToUsedFiles(skinMeshPath);
      CreateFileAndSerializeObject(mainSkinMesh.get(), skinMeshPath);
    }
  }

  std::vector<LightPtr> sceneLights;

  void ImportLights()
  {
    for (uint i = 0; i < g_scene->mNumLights; i++)
    {
      LightPtr tkLight  = nullptr;
      aiLight* light    = g_scene->mLights[i];
      float lightRadius = 1.0f;
      {
        // radius for attenuation = 0.01
        float treshold = 0.01f;
        float a        = light->mAttenuationQuadratic * treshold;
        float b        = light->mAttenuationLinear * treshold;
        float c        = light->mAttenuationConstant * treshold - 1.0f;
        float disc     = (b * b) - (4.0f * a * c);
        if (disc >= 0.0f)
        {
          float t1 = (-b - glm::sqrt(disc)) / (2.0f * a);
          float t2 = (-b + glm::sqrt(disc)) / (2.0f * a);
          float t  = glm::max(t1, t2);
          if (t > 0.0f)
          {
            lightRadius = t;
          }
        }
      }
      if (light->mType == aiLightSource_DIRECTIONAL)
      {
        DirectionalLightPtr dirLight = MakeNewPtr<DirectionalLight>();
        dirLight->SetNameVal(light->mName.C_Str());
        dirLight->m_node->SetTranslation(Vec3(light->mPosition.x, light->mPosition.y, light->mPosition.z));
        dirLight->GetComponent<DirectionComponent>()->LookAt(
            Vec3(light->mDirection.x, light->mDirection.y, light->mDirection.z));
        dirLight->SetColorVal(Vec3(light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b));
        tkLight = dirLight;
      }
      else if (light->mType == aiLightSource_POINT)
      {
        PointLightLightPtr pointLight = MakeNewPtr<PointLight>();
        pointLight->SetNameVal(light->mName.C_Str());
        pointLight->m_node->SetTranslation(Vec3(light->mPosition.x, light->mPosition.y, light->mPosition.z));
        pointLight->SetRadiusVal(light->mAttenuationConstant);
        pointLight->SetColorVal(Vec3(light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b));
        pointLight->SetRadiusVal(lightRadius);
        tkLight = pointLight;
      }
      else if (light->mType == aiLightSource_SPOT)
      {
        SpotLightPtr spotLight = MakeNewPtr<SpotLight>();
        spotLight->SetNameVal(light->mName.C_Str());
        spotLight->m_node->SetTranslation(Vec3(light->mPosition.x, light->mPosition.y, light->mPosition.z));
        spotLight->GetComponent<DirectionComponent>()->LookAt(
            Vec3(light->mDirection.x, light->mDirection.y, light->mDirection.z));
        spotLight->SetRadiusVal(light->mAttenuationConstant);
        spotLight->SetColorVal(Vec3(light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b));
        spotLight->SetInnerAngleVal(glm::degrees(light->mAngleInnerCone));
        spotLight->SetOuterAngleVal(glm::degrees(light->mAngleOuterCone));
        spotLight->SetRadiusVal(lightRadius);
        tkLight = spotLight;
      }
      else
      {
        // unknown light type
        continue;
      }

      sceneLights.push_back(tkLight);
    }
  }

  std::vector<CameraPtr> sceneCameras;

  void ImportCameras()
  {
    for (uint i = 0; i < g_scene->mNumCameras; i++)
    {
      aiCamera* cam = g_scene->mCameras[i];
      if (cam->mOrthographicWidth > 0.0f)
      {
        continue; // Skip orthographic cameras.
      }

      CameraPtr tkCam = MakeNewPtr<Camera>();
      tkCam->SetNameVal(cam->mName.C_Str());

      // Horizontal to vertical fov.
      float aspect               = cam->mAspect > 0.0f ? cam->mAspect : 1.0f;
      float tanHalfHorizontalFov = std::tan(cam->mHorizontalFOV * 0.5f);
      float fov                  = 2.0f * std::atan(tanHalfHorizontalFov / aspect);

      aiMatrix4x4 transform;
      cam->GetCameraMatrix(transform);
      tkCam->m_node->SetTransform(toMat4(transform));
      tkCam->SetLens(fov, aspect, cam->mClipPlaneNear, cam->mClipPlaneFar);

      sceneCameras.push_back(tkCam);
    }
  }

  EntityPtrArray deletedEntities;

  bool DeleteEmptyEntitiesRecursively(ScenePtr tScene, EntityPtr ntt)
  {
    bool shouldDelete = true;
    if (ntt->GetComponentPtrArray().size())
    {
      shouldDelete = false;
    }

    VariantCategoryArray varCategories;
    ntt->m_localData.GetCategories(varCategories, true, false);
    if (varCategories.size() > 1)
    {
      shouldDelete = false;
    }

    for (Node* child : ntt->m_node->m_children)
    {
      if (!DeleteEmptyEntitiesRecursively(tScene, child->OwnerEntity()))
      {
        shouldDelete = false;
      }
    }
    if (shouldDelete)
    {
      deletedEntities.push_back(ntt);
    }
    return shouldDelete;
  }

  void TraverseScene(ScenePtr tScene, const aiNode* node, EntityPtr parent)
  {
    EntityPtr ntt = nullptr;

    // Camera transform data is local, it gets its full transforms when merged with node.
    // So camera must be matched with a node in the graph. (Look at aiCamera doc)
    for (CameraPtr cam : sceneCameras)
    {
      if (cam->GetNameVal() == node->mName.C_Str())
      {
        ntt = cam;
        ntt->m_node->Rotate(glm::angleAxis(glm::pi<float>(), Y_AXIS)); // Align dir.
        break;
      }
    }

    // Same as light.
    for (LightPtr light : sceneLights)
    {
      if (light->GetNameVal() == node->mName.C_Str())
      {
        ntt = light;
        break;
      }
    }

    // If there is no matching cam or light, its a mesh. Create a new entity for it.
    if (ntt == nullptr)
    {
      ntt = MakeNewPtr<Entity>();
    }

    ntt->m_node->m_inheritScale = true;
    ntt->SetNameVal(node->mName.C_Str());

    Vec3 t, s;
    Quaternion rt;
    DecomposeAssimpMatrix(node->mTransformation, &t, &rt, &s);

    if (parent)
    {
      // Sanity check.
      if (ntt->m_node->m_parent != nullptr)
      {
        TK_ERR("Adding child to '%s' failed. Entity '%s' has already a parent '%s'.",
               parent->GetNameVal().c_str(),
               ntt->GetNameVal().c_str(),
               ntt->m_node->ParentEntity()->GetNameVal().c_str());
        return;
      }
      else
      {
        // If a parent is provided, set it.
        parent->m_node->AddChild(ntt->m_node);
      }
    }

    ntt->m_node->Translate(t, TransformationSpace::TS_LOCAL);
    ntt->m_node->Rotate(rt, TransformationSpace::TS_LOCAL);
    ntt->m_node->Scale(s);

    // Insert all meshes to the entity.
    for (uint meshIndx = 0; meshIndx < node->mNumMeshes; meshIndx++)
    {
      aiMesh* aMesh = g_scene->mMeshes[node->mMeshes[meshIndx]];
      if (aMesh->HasBones() && isSkeletonEntityCreated)
      {
        continue;
      }

      bool firstMesh            = false;
      MeshComponentPtr meshComp = ntt->GetComponent<MeshComponent>();
      if (meshComp == nullptr)
      {
        firstMesh = true;
        meshComp  = ntt->AddComponent<MeshComponent>();
      }

      if (aMesh->HasBones())
      {
        meshComp->SetMeshVal(mainSkinMesh);

        SkeletonComponentPtr skelComp = ntt->AddComponent<SkeletonComponent>();
        skelComp->SetSkeletonResourceVal(g_skeleton);

        isSkeletonEntityCreated = true;
      }
      else
      {
        if (firstMesh)
        {
          meshComp->SetMeshVal(g_meshes[aMesh]);
        }
        else
        {
          // Check if a combination is needed.
          MeshPtr mesh = meshComp->GetMeshVal();
          if (mesh->GetMeshCount() != node->mNumMeshes)
          {
            mesh->m_subMeshes.push_back(g_meshes[aMesh]);
            mesh->m_dirty = true; // We only mesh to be saved.
          }
        }
      }

      MaterialComponentPtr matComp = ntt->GetComponent<MaterialComponent>();
      if (matComp == nullptr)
      {
        matComp = ntt->AddComponent<MaterialComponent>();
      }
      matComp->UpdateMaterialList();
    }

    // Re save combined mesh.
    if (node->mNumMeshes > 1)
    {
      if (MeshComponentPtr meshCom = ntt->GetMeshComponent())
      {
        if (MeshPtr combinedMesh = meshCom->GetMeshVal())
        {
          combinedMesh->Save(true);
        }
      }
    }

    for (uint childIndx = 0; childIndx < node->mNumChildren; childIndx++)
    {
      TraverseScene(tScene, node->mChildren[childIndx], ntt);
    }

    tScene->AddEntity(ntt);
  }

  void ImportScene(string& filePath)
  {
    // Print Scene.
    string path, name;
    Decompose(filePath, path, name);

    string fullPath = path + name + SCENE;
    AddToUsedFiles(fullPath);
    ScenePtr tScene = MakeNewPtr<Scene>();

    TraverseScene(tScene, g_scene->mRootNode, nullptr);
    // First entity is the root entity
    EntityPtrArray roots;
    GetRootEntities(tScene->GetEntities(), roots);
    for (EntityPtr r : roots)
    {
      DeleteEmptyEntitiesRecursively(tScene, r);
    }

    for (EntityPtr ntt : deletedEntities)
    {
      tScene->RemoveEntity(ntt->GetIdVal(), false);
    }
    deletedEntities.clear();
    Assimp::DefaultLogger::get()->info("scene path: ", fullPath);

    CreateFileAndSerializeObject(tScene.get(), fullPath);
  }

  void ImportSkeleton(string& filePath)
  {
    auto addBoneNodeFn = [](aiNode* node, aiBone* bone) -> void
    {
      BoneNode bn(node, 0);
      if (node->mName == bone->mName)
      {
        bn.bone = bone;
      }
      g_skeletonMap[node->mName.C_Str()] = bn;
    };

    // Collect skeleton parts
    vector<aiBone*> bones;
    for (unsigned int i = 0; i < g_scene->mNumMeshes; i++)
    {
      aiMesh* mesh     = g_scene->mMeshes[i];
      aiNode* meshNode = g_scene->mRootNode->FindNode(mesh->mName);
      for (unsigned int j = 0; j < mesh->mNumBones; j++)
      {
        aiBone* bone = mesh->mBones[j];
        bones.push_back(bone);
        aiNode* node = g_scene->mRootNode->FindNode(bone->mName);
        while (node) // Go Up
        {
          if (node == meshNode)
          {
            break;
          }

          if (meshNode != nullptr)
          {
            if (node == meshNode->mParent)
            {
              break;
            }
          }

          addBoneNodeFn(node, bone);
          node = node->mParent;
        }

        node                                     = g_scene->mRootNode->FindNode(bone->mName);

        // Go Down
        std::function<void(aiNode*)> checkDownFn = [&checkDownFn, &bone, &addBoneNodeFn](aiNode* node) -> void
        {
          if (node == nullptr)
          {
            return;
          }

          addBoneNodeFn(node, bone);

          for (unsigned int i = 0; i < node->mNumChildren; i++)
          {
            checkDownFn(node->mChildren[i]);
          }
        };
        checkDownFn(node);
      }
    }

    for (auto& bone : bones)
    {
      if (g_skeletonMap.find(bone->mName.C_Str()) != g_skeletonMap.end())
      {
        g_skeletonMap[bone->mName.C_Str()].bone = bone;
      }
    }

    if (bones.size() == 0)
    {
      return;
    }

    // Assign indices
    std::function<void(aiNode*, uint&)> assignBoneIndexFn = [&assignBoneIndexFn](aiNode* node, uint& index) -> void
    {
      if (g_skeletonMap.find(node->mName.C_Str()) != g_skeletonMap.end())
      {
        g_skeletonMap[node->mName.C_Str()].boneIndex = index++;
      }

      for (uint i = 0; i < node->mNumChildren; i++)
      {
        assignBoneIndexFn(node->mChildren[i], index);
      }
    };

    uint boneIndex = 0;
    assignBoneIndexFn(g_scene->mRootNode, boneIndex);

    string name, path;
    Decompose(filePath, path, name);
    string fullPath = path + name + SKELETON;

    g_skeleton      = MakeNewPtr<Skeleton>();
    g_skeleton->SetFile(fullPath);

    // Print
    std::function<void(aiNode * node, DynamicBoneMap::DynamicBone*)> setBoneHierarchyFn =
        [&setBoneHierarchyFn](aiNode* node, DynamicBoneMap::DynamicBone* parentBone) -> void
    {
      DynamicBoneMap::DynamicBone* searchDBone = parentBone;
      if (g_skeletonMap.find(node->mName.C_Str()) != g_skeletonMap.end())
      {
        assert(node->mName.length);
        g_skeleton->m_Tpose.m_boneMap.insert(
            std::make_pair(String(node->mName.C_Str()), DynamicBoneMap::DynamicBone()));

        searchDBone                       = &g_skeleton->m_Tpose.m_boneMap.find(node->mName.C_Str())->second;
        searchDBone->node                 = new Node();
        searchDBone->node->m_inheritScale = true;
        searchDBone->boneIndx             = (uint) g_skeleton->m_bones.size();
        g_skeleton->m_Tpose.AddDynamicBone(node->mName.C_Str(), *searchDBone, parentBone);

        StaticBone* sBone = new StaticBone(node->mName.C_Str());
        g_skeleton->m_bones.push_back(sBone);
      }
      for (uint i = 0; i < node->mNumChildren; i++)
      {
        setBoneHierarchyFn(node->mChildren[i], searchDBone);
      }
    };

    std::function<void(aiNode * node)> setTransformationsFn = [&setTransformationsFn](aiNode* node) -> void
    {
      if (g_skeletonMap.find(node->mName.C_Str()) != g_skeletonMap.end())
      {
        StaticBone* sBone = g_skeleton->GetBone(node->mName.C_Str());

        // Set bone node transformation
        {
          DynamicBoneMap::DynamicBone& dBone = g_skeleton->m_Tpose.m_boneMap[node->mName.C_Str()];
          Vec3 t, s;
          Quaternion r;
          DecomposeAssimpMatrix(node->mTransformation, &t, &r, &s);

          dBone.node->SetTranslation(t);
          dBone.node->SetOrientation(r);
          dBone.node->SetScale(s);
        }

        // Set bind pose transformation
        {
          aiBone* bone = g_skeletonMap[node->mName.C_Str()].bone;

          if (bone)
          {
            Vec3 t, s;
            Quaternion r;
            DecomposeAssimpMatrix(bone->mOffsetMatrix, &t, &r, &s);

            Mat4 tMat, rMat, sMat;
            tMat                        = glm::translate(tMat, t);
            rMat                        = glm::toMat4(r);
            sMat                        = glm::scale(sMat, s);
            sBone->m_inverseWorldMatrix = tMat * rMat * sMat;
          }
        }
      }

      for (uint i = 0; i < node->mNumChildren; i++)
      {
        setTransformationsFn(node->mChildren[i]);
      }
    };

    setBoneHierarchyFn(g_scene->mRootNode, nullptr);
    setTransformationsFn(g_scene->mRootNode);

    CreateFileAndSerializeObject(g_skeleton.get(), fullPath);
    AddToUsedFiles(fullPath);
  }

  void ImportTextures(const string& filePath)
  {
    // Embedded textures.
    if (g_scene->HasTextures())
    {
      for (uint i = 0; i < g_scene->mNumTextures; i++)
      {
        aiTexture* texture = g_scene->mTextures[i];
        string embId       = GetEmbeddedTextureName(texture, i);

        // Compressed.
        if (texture->mHeight == 0)
        {
          ofstream file(filePath + embId, fstream::out | std::fstream::binary);
          assert(file.good());

          file.write((const char*) texture->pcData, texture->mWidth);
        }
        else
        {
          unsigned char* buffer = (unsigned char*) texture->pcData;
          WritePNG(filePath.c_str(), texture->mWidth, texture->mHeight, 4, buffer, texture->mWidth * 4);
        }
      }
    }
  }

  int ToolKitMain(int argc, char* argv[])
  {
    try
    {
      if (argc < 2)
      {
        cout << "usage: Import 'fileToImport.format' <op> -t 'importTo' <op> -s 1.0 <op> -o 0";
        throw(-1);
      }

      Assimp::Importer importer;
      importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);

      int optimizationLevel = 0; // 0 or 1
      string dest, file = argv[1];
      Assimp::DefaultLogger::create("Assimplog.txt", Assimp::Logger::VERBOSE);
      for (int i = 0; i < argc; i++)
      {
        string arg = argv[i];
        Assimp::DefaultLogger::get()->info(arg);

        if (arg == "-t")
        {
          dest = fs::path(argv[i + 1]).append("").u8string();
        }

        if (arg == "-s")
        {
          float scale = (float) (std::atof(argv[i + 1]));
          importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, scale);
        }

        if (arg == "-o")
        {
          optimizationLevel = std::atoi(argv[i + 1]);
        }
      }

      dest = fs::path(dest).lexically_normal().u8string();
      if (!dest.empty())
      {
        fs::create_directories(dest);
      }

      string ext = file.substr(file.find_last_of("."));
      std::vector<string> files;
      if (ext == ".txt")
      {
        fstream fList;
        fList.open(file, ios::in);
        if (fList.is_open())
        {
          string fileStr;
          while (getline(fList, fileStr))
          {
            files.push_back(fileStr);
          }
          fList.close();
        }
      }
      else
      {
        files.push_back(file);
      }

      // Initialize ToolKit to serialize resources
      std::unique_ptr<Main> g_proxy = std::make_unique<Main>();
      Main::SetProxy(g_proxy.get());

      g_proxy->SetDefaultPath(ConcatPaths({"..", "..", "Resources", "Engine"}));
      g_proxy->SetConfigPath(ConcatPaths({"..", "..", "..", "Config"}));
      g_proxy->PreInit();

      GetLogger()->SetPlatformConsoleFn([](LogType type, const String& msg) -> void
                                        { ToolKit::PlatformHelpers::OutputLog((int) type, msg.c_str()); });

      // Init SDL
      SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

      SDL_Window* g_window    = SDL_CreateWindow("temp",
                                              SDL_WINDOWPOS_UNDEFINED,
                                              SDL_WINDOWPOS_UNDEFINED,
                                              32,
                                              32,
                                              SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
      SDL_GLContext g_context = SDL_GL_CreateContext(g_window);

      g_proxy->m_renderSys->InitGl(SDL_GL_GetProcAddress, nullptr);

      g_proxy->Init();

      for (int i = 0; i < (int) (files.size()); i++)
      {
        file = files[i];
        // Clear global materials for each scene to prevent wrong referencing
        tMaterials.clear();

        int optFlags = aiProcess_FlipUVs | aiProcess_GlobalScale;
        if (optimizationLevel == 1)
        {
          optFlags |= aiProcessPreset_TargetRealtime_MaxQuality;
        }

        const aiScene* scene = importer.ReadFile(file, optFlags);
        if (scene == nullptr)
        {
          assert(0 && "Assimp failed to import the file. Probably file is corrupted!");
          throw(-1);
        }
        g_scene                 = scene;
        isSkeletonEntityCreated = false;

        String fileName;
        DecomposePath(file, nullptr, &fileName, &g_currentExt);
        string destFile = dest + fileName;
        // DON'T BREAK THE CALLING ORDER!

        ImportAnimation(dest);

        // Create Textures to reference in Materials
        ImportTextures(dest);

        // Create Materials to reference in Meshes
        ImportMaterial(dest, file);

        // Create a Skeleton to reference in Meshes
        ImportSkeleton(destFile);

        // Add Meshes.
        ImportMeshes(destFile);

        // Add lights.
        ImportLights();

        // Add cameras.
        ImportCameras();

        // Create Meshes & Scene
        ImportScene(destFile);
      }

      // Report all in use files.
      fstream inUse("out.txt", ios::out);
      for (const string& fs : g_usedFiles)
      {
        inUse << fs << endl;
      }
      inUse.close();

      g_proxy->Uninit();
      g_proxy = nullptr;
    }
    catch (int code)
    {
      Assimp::DefaultLogger::get()->error("Import failed");
      Assimp::DefaultLogger::kill();
      return code;
    }

    Assimp::DefaultLogger::get()->info("Import success");
    Assimp::DefaultLogger::kill();

    return 0;
  }
} // namespace ToolKit

int main(int argc, char* argv[]) { return ToolKit::ToolKitMain(argc, argv); }
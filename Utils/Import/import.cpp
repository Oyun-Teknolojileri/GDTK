/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include <Animation.h>
#include <Common/PlatformHelper.h>
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
#include <limits>

using std::cout;
using std::endl;
using std::fstream;
using std::ifstream;
using std::ios;
using std::ofstream;
using std::string;
using std::to_string;
using std::unordered_map;
using std::unordered_set;
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

void TrunckToFileName(string& fullPath)
{
  // Normalize separators first. FBX texture references often carry Windows
  // paths from the authoring machine (e.g. "U:\...\Tex.png") and backslash
  // is not a path separator on Linux, so fs::path::filename() would keep
  // the whole string as the "file name" otherwise.
  ToolKit::NormalizePathInplace(fullPath);
  fs::path patify = fullPath;
  fullPath        = ToolKit::PathToString(patify.filename());
}

namespace ToolKit
{

  // Global variables for import process.
  ////////////////////////////////////////////////////////////////
  vector<string> g_usedFiles;
  bool isSkeletonEntityCreated = false;
  const aiScene* g_scene       = nullptr;

  SkeletonPtr g_skeleton;
  std::vector<MaterialPtr> tMaterials;
  std::unordered_map<aiMesh*, MeshPtr> g_meshes;
  SkinMeshPtr mainSkinMesh;
  std::vector<LightPtr> sceneLights;
  std::vector<CameraPtr> sceneCameras;
  EntityPtrArray deletedEntities;

  const float g_desiredFps = 30.0f;
  const float g_animEps    = 0.001f;
  // Max element diff between a node's bind transform and the transform
  // reconstructed from a bone's inverse bind matrix. Used to match bones to
  // scene nodes when several nodes share the same name.
  const float g_boneMatrixMatchEps = 0.001f;
  // Max distance between an animation channel's initial values and a node's
  // bind transform when a channel name matches multiple bone nodes.
  const float g_animChannelMatchEps = 0.01f;
  String g_currentExt;

  class BoneNode
  {
   public:
    BoneNode() {}

    BoneNode(aiNode* node, uint index)
    {
      boneIndex = index;
      boneNode  = node;
    }

    aiNode* boneNode = nullptr;
    aiBone* bone     = nullptr;
    uint boneIndex   = 0;
    String name; //!< Unique name assigned by the importer.
  };

  unordered_map<string, BoneNode> g_skeletonMap;
  // Bones resolved to their skeleton entries (aiBone* -> map entry). aiBone
  // names are not unique, so meshes must use this instead of a name lookup to
  // fetch vertex weight indices.
  unordered_map<const aiBone*, BoneNode*> g_aiBoneToEntry;
  // Scene nodes that became bones -> their skeleton entry.
  unordered_map<const aiNode*, BoneNode*> g_nodeToEntry;
  // Scene nodes indexed by their original name (duplicates included).
  unordered_map<string, vector<aiNode*>> g_nameToNodes;

  // Importer helper functions.
  ////////////////////////////////////////////////////////////////
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

  bool IsUsed(const string& file) { return find(g_usedFiles.begin(), g_usedFiles.end(), file) == g_usedFiles.end(); }

  void AddToUsedFiles(const string& file)
  {
    // Add unique.
    if (IsUsed(file))
    {
      g_usedFiles.push_back(file);
    }
  }

  void ClearForbidden(string& str)
  {
    const string forbiddenChars = "\\/:?\"<>|";
    replace_if(
        str.begin(),
        str.end(),
        [&forbiddenChars](char c) { return string::npos != forbiddenChars.find(c); },
        ' ');
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

    String fpath, fname, fext;
    DecomposePath(name, &fpath, &fname, &fext);
    if (fext.empty())
    {
      fext = texture->achFormatHint;
      if (!StartsWith(fext, "."))
      {
        fext = "." + fext;
      }
    }

    name = fname + fext;

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

  template <typename T>
  void CreateFileAndSerializeObject(T* objectToSerialize, const String& filePath)
  {
    objectToSerialize->SetFile(filePath);
    objectToSerialize->Save(false);
  }

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

    return GetMax(0, pNodeAnim->mNumRotationKeys - 2);
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

    return GetMax(0, pNodeAnim->mNumScalingKeys - 2);
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

    float Factor            = (AnimationTime - (float) (pNodeAnim->mPositionKeys[PositionIndex].mTime)) / DeltaTime;
    Factor                  = glm::clamp(Factor, 0.0f, 1.0f);

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
    Factor       = glm::clamp(Factor, 0.0f, 1.0f);

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

    float Factor            = (AnimationTime - (float) (pNodeAnim->mScalingKeys[ScalingIndex].mTime)) / DeltaTime;
    Factor                  = glm::clamp(Factor, 0.0f, 1.0f);

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

      for (uint chIndx = 0; chIndx < anim->mNumChannels; chIndx++)
      {
        KeyArray keys;
        aiNodeAnim* nodeAnim = anim->mChannels[chIndx];

        // Determine the first tick where all channels have data.
        float firstPosTick   = (nodeAnim->mNumPositionKeys > 0) ? (float) nodeAnim->mPositionKeys[0].mTime : 0.0f;
        float firstRotTick   = (nodeAnim->mNumRotationKeys > 0) ? (float) nodeAnim->mRotationKeys[0].mTime : 0.0f;
        float firstSclTick   = (nodeAnim->mNumScalingKeys > 0) ? (float) nodeAnim->mScalingKeys[0].mTime : 0.0f;

        // Get the initial values for each channel (first key or defaults).
        aiVector3D initPos = (nodeAnim->mNumPositionKeys > 0) ? nodeAnim->mPositionKeys[0].mValue : aiVector3D(0, 0, 0);
        aiQuaternion initRot =
            (nodeAnim->mNumRotationKeys > 0) ? nodeAnim->mRotationKeys[0].mValue : aiQuaternion(1, 0, 0, 0);
        aiVector3D initScl = (nodeAnim->mNumScalingKeys > 0) ? nodeAnim->mScalingKeys[0].mValue : aiVector3D(1, 1, 1);

        // Find the earliest tick among all channels for this bone.
        float earliestTick = glm::min(firstPosTick, glm::min(firstRotTick, firstSclTick));

        // Convert earliest tick to frame number.
        uint earliestFrame = 0;
        if (earliestTick > g_animEps && fps > 0.0)
        {
          earliestFrame = (uint) glm::round((earliestTick / (float) fps) * g_desiredFps);
        }

        // If the animation doesn't start at frame 0, insert a hold key at frame 0 and at the frame
        // just before the animation starts, so there is no blend from T-pose.
        if (earliestFrame > 0)
        {
          Key holdKey;
          holdKey.m_frame    = 0;
          holdKey.m_position = Vec3(initPos.x, initPos.y, initPos.z);
          holdKey.m_rotation = Quaternion(initRot.x, initRot.y, initRot.z, initRot.w);
          holdKey.m_scale    = Vec3(initScl.x, initScl.y, initScl.z);
          keys.push_back(holdKey);

          if (earliestFrame > 1)
          {
            holdKey.m_frame = earliestFrame - 1;
            keys.push_back(holdKey);
          }
        }

        // Bake every frame: sample all three channels at every frame.
        for (uint frame = earliestFrame; frame <= frameCount; frame++)
        {
          float timeInTicks = (frame / g_desiredFps) * (float) fps;

          aiVector3D t;
          if (nodeAnim->mNumPositionKeys > 0)
          {
            if (timeInTicks <= firstPosTick)
            {
              t = initPos;
            }
            else
            {
              CalcInterpolatedPosition(t, timeInTicks, nodeAnim);
            }
          }
          else
          {
            t = initPos;
          }

          aiQuaternion r;
          if (nodeAnim->mNumRotationKeys > 0)
          {
            if (timeInTicks <= firstRotTick)
            {
              r = initRot;
            }
            else
            {
              CalcInterpolatedRotation(r, timeInTicks, nodeAnim);
            }
          }
          else
          {
            r = initRot;
          }

          aiVector3D s;
          if (nodeAnim->mNumScalingKeys > 0)
          {
            if (timeInTicks <= firstSclTick)
            {
              s = initScl;
            }
            else
            {
              CalcInterpolatedScaling(s, timeInTicks, nodeAnim);
            }
          }
          else
          {
            s = initScl;
          }

          Key tKey;
          tKey.m_frame    = frame;
          tKey.m_position = Vec3(t.x, t.y, t.z);
          tKey.m_rotation = Quaternion(r.x, r.y, r.z, r.w);
          tKey.m_scale    = Vec3(s.x, s.y, s.z);
          keys.push_back(tKey);
        }

        // Resolve the channel to a skeleton bone when possible. Duplicated
        // node names are disambiguated with the channel's initial values
        // against the candidates' bind transforms.
        String boneKeyName = nodeAnim->mNodeName.C_Str();
        auto candidatesIt  = g_nameToNodes.find(boneKeyName);
        if (candidatesIt != g_nameToNodes.end())
        {
          vector<const aiNode*> boneCandidates;
          for (const aiNode* candidate : candidatesIt->second)
          {
            if (g_nodeToEntry.find(candidate) != g_nodeToEntry.end())
            {
              boneCandidates.push_back(candidate);
            }
          }

          if (boneCandidates.size() == 1)
          {
            boneKeyName = g_nodeToEntry[boneCandidates[0]]->name;
          }
          else if (boneCandidates.size() > 1)
          {
            // Multiple bones share the channel name (FBX also merges their
            // curves into this one channel, so the values may already be
            // conflated). Pick the bone whose bind transform matches the
            // channel's initial values and report it.
            float bestDist     = std::numeric_limits<float>::max();
            const aiNode* best = nullptr;
            for (const aiNode* candidate : boneCandidates)
            {
              Vec3 t, s;
              Quaternion r;
              DecomposeAssimpMatrix(candidate->mTransformation, &t, &r, &s);

              float tDist = glm::length2(t - Vec3(initPos.x, initPos.y, initPos.z));
              float sDist = glm::length2(s - Vec3(initScl.x, initScl.y, initScl.z));
              float rDist = 1.0f - glm::abs(glm::dot(r, Quaternion(initRot.x, initRot.y, initRot.z, initRot.w)));
              float dist  = tDist + sDist + rDist;
              if (dist < bestDist)
              {
                bestDist = dist;
                best     = candidate;
              }
            }

            if (best != nullptr)
            {
              boneKeyName = g_nodeToEntry[best]->name;
              if (bestDist > g_animChannelMatchEps)
              {
                Assimp::DefaultLogger::get()->warn("Ambiguous animation channel '" + boneKeyName +
                                                   "' matches no bone bind transform; it is assigned to the closest "
                                                   "bone.");
              }
            }
          }
        }

        tAnim->m_keys.insert(std::make_pair(boneKeyName, keys));
      }

      // For skeleton bones that have no animation channel, write their T-pose transform
      // to every frame so the runtime doesn't reset them to origin.
      if (!g_skeletonMap.empty())
      {
        for (auto& skelEntry : g_skeletonMap)
        {
          const string& boneName = skelEntry.first;
          if (tAnim->m_keys.find(boneName) == tAnim->m_keys.end())
          {
            // Get T-pose local transform from the scene node.
            aiNode* boneNode = skelEntry.second.boneNode;
            Vec3 t, s;
            Quaternion r;
            DecomposeAssimpMatrix(boneNode->mTransformation, &t, &r, &s);

            KeyArray keys;
            for (uint frame = 0; frame <= frameCount; frame++)
            {
              Key tKey;
              tKey.m_frame    = frame;
              tKey.m_position = t;
              tKey.m_rotation = r;
              tKey.m_scale    = s;
              keys.push_back(tKey);
            }
            tAnim->m_keys.insert(std::make_pair(boneName, keys));
          }
        }
      }

      tAnim->m_duration = (float) duration;
      tAnim->m_fps      = (float) g_desiredFps;

      CreateFileAndSerializeObject(tAnim.get(), animFilePath);
    }
  }

  void ImportMaterial(const string& matDir, const string& texDir, const string& origin)
  {
    fs::path pathOrg              = fs::path(origin).parent_path();

    auto textureFindAndCreateFunc = [texDir, pathOrg](aiTextureType textureAssimpType,
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
        string textPath = NormalizePath(PathToString(fs::path(texDir + fileName).lexically_normal()));

        if (!embedded && !CheckSystemFile(textPath))
        {
          // Try copying the texture next to the imported assets. FBX files
          // commonly store the absolute texture path of the authoring machine
          // (e.g. "U:\...\Tex.png"), so try the reference as-is first and
          // fall back to the bare file name inside the source folder.
          fs::path fullPath(NormalizePath(tName));
          if (!fullPath.is_absolute())
          {
            fullPath = pathOrg / fullPath;
          }
          fullPath = fullPath.lexically_normal();

          if (!CheckSystemFile(PathToString(fullPath)))
          {
            fullPath = (pathOrg / fileName).lexically_normal();
          }

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
          else
          {
            Assimp::DefaultLogger::get()->warn(
                "Texture not found: '" + fileName + "' (referenced as '" + tName +
                "'). Locate it with the search window when the editor asks for the missing file.");
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
      string writePath      = matDir + name + MATERIAL;
      MaterialPtr tMaterial = MakeNewPtr<Material>();

      // Diffuse / Base Color texture
      TexturePtr diffuse    = textureFindAndCreateFunc(aiTextureType_DIFFUSE, material);
      if (!diffuse)
      {
        diffuse = textureFindAndCreateFunc(aiTextureType_BASE_COLOR, material);
      }
      if (diffuse)
      {
        tMaterial->SetDiffuseTextureVal(diffuse);
      }

      // Base color factor
      aiColor4D baseColor;
      if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) == aiReturn_SUCCESS)
      {
        tMaterial->SetColorVal(Vec3(baseColor.r, baseColor.g, baseColor.b));
        if (baseColor.a < 1.0f)
        {
          tMaterial->SetAlphaVal(baseColor.a);
        }
      }
      else
      {
        aiColor4D diffuseColor;
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == aiReturn_SUCCESS)
        {
          tMaterial->SetColorVal(Vec3(diffuseColor.r, diffuseColor.g, diffuseColor.b));
          if (diffuseColor.a < 1.0f)
          {
            tMaterial->SetAlphaVal(diffuseColor.a);
          }
        }
      }

      // Emissive texture
      TexturePtr emissive = textureFindAndCreateFunc(aiTextureType_EMISSIVE, material);
      if (!emissive)
      {
        emissive = textureFindAndCreateFunc(aiTextureType_EMISSION_COLOR, material);
      }
      if (emissive)
      {
        tMaterial->SetEmissiveTextureVal(emissive);
      }

      // Emissive color
      aiColor3D emissiveColor;
      if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == aiReturn_SUCCESS)
      {
        Vec3 emColor            = Vec3(emissiveColor.r, emissiveColor.g, emissiveColor.b);

        // Apply emissive intensity if available
        float emissiveIntensity = 1.0f;
        if (material->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity) == aiReturn_SUCCESS)
        {
          emColor *= emissiveIntensity;
        }

        tMaterial->SetEmissiveColorVal(emColor);
      }

      // Metallic-Roughness texture
      TexturePtr metallicRoughness = textureFindAndCreateFunc(aiTextureType_UNKNOWN, material);
      if (!metallicRoughness)
      {
        metallicRoughness = textureFindAndCreateFunc(aiTextureType_METALNESS, material);
      }
      if (metallicRoughness)
      {
        tMaterial->SetMetallicRoughnessTextureVal(metallicRoughness);
      }

      // Metallic and Roughness factors
      float metalness, roughness;
      if (material->Get(AI_MATKEY_METALLIC_FACTOR, metalness) == aiReturn_SUCCESS)
      {
        tMaterial->SetMetallicVal(metalness);
      }
      if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == aiReturn_SUCCESS)
      {
        tMaterial->SetRoughnessVal(roughness);
      }

      // Normal texture
      TexturePtr normal = textureFindAndCreateFunc(aiTextureType_NORMALS, material);
      if (normal)
      {
        tMaterial->SetNormalTextureVal(normal);
      }

      // Two-sided / Cull mode
      int twoSided = 0;
      if (material->Get(AI_MATKEY_TWOSIDED, twoSided) == aiReturn_SUCCESS && twoSided)
      {
        tMaterial->cullMode = CullingType::TwoSided;
      }

      // Alpha mode handling (glTF: OPAQUE, MASK, BLEND)
      aiString alphaMode;
      if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == aiReturn_SUCCESS)
      {
        string mode = alphaMode.C_Str();
        if (mode == "BLEND")
        {
          tMaterial->blendFunction = BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;

          // Read alpha/opacity for blend mode
          float transparency       = 1.0f;
          if (material->Get(AI_MATKEY_OPACITY, transparency) == aiReturn_SUCCESS)
          {
            tMaterial->SetAlphaVal(transparency);
          }
        }
        else if (mode == "MASK")
        {
          tMaterial->blendFunction = BlendFunction::ALPHA_MASK;

          float alphaCutoff        = 0.5f;
          material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff);
          tMaterial->alphaMaskTreshold = alphaCutoff;
        }
        // OPAQUE: default, no blending needed
      }
      else
      {
        // Non-glTF fallback: try various alpha retrieval methods
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
            tMaterial->blendFunction = BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;
          }
          else
          {
            tMaterial->blendFunction = BlendFunction::ONE_TO_ONE;
          }
        }
        else if (transparency != 1.0f)
        {
          tMaterial->blendFunction = BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;
        }

        material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, tMaterial->alphaMaskTreshold);
      }

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
        auto entryIt = g_aiBoneToEntry.find(bone);
        if (entryIt == g_aiBoneToEntry.end())
        {
          // The bone could not be matched to a scene node (see
          // ImportSkeleton). Drop its weights instead of pointing them at a
          // wrong bone.
          Assimp::DefaultLogger::get()->warn(string("Bone '") + bone->mName.C_Str() +
                                             "' has no matching scene node; its weights are dropped.");
          continue;
        }

        const BoneNode& bn = *entryIt->second;
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
        Vec3 T     = Vec3(mesh->mTangents[vIndex].x, mesh->mTangents[vIndex].y, mesh->mTangents[vIndex].z);
        Vec3 B     = Vec3(mesh->mBitangents[vIndex].x, mesh->mBitangents[vIndex].y, mesh->mBitangents[vIndex].z);
        float sign = glm::dot(glm::cross(v.norm, T), B) < 0.0f ? -1.0f : 1.0f;
        v.tan      = Vec4(T, sign);
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

  void ImportMeshes(string& filePath)
  {
    string path, name;
    DecomposePath(filePath, &path, &name, nullptr);
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
        String meshPath = ConcatPaths({path, fileName + MESH});

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
      String skinMeshPath = ConcatPaths({path, name + SKINMESH});
      mainSkinMesh->SetFile(skinMeshPath);

      AddToUsedFiles(skinMeshPath);
      CreateFileAndSerializeObject(mainSkinMesh.get(), skinMeshPath);
    }
  }

  void ImportLights()
  {
    for (uint i = 0; i < g_scene->mNumLights; i++)
    {
      LightPtr tkLight  = nullptr;
      aiLight* light    = g_scene->mLights[i];
      float lightRadius = 10.0f;
      {
        // Calculate a finite radius from attenuation values for our PBR distance attenuation using a threshold.
        // Solving: a*d^2 + b*d + c = 1/threshold
        float threshold = 0.01f;
        float a         = light->mAttenuationQuadratic;
        float b         = light->mAttenuationLinear;
        float c         = light->mAttenuationConstant - (1.0f / threshold);

        if (a > 0.000001f)
        {
          float disc = (b * b) - (4.0f * a * c);
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
        else if (b > 0.000001f)
        {
          float t = -c / b;
          if (t > 0.0f)
          {
            lightRadius = t;
          }
        }
      }

      // Extract intensity from color using max component to preserve color ratios.
      Vec3 lightColor = Vec3(light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b);
      float intensity = glm::max(lightColor.r, glm::max(lightColor.g, lightColor.b));

      if (intensity > 0.00001f)
      {
        lightColor /= intensity;
      }
      else
      {
        intensity  = 1.0f;
        lightColor = Vec3(1.0f);
      }

      // glTF uses physical light units: candela for point/spot, lux for directional.
      float luxNormalization     = 10.0f;
      float candelaNormalization = 1.0f;
      if ((g_currentExt == ".glb" || g_currentExt == ".gltf"))
      {
        luxNormalization     = 1.0f / 10000.0f; // Day light is around 10,000 lux.
        candelaNormalization = 1.0f / 800.0f;   // A bright light bulb is around 800 candela.
      }

      if (light->mType == aiLightSource_DIRECTIONAL)
      {
        DirectionalLightPtr dirLight = MakeNewPtr<DirectionalLight>();
        dirLight->SetNameVal(light->mName.C_Str());
        dirLight->SetColorVal(lightColor);
        dirLight->SetIntensityVal(intensity * luxNormalization);
        tkLight = dirLight;
      }
      else if (light->mType == aiLightSource_POINT)
      {
        PointLightLightPtr pointLight = MakeNewPtr<PointLight>();
        pointLight->SetNameVal(light->mName.C_Str());
        pointLight->SetColorVal(lightColor);
        pointLight->SetIntensityVal(intensity * candelaNormalization);
        pointLight->SetRadiusVal(lightRadius);
        tkLight = pointLight;
      }
      else if (light->mType == aiLightSource_SPOT)
      {
        SpotLightPtr spotLight = MakeNewPtr<SpotLight>();
        spotLight->SetNameVal(light->mName.C_Str());
        spotLight->SetColorVal(lightColor);
        spotLight->SetIntensityVal(intensity * candelaNormalization);
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

      float aspect               = cam->mAspect > 0.0f ? cam->mAspect : 1.0f;

      // Convert horizontal to vertical FOV.
      float tanHalfHorizontalFov = std::tan(cam->mHorizontalFOV * 0.5f);
      float fov                  = 2.0f * std::atan(tanHalfHorizontalFov / aspect);

      // Camera transform is handled by the scene node in TraverseScene.
      // So we only set the lens parameters here.
      tkCam->SetLens(fov, aspect, cam->mClipPlaneNear, cam->mClipPlaneFar);

      sceneCameras.push_back(tkCam);
    }
  }

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

    ntt->m_node->SetTranslation(t, TransformationSpace::TS_LOCAL);
    ntt->m_node->SetOrientation(rt, TransformationSpace::TS_LOCAL);
    ntt->m_node->SetScale(s);

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
    DecomposePath(filePath, &path, &name, nullptr);

    string fullPath = ConcatPaths({path, name + SCENE});
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

  aiMatrix4x4 GetAiNodeWorldTransform(const aiNode* node, unordered_map<const aiNode*, aiMatrix4x4>& cache)
  {
    auto cachedIt = cache.find(node);
    if (cachedIt != cache.end())
    {
      return cachedIt->second;
    }

    aiMatrix4x4 world = node->mTransformation;
    if (node->mParent != nullptr)
    {
      world = GetAiNodeWorldTransform(node->mParent, cache) * world;
    }

    cache[node] = world;
    return world;
  }

  float AiMatrixDistance(const aiMatrix4x4& a, const aiMatrix4x4& b)
  {
    float maxDist   = 0.0f;
    const float* ap = &a.a1;
    const float* bp = &b.a1;
    for (int i = 0; i < 16; i++)
    {
      maxDist = glm::max(maxDist, glm::abs(ap[i] - bp[i]));
    }

    return maxDist;
  }

  // NOTE: aiBone carries no link to its aiNode (assimp drops FBX object ids,
  // and even aiProcess_PopulateArmatureData resolves aiBone::mNode by name),
  // so bones are matched to scene nodes by name plus -- when several nodes
  // share the name -- the inverse bind matrix:
  //   nodeWorldAtBind == meshWorld * inverse(aiBone::mOffsetMatrix)
  // holds for both FBX and glTF2. Every distinct bone node then receives a
  // unique name ("finger", "finger_1", "finger_2", ... in scene DFS order),
  // which keeps the engine's unique-bone-name assumption intact. Two assimp
  // internal merges cannot be recovered here and are only warned about:
  // same-name clusters on one mesh collapse into a single aiBone, and
  // same-name FBX animation channels collapse into a single aiNodeAnim.
  void ImportSkeleton(string& filePath)
  {
    // Index every scene node by its original name (duplicates included).
    g_nameToNodes.clear();
    std::function<void(aiNode*)> indexNodesFn = [&indexNodesFn](aiNode* node) -> void
    {
      g_nameToNodes[node->mName.C_Str()].push_back(node);
      for (uint i = 0; i < node->mNumChildren; i++)
      {
        indexNodesFn(node->mChildren[i]);
      }
    };
    indexNodesFn(g_scene->mRootNode);

    // Map every aiMesh to the aiNode that carries it. FindNode would return
    // the first node with the mesh's name, which is wrong when several nodes
    // share a name.
    unordered_map<const aiMesh*, aiNode*> meshNodes;
    std::function<void(aiNode*)> indexMeshesFn = [&indexMeshesFn, &meshNodes](aiNode* node) -> void
    {
      for (uint i = 0; i < node->mNumMeshes; i++)
      {
        meshNodes[g_scene->mMeshes[node->mMeshes[i]]] = node;
      }
      for (uint i = 0; i < node->mNumChildren; i++)
      {
        indexMeshesFn(node->mChildren[i]);
      }
    };
    indexMeshesFn(g_scene->mRootNode);

    // Resolve every aiBone to the exact aiNode it belongs to.
    struct BoneResolution
    {
      aiBone* bone     = nullptr;
      aiNode* node     = nullptr;
      aiNode* meshNode = nullptr;
    };
    vector<BoneResolution> resolutions;
    unordered_set<string> unresolvedBoneNames;

    unordered_map<const aiNode*, aiMatrix4x4> worldCache;
    for (uint meshIndx = 0; meshIndx < g_scene->mNumMeshes; meshIndx++)
    {
      aiMesh* mesh     = g_scene->mMeshes[meshIndx];
      auto meshNodeIt  = meshNodes.find(mesh);
      aiNode* meshNode = (meshNodeIt != meshNodes.end()) ? meshNodeIt->second : nullptr;

      for (uint boneIndx = 0; boneIndx < mesh->mNumBones; boneIndx++)
      {
        aiBone* bone = mesh->mBones[boneIndx];

        aiNode* resolvedNode              = nullptr;
        auto candsIt                      = g_nameToNodes.find(bone->mName.C_Str());
        if (candsIt != g_nameToNodes.end() && candsIt->second.size() == 1)
        {
          resolvedNode = candsIt->second[0];
        }
        else if (candsIt != g_nameToNodes.end() && !candsIt->second.empty())
        {
          // Several nodes share the bone's name. Assimp merges same-name
          // clusters per mesh, so candidates only collide across meshes and
          // their inverse bind matrices tell them apart.
          aiMatrix4x4 offsetInv = bone->mOffsetMatrix;
          offsetInv.Inverse();
          aiMatrix4x4 expectedWorld = offsetInv;
          if (meshNode != nullptr)
          {
            expectedWorld = GetAiNodeWorldTransform(meshNode, worldCache) * offsetInv;
          }

          float bestDist = std::numeric_limits<float>::max();
          for (aiNode* cand : candsIt->second)
          {
            float dist = AiMatrixDistance(GetAiNodeWorldTransform(cand, worldCache), expectedWorld);
            if (dist < bestDist)
            {
              bestDist     = dist;
              resolvedNode = cand;
            }
          }

          if (bestDist > g_boneMatrixMatchEps)
          {
            // No candidate matches the bind pose. Fall back to the first
            // candidate; the assignment may be wrong, so report it.
            Assimp::DefaultLogger::get()->warn("Bone '" + string(bone->mName.C_Str()) +
                                               "' matches no scene node by bind pose; the first same-name node is "
                                               "used.");
            resolvedNode = candsIt->second[0];
          }
        }

        if (resolvedNode == nullptr)
        {
          unresolvedBoneNames.insert(bone->mName.C_Str());
          continue;
        }

        resolutions.push_back({bone, resolvedNode, meshNode});
      }
    }

    if (resolutions.empty())
    {
      return;
    }

    if (!unresolvedBoneNames.empty())
    {
      string names;
      size_t shown = 0;
      for (const string& boneName : unresolvedBoneNames)
      {
        if (shown++ >= 8)
        {
          names += "...";
          break;
        }

        if (!names.empty())
        {
          names += ", ";
        }
        names += "'" + boneName + "'";
      }

      Assimp::DefaultLogger::get()->warn("Bones with no matching scene node: " + names +
                                         ". Their weights are dropped and they are not added to the skeleton.");
    }

    // Collect every node that becomes a bone. Node identity is the key here;
    // names may still collide at this point.
    unordered_set<const aiNode*> boneNodes;
    auto addBoneNodeFn = [&boneNodes](const aiNode* node) -> void { boneNodes.insert(node); };

    for (const BoneResolution& res : resolutions)
    {
      const aiNode* node = res.node;
      while (node) // Go Up
      {
        if (node == res.meshNode)
        {
          break;
        }

        if (res.meshNode != nullptr)
        {
          if (node == res.meshNode->mParent)
          {
            break;
          }
        }

        addBoneNodeFn(node);
        node = node->mParent;
      }

      // Go Down
      std::function<void(const aiNode*)> checkDownFn = [&checkDownFn, &addBoneNodeFn](const aiNode* node) -> void
      {
        if (node == nullptr)
        {
          return;
        }

        addBoneNodeFn(node);

        for (uint i = 0; i < node->mNumChildren; i++)
        {
          checkDownFn(node->mChildren[i]);
        }
      };
      checkDownFn(res.node);
    }

    // A node named after a bone keeps the bind pose of the bone that resolved
    // to it. Last resolution wins, same as the old name-keyed lookup.
    unordered_map<const aiNode*, aiBone*> nodeBindBones;
    for (const BoneResolution& res : resolutions)
    {
      nodeBindBones[res.node] = res.bone;
    }

    // Assign a unique name to every bone node. The first occurrence keeps the
    // original name; later occurrences get "_1", "_2", ... suffixes. Names of
    // non-bone scene nodes are reserved so generated names never collide with
    // them.
    unordered_set<string> takenNames;
    std::function<void(const aiNode*)> reserveNodeNamesFn = [&reserveNodeNamesFn, &takenNames, &boneNodes](
                                                                const aiNode* node) -> void
    {
      if (boneNodes.find(node) == boneNodes.end())
      {
        takenNames.insert(node->mName.C_Str());
      }
      for (uint i = 0; i < node->mNumChildren; i++)
      {
        reserveNodeNamesFn(node->mChildren[i]);
      }
    };
    reserveNodeNamesFn(g_scene->mRootNode);

    unordered_map<string, uint> nameCounters;
    vector<std::pair<string, string>> renamedBones;
    g_skeletonMap.clear();
    g_nodeToEntry.clear();
    g_aiBoneToEntry.clear();

    std::function<void(aiNode*)> nameBonesFn =
        [&nameBonesFn, &boneNodes, &nameCounters, &takenNames, &renamedBones, &nodeBindBones](aiNode* node) -> void
    {
      if (boneNodes.find(node) != boneNodes.end())
      {
        const string originalName = node->mName.C_Str();
        uint& counter             = nameCounters[originalName];
        string uniqueName         = (counter == 0) ? originalName : originalName + "_" + to_string(counter);
        while (takenNames.find(uniqueName) != takenNames.end())
        {
          counter++;
          uniqueName = originalName + "_" + to_string(counter);
        }
        takenNames.insert(uniqueName);

        if (uniqueName != originalName)
        {
          renamedBones.push_back({originalName, uniqueName});
        }

        BoneNode bn(node, 0);
        bn.name = uniqueName;
        auto bindBoneIt = nodeBindBones.find(node);
        if (bindBoneIt != nodeBindBones.end())
        {
          bn.bone = bindBoneIt->second;
        }

        g_skeletonMap[uniqueName] = bn;
        g_nodeToEntry[node]       = &g_skeletonMap[uniqueName];
      }

      for (uint i = 0; i < node->mNumChildren; i++)
      {
        nameBonesFn(node->mChildren[i]);
      }
    };
    nameBonesFn(g_scene->mRootNode);

    // Map every aiBone to its (renamed) skeleton entry so meshes can read the
    // right bone index per bone instead of looking the bone's name up.
    for (const BoneResolution& res : resolutions)
    {
      auto entryIt = g_nodeToEntry.find(res.node);
      assert(entryIt != g_nodeToEntry.end());
      g_aiBoneToEntry[res.bone] = entryIt->second;
    }

    if (!renamedBones.empty())
    {
      string names;
      size_t shown = 0;
      for (const auto& rename : renamedBones)
      {
        if (shown++ >= 8)
        {
          names += "...";
          break;
        }

        if (!names.empty())
        {
          names += ", ";
        }
        names += "'" + rename.first + "' -> '" + rename.second + "'";
      }

      Assimp::DefaultLogger::get()->warn(
          "Duplicated bone node names detected: " + names +
          ". Distinct bones that share a name are renamed with _N suffixes so every bone is imported separately.");
    }

    // Assign indices. Every bone node now has a unique name, so there is one
    // index per bone. Indices follow the same DFS order used below to build
    // m_bones, so mesh vertex bone indices match the serialized skeleton.
    std::function<void(aiNode*, uint&)> assignBoneIndexFn = [&assignBoneIndexFn](aiNode* node, uint& index) -> void
    {
      auto entryIt = g_nodeToEntry.find(node);
      if (entryIt != g_nodeToEntry.end())
      {
        entryIt->second->boneIndex = index++;
      }

      for (uint i = 0; i < node->mNumChildren; i++)
      {
        assignBoneIndexFn(node->mChildren[i], index);
      }
    };

    uint boneIndex = 0;
    assignBoneIndexFn(g_scene->mRootNode, boneIndex);

    string name, path;
    DecomposePath(filePath, &path, &name, nullptr);
    string fullPath = ConcatPaths({path, name + SKELETON});

    g_skeleton      = MakeNewPtr<Skeleton>();
    g_skeleton->SetFile(fullPath);

    // Build the bone hierarchy and T-pose map from the renamed entries.
    std::function<void(aiNode*, DynamicBoneMap::DynamicBone*)> setBoneHierarchyFn =
        [&setBoneHierarchyFn](aiNode* node, DynamicBoneMap::DynamicBone* parentBone) -> void
    {
      DynamicBoneMap::DynamicBone* searchDBone = parentBone;
      auto entryIt                             = g_nodeToEntry.find(node);
      if (entryIt != g_nodeToEntry.end())
      {
        BoneNode* entry = entryIt->second;
        assert(node->mName.length);
        g_skeleton->m_Tpose.m_boneMap.insert(std::make_pair(entry->name, DynamicBoneMap::DynamicBone()));

        searchDBone                       = &g_skeleton->m_Tpose.m_boneMap.find(entry->name)->second;
        searchDBone->node                 = new Node();
        searchDBone->node->m_inheritScale = true;
        searchDBone->boneIndx             = entry->boneIndex;
        g_skeleton->m_Tpose.AddDynamicBone(entry->name, *searchDBone, parentBone);

        StaticBone* sBone = new StaticBone(entry->name);
        g_skeleton->m_bones.push_back(sBone);
      }
      for (uint i = 0; i < node->mNumChildren; i++)
      {
        setBoneHierarchyFn(node->mChildren[i], searchDBone);
      }
    };

    std::function<void(aiNode*)> setTransformationsFn = [&setTransformationsFn](aiNode* node) -> void
    {
      auto entryIt = g_nodeToEntry.find(node);
      if (entryIt != g_nodeToEntry.end())
      {
        BoneNode* entry   = entryIt->second;
        StaticBone* sBone = g_skeleton->GetBone(entry->name);

        // Set bone node transformation
        {
          DynamicBoneMap::DynamicBone& dBone = g_skeleton->m_Tpose.m_boneMap[entry->name];

          // Set translation directly
          Vec3 t, s;
          Quaternion r;
          DecomposeAssimpMatrix(node->mTransformation, &t, &r, &s);

          dBone.node->SetTranslation(t);
          dBone.node->SetOrientation(r);
          dBone.node->SetScale(s);
        }

        // Set bind pose transformation
        {
          aiBone* bone = entry->bone;

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
      // Do not split FBX pivots (PreRotation, RotationOffset, ...) into
      // separate _$AssimpFbx$_* chain nodes. Both the skeleton and the
      // animation-only exports then use the plain bone names, so animation
      // channels match the skeleton regardless of how the DCC exported them.
      importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

      int optimizationLevel = 0; // 0 or 1
      string dest, file = argv[1];
      Assimp::DefaultLogger::create("Assimplog.txt", Assimp::Logger::VERBOSE);
      for (int i = 0; i < argc; i++)
      {
        string arg = argv[i];
        Assimp::DefaultLogger::get()->info(arg);

        if (arg == "-t")
        {
          dest = PathToString(fs::path(argv[i + 1]).append(""));
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

      // Resources are written under Temp/<type-layer>/<target>/... mirroring
      // the editor's resource tree (Textures/, Materials/, Meshes/, Prefabs/).
      // GetRelativeResourcePath() -- with m_resourceRoot "Temp" -- then strips
      // Temp + the type layer and leaves "<target>/file" in every serialized
      // cross-reference, exactly what TexturePath/MeshPath/MaterialPath resolve
      // in the editor's Resources tree. Writing flat (Temp/<target>/) instead
      // would make that layer-strip eat the user's target subdirectory.
      string subDest = dest;
      auto makeDest  = [&](const string& layer) -> string
      {
        string d = ConcatPaths({"Temp", layer, subDest});
        return NormalizePath(PathToString(fs::path(d).lexically_normal()));
      };
      string texDest    = makeDest("Textures");
      string matDest    = makeDest("Materials");
      string meshDest   = makeDest("Meshes");
      string prefabDest = makeDest("Prefabs");
      for (const string& d : {texDest, matDest, meshDest, prefabDest})
      {
        if (!d.empty())
        {
          fs::create_directories(d);
        }
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

      g_proxy->SetDefaultPath(ConcatPaths({"..", "Resources", "Engine"}));
      g_proxy->SetConfigPath(ConcatPaths({"..", "Config"}));

      // Headless mode: use NullBackend (no GPU), dummy SDL video driver.
      // Import only serializes resources — it never renders.
      SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
      RenderSystem::UseNullBackend();

      g_proxy->PreInit();

      GetLogger()->SetPlatformConsoleFn([](LogType type, const String& msg) -> void
                                        { ToolKit::PlatformHelpers::OutputLog((int) type, msg.c_str()); });

      // Init SDL with dummy driver (no video / GPU required).
      // Only events and gamecontroller are needed for the engine to boot.
      SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER);

      // No window, no GL context, no InitGraphics.
      // RenderSystem already created a NullBackend (UseNullBackend was called
      // before PreInit), so all GPU calls are no-ops.
      g_proxy->m_renderSys->SetPresentCallback([]() { /* headless, no swap */ });

      g_proxy->Init();

      // Resources live under Temp/<type-layer>/<target>/... (see makeDest
      // above). Set "Temp" as the resource root so GetRelativeResourcePath()
      // strips Temp + the type layer from every cross-reference it serializes
      // inside .material / .mesh / .scene files, leaving "<target>/file".
      g_proxy->m_resourceRoot = "Temp";

      for (int i = 0; i < (int) (files.size()); i++)
      {
        file = files[i];
        // Clear global materials for each scene to prevent wrong referencing
        tMaterials.clear();

        int optFlags = aiProcess_GlobalScale | aiProcess_Triangulate;
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
        string meshDestFile   = meshDest + fileName;
        string prefabDestFile = prefabDest + fileName;
        // DON'T BREAK THE CALLING ORDER!

        // Create Textures to reference in Materials
        ImportTextures(texDest);

        // Create Materials to reference in Meshes
        ImportMaterial(matDest, texDest, file);

        // Create a Skeleton to reference in Meshes
        ImportSkeleton(meshDestFile);

        // Import animations after skeleton so g_skeletonMap is available.
        ImportAnimation(meshDest);

        // Add Meshes.
        ImportMeshes(meshDestFile);

        // Add lights.
        ImportLights();

        // Add cameras.
        ImportCameras();

        // Create Meshes & Scene
        ImportScene(prefabDestFile);
      }

      // Report all in use files.
      fstream inUse("out.txt", ios::out);
      for (const string& fs : g_usedFiles)
      {
        inUse << fs << endl;
      }
      inUse.close();

      // Uninit globals
      g_skeleton = nullptr;
      g_skeletonMap.clear();
      g_aiBoneToEntry.clear();
      g_nodeToEntry.clear();
      g_nameToNodes.clear();
      tMaterials.clear();
      g_meshes.clear();
      mainSkinMesh = nullptr;
      sceneLights.clear();
      sceneCameras.clear();
      deletedEntities.clear();

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
/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Mesh.h"

#include "Common/base64.h"
#include "FileManager.h"
#include "IGraphicsBackend.h"
#include "Material.h"
#include "MathUtil.h"
#include "RHI.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "ResourceManager.h"
#include "Skeleton.h"
#include "Stats.h"
#include "TKAssert.h"
#include "Texture.h"
#include "Threads.h"
#include "ToolKit.h"
#include "Util.h"

#include "DebugNew.h"

static constexpr bool SERIALIZE_MESH_AS_BINARY = true;

namespace ToolKit
{


  // Mesh
  //////////////////////////////////////////

  TKDefineClass(Mesh, Resource);

  Mesh::Mesh()
  {
    m_material     = GetMaterialManager()->GetCopyOfDefaultMaterial(false);
    m_vertexLayout = VertexLayout::Mesh;
  }

  Mesh::Mesh(const String& file) : Mesh() { SetFile(file); }

  Mesh::~Mesh() { UnInit(); }

  void Mesh::Init(bool flushClientSideArray)
  {
    if (m_initiated)
    {
      return;
    }

    TK_ASSERT_ONCE(!m_clientSideVertices.empty() || m_vertexLayout == VertexLayout::SkinMesh);

    IGraphicsBackend* backend = GetRenderSystem()->GetRenderer()->GetBackend();
    backend->CreateMesh(this);

    Stats::AddVRAMUsageInBytes(GetVertexSize() * (uint64) m_vertexCount);
    if (m_indexCount > 0)
    {
      Stats::AddVRAMUsageInBytes(sizeof(uint) * (uint64) m_indexCount);
    }

    if (flushClientSideArray)
    {
      ClearClientVertexData();
      m_clientSideIndices.clear();
    }
    else
    {
      ConstructFaces();
    }

    m_material->Init(flushClientSideArray);

    for (const MeshPtr& mesh : m_subMeshes)
    {
      mesh->Init(flushClientSideArray);
    }

    GetAllMeshes(m_allMeshes, true);

    m_initiated = true;
  }

  void Mesh::UnInit()
  {
    if (m_initiated)
    {
      if (m_gpuData)
      {
        Stats::RemoveVRAMUsageInBytes(GetVertexSize() * m_vertexCount);
        Stats::RemoveVRAMUsageInBytes(sizeof(uint) * m_indexCount);
      }

      GetRenderSystem()->GetRenderer()->GetBackend()->DestroyMesh(this);
    }

    m_subMeshes.clear();
    m_initiated = false;
  }

  void Mesh::Load()
  {
    if (!m_loaded)
    {
      ParseDocument("meshContainer");
      m_loaded = true;
    }
  }

  void Mesh::Save(bool onlyIfDirty)
  {
    if (onlyIfDirty)
    {
      // if the mesh is dirty, false needs to be send to save always.
      Resource::Save(!m_dirty && !m_material->m_dirty);
    }
    else
    {
      Resource::Save(false);
    }

    m_material->Save(onlyIfDirty);
  }

  void Mesh::CopyTo(Resource* other)
  {
    Super::CopyTo(other);
    Mesh* cpy                 = static_cast<Mesh*>(other);
    cpy->m_clientSideVertices = m_clientSideVertices;
    cpy->m_vertexCount        = m_vertexCount;
    cpy->m_clientSideIndices  = m_clientSideIndices;
    cpy->m_indexCount         = m_indexCount;
    cpy->m_faces              = m_faces;

    // Upload copy's client-side data (already copied above) to GPU.
    if (m_vertexCount > 0)
    {
      IGraphicsBackend* backend = GetRenderSystem()->GetRenderer()->GetBackend();
      backend->CreateMesh(cpy);

      Stats::AddVRAMUsageInBytes((uint64) GetVertexSize() * cpy->m_vertexCount);
      if (cpy->m_indexCount > 0)
      {
        Stats::AddVRAMUsageInBytes(sizeof(uint) * (uint64) cpy->m_indexCount);
      }
    }

    cpy->m_material    = GetMaterialManager()->Copy<Material>(m_material);
    cpy->m_boundingBox = m_boundingBox;

    for (MeshPtr child : m_subMeshes)
    {
      MeshPtr childCopy = GetMeshManager()->Copy<Mesh>(child);
      cpy->m_subMeshes.push_back(childCopy);
    }
  }

  int Mesh::GetVertexSize() const { return sizeof(Vertex); }

  const void* Mesh::GetClientVertexData() const { return m_clientSideVertices.empty() ? nullptr : m_clientSideVertices.data(); }

  size_t Mesh::GetClientVertexCount() const { return m_clientSideVertices.size(); }

  void Mesh::ClearClientVertexData() { m_clientSideVertices.clear(); }

  uint Mesh::GetVertexCount() const { return (uint) m_clientSideVertices.size(); }

  bool Mesh::IsSkinned() const { return false; }

  void Mesh::CalculateAABB()
  {
    // Construct aabb of all submeshes.
    MeshRawPtrArray meshes;
    GetAllMeshes(meshes, true);

    BoundingBox aabb;
    for (Mesh* mesh : meshes)
    {
      for (size_t i = 0; i < mesh->m_clientSideVertices.size(); i++)
      {
        Vertex& v = mesh->m_clientSideVertices[i];
        aabb.UpdateBoundary(v.pos);
      }
    }
    m_boundingBox = aabb;
  }

  void GetAllMeshHelper(const Mesh* mesh, MeshRawPtrArray& meshes)
  {
    if (mesh == nullptr)
    {
      return;
    }

    meshes.push_back(const_cast<Mesh*>(mesh));

    for (MeshPtr subMesh : mesh->m_subMeshes)
    {
      GetAllMeshHelper(subMesh.get(), meshes);
    }
  }

  void GetAllMeshHelper(MeshPtr mesh, MeshPtrArray& meshes)
  {
    if (mesh == nullptr)
    {
      return;
    }

    meshes.push_back(mesh);

    for (MeshPtr subMesh : mesh->m_subMeshes)
    {
      GetAllMeshHelper(subMesh, meshes);
    }
  }

  void Mesh::GetAllMeshes(MeshRawPtrArray& meshes, bool updateCache) const
  {
    if (updateCache)
    {
      m_allMeshes.clear();
      GetAllMeshHelper(this, m_allMeshes);
    }

    meshes = m_allMeshes;
  }

  void Mesh::GetAllSubMeshes(MeshPtrArray& meshes) const
  {
    for (MeshPtr mesh : m_subMeshes)
    {
      meshes.push_back(mesh);
      GetAllMeshHelper(mesh, meshes);
    }
  }

  const MeshRawPtrArray& Mesh::GetAllMeshes() const { return m_allMeshes; }

  int Mesh::GetMeshCount() const { return (int) m_allMeshes.size(); }

  uint Mesh::TotalVertexCount() const
  {
    uint total = 0;
    for (Mesh* mesh : m_allMeshes)
    {
      total += mesh->m_vertexCount;
    }

    return total;
  }

  template <typename T>
  void ConstructFacesT(T* mesh)
  {
    size_t triCnt = mesh->m_clientSideIndices.size() / 3;
    if (mesh->m_clientSideIndices.empty())
    {
      triCnt = mesh->m_clientSideVertices.size() / 3;
    }

    mesh->m_faces.resize(triCnt);
    for (size_t i = 0; i < triCnt; i++)
    {
      if (mesh->m_clientSideIndices.empty())
      {
        for (size_t j = 0; j < 3; j++)
        {
          mesh->m_faces[i].vertices[j] = &mesh->m_clientSideVertices[i * 3 + j];
        }
      }
      else
      {
        for (size_t j = 0; j < 3; j++)
        {
          size_t indx                  = mesh->m_clientSideIndices[i * 3 + j];
          mesh->m_faces[i].vertices[j] = &mesh->m_clientSideVertices[indx];
        }
      }
    }
  }

  void Mesh::ConstructFaces()
  {
    if (IsSkinned())
    {
      ConstructFacesT(reinterpret_cast<SkinMesh*>(this));
    }
    else
    {
      ConstructFacesT(this);
    }
  }

  void Mesh::ApplyTransform(const Mat4& transform)
  {
    Mat4 its = glm::inverseTranspose(transform);
    for (Vertex& v : m_clientSideVertices)
    {
      v.pos    = transform * Vec4(v.pos, 1.0f);
      v.norm   = glm::normalize(its * Vec4(v.norm, 0.0f));
      float tw = v.tan.w;
      v.tan    = Vec4(Vec3(glm::normalize(its * Vec4(Vec3(v.tan), 0.0f))), tw);
    }
  }

  void Mesh::SetMaterial(MaterialPtr material)
  {
    m_material = material;
    m_material->Init();
    m_dirty = true;
  }

  template <typename T>
  void writeMesh(XmlDocument* doc, XmlNode* parent, const T* mesh)
  {
    XmlNode* meshNode;
    if constexpr (std::is_same<T, ToolKit::SkinMesh>::value)
    {
      meshNode = CreateXmlNode(doc, "skinMesh", parent);
    }
    else
    {
      meshNode = CreateXmlNode(doc, "mesh", parent);
    }

    WriteMaterial(meshNode, doc, mesh->m_material->GetSerializeFile());

    // Write Skeleton file reference
    if constexpr (std::is_same<T, ToolKit::SkinMesh>::value)
    {
      mesh->m_skeleton->SerializeRef(doc, meshNode);
    }

    XmlNode* vertices = CreateXmlNode(doc, "vertices", meshNode);

    // Serialize vertex
    if constexpr (SERIALIZE_MESH_AS_BINARY)
    {
      size_t vertexBufferDataSize = mesh->m_clientSideVertices.size() * sizeof(mesh->m_clientSideVertices[0]);
      if (vertexBufferDataSize > 0)
      {
        WriteAttr(vertices, doc, "VertexCount", std::to_string(mesh->m_clientSideVertices.size()));
        char* b64Data = new char[vertexBufferDataSize * 2];
        bintob64(b64Data, mesh->m_clientSideVertices.data(), vertexBufferDataSize);
        XmlNode* base64XML = CreateXmlNode(doc, "Base64", vertices);
        base64XML->value(doc->allocate_string(b64Data));
        SafeDelArray(b64Data);
      }
    }
    else
    {
      for (const auto& v : mesh->m_clientSideVertices)
      {
        XmlNode* vNod = CreateXmlNode(doc, "v", vertices);

        XmlNode* p    = CreateXmlNode(doc, "p", vNod);
        WriteVec(p, doc, v.pos);

        XmlNode* n = CreateXmlNode(doc, "n", vNod);
        WriteVec(n, doc, v.norm);

        XmlNode* t = CreateXmlNode(doc, "t", vNod);
        WriteVec(t, doc, v.tex);

        XmlNode* bt = CreateXmlNode(doc, "bt", vNod);
        WriteVec(bt, doc, v.btan);
        if constexpr (std::is_same<T, ToolKit::SkinMesh>::value)
        {
          XmlNode* b = CreateXmlNode(doc, "b", vNod);
          WriteVec(b, doc, v.bones);

          XmlNode* w = CreateXmlNode(doc, "w", vNod);
          WriteVec(w, doc, v.weights);
        }
      }
    }

    // Serialize faces
    XmlNode* faces = CreateXmlNode(doc, "faces", meshNode);
    if constexpr (SERIALIZE_MESH_AS_BINARY)
    {
      size_t facesBufferDataSize = mesh->m_clientSideIndices.size() * sizeof(mesh->m_clientSideIndices[0]);
      if (facesBufferDataSize > 0)
      {
        WriteAttr(faces, doc, "FaceCount", std::to_string(mesh->m_clientSideIndices.size()));
        char* b64Data = new char[facesBufferDataSize * 2];
        bintob64(b64Data, mesh->m_clientSideIndices.data(), facesBufferDataSize);
        XmlNode* base64XML = CreateXmlNode(doc, "Base64", faces);
        base64XML->value(doc->allocate_string(b64Data));
        SafeDelArray(b64Data);
      }
    }
    else
    {
      for (size_t i = 0; i < mesh->m_clientSideIndices.size() / 3; i++)
      {
        XmlNode* f = CreateXmlNode(doc, "f", faces);

        WriteAttr(f, doc, "x", std::to_string(mesh->m_clientSideIndices[i * 3]));
        WriteAttr(f, doc, "y", std::to_string(mesh->m_clientSideIndices[i * 3 + 1]));
        WriteAttr(f, doc, "z", std::to_string(mesh->m_clientSideIndices[i * 3 + 2]));
      }
    }
  };

  template <typename T>
  void LoadMesh(XmlDocument* doc, XmlNode* parent, T* mainMesh)
  {
    mainMesh->m_boundingBox = BoundingBox();

    T* mesh                 = mainMesh;
    XmlNode* node           = parent;

    String typeString;
    if constexpr (std::is_same<T, Mesh>())
    {
      typeString = "mesh";
    }
    else
    {
      typeString = "skinMesh";
    }

    for (node = node->first_node(typeString.c_str()); node; node = node->next_sibling(typeString.c_str()))
    {
      if (mesh == nullptr)
      {
        std::shared_ptr<T> meshPtr = MakeNewPtr<T>();
        mesh                       = meshPtr.get();
        mainMesh->m_subMeshes.push_back(meshPtr);
      }

      mesh->m_material = ReadMaterial(node);

      if constexpr (std::is_same<T, SkinMesh>())
      {
        String path = Skeleton::DeserializeRef(node);
        if (path.length() == 0)
        {
          assert(0 && "SkinMesh has no skeleton!");
        }

        NormalizePath(path);
        String skelFile  = SkeletonPath(path);
        mesh->m_skeleton = GetSkeletonManager()->Create<Skeleton>(skelFile);
      }

      XmlNode* vertex = node->first_node("vertices");
      if (XmlAttribute* dataSizeAttr = vertex->first_attribute("VertexCount"))
      {
        // Vertex Buffer stored as binary.
        uint vertexCount = 0;
        ReadAttr(vertex, "VertexCount", vertexCount);
        mesh->m_clientSideVertices.resize(vertexCount);
        XmlNode* b64Node = vertex->first_node("Base64");
        b64tobin(mesh->m_clientSideVertices.data(), b64Node->value());
      }
      else
      {
        // Vertex Buffer stored as text.
        for (XmlNode* v = vertex->first_node("v"); v; v = v->next_sibling())
        {
          SkinVertex vd;
          ReadVec(v->first_node("p"), vd.pos);
          ReadVec(v->first_node("n"), vd.norm);
          ReadVec(v->first_node("t"), vd.tex);
          ReadVec(v->first_node("bt"), vd.tan);

          if constexpr (std::is_same<T, SkinMesh>())
          {
            ReadVec(v->first_node("b"), vd.bones);
            ReadVec(v->first_node("w"), vd.weights);
          }

          mesh->m_clientSideVertices.push_back(vd);
        }
      }

      XmlNode* faces = node->first_node("faces");
      if (XmlAttribute* faceCountAttr = faces->first_attribute("FaceCount"))
      {
        // Binary.
        uint faceCount = 0;
        ReadAttr(faces, "FaceCount", faceCount);
        mesh->m_clientSideIndices.resize(faceCount);
        XmlNode* b64Node = faces->first_node("Base64");
        b64tobin(mesh->m_clientSideIndices.data(), b64Node->value());
      }
      else
      {
        // Text.
        for (XmlNode* i = faces->first_node("f"); i; i = i->next_sibling())
        {
          glm::ivec3 indices;
          ReadVec(i, indices);
          mesh->m_clientSideIndices.push_back(indices.x);
          mesh->m_clientSideIndices.push_back(indices.y);
          mesh->m_clientSideIndices.push_back(indices.z);
        }
      }

      mesh->m_loaded      = true;
      mesh->m_vertexCount = (int) (mesh->m_clientSideVertices.size());
      mesh->m_indexCount  = (int) (mesh->m_clientSideIndices.size());
      mesh                = nullptr;
    }

    mainMesh->CalculateAABB();
  }

  XmlNode* Mesh::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* container = CreateXmlNode(doc, "meshContainer", parent);

    // This approach will flatten the mesh on a single sibling level.
    // To keep the depth hierarchy, recursive save is needed.
    MeshRawPtrArray cMeshes;
    GetAllMeshes(cMeshes, true);

    for (const Mesh* m : cMeshes)
    {
      if (m->IsSkinned())
      {
        writeMesh(doc, container, static_cast<const SkinMesh*>(m));
      }
      else
      {
        writeMesh(doc, container, static_cast<const Mesh*>(m));
      }
    }

    return container;
  }

  XmlNode* Mesh::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    LoadMesh(info.Document, parent, this);
    return nullptr;
  }

  void Mesh::InitVertices(bool flush)
  {
    if (m_gpuData != nullptr)
    {
      Stats::RemoveVRAMUsageInBytes(GetVertexSize() * m_vertexCount);
    }

    IGraphicsBackend* backend = GetRenderSystem()->GetRenderer()->GetBackend();
    backend->CreateMesh(this);

    Stats::AddVRAMUsageInBytes(GetVertexSize() * (uint64) m_vertexCount);

    if (flush)
    {
      ClearClientVertexData();
    }
  }

  void Mesh::InitIndices(bool flush)
  {
    if (m_gpuData != nullptr)
    {
      Stats::RemoveVRAMUsageInBytes(sizeof(uint) * m_indexCount);
    }

    IGraphicsBackend* backend = GetRenderSystem()->GetRenderer()->GetBackend();
    backend->CreateMesh(this);

    Stats::AddVRAMUsageInBytes(sizeof(uint) * (uint64) m_indexCount);

    if (flush)
    {
      m_clientSideIndices.clear();
    }
  }

  // SkinMesh
  //////////////////////////////////////////

  TKDefineClass(SkinMesh, Mesh);

  SkinMesh::SkinMesh() : Mesh() { m_vertexLayout = VertexLayout::SkinMesh; }

  SkinMesh::SkinMesh(const String& file) : SkinMesh()
  {
    SetFile(file);

    String skelFile  = file.substr(0, file.find_last_of("."));
    skelFile        += ".skeleton";

    m_skeleton       = GetSkeletonManager()->Create<Skeleton>(skelFile);
  }

  SkinMesh::~SkinMesh() { UnInit(); }

  void SkinMesh::Init(bool flushClientSideArray)
  {
    if (m_skeleton == nullptr)
    {
      return;
    }
    // Don't flush, otherwise CPU skinning won't work. So neither
    // CalculateBoundary() nor RayMeshIntersection() will work as expected
    m_skeleton->Init(false);
    Mesh::Init(false);
  }

  void SkinMesh::UnInit() { Mesh::UnInit(); }

  void SkinMesh::Load()
  {
    if (m_loaded)
    {
      return;
    }

    // If skeleton is specified, load it
    // While reading from a file, it's probably not loaded
    // So Deserialize will also try to load it
    if (m_skeleton)
    {
      m_skeleton->Load();
      assert(m_skeleton->m_loaded);
      if (!m_skeleton->m_loaded)
      {
        return;
      }
    }

    ParseDocument("meshContainer");
    m_loaded = true;
  }

  XmlNode* SkinMesh::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    LoadMesh(info.Document, parent, this);
    return nullptr;
  }

  BoundingBox SkinMesh::CalculateAABB(const Skeleton* skel, DynamicBoneMapPtr boneMap)
  {
    if (m_bindPoseAABBCalculated)
    {
      return m_bindPoseAABB;
    }

    BoundingBox finalAABB;
    MeshRawPtrArray meshes;
    GetAllMeshes(meshes);

    std::vector<BoundingBox> AABBs(meshes.size());
    UIntArray indexes(meshes.size());
    for (uint i = 0; i < meshes.size(); i++)
    {
      indexes[i] = i;
    }

    std::for_each(indexes.begin(),
                  indexes.end(),
                  [skel, boneMap, &AABBs, &meshes](uint index)
                  {
                    SkinMesh* m = (SkinMesh*) meshes[index];
                    if (m->m_clientSideVertices.empty())
                    {
                      return;
                    }

                    BoundingBox& meshAABB = AABBs[index];
                    Spinlock aabbWriteLock;

                    std::for_each(TKExecBy(WorkerManager::FramePool),
                                  m->m_clientSideVertices.begin(),
                                  m->m_clientSideVertices.end(),
                                  [skel, boneMap, &aabbWriteLock, &meshAABB](SkinVertex& v)
                                  {
                                    Vec3 skinnedPos = CPUSkinning(&v, skel, boneMap, false);
                                    SpinlockGuard lock(aabbWriteLock);
                                    meshAABB.UpdateBoundary(skinnedPos);
                                  });
                  });

    for (BoundingBox& aabb : AABBs)
    {
      finalAABB.UpdateBoundary(aabb.max);
      finalAABB.UpdateBoundary(aabb.min);
    }

    m_bindPoseAABBCalculated = true;
    m_bindPoseAABB           = finalAABB;

    return finalAABB;
  }

  void SkinMesh::CalculateAABB()
  {
    // Construct aabb of all submeshes.
    MeshRawPtrArray meshes;
    GetAllMeshes(meshes, true);

    BoundingBox aabb;
    for (Mesh* mesh : meshes)
    {
      SkinMesh* smesh = static_cast<SkinMesh*>(mesh);
      for (size_t i = 0; i < smesh->m_clientSideVertices.size(); i++)
      {
        SkinVertex& v = smesh->m_clientSideVertices[i];
        aabb.UpdateBoundary(v.pos);
      }
    }
    m_boundingBox = aabb;
  }

  uint SkinMesh::TotalVertexCount() const
  {
    uint total = 0;
    for (Mesh* mesh : m_allMeshes)
    {
      total += mesh->m_vertexCount;
    }

    total += (uint) m_clientSideVertices.size();

    return total;
  }

  uint SkinMesh::GetVertexCount() const { return (uint) m_clientSideVertices.size(); }

  int SkinMesh::GetVertexSize() const { return sizeof(SkinVertex); }

  const void* SkinMesh::GetClientVertexData() const { return m_clientSideVertices.empty() ? nullptr : m_clientSideVertices.data(); }

  size_t SkinMesh::GetClientVertexCount() const { return m_clientSideVertices.size(); }

  void SkinMesh::ClearClientVertexData() { m_clientSideVertices.clear(); }

  bool SkinMesh::IsSkinned() const { return true; }

  void SkinMesh::InitVertices(bool flush)
  {
    Mesh::InitVertices(flush);
  }

  void SkinMesh::CopyTo(Resource* other)
  {
    Super::CopyTo(other);
    SkinMesh* cpy   = static_cast<SkinMesh*>(other);
    cpy->m_skeleton = GetSkeletonManager()->Copy<Skeleton>(m_skeleton);
    cpy->m_skeleton->Init();
  }

  MeshManager::MeshManager() { m_baseType = Mesh::StaticClass(); }

  MeshManager::~MeshManager() {}

  bool MeshManager::CanStore(ClassMeta* Class) { return Class->IsSublcassOf(Mesh::StaticClass()); }

  String MeshManager::GetDefaultResource(ClassMeta* Class) { return MeshPath("Suzanne.mesh", true); }

} // namespace ToolKit

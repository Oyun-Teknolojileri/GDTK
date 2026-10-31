/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Shader.h"

#include "EngineSettings.h"
#include "FileManager.h"
#include "GpuProgram.h"
#include "Logger.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "TKAssert.h"
#include "ToolKit.h"
#include "Util.h"

#include <unordered_set>

#include "DebugNew.h"

namespace ToolKit
{

  // Duplicate Include Prune Utility
  //////////////////////////////////////////

  namespace
  {
    /** The characters a regular expression counted as \s, so the manual scans see the same lines. */
    bool IsShaderSpace(char character)
    {
      return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
             character == '\f' || character == '\v';
    }

    /**
     * Reads a "// @include begin:<name>" or "// @include end:<name>" marker line.
     *
     * The engine writes these markers itself, so their shape is fixed: optional whitespace, "//",
     * optional whitespace, "@include", at least one whitespace, then "begin:" or "end:" followed by
     * the name, which runs to the end of the line.
     *
     * A regular expression used to do this for every line of every shader. It was the largest
     * single cost of a device start, because a std::regex is compiled once and then matched per
     * line, so the fixed shape is read by hand instead.
     *
     * @param line The line without its terminating newline.
     * @param isEnd Set to true when the marker closes a block.
     * @return The include name, empty when the line carries no marker.
     */
    StringView ParseIncludeMarker(StringView line, bool& isEnd)
    {
      isEnd = false;

      size_t pos = 0;
      while (pos < line.size() && IsShaderSpace(line[pos]))
      {
        pos++;
      }

      if (pos + 1 >= line.size() || line[pos] != '/' || line[pos + 1] != '/')
      {
        return StringView();
      }
      pos += 2;

      while (pos < line.size() && IsShaderSpace(line[pos]))
      {
        pos++;
      }

      if (line.compare(pos, 8, "@include") != 0)
      {
        return StringView();
      }
      pos += 8;

      // At least one whitespace separates "@include" from the name.
      if (pos >= line.size() || !IsShaderSpace(line[pos]))
      {
        return StringView();
      }
      while (pos < line.size() && IsShaderSpace(line[pos]))
      {
        pos++;
      }

      if (line.compare(pos, 6, "begin:") == 0)
      {
        pos += 6;
      }
      else if (line.compare(pos, 4, "end:") == 0)
      {
        pos   += 4;
        isEnd  = true;
      }
      else
      {
        return StringView();
      }

      const size_t nameBegin = pos;
      while (pos < line.size() && !IsShaderSpace(line[pos]))
      {
        pos++;
      }

      // The name has to run to the end of the line: anything after it means this is not a marker.
      if (pos == nameBegin || pos != line.size())
      {
        return StringView();
      }

      return line.substr(nameBegin);
    }

    /**
     * Removes a "#version" or "precision" statement from the top of an included source.
     *
     * The including shader writes both, so an included file must not carry its own. This is the
     * same single statement the regular expression replaced (it was anchored at the start of the
     * text), done by hand because the expression was built per include.
     */
    void StripLeadingVersionDirective(String& source)
    {
      size_t pos = 0;
      while (pos < source.size() && IsShaderSpace(source[pos]))
      {
        pos++;
      }

      const bool version   = source.compare(pos, 8, "#version") == 0;
      const bool precision = source.compare(pos, 9, "precision") == 0;

      if (!version && !precision)
      {
        return;
      }

      // The statement runs to the end of its line. The newline stays, so the lines around it do not
      // shift in a compiler error.
      const size_t lineEnd = source.find('\n', pos);
      source.erase(0, lineEnd == String::npos ? source.size() : lineEnd);
    }
  } // namespace

  void PruneDuplicateIncludes(String& source)
  {
    // Keep track of which include names we've already output
    std::unordered_set<String> seen;
    // Stack of (include name, skip flag) for nested scopes
    std::vector<std::pair<String, bool>> includeStack;

    String pruned;
    pruned.reserve(source.size());

    const StringView text(source);
    size_t lineBegin = 0;

    while (lineBegin < text.size())
    {
      size_t lineEnd        = text.find('\n', lineBegin);
      const bool terminated = lineEnd != StringView::npos;
      if (!terminated)
      {
        lineEnd = text.size();
      }

      const StringView line = text.substr(lineBegin, lineEnd - lineBegin);
      lineBegin             = terminated ? lineEnd + 1 : text.size();

      bool isEnd;
      const StringView name = ParseIncludeMarker(line, isEnd);

      if (!name.empty())
      {
        if (!isEnd)
        {
          // Found a begin scope.
          const String includeName(name);
          const bool skip = seen.count(includeName) > 0;
          includeStack.emplace_back(includeName, skip);

          if (!skip)
          {
            seen.insert(includeName);
            pruned.append(line.data(), line.size());
            pruned += '\n';
          }
        }
        else if (!includeStack.empty())
        {
          // Found an end scope: close the innermost one, and keep its line when it was kept.
          const bool skip = includeStack.back().second;
          includeStack.pop_back();

          if (!skip)
          {
            pruned.append(line.data(), line.size());
            pruned += '\n';
          }
        }

        continue;
      }

      // Regular line: output only if not inside a skipped block.
      if (includeStack.empty() || !includeStack.back().second)
      {
        pruned.append(line.data(), line.size());
        pruned += '\n';
      }
    }

    // Replace the source with the pruned result.
    source = std::move(pruned);
  }

  // Shader
  //////////////////////////////////////////

#define TK_DEFAULT_FORWARD_FRAG  "defaultFragment.shader"
#define TK_DEFAULT_VERTEX_SHADER "defaultVertex.shader"

  TKDefineClass(Shader, Resource);

  Shader::Shader() {}

  Shader::Shader(const String& file) : Shader() { SetFile(file); }

  Shader::~Shader() { UnInit(); }

  void Shader::Load()
  {
    if (!m_loaded)
    {
      ParseDocument("shader", true);
      m_loaded = true;
    }
  }

  void Shader::Init(bool flushClientSideArray)
  {
    if (m_initiated)
    {
      return;
    }

    if (!m_defineArray.empty())
    {
      // Try syncing presets.
      GetEngineSettings().m_shaderSettings->SyncDefinesForShader(GetFile(), m_defineArray);

      // Compiling the whole combination set upfront walks every define against every other one -- the
      // forward shader alone is over a hundred variants -- while a frame uses a handful. Instead the
      // selection starts on the first variant of every define, which is what SetDefine edits from
      // then on, and CreateProgram asks for the pair it needs: the program cache answers for a warm
      // start, and EnsureVariantCompiled compiles exactly one variant on a miss.
      //
      // The selection has to exist here, before anything is compiled: SetDefine and the program cache
      // key both read it, and a program restored from a cache never reaches a compile that could have
      // initialized it. Leaving it empty was what made every SetDefine fail on a warm start.
      m_currentDefineValues.clear();
      for (int i = 0; i < (int) m_defineArray.size(); i++)
      {
        m_currentDefineValues.push_back({i, 0});
      }
      m_currentVariantKey = VariantKey();
    }
    else
    {
      Compile(m_source);
    }

    // A shader with defines keeps its text: a variant compiled later needs it, and dropping it here
    // is what made an on-demand compile produce nothing.
    if (flushClientSideArray && m_defineArray.empty())
    {
      m_source.clear();
    }

    m_initiated = true;
  }

  void Shader::UnInit()
  {
    // A shader can outlive the engine when an application static or global keeps a reference
    // to it. In that case the backend is already gone and there is nothing left to destroy.
    IGraphicsBackend* backend = GetBackend_noexcep();

    if (backend != nullptr)
    {
      // Destroy all compiled variants.
      bool handleInMap = false;
      for (auto& [key, data] : m_shaderVariantMap)
      {
        if (data.get() == m_gpuData.get())
        {
          handleInMap = true;
        }
        backend->DestroyShader(data.get());
      }

      // If m_gpuData was not part of a variant (base compile without defines), destroy it too.
      if (!handleInMap)
      {
        backend->DestroyShader(m_gpuData.get());
      }
    }

    m_shaderVariantMap.clear();
    m_gpuData.reset();
    m_initiated = false;
  }

  void Shader::Save(bool onlyIfDirty)
  {
    // Shaders are created from code. So don't get saved.
    m_dirty = false;
  }

  String Shader::VariantKey() const
  {
    // The same "Define:Value|Define:Value" form the variant map is keyed by. Built from the current
    // selection rather than cached: the passes flip a define per frame and ask for the program right
    // after, so the key has to follow the selection exactly.
    String key;
    for (const ShaderDefineIndex& selection : m_currentDefineValues)
    {
      if (selection.define < 0 || selection.define >= (int) m_defineArray.size())
      {
        continue;
      }

      const ShaderDefine& definition = m_defineArray[selection.define];
      if (selection.variant < 0 || selection.variant >= (int) definition.variants.size())
      {
        continue;
      }

      key += definition.define + ":" + definition.variants[selection.variant] + "|";
    }

    if (!key.empty())
    {
      key.pop_back();
    }

    return key;
  }

  void Shader::SetDefine(const StringView name, const StringView val)
  {
    // Sanity checks.
    if (!m_initiated)
    {
      // TK_ERR("Initialize the shader before setting a value for a define.");
      TK_ERR("Initialize the shader before setting a value for a define. (%s)", GetFile().c_str());

      return;
    }

    // Look the define up in the shader's own list, not in the current selection: a selection entry
    // that was never initialized made every SetDefine of a cache restored shader fail, and a failed
    // SetDefine silently keeps the previous variant, which is how instanced draws ended up with the
    // per object program.
    int defineIndx = -1;
    for (int i = 0; i < (int) m_defineArray.size(); i++)
    {
      if (m_defineArray[i].define == name)
      {
        defineIndx = i;
        break;
      }
    }

    if (defineIndx < 0)
    {
      // In this case, shader must be updated, because the define is not present in the shader.
      TK_ERR("Define: %s not found in shader: %s", name.data(), GetFile().data());
      return;
    }

    int variantIndx = -1;
    for (int i = 0; i < (int) m_defineArray[defineIndx].variants.size(); i++)
    {
      if (m_defineArray[defineIndx].variants[i] == val)
      {
        variantIndx = i;
        break;
      }
    }

    if (variantIndx < 0)
    {
      // Push a new define variant.
      m_defineArray[defineIndx].variants.push_back(val.data());
      variantIndx = (int) m_defineArray[defineIndx].variants.size() - 1;
    }

    // Init sized the selection with the define list. A shader whose list grew since then (an include
    // that declares more defines) gets the missing entries here, each on its first variant.
    if (m_currentDefineValues.size() < m_defineArray.size())
    {
      const size_t known = m_currentDefineValues.size();
      m_currentDefineValues.resize(m_defineArray.size());
      for (size_t i = known; i < m_defineArray.size(); i++)
      {
        m_currentDefineValues[i].define  = (int) i;
        m_currentDefineValues[i].variant = 0;
      }
    }

    m_currentDefineValues[defineIndx].define  = defineIndx;
    m_currentDefineValues[defineIndx].variant = variantIndx;

    m_currentVariantKey = VariantKey();

    // Point at the variant when it is already compiled. When it is not, the program that uses it is
    // what brings it in: it restores the binary from the program cache, or EnsureVariantCompiled
    // compiles this one variant.
    const auto handle = m_shaderVariantMap.find(m_currentVariantKey);
    if (handle != m_shaderVariantMap.end())
    {
      m_gpuData = handle->second;
    }
  }

  XmlNode* Shader::SerializeImp(XmlDocument* doc, XmlNode* parent) const { return nullptr; }

  XmlNode* Shader::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    m_includeFiles.clear();
    m_resources.clear();

    XmlNode* rootNode = parent;
    for (XmlNode* node = rootNode->first_node(); node; node = node->next_sibling())
    {
      if (strcmp("type", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute("name");
        if (strcmp("vertexShader", attr->value()) == 0)
        {
          m_shaderType = ShaderType::VertexShader;
        }
        else if (strcmp("fragmentShader", attr->value()) == 0)
        {
          m_shaderType = ShaderType::FragmentShader;
        }
        else if (strcmp("includeShader", attr->value()) == 0)
        {
          m_shaderType = ShaderType::IncludeShader;
        }
        else
        {
          TK_ERR("Unrecognized shader type: %s Shader: %s", attr->value(), info.File.c_str());
        }
      }

      if (strcmp("include", node->name()) == 0)
      {
        m_includeFiles.push_back(node->first_attribute("name")->value());
      }

      if (strcmp("define", node->name()) == 0)
      {
        ShaderDefine def;
        def.define = node->first_attribute("name")->value();

        String val = node->first_attribute("val")->value();
        Split(val, ",", def.variants);

        m_defineArray.push_back(def);
      }

      if (strcmp("texture", node->name()) == 0)
      {
        ShaderResource res;
        res.type = ShaderResource::Type::Texture;

        if (XmlAttribute* slotAttr = node->first_attribute("slot"))
        {
          res.slot = std::atoi(slotAttr->value());
        }
        if (XmlAttribute* nameAttr = node->first_attribute("name"))
        {
          res.name = nameAttr->value();
        }
        if (XmlAttribute* vtAttr = node->first_attribute("viewType"))
        {
          StringView vt = vtAttr->value();
          if (vt == "2darray")
            res.viewType = ShaderResource::ViewType::Tex2DArray;
          else if (vt == "cube")
            res.viewType = ShaderResource::ViewType::TexCube;
        }

        m_resources.push_back(res);
      }

      if (strcmp("uniform", node->name()) == 0)
      {
        ShaderResource res;
        res.type = ShaderResource::Type::UniformBuffer;

        if (XmlAttribute* slotAttr = node->first_attribute("slot"))
        {
          res.slot = std::atoi(slotAttr->value());
        }
        if (XmlAttribute* nameAttr = node->first_attribute("name"))
        {
          res.name = nameAttr->value();
        }

        m_resources.push_back(res);
      }

      if (strcmp("source", node->name()) == 0)
      {
        m_source = node->first_node()->value();
      }
    }

    // Iterate back to forth
    for (auto i = m_includeFiles.rbegin(); i != m_includeFiles.rend(); ++i)
    {
      HandleShaderIncludes(*i);
    }

    if (m_shaderType != ShaderType::IncludeShader)
    {
      PruneDuplicateIncludes(m_source);

      // Inject the version directive. .shader sources no longer carry their own #version line -
      // one source must compile on both backends, so the right line is picked here based on the
      // active backend. Goes to the very top because GLSL requires #version as the first
      // statement (anything before it is a compile error). HandleShaderIncludes already strips
      // any stray #version/precision from included files, so prepending here can't double up.
      // Note: VulkanShader.cpp additionally forces shaderc to 450 core via SetForcedVersionProfile,
      // so even older shaders with a leftover #version would still compile on the VK path; we
      // still emit the matching line here for consistency and so SPIR-V disasm shows the right
      // header.
      // A sampler defaults to lowp in GLSL ES, and a lowp fetch does not return what the texture holds: it
      // comes back rounded to about eleven bits, which is half float on this driver. That is not enough for
      // the data this engine samples - an instanced model matrix read out of the instance table lands the
      // object a fraction of a unit from where its transform says it is, and the occlusion grid's depths
      // come back quantized to 1/2048 of the window range, which is metres wide near the far plane. The
      // default is raised here rather than in each shader because HandleShaderIncludes strips precision
      // statements out of included files, so the one place that sees every shader whole is this one.
      // Written after the version line and before any sampler declaration, which is where a default has to
      // be. Desktop GLSL takes no precision statements, so the Vulkan path keeps its own line.
#ifdef TK_VULKAN
      m_source.insert(0, "#version 450 core\n");
#else
      m_source.insert(0, "#version 300 es\nprecision highp sampler2D;\nprecision highp sampler2DArray;\n"
                         "precision highp samplerCube;\n");
#endif
    }

    return nullptr;
  }

  void Shader::HandleShaderIncludes(const String& file)
  {
    size_t mergeLoc    = FindShaderMergeLocation(m_source);

    // Mark the file beginning of include file.
    String includeMark = "// @include begin:" + file + "\n";
    m_source.replace(mergeLoc, 0, includeMark);
    mergeLoc                += includeMark.length();

    // Perform the include.
    ShaderPtr includeShader  = GetShaderManager()->Create<Shader>(ShaderPath(file, true));

    // CLEANUP: We must strip #version and precision from the included file
    // to prevent multiple definitions in the final output.
    String includeContent    = includeShader->m_source;
    StripLeadingVersionDirective(includeContent);

    // Ensure the included content ends with a newline to avoid "gluing" lines together
    if (!includeContent.empty() && includeContent.back() != '\n')
    {
      includeContent += "\n";
    }

    m_source.replace(mergeLoc, 0, includeContent);

    // Mark the end of include file.
    mergeLoc    += includeContent.length();
    includeMark  = "// @include end:" + file + "\n";
    m_source.replace(mergeLoc, 0, includeMark);

    // Handle m_defineArray
    m_defineArray.insert(m_defineArray.end(), includeShader->m_defineArray.begin(), includeShader->m_defineArray.end());
    std::sort(m_defineArray.begin(), m_defineArray.end());
    m_defineArray.erase(std::unique(m_defineArray.begin(), m_defineArray.end()), m_defineArray.end());

    // Inherit resource declarations from the included shader, deduplicated by (type, slot, name).
    for (const ShaderResource& incRes : includeShader->m_resources)
    {
      auto same = [&incRes](const ShaderResource& r)
      { return r.type == incRes.type && r.slot == incRes.slot && r.name == incRes.name; };

      if (std::find_if(m_resources.begin(), m_resources.end(), same) == m_resources.end())
      {
        m_resources.push_back(incRes);
      }
    }
  }

  uint Shader::FindShaderMergeLocation(const String& source)
  {
    // Put included file after precision and version defines
    size_t includeLoc = 0;

    // Find the end of #version line
    size_t versionLoc = source.find("#version");
    if (versionLoc != String::npos)
    {
      size_t lineEnd = source.find('\n', versionLoc);
      if (lineEnd != String::npos)
      {
        includeLoc = std::max(includeLoc, lineEnd + 1);
      }
    }

    // Find the end of the last precision line. Only a statement counts: the word may appear in a
    // comment or in an identifier further down the shader, and matching that would splice the
    // includes and the variant defines into the middle of the file, which then fails to compile.
    size_t precisionLoc = 0;
    while ((precisionLoc = source.find("precision", precisionLoc)) != String::npos)
    {
      const size_t lineStart = source.rfind('\n', precisionLoc);
      const size_t firstChar = source.find_first_not_of(" \t", lineStart == String::npos ? 0 : lineStart + 1);

      if (firstChar == precisionLoc)
      {
        size_t statementEnd = source.find(';', precisionLoc);
        if (statementEnd != String::npos)
        {
          // Find the actual line end to avoid inserting in the middle of a line
          size_t lineEnd = source.find('\n', statementEnd);
          if (lineEnd != String::npos)
          {
            includeLoc = std::max(includeLoc, lineEnd + 1);
          }
          else
          {
            includeLoc = std::max(includeLoc, statementEnd + 1);
          }
        }
      }

      precisionLoc += 9; // Move past current "precision"
    }

    return (uint) includeLoc;
  }

  bool Shader::Compile(String source, const String& variantKey)
  {
    // One line per compile, with the variant: the lazy path compiles exactly the combinations the
    // programs ask for, so this is the record of what a build needs. A variant that fails to compile
    // reports through the backend's own error log.
    const String relPath = GetRelativeResourcePath(GetFile());
    if (variantKey.empty())
    {
      TK_LOG("Shader in compile %s", relPath.c_str());
    }
    else
    {
      TK_LOG("Shader in compile %s [%s]", relPath.c_str(), variantKey.c_str());
    }

    GpuResourceDataPtr data = GetRenderSystem()->GetBackend()->CreateShader(this, source);

    // Keep the last working variant on failure: a null here reaches the program creation, which
    // dereferences it, so a shader that failed to compile would take the renderer down instead of
    // reporting the compile error the backend already logged.
    if (data == nullptr)
    {
      return false;
    }

    m_gpuData = data;
    return true;
  }

  bool Shader::CompileVariantKey(const String& variantKey)
  {
    if (variantKey.empty())
    {
      return false;
    }

    // Already compiled: point the current data at it and report it as ready. Returning false here
    // used to leave m_gpuData on whichever variant was compiled last, so a program asking for a
    // variant twice was built from the wrong stage.
    const auto existing = m_shaderVariantMap.find(variantKey);
    if (existing != m_shaderVariantMap.end())
    {
      m_gpuData           = existing->second;
      m_currentVariantKey = variantKey;
      return true;
    }

    // The key is the define list the variant is compiled with; rebuild the #define lines from it so
    // exactly that variant comes back, without walking the combination space.
    String defineText;
    StringArray parts;
    Split(variantKey, "|", parts);
    for (const String& part : parts)
    {
      const size_t separator = part.find(':');
      if (separator == String::npos)
      {
        continue;
      }
      defineText += "#define " + part.substr(0, separator) + " " + part.substr(separator + 1) + "\n";
    }

    String source      = m_source;
    const uint mergeAt = FindShaderMergeLocation(source);
    source.insert(mergeAt, defineText);

    if (Compile(source, variantKey))
    {
      m_currentVariantKey            = variantKey;
      m_shaderVariantMap[variantKey] = m_gpuData;
      return true;
    }
    return false;
  }

  void Shader::EnsureVariantCompiled()
  {
    // The variant the shader is currently selecting, i.e. what a program built right now would use.
    // Init seeds that selection, so this is never empty for a shader that declares defines.
    const String key = VariantKey();

    if (!key.empty() && CompileVariantKey(key))
    {
      return;
    }

    if (m_gpuData != nullptr)
    {
      return; // Something usable is already compiled.
    }

    if (m_defineArray.empty())
    {
      Compile(m_source);
    }
  }

  // ShaderManager
  //////////////////////////////////////////

  ShaderManager::ShaderManager() { m_baseType = Shader::StaticClass(); }

  ShaderManager::~ShaderManager() {}

  void ShaderManager::Init()
  {
    ResourceManager::Init();

    m_pbrForwardShaderFile    = ShaderPath(TK_DEFAULT_FORWARD_FRAG, true);
    m_defaultVertexShaderFile = ShaderPath(TK_DEFAULT_VERTEX_SHADER, true);

    Create<Shader>(m_pbrForwardShaderFile);
    Create<Shader>(m_defaultVertexShaderFile);
  }

  bool ShaderManager::CanStore(ClassMeta* Class) { return Class == Shader::StaticClass(); }

  ShaderPtr ShaderManager::GetDefaultVertexShader() { return Cast<Shader>(m_storage[m_defaultVertexShaderFile]); }

  ShaderPtr ShaderManager::GetPbrForwardShader() { return Cast<Shader>(m_storage[m_pbrForwardShaderFile]); }

  const String& ShaderManager::PbrForwardShaderFile() { return m_pbrForwardShaderFile; }

} // namespace ToolKit

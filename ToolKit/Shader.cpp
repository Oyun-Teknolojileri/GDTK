/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Shader.h"

#include "FileManager.h"
#include "GpuProgram.h"
#include "Logger.h"
#include "TKAssert.h"
#include "TKOpenGL.h"
#include "ToolKit.h"
#include "Util.h"

#include <regex>
#include <sstream>
#include <unordered_set>

#include "DebugNew.h"

namespace ToolKit
{

  // Duplicate Include Prune Utility
  //////////////////////////////////////////

  void PruneDuplicateIncludes(String& source)
  {
    std::istringstream in(source);
    std::ostringstream out;

    // Regexes to detect include begin/end with the include name in group 1
    const std::regex beginRegex(R"(^\s*//\s*@include\s+begin:(\S+))");
    const std::regex endRegex(R"(^\s*//\s*@include\s+end:(\S+))");

    std::string line;
    // Keep track of which include names we've already output
    std::unordered_set<String> seen;
    // Stack of (include name, skip flag) for nested scopes
    std::vector<std::pair<String, bool>> includeStack;

    while (std::getline(in, line))
    {
      std::smatch match;
      if (std::regex_match(line, match, beginRegex))
      {
        // Found a begin scope
        String name = match[1].str();
        bool skip   = seen.count(name) > 0;
        includeStack.emplace_back(name, skip);

        if (!skip)
        {
          seen.insert(name);
          out << line << "\n";
        }
      }
      else if (std::regex_match(line, match, endRegex))
      {
        // Found an end scope
        String name = match[1].str();
        if (!includeStack.empty())
        {
          auto [topName, skip] = includeStack.back();
          includeStack.pop_back();
          // Only output the end marker if we weren't skipping this block
          if (!skip)
          {
            out << line << "\n";
          }
        }
      }
      else
      {
        // Regular line: output only if not inside a skipped block
        if (includeStack.empty() || !includeStack.back().second)
        {
          out << line << "\n";
        }
      }
    }

    // Replace the source with the pruned result
    source = out.str();
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
      ShaderDefineCombinaton defineCombo;
      ComplieShaderCombinations(m_defineArray, 0, defineCombo);
    }
    else
    {
      Compile(m_source);
    }

    if (flushClientSideArray)
    {
      m_source.clear();
    }

    m_initiated = true;
  }

  void Shader::UnInit()
  {
    glDeleteShader(m_shaderHandle);
    m_initiated = false;
  }

  void Shader::Save(bool onlyIfDirty)
  {
    // Shaders are created from code. So don't get saved.
    m_dirty = false;
  }

  void Shader::SetDefine(const StringView name, const StringView val)
  {
    // Sanity checks.
    if (!m_initiated)
    {
      TK_ERR("Initialize the shader before setting a value for a define.");
      return;
    }

    // Construct the key.
    String key;
    for (int i = 0; i < (int) m_currentDefineValues.size(); i++)
    {
      int defineIndx  = m_currentDefineValues[i].define;
      int variantIndx = m_currentDefineValues[i].variant;

      // If define found
      if (m_defineArray[defineIndx].define == name)
      {
        // find the variant.
        variantIndx = -1;
        for (int ii = 0; ii < (int) m_defineArray[defineIndx].variants.size(); ii++)
        {
          if (m_defineArray[defineIndx].variants[ii] == val)
          {
            variantIndx                      = ii;
            m_currentDefineValues[i].variant = ii; // update current define variant.
            break;
          }
        }

        if (variantIndx == -1)
        {
          TK_WRN("Shader define can't be set. There is no variant: %s for define: %s", name.data(), val.data());
          return;
        }
      }

      const String& defName  = m_defineArray[defineIndx].define;
      const String& defVal   = m_defineArray[defineIndx].variants[variantIndx];
      key                   += defName + ":" + defVal + "|";
    }

    key.pop_back();

    // Set the shader variant.
    auto handle = m_shaderVariantMap.find(key);
    if (handle != m_shaderVariantMap.end())
    {
      m_shaderHandle = m_shaderVariantMap[key];
    }
    else
    {
      TK_ERR("Unknown shader combination %s", key.c_str());
    }
  }

  XmlNode* Shader::SerializeImp(XmlDocument* doc, XmlNode* parent) const { return nullptr; }

  XmlNode* Shader::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    m_includeFiles.clear();

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

      if (strcmp("uniform", node->name()) == 0)
      {
        XmlAttribute* nameAttr = node->first_attribute("name");
        XmlAttribute* sizeAttr = node->first_attribute("size");

        bool isUniformFound    = false;
        for (uint i = 0; i < (uint) Uniform::UNIFORM_MAX_INVALID; i++)
        {
          // Find uniform from name
          if (strcmp(GetUniformName((Uniform) i), nameAttr->value()) == 0)
          {
            isUniformFound = true;

            if (sizeAttr != nullptr)
            {
              // Uniform is array
              int size = std::atoi(sizeAttr->value());
              m_arrayUniforms.push_back({(Uniform) i, size});
            }
            else
            {
              m_uniforms.push_back((Uniform) i);
            }

            break;
          }
        }

        if (!isUniformFound)
        {
          TK_ERR("Unrecognized uniform: %s", nameAttr->value());
        }
      }

      if (strcmp("define", node->name()) == 0)
      {
        ShaderDefine def;
        def.define = node->first_attribute("name")->value();

        String val = node->first_attribute("val")->value();
        Split(val, ",", def.variants);

        m_defineArray.push_back(def);
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
    }

    return nullptr;
  }

  void Shader::HandleShaderIncludes(const String& file)
  {
    // Mark the file beginning of include file.
    String includeMark = "// @include begin:" + file + "\n";
    size_t mergeLoc    = FindShaderMergeLocation(m_source);
    m_source.replace(mergeLoc, 0, includeMark);
    mergeLoc                += includeMark.length();

    // Perform the include.
    ShaderPtr includeShader  = GetShaderManager()->Create<Shader>(ShaderPath(file, true));
    m_source.replace(mergeLoc, 0, includeShader->m_source);

    // Mark the end of include file.
    mergeLoc    += includeShader->m_source.length();
    includeMark  = "// @include end:" + file + "\n";
    m_source.replace(mergeLoc, 0, includeMark);

    // Handle m_defineArray
    m_defineArray.insert(m_defineArray.end(), includeShader->m_defineArray.begin(), includeShader->m_defineArray.end());
    std::sort(m_defineArray.begin(), m_defineArray.end());
    m_defineArray.erase(std::unique(m_defineArray.begin(), m_defineArray.end()), m_defineArray.end());

    // Handle m_uniforms
    m_uniforms.insert(m_uniforms.end(), includeShader->m_uniforms.begin(), includeShader->m_uniforms.end());
    std::sort(m_uniforms.begin(), m_uniforms.end());
    m_uniforms.erase(std::unique(m_uniforms.begin(), m_uniforms.end()), m_uniforms.end());

    // Handle m_arrayUniforms
    m_arrayUniforms.insert(m_arrayUniforms.end(),
                           includeShader->m_arrayUniforms.begin(),
                           includeShader->m_arrayUniforms.end());
    std::sort(m_arrayUniforms.begin(), m_arrayUniforms.end());
    m_arrayUniforms.erase(std::unique(m_arrayUniforms.begin(), m_arrayUniforms.end()), m_arrayUniforms.end());
  }

  uint Shader::FindShaderMergeLocation(const String& file)
  {
    // Put included file after precision and version defines
    size_t includeLoc = 0;
    size_t versionLoc = m_source.find("#version");
    for (size_t fileLoc = versionLoc; fileLoc < m_source.length(); fileLoc++)
    {
      if (m_source[fileLoc] == '\n')
      {
        includeLoc = std::max(includeLoc, fileLoc + 1);
        break;
      }
    }

    size_t precisionLoc = 0;
    while ((precisionLoc = m_source.find("precision", precisionLoc)) != String::npos)
    {
      for (size_t fileLoc = precisionLoc; fileLoc < m_source.length(); fileLoc++)
      {
        if (m_source[fileLoc] == ';')
        {
          includeLoc = std::max(includeLoc, fileLoc + 3);
          break;
        }
      }

      precisionLoc += 9;
    }

    return (uint) includeLoc;
  }

  uint Shader::Compile(String source)
  {
    TK_LOG("Shader in compile %s", GetFile().c_str());

    GLenum type = 0;
    if (m_shaderType == ShaderType::VertexShader)
    {
      type = (GLenum) GraphicTypes::VertexShader;
    }
    else if (m_shaderType == ShaderType::FragmentShader)
    {
      type = (GLenum) GraphicTypes::FragmentShader;
    }
    else
    {
      TK_ERR("Include shader can't be compiled: %s", GetFile().c_str());
      return 0;
    }

    m_shaderHandle = glCreateShader(type);
    if (m_shaderHandle == 0)
    {
      return 0;
    }

    // Start with #version
    const char* str = nullptr;
    size_t loc      = source.find("#version");
    if (loc != String::npos)
    {
      source = source.substr(loc);
      str    = source.c_str();
    }
    else
    {
      str = source.c_str();
    }

    glShaderSource(m_shaderHandle, 1, &str, nullptr);
    glCompileShader(m_shaderHandle);

    GLint compiled;
    glGetShaderiv(m_shaderHandle, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
      GLint infoLen = 0;
      glGetShaderiv(m_shaderHandle, GL_INFO_LOG_LENGTH, &infoLen);
      if (infoLen > 1)
      {
        char* log = new char[infoLen];
        glGetShaderInfoLog(m_shaderHandle, infoLen, nullptr, log);

        TK_ERR(log);
        SafeDelArray(log);
      }

      glDeleteShader(m_shaderHandle);
      return 0;
    }

    return m_shaderHandle;
  }

  void Shader::CompileWithDefines(String source, const ShaderDefineCombinaton& defineCombo)
  {
    String key; // Hash key for the shader variant.
    String defineText;
    for (const ShaderDefineIndex& def : defineCombo)
    {
      String defName  = m_defineArray[def.define].define;
      String defVal   = m_defineArray[def.define].variants[def.variant];
      key            += defName + ":" + defVal + "|";

      defineText     += "#define " + defName + " " + defVal + "\n";
    }

    // Insert defines.
    uint mergeLoc = FindShaderMergeLocation(source);
    source.insert(mergeLoc, defineText);

    key.pop_back(); // remove last "|"

    TK_LOG("Compiling shader with defines: %s", key.c_str());

    if (Compile(source) != 0)
    {
      m_currentDefineValues   = defineCombo;
      m_shaderVariantMap[key] = m_shaderHandle;
    }
  }

  void Shader::ComplieShaderCombinations(const ShaderDefineArray& defineArray,
                                         int index,
                                         ShaderDefineCombinaton& currentCombinaiton)
  {
    if (index == defineArray.size())
    {
      // Compile the final combo.
      CompileWithDefines(m_source, currentCombinaiton);
      return;
    }

    const ShaderDefine& currentDefine = defineArray[index];
    for (int vi = (int) currentDefine.variants.size() - 1; vi >= 0; vi--)
    {
      currentCombinaiton.push_back({index, vi});

      // Recursively generate combinations
      ComplieShaderCombinations(defineArray, index + 1, currentCombinaiton);
      currentCombinaiton.pop_back();
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

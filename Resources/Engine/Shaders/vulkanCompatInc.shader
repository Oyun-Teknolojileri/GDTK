<shader>
  <type name = "includeShader" />
  <source>
  <!--
#ifndef TK_VULKAN_COMPAT
#define TK_VULKAN_COMPAT

// Vulkan/GL single-source compatibility macros.
//
// GL ES 3.00 doesn't support layout(binding = N) on UBO blocks or samplers; in the GL path
// bindings are wired up at runtime via glUniformBlockBinding / glUniform1i. Vulkan/SPIR-V
// requires explicit binding qualifiers in the source.
//
// To keep one .shader file per program and zero runtime regex on Vulkan's side, we wrap
// every UBO block declaration and every sampler declaration with these macros. The macro
// expansion is conditioned on the VULKAN macro that VulkanShader::CompileGlslToSpirv sets
// via shaderc options.AddMacroDefinition("VULKAN", "1").
//
//   GL   path: VULKAN undefined  -> macro emits the ES 3.00-friendly form (no binding= qualifier).
//   VK   path: VULKAN defined    -> macro emits the Vulkan-required layout(binding=N) form.
//
// Vulkan binding numbers are produced by shaderc's SetBindingBase(uniform_kind_buffer, 8)
// shifting every UBO binding up by kUboBindingBase. So writing TK_UBO_BINDING(3) here lands
// at SPIR-V binding 11 for Vulkan and stays a plain layout(std140) uniform on GL where the
// number 3 is bound from the C++ side via glUniformBlockBinding. Sampler bindings are not
// shifted (kTextureBindingBase = 0); s_textureN's N is its binding directly.
//
// See VulkanBindings.h for the full Vulkan binding map.

#ifdef VULKAN
  #define TK_UBO_BINDING(n)     layout(std140, binding = n)
  #define TK_SAMPLER_BINDING(n) layout(binding = n)
#else
  #define TK_UBO_BINDING(n)     layout(std140)
  #define TK_SAMPLER_BINDING(n)
#endif

#endif // TK_VULKAN_COMPAT
  -->
  </source>
</shader>

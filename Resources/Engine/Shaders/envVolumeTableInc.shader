<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "18" name = "s_envVolumeTable" />
	<source>
	<!--
  #ifndef ENV_VOLUME_TABLE_INC
  #define ENV_VOLUME_TABLE_INC

  // Env-volume table accessor — Phase 2b step 4.
  // Each row holds 11 RGBA32F texels: params + min + max + inverse-transform (4) + world-transform (4).
  // This include is ONLY pulled in by instanceDataInc.shader (vertex shader),
  // so the sampler + LoadEnvVolume are never seen by the fragment shader
  // (which stays on the legacy perDraw-UBO path through step 6).

  #define ENV_VOLUME_STRIDE 11

  struct EnvVolumeData
  {
    vec4 params;     // x: intensity, y: fadeDistance, z: interior, w: pccEnabled
    vec4 volMin;     // xyz: volume min (local space)
    vec4 volMax;     // xyz: volume max (local space)
    mat4 invTransform;
    mat4 worldTransform;
  };

  TK_SAMPLER_BINDING(18) uniform sampler2D s_envVolumeTable;
  #define TK_ENV_TABLE_TEX_WIDTH 1024

  ivec2 envTableTexel(int linear)
  {
    return ivec2(linear % TK_ENV_TABLE_TEX_WIDTH, linear / TK_ENV_TABLE_TEX_WIDTH);
  }

  // Fetch one env-volume row (11 RGBA32F texels) from the global table.
  EnvVolumeData LoadEnvVolume(int idx)
  {
    int o = idx * ENV_VOLUME_STRIDE;
    vec4 d0  = texelFetch(s_envVolumeTable, envTableTexel(o),   0); // params
    vec4 d1  = texelFetch(s_envVolumeTable, envTableTexel(o+1), 0); // volMin
    vec4 d2  = texelFetch(s_envVolumeTable, envTableTexel(o+2), 0); // volMax
    vec4 d3  = texelFetch(s_envVolumeTable, envTableTexel(o+3), 0); // invT col0
    vec4 d4  = texelFetch(s_envVolumeTable, envTableTexel(o+4), 0); // invT col1
    vec4 d5  = texelFetch(s_envVolumeTable, envTableTexel(o+5), 0); // invT col2
    vec4 d6  = texelFetch(s_envVolumeTable, envTableTexel(o+6), 0); // invT col3
    vec4 d7  = texelFetch(s_envVolumeTable, envTableTexel(o+7), 0); // wldT col0
    vec4 d8  = texelFetch(s_envVolumeTable, envTableTexel(o+8), 0); // wldT col1
    vec4 d9  = texelFetch(s_envVolumeTable, envTableTexel(o+9), 0); // wldT col2
    vec4 d10 = texelFetch(s_envVolumeTable, envTableTexel(o+10), 0); // wldT col3

    EnvVolumeData v;
    v.params    = d0;
    v.volMin    = d1;
    v.volMax    = d2;
    v.invTransform  = mat4(d3, d4, d5, d6);
    v.worldTransform = mat4(d7, d8, d9, d10);
    return v;
  }

  #endif // ENV_VOLUME_TABLE_INC
	-->
	</source>
</shader>

<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "23" name = "s_envVolumeTableFS" />
	<source>
	<!--
  #ifndef ENV_VOLUME_TABLE_FS_INC
  #define ENV_VOLUME_TABLE_FS_INC

  // Fragment-shader-safe env-volume table — Phase 2b step 7.
  // Same table as the vertex shader's s_envVolumeTable (slot 18), bound at slot 23
  // to avoid conflict with fragment-stage IBL cubemaps.

  #define ENV_VOLUME_FS_STRIDE 11

  struct EnvVolumeDataFS
  {
    vec4 params;     // x: intensity, y: fadeDistance, z: interior, w: pccEnabled
    vec4 volMin;     // xyz: volume min (local space)
    vec4 volMax;     // xyz: volume max (local space)
    mat4 invTransform;
    mat4 worldTransform;
  };

  TK_SAMPLER_BINDING(23) uniform sampler2D s_envVolumeTableFS;
  #define TK_ENV_TABLE_FS_TEX_WIDTH 1024

  ivec2 envTableFSTexel(int linear)
  {
    return ivec2(linear % TK_ENV_TABLE_FS_TEX_WIDTH, linear / TK_ENV_TABLE_FS_TEX_WIDTH);
  }

  EnvVolumeDataFS LoadEnvVolumeFS(int idx)
  {
    int o = idx * ENV_VOLUME_FS_STRIDE;
    EnvVolumeDataFS v;
    v.params    = texelFetch(s_envVolumeTableFS, envTableFSTexel(o),   0);
    v.volMin    = texelFetch(s_envVolumeTableFS, envTableFSTexel(o+1), 0);
    v.volMax    = texelFetch(s_envVolumeTableFS, envTableFSTexel(o+2), 0);
    v.invTransform  = mat4(texelFetch(s_envVolumeTableFS, envTableFSTexel(o+3), 0),
                           texelFetch(s_envVolumeTableFS, envTableFSTexel(o+4), 0),
                           texelFetch(s_envVolumeTableFS, envTableFSTexel(o+5), 0),
                           texelFetch(s_envVolumeTableFS, envTableFSTexel(o+6), 0));
    v.worldTransform = mat4(texelFetch(s_envVolumeTableFS, envTableFSTexel(o+7), 0),
                            texelFetch(s_envVolumeTableFS, envTableFSTexel(o+8), 0),
                            texelFetch(s_envVolumeTableFS, envTableFSTexel(o+9), 0),
                            texelFetch(s_envVolumeTableFS, envTableFSTexel(o+10), 0));
    return v;
  }

  #endif // ENV_VOLUME_TABLE_FS_INC
	-->
	</source>
</shader>

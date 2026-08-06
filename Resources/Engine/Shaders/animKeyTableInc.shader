<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "21" name = "s_animKeyTable" />
	<source>
	<!--
  #ifndef ANIM_KEY_TABLE_INC
  #define ANIM_KEY_TABLE_INC

  // Animation key table — Phase 2b step 6.
  // Frame-local storage for per-instance keyframe/blend/skin parameters (4 RGBA32F texels).
  // Indexed by InstanceRecord.animKeyIndex; rows are rebuilt each frame.
  // This include is ONLY pulled in by instanceDataInc.shader (vertex shader),
  // so the sampler + LoadAnim are never seen by the fragment shader
  // (which stays on the legacy perDraw-UBO path through step 6).

  #define ANIM_KEY_STRIDE 4

  struct AnimKeyData
  {
    vec4 keyFrameData;          // x: kf1, y: kf2, z: interpTime, w: kfCount
    vec4 blendFrameData;        // x: blendKf1, y: blendKf2, z: blendInterpTime, w: blendKfCount
    vec4 skinParams;            // x: numBones, y: isSkinned, z: isAnimated, w: blendAnimation
    vec4 animBlendFactorAndPad; // x: blendFactor, yzw: pad
  };

  TK_SAMPLER_BINDING(21) uniform sampler2D s_animKeyTable;
  #define TK_ANIM_KEY_TABLE_TEX_WIDTH 1024

  ivec2 animKeyTexel(int linear)
  {
    return ivec2(linear % TK_ANIM_KEY_TABLE_TEX_WIDTH, linear / TK_ANIM_KEY_TABLE_TEX_WIDTH);
  }

  AnimKeyData LoadAnim(int idx)
  {
    int o = idx * ANIM_KEY_STRIDE;
    AnimKeyData a;
    a.keyFrameData          = texelFetch(s_animKeyTable, animKeyTexel(o),   0);
    a.blendFrameData        = texelFetch(s_animKeyTable, animKeyTexel(o+1), 0);
    a.skinParams            = texelFetch(s_animKeyTable, animKeyTexel(o+2), 0);
    a.animBlendFactorAndPad = texelFetch(s_animKeyTable, animKeyTexel(o+3), 0);
    return a;
  }

  #endif // ANIM_KEY_TABLE_INC
	-->
	</source>
</shader>

<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<texture slot = "2" name = "s_blendWeights" />
	<texture slot = "3" name = "s_skinningPose" />
	<source>
	<!--
  #ifndef SKIN_SHADER
  #define SKIN_SHADER

  layout(location = 4) in vec4 vBones;
  layout(location = 5) in vec4 vWeights;

  // Skin / animation uniforms now live in PerDrawData UBO (slot 6). Backing fields:
  //   perDraw._skinParams           (numBones, isSkinned, isAnimated, blendAnimation)
  //   perDraw._keyFrameData         (kf1, kf2, interpTime, kfCount)
  //   perDraw._blendFrameData       (blendKf1, blendKf2, blendInterpTime, blendKfCount)
  //   perDraw._animBlendFactorAndPad.x  // animationBlendFactor

  TK_SAMPLER_BINDING(2) uniform sampler2D s_blendWeights; // Blend animation texture
  TK_SAMPLER_BINDING(3) uniform sampler2D s_skinningPose; // Animation data texture

  // Phase 2b step 7: TK_INSTANCED=1 uses safe defaults (skinCalc is never called
  // in the fragment shader; skinned objects stay on the legacy perDraw path until Phase 7).
  #if TK_INSTANCED
    #define numBones        0.0
    #define isSkinned       false
    #define isAnimated      false
    #define blendAnimation  false
  #else
    #define numBones        perDraw._skinParams.x
    #define isSkinned       (perDraw._skinParams.y > 0.5)
    #define isAnimated      (perDraw._skinParams.z > 0.5)
    #define blendAnimation  (perDraw._skinParams.w > 0.5)
  #endif

  struct SkinLookup
  {
    float stepX;
    float halfStepX;
    float invBoneCount;
  };

  SkinLookup buildSkinLookup()
  {
    SkinLookup lk;
    lk.invBoneCount = 1.0 / numBones;
    lk.stepX = lk.invBoneCount * 0.25;
    lk.halfStepX = lk.stepX * 0.5;
    return lk;
  }

  // Fetch single bone matrix (non-animated or bind pose)
  mat4 fetchBoneMatrix(sampler2D tex, float boneIdx,
                       mediump float keyframe, mediump float invFrameCount,
                       const SkinLookup lk)
  {
    mediump float u0 = boneIdx * lk.invBoneCount + lk.halfStepX;
    mediump float v  = keyframe + invFrameCount * 0.5;
    float s = lk.stepX;
    mediump float u1 = u0 + s;
    mediump float u2 = u1 + s;
    mediump float u3 = u2 + s;

    return mat4(
      texture(tex, vec2(u0, v)),
      texture(tex, vec2(u1, v)),
      texture(tex, vec2(u2, v)),
      texture(tex, vec2(u3, v))
    );
  }

  // Fetch & interpolate two keyframes in one pass — halves register pressure
  mat4 fetchBoneMatrixLerp(sampler2D tex, float boneIdx,
                           mediump float kf1, mediump float kf2,
                           mediump float t, mediump float invFrameCount,
                           const SkinLookup lk)
  {
    mediump float u0 = boneIdx * lk.invBoneCount + lk.halfStepX;
    mediump float halfInv = invFrameCount * 0.5;
    mediump float v1 = kf1 + halfInv;
    mediump float v2 = kf2 + halfInv;
    float s = lk.stepX;
    mediump float u1 = u0 + s;
    mediump float u2 = u1 + s;
    mediump float u3 = u2 + s;

    return mat4(
      mix(texture(tex, vec2(u0, v1)), texture(tex, vec2(u0, v2)), t),
      mix(texture(tex, vec2(u1, v1)), texture(tex, vec2(u1, v2)), t),
      mix(texture(tex, vec2(u2, v1)), texture(tex, vec2(u2, v2)), t),
      mix(texture(tex, vec2(u3, v1)), texture(tex, vec2(u3, v2)), t)
    );
  }

  // Resolve bone matrix — unified path, no isAnimated branch
  mat4 resolveBoneMatrix(sampler2D tex, float boneIdx, vec4 kd, const SkinLookup lk)
  {
    mediump float invFC = 1.0 / kd.w;
    if (!isAnimated)
      return fetchBoneMatrix(tex, boneIdx, 0.5, invFC, lk);
    else
      return fetchBoneMatrixLerp(tex, boneIdx, kd.x, kd.y, kd.z, invFC, lk);
  }

  // -- Position-only skinning ------------------------------------------

  void skinCalc(in sampler2D tex, vec4 kd, in vec4 vtx, out vec4 dst)
  {
    dst = vec4(0.0);
    SkinLookup lk = buildSkinLookup();

    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue;
      dst += resolveBoneMatrix(tex, vBones[i], kd, lk) * vtx * w;
    }
  }

  // -- Position + Normal skinning --------------------------------------

  void skinCalc(in sampler2D tex, vec4 kd, in vec4 vtx, in vec3 nrm,
                out vec4 dstPos, out vec3 dstNrm)
  {
    dstPos = vec4(0.0);
    dstNrm = vec3(0.0);
    SkinLookup lk = buildSkinLookup();

    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue;

      mat4 m = resolveBoneMatrix(tex, vBones[i], kd, lk);
      dstPos += m * vtx * w;
      dstNrm += mat3(m) * nrm * w;
    }
    dstNrm = normalize(dstNrm);
  }

  // -- Position + Normal + BiTangent skinning --------------------------

  void skinCalc(in sampler2D tex, vec4 kd, in vec4 vtx, in vec3 nrm, in vec3 biTan,
                out vec4 dstPos, out vec3 dstNrm, out vec3 dstBiTan)
  {
    dstPos   = vec4(0.0);
    dstNrm   = vec3(0.0);
    dstBiTan = vec3(0.0);
    SkinLookup lk = buildSkinLookup();

    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue;

      mat4 m  = resolveBoneMatrix(tex, vBones[i], kd, lk);
      mat3 m3 = mat3(m);
      dstPos   += m  * vtx   * w;
      dstNrm   += m3 * nrm   * w;
      dstBiTan += m3 * biTan * w;
    }
    dstNrm   = normalize(dstNrm);
    dstBiTan = normalize(dstBiTan);
  }

  // -- skin() entry points ---------------------------------------------

  // Phase 2b step 7: skin() entry points reference perDraw UBO fields and are never
  // called when TK_INSTANCED=1 (skinned objects stay on legacy path until Phase 7).
  #if !TK_INSTANCED
  void skin(in vec4 vertexPos, out vec4 skinnedPos)
  {
    skinCalc(s_skinningPose, perDraw._keyFrameData, vertexPos, skinnedPos);

    if (blendAnimation)
    {
      vec4 blendResult;
      skinCalc(s_blendWeights, perDraw._blendFrameData, vertexPos, blendResult);
      skinnedPos = mix(skinnedPos, blendResult, perDraw._animBlendFactorAndPad.x);
    }
  }

  void skin(in vec4 vertexPos, in vec3 vertexNormal,
            out vec4 skinnedPos, out vec3 skinnedNormal)
  {
    skinCalc(s_skinningPose, perDraw._keyFrameData, vertexPos, vertexNormal, skinnedPos, skinnedNormal);

    if (blendAnimation)
    {
      vec4 bPos;
      vec3 bNrm;
      skinCalc(s_blendWeights, perDraw._blendFrameData, vertexPos, vertexNormal, bPos, bNrm);
      skinnedPos    = mix(skinnedPos, bPos, perDraw._animBlendFactorAndPad.x);
      skinnedNormal = normalize(mix(skinnedNormal, bNrm, perDraw._animBlendFactorAndPad.x));
    }
  }

  void skin(in vec4 vertexPos, in vec3 vertexNormal, in vec3 vertexBiTangent,
            out vec4 skinnedPos, out vec3 skinnedNormal, out vec3 skinnedBiTangent)
  {
    skinCalc(s_skinningPose, perDraw._keyFrameData,
             vertexPos, vertexNormal, vertexBiTangent,
             skinnedPos, skinnedNormal, skinnedBiTangent);

    if (blendAnimation)
    {
      vec4 bPos;
      vec3 bNrm, bBiTan;
      skinCalc(s_blendWeights, perDraw._blendFrameData,
               vertexPos, vertexNormal, vertexBiTangent,
               bPos, bNrm, bBiTan);
      skinnedPos       = mix(skinnedPos, bPos, perDraw._animBlendFactorAndPad.x);
      skinnedNormal    = normalize(mix(skinnedNormal, bNrm, perDraw._animBlendFactorAndPad.x));
      skinnedBiTangent = normalize(mix(skinnedBiTangent, bBiTan, perDraw._animBlendFactorAndPad.x));
    }
  }
  #endif // !TK_INSTANCED
  #endif
	-->
	</source>
</shader>

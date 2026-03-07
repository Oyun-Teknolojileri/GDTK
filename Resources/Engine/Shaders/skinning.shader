<shader>
	<type name = "includeShader" />
  <uniform name = "isSkinned" />
  <uniform name = "numBones" />
  <uniform name = "keyFrame1" />
  <uniform name = "keyFrame2" />
  <uniform name = "keyFrameIntepolationTime" />
  <uniform name = "keyFrameCount" />
  <uniform name  = "isAnimated" />
  <uniform name = "blendAnimation" />
  <uniform name = "blendFactor" />
  <uniform name = "blendKeyFrame1" />
  <uniform name = "blendKeyFrame2" />
  <uniform name = "blendKeyFrameIntepolationTime" />
  <uniform name = "blendKeyFrameCount" />
	<source>
	<!--

#ifndef SKIN_SHADER
#define SKIN_SHADER

layout(location = 4) in vec4 vBones;
layout(location = 5) in vec4 vWeights;

uniform float numBones;
uniform uint isSkinned;
uniform uint isAnimated;
uniform float keyFrame1; // normalized via key / keycount
uniform float keyFrame2; // normalized via key / keycount
uniform float keyFrameIntepolationTime;
uniform float keyFrameCount;
uniform int blendAnimation;
uniform float blendFactor;
uniform float blendKeyFrame1;
uniform float blendKeyFrame2;
uniform float blendKeyFrameIntepolationTime;
uniform float blendKeyFrameCount;
uniform sampler2D s_texture2; // Blend animation data texture
uniform sampler2D s_texture3; // Animation data texture

// Precomputed texture lookup constants to avoid redundant math per bone.
struct SkinLookup
{
  float stepX;
  vec2 centerOffset;
};

SkinLookup buildSkinLookup(float boneCount, float frameCount)
{
  SkinLookup lk;
  lk.stepX = 1.0 / (boneCount * 4.0);
  lk.centerOffset = vec2(lk.stepX * 0.5, 0.5 / frameCount);
  return lk;
}

mat4 getMatrixFromTexture(sampler2D tex, float boneIdx, float keyframe, const SkinLookup lk)
{
  float u = boneIdx / numBones + lk.centerOffset.x;
  float v = keyframe + lk.centerOffset.y;
  float s = lk.stepX;

  return mat4(
    texture(tex, vec2(u,           v)),
    texture(tex, vec2(u + s,       v)),
    texture(tex, vec2(u + s + s,   v)),
    texture(tex, vec2(u + s + s + s, v))
  );
}

// Column-wise linear interpolation for mat4 (mix() does not support mat4 in GLSL ES).
mat4 lerpMat4(mat4 a, mat4 b, float t)
{
  return mat4(
    mix(a[0], b[0], t),
    mix(a[1], b[1], t),
    mix(a[2], b[2], t),
    mix(a[3], b[3], t)
  );
}

// -- Position-only skinning ------------------------------------------

void skinCalc(in sampler2D tex, vec4 kd, in vec4 vtx, out vec4 dst)
{
  dst = vec4(0.0);
  SkinLookup lk = buildSkinLookup(numBones, kd.w);

  if (isAnimated == 0u)
  {
    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue; // Skip zero-weight bones — saves up to 4 texture fetches each
      dst += getMatrixFromTexture(tex, vBones[i], 0.5, lk) * vtx * w;
    }
  }
  else
  {
    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue;

      dst += lerpMat4(
        getMatrixFromTexture(tex, vBones[i], kd.x, lk),
        getMatrixFromTexture(tex, vBones[i], kd.y, lk),
        kd.z
      ) * vtx * w;
    }
  }
}

// -- Position + Normal skinning --------------------------------------

void skinCalc(in sampler2D tex, vec4 kd, in vec4 vtx, in vec3 nrm,
              out vec4 dstPos, out vec3 dstNrm)
{
  dstPos = vec4(0.0);
  dstNrm = vec3(0.0);
  SkinLookup lk = buildSkinLookup(numBones, kd.w);

  if (isAnimated == 0u)
  {
    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue;

      mat4 m = getMatrixFromTexture(tex, vBones[i], 0.5, lk);
      dstPos += m * vtx * w;
      dstNrm += mat3(m) * nrm * w;
    }
  }
  else
  {
    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue;

      mat4 m = lerpMat4(
        getMatrixFromTexture(tex, vBones[i], kd.x, lk),
        getMatrixFromTexture(tex, vBones[i], kd.y, lk),
        kd.z
      );
      dstPos += m * vtx * w;
      dstNrm += mat3(m) * nrm * w;
    }
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
  SkinLookup lk = buildSkinLookup(numBones, kd.w);

  if (isAnimated == 0u)
  {
    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue;

      mat4 m  = getMatrixFromTexture(tex, vBones[i], 0.5, lk);
      mat3 m3 = mat3(m);
      dstPos   += m  * vtx   * w;
      dstNrm   += m3 * nrm   * w;
      dstBiTan += m3 * biTan * w;
    }
  }
  else
  {
    for (int i = 0; i < 4; i++)
    {
      float w = vWeights[i];
      if (w < 0.001) continue;

      mat4 m = lerpMat4(
        getMatrixFromTexture(tex, vBones[i], kd.x, lk),
        getMatrixFromTexture(tex, vBones[i], kd.y, lk),
        kd.z
      );
      mat3 m3 = mat3(m);
      dstPos   += m  * vtx   * w;
      dstNrm   += m3 * nrm   * w;
      dstBiTan += m3 * biTan * w;
    }
  }
  dstNrm   = normalize(dstNrm);
  dstBiTan = normalize(dstBiTan);
}

// -- skin() entry points (unchanged signatures) ---------------------

void skin(in vec4 vertexPos, out vec4 skinnedPos)
{
  skinCalc(s_texture3, vec4(keyFrame1, keyFrame2, keyFrameIntepolationTime, keyFrameCount),
           vertexPos, skinnedPos);

  if (blendAnimation != 0)
  {
    vec4 blendResult;
    skinCalc(s_texture2, vec4(blendKeyFrame1, blendKeyFrame2, blendKeyFrameIntepolationTime, blendKeyFrameCount),
             vertexPos, blendResult);
    skinnedPos = mix(skinnedPos, blendResult, blendFactor);
  }
}

void skin(in vec4 vertexPos, in vec3 vertexNormal, out vec4 skinnedPos, out vec3 skinnedNormal)
{
  skinCalc(s_texture3, vec4(keyFrame1, keyFrame2, keyFrameIntepolationTime, keyFrameCount),
           vertexPos, vertexNormal, skinnedPos, skinnedNormal);

  if (blendAnimation != 0)
  {
    vec4 bPos;
    vec3 bNrm;
    skinCalc(s_texture2, vec4(blendKeyFrame1, blendKeyFrame2, blendKeyFrameIntepolationTime, blendKeyFrameCount),
             vertexPos, vertexNormal, bPos, bNrm);
    skinnedPos    = mix(skinnedPos, bPos, blendFactor);
    skinnedNormal = normalize(mix(skinnedNormal, bNrm, blendFactor));
  }
}

void skin(in vec4 vertexPos, in vec3 vertexNormal, in vec3 vertexBiTangent,
          out vec4 skinnedPos, out vec3 skinnedNormal, out vec3 skinnedBiTangent)
{
  skinCalc(s_texture3, vec4(keyFrame1, keyFrame2, keyFrameIntepolationTime, keyFrameCount),
           vertexPos, vertexNormal, vertexBiTangent, skinnedPos, skinnedNormal, skinnedBiTangent);

  if (blendAnimation != 0)
  {
    vec4 bPos;
    vec3 bNrm, bBiTan;
    skinCalc(s_texture2, vec4(blendKeyFrame1, blendKeyFrame2, blendKeyFrameIntepolationTime, blendKeyFrameCount),
             vertexPos, vertexNormal, vertexBiTangent, bPos, bNrm, bBiTan);
    skinnedPos       = mix(skinnedPos, bPos, blendFactor);
    skinnedNormal    = normalize(mix(skinnedNormal, bNrm, blendFactor));
    skinnedBiTangent = normalize(mix(skinnedBiTangent, bBiTan, blendFactor));
  }
}

#endif

	-->
	</source>
</shader>

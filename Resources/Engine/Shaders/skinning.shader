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

mat4 getMatrixFromTexture(sampler2D animDataTexture, float boneIndx, float keyframe, float stepX, vec2 centerOffset)
{
  float matrixPos = boneIndx / numBones;

  return mat4(texture(animDataTexture, vec2(matrixPos,              keyframe) + centerOffset),
              texture(animDataTexture, vec2(matrixPos + stepX,      keyframe) + centerOffset),
              texture(animDataTexture, vec2(matrixPos + stepX*2.0,  keyframe) + centerOffset),
              texture(animDataTexture, vec2(matrixPos + stepX*3.0,  keyframe) + centerOffset));
}

void skinCalc(in sampler2D animDataTexture, vec4 keyFramesData, in vec4 vertexPos, out vec4 skinnedPos)
{
  skinnedPos = vec4(0.0);

  float stepX      = 1.0 / (numBones * 4.0);
  vec2 centerOffset = vec2(stepX * 0.5, 1.0 / (keyFramesData.w * 2.0));

  if (isAnimated == 0u)
  {
    for (int i = 0; i < 4; i++)
    {
      mat4 bindPoseMatrix = getMatrixFromTexture(animDataTexture, vBones[i], 0.5, stepX, centerOffset);
      skinnedPos += bindPoseMatrix * vertexPos * vWeights[i];
    }
  }
  else
  {
    for (int i = 0; i < 4; i++)
    {
      mat4 kf1Mat = getMatrixFromTexture(animDataTexture, vBones[i], keyFramesData.x, stepX, centerOffset);
      mat4 kf2Mat = getMatrixFromTexture(animDataTexture, vBones[i], keyFramesData.y, stepX, centerOffset);

      skinnedPos += (kf1Mat + (kf2Mat - kf1Mat) * keyFramesData.z) * vertexPos * vWeights[i];
    }
  }
}

void skinCalc
(
  in sampler2D animDataTexture,
  vec4 keyFramesData,
  in vec4 vertexPos,
  in vec3 vertexNormal,
  out vec4 skinnedPos,
  out vec3 skinnedNormal
)
{
  skinnedPos    = vec4(0.0);
  skinnedNormal = vec3(0.0);

  float stepX       = 1.0 / (numBones * 4.0);
  vec2 centerOffset = vec2(stepX * 0.5, 1.0 / (keyFramesData.w * 2.0));

  if (isAnimated == 0u)
  {
    for (int i = 0; i < 4; i++)
    {
      mat4 bindPoseMatrix  = getMatrixFromTexture(animDataTexture, vBones[i], 0.5, stepX, centerOffset);
      float w              = vWeights[i];
      skinnedPos          += bindPoseMatrix * vertexPos * w;
      skinnedNormal       += mat3(bindPoseMatrix) * vertexNormal * w;
    }
  }
  else
  {
    for (int i = 0; i < 4; i++)
    {
      mat4 kf1Mat  = getMatrixFromTexture(animDataTexture, vBones[i], keyFramesData.x, stepX, centerOffset);
      mat4 kf2Mat  = getMatrixFromTexture(animDataTexture, vBones[i], keyFramesData.y, stepX, centerOffset);
      float w      = vWeights[i];

      mat4 blendedMat  = kf1Mat + (kf2Mat - kf1Mat) * keyFramesData.z;
      mat3 blendedMat3 = mat3(blendedMat);

      skinnedPos    += blendedMat  * vertexPos    * w;
      skinnedNormal += blendedMat3 * vertexNormal * w;
    }
  }
  skinnedNormal = normalize(skinnedNormal);
}

void skinCalc
(
  in sampler2D animDataTexture,
  vec4 keyFramesData,
  in vec4 vertexPos,
  in vec3 vertexNormal,
  in vec3 vertexBiTangent,
  out vec4 skinnedPos,
  out vec3 skinnedNormal,
  out vec3 skinnedBiTangent
)
{
  skinnedPos       = vec4(0.0);
  skinnedNormal    = vec3(0.0);
  skinnedBiTangent = vec3(0.0);

  float stepX       = 1.0 / (numBones * 4.0);
  vec2 centerOffset = vec2(stepX * 0.5, 1.0 / (keyFramesData.w * 2.0));

  if (isAnimated == 0u)
  {
    for (int i = 0; i < 4; i++)
    {
      mat4 bindPoseMatrix   = getMatrixFromTexture(animDataTexture, vBones[i], 0.5, stepX, centerOffset);
      mat3 bindPoseMatrix3  = mat3(bindPoseMatrix);
      float w               = vWeights[i];
      skinnedPos           += bindPoseMatrix  * vertexPos       * w;
      skinnedNormal        += bindPoseMatrix3 * vertexNormal    * w;
      skinnedBiTangent     += bindPoseMatrix3 * vertexBiTangent * w;
    }
  }
  else
  {
    for (int i = 0; i < 4; i++)
    {
      mat4 kf1Mat  = getMatrixFromTexture(animDataTexture, vBones[i], keyFramesData.x, stepX, centerOffset);
      mat4 kf2Mat  = getMatrixFromTexture(animDataTexture, vBones[i], keyFramesData.y, stepX, centerOffset);
      float w      = vWeights[i];

      mat4 blendedMat  = kf1Mat + (kf2Mat - kf1Mat) * keyFramesData.z;
      mat3 blendedMat3 = mat3(blendedMat);

      skinnedPos       += blendedMat  * vertexPos       * w;
      skinnedNormal    += blendedMat3 * vertexNormal     * w;
      skinnedBiTangent += blendedMat3 * vertexBiTangent  * w;
    }
  }
  skinnedNormal    = normalize(skinnedNormal);
  skinnedBiTangent = normalize(skinnedBiTangent);
}

void skin(in vec4 vertexPos, out vec4 skinnedPos)
{
  skinCalc(s_texture3, vec4(keyFrame1, keyFrame2, keyFrameIntepolationTime, keyFrameCount), vertexPos, skinnedPos);

  if (blendAnimation != 0)
  {
    vec4 blendingAnimSkinnedPos;
    skinCalc(s_texture2, vec4(blendKeyFrame1, blendKeyFrame2, blendKeyFrameIntepolationTime, blendKeyFrameCount),
      vertexPos, blendingAnimSkinnedPos);
    skinnedPos = mix(skinnedPos, blendingAnimSkinnedPos, blendFactor);
  }
}

void skin(in vec4 vertexPos, in vec3 vertexNormal, out vec4 skinnedPos, out vec3 skinnedNormal)
{
  skinCalc(s_texture3, vec4(keyFrame1, keyFrame2, keyFrameIntepolationTime, keyFrameCount),
    vertexPos, vertexNormal, skinnedPos, skinnedNormal);

  if (blendAnimation != 0)
  {
    vec4 blendingAnimSkinnedPos;
    vec3 blendingAnimNormal;
    skinCalc(s_texture2, vec4(blendKeyFrame1, blendKeyFrame2, blendKeyFrameIntepolationTime, blendKeyFrameCount),
      vertexPos, vertexNormal, blendingAnimSkinnedPos, blendingAnimNormal);
    skinnedPos    = mix(skinnedPos, blendingAnimSkinnedPos, blendFactor);
    skinnedNormal = normalize(mix(skinnedNormal, blendingAnimNormal, blendFactor));
  }
}

void skin
(
  in vec4 vertexPos,
  in vec3 vertexNormal,
  in vec3 vertexBiTangent,
  out vec4 skinnedPos,
  out vec3 skinnedNormal,
  out vec3 skinnedBiTangent
)
{
  skinCalc(s_texture3, vec4(keyFrame1, keyFrame2, keyFrameIntepolationTime, keyFrameCount),
    vertexPos, vertexNormal, vertexBiTangent, skinnedPos, skinnedNormal, skinnedBiTangent);

  if (blendAnimation != 0)
  {
    vec4 blendingAnimSkinnedPos;
    vec3 blendingAnimNormal;
    vec3 blendingBiTangent;
    skinCalc(s_texture2, vec4(blendKeyFrame1, blendKeyFrame2, blendKeyFrameIntepolationTime, blendKeyFrameCount),
      vertexPos, vertexNormal, vertexBiTangent, blendingAnimSkinnedPos, blendingAnimNormal, blendingBiTangent);
    skinnedPos       = mix(skinnedPos, blendingAnimSkinnedPos, blendFactor);
    skinnedNormal    = normalize(mix(skinnedNormal, blendingAnimNormal, blendFactor));
    skinnedBiTangent = normalize(mix(skinnedBiTangent, blendingBiTangent, blendFactor));
  }
}

#endif

	-->
	</source>
</shader>

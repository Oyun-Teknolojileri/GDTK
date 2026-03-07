<shader>
	<type name = "includeShader" />
  <define name = "SMFormat16Bit" val="0,1" />
	<source>
	<!--
  #ifndef VSM_SHADER
  #define VSM_SHADER
  const vec3 PoissonDisk[16] = vec3[]
  (
    vec3(-0.308466, -0.140553, -0.393857),
    vec3(-0.422605, 0.380276, -0.49527),
    vec3(0.446471, 0.0725272, -0.292199),
    vec3(-0.346095, -0.128437, 0.212912),
    vec3(0.186911, -0.471374, -0.118),
    vec3(-0.0681326, 0.347896, 0.0368816),
    vec3(-0.488922, 0.447325, 0.324915),
    vec3(0.228751, 0.05974, 0.331782),
    vec3(0.151418, 0.443449, -0.48233),
    vec3(0.417875, -0.463469, 0.418912),
    vec3(0.229453, 0.412809, 0.475585),
    vec3(0.0848262, -0.0337077, -0.268303),
    vec3(0.375729, -0.330988, -0.458647),
    vec3(-0.165731, 0.207511, 0.427885),
    vec3(-0.0748772, -0.282098, 0.440733),
    vec3(0.366756, 0.323298, 0.0856197)
  );

  float Random(vec4 seed)
  {
	  float dot_product = dot(seed, vec4(12.9898,78.233,45.164,94.673));
	  return fract(sin(dot_product) * 43758.5453);
  }

  // Evsm from TheRealMJP shadow sample app.
  // https://github.com/TheRealMJP/Shadows

  #define EvsmExponents GetEVSMExponents(40.0, 5.0)

  vec2 GetEVSMExponents(in float positiveExponent, in float negativeExponent)
  {
  #if SMFormat16Bit
      float maxExponent = 5.54f;
  #else
      float maxExponent = 42.0f;
  #endif

      // Clamp to maximum range of fp32/fp16 to prevent overflow/underflow
      return min(vec2(positiveExponent, negativeExponent), maxExponent);
  }

  // Applies exponential warp to shadow map depth, input depth should be in [0, 1]
  vec2 WarpDepth(float depth, vec2 exponents)
  {
      // Rescale depth into [-1, 1]
      depth = 2.0 * depth - 1.0;
      float pos =  exp( exponents.x * depth);
      float neg = -exp(-exponents.y * depth);
      return vec2(pos, neg);
  }

  float saturate(float value) {
    return clamp(value, 0.0, 1.0);
  }
    
  float Linstep(float a, float b, float v)
  {
      return saturate((v - a) / (b - a));
  }

  // Reduces VSM light bleedning
  float ReduceLightBleeding(float pMax, float amount)
  {
    // Remove the [0, amount] tail and linearly rescale (amount, 1].
      return Linstep(amount, 1.0f, pMax);
  }

  float ChebyshevUpperBound(vec2 moments, float mean, float minVariance, float lightBleedingReduction)
  {
      // Compute variance
      float variance = moments.y - (moments.x * moments.x);
      variance = max(variance, minVariance);

      // Compute probabilistic upper bound
      float d = mean - moments.x;
      float pMax = variance / (variance + (d * d));

      pMax = ReduceLightBleeding(pMax, lightBleedingReduction);

      // One-tailed Chebyshev
      return (mean <= moments.x ? 1.0f : pMax);
  }
  #endif
	-->
	</source>
</shader>

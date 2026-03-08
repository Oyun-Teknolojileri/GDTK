<shader>
	<type name = "includeShader" />
	<define name = "SMFormat16Bit" val="0,1" />
	<source>
	<!--
  #ifndef VSM_COMMON_SHADER
  #define VSM_COMMON_SHADER

  #if SMFormat16Bit
      const float VSM_MAX_EXPONENT = 5.54;
  #else
      const float VSM_MAX_EXPONENT = 42.0;
  #endif

  const vec2 EvsmExponents = vec2(min(40.0, VSM_MAX_EXPONENT), min(5.0, VSM_MAX_EXPONENT));

  // Applies exponential warp to shadow map depth, input depth should be in [0, 1]
  vec2 WarpDepth(float depth, vec2 exponents)
  {
      // Rescale depth into [-1, 1]
      depth = 2.0 * depth - 1.0;
      float pos =  exp( exponents.x * depth);
      float neg = -exp(-exponents.y * depth);
      return vec2(pos, neg);
  }

  #endif
	-->
	</source>
</shader>
<shader>
	<type name = "includeShader" />
	<define name = "SMFormat16Bit" val="0,1" />
	<source>
	<!--
  #ifndef VSM_COMMON_SHADER
  #define VSM_COMMON_SHADER

  // ---------------------------------------------------------------------------
  // EVSM Configuration (Filament style)
  // ---------------------------------------------------------------------------

  #if SMFormat16Bit
      const float VSM_MAX_EXPONENT = 5.54;
  #else
      const float VSM_MAX_EXPONENT = 42.0;
  #endif

  // Filament uses a single configurable exponent clamped to the format's max
  const float VsmExponent = min(40.0, VSM_MAX_EXPONENT);

  // Legacy compat: EvsmExponents used by depth shaders
  const vec2 EvsmExponents = vec2(VsmExponent, min(5.0, VSM_MAX_EXPONENT));

  // ---------------------------------------------------------------------------
  // Depth warping (Filament: evaluateEVSM)
  // ---------------------------------------------------------------------------

  // Warps a [0,1] depth to exponential space for EVSM storage
  vec2 WarpDepth(float depth, vec2 exponents)
  {
      // Remap depth into [-1, 1]
      depth = 2.0 * depth - 1.0;
      float pos =  exp( exponents.x * depth);
      float neg = -exp(-exponents.y * depth);
      return vec2(pos, neg);
  }

  #endif
	-->
	</source>
</shader>
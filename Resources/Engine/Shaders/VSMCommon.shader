<shader>
	<type name = "includeShader" />
	<define name = "SMFormat16Bit" val="0,1" />
	<source>
	<!--
  #ifndef VSM_COMMON_SHADER
  #define VSM_COMMON_SHADER

  // ---------------------------------------------------------------------------
  // EVSM Configuration (Filament style)
  // https://google.github.io/filament/Filament.html
  // ---------------------------------------------------------------------------

  #if SMFormat16Bit
      const float VsmExponent = 5.54;
  #else
      const float VsmExponent = 15.0;
  #endif

  // ---------------------------------------------------------------------------
  // Depth warping (Filament: evaluateEVSM)
  // ---------------------------------------------------------------------------

  float WarpDepth(float depth)
  {
      depth = 2.0 * depth - 1.0;
      return exp(VsmExponent * depth);
  }

  #endif
	-->
	</source>
</shader>

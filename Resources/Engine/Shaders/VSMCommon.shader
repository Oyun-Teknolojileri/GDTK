<shader>
	<type name = "includeShader" />
	<source>
	<!--
  #ifndef VSM_COMMON_SHADER
  #define VSM_COMMON_SHADER

  // ---------------------------------------------------------------------------
  // EVSM Configuration (Filament style)
  // https://google.github.io/filament/Filament.html
  // ---------------------------------------------------------------------------

  const float VsmExponent = 5.54;
  const float VsmMaxMoment = 65000.0;

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

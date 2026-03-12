<shader>
	<type name = "includeShader" />
	<include name = "VSMCommon.shader" />
	<source>
	<!--
  #ifndef VSM_SHADER
  #define VSM_SHADER

  // ---------------------------------------------------------------------------
  // Utility
  // ---------------------------------------------------------------------------

  float saturate(float value)
  {
    return clamp(value, 0.0, 1.0);
  }

  // ---------------------------------------------------------------------------
  // Chebyshev Upper Bound (Filament style)
  // https://google.github.io/filament/Filament.html
  // ---------------------------------------------------------------------------

  float ChebyshevUpperBound(vec2 moments, float mean, float minVariance, float lightBleedingReduction)
  {
      // Fast path: receiver is fully in front of the caster
      if (mean <= moments.x)
      {
          return 1.0;
      }

      // Variance with clamped minimum to reduce acne
      float variance = moments.y - (moments.x * moments.x);
      variance = max(variance, minVariance);

      // Standard Chebyshev inequality
      float d = mean - moments.x;
      float pMax = variance / (variance + d * d);

      // Light Bleeding Reduction (Filament style)
      // Remaps [lbr, 1] to [0, 1]
      pMax = saturate((pMax - lightBleedingReduction) / (1.0 - lightBleedingReduction));

      return pMax;
  }

  #endif
	-->
	</source>
</shader>
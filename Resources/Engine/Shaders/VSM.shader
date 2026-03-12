<shader>
	<type name = "includeShader" />
	<include name = "VSMCommon.shader" />
	<source>
	<!--
  #ifndef VSM_SHADER
  #define VSM_SHADER

  // ---------------------------------------------------------------------------
  // Poisson Disk for PCF / blocker search
  // ---------------------------------------------------------------------------

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
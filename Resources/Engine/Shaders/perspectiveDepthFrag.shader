<shader>
	<type name = "fragmentShader" />
	<include name = "VSM.shader" />
    <include name = "materialCacheInc.shader" />
	<include name = "drawDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<define name = "EVSM4" val="0,1" />
	<source>
	<!--
  #version 300 es
  precision highp float;
  precision lowp int;

  in vec4 v_pos;
  in vec2 v_texture;
  out vec4 fragColor;

  uniform sampler2D s_texture0;

  void main()
  {
      Material material = GetMaterial();

  #if DrawAlphaMasked
      float alpha;
      if (material.diffuseTextureInUse == 1)
      {
          alpha = texture(s_texture0, v_texture).a;
      }
      else
      {
          alpha = material.alpha;
      }

      if (alpha <= material.alphaMaskThreshold)
      {
          discard;
      }
  #endif

      float depth = length(v_pos.xyz);
      vec2 exponents = EvsmExponents;
      vec2 vsmDepth = WarpDepth(depth, exponents);
      vec2 vsmDepthSq = vsmDepth * vsmDepth;

  #if EVSM4
      fragColor = vec4(vsmDepth, vsmDepthSq);
  #else
      fragColor = vec4(vsmDepth.x, vsmDepthSq.x, vsmDepth.x, vsmDepthSq.x);
  #endif
  }
	-->
	</source>
</shader>
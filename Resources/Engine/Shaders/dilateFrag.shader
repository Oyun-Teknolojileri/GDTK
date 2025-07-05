<shader>
	<type name = "fragmentShader" />
	<source>
	<!--
  #version 300 es
  precision mediump float;

  uniform vec4 Color;
  uniform sampler2D u_texture;

  in vec2 v_texture;
  out vec4 o_fragColor;

  vec2 g_textureSize;

  #define R 4
  #define SMOOTHING 3.0

  float sampleTexture(vec2 uv) {
      return texture(u_texture, uv).r;
  }

  void main()
  {
      vec2 uv = v_texture;
      float center = sampleTexture(uv);
      if (center == 0.0) {
          // Reject inner part of the stencil
          discard;
      }
    
      g_textureSize = vec2(textureSize(u_texture, 0));
      float minDistance = float(R);
    
      // Search for nearest edge in the kernel
      for (int i = -R; i <= R; i++) {
          for (int j = -R; j <= R; j++) {
              vec2 offset = vec2(i, j);
              float sampleColor = sampleTexture(uv + (offset / g_textureSize));
            
              if (sampleColor == 0.0) {
                  // Calculate distance to this edge pixel
                  float distance = length(offset);
                  minDistance = min(minDistance, distance);
              }
          }
      }
    
      if (minDistance < float(R)) {
          // Create smooth falloff based on distance
          float alpha = 1.0 - smoothstep(0.0, SMOOTHING, minDistance);
          o_fragColor = vec4(Color.rgb, alpha);
      } else {
          discard;
      }
  }
	-->
	</source>
</shader>
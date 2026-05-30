<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<uniform slot = "5" name = "DilatePassData" />
	<texture slot = "0" name = "u_texture" />
	<source>
	<!--
	
	precision mediump float;

	// Pass-specific UBO (slot 5). Mirrors `DilatePassDataLayout` in Renderer.h byte-for-byte.
	TK_UBO_BINDING(5) uniform DilatePassData
	{
		vec4 color;
	} dilate;

	TK_SAMPLER_BINDING(0) uniform sampler2D u_texture;

	TK_LOC(2) in vec2 v_texture;
	out vec4 o_fragColor;

	vec2 g_textureSize;

	#define R 4

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
					o_fragColor = vec4(dilate.color.rgb, 1.0);
			} else {
					discard;
			}
	}
	-->
	</source>
</shader>
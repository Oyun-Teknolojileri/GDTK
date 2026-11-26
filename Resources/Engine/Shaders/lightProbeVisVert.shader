<shader>
	<type name = "vertexShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "cameraDataInc.shader" />
	<texture slot = "0" name = "s_diffuseColor" />
	<source>
	<!--

	precision highp float;
	precision highp int;
	precision highp sampler2D;

	// Light probe sample points. One sphere per instance, placed and scaled here, so the whole
	// probe grid is a single instanced draw.
	//
	// The table arrives on the material's diffuse slot: the renderer binds that one for every job
	// on its own, and it is the only way a texture reaches a shader that is drawn through
	// Renderer::Render - a binding made before the draw is wiped by the pipeline bind on Vulkan.
	// Two texels per point: position and radius, then the colour.
	TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor;

	// Must match SampleTableWidth in LightProbeVisualPass.cpp.
	#define SAMPLE_TEX_WIDTH 256

	layout(location = 0) in vec3 vPosition;

	TK_LOC(0) out vec3 v_color;

	vec4 FetchSample(int point, int texel)
	{
		int linear = point * 2 + texel;
		return texelFetch(s_diffuseColor, ivec2(linear % SAMPLE_TEX_WIDTH, linear / SAMPLE_TEX_WIDTH), 0);
	}

	void main()
	{
		vec4 samplePos   = FetchSample(gl_InstanceID, 0);
		vec4 sampleColor = FetchSample(gl_InstanceID, 1);

		v_color     = sampleColor.rgb;
		gl_Position = camera.projectionView * vec4(samplePos.xyz + vPosition * samplePos.w, 1.0);
	}

	-->
	</source>
</shader>

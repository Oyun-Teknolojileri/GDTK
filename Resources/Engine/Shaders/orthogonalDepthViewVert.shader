<shader>
	<type name = "vertexShader" />
	<include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
	<include name = "drawDataInc.shader" />
	<uniform name = "model" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	// Fixed Attributes.
	layout (location = 0) in vec3 vPosition;
	layout (location = 1) in vec3 vNormal;
	layout (location = 2) in vec2 vTexture;
	layout (location = 3) in vec3 vBiTan;

	uniform mat4 LightView;
	uniform float LightFrustumHalfSize;
	uniform vec3 LightDir; // Should be normalized
	uniform mat4 model;

	out float v_depth;
	out vec2 v_texture;

	void main()
	{
		v_texture = vTexture;
		vec4 skinnedVPos = vec4(vPosition, 1.0);
			
		if(isSkinned > 0u){
			skin(skinnedVPos, skinnedVPos);
		}

		gl_Position = camera.projectionView * model * skinnedVPos;
		v_depth = (LightView * model * skinnedVPos).z / LightFrustumHalfSize;
		v_depth = v_depth * 0.5 + 0.5;
	}
	-->
	</source>
</shader>
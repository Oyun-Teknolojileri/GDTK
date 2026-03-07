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

    uniform mat4 model;
    out vec2 v_texture;
    out float z;

    void main()
    {
      v_texture = vTexture;
      vec4 skinnedVPos = vec4(vPosition, 1.0);

      if (isSkinned)
      {
        skin(skinnedVPos, skinnedVPos);
      }

      vec4 clipPos = camera.projectionView * model * skinnedVPos;

      // Compute normalized depth without gl_DepthRange (avoid driver uniform reads on mobile).
      // Equivalent to: (diff * NDC_z + near + far) * 0.5  where near=0, far=1, diff=1 for standard [0,1].
      z = clipPos.z / clipPos.w * 0.5 + 0.5;

      clipPos.z = 0.0; // Pancake near-plane clamp for shadow casters behind the view frustum.
      gl_Position = clipPos;
    }
	-->
	</source>
</shader>
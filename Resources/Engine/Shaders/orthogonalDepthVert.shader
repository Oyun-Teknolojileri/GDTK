<shader>
	<type name = "vertexShader" />
	<include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
    <define name = "Pancake" val="0,1" />
	<uniform name = "model" />
	<source>
	<!--
		#version 300 es
		precision highp float;
		precision lowp int;

    // Fixed Attributes.
    layout (location = 0) in vec3 vPosition;
    layout (location = 2) in vec2 vTexture;

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

      #if Pancake
      z = clipPos.z / clipPos.w * 0.5 + 0.5;
      clipPos.z = 0.0;
      #endif

      gl_Position = clipPos;
    }
	-->
	</source>
</shader>
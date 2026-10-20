<shader>
	<type name = "vertexShader" />
	<include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
    <define name = "Pancake" val="0,1" />
    <define name = "DrawAlphaMasked" val="0,1" />
	<include name = "perDrawDataInc.shader" />
	<include name = "instanceDataInc.shader" />
	<define name = "INSTANCE_TABLE" val = "0,1" />
	<source>
	<!--
	
	precision highp float;
	precision lowp int;

    // Fixed Attributes.
    layout (location = 0) in vec3 vPosition;

#if DrawAlphaMasked
    layout (location = 2) in vec2 vTexture;
    out vec2 v_texture;
#endif

#if Pancake
    out float z;
#endif

    void main()
    {
    #if DrawAlphaMasked
      v_texture = vTexture;
    #endif

      vec4 skinnedVPos = vec4(vPosition, 1.0);

      if (isSkinned)
      {
        skin(skinnedVPos, skinnedVPos);
      }

      vec4 clipPos = camera.projectionView * INSTANCE_MODEL_MATRIX * skinnedVPos;

    #if Pancake
      z = clipPos.z / clipPos.w * 0.5 + 0.5;
      clipPos.z = 0.0;
    #endif

      gl_Position = clipPos;
    }
	-->
	</source>
</shader>
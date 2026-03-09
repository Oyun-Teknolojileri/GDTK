<shader>
  <type name = "vertexShader" />
  <include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
    <include name = "materialCacheInc.shader" />
	<include name = "drawDataInc.shader" />
    <uniform name = "model" />
    <uniform name = "inverseTransposeModel" />
  <source>
	<!--
  #version 300 es
  precision highp float;
  precision lowp int;

  layout(location = 0) in vec3 vPosition;
  layout(location = 1) in vec3 vNormal;
  layout(location = 2) in vec2 vTexture;
  layout(location = 3) in vec3 vBiTan;

  uniform mat4 model;
  uniform mat4 inverseTransposeModel;

  out vec3 v_viewDepth;
  out vec3 v_normal;
  out mediump vec2 v_texture;
  out mediump mat3 TBN;

  void main()
  {
	    bool normalMapInUse = materialCache[3].y > 0.5; // GetMaterial().normalMapInUse

      vec4 localPos = vec4(vPosition, 1.0);
      vec3 N = vNormal;
      vec3 B = vBiTan;

      // Skinning
      if (isSkinned)
      {
          if (normalMapInUse)
		      {
			      skin(localPos, N, B, localPos, N, B);
		      }    
          else
		      {
            skin(localPos, N, localPos, N);
		      }
      }

      // World-space normal / TBN
	    if (normalMapInUse)
	    {
		    vec3 worldNormal = normalize(mat3(inverseTransposeModel) * N);
		    vec3 worldBiTan = normalize(mat3(inverseTransposeModel) * B);
		    vec3 worldTan = normalize(cross(worldBiTan, worldNormal));
        
		    TBN = mat3(worldTan, worldBiTan, worldNormal);
	    }
	    else
	    {
		    v_normal = normalize(mat3(inverseTransposeModel) * N);
	    }

      // Position
      vec4 worldPos = model * localPos;
      v_viewDepth   = (camera.view * worldPos).xyz;
      v_texture     = vTexture;
      gl_Position   = camera.projectionView * worldPos;
  }
	-->
	</source>
</shader>
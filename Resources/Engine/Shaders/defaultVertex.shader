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
  layout(location = 3) in vec3 vTangent;

  out vec3 v_worldPos;
  out vec3 v_worldNormal;
  out float v_viewDepth;
  out mediump vec2 v_texture;
  out mediump mat3 TBN;

  uniform mat4 model;
  uniform mat4 inverseTransposeModel;

    void main()
    {
	    bool normalMapInUse = IsNormalMapInUse();
  
      vec4 localPos = vec4(vPosition, 1.0);
      vec3 N = vNormal;
      vec3 T = vTangent;

      // Skinning
      if (isSkinned)
      {
          if (normalMapInUse)
		      {
			      skin(localPos, N, T, localPos, N, T);
		      }    
          else
		      {
            skin(localPos, N, localPos, N);
		      }
      }

      // World-space normal / TBN
	    if (normalMapInUse)
	    {
		    mat3 normalMatrix = mat3(inverseTransposeModel);

		    vec3 wN = normalize(normalMatrix * N);
		    vec3 wT = normalize(normalMatrix * T);
		    // T = normalize(T - dot(T, N) * N); // Re orthogonalize.
		    vec3 wB = cross(N, T);
		    TBN = mat3(wT, wB, wN);
	    }
	    else
	    {
		    v_worldNormal = normalize(mat3(inverseTransposeModel) * N);
	    }

      vec4 worldPos = model * localPos;
      v_worldPos = worldPos.xyz;
      v_viewDepth = (camera.view * worldPos).z;
      v_texture = vTexture;
      gl_Position = camera.projectionView * worldPos;
    }
	-->
	</source>
</shader>
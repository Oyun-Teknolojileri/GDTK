<shader>
	<type name = "vertexShader" />
    <include name = "skinning.shader" />
	  <include name = "cameraDataInc.shader" />
    <include name = "materialCacheInc.shader" />
    <include name = "drawDataInc.shader" />
    <include name = "perDrawDataInc.shader" />
    <include name = "instanceDataInc.shader" />
    <define name = "INSTANCE_TABLE" val = "0,1" />
	<source>
	<!--
  
  precision highp float;
  precision highp int;

  layout(location = 0) in vec3 vPosition;
  layout(location = 1) in vec3 vNormal;
  layout(location = 2) in vec2 vTexture;
  layout(location = 3) in vec4 vTangent;

  TK_LOC(0) out vec3 v_worldPos;
  TK_LOC(1) out vec3 v_worldNormal;
  TK_LOC(3) out float v_viewDepth;
  TK_LOC(2) out highp vec2 v_texture;
  TK_LOC(4) out highp mat3 TBN;

    void main()
    {
	    bool normalMapInUse = IsNormalMapInUse();
  
      vec4 localPos = vec4(vPosition, 1.0);
	  vec3 N = vNormal;
	  vec3 T = vTangent.xyz;
	  float bitangentSign = vTangent.w;

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
			mat3 normalMatrix = mat3(INSTANCE_NORMAL_MATRIX);

			vec3 wN = normalize(normalMatrix * N);
			vec3 wT = normalize(normalMatrix * T);
			wT = normalize(wT - dot(wT, wN) * wN); // Re orthogonalize.
			vec3 wB = cross(wN, wT) * bitangentSign;
			TBN = mat3(wT, wB, wN);
		}
	    else
	    {
		    v_worldNormal = normalize(mat3(INSTANCE_NORMAL_MATRIX) * N);
	    }

      vec4 worldPos = INSTANCE_MODEL_MATRIX * localPos;
      v_worldPos = worldPos.xyz;
      v_viewDepth = (camera.view * worldPos).z;
      v_texture = vTexture;
      gl_Position = camera.projectionView * worldPos;
    }
	-->
	</source>
</shader>
<shader>
	<type name = "vertexShader" />
    <include name = "skinning.shader" />
	  <include name = "cameraDataInc.shader" />
    <include name = "materialCacheInc.shader" />
    <include name = "drawDataInc.shader" />
    <include name = "perDrawDataInc.shader" />
    <include name = "instanceDataInc.shader" />
    <define name = "TK_INSTANCED" val="0,1" />
	<source>
	<!--
  
  precision highp float;
  precision lowp int;

  layout(location = 0) in vec3 vPosition;
  layout(location = 1) in vec3 vNormal;
  layout(location = 2) in vec2 vTexture;
  layout(location = 3) in vec4 vTangent;

  TK_LOC(0) out vec3 v_worldPos;
  TK_LOC(1) out vec3 v_worldNormal;
  TK_LOC(3) out float v_viewDepth;
  TK_LOC(2) out mediump vec2 v_texture;
  TK_LOC(4) out mediump mat3 TBN;

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

	  // World-space transform matrices. TK_INSTANCED reads them from the instance data
	  // texture via LoadInstance (Phase 2a transport); the legacy path reads them from
	  // the per-draw UBO. Both hold identical PerDrawUboLayout bytes -> identical result.
#if TK_INSTANCED
	  InstanceRecord inst = LoadInstance(TK_INSTANCE_ID);
	  mat4 model                 = inst.model;
	  mat4 inverseTransposeModel = inst.inverseTransposeModel;
#else
	  mat4 model                 = perDraw._model;
	  mat4 inverseTransposeModel = perDraw._inverseTransposeModel;
#endif

	  // World-space normal / TBN
		if (normalMapInUse)
		{
			mat3 normalMatrix = mat3(inverseTransposeModel);

			vec3 wN = normalize(normalMatrix * N);
			vec3 wT = normalize(normalMatrix * T);
			wT = normalize(wT - dot(wT, wN) * wN); // Re orthogonalize.
			vec3 wB = cross(wN, wT) * bitangentSign;
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
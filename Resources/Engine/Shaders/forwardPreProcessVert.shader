<shader>
  <type name = "vertexShader" />
  <include name = "skinning.shader" />
  <include name = "cameraDataInc.shader" />
  <include name = "materialCacheInc.shader" />
  <include name = "drawDataInc.shader" />
  <include name = "perDrawDataInc.shader" />
  <include name="vulkanCompatInc.shader" />
	<source>
  <!--
  #version 300 es
  precision highp float;
  precision lowp int;

  layout(location = 0) in vec3 vPosition;
  layout(location = 1) in vec3 vNormal;
  layout(location = 2) in vec2 vTexture;
  layout(location = 3) in vec4 vTangent;

  TK_LOC(0) out float v_linearDepth;
  TK_LOC(1) out vec3 v_normal;
  TK_LOC(2) out mediump vec2 v_texture;
  TK_LOC(3) out mediump mat3 TBN;

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
          mat3 normalMatrix = mat3(perDraw._inverseTransposeModel);

          vec3 wN = normalize(normalMatrix * N);
          vec3 wT = normalize(normalMatrix * T);
          wT = normalize(wT - dot(wT, wN) * wN); // Re orthogonalize.
          vec3 wB = cross(wN, wT) * bitangentSign;
          TBN = mat3(wT, wB, wN);
      }
      else
      {
          v_normal = normalize(mat3(perDraw._inverseTransposeModel) * N);
      }

      // Position
      vec4 worldPos = perDraw._model * localPos;
      v_linearDepth = -(camera.view * worldPos).z;
      v_texture     = vTexture;
      gl_Position   = camera.projectionView * worldPos;
  }
  -->
  </source>
</shader>

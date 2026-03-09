<shader>
	<type name = "vertexShader" />
    <include name = "skinning.shader" />
	  <include name = "cameraDataInc.shader" />
    <uniform name = "model" />
    <uniform name = "inverseTransposeModel" />
    <uniform name = "normalMapInUse" />
	<source>
	<!--
  #version 300 es
  precision highp float;
  precision lowp int;

  layout(location = 0) in vec3 vPosition;
  layout(location = 1) in vec3 vNormal;
  layout(location = 2) in vec2 vTexture;
  layout(location = 3) in vec3 vBiTan;

  out vec3 v_pos;
  out vec3 v_normal;
  out vec2 v_texture;
  out float v_viewPosDepth;
  out mat3 TBN;

  uniform mat4 model;
  uniform mat4 inverseTransposeModel;
  uniform bool normalMapInUse;

    void main()
    {
      vec4 localPos = vec4(vPosition, 1.0);
      vec3 N = vNormal;
      vec3 B = vBiTan;

      if (isSkinned)
      {
        // Skin in local space first, then transform to world space.
        // Bone matrices operate in local space.
        if (normalMapInUse)
        {
          skin(localPos, N, B, localPos, N, B);
          N = normalize((inverseTransposeModel * vec4(N, 0.0)).xyz);
          B = normalize((inverseTransposeModel * vec4(B, 0.0)).xyz);
        }
        else
        {
          skin(localPos, N, localPos, N);
          N = normalize((inverseTransposeModel * vec4(N, 0.0)).xyz);
        }
      }
      else
      {
        N = normalize((inverseTransposeModel * vec4(N, 0.0)).xyz);
        if (normalMapInUse)
        {
          B = normalize((inverseTransposeModel * vec4(B, 0.0)).xyz);
        }
      }

      if (normalMapInUse)
      {
        vec3 T = normalize(cross(B, N));
        TBN = mat3(T, B, N);
      }
      else
      {
        v_normal = N;
      }

      vec4 worldPos = model * localPos;
      v_pos = worldPos.xyz;
      v_viewPosDepth = (camera.view * worldPos).z;
      gl_Position = camera.projectionView * worldPos;
      v_texture = vTexture;
    }
	-->
	</source>
</shader>
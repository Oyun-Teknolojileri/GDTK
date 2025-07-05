<shader>
  <type name = "vertexShader" />
  <include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
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

  // out vec3 v_pos;
  out vec3 v_viewDepth;
  out vec3 v_normal;
  out vec2 v_texture;
  out mat3 TBN;
      
  void main()
  {
    Material material = GetMaterial();
  
    gl_Position   = vec4(vPosition, 1.0f);

      if(isSkinned > 0u)
      {
        if (material.normalMapInUse == 1)
        {
            vec3 B = normalize(vec3(model * vec4(vBiTan, 0.0)));
            vec3 N = normalize(vec3(model * vec4(vNormal, 0.0)));

            skin(gl_Position, N, B, gl_Position, N, B);

            vec3 T = normalize(cross(B,N));
            TBN = mat3(T,B,N);
        }
        else
        {
            v_normal = (inverseTransposeModel * vec4(vNormal, 1.0)).xyz;
            skin(gl_Position, v_normal, gl_Position, v_normal);
        }
      }
      else
      {
			  if (material.normalMapInUse == 1)
			  {
            vec3 B = normalize(vec3(model * vec4(vBiTan, 0.0)));
            vec3 N = normalize(vec3(model * vec4(vNormal, 0.0)));
            vec3 T = normalize(cross(B,N));
            TBN = mat3(T,B,N);
        }
        else
        {
            v_normal = (inverseTransposeModel * vec4(vNormal, 1.0)).xyz;
        }
      }

    vec3 v_pos    = (model * gl_Position).xyz;
    v_viewDepth = (camera.view * vec4(v_pos, 1.0)).xyz;
        
    v_texture = vTexture;

    gl_Position   = camera.projectionView * model * gl_Position;
  }
	-->
	</source>
</shader>
<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<texture slot = "14" name = "s_instanceData" />
	<source>
	<!--
	#ifndef INSTANCE_DATA
	#define INSTANCE_DATA

	// Per instance model matrices, fetched in the vertex stage.
	//
	// Layout: 8 texels per instance, the four columns of the model matrix followed by the four
	// columns of its inverse transpose, in an RGBA32F texture 256 texels wide (32 instances per
	// row). Both packers, ShadowPass and ForwardRenderPass, must match these constants.
	//
	// A batch is drawn with glDrawElementsInstanced, so gl_InstanceID counts from zero and
	// perDraw._instanceParams.x carries the index of the batch's first instance in the table.
	// Reading the matrix from a texture instead of an instanced vertex attribute is deliberate:
	// the vertex input, the mesh VAOs and the Vulkan pipeline variants stay untouched, which is
	// what makes this work the same on WebGL 2.0.
	//////////////////////////////////////////

	TK_SAMPLER_BINDING(14) uniform sampler2D s_instanceData;

	#define INSTANCE_TEXELS_PER_MATRIX 4
	#define INSTANCE_TEXELS_PER_ITEM 8
	#define INSTANCE_TEX_WIDTH 256

	ivec2 InstanceTexelCoord(int instance, int texel)
	{
		int linear = instance * INSTANCE_TEXELS_PER_ITEM + texel;
		return ivec2(linear % INSTANCE_TEX_WIDTH, linear / INSTANCE_TEX_WIDTH);
	}

	mat4 FetchInstanceMatrix(int instance, int firstTexel)
	{
		mat4 m;
		m[0] = texelFetch(s_instanceData, InstanceTexelCoord(instance, firstTexel + 0), 0);
		m[1] = texelFetch(s_instanceData, InstanceTexelCoord(instance, firstTexel + 1), 0);
		m[2] = texelFetch(s_instanceData, InstanceTexelCoord(instance, firstTexel + 2), 0);
		m[3] = texelFetch(s_instanceData, InstanceTexelCoord(instance, firstTexel + 3), 0);
		return m;
	}

	// The model matrix of the instance this vertex belongs to.
	mat4 InstanceModel() { return FetchInstanceMatrix(perDraw._instanceParams.x + gl_InstanceID, 0); }

	// Its inverse transpose, which is what normals and tangents need.
	mat4 InstanceInverseTransposeModel()
	{
		return FetchInstanceMatrix(perDraw._instanceParams.x + gl_InstanceID, INSTANCE_TEXELS_PER_MATRIX);
	}

	// Model matrix accessor for shaders that can run both batched and per object. The flag is
	// defaulted to a number on purpose: a bare #if INSTANCED with the name undefined makes the
	// driver report "syntax error in #if".
	#ifndef INSTANCE_TABLE
	  #define INSTANCE_TABLE 0
	#endif

	#if INSTANCE_TABLE
	  #define INSTANCE_MODEL_MATRIX InstanceModel()
	  #define INSTANCE_NORMAL_MATRIX InstanceInverseTransposeModel()
	#else
	  #define INSTANCE_MODEL_MATRIX perDraw._model
	  #define INSTANCE_NORMAL_MATRIX perDraw._inverseTransposeModel
	#endif

	#endif // INSTANCE_DATA
	-->
	</source>
</shader>

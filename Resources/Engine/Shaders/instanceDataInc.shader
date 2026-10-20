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
	// Layout: 4 texels per instance holding the four columns of the model matrix, laid out in an
	// RGBA32F texture 256 texels wide (64 instances per row). The packer in
	// ShadowPass::PrepareShadowCasterJobs must match these constants exactly.
	//
	// A batch is drawn with glDrawElementsInstanced, so gl_InstanceID counts from zero and
	// perDraw._instanceParams.x carries the index of the batch's first instance in the table.
	// Reading the matrix from a texture instead of an instanced vertex attribute is deliberate:
	// the vertex input, the mesh VAOs and the Vulkan pipeline variants stay untouched, which is
	// what makes this work the same on WebGL 2.0.
	//////////////////////////////////////////

	TK_SAMPLER_BINDING(14) uniform sampler2D s_instanceData;

	#define INSTANCE_TEXELS_PER_MATRIX 4
	#define INSTANCE_TEX_WIDTH 256

	ivec2 InstanceTexelCoord(int instance, int texel)
	{
		int linear = instance * INSTANCE_TEXELS_PER_MATRIX + texel;
		return ivec2(linear % INSTANCE_TEX_WIDTH, linear / INSTANCE_TEX_WIDTH);
	}

	mat4 GetInstanceMatrix(int instance)
	{
		mat4 m;
		m[0] = texelFetch(s_instanceData, InstanceTexelCoord(instance, 0), 0);
		m[1] = texelFetch(s_instanceData, InstanceTexelCoord(instance, 1), 0);
		m[2] = texelFetch(s_instanceData, InstanceTexelCoord(instance, 2), 0);
		m[3] = texelFetch(s_instanceData, InstanceTexelCoord(instance, 3), 0);
		return m;
	}

	// The model matrix of the instance this vertex belongs to.
	mat4 InstanceModel()
	{
		return GetInstanceMatrix(perDraw._instanceParams.x + gl_InstanceID);
	}

	// Model matrix accessor for shaders that can run both batched and per object. The flag is
	// defaulted to a number on purpose: a bare #if INSTANCED with the name undefined makes the
	// driver report "syntax error in #if".
	#ifndef INSTANCE_TABLE
	  #define INSTANCE_TABLE 0
	#endif

	#if INSTANCE_TABLE
	  #define INSTANCE_MODEL_MATRIX InstanceModel()
	#else
	  #define INSTANCE_MODEL_MATRIX perDraw._model
	#endif

	#endif // INSTANCE_DATA
	-->
	</source>
</shader>

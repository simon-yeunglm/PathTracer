
// by simon yeung, 26/05/2018
// all rights reserved

#define PI			3.14159265358979323846
#define MAXLIGHT	(4)

struct VSInput
{
	float2 pos	: POSITION;
};

struct PSInput
{
	float4 pos	: SV_POSITION;
	float2 uv	: TEXCOORD;
};

struct Material
{
	float4 albedo;
	float4 emissive;
};

struct AreaLight
{
	float4x4	xform;
	float4x4	xformInv;
	float4		radiance;
	float		halfWidth;
	float		halfHeight;
	float		oneOverArea;
	int			padding0;
};

struct Ray
{
	float3		pos;
	float3		dir;
};

cbuffer SceneConstantBuffer : register(b0)
{
	AreaLight	areaLight[MAXLIGHT];
	int			numLight;
	int			numMesh;
};

cbuffer ViewConstantBuffer : register(b1)
{
	float4x4	projInv;
	float3		camPos;
	int			frameIdx;
	float2		camPixelOffset;
	int2		viewportSize;
	int			randSeedInterval;
	int			randSeedOffset;
	int			randSeedAdd;
	int			isEnableBlur;
};

SamplerState					linear_sampler				: register(s0);
Texture2D						path_trace_tex				: register(t0);
StructuredBuffer<float4		>	scene_bufferTriPos			: register(t1);
StructuredBuffer<float4		>	scene_bufferTriNor			: register(t2);
StructuredBuffer<int		>	scene_bufferTriIdx			: register(t3);
StructuredBuffer<Material	>	scene_bufferMeshMaterial	: register(t4);
StructuredBuffer<int2		>	scene_bufferMeshIdxRange	: register(t5);

uint wang_hash(uint seed)
{
	seed = (seed ^ 61) ^ (seed >> 16);
	seed *= 9;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return seed;
}

float rand(inout uint seed)
{
	float r= wang_hash(seed) * (1.0 / 4294967296.0);
	seed+= randSeedAdd;
	return r;
}

float3	createPerpendicularVector(float3 u)
{
	float3 a = abs(u);

	uint tmpX = (asuint(a.x - a.y) & 0x80000000) >> 31;
	uint tmpY = (asuint(a.x - a.z) & 0x80000000) >> 31;
	uint tmpZ = (asuint(a.y - a.z) & 0x80000000) >> 31;

	uint uyx = tmpX;
	uint uzx = tmpY;
	uint uzy = tmpZ;

	uint xm = uyx & uzx;
	uint ym = (1 ^ xm) & uzy;
	uint zm = 1 ^ (xm & ym);

	float3 v = cross(u, float3((float)xm, (float)ym, (float)zm));
	return normalize(v);
}

// return float3(t, u, v), t < 0 implies not intersect
float3 rayTriIntersect(Ray ray, float3 vertex0, float3 vertex1, float3 vertex2)
{
	const float epsilon = 0.00001f;
	float3 edge1, edge2, h, s, q;
	float a, f, u, v;
	edge1 = vertex1 - vertex0;
	edge2 = vertex2 - vertex0;
	h = cross(ray.dir, edge2);
	a = dot(edge1, h);
	if (
#if 0	// is back-face culling?
		a > -epsilon &&
#endif
		a < epsilon)
		return float3(-1.0, 0, 0);

	f = 1 / a;
	s = ray.pos - vertex0;
	u = f * dot(s, h);
	if (u < 0.0 || u > 1.0)
		return float3(-1.0, 0, 0);

	q = cross(s, edge1);
	v = f * dot(ray.dir, q);
	if (v < 0.0 || u + v > 1.0)
		return float3(-1.0, 0, 0);

	// At this stage we can compute t to find out where the intersection point is on the line.
	float t = f * dot(edge2, q);
	if (t <= epsilon)
		return float3(-1.0, 0, 0); // This means that there is a line intersection but not a ray intersection.
	else
		return float3(t, u, v);
}

float3	sceneRayCast(Ray ray, out int hitMeshIdx, out int3 hitTriIdx)
{
	const float MAX_T	= 999999999999999.0f;
	float3	hitTUV		= float3(MAX_T, 0, 0);
	int		hit_meshIdx = 0;
	int3	hit_triIdx	= int3(0, 0, 0);
	int		num_mesh	= numMesh;
	for (int mesh = 0; mesh < num_mesh; ++mesh)
	{
		int2 meshIdxRange = scene_bufferMeshIdxRange[mesh];
		for (int triIdx = meshIdxRange.x; triIdx < meshIdxRange.y; triIdx+= 3)
		{
			int idx0	= scene_bufferTriIdx[triIdx  ];
			int idx1	= scene_bufferTriIdx[triIdx+1];
			int idx2	= scene_bufferTriIdx[triIdx+2];

			float4 pos0 = scene_bufferTriPos[idx0];
			float4 pos1 = scene_bufferTriPos[idx1];
			float4 pos2 = scene_bufferTriPos[idx2];

			float3 tuv	= rayTriIntersect(ray, pos0.xyz, pos1.xyz, pos2.xyz);
			if (tuv.x < hitTUV.x && tuv.x >= 0)
			{
				hitTUV		= tuv;
				hit_meshIdx = mesh;
				hit_triIdx	= int3(idx0, idx1, idx2);
			}
		}
	}

	hitMeshIdx	= hit_meshIdx;
	hitTriIdx	= hit_triIdx;
	if (hitTUV.x == MAX_T)
		return float3(-1.0f, 0, 0);
	else
		return hitTUV;
}

void sampleBrdfDir_uniformHemiSphere(Material material, inout float3 outDir, inout float outPropability, inout uint randSeed)
{
	float r0 = rand(randSeed);
	float r1 = rand(randSeed);
	float r0_1_minus_sqrt = sqrt(1.0f - r0*r0);
	float r1_2_pi = 2.0f * PI * r1;
	outDir = float3(	cos(r1_2_pi) * r0_1_minus_sqrt	,
						sin(r1_2_pi) * r0_1_minus_sqrt	,
						r0								);
	outPropability = 1.0f / (2.0 * PI);
}

void sampleBrdfDir_cosWeightHemiSphere(Material material, inout float3 outDir, inout float outPropability, inout uint randSeed)
{
	float r0;
	float r1;
	r0				= rand(randSeed);
	r1				= max(rand(randSeed), 0.001);	// otherwise outPropability will be too close to zero which result in artificat when dividing this pdf
	float phi		= 2.0 * PI * r0;
	float sinPhi	= sin(phi);
	float cosPhi	= cos(phi);
	float cosTheta	= sqrt(r1);
	float sinTheta	= sqrt(1.0f - r1);

	outDir = float3(	sinTheta * cosPhi	,
						sinTheta * sinPhi	,
						cosTheta			);
	outPropability = cosTheta / PI;
}

void sampleBrdfDir(Material material, inout float3 outDir, inout float outPropability, inout uint randSeed)
{
	sampleBrdfDir_cosWeightHemiSphere(material, outDir, outPropability, randSeed);
//	sampleBrdfDir_uniformHemiSphere(material, outDir, outPropability, randSeed);
}

float3	sampleAreaLightPos(AreaLight light, inout uint randSeed)
{
	float2 posLS_xz	= mad(float2(rand(randSeed), rand(randSeed)), 2, -1) * float2(light.halfWidth, light.halfHeight);
	return mul(light.xform, float4(posLS_xz.x, 0, posLS_xz.y, 1)).xyz;
}

float3	sampleAreaLightRadiance(AreaLight light, float3 emitDir, out float propability)
{
	propability= light.oneOverArea;
	float	cosAngle	= dot(emitDir, light.xform[1].xyz);
	return cosAngle > 0 ? light.radiance.xyz : float3(0, 0, 0);
}

PSInput fullscreenQuad_vs(VSInput input_vs)
{
	PSInput result;

	result.pos	= float4(input_vs.pos, 0, 1);
	result.uv	= mad(input_vs.pos, float2(0.5, -0.5), 0.5);

	return result;
}

float4 debug_rand_num_ps(PSInput input_ps) : SV_TARGET
{
	//  random number debug display
	int2	px_pos		= (int2)input_ps.pos.xy;
	uint	randSeed	= (px_pos.y * viewportSize.x + px_pos.x) * randSeedInterval + randSeedOffset;

	float r = rand(randSeed);
	return float4(r, r, r, 1);
}

float4 tonemap_ps(PSInput input_ps) : SV_TARGET
{
	// current not tonemap for simplicity...
	const int blurFrame= 16;
	if ((frameIdx < blurFrame) && isEnableBlur)
	{
		float4		centerColor		= path_trace_tex.SampleLevel(linear_sampler, input_ps.uv, 0);

		// early out to save performance
		[branch]
		if (centerColor.a	== 0)
			return float4(0, 0, 0, 0);

		// blur to reduce noise for the first few frames
		const int	maxBlurSmaple	= 12;
		const int	num				= maxBlurSmaple - max(frameIdx - (blurFrame - maxBlurSmaple), 0);
		float2		pxOffset		= rcp((float2)viewportSize);
		float4		avgColor		= float4(0, 0, 0, 0);
		float		weight			= 0;
		for(int x=-num; x<=num; ++x)
			for(int y=-num; y<=num; ++y)
			{
				float4	color= path_trace_tex.SampleLevel(linear_sampler, input_ps.uv +  float2( x * pxOffset.x	,  y * pxOffset.y), 0);
				float	w	 = abs(color.a - centerColor.a) < 0.1;		// only average for the same surface geometry hash
				avgColor	 += color * w;
				weight		 += w;
			}
		avgColor/= weight;

		return avgColor;
	}
	else
		return path_trace_tex.SampleLevel(linear_sampler, input_ps.uv, 0);
}

PSInput pathTrace_vs(VSInput input_vs)
{
	PSInput result;

	result.pos	= float4(input_vs.pos, 0, 1);
	result.uv	= input_vs.pos;

	return result;
}

float4 pathTrace_ps(PSInput input_ps) : SV_TARGET
{
	// generate rand seed
	int2	px_pos		= (int2)input_ps.pos.xy;
	uint	randSeed	= (px_pos.y * viewportSize.x + px_pos.x) * randSeedInterval + randSeedOffset;

	// generate primary ray
	float4 pos_ndc	= float4(input_ps.uv + camPixelOffset, -1.0, 1.0);
	float4 pos_ws	= mul(projInv, pos_ndc);
	pos_ws			/= pos_ws.w;

	Ray ray;
	ray.pos			= camPos;
	ray.dir			= pos_ws.xyz - camPos;
	
	int		trace_depth					= 10;	// choose a small number to have better performance, but a biased result...
	float3	coef_brdf					= float3(1, 1, 1);
	float3	totalOutgoingRadiance		= float3(0, 0, 0);
	float	russianRoulettePropability	= 1;
	float3	triTUV;
	int		hitMeshIdx;
	int3	hitTriIdx;
	
	int		firstHhitMeshIdx			= -1;
	float3	firstHitNormal				= float3(0, 0, 0);

	// path tracing iteration
	[loop]
	for(int d=0; d<trace_depth; ++d)
	{
		triTUV= sceneRayCast(ray, hitMeshIdx, hitTriIdx);
		if (triTUV.x < 0.0)
			break;

		// compute hit surface parameter
		Material	hitMaterial	= scene_bufferMeshMaterial[hitMeshIdx];
		float3		hitPos		= ray.pos + ray.dir * triTUV.x;
		float3		hitNormal	=(	scene_bufferTriNor[hitTriIdx.x] * (1.0f - triTUV.y - triTUV.z)	+
									scene_bufferTriNor[hitTriIdx.y] * triTUV.y						+
									scene_bufferTriNor[hitTriIdx.z] * triTUV.z						).xyz;
		hitNormal				= normalize(hitNormal);

		// store first hit mesh for de-noise
		[unroll]
		if (d ==0)
		{
			firstHhitMeshIdx	= hitMeshIdx;
			firstHitNormal		= hitNormal;
		}
		
		// direct lighting
		totalOutgoingRadiance += coef_brdf * hitMaterial.emissive.xyz;
		for(int l= 0; l<numLight; ++l)
		{
			// sample light surface pos
			Ray			shadowRay;
			float		shadowRayEpsilon	= 0.000001;
			AreaLight	light				= areaLight[l];
			float3		lightSurfacePosWS	= sampleAreaLightPos(light, randSeed);
			float3		lightDir			= lightSurfacePosWS - hitPos;
			float	len2					= dot(lightDir, lightDir);
			float	len						= sqrt(len2);
			lightDir						= lightDir / len;	// normalize
			shadowRay.dir					= lightDir;
			shadowRay.pos					= hitPos + shadowRay.dir * shadowRayEpsilon;

			// cast shadow ray
			float3	shadowTriTUV;
			int		shadowHitMeshIdx;
			int3	shadowHitTriIdx;
			shadowTriTUV= sceneRayCast(shadowRay, shadowHitMeshIdx, shadowHitTriIdx);

			// skip light if in shadow
			if (shadowTriTUV.x >= shadowRayEpsilon && (shadowTriTUV.x < len) )
				continue;

			// sample light radiance
			float	propability;
			float3	radiance	= sampleAreaLightRadiance(light, -lightDir, propability);
			propability			*= russianRoulettePropability;
			float	lightAngle	= max(dot(light.xform[1].xyz, -lightDir), 0.0f);
			float	cosFactor	= max(dot(lightDir, hitNormal), 0.0f);
			totalOutgoingRadiance += radiance * coef_brdf * hitMaterial.albedo.xyz * (cosFactor / propability) *(lightAngle / len2);
		}
		
		// russian roulette terminate
		if (d > 5)	// skip russian roulette in first few iteration to reduce noise
		{
			float terminatePropability = max(hitMaterial.albedo.x, max(hitMaterial.albedo.y, hitMaterial.albedo.z))*PI;
			russianRoulettePropability *= terminatePropability;
			if (rand(randSeed) > terminatePropability)
				break;
		}
		
		// path traced
		float3	randDirTS;
		float	randDirProbability;
		sampleBrdfDir(hitMaterial, randDirTS, randDirProbability, randSeed);
		
		// convert randDirTS to world space
		float3	binormal	= createPerpendicularVector(hitNormal);
		float3	tangent		= cross(hitNormal, binormal);
		binormal			= normalize(binormal);
		tangent				= normalize(tangent );
		float3x3	basis;
		basis[0]			= tangent;
		basis[1]			= binormal;
		basis[2]			= hitNormal;
		float3 randDirWS	= mul(randDirTS, basis);

		ray.pos				= hitPos;
		ray.dir				= randDirWS;
		coef_brdf			*= hitMaterial.albedo.xyz * max(dot(randDirWS, hitNormal), 0.0f) / randDirProbability;
	}
	
	// light directly hit the camera
	pos_ndc	= float4(input_ps.uv, -1.0, 1.0);
	pos_ws	= mul(projInv, pos_ndc);
	pos_ws			/= pos_ws.w;
	for(int l= 0; l<numLight; ++l)
	{
		AreaLight	light				= areaLight[l];
		float4		lightPlaneLS		= float4(0, 1, 0, 0);	// LS == local space

		// use un-jitter ray to avoid strong contrast light color bleed to geometry during de-noise pass
		Ray unJitterCameraRay;
		unJitterCameraRay.pos			= camPos;
		unJitterCameraRay.dir			= pos_ws.xyz - camPos;

		Ray rayInv;
		rayInv.pos	= mul(light.xformInv, float4(unJitterCameraRay.pos, 1)).xyz;
		rayInv.dir	= mul(light.xformInv, float4(unJitterCameraRay.dir, 0)).xyz;
		
        float dirDotNormal= dot(rayInv.dir, lightPlaneLS.xyz);
        float hitT	=  dirDotNormal == 0 ? -1 : -(dot(rayInv.pos, lightPlaneLS.xyz) + lightPlaneLS.w)/dirDotNormal;
		if (hitT <= 0 || dirDotNormal >= 0)	// check if ray hit light ray and is back face culled
			continue;
		
		float3	hitPos	= mad(rayInv.dir, hitT, rayInv.pos);
		if (abs(hitPos.x) > light.halfWidth ||
			abs(hitPos.z) > light.halfHeight )
			continue;
		
		// cast ray to check if light is blocked by other geometry
		float	shadowRayEpsilon	= 0.000001;
		float3	shadowTriTUV;
		int		shadowHitMeshIdx;
		int3	shadowHitTriIdx;
		shadowTriTUV= sceneRayCast(unJitterCameraRay, shadowHitMeshIdx, shadowHitTriIdx);
		if (shadowTriTUV.x >= shadowRayEpsilon && shadowTriTUV.x < hitT )
			continue;

		totalOutgoingRadiance	+= light.radiance.xyz;
		firstHitNormal			= 0;
		firstHhitMeshIdx		= numMesh + l;
	}

	// use normal as geometry hash because all mesh are cube
	float geometryHash	=	firstHhitMeshIdx	== -1 ? 0 : 
							dot(firstHitNormal, float3(1, 10, 100)) + (firstHhitMeshIdx + 1) * 1000;	
							// firstHhitMeshIdx + 1 to identify hit pixel with alpha == 0 for de-noise
	return float4(totalOutgoingRadiance * rcp(frameIdx+1), geometryHash); 
}

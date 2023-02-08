// Shadow map related variables
#define LIGHT_WORLD_SIZE 0.05
#define LIGHT_FRUSTUM_WIDTH 10.0
#define LIGHT_SIZE_UV (LIGHT_WORLD_SIZE / LIGHT_FRUSTUM_WIDTH)

#define NEAR_PLANE 1.0

#define NUM_SAMPLES 10
#define BLOCKER_SEARCH_NUM_SAMPLES NUM_SAMPLES
#define PCF_NUM_SAMPLES NUM_SAMPLES
#define NUM_RINGS 10

#define PI 3.141592653589793
#define PI2 6.283185307179586

highp vec2 poissonDisk[NUM_SAMPLES];


highp float rand_2to1(highp vec2 uv) 
{
	// 0 -1
	const highp float a = 12.9898, b = 78.233, c= 43758.5453;
    highp float       dt = dot(uv.xy, vec2(a, b));
	highp float sn = mod(dt, PI);
    return fract(sin(sn) * c);
}

highp float unpack(in highp vec4 rgbaDepth) 
{ 
	const highp vec4 bitShift = vec4(1.0, 1.0 / 256.0, 1.0 / (256.0 * 256.0), 1.0 / (256.0 * 256.0 * 256.0));
    return dot(rgbaDepth, bitShift);
}

void poissonDiskSamples(const in highp vec2 randomSeed)
{
	highp float ANGLES_STEP = PI2 * float(NUM_RINGS) / float(NUM_SAMPLES);
    highp float INV_NUM_SAMPLES = 1.0 / float(NUM_SAMPLES);

	highp float angle      = rand_2to1(randomSeed) * PI2;
    highp float radius     = INV_NUM_SAMPLES;
    highp float radiusStep = radius;

	for (int i = 0; i < NUM_SAMPLES; ++i)
	{
        poissonDisk[i] = vec2(cos(angle), sin(angle)) * pow(radius, 0.75);
		radius += radiusStep;
		angle += ANGLES_STEP;
	}
}

highp float findBlocker(in sampler2D shadowMap, highp vec2 uv, highp float zReceiver) 
{
	// This uses similar triangles to compute what
	// area of the shadow map we should search
    highp float searchRadius = LIGHT_SIZE_UV * (zReceiver - NEAR_PLANE) / zReceiver;
	highp float blockerDepthSum = 0.0;
	highp int numBlockers = 0;
	for (int i = 0; i < BLOCKER_SEARCH_NUM_SAMPLES; ++i)
	{
        highp float shadowMapDepth = unpack(texture(shadowMap, uv + poissonDisk[i] * searchRadius));

		if (shadowMapDepth < zReceiver)
		{
			blockerDepthSum += shadowMapDepth;
			++numBlockers;
		}
	}

	if (numBlockers == 0)
	{
		return -1.0;
	}

	return blockerDepthSum / float(numBlockers);
}

highp float penumbraSize(highp float zReceiver, highp float zBlocker) 
{
	// Parallel plane estimation
    return (zReceiver - zBlocker) / zBlocker;
}

highp float PCF_Filter(in sampler2D shadowMap, highp vec2 uv, highp float zReceiver, highp float filterRadius)
{ 
	highp float sum = 0.0;

	for (int i = 0; i < PCF_NUM_SAMPLES; ++i)
	{
        highp float depth = unpack(texture(shadowMap, uv + poissonDisk[i] * filterRadius));

		if (zReceiver <= depth)
		{
			sum += 1.0;
		}
	}

	for (int i = 0; i < PCF_NUM_SAMPLES; ++i)
	{
        highp float depth = unpack(texture(shadowMap, uv + -poissonDisk[i].yx * filterRadius));

		if (zReceiver <= depth)
		{
			sum += 1.0;
		}
	}

	return sum / (2.0 * float(PCF_NUM_SAMPLES));
}

highp float PCF(in sampler2D shadowMap, in highp vec4 coords) 
{
    highp vec2  uv        = coords.xy;
    highp float zReceiver = coords.z; // Assumed to be eye-space z in this code

	poissonDiskSamples(uv);
	return PCF_Filter(shadowMap, uv, zReceiver, 0.002);
}

highp float PCSS(in sampler2D shadowMap, in highp vec4 coords)
{
	highp vec2 uv = coords.xy;
	highp float zReceiver = coords.z; // Assumed to be eye-space z in this code
	// STEP 1: blocker search
    poissonDiskSamples(uv);
	highp float avgBlockerDepth = findBlocker(shadowMap, uv, zReceiver);

	// There are no occluders so early out(this saves filtering)
	if (avgBlockerDepth == -1.0)
	{
		return 1.0;
	}

	// STEP 2: penumbra size
	highp float penumbraRatio = penumbraSize(zReceiver, avgBlockerDepth);
    highp float filterSize    = penumbraRatio * LIGHT_SIZE_UV * NEAR_PLANE / zReceiver;

	// STEP 3: filtering
	// return avgBlockerDepth;
	return PCF_Filter(shadowMap, coords.xy, zReceiver, filterSize);
}

#undef PI

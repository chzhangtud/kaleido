// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool projectSphere(vec3 c, float r, float znear, float P00, float P11, out vec4 aabb)
{
	if (c.z < r + znear)
		return false;

	vec3 cr = c * r;
	float czr2 = c.z * c.z - r * r;

	float vx = sqrt(c.x * c.x + czr2);
	float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
	float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

	float vy = sqrt(c.y * c.y + czr2);
	float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
	float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

	aabb = vec4(minx * P00, miny * P11, maxx * P00, maxy * P11);
	aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f); // clip space -> uv space

	return true;
}

bool coneCull(vec3 center, float radius, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
    return dot(center - camera_position, cone_axis) >= cone_cutoff * length(center - camera_position) * length(cone_axis) + radius;
}

vec3 rotateQuat(vec3 v, vec4 q)
{
	vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

vec3 transformPoint(mat4 m, vec3 p)
{
	return (m * vec4(p, 1.0)).xyz;
}

float maxColumnLength(mat4 m)
{
	return max(max(length(m[0].xyz), length(m[1].xyz)), length(m[2].xyz));
}

// Normal / tangent for mesh shading: inverse-transpose of upper 3x3. When W is singular (e.g. zero scale
// placeholder draw), transpose(inverse(L))) is undefined — fall back to object-space TBN (matches legacy rotateQuat-only path).
void transformWorldTBN(mat4 W, vec3 inNormal, vec3 inTangent, out vec3 outNormal, out vec3 outTangent)
{
	mat3 L = mat3(W);
	float colLen = max(max(length(L[0]), length(L[1])), length(L[2]));
	float det = dot(cross(L[0], L[1]), L[2]);
	if (colLen < 1e-12f || abs(det) < 1e-30f)
	{
		outNormal = normalize(inNormal);
		outTangent = normalize(inTangent);
	}
	else
	{
		mat3 N = transpose(inverse(L));
		outNormal = normalize(N * inNormal);
		outTangent = normalize(N * inTangent);
	}
}

// A Survey of Efficient Representations for Independent Unit Vectors
vec2 encodeOct(vec3 v)
{
	vec2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
	vec2 s = vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
	vec2 r = (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * s) : p;
	return r;
}

vec3 decodeOct(vec2 e)
{
	// https://x.com/Stubbesaurus/status/937994790553227264
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-v.z, 0);
	v.xy += vec2(v.x >= 0 ? -t : t, v.y >= 0 ? -t : t);
	return normalize(v);
}

vec3 tosrgb(vec3 c)
{
	return pow(c.xyz, vec3(1.0 / 2.2));
}

vec4 tosrgb(vec4 c)
{
	return vec4(pow(c.xyz, vec3(1.0 / 2.2)), c.w);
}

vec3 fromsrgb(vec3 c)
{
	return pow(c.xyz, vec3(2.2));
}

vec4 fromsrgb(vec4 c)
{
	return vec4(pow(c.xyz, vec3(2.2)), c.w);
}

// Optimized filmic operator by Jim Hejl and Richard Burgess-Dawson
// http://filmicworlds.com/blog/filmic-tonemapping-operators/
vec3 tonemap(vec3 c)
{
	vec3 x = max(vec3(0), c - 0.004);
	return (x * (6.2 * x + .5)) / (x * (6.2 * x + 1.7) + 0.06);
}

// Gradient noise from Jorge Jimenez's presentation:
// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
float gradientNoise(vec2 uv)
{
	return fract(52.9829189 * fract(dot(uv, vec2(0.06711056, 0.00583715))));
}

void unpackTBN(uint np, uint tp, out vec3 normal, out vec4 tangent)
{
	normal = ((ivec3(np) >> ivec3(0, 10, 20)) & ivec3(1023)) / 511.0 - 1.0;
	tangent.xyz = decodeOct(((ivec2(tp) >> ivec2(0, 8)) & ivec2(255)) / 127.0 - 1.0);
	tangent.w = (np & (1 << 30)) != 0 ? -1.0 : 1.0;
}
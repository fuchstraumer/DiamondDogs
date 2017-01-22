#version 440
// Somehow this makes a plasma-like effect idk
uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;
// Radius of this star
uniform float radius;
// Used to find color in texture map
uniform int temperature;
// Used to shift color to brighter based on temp
uniform vec3 colorShift;
// Color texture, for color-to-temp map
uniform sampler1D blackbody;
// Texture texture, for getting better surface definition.
// Frame counter
uniform int frame;
// Screen resolution
uniform vec2 resolution;
// Camera position
uniform vec3 cameraPos;
// Used to get surface texture.
uniform sampler2D surface;

in vec3 fPos;
in vec3 vNormal;

out vec4 fragColor;

// Noise generation function that also finds gradient.

// Noise generation functions
vec3 mod289(vec3 x) { return x - floor(x * (1.0f / 289.0f)) * 289.0f; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0f / 289.0f)) * 289.0f; }
vec4 permute(vec4 x){return mod(((x*34.0)+1.0)*x, 289.0);}
float permute(float x){return floor(mod(((x*34.0)+1.0)*x, 289.0));}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
float taylorInvSqrt(float r){return 1.79284291400159 - 0.85373472095314 * r;}

vec4 grad4(float j, vec4 ip){
  const vec4 ones = vec4(1.0, 1.0, 1.0, -1.0);
  vec4 p,s;

  p.xyz = floor( fract (vec3(j) * ip.xyz) * 7.0) * ip.z - 1.0;
  p.w = 1.5 - dot(abs(p.xyz), ones.xyz);
  s = vec4(lessThan(p, vec4(0.0)));
  p.xyz = p.xyz + (s.xyz*2.0 - 1.0) * s.www; 

  return p;
}

// Used for corona
float cnoise(vec3 uv, float res){
	const vec3 s = vec3(1e0, 1e2, 1e4);
	
	uv *= res;
	
	vec3 uv0 = floor(mod(uv, res))*s;
	vec3 uv1 = floor(mod(uv+vec3(1.), res))*s;
	
	vec3 f = fract(uv); f = f*f*(3.0-2.0*f);
	
	vec4 v = vec4(uv0.x+uv0.y+uv0.z, uv1.x+uv0.y+uv0.z,
		      	  uv0.x+uv1.y+uv0.z, uv1.x+uv1.y+uv0.z);
	
	vec4 r = fract(sin(v*1e-3)*1e5);
	float r0 = mix(mix(r.x, r.y, f.x), mix(r.z, r.w, f.x), f.y);
	
	r = fract(sin((v + uv1.z - uv0.z)*1e-3)*1e5);
	float r1 = mix(mix(r.x, r.y, f.x), mix(r.z, r.w, f.x), f.y);
	
	return mix(r0, r1, f.z)*2.-1.;
}


float snoise(vec4 v){
  const vec2  C = vec2( 0.138196601125010504,  // (5 - sqrt(5))/20  G4
                        0.309016994374947451); // (sqrt(5) - 1)/4   F4
// First corner
  vec4 i  = floor(v + dot(v, C.yyyy) );
  vec4 x0 = v -   i + dot(i, C.xxxx);

// Other corners

// Rank sorting originally contributed by Bill Licea-Kane, AMD (formerly ATI)
  vec4 i0;

  vec3 isX = step( x0.yzw, x0.xxx );
  vec3 isYZ = step( x0.zww, x0.yyz );
//  i0.x = dot( isX, vec3( 1.0 ) );
  i0.x = isX.x + isX.y + isX.z;
  i0.yzw = 1.0 - isX;

//  i0.y += dot( isYZ.xy, vec2( 1.0 ) );
  i0.y += isYZ.x + isYZ.y;
  i0.zw += 1.0 - isYZ.xy;

  i0.z += isYZ.z;
  i0.w += 1.0 - isYZ.z;

  // i0 now contains the unique values 0,1,2,3 in each channel
  vec4 i3 = clamp( i0, 0.0, 1.0 );
  vec4 i2 = clamp( i0-1.0, 0.0, 1.0 );
  vec4 i1 = clamp( i0-2.0, 0.0, 1.0 );

  //  x0 = x0 - 0.0 + 0.0 * C 
  vec4 x1 = x0 - i1 + 1.0 * C.xxxx;
  vec4 x2 = x0 - i2 + 2.0 * C.xxxx;
  vec4 x3 = x0 - i3 + 3.0 * C.xxxx;
  vec4 x4 = x0 - 1.0 + 4.0 * C.xxxx;

// Permutations
  i = mod(i, 289.0); 
  float j0 = permute( permute( permute( permute(i.w) + i.z) + i.y) + i.x);
  vec4 j1 = permute( permute( permute( permute (
             i.w + vec4(i1.w, i2.w, i3.w, 1.0 ))
           + i.z + vec4(i1.z, i2.z, i3.z, 1.0 ))
           + i.y + vec4(i1.y, i2.y, i3.y, 1.0 ))
           + i.x + vec4(i1.x, i2.x, i3.x, 1.0 ));
// Gradients
// ( 7*7*6 points uniformly over a cube, mapped onto a 4-octahedron.)
// 7*7*6 = 294, which is close to the ring size 17*17 = 289.

  vec4 ip = vec4(1.0/294.0, 1.0/49.0, 1.0/7.0, 0.0) ;

  vec4 p0 = grad4(j0,   ip);
  vec4 p1 = grad4(j1.x, ip);
  vec4 p2 = grad4(j1.y, ip);
  vec4 p3 = grad4(j1.z, ip);
  vec4 p4 = grad4(j1.w, ip);

// Normalise gradients
  vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
  p0 *= norm.x;
  p1 *= norm.y;
  p2 *= norm.z;
  p3 *= norm.w;
  p4 *= taylorInvSqrt(dot(p4,p4));

// Mix contributions from the five corners
  vec3 m0 = max(0.6 - vec3(dot(x0,x0), dot(x1,x1), dot(x2,x2)), 0.0);
  vec2 m1 = max(0.6 - vec2(dot(x3,x3), dot(x4,x4)            ), 0.0);
  m0 = m0 * m0;
  m1 = m1 * m1;
  return 49.0 * ( dot(m0*m0, vec3( dot( p0, x0 ), dot( p1, x1 ), dot( p2, x2 )))
               + dot(m1*m1, vec2( dot( p3, x3 ), dot( p4, x4 ) ) ) ) ;

}

float sdnoise(vec3 v, out vec3 gradient){
  const vec2  C = vec2(1.0/6.0, 1.0/3.0) ;
  const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);

// First corner
  vec3 i  = floor(v + dot(v, C.yyy) );
  vec3 x0 =   v - i + dot(i, C.xxx) ;

// Other corners
  vec3 g = step(x0.yzx, x0.xyz);
  vec3 l = 1.0 - g;
  vec3 i1 = min( g.xyz, l.zxy );
  vec3 i2 = max( g.xyz, l.zxy );

  //   x0 = x0 - 0.0 + 0.0 * C.xxx;
  //   x1 = x0 - i1  + 1.0 * C.xxx;
  //   x2 = x0 - i2  + 2.0 * C.xxx;
  //   x3 = x0 - 1.0 + 3.0 * C.xxx;
  vec3 x1 = x0 - i1 + C.xxx;
  vec3 x2 = x0 - i2 + C.yyy; // 2.0*C.x = 1/3 = C.y
  vec3 x3 = x0 - D.yyy;      // -1.0+3.0*C.x = -0.5 = -D.y

// Permutations
  i = mod289(i); 
  vec4 p = permute( permute( permute( 
             i.z + vec4(0.0, i1.z, i2.z, 1.0 ))
           + i.y + vec4(0.0, i1.y, i2.y, 1.0 )) 
           + i.x + vec4(0.0, i1.x, i2.x, 1.0 ));

// Gradients: 7x7 points over a square, mapped onto an octahedron.
// The ring size 17*17 = 289 is close to a multiple of 49 (49*6 = 294)
  float n_ = 0.142857142857; // 1.0/7.0
  vec3  ns = n_ * D.wyz - D.xzx;

  vec4 j = p - 49.0 * floor(p * ns.z * ns.z);  //  mod(p,7*7)

  vec4 x_ = floor(j * ns.z);
  vec4 y_ = floor(j - 7.0 * x_ );    // mod(j,N)

  vec4 x = x_ *ns.x + ns.yyyy;
  vec4 y = y_ *ns.x + ns.yyyy;
  vec4 h = 1.0 - abs(x) - abs(y);

  vec4 b0 = vec4( x.xy, y.xy );
  vec4 b1 = vec4( x.zw, y.zw );

  //vec4 s0 = vec4(lessThan(b0,0.0))*2.0 - 1.0;
  //vec4 s1 = vec4(lessThan(b1,0.0))*2.0 - 1.0;
  vec4 s0 = floor(b0)*2.0 + 1.0;
  vec4 s1 = floor(b1)*2.0 + 1.0;
  vec4 sh = -step(h, vec4(0.0));

  vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy ;
  vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww ;

  vec3 p0 = vec3(a0.xy,h.x);
  vec3 p1 = vec3(a0.zw,h.y);
  vec3 p2 = vec3(a1.xy,h.z);
  vec3 p3 = vec3(a1.zw,h.w);

//Normalise gradients
  vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
  p0 *= norm.x;
  p1 *= norm.y;
  p2 *= norm.z;
  p3 *= norm.w;

// Mix final noise value
  vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
  vec4 m2 = m * m;
  vec4 m4 = m2 * m2;
  vec4 pdotx = vec4(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3));

// Determine noise gradient
  vec4 temp = m2 * m * pdotx;
  gradient = -8.0 * (temp.x * x0 + temp.y * x1 + temp.z * x2 + temp.w * x3);
  gradient += m4.x * p0 + m4.y * p1 + m4.z * p2 + m4.w * p3;
  gradient *= 42.0;

  return 42.0 * dot(m4, pdotx);
}

// Fractal noise function
 float noise(vec4 position, int octaves, float frequency, float persistence) {
    float total = 0.0; // Total value so far
    float maxAmplitude = 0.0; // Accumulates highest theoretical amplitude
    float amplitude = 1.0;
    for (int i = 0; i < octaves; i++) {
        // Get the noise sample
        total += snoise(position * frequency) * amplitude;
        // Make the wavelength twice as small
        frequency *= 2.0;
        // Add to our maximum possible amplitude
        maxAmplitude += amplitude;
        // Reduce amplitude according to persistence for the next octave
        amplitude *= persistence;
    }
    // Scale the result by the maximum amplitude
    return total / maxAmplitude;
}

float noise(vec3 position, int octaves, float frequency, float persistence, out vec3 gradient){
  float total = 0.0f;
  float maxAmplitude = 0.0f;
  float amplitude = 1.0f;
  for(int i = 0; i < octaves; ++i){
    vec3 gradtemp;
    total += sdnoise(position * frequency, gradtemp) * amplitude;
    gradient += gradtemp;
    frequency *= 2.0f;
    maxAmplitude += amplitude;
    amplitude *= persistence;
  }
  gradient = normalize(gradient);
  return total / maxAmplitude;
}

float noise(vec3 position, int octaves, float frequency, float persistence){
  float total = 0.0f;
  float maxAmplitude = 0.0f;
  float amplitude = 1.0f;
  for(int i = 0; i < octaves; ++i){
    vec3 gradtemp;
    total += sdnoise(position * frequency, gradtemp) * amplitude;
    frequency *= 2.0f;
    maxAmplitude += amplitude;
    amplitude *= persistence;
  }
  return total / maxAmplitude;
}

// random function used to offset noise values in a random direction with each frame

// Main function

void main(){

    float brightness = 0.1f;

    float Radius = (0.70f * radius) + brightness * 0.20f;
    // Set up screenspace stuff for making glow + corona
    float aspect = resolution.x / resolution.y;
    vec2 uv = fPos.xy / resolution.xy;
    vec2 p = -0.50f + uv;
    p.x *= aspect;

    // Exponential falloff of brightness
    float fade = pow(length(2.0f * p), 0.50f);
    float fVal1 = 1.0f - fade;
    float fVal2 = 1.0f - fade;

    float angle = atan(p.x, p.y) / 6.2832f;
    float dist = length(p);
    vec3 coord = vec3(angle, dist, frame * 0.001f);

    float newTime1 = abs(cnoise(coord + vec3(0.0f, -frame * (0.35f + brightness * 0.001f), frame * 0.0015f ), 15.0f));
    float newTime2 = abs(cnoise(coord + vec3(0.0f, -frame * (0.15f + brightness * 0.001f), frame * 0.0015f ), 45.0f));
    // Get values use to make corona.
    for( int i=1; i<=7; i++ ){
      float power = pow( 2.0, float(i + 1) );
      fVal1 += ( 0.5f / power ) * cnoise(coord + vec3(0.0f, -frame, frame * 0.20f),(power * (10.0f) * (newTime1 + 1.0f)));
      fVal2 += ( 0.5f / power ) * cnoise(coord + vec3(0.0f, -frame, frame * 0.20f),(power * (25.0f) * (newTime2 + 1.0f)));
    }

    // Setup corona
    float corona = pow(fVal1 * max(1.10f - fade, 0.0f), 2.0f) * 50.0f;
    corona += pow(fVal2 * max(1.10f - fade, 0.0f), 2.0f) * 50.0f;
    corona *= 1.20f - newTime1;
    vec2 sp = -1.0f + 2.0f * uv;
    sp.x *= aspect;
    sp *= 1.95f - brightness;
    float r = dot(sp,sp);
    float f = (1.0f - sqrt(abs(1.0f-r))) / 1.0f + (brightness * 0.50f);
    // N is first noise sample, u is initial texture pos in 1D texture, n2 is second noise sample
    float n, u, n2;
    vec4 color;
    vec3 grad;
    if(dist <= Radius){
      corona *= pow(dist * (1.0f / Radius), 24.0f);
      // Get surface texture
      vec3 noisePos = fPos + (frame * 0.01f);
      n = (noise(noisePos, 4, 0.25f, 0.10f, grad) * 0.40f);
      float sampleOffset = (n * brightness * 4.50f * frame);
      n2 = (noise(noisePos + sampleOffset, 2, 0.25f, 0.09f) * 0.40f);
      // Get texture coordinate from temperature
      u = (temperature - 800.0f)/29200.0f;
      color = texture(blackbody, u);
      // Intermediate color based on blackbody spectrum, colorshift, and noise value
      color = (vec4(colorShift,1.0f) + color);
    }

    float starGlow = min(max(1.0f - dist * (1.0f - brightness), 0.0f), 1.0f);
    vec4 starGlowColor = texture(blackbody, (temperature / 29200.0f));
    fragColor.rgb = vec3(f * (0.75f + brightness * 0.30f) * color.xyz) + n + (starGlow * starGlowColor.xyz + n2) + corona * color.xyz;
    fragColor.a = 1.0f;
}
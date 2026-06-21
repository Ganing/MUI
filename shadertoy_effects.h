// MPlayer - ShaderToy Effects Library
// Each effect is a complete GLSL fragment shader for Filament material()
// Input uniforms: materialParams.iTime (float), materialParams.iResolution (float2)
// Output: m.baseColor (vec4) with alpha for TRANSPARENT blending
// All user functions prefixed with fx_ to avoid conflicts

#pragma once

namespace FX {

// ============================================================================
// Effect 0: Dynamic Noise Background with Floating Rectangles
// ============================================================================
const char* DynamicRect = R"FILAMENT(
vec3 bgColor = vec3(0.01, 0.16, 0.42);
vec3 rectColor = vec3(0.01, 0.26, 0.57);

const float noiseIntensity = 2.8;
const float noiseDefinition = 0.6;
const vec2 glowPos = vec2(-2., 0.);

const float total = 60.;
const float minSize = 0.03;
const float maxSize = 0.08;
const float yDistribution = 0.5;

float fx_random(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

float fx_noise(in vec2 p) {
    p *= noiseIntensity;
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(fx_random(i + vec2(0.0,0.0)),
                   fx_random(i + vec2(1.0,0.0)), u.x),
               mix(fx_random(i + vec2(0.0,1.0)),
                   fx_random(i + vec2(1.0,1.0)), u.x), u.y);
}

float fx_fbm(in vec2 uv) {
    uv *= 5.0;
    mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
    float f = 0.5000 * fx_noise(uv); uv = m * uv;
    f += 0.2500 * fx_noise(uv); uv = m * uv;
    f += 0.1250 * fx_noise(uv); uv = m * uv;
    f += 0.0625 * fx_noise(uv); uv = m * uv;
    f = 0.5 + 0.5 * f;
    return f;
}

vec3 fx_bg(vec2 uv) {
    float velocity = materialParams.iTime / 1.6;
    float intensity = sin(uv.x * 3. + velocity * 2.) * 1.1 + 1.5;
    uv.y -= 2.;
    vec2 bp = uv + glowPos;
    uv *= noiseDefinition;

    float rb = fx_fbm(vec2(uv.x * .5 - velocity * .03, uv.y)) * .1;
    uv += rb;

    float rz = fx_fbm(uv * .9 + vec2(velocity * .35, 0.0));
    rz *= dot(bp * intensity, bp) + 1.2;

    vec3 col = bgColor / (.1 - rz);
    return sqrt(abs(col));
}

float fx_rectangle(vec2 uv, vec2 pos, float width, float height, float blur) {
    pos = (vec2(width, height) + .01) / 2. - abs(uv - pos);
    pos = smoothstep(0., blur, pos);
    return pos.x * pos.y;
}

mat2 fx_rotate2d(float ang) {
    return mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
}

void material(inout MaterialInputs m) {
    prepareMaterial(m);
    vec2 uv = gl_FragCoord.xy / materialParams.iResolution * 2. - 1.;
    uv.x *= materialParams.iResolution.x / materialParams.iResolution.y;

    float t = materialParams.iTime;

    // bg
    vec3 color = fx_bg(uv) * (2. - abs(uv.y * 2.));

    // rectangles
    float velX = -t / 8.;
    float velY = t / 10.;
    for (float i = 0.; i < total; i++) {
        float index = i / total;
        float rnd = fx_random(vec2(index));
        vec3 pos = vec3(0.);
        pos.x = fract(velX * rnd + index) * 4. - 2.0;
        pos.y = sin(index * rnd * 1000. + velY) * yDistribution;
        pos.z = maxSize * rnd + minSize;
        vec2 uvRot = uv - pos.xy + pos.z / 2.;
        uvRot = fx_rotate2d(i + t / 2.) * uvRot;
        uvRot += pos.xy + pos.z / 2.;
        float rect = fx_rectangle(uvRot, pos.xy, pos.z, pos.z, (maxSize + minSize - pos.z) / 2.);
        color += rectColor * rect * pos.z / maxSize;
    }

    float fxAlpha = clamp(dot(color, vec3(0.299, 0.587, 0.114)) * 3.0, 0.0, 0.85);
    m.baseColor = vec4(color, fxAlpha);
}
)FILAMENT";

// ============================================================================
// Effect 1: Van Gogh Sunset - Painted landscape (by Noztol)
// ============================================================================
const char* VanGoghSunset = R"FILAMENT(

#define fx_p(t, a, b, c, d) ( a + b*cos( 6.28318*(c*t+d) ) )
#define fx_sp(t) fx_p(t,vec3(.26,.76,.77),vec3(1,.3,1),vec3(.8,.4,.7),vec3(0,.12,.54))
#define fx_hue(v) ( .6 + .76 * cos(6.3*(v) + vec4(0,23,21,0) ) )
#define FX_TWO_PI 6.28318530718

#define MountainLayerThreecol vec3(26., 65., 74.)/255.
#define MountainLayerFourCol vec3(14., 49., 55.)/255.
#define FieldMid              vec3(94., 121., 62.)/255.

float fx_hash1(float p) {
    p = fract(p * .1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float fx_noise1d(float x) {
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(fx_hash1(i), fx_hash1(i + 1.0), f);
}

float fx_hash12(vec2 p) {
    vec3 p3  = fract(vec3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 fx_hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xx+p3.yz)*p3.zy);
}

vec2 fx_rot2D(vec2 st, float a){
    return mat2(cos(a),-sin(a),sin(a),cos(a))*st;
}
mat2 fx_rot2Dm(float a){
    return mat2(cos(a),-sin(a),sin(a),cos(a));
}

float fx_st(float a, float b, float s) { return smoothstep(a-s, a+s, b); }

float fx_noise2d(in vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f*f*(3.-2.*f);
    return mix(mix(dot(fx_hash22(i+vec2(0,0)), f-vec2(0,0)),
                   dot(fx_hash22(i+vec2(1,0)), f-vec2(1,0)), u.x),
               mix(dot(fx_hash22(i+vec2(0,1)), f-vec2(0,1)),
                   dot(fx_hash22(i+vec2(1,1)), f-vec2(1,1)), u.x), u.y);
}

float fx_snoise(vec2 p) { return fx_noise2d(p)*0.5 + 0.5; }

vec3 fx_paintedSkyMountains(vec2 g, vec2 r, float time) {
    vec2 uv = (g+g-r)/r.y;
    vec2 sun_pos = vec2(r.x/r.y * 0.35, -0.15);
    vec2 sh = fx_rot2D(sun_pos, fx_noise2d(uv+time*.25)*.3);

    vec3 f = vec3(0);
    float sm = 3./r.y;
    vec2 u, id, lc, t;
    float xd, yd, h;

    u = uv + sh;
    yd = 60.;
    id = vec2((length(u)+.01)*yd,0);
    xd = floor(id.x)*.09;
    h = (fx_hash12(floor(id.xx))*.5+.25)*(time+10.)*.25;
    t = fx_rot2D(u,h);
    id.y = atan(t.y,t.x)*xd;
    lc = fract(id);
    id -= lc;

    t = vec2(cos((id.y+.5)/xd)*(id.x+.5)/yd,sin((id.y+.5)/xd)*(id.x+.5)/yd);
    t = fx_rot2D(t,-h) - sh;

    h = fx_noise2d(t*vec2(.5,1)-vec2(time*.2,0)) * step(-.25,t.y);
    h = smoothstep(.052,.055, h);
    lc += (fx_noise2d(lc*vec2(1,4)+id))*vec2(.7,.2);

    f = mix(fx_sp(sin(length(u)-.1))*.35,
           mix(fx_sp(sin(length(u)-.1)+(fx_hash12(id)-.5)*.15),vec3(1),h),
           fx_st(abs(lc.x-.5),.4,sm*yd)*fx_st(abs(lc.y-.5),.48,sm*xd));

    float cld = fx_noise2d(-sh*vec2(.5,1) - vec2(time*.2,0));
    cld = 1.- smoothstep(.0,.15,cld)*.5;
    u = (uv - vec2(0.0, 0.25)) * vec2(1, 15);
    id = floor(u);

    for (float i = 1.; i > -1.; i--) {
        if (id.y+i < 0.0) {
            lc = fract(u)-.5;
            lc.y = (lc.y+(sin(uv.x*8.-time*0.8+id.y+i))*.3-i)*4.;
            h = fx_hash12(vec2(id.y+i,floor(lc.y)));
            xd = 6.+h*4.;
            yd = 30.;
            lc.x = uv.x*xd+sh.x*9.;
            lc.x += sin(time * (.2 + h*1.5))*.5;
            h = .8*smoothstep(5.,.0,abs(floor(lc.x)))*cld+.1;

            f = mix(f,mix(MountainLayerFourCol,MountainLayerThreecol,h),fx_st(lc.y,0.,sm*yd));
            lc += fx_noise2d(lc*vec2(3,.5))*vec2(.1,.6);

            vec3 strokeCol = fx_hue(fx_hash12(floor(lc))*.1+.35).rgb*(1.2+floor(lc.y)*.17);
            f = mix(f, mix(strokeCol,FieldMid,h), fx_st(lc.y,0.,sm*xd)*fx_st(abs(fract(lc.x)-.5),.48,sm*xd)*fx_st(abs(fract(lc.y)-.5),.3,sm*yd));
        }
    }
    return f;
}

vec3 fx_paintedWaterSand(vec2 g, vec2 r, float time, vec3 bgCol) {
    vec2 uv = (g+g-r)/r.y;
    vec2 sun_pos = vec2(r.x/r.y * 0.35, -0.15);
    vec2 sh = fx_rot2D(sun_pos, fx_noise2d(uv+time*.25)*.3);
    float sm = 3./r.y;
    vec3 f = bgCol;

    float waterZone = smoothstep(0.16, 0.14, uv.y);
    f = mix(f, vec3(0.05, 0.15, 0.3), waterZone);

    float cld = fx_noise2d(-sh*vec2(.5,1) - vec2(time*.2,0));
    cld = 1.- smoothstep(.0,.15,cld)*.5;

    vec2 u = (uv - vec2(0.0, 0.15)) * vec2(1,15);
    vec2 id = floor(u);

    for (float i = 1.; i > -1.; i--) {
        if (id.y+i < 0.0) {
            vec2 lc = fract(u)-.5;
            lc.y = (lc.y+(sin(uv.x*12.-time*3.+id.y+i))*.25-i)*4.;
            float h = fx_hash12(vec2(id.y+i,floor(lc.y)));
            float xd = 6.+h*4.;
            float yd = 30.;
            lc.x = uv.x*xd+sh.x*9.;
            lc.x += sin(time * (.5 + h*2.))*.5;
            h = .8*smoothstep(5.,.0,abs(floor(lc.x)))*cld+.1;

            vec3 waterBase = mix(vec3(0.0, 0.15, 0.4), vec3(0.6, 0.4, 0.1), h);
            f = mix(f, waterBase, fx_st(lc.y, 0., sm*yd));
            lc += fx_noise2d(lc*vec2(3,.5))*vec2(.1,.6);

            vec3 strokeCol = mix(fx_hue(fx_hash12(floor(lc))*.1+.56).rgb*(1.2+floor(lc.y)*.17), vec3(1.0, 0.8, 0.2), h);
            f = mix(f, strokeCol, fx_st(lc.y,0.,sm*xd)*fx_st(abs(fract(lc.x)-.5),.48,sm*xd)*fx_st(abs(fract(lc.y)-.5),.3,sm*yd));
        }
    }

    vec2 u_sand = uv + fx_noise2d(uv*2.)*.1 + vec2(0, sin(uv.x*1.2+3.)*.2 + 0.7);
    vec3 sandCol = vec3(0.85, 0.75, 0.50);
    vec3 sandDark = vec3(0.60, 0.45, 0.25);
    f = mix(f, sandDark * 0.7, step(u_sand.y, .0));

    float xd_s = 60.;
    u_sand = u_sand * vec2(xd_s, xd_s/3.5);

    if (u_sand.y < 1.2) {
        for (float y = 0.; y > -3.; y--) {
            for (float x = -2.; x < 3.; x++) {
                vec2 id_s = floor(u_sand) + vec2(x,y);
                vec2 lc_s = (fract(u_sand) + vec2(1.-x,-y))/vec2(5,3);
                float h_s = (fx_hash12(id_s)-.5)*.25+.5;

                lc_s -= vec2(.3, .5-h_s*.4);
                lc_s.x += sin(((time*0.5+h_s*2.-id_s.x*.05-id_s.y*.05)*1.1+id_s.y*.5)*2.)*(lc_s.y+.5)*.2;
                vec2 t_s = abs(lc_s)-vec2(.03, .5-h_s*.5);
                float l = length(max(t_s,0.)) + min(max(t_s.x,t_s.y),0.);

                l -= fx_noise2d(lc_s*7.+id_s)*.1;

                vec4 C = vec4(sandDark * 0.5, fx_st(l, .1, sm*xd_s*.09));
                vec4 fg = vec4(sandCol * (1.1+lc_s.y*1.2) * (1.2-h_s*1.2), 1.);
                C = mix(C, fg, fx_st(l, .04, sm*xd_s*.09));

                f = mix(f, C.rgb, C.a * step(id_s.y, -1.));
            }
        }
    }
    return f;
}

float fx_sdTri(vec2 p, float base, float tip, float width) {
    float dY = max(base - p.y, p.y - tip);
    float frac = clamp((p.y - base) / (tip - base), 0.0, 1.0);
    float curWidth = width * (1.0 - frac);
    float dX = abs(p.x) - curWidth;
    return max(dX, dY);
}

float fx_sdTree(vec2 p, vec2 pos, float scale, float ftime) {
    vec2 q = (p - pos) / scale;
    q.x -= sin(ftime * 1.5 + pos.x * 10.0) * 0.02 * max(0.0, q.y);
    float d = max(abs(q.x) - 0.03, max(-q.y, q.y - 0.2));
    d = min(d, fx_sdTri(q, 0.1, 0.45, 0.25));
    d = min(d, fx_sdTri(q, 0.25, 0.65, 0.2));
    d = min(d, fx_sdTri(q, 0.45, 0.85, 0.15));
    d = min(d, fx_sdTri(q, 0.65, 1.0, 0.1));
    return d * scale;
}

float fx_getLand(vec2 uv, float aspect) {
    float d = 1.0;
    float w1 = 0.05 * exp(-12.0 * pow(uv.x - aspect*0.1, 2.0));
    float c1 = 0.36 - 0.01 * uv.x;
    float land1 = abs(uv.y - c1) - w1 + smoothstep(0.008, 0.0, w1);
    d = min(d, land1);
    float w2 = 0.03 * exp(-30.0 * pow(uv.x - aspect*0.52, 2.0));
    float c2 = 0.48;
    float land2 = abs(uv.y - c2) - w2 + smoothstep(0.008, 0.0, w2);
    d = min(d, land2);
    return d;
}

float fx_getTrees(vec2 uv, float aspect, float ftime) {
    float d = 1.0;
    d = min(d, fx_sdTree(uv, vec2(aspect*0.05, 0.41), 0.18, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.11, 0.40), 0.22, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.17, 0.38), 0.14, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.23, 0.36), 0.09, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.48, 0.508), 0.07, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.51, 0.51), 0.10, ftime));
    d = min(d, fx_sdTree(uv, vec2(aspect*0.54, 0.50), 0.06, ftime));
    return d;
}

vec4 fx_paintedTrees(vec2 uv, vec2 r, float time, float aspect) {
    float sm = 3.0 / r.y;
    float treeDist = fx_getTrees(uv, aspect, time);
    float treeMask = smoothstep(0.02, -0.01, treeDist);
    float coreMask = smoothstep(0.003, 0.0, treeDist);
    if (treeMask <= 0.0) return vec4(0.0);
    vec3 baseCol = vec3(0.05, 0.1, 0.08);
    vec4 C = vec4(baseCol, coreMask);
    float xd = 160.0;
    float yd = 45.0;
    for (float i = 0.0; i < 3.0; i++) {
        vec2 u = uv;
        u.x -= sin(time * 1.5 + uv.x * 10.0) * 0.02 * max(0.0, uv.y - 0.4);
        u += fx_noise2d(u * 12.0 + vec2(time * 0.3, i * 5.0)) * 0.025;
        vec2 t = u * vec2(xd, yd) + vec2(i * 11.3, i * 17.7);
        vec2 id = floor(t);
        vec2 lc = fract(t);
        float h = fx_hash12(id);
        if (h < 0.4) continue;
        float taper = mix(1.2, 0.2, lc.y);
        float outline = fx_st(abs(lc.x - 0.5), 0.35 * taper, sm * xd) * fx_st(abs(lc.y - 0.5), 0.48, sm * yd);
        float body = fx_st(abs(lc.x - 0.5), 0.22 * taper, sm * xd) * fx_st(abs(lc.y - 0.5), 0.4, sm * yd);
        vec3 strokeCol = mix(vec3(0.05, 0.25, 0.1), vec3(0.15, 0.4, 0.15), h);
        if (h > 0.8) strokeCol = mix(strokeCol, vec3(0.3, 0.5, 0.15), 0.6);
        if (h < 0.55) strokeCol = mix(strokeCol, vec3(0.0, 0.15, 0.2), 0.7);
        float strokeAlpha = outline * treeMask;
        C.rgb = mix(C.rgb, vec3(0.02, 0.04, 0.03), strokeAlpha);
        C.rgb = mix(C.rgb, strokeCol, body * treeMask);
        C.a = max(C.a, strokeAlpha);
    }
    return C;
}

vec4 fx_islandGrass(vec2 uv, vec2 r, float time, float aspect) {
    float sm = 3.0 / r.y;
    vec4 O = vec4(0.0);
    vec2 u = uv + fx_noise2d(uv*2.0)*0.05;
    float xd = 160.0;
    u = u * vec2(xd, xd/2.0);
    vec3 f_col = mix(vec3(0.3, 0.65, 0.2), vec3(0.5, 0.8, 0.2), sin(time*0.5)*0.5 + 0.5);
    vec3 cRock = vec3(0.08, 0.1, 0.12);
    for (float y = 0.0; y > -4.0; y--) {
        for (float x = -2.0; x < 3.0; x++) {
            vec2 id = floor(u) + vec2(x,y);
            vec2 root_uv = id / vec2(xd, xd/2.0);
            float w1 = 0.05 * exp(-12.0 * pow(root_uv.x - aspect*0.1, 2.0));
            float c1 = 0.36 - 0.01 * root_uv.x;
            float land1 = abs(root_uv.y - c1) - w1 + smoothstep(0.008, 0.0, w1);
            float w2 = 0.03 * exp(-30.0 * pow(root_uv.x - aspect*0.52, 2.0));
            float c2 = 0.48;
            float land2 = abs(root_uv.y - c2) - w2 + smoothstep(0.008, 0.0, w2);
            float landDist = min(land1, land2);
            float rootAlpha = smoothstep(0.01, -0.01, landDist);
            if (rootAlpha <= 0.0) continue;
            vec2 lc = (fract(u) + vec2(1.0-x, -y)) / vec2(5.0, 3.0);
            float h = (fx_hash12(id) - 0.5) * 0.25 + 0.5;
            lc -= vec2(0.3, 0.5 - h*0.4);
            lc.x += sin(((time*1.5 + h*2.0 - id.x*0.05 - id.y*0.05)*1.1 + id.y*0.5)*2.0) * (lc.y + 0.5) * 0.25;
            vec2 t = abs(lc) - vec2(0.02, 0.5 - h*0.5);
            float l = length(max(t, 0.0)) + min(max(t.x, t.y), 0.0);
            l -= fx_noise2d(lc*7.0 + id) * 0.1;
            vec4 C = vec4(cRock, fx_st(l, 0.1, sm*xd*0.09));
            vec4 fg = vec4(f_col * (1.2 + lc.y*2.0) * (1.8 - h*2.5), 1.0);
            C = mix(C, fg, fx_st(l, 0.04, sm*xd*0.09));
            C.a *= rootAlpha;
            O = mix(O, C, C.a);
        }
    }
    return O;
}

void material(inout MaterialInputs m) {
    prepareMaterial(m);
    vec2 uv = gl_FragCoord.xy / materialParams.iResolution;
    float aspect = materialParams.iResolution.x / materialParams.iResolution.y;
    uv.x *= aspect;
    float time = materialParams.iTime;

    // Sky & Mountains
    vec3 col = fx_paintedSkyMountains(gl_FragCoord.xy, materialParams.iResolution, time);
    // Water & Sand
    col = fx_paintedWaterSand(gl_FragCoord.xy, materialParams.iResolution, time, col);
    // Land base
    float landDist = fx_getLand(uv, aspect);
    float landMask = smoothstep(0.003, 0.0, landDist);
    vec3 cRock = vec3(0.08, 0.1, 0.12);
    col = mix(col, cRock, landMask);
    // Painted Trees
    vec4 trees = fx_paintedTrees(uv, materialParams.iResolution, time, aspect);
    col = mix(col, trees.rgb, trees.a);
    // Island Grass
    vec4 grass = fx_islandGrass(uv, materialParams.iResolution, time, aspect);
    col = mix(col, grass.rgb, grass.a);

    float fxAlpha = clamp(dot(col, vec3(0.299, 0.587, 0.114)) * 1.8 + 0.15, 0.0, 0.85);
    m.baseColor = vec4(col, fxAlpha);
}
)FILAMENT";

// ============================================================================
// Effect 2: Heartfelt Rain - Glass refraction + raindrops (by BigWIngs)
//   Faithful adaptation from ShaderToy. Changes from original:
//     mainImage→material(), fragCoord→gl_FragCoord, fragColor→m.baseColor
//     iTime→materialParams.iTime, iResolution→materialParams.iResolution
//     iChannel0→materialParams_sceneTex, iMouse→vec4(0)
//     textureLod→texture (Filament limitation), user functions prefixed fx_
// ============================================================================
// TEMP DEBUG: step 3 - add HAS_HEART
const char* HeartfeltRain = R"FILAMENT(
#define S(a, b, t) smoothstep(a, b, t)
#define HAS_HEART
#define USE_POST_PROCESSING

vec3 fx_N13(float p) {
   vec3 p3 = fract(vec3(p) * vec3(.1031,.11369,.13787));
   p3 += dot(p3, p3.yzx + 19.19);
   return fract(vec3((p3.x + p3.y)*p3.z, (p3.x+p3.z)*p3.y, (p3.y+p3.z)*p3.x));
}

float fx_N(float t) {
    return fract(sin(t*12345.564)*7658.76);
}

float fx_Saw(float b, float t) {
    return S(0., b, t)*S(1., b, t);
}

vec2 fx_DropLayer2(vec2 uv, float t) {
    vec2 UV = uv;
    uv.y += t*0.75;
    vec2 a = vec2(6., 1.);
    vec2 grid = a*2.;
    vec2 id = floor(uv*grid);

    float colShift = fx_N(id.x);
    uv.y += colShift;

    id = floor(uv*grid);
    vec3 n = fx_N13(id.x*35.2+id.y*2376.1);
    vec2 st = fract(uv*grid)-vec2(.5, 0);

    float x = n.x-.5;
    float y = UV.y*20.;
    float wiggle = sin(y+sin(y));
    x += wiggle*(.5-abs(x))*(n.z-.5);
    x *= .7;
    float ti = fract(t+n.z);
    y = (fx_Saw(.85, ti)-.5)*.9+.5;
    vec2 p = vec2(x, y);

    float d = length((st-p)*a.yx);
    float mainDrop = S(.4, .0, d);

    float r = sqrt(S(1., y, st.y));
    float cd = abs(st.x-x);
    float trail = S(.23*r, .15*r*r, cd);
    float trailFront = S(-.02, .02, st.y-y);
    trail *= trailFront*r*r;

    y = UV.y;
    float trail2 = S(.2*r, .0, cd);
    float droplets = max(0., (sin(y*(1.-y)*120.)-st.y))*trail2*trailFront*n.z;
    y = fract(y*10.)+(st.y-.5);
    float dd = length(st-vec2(x, y));
    droplets = S(.3, 0., dd);
    float m = mainDrop+droplets*r*trailFront;

    return vec2(m, trail);
}

float fx_StaticDrops(vec2 uv, float t) {
    uv *= 40.;
    vec2 id = floor(uv);
    uv = fract(uv)-.5;
    vec3 n = fx_N13(id.x*107.45+id.y*3543.654);
    vec2 p = (n.xy-.5)*.7;
    float d = length(uv-p);
    float fade = fx_Saw(.025, fract(t+n.z));
    float c = S(.3, 0., d)*fract(n.z*10.)*fade;
    return c;
}

vec2 fx_Drops(vec2 uv, float t, float l0, float l1, float l2) {
    float s = fx_StaticDrops(uv, t)*l0;
    vec2 m1 = fx_DropLayer2(uv, t)*l1;
    vec2 m2 = fx_DropLayer2(uv*1.85, t)*l2;
    float c = s+m1.x+m2.x;
    c = S(.3, 1., c);
    return vec2(c, max(m1.y*l0, m2.y*l1));
}

void material(inout MaterialInputs m) {
    prepareMaterial(m);
    vec2 uv = (gl_FragCoord.xy-.5*materialParams.iResolution) / materialParams.iResolution.y;
    vec2 UV = gl_FragCoord.xy/materialParams.iResolution;
    vec2 lrcUV = UV;  // save original screen UV for lyrics (before zoom)
    // iMouse from FLOAT2 uniforms: xy=pos, zw=click
    float mx = materialParams.iMouseXY.x / materialParams.iResolution.x;
    float my = materialParams.iMouseXY.y / materialParams.iResolution.y;
    float mz = materialParams.iMouseZW.x; // >0 = button held, <0 = released
    float T = materialParams.iTime + mx*2.;

    #ifdef HAS_HEART
    T = mod(materialParams.iTime, 102.);
    T = mix(T, mx*102., mz>0.?1.:0.);
    #endif

    float t = T*.2;
    float rainAmount = mz>0. ? my : sin(T*.05)*.3+.7;

    float maxBlur = mix(3., 6., rainAmount);
    float minBlur = 2.;

    #ifdef HAS_HEART
    float story = S(0., 70., T);
    t = min(1., T/70.);
    t = 1.-t;
    t = (1.-t*t)*70.;
    float zoom = mix(.3, 1.2, story);
    uv *= zoom;
    minBlur = 4.+S(.5, 1., story)*3.;
    maxBlur = 6.+S(.5, 1., story)*1.5;
    vec2 hv = uv-vec2(.0, -.1);
    hv.x *= .5;
    float s = S(110., 70., T);
    hv.y -= sqrt(abs(hv.x))*.5*s;
    float heart = length(hv);
    heart = S(.4*s, .2*s, heart)*s;
    rainAmount = heart;
    maxBlur -= heart;
    uv *= 1.5;
    t *= .25;
    #else
    float zoom = -cos(T*.2);
    uv *= .7+zoom*.3;
    #endif
    UV = (UV-.5)*(.9+zoom*.1)+.5;

    // Aspect-ratio correction: keep background at native video ratio
    //   with black bars (letterbox/pillarbox) instead of stretching
    float texAspect = materialParams.texSize.x / materialParams.texSize.y;
    float scrAspect = materialParams.iResolution.x / materialParams.iResolution.y;
    vec2 bgUV = UV;
    float inBounds = 1.0;
    if (scrAspect > texAspect) {
        float w = texAspect / scrAspect;
        float l = (1.0 - w) * 0.5;
        bgUV.x = (UV.x - l) / w;
        inBounds = step(l, UV.x) * step(UV.x, 1.0 - l);
    } else {
        float h = scrAspect / texAspect;
        float t = (1.0 - h) * 0.5;
        bgUV.y = (UV.y - t) / h;
        inBounds = step(t, UV.y) * step(UV.y, 1.0 - t);
    }

    float staticDrops = S(-.5, 1., rainAmount)*2.;
    float layer1 = S(.25, .75, rainAmount);
    float layer2 = S(.0, .5, rainAmount);

    vec2 c = fx_Drops(uv, t, staticDrops, layer1, layer2);
    vec2 e = vec2(.001, 0.);
    float cx = fx_Drops(uv+e, t, staticDrops, layer1, layer2).x;
    float cy = fx_Drops(uv+e.yx, t, staticDrops, layer1, layer2).x;
    vec2 n = vec2(cx-c.x, cy-c.x);

    #ifdef HAS_HEART
    n *= 1.-S(60., 85., T);
    c.y *= 1.-S(80., 100., T)*.8;
    #endif

    // NOTE: original used textureLod(iChannel0, UV+n, focus).
    //   Filament may not support textureLod; we use texture() and compensate
    //   focus via UV scaling. Y-flipped because video texture v=0 is top row.
    float focus = mix(maxBlur-c.y, minBlur, S(.1, .2, c.x));
    vec2 sampleUV = vec2(bgUV.x, 1.0-bgUV.y) + vec2(n.x,-n.y)*(1.0/(1.0+focus*4.0));
    // lyrics texture: no Y-flip needed (glReadPixels is already GL-aligned)
    vec2 lrcSampleUV = vec2(lrcUV.x, lrcUV.y) + vec2(n.x, n.y)*(1.0/(1.0+focus*4.0));
    // Sample both at rain-distorted UV — rain covers everything
    vec3 col = inBounds > 0.5 ? texture(materialParams_sceneTex, sampleUV).rgb : vec3(0.0);
    vec4 lrc = texture(materialParams_lyricsTex, lrcSampleUV);
    col = mix(col, lrc.rgb, lrc.a);

    #ifdef USE_POST_PROCESSING
    t = (T+3.)*.5;
    float colFade = sin(t*.2)*.5+.5+story;
    col *= mix(vec3(1.), vec3(.8, .9, 1.3), colFade);
    float fade = S(0., 10., T);
    float lightning = sin(t*sin(t*10.));
    lightning *= pow(max(0., sin(t+sin(t))), 10.);
    col *= 1.+lightning*fade*mix(1., .1, story*story);
    col *= 1.-dot(UV-=.5, UV);

    #ifdef HAS_HEART
    col = mix(pow(col, vec3(1.2)), col, heart);
    fade *= S(102., 97., T);
    #endif

    col *= fade;
    #endif

    m.baseColor = vec4(col, 1.0);
}
)FILAMENT";

} // namespace FX

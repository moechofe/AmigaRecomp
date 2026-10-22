// PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER
//
//   by Timothy Lottes
//
// This is more along the style of a really good CGA arcade monitor.
// With RGB inputs instead of NTSC.
// The shadow mask example has the mask rotated 90 degrees for less chromatic aberration.
//
// It is an example what I personally would want as a display option for pixel art games.
// Please take and use, change, or whatever.
//
// AmigaRecomp's copy of libretro glsl-shaders e80b60a crt/shaders/crt-lottes.glsl, reworked for
// speed (docs/runtime.md): the fragment stage alone, after the frontend's vertex shader. Its
// parameters are fixed: libretro's defaults, but shadowMask 4 (the VGA style mask, the only one
// Mask draws) and brightBoost 1.3; shape 2 is the square in Gaus, and scaleInLinearGamma 1 and
// bloom are always on. Each of the 31 texels the filters share is fetched and made linear once.

#define hardScan -8.0
#define hardPix -3.0
#define warpX 0.031
#define warpY 0.041
#define maskDark 0.5
#define maskLight 1.5
#define brightBoost 1.3
#define hardBloomPix -1.5
#define hardBloomScan -2.0
#define bloomAmount 0.15

in vec2 uv;
uniform sampler2D Source;
uniform vec2 SourceSize;
out vec4 FragColor;

// sRGB to Linear.
float ToLinear1(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

vec3 ToLinear(vec3 c)
{
    return vec3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b));
}

// Linear to sRGB.
float ToSrgb1(float c)
{
    return c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055;
}

vec3 ToSrgb(vec3 c)
{
    return vec3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
}

// 1D Gaussian.
float Gaus(float pos, float scale)
{
    return exp2(scale * pos * pos);
}

// Gaussian filters along a line of seven texels, t[3] the nearest: 3 and 5 taps weighted by
// w[0..4] for offsets -2..2, 7 taps by w[0..6] for -3..3.
vec3 Horz3(vec3 t[7], float w[5])
{
    return (t[2] * w[1] + t[3] * w[2] + t[4] * w[3]) / (w[1] + w[2] + w[3]);
}

vec3 Horz5(vec3 t[7], float w[5])
{
    return (t[1] * w[0] + t[2] * w[1] + t[3] * w[2] + t[4] * w[3] + t[5] * w[4]) / (w[0] + w[1] + w[2] + w[3] + w[4]);
}

vec3 Horz7(vec3 t[7], float w[7])
{
    return (t[0] * w[0] + t[1] * w[1] + t[2] * w[2] + t[3] * w[3] + t[4] * w[4] + t[5] * w[5] + t[6] * w[6]) / (w[0] + w[1] + w[2] + w[3] + w[4] + w[5] + w[6]);
}

// Distortion of scanlines, and end of screen alpha.
vec2 Warp(vec2 pos)
{
    pos = pos * 2.0 - 1.0;
    pos *= vec2(1.0 + (pos.y * pos.y) * warpX, 1.0 + (pos.x * pos.x) * warpY);
    return pos * 0.5 + 0.5;
}

// VGA style shadow mask.
vec3 Mask(vec2 pos)
{
    vec3 mask = vec3(maskDark, maskDark, maskDark);
    pos.xy = floor(pos.xy * vec2(1.0, 0.5));
    pos.x += pos.y * 3.0;
    pos.x = fract(pos.x * 0.166666666);
    if      (pos.x < 0.333) mask.r = maskLight;
    else if (pos.x < 0.666) mask.g = maskLight;
    else                    mask.b = maskLight;
    return mask;
}

// The texel x, y from the nearest, linear; past the edges black (the texture's border).
#define TAP(x, y) ToLinear(brightBoost * textureOffset(Source, centre, ivec2(x, y)).rgb)
#define LINE(y) vec3[7](TAP(-3, y), TAP(-2, y), TAP(-1, y), TAP(0, y), TAP(1, y), TAP(2, y), TAP(3, y))

void main()
{
    vec2 pos = Warp(uv);
    vec2 texel = pos * SourceSize;
    vec2 cell = floor(texel);
    vec2 centre = (cell + 0.5) / SourceSize;
    // Distance in emulated pixels to nearest texel.
    vec2 dst = -((texel - cell) - vec2(0.5));

    float px[5], bx[7], sy[3], by[5];
    for(int i = 0; i < 7; i++)
        bx[i] = Gaus(dst.x + float(i - 3), hardBloomPix);
    for(int i = 0; i < 5; i++)
    {
        px[i] = Gaus(dst.x + float(i - 2), hardPix);
        by[i] = Gaus(dst.y + float(i - 2), hardBloomScan);
    }
    for(int i = 0; i < 3; i++)
        sy[i] = Gaus(dst.y + float(i - 1), hardScan);

    // Lines -2 and 2 feed only Horz5, so their outer two texels are never read.
    vec3 l0[7] = LINE(-2);
    vec3 l1[7] = LINE(-1);
    vec3 l2[7] = LINE(0);
    vec3 l3[7] = LINE(1);
    vec3 l4[7] = LINE(2);

    // The nearest three lines, then a small bloom over five.
    vec3 outColor = Horz3(l1, px) * sy[0] + Horz5(l2, px) * sy[1] + Horz3(l3, px) * sy[2];
    outColor += (Horz5(l0, px) * by[0] + Horz7(l1, bx) * by[1] + Horz7(l2, bx) * by[2] + Horz7(l3, bx) * by[3] + Horz5(l4, px) * by[4]) * bloomAmount;
    outColor *= Mask(gl_FragCoord.xy * 1.000001);
    FragColor = vec4(ToSrgb(outColor), 1.0);
}

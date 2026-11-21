// LiquidGlassPS.hlsl — Apple-style Optical Liquid Glass Pixel Shader (Shader Model 4.0)
// Implements curved meniscus lens refraction, subtle RGB chromatic dispersion along
// the curved rim, high-vibrancy crystal transmission, and dual specular catchlights.

Texture2D InputTexture : register(t0);
SamplerState InputSampler : register(s0);

cbuffer LiquidGlassConstants : register(b0)
{
    float2 g_Resolution;      // Window dimensions in DIPs (e.g., 440x260 or 140x4)
    float  g_CornerRadius;    // Rounded bottom radius (2.0 collapsed, 26.0 expanded)
    float  g_ExpandProgress;  // 0.0 = Slim Line, 1.0 = Expanded Hub
    float4 g_TintColor;       // Optical tint + clarity alpha
    float  g_Saturation;      // 1.50 - 1.85 Apple vibrancy boost
    float  g_RefractionPx;    // Edge lens refraction strength in pixels
    float  g_BackdropLuma;    // Sampled top-edge desktop luminance [0..1]
    float  g_Padding;
};

float sdTopPill(float2 p, float2 halfSize, float radius)
{
    float r = (p.y < 0.0) ? 2.0 : radius;
    float2 d = abs(p) - halfSize + r;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

float4 main(
    float4 pos : SV_POSITION,
    float4 posScene : SCENE_POSITION,
    float4 uv0 : TEXCOORD0
) : SV_TARGET
{
    float2 pixelPos = uv0.xy * g_Resolution;
    float2 center = g_Resolution * 0.5;
    float2 localPos = pixelPos - center;
    float2 halfSize = center;

    float dist = sdTopPill(localPos, halfSize, g_CornerRadius);

    // Anti-aliased optical boundary
    float outerAlpha = 1.0 - smoothstep(-0.75, 0.75, dist);
    if (outerAlpha <= 0.001)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // Surface normal of the curved glass meniscus
    float2 grad = float2(
        sdTopPill(localPos + float2(1.0, 0.0), halfSize, g_CornerRadius) - dist,
        sdTopPill(localPos + float2(0.0, 1.0), halfSize, g_CornerRadius) - dist
    );
    float2 normal = normalize(grad + 1e-5);

    // Curved meniscus lensing (strongest in the outer 22px bevel curve)
    float bevel = smoothstep(-22.0, 0.0, dist);
    float lensCurve = bevel * bevel;
    float2 baseDisp = normal * (lensCurve * g_RefractionPx / g_Resolution);

    // Subtle chromatic dispersion across R, G, B channels like real optical glass
    float2 uvR = clamp(uv0.xy - baseDisp * 1.12, float2(0.001, 0.001), float2(0.999, 0.999));
    float2 uvG = clamp(uv0.xy - baseDisp * 1.00, float2(0.001, 0.001), float2(0.999, 0.999));
    float2 uvB = clamp(uv0.xy - baseDisp * 0.88, float2(0.001, 0.001), float2(0.999, 0.999));

    float3 backdrop;
    backdrop.r = InputTexture.Sample(InputSampler, uvR).r;
    backdrop.g = InputTexture.Sample(InputSampler, uvG).g;
    backdrop.b = InputTexture.Sample(InputSampler, uvB).b;

    // Vibrancy & brightness lift for luminous crystal transmission
    float luma = dot(backdrop, float3(0.2126, 0.7152, 0.0722));
    float3 vibrant = lerp(float3(luma, luma, luma), backdrop, g_Saturation);
    vibrant = vibrant * 1.06 + 0.02;

    // Adaptive optical clarity (keeps background refraction visible without milky mud)
    float adaptiveAlpha = lerp(g_TintColor.a * 0.95, g_TintColor.a * 0.65, g_BackdropLuma);
    float3 glassBody = lerp(vibrant, g_TintColor.rgb, adaptiveAlpha);

    // Ensure collapsed 140x4 slim line reads crisply on both bright and dark wallpapers
    float3 collapsedPill = float3(0.08, 0.12, 0.20) * (1.0 - g_ExpandProgress) * 0.85;
    glassBody = lerp(glassBody, collapsedPill + float3(0.22, 0.56, 0.92) * 0.28, (1.0 - g_ExpandProgress) * 0.72);

    // Soft internal caustic sheen across upper-left curved face
    float caustic = saturate(1.0 - length((localPos + float2(halfSize.x * 0.35, halfSize.y * 0.45)) / (halfSize * 1.4)));
    glassBody += float3(1.0, 1.0, 1.0) * (caustic * 0.09 * g_ExpandProgress);

    // Dual-layer Specular Rim: crisp 1.25px bright top-left highlight + subtle bottom bounce rim
    float rimMask = smoothstep(-1.8, -0.35, dist) * (1.0 - smoothstep(-0.35, 0.65, dist));
    float topLeftLight = saturate(dot(normal, normalize(float2(-0.55, -0.84))) * 0.55 + 0.45);
    float bottomBounce = saturate(dot(normal, normalize(float2(0.45, 0.89))) * 0.50 + 0.25);

    float3 specularRim = float3(1.0, 1.0, 1.0) * (topLeftLight * 0.78 + bottomBounce * 0.28);

    // Luminous top-edge pill glow when collapsed to 4px
    float slimGlow = (1.0 - g_ExpandProgress) * 0.42 * (1.0 - saturate(abs(localPos.x) / halfSize.x));

    float3 finalRgb = glassBody + (rimMask * specularRim) + slimGlow;
    float finalAlpha = max(outerAlpha * 0.96, (1.0 - g_ExpandProgress) * outerAlpha);
    return float4(saturate(finalRgb) * finalAlpha, finalAlpha);
}

Texture2D mytexture : register(t0);
SamplerState mysampler : register(s0);

cbuffer cbuffer0 : register(b0)
{
    float4x4 projection_matrix;
};

struct VS_Input
{
    float2 pos : POSITION;
    float2 uv : UV;
    float4 col : COLOR;
    int tex_index : TEXINDEX;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 col : COLOR;
    int tex_index : TEXINDEX;
};

PS_INPUT vs(VS_Input input)
{
    PS_INPUT output;
    output.pos = mul(projection_matrix, float4(input.pos, 0.0f, 1.0f));
    output.uv = input.uv;
    output.col = input.col;
    output.tex_index = input.tex_index;
    return output;
}

float4 ps(PS_INPUT input) : SV_TARGET
{
    float4 tex_color = mytexture.Sample(mysampler, input.uv);
    if (input.tex_index == 0) {
        return float4(tex_color.rrrr) * input.col;
    } else {
        return tex_color * input.col;
    }
}

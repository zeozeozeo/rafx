struct VertexInput {
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VertexOutput {
    float4 position : SV_Position;
    float4 color    : COLOR;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;

    // ------------------------------------------------------------------------
    // TRY CHANGING THIS SECTION!
    // ------------------------------------------------------------------------

    float3 pos = input.position;

    // Uncomment to flip the triangle upside down:
    // pos = input.position * float3(1.0, -1.0, 1.0);

    // Uncomment to make the triangle smaller:
    // pos = input.position * 0.5;

    // ------------------------------------------------------------------------

    output.position = float4(pos, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {

    // ------------------------------------------------------------------------
    // TRY CHANGING THIS SECTION!
    // ------------------------------------------------------------------------

    float4 finalColor = input.color;

    // Uncomment to make the triangle solid red:
    // finalColor = float4(1.0, 0.0, 0.0, 1.0);

    // Uncomment for a pretty blue-green gradient:
    // finalColor = float4(0.0, input.color.g, input.color.b + 0.5, 1.0);

    // Uncomment to invert colors:
    // finalColor = 1.0 - input.color;

    // ------------------------------------------------------------------------

    return finalColor;
}

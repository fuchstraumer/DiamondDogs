// triangle.slang - Classic Vulkan triangle shader
// Simple vertex and fragment shaders for rendering a colored triangle
// Used for testing the ShaderObject system

// Vertex shader output / Fragment shader input
struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
}

// Vertex shader entry point
[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID)
{
    VertexOutput output;
    
    // Classic Vulkan triangle vertices (NDC coordinates)
    float2 positions[3] = {
        float2( 0.0, -0.5),  // Bottom center
        float2(-0.5,  0.5),  // Top left  
        float2( 0.5,  0.5)   // Top right
    };
    
    // RGB colors for each vertex
    float3 colors[3] = {
        float3(1.0, 0.0, 0.0),  // Red
        float3(0.0, 1.0, 0.0),  // Green
        float3(0.0, 0.0, 1.0)   // Blue
    };
    
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.color = colors[vertexID];
    
    return output;
}

// Fragment shader entry point
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
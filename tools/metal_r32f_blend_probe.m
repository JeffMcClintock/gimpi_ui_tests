// R32Float blend probe (macOS, manual): does this GPU blend into R32Float, and
// how exactly? Adds 1/N into a 4x4 target N times with ONE/ONE blending and
// reads it back, with R16Float for comparison. The GPU backend's path atlas
// accumulates coverage this way (METAL_BACKEND_PLAN.md, "Coverage atlas
// precision"); a GPU that refuses the pipeline or drifts at fp16 rates needs
// the fallback described there.
//
//   clang -fobjc-arc -O1 -framework Metal -framework Foundation tools/metal_r32f_blend_probe.m -o r32f_blend_probe
//   MTL_DEBUG_LAYER=1 ./r32f_blend_probe
//
// Apple M1, 2026-10-23: R32Float 400 x 1/400 -> 0.999999166, R16Float -> 0.977539062.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>

static NSString* src = @
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct V { float4 p [[position]]; };\n"
    "vertex V vs(uint id [[vertex_id]]) { float2 q[3] = { {-1,-1}, {3,-1}, {-1,3} }; V v; v.p = float4(q[id], 0, 1); return v; }\n"
    "fragment float4 fs(constant float& value [[buffer(0)]]) { return float4(value); }\n";

static float halfToFloat(uint16_t h)
{
    __fp16 f; memcpy(&f, &h, 2); return (float)f;
}

static void run(id<MTLDevice> dev, id<MTLLibrary> lib, MTLPixelFormat fmt, const char* name, int passes)
{
    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
    pd.colorAttachments[0].pixelFormat = fmt;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    NSError* err = nil;
    id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!ps) { printf("%s: pipeline with blending REFUSED: %s\n", name, err.localizedDescription.UTF8String); return; }

    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:4 height:4 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
    const size_t bpp = fmt == MTLPixelFormatR32Float ? 4 : 2;
    id<MTLBuffer> out = [dev newBufferWithLength:16 * bpp options:MTLResourceStorageModeShared];

    id<MTLCommandQueue> q = [dev newCommandQueue];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    const float value = 1.0f / passes;
    for (int i = 0; i < passes; ++i)
    {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = tex;
        rp.colorAttachments[0].loadAction = i == 0 ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
        [e setRenderPipelineState:ps];
        [e setFragmentBytes:&value length:4 atIndex:0];
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [e endEncoding];
    }
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(4, 4, 1)
              toBuffer:out destinationOffset:0 destinationBytesPerRow:4 * bpp destinationBytesPerImage:16 * bpp];
    [b endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.error) { printf("%s: command buffer error: %s\n", name, cb.error.localizedDescription.UTF8String); return; }

    const float got = fmt == MTLPixelFormatR32Float ? ((const float*)out.contents)[5] : halfToFloat(((const uint16_t*)out.contents)[5]);
    printf("%s: %d passes of 1/%d blend to %.9f (error %.3g)\n", name, passes, passes, got, got - 1.0f);
}

int main()
{
    @autoreleasepool
    {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        printf("device: %s, Apple7: %d, Apple8: %d, Mac2: %d\n", dev.name.UTF8String,
               [dev supportsFamily:MTLGPUFamilyApple7], [dev supportsFamily:MTLGPUFamilyApple8], [dev supportsFamily:MTLGPUFamilyMac2]);
        NSError* err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
        if (!lib) { printf("compile failed: %s\n", err.localizedDescription.UTF8String); return 1; }
        const int counts[] = { 3, 400, 2048 };
        for (int i = 0; i < 3; ++i)
        {
            run(dev, lib, MTLPixelFormatR32Float, "R32Float", counts[i]);
            run(dev, lib, MTLPixelFormatR16Float, "R16Float", counts[i]);
        }
    }
    return 0;
}

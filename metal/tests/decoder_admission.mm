// Copyright (c) 2026 OpenAI
// SPDX-License-Identifier: MIT
#include "pyrowave_metal.h"
#import <Metal/Metal.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
std::vector<uint8_t> readback(id<MTLTexture> texture) {
    size_t bytes=texture.pixelFormat==MTLPixelFormatR16Unorm?2:1;
    size_t row=(texture.width*bytes+255)&~size_t(255);
    id<MTLBuffer> buffer=[texture.device newBufferWithLength:row*texture.height options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue=[texture.device newCommandQueue];id<MTLCommandBuffer> cmd=[queue commandBuffer];
    id<MTLBlitCommandEncoder> encoder=[cmd blitCommandEncoder];
    [encoder copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(texture.width,texture.height,1)
                   toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*texture.height];
    [encoder endEncoding];[cmd commit];[cmd waitUntilCompleted];assert(cmd.status==MTLCommandBufferStatusCompleted);
    std::vector<uint8_t> result(texture.width*texture.height*bytes);
    for(size_t y=0;y<texture.height;++y)memcpy(result.data()+y*texture.width*bytes,static_cast<uint8_t*>(buffer.contents)+y*row,texture.width*bytes);
    return result;
}
void upload_admission() {
    id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
    pyrowave_device device=nullptr;pyrowave_device_create_info create_device{};create_device.mtl_device=(__bridge void*)metal;
    assert(pyrowave_device_create(&create_device,&device)==PYROWAVE_SUCCESS);
    pyrowave_decoder decoder=nullptr;pyrowave_decoder_create_info create{device,128,128,PYROWAVE_CHROMA_SUBSAMPLING_420};
    assert(pyrowave_decoder_create(&create,&decoder)==PYROWAVE_SUCCESS);
    uint32_t sequence[2]={0x80000000u|127u|(127u<<14),0};
    id<MTLTexture> textures[3];pyrowave_gpu_buffers buffers{};
    for(int i=0;i<3;++i){auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:i?64:128 height:i?64:128 mipmapped:NO];
        descriptor.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;descriptor.storageMode=MTLStorageModePrivate;
        textures[i]=[metal newTextureWithDescriptor:descriptor];buffers.planes[i]=(__bridge void*)textures[i];}
    id<MTLCommandQueue> queue=[metal newCommandQueue];id<MTLCommandBuffer> commands[5];
    for(int i=0;i<5;++i){pyrowave_decoder_clear(decoder);assert(pyrowave_decoder_push_packet(decoder,sequence,sizeof(sequence))==PYROWAVE_SUCCESS);
        commands[i]=[queue commandBuffer];auto result=pyrowave_decoder_decode_gpu_buffer(decoder,(__bridge void*)commands[i],&buffers);
        assert(result==(i==4?PYROWAVE_ERROR_BUSY:PYROWAVE_SUCCESS));}
    assert(pyrowave_decoder_decode_is_ready(decoder,false)); // Busy consumed nothing.
    assert(!pyrowave_decoder_decode_is_ready_with_sideband(decoder,true,5,0.9f,nullptr,0));
    assert(!pyrowave_decoder_decode_is_ready_with_sideband(decoder,true,-1,0.9f,nullptr,0));
    assert(!pyrowave_decoder_decode_is_ready_with_sideband(decoder,true,0,NAN,nullptr,0));
    for(int i=0;i<4;++i)[commands[i] commit];for(int i=0;i<4;++i)[commands[i] waitUntilCompleted];
    assert(pyrowave_decoder_decode_gpu_buffer(decoder,(__bridge void*)commands[4],&buffers)==PYROWAVE_SUCCESS);
    [commands[4] commit];[commands[4] waitUntilCompleted];
    auto result=readback(textures[0]);for(uint8_t value:result)assert(value==128);
    pyrowave_decoder_destroy(decoder);pyrowave_device_destroy(device);
    std::cout<<"Bounded upstream upload admission and sideband validation passed\n";
}
int main() { @autoreleasepool {
    if (!pyrowave_device_is_supported((__bridge void*)MTLCreateSystemDefaultDevice())) return 77;
    upload_admission();
}}

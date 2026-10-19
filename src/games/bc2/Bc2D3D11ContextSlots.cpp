// Compile-time proof of the ID3D11DeviceContext vtable slots NativeProbe hooks
// (public D3D11 ABI). Built with the SDK's C interface, whose Vtbl struct lists
// every method in ABI order. Runtime additionally pins each entry to d3d11.dll.
#define CINTERFACE
#define D3D11_NO_HELPERS
#include <d3d11.h>
#include <cstddef>
static_assert(offsetof(ID3D11DeviceContextVtbl,DrawIndexed)==12*sizeof(void*),"DrawIndexed slot");
static_assert(offsetof(ID3D11DeviceContextVtbl,Draw)==13*sizeof(void*),"Draw slot");
static_assert(offsetof(ID3D11DeviceContextVtbl,DrawIndexedInstanced)==20*sizeof(void*),"DrawIndexedInstanced slot");
static_assert(offsetof(ID3D11DeviceContextVtbl,DrawInstanced)==21*sizeof(void*),"DrawInstanced slot");
static_assert(offsetof(ID3D11DeviceContextVtbl,Map)==14*sizeof(void*),"Map slot");
static_assert(offsetof(ID3D11DeviceContextVtbl,Unmap)==15*sizeof(void*),"Unmap slot");
static_assert(offsetof(ID3D11DeviceContextVtbl,UpdateSubresource)==48*sizeof(void*),"UpdateSubresource slot");
static_assert(offsetof(ID3D11DeviceVtbl,CreateInputLayout)==11*sizeof(void*),"CreateInputLayout slot");
static_assert(offsetof(ID3D11DeviceVtbl,CreateVertexShader)==12*sizeof(void*),"CreateVertexShader slot");
static_assert(offsetof(ID3D11DeviceVtbl,CreatePixelShader)==15*sizeof(void*),"CreatePixelShader slot");

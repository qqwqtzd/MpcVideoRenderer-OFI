// Optical-flow frame interpolation for MPC Video Renderer.
//
// Generates one in-between frame from two neighbouring RGB frames using the
// NVIDIA Optical Flow Accelerator (the same hardware DLSS Frame Generation
// uses for motion estimation) plus a warp+blend pixel shader.
//
// The interpolator owns its OFA instance, its luma textures and its shaders.
// MPC-VR has no compute-shader infrastructure, so everything here is ps_4_0.

#pragma once

#include <atlbase.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <cstdint>
#include <memory>
#include <vector>

#include "NvOF.h"
#include "NvOFD3D11.h"

class CFrameInterpolator
{
public:
	CFrameInterpolator() = default;
	~CFrameInterpolator();

	CFrameInterpolator(const CFrameInterpolator&) = delete;
	CFrameInterpolator& operator=(const CFrameInterpolator&) = delete;

	// Loads the quad shaders from this module's resources. Safe to call once.
	HRESULT Init(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
	void Release();

	// (Re)creates size-dependent resources and brings up the OFA. Returns
	// S_FALSE when the OFA is unavailable (non-RTX GPU, old driver, missing
	// nvofapi64.dll); the renderer should then keep interpolation disabled.
	HRESULT Configure(UINT width, UINT height);
	void ReleaseSize();

	bool IsReady() const { return m_bReady; }
	int GridSize() const { return m_iGridSize; }

	// pPrev / pCur are SRV-bindable frames in the renderer's internal RGB
	// format; pDst is a render-target frame of the same format and size.
	// All three belong to the renderer's D3D11 device.
	HRESULT Interpolate(ID3D11ShaderResourceView* pPrevSrv,
	                    ID3D11ShaderResourceView* pCurSrv,
	                    ID3D11Texture2D* pDst);

private:
	HRESULT CreateQuadShaders();
	HRESULT CreateQuadGeometry();
	HRESULT CreateLumaResources();
	void DestroyLumaResources();

	// Runs the RGB->luma shader for one source into m_pLumaRT[slot].
	HRESULT ExtractLuma(ID3D11ShaderResourceView* pSrv, int slot);

	CComPtr<ID3D11Device>        m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pContext;

	CComPtr<ID3D11VertexShader>  m_pVS;
	CComPtr<ID3D11InputLayout>   m_pInputLayout;
	CComPtr<ID3D11Buffer>        m_pVertexBuffer;
	CComPtr<ID3D11PixelShader>   m_pPSLuma;
	CComPtr<ID3D11PixelShader>   m_pPSInterp;
	CComPtr<ID3D11SamplerState>  m_pSampler;
	CComPtr<ID3D11Buffer>        m_pInterpConstants;

	// R8 luma render targets (shader output) and their SRVs.
	CComPtr<ID3D11Texture2D>          m_pLumaRT[2];
	CComPtr<ID3D11RenderTargetView>   m_pLumaRTV[2];
	CComPtr<ID3D11ShaderResourceView> m_pLumaSRV[2];

	// Optical flow accelerator.
	NvOFObj                     m_pOF;
	std::vector<NvOFBufferObj>  m_OFInputs;
	NvOFBufferObj               m_OFOutput;
	CComPtr<ID3D11ShaderResourceView> m_pFlowSRV;
	CComPtr<ID3D11Texture2D>    m_pFlowTexture; // borrowed from m_OFOutput

	UINT m_Width = 0;
	UINT m_Height = 0;
	int  m_iGridSize = 1;
	bool m_bReady = false;
};

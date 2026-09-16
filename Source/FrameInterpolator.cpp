#include "stdafx.h"

#include "FrameInterpolator.h"

#include "resource.h"
#include "Utils/Util.h"

namespace
{
	struct QuadVertex
	{
		float x, y, z;
		float u, v;
	};

	const QuadVertex kQuad[4] =
	{
		{ -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
		{ -1.0f,  1.0f, 0.0f, 0.0f, 0.0f },
		{  1.0f, -1.0f, 0.0f, 1.0f, 1.0f },
		{  1.0f,  1.0f, 0.0f, 1.0f, 0.0f },
	};

	struct InterpConstants
	{
		float alpha;
		float invGrid;
		UINT  width;
		UINT  height;
	};
}

CFrameInterpolator::~CFrameInterpolator()
{
	Release();
}

HRESULT CFrameInterpolator::Init(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	if (!pDevice || !pContext)
	{
		return E_POINTER;
	}

	if (m_pDevice == pDevice && m_pVS)
	{
		return S_OK;
	}

	m_pDevice = pDevice;
	m_pContext = pContext;

	HRESULT hr = CreateQuadShaders();
	if (FAILED(hr))
	{
		Release();
		return hr;
	}

	hr = CreateQuadGeometry();
	if (FAILED(hr))
	{
		Release();
		return hr;
	}

	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	hr = m_pDevice->CreateSamplerState(&sd, &m_pSampler);
	if (FAILED(hr))
	{
		Release();
		return hr;
	}

	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = sizeof(InterpConstants);
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	hr = m_pDevice->CreateBuffer(&bd, nullptr, &m_pInterpConstants);
	if (FAILED(hr))
	{
		Release();
		return hr;
	}

	return S_OK;
}

HRESULT CFrameInterpolator::CreateQuadShaders()
{
	LPVOID data = nullptr;
	DWORD size = 0;

	HRESULT hr = GetDataFromResource(data, size, IDF_VS_11_SIMPLE);
	if (FAILED(hr))
	{
		return hr;
	}
	hr = m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVS);
	if (FAILED(hr))
	{
		return hr;
	}

	hr = GetDataFromResource(data, size, IDF_PS_11_LUMA);
	if (FAILED(hr))
	{
		return hr;
	}
	hr = m_pDevice->CreatePixelShader(data, size, nullptr, &m_pPSLuma);
	if (FAILED(hr))
	{
		return hr;
	}

	hr = GetDataFromResource(data, size, IDF_PS_11_INTERP);
	if (FAILED(hr))
	{
		return hr;
	}
	return m_pDevice->CreatePixelShader(data, size, nullptr, &m_pPSInterp);
}

HRESULT CFrameInterpolator::CreateQuadGeometry()
{
	LPVOID data = nullptr;
	DWORD size = 0;
	HRESULT hr = GetDataFromResource(data, size, IDF_VS_11_SIMPLE);
	if (FAILED(hr))
	{
		return hr;
	}

	const D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	hr = m_pDevice->CreateInputLayout(layout, _countof(layout), data, size, &m_pInputLayout);
	if (FAILED(hr))
	{
		return hr;
	}

	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = sizeof(kQuad);
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA init = {};
	init.pSysMem = kQuad;
	return m_pDevice->CreateBuffer(&bd, &init, &m_pVertexBuffer);
}

HRESULT CFrameInterpolator::Configure(UINT width, UINT height)
{
	ReleaseSize();

	if (!m_pDevice || !m_pContext || !width || !height)
	{
		return E_INVALIDARG;
	}

	m_Width = width;
	m_Height = height;

	try
	{
		m_pOF = NvOFD3D11::Create(m_pDevice, m_pContext, width, height,
			NV_OF_BUFFER_FORMAT_GRAYSCALE8, NV_OF_MODE_OPTICALFLOW, NV_OF_PERF_LEVEL_FAST);
	}
	catch (...)
	{
		m_pOF.reset();
		return S_FALSE;
	}
	if (!m_pOF)
	{
		return S_FALSE;
	}

	uint32_t grid = NV_OF_OUTPUT_VECTOR_GRID_SIZE_1;
	if (!m_pOF->CheckGridSize(grid))
	{
		uint32_t minGrid = 0;
		if (!m_pOF->GetNextMinGridSize(grid, minGrid))
		{
			ReleaseSize();
			return S_FALSE;
		}
		grid = minGrid;
	}

	try
	{
		m_pOF->Init(grid);

		m_OFInputs = m_pOF->CreateBuffers(NV_OF_BUFFER_USAGE_INPUT, 2);
		if (m_OFInputs.size() < 2)
		{
			ReleaseSize();
			return S_FALSE;
		}
		auto outs = m_pOF->CreateBuffers(NV_OF_BUFFER_USAGE_OUTPUT, 1);
		if (outs.empty())
		{
			ReleaseSize();
			return S_FALSE;
		}
		m_OFOutput = std::move(outs[0]);
	}
	catch (...)
	{
		ReleaseSize();
		return S_FALSE;
	}

	m_pFlowTexture = static_cast<NvOFBufferD3D11*>(m_OFOutput.get())->getD3D11TextureHandle();
	if (!m_pFlowTexture ||
		FAILED(m_pDevice->CreateShaderResourceView(m_pFlowTexture, nullptr, &m_pFlowSRV)) ||
		FAILED(CreateLumaResources()))
	{
		ReleaseSize();
		return S_FALSE;
	}

	m_iGridSize = static_cast<int>(grid);
	m_bReady = true;
	return S_OK;
}

HRESULT CFrameInterpolator::CreateLumaResources()
{
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = m_Width;
	td.Height = m_Height;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	for (int i = 0; i < 2; ++i)
	{
		if (FAILED(m_pDevice->CreateTexture2D(&td, nullptr, &m_pLumaRT[i])) ||
			FAILED(m_pDevice->CreateRenderTargetView(m_pLumaRT[i], nullptr, &m_pLumaRTV[i])))
		{
			return E_FAIL;
		}
	}
	return S_OK;
}

HRESULT CFrameInterpolator::ExtractLuma(ID3D11ShaderResourceView* pSrv, int slot)
{
	if (!pSrv)
	{
		return E_POINTER;
	}

	ID3D11RenderTargetView* rtv = m_pLumaRTV[slot];
	m_pContext->OMSetRenderTargets(1, &rtv, nullptr);

	D3D11_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f };
	m_pContext->RSSetViewports(1, &vp);

	UINT stride = sizeof(QuadVertex);
	UINT offset = 0;
	m_pContext->IASetInputLayout(m_pInputLayout);
	m_pContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer.p, &stride, &offset);
	m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_pContext->VSSetShader(m_pVS, nullptr, 0);
	m_pContext->PSSetShader(m_pPSLuma, nullptr, 0);

	ID3D11ShaderResourceView* srvs[1] = { pSrv };
	m_pContext->PSSetShaderResources(0, 1, srvs);
	ID3D11SamplerState* samp = m_pSampler;
	m_pContext->PSSetSamplers(0, 1, &samp);

	m_pContext->Draw(4, 0);

	ID3D11ShaderResourceView* none[1] = {};
	m_pContext->PSSetShaderResources(0, 1, none);
	ID3D11RenderTargetView* noRtv = nullptr;
	m_pContext->OMSetRenderTargets(1, &noRtv, nullptr);

	return S_OK;
}

HRESULT CFrameInterpolator::Interpolate(ID3D11ShaderResourceView* pPrevSrv, ID3D11ShaderResourceView* pCurSrv, ID3D11Texture2D* pDst)
{
	if (!m_bReady || !pPrevSrv || !pCurSrv || !pDst)
	{
		return E_FAIL;
	}

	HRESULT hr = ExtractLuma(pPrevSrv, 0);
	if (FAILED(hr))
	{
		return hr;
	}
	hr = ExtractLuma(pCurSrv, 1);
	if (FAILED(hr))
	{
		return hr;
	}

	for (int i = 0; i < 2; ++i)
	{
		ID3D11Texture2D* pLuma = static_cast<NvOFBufferD3D11*>(m_OFInputs[i].get())->getD3D11TextureHandle();
		m_pContext->CopyResource(pLuma, m_pLumaRT[i]);
	}

	try
	{
		m_pOF->Execute(m_OFInputs[0].get(), m_OFInputs[1].get(), m_OFOutput.get());
	}
	catch (...)
	{
		return E_FAIL;
	}

	D3D11_MAPPED_SUBRESOURCE mr = {};
	if (FAILED(m_pContext->Map(m_pInterpConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr)))
	{
		return E_FAIL;
	}
	InterpConstants constants = {};
	constants.alpha = 0.5f;
	constants.invGrid = 1.0f / static_cast<float>(m_iGridSize);
	constants.width = m_Width;
	constants.height = m_Height;
	memcpy(mr.pData, &constants, sizeof(constants));
	m_pContext->Unmap(m_pInterpConstants, 0);

	CComPtr<ID3D11RenderTargetView> rtv;
	if (FAILED(m_pDevice->CreateRenderTargetView(pDst, nullptr, &rtv)))
	{
		return E_FAIL;
	}

	ID3D11RenderTargetView* pRtv = rtv;
	m_pContext->OMSetRenderTargets(1, &pRtv, nullptr);

	D3D11_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f };
	m_pContext->RSSetViewports(1, &vp);

	UINT stride = sizeof(QuadVertex);
	UINT offset = 0;
	m_pContext->IASetInputLayout(m_pInputLayout);
	m_pContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer.p, &stride, &offset);
	m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_pContext->VSSetShader(m_pVS, nullptr, 0);
	m_pContext->PSSetShader(m_pPSInterp, nullptr, 0);

	ID3D11ShaderResourceView* srvs[3] = { pPrevSrv, pCurSrv, m_pFlowSRV.p };
	m_pContext->PSSetShaderResources(0, 3, srvs);
	ID3D11SamplerState* samp = m_pSampler;
	m_pContext->PSSetSamplers(0, 1, &samp);
	ID3D11Buffer* cb = m_pInterpConstants;
	m_pContext->PSSetConstantBuffers(0, 1, &cb);

	m_pContext->Draw(4, 0);

	ID3D11ShaderResourceView* none[3] = {};
	m_pContext->PSSetShaderResources(0, 3, none);
	ID3D11RenderTargetView* noRtv = nullptr;
	m_pContext->OMSetRenderTargets(1, &noRtv, nullptr);

	return S_OK;
}

void CFrameInterpolator::DestroyLumaResources()
{
	for (int i = 0; i < 2; ++i)
	{
		m_pLumaSRV[i].Release();
		m_pLumaRTV[i].Release();
		m_pLumaRT[i].Release();
	}
}

void CFrameInterpolator::ReleaseSize()
{
	m_bReady = false;
	m_iGridSize = 1;
	m_pFlowSRV.Release();
	m_pFlowTexture.Release();
	m_OFOutput.reset();
	m_OFInputs.clear();
	m_pOF.reset();
	DestroyLumaResources();
	m_Width = 0;
	m_Height = 0;
}

void CFrameInterpolator::Release()
{
	ReleaseSize();
	m_pInterpConstants.Release();
	m_pSampler.Release();
	m_pPSInterp.Release();
	m_pPSLuma.Release();
	m_pVertexBuffer.Release();
	m_pInputLayout.Release();
	m_pVS.Release();
	m_pContext.Release();
	m_pDevice.Release();
}

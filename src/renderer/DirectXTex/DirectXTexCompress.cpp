//-------------------------------------------------------------------------------------
// DirectXTexCompress.cpp
//
// DirectX Texture Library - Texture compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248926
//-------------------------------------------------------------------------------------

#include "DirectXTexP.h"

#ifdef _OPENMP
#include <omp.h>
#pragma warning(disable : 4616 6993)
#endif

#include "BC.h"

using namespace DirectX;
using namespace DirectX::Internal;
using namespace DirectX::PackedVector;
using Microsoft::WRL::ComPtr;

namespace DirectX
{
    constexpr uint32_t GetBCFlags(_In_ TEX_COMPRESS_FLAGS compress) noexcept
    {
        static_assert(static_cast<int>(TEX_COMPRESS_RGB_DITHER) == static_cast<int>(BC_FLAGS_DITHER_RGB), "TEX_COMPRESS_* flags should match BC_FLAGS_*");
        static_assert(static_cast<int>(TEX_COMPRESS_A_DITHER) == static_cast<int>(BC_FLAGS_DITHER_A), "TEX_COMPRESS_* flags should match BC_FLAGS_*");
        static_assert(static_cast<int>(TEX_COMPRESS_DITHER) == static_cast<int>(BC_FLAGS_DITHER_RGB | BC_FLAGS_DITHER_A), "TEX_COMPRESS_* flags should match BC_FLAGS_*");
        static_assert(static_cast<int>(TEX_COMPRESS_UNIFORM) == static_cast<int>(BC_FLAGS_UNIFORM), "TEX_COMPRESS_* flags should match BC_FLAGS_*");
        static_assert(static_cast<int>(TEX_COMPRESS_BC7_USE_3SUBSETS) == static_cast<int>(BC_FLAGS_USE_3SUBSETS), "TEX_COMPRESS_* flags should match BC_FLAGS_*");
        static_assert(static_cast<int>(TEX_COMPRESS_BC7_QUICK) == static_cast<int>(BC_FLAGS_FORCE_BC7_MODE6), "TEX_COMPRESS_* flags should match BC_FLAGS_*");
        return (compress & (BC_FLAGS_DITHER_RGB | BC_FLAGS_DITHER_A | BC_FLAGS_UNIFORM | BC_FLAGS_USE_3SUBSETS | BC_FLAGS_FORCE_BC7_MODE6));
    }

    constexpr TEX_FILTER_FLAGS GetSRGBFlags(_In_ TEX_COMPRESS_FLAGS compress) noexcept
    {
        static_assert(TEX_FILTER_SRGB_IN == 0x1000000, "TEX_FILTER_SRGB flag values don't match TEX_FILTER_SRGB_MASK");
        static_assert(static_cast<int>(TEX_COMPRESS_SRGB_IN) == static_cast<int>(TEX_FILTER_SRGB_IN), "TEX_COMPRESS_SRGB* should match TEX_FILTER_SRGB*");
        static_assert(static_cast<int>(TEX_COMPRESS_SRGB_OUT) == static_cast<int>(TEX_FILTER_SRGB_OUT), "TEX_COMPRESS_SRGB* should match TEX_FILTER_SRGB*");
        static_assert(static_cast<int>(TEX_COMPRESS_SRGB) == static_cast<int>(TEX_FILTER_SRGB), "TEX_COMPRESS_SRGB* should match TEX_FILTER_SRGB*");
        return static_cast<TEX_FILTER_FLAGS>(compress & TEX_FILTER_SRGB_MASK);
    }

    inline bool DetermineEncoderSettings(_In_ DXGI_FORMAT format, _Out_ BC_ENCODE& pfEncode, _Out_ size_t& blocksize, _Out_ TEX_FILTER_FLAGS& cflags) noexcept
    {
        switch (format)
        {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:    pfEncode = nullptr;         blocksize = 8;   cflags = TEX_FILTER_DEFAULT; break;
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:    pfEncode = D3DXEncodeBC2;   blocksize = 16;  cflags = TEX_FILTER_DEFAULT; break;
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:    pfEncode = D3DXEncodeBC3;   blocksize = 16;  cflags = TEX_FILTER_DEFAULT; break;
        //case DXGI_FORMAT_BC4_UNORM:         pfEncode = D3DXEncodeBC4U;  blocksize = 8;   cflags = TEX_FILTER_RGB_COPY_RED; break;
        //case DXGI_FORMAT_BC4_SNORM:         pfEncode = D3DXEncodeBC4S;  blocksize = 8;   cflags = TEX_FILTER_RGB_COPY_RED; break;
        //case DXGI_FORMAT_BC5_UNORM:         pfEncode = D3DXEncodeBC5U;  blocksize = 16;  cflags = TEX_FILTER_RGB_COPY_RED | TEX_FILTER_RGB_COPY_GREEN; break;
        //case DXGI_FORMAT_BC5_SNORM:         pfEncode = D3DXEncodeBC5S;  blocksize = 16;  cflags = TEX_FILTER_RGB_COPY_RED | TEX_FILTER_RGB_COPY_GREEN; break;
        //case DXGI_FORMAT_BC6H_UF16:         pfEncode = D3DXEncodeBC6HU; blocksize = 16;  cflags = TEX_FILTER_DEFAULT; break;
        //case DXGI_FORMAT_BC6H_SF16:         pfEncode = D3DXEncodeBC6HS; blocksize = 16;  cflags = TEX_FILTER_DEFAULT; break;
        //case DXGI_FORMAT_BC7_UNORM:
        //case DXGI_FORMAT_BC7_UNORM_SRGB:    pfEncode = D3DXEncodeBC7;   blocksize = 16;  cflags = TEX_FILTER_DEFAULT; break;
        default:                            pfEncode = nullptr;         blocksize = 0;   cflags = TEX_FILTER_DEFAULT; return false;
        }

        return true;
    }


    //-------------------------------------------------------------------------------------
    HRESULT CompressBC(
        const Image& image,
        const Image& result,
        uint32_t bcflags,
        TEX_FILTER_FLAGS srgb,
        float threshold,
        const std::function<bool __cdecl(size_t, size_t)>& statusCallback) noexcept
    {
        if (!image.pixels || !result.pixels)
            return E_POINTER;

        assert(image.width == result.width);
        assert(image.height == result.height);

        const DXGI_FORMAT format = image.format;
        size_t sbpp = BitsPerPixel(format);
        if (!sbpp)
            return E_FAIL;

        if (sbpp < 8)
        {
            // We don't support compressing from monochrome (DXGI_FORMAT_R1_UNORM)
            return HRESULT_E_NOT_SUPPORTED;
        }

        // Round to bytes
        sbpp = (sbpp + 7) / 8;

        uint8_t *pDest = result.pixels;

        // Determine BC format encoder
        BC_ENCODE pfEncode;
        size_t blocksize;
        TEX_FILTER_FLAGS cflags;
        if (!DetermineEncoderSettings(result.format, pfEncode, blocksize, cflags))
            return HRESULT_E_NOT_SUPPORTED;

        XM_ALIGNED_DATA(16) XMVECTOR temp[16];
        const uint8_t *pSrc = image.pixels;
        const uint8_t *pEnd = image.pixels + image.slicePitch;
        const size_t rowPitch = image.rowPitch;
        for (size_t h = 0; h < image.height; h += 4)
        {
            if (statusCallback)
            {
                if (!statusCallback(h, image.height))
                {
                    return E_ABORT;
                }
            }

            const uint8_t *sptr = pSrc;
            uint8_t* dptr = pDest;
            const size_t ph = std::min<size_t>(4, image.height - h);
            size_t w = 0;
            for (size_t count = 0; (count < result.rowPitch) && (w < image.width); count += blocksize, w += 4)
            {
                const size_t pw = std::min<size_t>(4, image.width - w);
                assert(pw > 0 && ph > 0);

                const ptrdiff_t bytesLeft = pEnd - sptr;
                assert(bytesLeft > 0);
                size_t bytesToRead = std::min<size_t>(rowPitch, static_cast<size_t>(bytesLeft));
                if (!LoadScanline(&temp[0], pw, sptr, bytesToRead, format))
                    return E_FAIL;

                if (ph > 1)
                {
                    bytesToRead = std::min<size_t>(rowPitch, static_cast<size_t>(bytesLeft) - rowPitch);
                    if (!LoadScanline(&temp[4], pw, sptr + rowPitch, bytesToRead, format))
                        return E_FAIL;

                    if (ph > 2)
                    {
                        bytesToRead = std::min<size_t>(rowPitch, static_cast<size_t>(bytesLeft) - rowPitch * 2);
                        if (!LoadScanline(&temp[8], pw, sptr + rowPitch * 2, bytesToRead, format))
                            return E_FAIL;

                        if (ph > 3)
                        {
                            bytesToRead = std::min<size_t>(rowPitch, static_cast<size_t>(bytesLeft) - rowPitch * 3);
                            if (!LoadScanline(&temp[12], pw, sptr + rowPitch * 3, bytesToRead, format))
                                return E_FAIL;
                        }
                    }
                }

                if (pw != 4 || ph != 4)
                {
                    // Replicate pixels for partial block
                    static const size_t uSrc[] = { 0, 0, 0, 1 };

                    if (pw < 4)
                    {
                        for (size_t t = 0; t < ph && t < 4; ++t)
                        {
                            for (size_t s = pw; s < 4; ++s)
                            {
                            #pragma prefast(suppress: 26000, "PREFAST false positive")
                                temp[(t << 2) | s] = temp[(t << 2) | uSrc[s]];
                            }
                        }
                    }

                    if (ph < 4)
                    {
                        for (size_t t = ph; t < 4; ++t)
                        {
                            for (size_t s = 0; s < 4; ++s)
                            {
                            #pragma prefast(suppress: 26000, "PREFAST false positive")
                                temp[(t << 2) | s] = temp[(uSrc[t] << 2) | s];
                            }
                        }
                    }
                }

                ConvertScanline(temp, 16, result.format, format, cflags | srgb);

                if (pfEncode)
                    pfEncode(dptr, temp, bcflags);
                else
                    D3DXEncodeBC1(dptr, temp, threshold, bcflags);

                sptr += sbpp * 4;
                dptr += blocksize;
            }

            pSrc += rowPitch * 4;
            pDest += result.rowPitch;
        }

        return S_OK;
    }
}

//-------------------------------------------------------------------------------------
// Loads an image row into standard RGBA XMVECTOR (aligned) array
//-------------------------------------------------------------------------------------
#define LOAD_SCANLINE( type, func )\
        if (size >= sizeof(type))\
        {\
            const type * __restrict sPtr = reinterpret_cast<const type*>(pSource);\
            for(size_t icount = 0; icount < (size - sizeof(type) + 1); icount += sizeof(type))\
            {\
                if (dPtr >= ePtr) break;\
                *(dPtr++) = func(sPtr++);\
            }\
            return true;\
        }\
        return false;

#pragma warning(suppress: 6101)
_Use_decl_annotations_ bool DirectX::Internal::LoadScanline(
	XMVECTOR* pDestination,
	size_t count,
	const void* pSource,
	size_t size,
	DXGI_FORMAT format) noexcept
{
	assert(pDestination && count > 0 && ((reinterpret_cast<uintptr_t>(pDestination) & 0xF) == 0));
	assert(pSource && size > 0);
	assert(IsValid(format) && !IsTypeless(format, false) && !IsCompressed(format) && !IsPlanar(format) && !IsPalettized(format));

	XMVECTOR* __restrict dPtr = pDestination;
	if (!dPtr)
		return false;

	const XMVECTOR* ePtr = pDestination + count;

	switch (static_cast<int>(format))
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		LOAD_SCANLINE(XMUBYTEN4, XMLoadUByteN4)

	case DXGI_FORMAT_R8G8B8A8_UINT:
		LOAD_SCANLINE(XMUBYTE4, XMLoadUByte4)

	case DXGI_FORMAT_R8G8B8A8_SNORM:
		LOAD_SCANLINE(XMBYTEN4, XMLoadByteN4)

	case DXGI_FORMAT_R8G8B8A8_SINT:
		LOAD_SCANLINE(XMBYTE4, XMLoadByte4)

		// We don't support the planar or palettized formats

	default:
		return false;
	}
}

#undef LOAD_SCANLINE

_Use_decl_annotations_
bool DirectX::IsPlanar(DXGI_FORMAT fmt) noexcept
{
	switch (static_cast<int>(fmt))
	{
	case DXGI_FORMAT_NV12:      // 4:2:0 8-bit
	case DXGI_FORMAT_P010:      // 4:2:0 10-bit
	case DXGI_FORMAT_P016:      // 4:2:0 16-bit
	case DXGI_FORMAT_420_OPAQUE:// 4:2:0 8-bit
	case DXGI_FORMAT_NV11:      // 4:1:1 8-bit

	case WIN10_DXGI_FORMAT_P208: // 4:2:2 8-bit
	case WIN10_DXGI_FORMAT_V208: // 4:4:0 8-bit
	case WIN10_DXGI_FORMAT_V408: // 4:4:4 8-bit
		// These are JPEG Hardware decode formats (DXGI 1.4)

	case XBOX_DXGI_FORMAT_D16_UNORM_S8_UINT:
	case XBOX_DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
	case XBOX_DXGI_FORMAT_X16_TYPELESS_G8_UINT:
		// These are Xbox One platform specific types
		return true;

	default:
		return false;
	}
}

_Use_decl_annotations_
bool DirectX::IsTypeless(DXGI_FORMAT fmt, bool partialTypeless) noexcept
{
	switch (static_cast<int>(fmt))
	{
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC7_TYPELESS:
		return true;

	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case XBOX_DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
	case XBOX_DXGI_FORMAT_X16_TYPELESS_G8_UINT:
		return partialTypeless;

	default:
		return false;
	}
}

_Use_decl_annotations_
size_t DirectX::BitsPerPixel(DXGI_FORMAT fmt) noexcept
{
	switch (static_cast<int>(fmt))
	{
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 128;

	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 96;

	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_Y416:
	case DXGI_FORMAT_Y210:
	case DXGI_FORMAT_Y216:
		return 64;

	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_AYUV:
	case DXGI_FORMAT_Y410:
	case DXGI_FORMAT_YUY2:
	case XBOX_DXGI_FORMAT_R10G10B10_7E3_A2_FLOAT:
	case XBOX_DXGI_FORMAT_R10G10B10_6E4_A2_FLOAT:
	case XBOX_DXGI_FORMAT_R10G10B10_SNORM_A2_UNORM:
		return 32;

	case DXGI_FORMAT_P010:
	case DXGI_FORMAT_P016:
	case XBOX_DXGI_FORMAT_D16_UNORM_S8_UINT:
	case XBOX_DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
	case XBOX_DXGI_FORMAT_X16_TYPELESS_G8_UINT:
	case WIN10_DXGI_FORMAT_V408:
		return 24;

	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_A8P8:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
	case WIN10_DXGI_FORMAT_P208:
	case WIN10_DXGI_FORMAT_V208:
	case WIN11_DXGI_FORMAT_A4B4G4R4_UNORM:
		return 16;

	case DXGI_FORMAT_NV12:
	case DXGI_FORMAT_420_OPAQUE:
	case DXGI_FORMAT_NV11:
		return 12;

	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
	case DXGI_FORMAT_AI44:
	case DXGI_FORMAT_IA44:
	case DXGI_FORMAT_P8:
	case XBOX_DXGI_FORMAT_R4G4_UNORM:
		return 8;

	case DXGI_FORMAT_R1_UNORM:
		return 1;

	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return 4;

	default:
		return 0;
	}
}

namespace
{
	struct ConvertData
	{
		DXGI_FORMAT format;
		size_t      datasize;
		uint32_t    flags;
	};

	const ConvertData g_ConvertTable[] =
	{
		{ DXGI_FORMAT_R32G32B32A32_FLOAT,           32, CONVF_FLOAT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R32G32B32A32_UINT,            32, CONVF_UINT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R32G32B32A32_SINT,            32, CONVF_SINT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R32G32B32_FLOAT,              32, CONVF_FLOAT | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_R32G32B32_UINT,               32, CONVF_UINT | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_R32G32B32_SINT,               32, CONVF_SINT | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_R16G16B16A16_FLOAT,           16, CONVF_FLOAT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R16G16B16A16_UNORM,           16, CONVF_UNORM | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R16G16B16A16_UINT,            16, CONVF_UINT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R16G16B16A16_SNORM,           16, CONVF_SNORM | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R16G16B16A16_SINT,            16, CONVF_SINT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R32G32_FLOAT,                 32, CONVF_FLOAT | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R32G32_UINT,                  32, CONVF_UINT | CONVF_R | CONVF_G  },
		{ DXGI_FORMAT_R32G32_SINT,                  32, CONVF_SINT | CONVF_R | CONVF_G  },
		{ DXGI_FORMAT_D32_FLOAT_S8X24_UINT,         32, CONVF_FLOAT | CONVF_DEPTH | CONVF_STENCIL },
		{ DXGI_FORMAT_R10G10B10A2_UNORM,            10, CONVF_UNORM | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R10G10B10A2_UINT,             10, CONVF_UINT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R11G11B10_FLOAT,              10, CONVF_FLOAT | CONVF_POS_ONLY | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_R8G8B8A8_UNORM,                8, CONVF_UNORM | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,           8, CONVF_UNORM | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R8G8B8A8_UINT,                 8, CONVF_UINT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R8G8B8A8_SNORM,                8, CONVF_SNORM | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R8G8B8A8_SINT,                 8, CONVF_SINT | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_R16G16_FLOAT,                 16, CONVF_FLOAT | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R16G16_UNORM,                 16, CONVF_UNORM | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R16G16_UINT,                  16, CONVF_UINT | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R16G16_SNORM,                 16, CONVF_SNORM | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R16G16_SINT,                  16, CONVF_SINT | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_D32_FLOAT,                    32, CONVF_FLOAT | CONVF_DEPTH },
		{ DXGI_FORMAT_R32_FLOAT,                    32, CONVF_FLOAT | CONVF_R },
		{ DXGI_FORMAT_R32_UINT,                     32, CONVF_UINT | CONVF_R },
		{ DXGI_FORMAT_R32_SINT,                     32, CONVF_SINT | CONVF_R },
		{ DXGI_FORMAT_D24_UNORM_S8_UINT,            32, CONVF_UNORM | CONVF_DEPTH | CONVF_STENCIL },
		{ DXGI_FORMAT_R8G8_UNORM,                    8, CONVF_UNORM | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R8G8_UINT,                     8, CONVF_UINT | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R8G8_SNORM,                    8, CONVF_SNORM | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R8G8_SINT,                     8, CONVF_SINT | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_R16_FLOAT,                    16, CONVF_FLOAT | CONVF_R },
		{ DXGI_FORMAT_D16_UNORM,                    16, CONVF_UNORM | CONVF_DEPTH },
		{ DXGI_FORMAT_R16_UNORM,                    16, CONVF_UNORM | CONVF_R },
		{ DXGI_FORMAT_R16_UINT,                     16, CONVF_UINT | CONVF_R },
		{ DXGI_FORMAT_R16_SNORM,                    16, CONVF_SNORM | CONVF_R },
		{ DXGI_FORMAT_R16_SINT,                     16, CONVF_SINT | CONVF_R },
		{ DXGI_FORMAT_R8_UNORM,                      8, CONVF_UNORM | CONVF_R },
		{ DXGI_FORMAT_R8_UINT,                       8, CONVF_UINT | CONVF_R },
		{ DXGI_FORMAT_R8_SNORM,                      8, CONVF_SNORM | CONVF_R },
		{ DXGI_FORMAT_R8_SINT,                       8, CONVF_SINT | CONVF_R },
		{ DXGI_FORMAT_A8_UNORM,                      8, CONVF_UNORM | CONVF_A },
		{ DXGI_FORMAT_R1_UNORM,                      1, CONVF_UNORM | CONVF_R },
		{ DXGI_FORMAT_R9G9B9E5_SHAREDEXP,            9, CONVF_FLOAT | CONVF_SHAREDEXP | CONVF_POS_ONLY | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_R8G8_B8G8_UNORM,               8, CONVF_UNORM | CONVF_PACKED | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_G8R8_G8B8_UNORM,               8, CONVF_UNORM | CONVF_PACKED | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_BC1_UNORM,                     8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC1_UNORM_SRGB,                8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC2_UNORM,                     8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC2_UNORM_SRGB,                8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC3_UNORM,                     8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC3_UNORM_SRGB,                8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC4_UNORM,                     8, CONVF_UNORM | CONVF_BC | CONVF_R },
		{ DXGI_FORMAT_BC4_SNORM,                     8, CONVF_SNORM | CONVF_BC | CONVF_R },
		{ DXGI_FORMAT_BC5_UNORM,                     8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_BC5_SNORM,                     8, CONVF_SNORM | CONVF_BC | CONVF_R | CONVF_G },
		{ DXGI_FORMAT_B5G6R5_UNORM,                  5, CONVF_UNORM | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_B5G5R5A1_UNORM,                5, CONVF_UNORM | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_B8G8R8A8_UNORM,                8, CONVF_UNORM | CONVF_BGR | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_B8G8R8X8_UNORM,                8, CONVF_UNORM | CONVF_BGR | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,   10, CONVF_UNORM | CONVF_XR | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,           8, CONVF_UNORM | CONVF_BGR | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,           8, CONVF_UNORM | CONVF_BGR | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_BC6H_UF16,                    16, CONVF_FLOAT | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC6H_SF16,                    16, CONVF_FLOAT | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC7_UNORM,                     8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_BC7_UNORM_SRGB,                8, CONVF_UNORM | CONVF_BC | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_AYUV,                          8, CONVF_UNORM | CONVF_YUV | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_Y410,                         10, CONVF_UNORM | CONVF_YUV | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_Y416,                         16, CONVF_UNORM | CONVF_YUV | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ DXGI_FORMAT_YUY2,                          8, CONVF_UNORM | CONVF_YUV | CONVF_PACKED | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_Y210,                         10, CONVF_UNORM | CONVF_YUV | CONVF_PACKED | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_Y216,                         16, CONVF_UNORM | CONVF_YUV | CONVF_PACKED | CONVF_R | CONVF_G | CONVF_B },
		{ DXGI_FORMAT_B4G4R4A4_UNORM,                4, CONVF_UNORM | CONVF_BGR | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ XBOX_DXGI_FORMAT_R10G10B10_7E3_A2_FLOAT,  10, CONVF_FLOAT | CONVF_POS_ONLY | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ XBOX_DXGI_FORMAT_R10G10B10_6E4_A2_FLOAT,  10, CONVF_FLOAT | CONVF_POS_ONLY | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ XBOX_DXGI_FORMAT_R10G10B10_SNORM_A2_UNORM,10, CONVF_SNORM | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
		{ XBOX_DXGI_FORMAT_R4G4_UNORM,               4, CONVF_UNORM | CONVF_R | CONVF_G },
		{ WIN11_DXGI_FORMAT_A4B4G4R4_UNORM,          4, CONVF_UNORM | CONVF_BGR | CONVF_R | CONVF_G | CONVF_B | CONVF_A },
	};

#pragma prefast( suppress : 25004, "Signature must match bsearch" );
	int __cdecl ConvertCompare(const void* ptr1, const void *ptr2) noexcept
	{
		auto p1 = static_cast<const ConvertData*>(ptr1);
		auto p2 = static_cast<const ConvertData*>(ptr2);
		if (p1->format == p2->format) return 0;
		else return (p1->format < p2->format) ? -1 : 1;
	}

	const XMVECTORF32 g_Grayscale = { { { 0.2125f, 0.7154f, 0.0721f, 0.0f } } };

}

_Use_decl_annotations_
uint32_t DirectX::Internal::GetConvertFlags(DXGI_FORMAT format) noexcept
{
#ifdef _DEBUG
	// Ensure conversion table is in ascending order
	assert(std::size(g_ConvertTable) > 0);
	DXGI_FORMAT lastvalue = g_ConvertTable[0].format;
	for (size_t index = 1; index < std::size(g_ConvertTable); ++index)
	{
		assert(g_ConvertTable[index].format > lastvalue);
		lastvalue = g_ConvertTable[index].format;
	}
#endif

	ConvertData key = { format, 0, 0 };
	auto in = reinterpret_cast<const ConvertData*>(bsearch(&key, g_ConvertTable, std::size(g_ConvertTable), sizeof(ConvertData),
		ConvertCompare));
	return (in) ? in->flags : 0;
}

_Use_decl_annotations_
void DirectX::Internal::ConvertScanline(
	XMVECTOR* pBuffer,
	size_t count,
	DXGI_FORMAT outFormat,
	DXGI_FORMAT inFormat,
	TEX_FILTER_FLAGS flags) noexcept
{
	assert(pBuffer && count > 0 && ((reinterpret_cast<uintptr_t>(pBuffer) & 0xF) == 0));
	assert(IsValid(outFormat) && !IsTypeless(outFormat) && !IsPlanar(outFormat) && !IsPalettized(outFormat));
	assert(IsValid(inFormat) && !IsTypeless(inFormat) && !IsPlanar(inFormat) && !IsPalettized(inFormat));

	if (!pBuffer)
		return;

#ifdef _DEBUG
	// Ensure conversion table is in ascending order
	assert(std::size(g_ConvertTable) > 0);
	DXGI_FORMAT lastvalue = g_ConvertTable[0].format;
	for (size_t index = 1; index < std::size(g_ConvertTable); ++index)
	{
		assert(g_ConvertTable[index].format > lastvalue);
		lastvalue = g_ConvertTable[index].format;
	}
#endif

	// Determine conversion details about source and dest formats
	ConvertData key = { inFormat, 0, 0 };
	auto in = reinterpret_cast<const ConvertData*>(
		bsearch(&key, g_ConvertTable, std::size(g_ConvertTable), sizeof(ConvertData), ConvertCompare));
	key.format = outFormat;
	auto out = reinterpret_cast<const ConvertData*>(
		bsearch(&key, g_ConvertTable, std::size(g_ConvertTable), sizeof(ConvertData), ConvertCompare));
	if (!in || !out)
	{
		assert(false);
		return;
	}

	assert(GetConvertFlags(inFormat) == in->flags);
	assert(GetConvertFlags(outFormat) == out->flags);

	// Handle SRGB filtering modes
	switch (inFormat)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		flags |= TEX_FILTER_SRGB_IN;
		break;

	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
		flags &= ~TEX_FILTER_SRGB_IN;
		break;

	default:
		break;
	}

	switch (outFormat)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		flags |= TEX_FILTER_SRGB_OUT;
		break;

	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
		flags &= ~TEX_FILTER_SRGB_OUT;
		break;

	default:
		break;
	}

	if ((flags & (TEX_FILTER_SRGB_IN | TEX_FILTER_SRGB_OUT)) == (TEX_FILTER_SRGB_IN | TEX_FILTER_SRGB_OUT))
	{
		flags &= ~(TEX_FILTER_SRGB_IN | TEX_FILTER_SRGB_OUT);
	}

	// sRGB input processing (sRGB -> Linear RGB)
	if (flags & TEX_FILTER_SRGB_IN)
	{
		if (!(in->flags & CONVF_DEPTH) && ((in->flags & CONVF_FLOAT) || (in->flags & CONVF_UNORM)))
		{
			XMVECTOR* ptr = pBuffer;
			for (size_t i = 0; i < count; ++i, ++ptr)
			{
				*ptr = XMColorSRGBToRGB(*ptr);
			}
		}
	}

	// Handle conversion special cases
	const uint32_t diffFlags = in->flags ^ out->flags;
	if (diffFlags != 0)
	{
		if (diffFlags & CONVF_DEPTH)
		{
			//--- Depth conversions ---
			if (in->flags & CONVF_DEPTH)
			{
				// CONVF_DEPTH -> !CONVF_DEPTH
				if (in->flags & CONVF_STENCIL)
				{
					// Stencil -> Alpha
					static const XMVECTORF32 S = { { { 1.f, 1.f, 1.f, 255.f } } };

					if (out->flags & CONVF_UNORM)
					{
						// UINT -> UNORM
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							XMVECTOR v1 = XMVectorSplatY(v);
							v1 = XMVectorClamp(v1, g_XMZero, S);
							v1 = XMVectorDivide(v1, S);
							*ptr++ = XMVectorSelect(v1, v, g_XMSelect1110);
						}
					}
					else if (out->flags & CONVF_SNORM)
					{
						// UINT -> SNORM
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							XMVECTOR v1 = XMVectorSplatY(v);
							v1 = XMVectorClamp(v1, g_XMZero, S);
							v1 = XMVectorDivide(v1, S);
							v1 = XMVectorMultiplyAdd(v1, g_XMTwo, g_XMNegativeOne);
							*ptr++ = XMVectorSelect(v1, v, g_XMSelect1110);
						}
					}
					else
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorSplatY(v);
							*ptr++ = XMVectorSelect(v1, v, g_XMSelect1110);
						}
					}
				}

				// Depth -> RGB
				if ((out->flags & CONVF_UNORM) && (in->flags & CONVF_FLOAT))
				{
					// Depth FLOAT -> UNORM
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						XMVECTOR v1 = XMVectorSaturate(v);
						v1 = XMVectorSplatX(v1);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
					}
				}
				else if (out->flags & CONVF_SNORM)
				{
					if (in->flags & CONVF_UNORM)
					{
						// Depth UNORM -> SNORM
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							XMVECTOR v1 = XMVectorMultiplyAdd(v, g_XMTwo, g_XMNegativeOne);
							v1 = XMVectorSplatX(v1);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
						}
					}
					else
					{
						// Depth FLOAT -> SNORM
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							XMVECTOR v1 = XMVectorClamp(v, g_XMNegativeOne, g_XMOne);
							v1 = XMVectorSplatX(v1);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
						}
					}
				}
				else
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSplatX(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
					}
				}
			}
			else
			{
				// !CONVF_DEPTH -> CONVF_DEPTH

				// RGB -> Depth (red channel or other specified channel)
				switch (flags & (TEX_FILTER_RGB_COPY_RED | TEX_FILTER_RGB_COPY_GREEN | TEX_FILTER_RGB_COPY_BLUE | TEX_FILTER_RGB_COPY_ALPHA))
				{
				case TEX_FILTER_RGB_COPY_GREEN:
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSplatY(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1000);
					}
				}
				break;

				case TEX_FILTER_RGB_COPY_BLUE:
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSplatZ(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1000);
					}
				}
				break;

				case TEX_FILTER_RGB_COPY_ALPHA:
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSplatW(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1000);
					}
				}
				break;

				default:
					if ((in->flags & CONVF_UNORM) && ((in->flags & CONVF_RGB_MASK) == (CONVF_R | CONVF_G | CONVF_B)))
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVector3Dot(v, g_Grayscale);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1000);
						}
						break;
					}

#if (__cplusplus >= 201703L)
					[[fallthrough]];
#elif defined(__clang__)
					[[clang::fallthrough]];
#elif defined(_MSC_VER)
					__fallthrough;
#endif

				case TEX_FILTER_RGB_COPY_RED:
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSplatX(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1000);
					}
				}
				break;
				}

				// Finialize type conversion for depth (red channel)
				if (out->flags & CONVF_UNORM)
				{
					if (in->flags & CONVF_SNORM)
					{
						// SNORM -> UNORM
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorMultiplyAdd(v, g_XMOneHalf, g_XMOneHalf);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1000);
						}
					}
					else if (in->flags & CONVF_FLOAT)
					{
						// FLOAT -> UNORM
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorSaturate(v);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1000);
						}
					}
				}

				if (out->flags & CONVF_STENCIL)
				{
					// Alpha -> Stencil (green channel)
					static const XMVECTORU32 select0100 = { { { XM_SELECT_0, XM_SELECT_1, XM_SELECT_0, XM_SELECT_0 } } };
					static const XMVECTORF32 S = { { { 255.f, 255.f, 255.f, 255.f } } };

					if (in->flags & CONVF_UNORM)
					{
						// UNORM -> UINT
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							XMVECTOR v1 = XMVectorMultiply(v, S);
							v1 = XMVectorSplatW(v1);
							*ptr++ = XMVectorSelect(v, v1, select0100);
						}
					}
					else if (in->flags & CONVF_SNORM)
					{
						// SNORM -> UINT
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							XMVECTOR v1 = XMVectorMultiplyAdd(v, g_XMOneHalf, g_XMOneHalf);
							v1 = XMVectorMultiply(v1, S);
							v1 = XMVectorSplatW(v1);
							*ptr++ = XMVectorSelect(v, v1, select0100);
						}
					}
					else
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorSplatW(v);
							*ptr++ = XMVectorSelect(v, v1, select0100);
						}
					}
				}
			}
		}
		else if (out->flags & CONVF_DEPTH)
		{
			// CONVF_DEPTH -> CONVF_DEPTH
			if (diffFlags & CONVF_FLOAT)
			{
				if (in->flags & CONVF_FLOAT)
				{
					// FLOAT -> UNORM depth, preserve stencil
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSaturate(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1000);
					}
				}
			}
		}
		else if (out->flags & CONVF_UNORM)
		{
			//--- Converting to a UNORM ---
			if (in->flags & CONVF_SNORM)
			{
				// SNORM -> UNORM
				XMVECTOR* ptr = pBuffer;
				for (size_t i = 0; i < count; ++i)
				{
					const XMVECTOR v = *ptr;
					*ptr++ = XMVectorMultiplyAdd(v, g_XMOneHalf, g_XMOneHalf);
				}
			}
			else if (in->flags & CONVF_FLOAT)
			{
				XMVECTOR* ptr = pBuffer;
				if (!(in->flags & CONVF_POS_ONLY) && (flags & TEX_FILTER_FLOAT_X2BIAS))
				{
					// FLOAT -> UNORM (x2 bias)
					for (size_t i = 0; i < count; ++i)
					{
						XMVECTOR v = *ptr;
						v = XMVectorClamp(v, g_XMNegativeOne, g_XMOne);
						*ptr++ = XMVectorMultiplyAdd(v, g_XMOneHalf, g_XMOneHalf);
					}
				}
				else
				{
					// FLOAT -> UNORM
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						*ptr++ = XMVectorSaturate(v);
					}
				}
			}
		}
		else if (out->flags & CONVF_SNORM)
		{
			//--- Converting to a SNORM ---
			if (in->flags & CONVF_UNORM)
			{
				// UNORM -> SNORM
				XMVECTOR* ptr = pBuffer;
				for (size_t i = 0; i < count; ++i)
				{
					const XMVECTOR v = *ptr;
					*ptr++ = XMVectorMultiplyAdd(v, g_XMTwo, g_XMNegativeOne);
				}
			}
			else if (in->flags & CONVF_FLOAT)
			{
				XMVECTOR* ptr = pBuffer;
				if ((in->flags & CONVF_POS_ONLY) && (flags & TEX_FILTER_FLOAT_X2BIAS))
				{
					// FLOAT (positive only, x2 bias) -> SNORM
					for (size_t i = 0; i < count; ++i)
					{
						XMVECTOR v = *ptr;
						v = XMVectorSaturate(v);
						*ptr++ = XMVectorMultiplyAdd(v, g_XMTwo, g_XMNegativeOne);
					}
				}
				else
				{
					// FLOAT -> SNORM
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						*ptr++ = XMVectorClamp(v, g_XMNegativeOne, g_XMOne);
					}
				}
			}
		}
		else if (diffFlags & CONVF_UNORM)
		{
			//--- Converting from a UNORM ---
			assert(in->flags & CONVF_UNORM);
			if (out->flags & CONVF_FLOAT)
			{
				if (!(out->flags & CONVF_POS_ONLY) && (flags & TEX_FILTER_FLOAT_X2BIAS))
				{
					// UNORM (x2 bias) -> FLOAT
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						*ptr++ = XMVectorMultiplyAdd(v, g_XMTwo, g_XMNegativeOne);
					}
				}
			}
		}
		else if (diffFlags & CONVF_POS_ONLY)
		{
			if (flags & TEX_FILTER_FLOAT_X2BIAS)
			{
				if (in->flags & CONVF_POS_ONLY)
				{
					if (out->flags & CONVF_FLOAT)
					{
						// FLOAT (positive only, x2 bias) -> FLOAT
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							XMVECTOR v = *ptr;
							v = XMVectorSaturate(v);
							*ptr++ = XMVectorMultiplyAdd(v, g_XMTwo, g_XMNegativeOne);
						}
					}
				}
				else if (out->flags & CONVF_POS_ONLY)
				{
					if (in->flags & CONVF_FLOAT)
					{
						// FLOAT -> FLOAT (positive only, x2 bias)
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							XMVECTOR v = *ptr;
							v = XMVectorClamp(v, g_XMNegativeOne, g_XMOne);
							*ptr++ = XMVectorMultiplyAdd(v, g_XMOneHalf, g_XMOneHalf);
						}
					}
					else if (in->flags & CONVF_SNORM)
					{
						// SNORM -> FLOAT (positive only, x2 bias)
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							*ptr++ = XMVectorMultiplyAdd(v, g_XMOneHalf, g_XMOneHalf);
						}
					}
				}
			}
		}

		// !CONVF_A -> CONVF_A is handled because LoadScanline ensures alpha defaults to 1.0 for no-alpha formats

		// CONVF_PACKED cases are handled because LoadScanline/StoreScanline handles packing/unpacking

		if (((out->flags & CONVF_RGBA_MASK) == CONVF_A) && !(in->flags & CONVF_A))
		{
			// !CONVF_A -> A format
			// We ignore TEX_FILTER_RGB_COPY_ALPHA since there's no input alpha channel.
			switch (flags & (TEX_FILTER_RGB_COPY_RED | TEX_FILTER_RGB_COPY_GREEN | TEX_FILTER_RGB_COPY_BLUE))
			{
			case TEX_FILTER_RGB_COPY_GREEN:
			{
				XMVECTOR* ptr = pBuffer;
				for (size_t i = 0; i < count; ++i)
				{
					const XMVECTOR v = *ptr;
					*ptr++ = XMVectorSplatY(v);
				}
			}
			break;

			case TEX_FILTER_RGB_COPY_BLUE:
			{
				XMVECTOR* ptr = pBuffer;
				for (size_t i = 0; i < count; ++i)
				{
					const XMVECTOR v = *ptr;
					*ptr++ = XMVectorSplatZ(v);
				}
			}
			break;

			default:
				if ((in->flags & CONVF_UNORM) && ((in->flags & CONVF_RGB_MASK) == (CONVF_R | CONVF_G | CONVF_B)))
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						*ptr++ = XMVector3Dot(v, g_Grayscale);
					}
					break;
				}

#if (__cplusplus >= 201703L)
				[[fallthrough]];
#elif defined(__clang__)
				[[clang::fallthrough]];
#elif defined(_MSC_VER)
				__fallthrough;
#endif

			case TEX_FILTER_RGB_COPY_RED:
			{
				XMVECTOR* ptr = pBuffer;
				for (size_t i = 0; i < count; ++i)
				{
					const XMVECTOR v = *ptr;
					*ptr++ = XMVectorSplatX(v);
				}
			}
			break;
			}
		}
		else if (((in->flags & CONVF_RGBA_MASK) == CONVF_A) && !(out->flags & CONVF_A))
		{
			// A format -> !CONVF_A
			XMVECTOR* ptr = pBuffer;
			for (size_t i = 0; i < count; ++i)
			{
				const XMVECTOR v = *ptr;
				*ptr++ = XMVectorSplatW(v);
			}
		}
		else if ((in->flags & CONVF_RGB_MASK) == CONVF_R)
		{
			if ((out->flags & CONVF_RGB_MASK) == (CONVF_R | CONVF_G | CONVF_B))
			{
				// R format -> RGB format
				XMVECTOR* ptr = pBuffer;
				for (size_t i = 0; i < count; ++i)
				{
					const XMVECTOR v = *ptr;
					const XMVECTOR v1 = XMVectorSplatX(v);
					*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
				}
			}
			else if ((out->flags & CONVF_RGB_MASK) == (CONVF_R | CONVF_G))
			{
				// R format -> RG format
				XMVECTOR* ptr = pBuffer;
				for (size_t i = 0; i < count; ++i)
				{
					const XMVECTOR v = *ptr;
					const XMVECTOR v1 = XMVectorSplatX(v);
					*ptr++ = XMVectorSelect(v, v1, g_XMSelect1100);
				}
			}
		}
		else if ((in->flags & CONVF_RGB_MASK) == (CONVF_R | CONVF_G | CONVF_B))
		{
			if ((out->flags & CONVF_RGB_MASK) == CONVF_R)
			{
				// RGB(A) format -> R format
				switch (flags & (TEX_FILTER_RGB_COPY_RED | TEX_FILTER_RGB_COPY_GREEN | TEX_FILTER_RGB_COPY_BLUE | TEX_FILTER_RGB_COPY_ALPHA))
				{
				case TEX_FILTER_RGB_COPY_GREEN:
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSplatY(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
					}
				}
				break;

				case TEX_FILTER_RGB_COPY_BLUE:
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSplatZ(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
					}
				}
				break;

				case TEX_FILTER_RGB_COPY_ALPHA:
				{
					XMVECTOR* ptr = pBuffer;
					for (size_t i = 0; i < count; ++i)
					{
						const XMVECTOR v = *ptr;
						const XMVECTOR v1 = XMVectorSplatW(v);
						*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
					}
				}
				break;

				default:
					if (in->flags & CONVF_UNORM)
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVector3Dot(v, g_Grayscale);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1110);
						}
						break;
					}

#if (__cplusplus >= 201703L)
					[[fallthrough]];
#elif defined(__clang__)
					[[clang::fallthrough]];
#elif defined(_MSC_VER)
					__fallthrough;
#endif

				case TEX_FILTER_RGB_COPY_RED:
					// Leave data unchanged and the store will handle this...
					break;
				}
			}
			else if ((out->flags & CONVF_RGB_MASK) == (CONVF_R | CONVF_G))
			{
				if ((flags & TEX_FILTER_RGB_COPY_ALPHA) && (in->flags & CONVF_A))
				{
					// RGBA -> RG format
					switch (static_cast<int>(flags & (TEX_FILTER_RGB_COPY_RED | TEX_FILTER_RGB_COPY_GREEN | TEX_FILTER_RGB_COPY_BLUE | TEX_FILTER_RGB_COPY_ALPHA)))
					{
					case (static_cast<int>(TEX_FILTER_RGB_COPY_RED) | static_cast<int>(TEX_FILTER_RGB_COPY_ALPHA)):
					default:
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorSwizzle<0, 3, 0, 3>(v);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1100);
						}
					}
					break;

					case (static_cast<int>(TEX_FILTER_RGB_COPY_GREEN) | static_cast<int>(TEX_FILTER_RGB_COPY_ALPHA)):
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorSwizzle<1, 3, 1, 3>(v);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1100);
						}
					}
					break;

					case (static_cast<int>(TEX_FILTER_RGB_COPY_BLUE) | static_cast<int>(TEX_FILTER_RGB_COPY_ALPHA)):
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorSwizzle<2, 3, 2, 3>(v);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1100);
						}
					}
					break;
					}
				}
				else
				{
					// RGB format -> RG format
					switch (static_cast<int>(flags & (TEX_FILTER_RGB_COPY_RED | TEX_FILTER_RGB_COPY_GREEN | TEX_FILTER_RGB_COPY_BLUE)))
					{
					case (static_cast<int>(TEX_FILTER_RGB_COPY_RED) | static_cast<int>(TEX_FILTER_RGB_COPY_BLUE)):
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorSwizzle<0, 2, 0, 2>(v);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1100);
						}
					}
					break;

					case (static_cast<int>(TEX_FILTER_RGB_COPY_GREEN) | static_cast<int>(TEX_FILTER_RGB_COPY_BLUE)):
					{
						XMVECTOR* ptr = pBuffer;
						for (size_t i = 0; i < count; ++i)
						{
							const XMVECTOR v = *ptr;
							const XMVECTOR v1 = XMVectorSwizzle<1, 2, 3, 0>(v);
							*ptr++ = XMVectorSelect(v, v1, g_XMSelect1100);
						}
					}
					break;

					case (static_cast<int>(TEX_FILTER_RGB_COPY_RED) | static_cast<int>(TEX_FILTER_RGB_COPY_GREEN)):
					default:
						// Leave data unchanged and the store will handle this...
						break;
					}
				}
			}
		}
	}

	// sRGB output processing (Linear RGB -> sRGB)
	if (flags & TEX_FILTER_SRGB_OUT)
	{
		if (!(out->flags & CONVF_DEPTH) && ((out->flags & CONVF_FLOAT) || (out->flags & CONVF_UNORM)))
		{
			XMVECTOR* ptr = pBuffer;
			for (size_t i = 0; i < count; ++i, ++ptr)
			{
				*ptr = XMColorRGBToSRGB(*ptr);
			}
		}
	}
}

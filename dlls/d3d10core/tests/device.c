/*
 * Copyright 2008 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS
#include "initguid.h"
#include "d3d11.h"
#include "wine/test.h"
#include <limits.h>

struct vec2
{
    float x, y;
};

struct vec3
{
    float x, y, z;
};

struct vec4
{
    float x, y, z, w;
};

static void set_box(D3D10_BOX *box, UINT left, UINT top, UINT front, UINT right, UINT bottom, UINT back)
{
    box->left = left;
    box->top = top;
    box->front = front;
    box->right = right;
    box->bottom = bottom;
    box->back = back;
}

static ULONG get_refcount(IUnknown *iface)
{
    IUnknown_AddRef(iface);
    return IUnknown_Release(iface);
}

static BOOL compare_float(float f, float g, unsigned int ulps)
{
    int x = *(int *)&f;
    int y = *(int *)&g;

    if (x < 0)
        x = INT_MIN - x;
    if (y < 0)
        y = INT_MIN - y;

    if (abs(x - y) > ulps)
        return FALSE;

    return TRUE;
}

static BOOL compare_color(DWORD c1, DWORD c2, BYTE max_diff)
{
    if (abs((c1 & 0xff) - (c2 & 0xff)) > max_diff)
        return FALSE;
    c1 >>= 8; c2 >>= 8;
    if (abs((c1 & 0xff) - (c2 & 0xff)) > max_diff)
        return FALSE;
    c1 >>= 8; c2 >>= 8;
    if (abs((c1 & 0xff) - (c2 & 0xff)) > max_diff)
        return FALSE;
    c1 >>= 8; c2 >>= 8;
    if (abs((c1 & 0xff) - (c2 & 0xff)) > max_diff)
        return FALSE;
    return TRUE;
}

struct texture_readback
{
    ID3D10Texture2D *texture;
    D3D10_MAPPED_TEXTURE2D mapped_texture;
};

static void get_texture_readback(ID3D10Texture2D *texture, struct texture_readback *rb)
{
    D3D10_TEXTURE2D_DESC texture_desc;
    ID3D10Device *device;
    HRESULT hr;

    memset(rb, 0, sizeof(*rb));

    ID3D10Texture2D_GetDevice(texture, &device);

    ID3D10Texture2D_GetDesc(texture, &texture_desc);
    texture_desc.Usage = D3D10_USAGE_STAGING;
    texture_desc.BindFlags = 0;
    texture_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    texture_desc.MiscFlags = 0;
    if (FAILED(hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &rb->texture)))
    {
        trace("Failed to create texture, hr %#x.\n", hr);
        ID3D10Device_Release(device);
        return;
    }

    ID3D10Device_CopyResource(device, (ID3D10Resource *)rb->texture, (ID3D10Resource *)texture);
    if (FAILED(hr = ID3D10Texture2D_Map(rb->texture, 0, D3D10_MAP_READ, 0, &rb->mapped_texture)))
    {
        trace("Failed to map texture, hr %#x.\n", hr);
        ID3D10Texture2D_Release(rb->texture);
        rb->texture = NULL;
    }

    ID3D10Device_Release(device);
}

static DWORD get_readback_color(struct texture_readback *rb, unsigned int x, unsigned int y)
{
    return rb->texture
            ? ((DWORD *)rb->mapped_texture.pData)[rb->mapped_texture.RowPitch * y / sizeof(DWORD)  + x] : 0xdeadbeef;
}

static void release_texture_readback(struct texture_readback *rb)
{
    if (!rb->texture)
        return;

    ID3D10Texture2D_Unmap(rb->texture, 0);
    ID3D10Texture2D_Release(rb->texture);
}

static DWORD get_texture_color(ID3D10Texture2D *texture, unsigned int x, unsigned int y)
{
    struct texture_readback rb;
    DWORD color;

    get_texture_readback(texture, &rb);
    color = get_readback_color(&rb, x, y);
    release_texture_readback(&rb);

    return color;
}

static ID3D10Device *create_device(void)
{
    ID3D10Device *device;

    if (SUCCEEDED(D3D10CreateDevice(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_SDK_VERSION, &device)))
        return device;
    if (SUCCEEDED(D3D10CreateDevice(NULL, D3D10_DRIVER_TYPE_WARP, NULL, 0, D3D10_SDK_VERSION, &device)))
        return device;
    if (SUCCEEDED(D3D10CreateDevice(NULL, D3D10_DRIVER_TYPE_REFERENCE, NULL, 0, D3D10_SDK_VERSION, &device)))
        return device;

    return NULL;
}

static IDXGISwapChain *create_swapchain(ID3D10Device *device, HWND window, BOOL windowed)
{
    IDXGISwapChain *swapchain;
    DXGI_SWAP_CHAIN_DESC desc;
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *adapter;
    IDXGIFactory *factory;
    HRESULT hr;

    hr = ID3D10Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    ok(SUCCEEDED(hr), "Failed to get DXGI device, hr %#x.\n", hr);
    hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    ok(SUCCEEDED(hr), "Failed to get adapter, hr %#x.\n", hr);
    IDXGIDevice_Release(dxgi_device);
    hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory, (void **)&factory);
    ok(SUCCEEDED(hr), "Failed to get factory, hr %#x.\n", hr);
    IDXGIAdapter_Release(adapter);

    desc.BufferDesc.Width = 640;
    desc.BufferDesc.Height = 480;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = windowed;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    desc.Flags = 0;

    hr = IDXGIFactory_CreateSwapChain(factory, (IUnknown *)device, &desc, &swapchain);
    ok(SUCCEEDED(hr), "Failed to create swapchain, hr %#x.\n", hr);
    IDXGIFactory_Release(factory);

    return swapchain;
}

static void test_feature_level(void)
{
    D3D_FEATURE_LEVEL feature_level;
    ID3D11Device *device11;
    ID3D10Device *device10;
    HRESULT hr;

    if (!(device10 = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    hr = ID3D10Device_QueryInterface(device10, &IID_ID3D11Device, (void **)&device11);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Failed to query ID3D11Device interface, hr %#x.\n", hr);
    if (FAILED(hr))
    {
        win_skip("D3D11 is not available.\n");
        ID3D10Device_Release(device10);
        return;
    }

    /* Device was created by D3D10CreateDevice. */
    feature_level = ID3D11Device_GetFeatureLevel(device11);
    ok(feature_level == D3D_FEATURE_LEVEL_10_0, "Got unexpected feature level %#x.\n", feature_level);

    ID3D11Device_Release(device11);
    ID3D10Device_Release(device10);
}

static void test_device_interfaces(void)
{
    IDXGIAdapter *dxgi_adapter;
    IDXGIDevice *dxgi_device;
    ID3D10Device *device;
    IUnknown *iface;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D10Device_QueryInterface(device, &IID_IUnknown, (void **)&iface);
    ok(SUCCEEDED(hr), "Device should implement IUnknown interface, hr %#x.\n", hr);
    IUnknown_Release(iface);

    hr = ID3D10Device_QueryInterface(device, &IID_IDXGIObject, (void **)&iface);
    ok(SUCCEEDED(hr), "Device should implement IDXGIObject interface, hr %#x.\n", hr);
    IUnknown_Release(iface);

    hr = ID3D10Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    ok(SUCCEEDED(hr), "Device should implement IDXGIDevice.\n");
    hr = IDXGIDevice_GetParent(dxgi_device, &IID_IDXGIAdapter, (void **)&dxgi_adapter);
    ok(SUCCEEDED(hr), "Device parent should implement IDXGIAdapter.\n");
    hr = IDXGIAdapter_GetParent(dxgi_adapter, &IID_IDXGIFactory, (void **)&iface);
    ok(SUCCEEDED(hr), "Adapter parent should implement IDXGIFactory.\n");
    IUnknown_Release(iface);
    IUnknown_Release(dxgi_adapter);
    hr = IDXGIDevice_GetParent(dxgi_device, &IID_IDXGIAdapter1, (void **)&dxgi_adapter);
    ok(SUCCEEDED(hr), "Device parent should implement IDXGIAdapter1.\n");
    hr = IDXGIAdapter_GetParent(dxgi_adapter, &IID_IDXGIFactory1, (void **)&iface);
    ok(hr == E_NOINTERFACE, "Adapter parent should not implement IDXGIFactory1.\n");
    IUnknown_Release(dxgi_adapter);
    IUnknown_Release(dxgi_device);

    hr = ID3D10Device_QueryInterface(device, &IID_IDXGIDevice1, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Device should implement IDXGIDevice1.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    hr = ID3D10Device_QueryInterface(device, &IID_ID3D10Multithread, (void **)&iface);
    ok(SUCCEEDED(hr), "Device should implement ID3D10Multithread interface, hr %#x.\n", hr);
    IUnknown_Release(iface);

    hr = ID3D10Device_QueryInterface(device, &IID_ID3D10Device1, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Device should implement ID3D10Device1 interface, hr %#x.\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    hr = ID3D10Device_QueryInterface(device, &IID_ID3D11Device, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Device should implement ID3D11Device interface, hr %#x.\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_texture2d(void)
{
    ULONG refcount, expected_refcount;
    D3D10_SUBRESOURCE_DATA data = {0};
    ID3D10Device *device, *tmp;
    D3D10_TEXTURE2D_DESC desc;
    ID3D10Texture2D *texture;
    UINT quality_level_count;
    IDXGISurface *surface;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    desc.Width = 512;
    desc.Height = 512;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &desc, &data, &texture);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10Texture2D_GetDevice(texture, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Texture should implement IDXGISurface\n");
    if (SUCCEEDED(hr)) IDXGISurface_Release(surface);
    ID3D10Texture2D_Release(texture);

    desc.MipLevels = 0;
    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10Texture2D_GetDevice(texture, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    ID3D10Texture2D_GetDesc(texture, &desc);
    ok(desc.Width == 512, "Got unexpected Width %u.\n", desc.Width);
    ok(desc.Height == 512, "Got unexpected Height %u.\n", desc.Height);
    ok(desc.MipLevels == 10, "Got unexpected MipLevels %u.\n", desc.MipLevels);
    ok(desc.ArraySize == 1, "Got unexpected ArraySize %u.\n", desc.ArraySize);
    ok(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM, "Got unexpected Format %#x.\n", desc.Format);
    ok(desc.SampleDesc.Count == 1, "Got unexpected SampleDesc.Count %u.\n", desc.SampleDesc.Count);
    ok(desc.SampleDesc.Quality == 0, "Got unexpected SampleDesc.Quality %u.\n", desc.SampleDesc.Quality);
    ok(desc.Usage == D3D10_USAGE_DEFAULT, "Got unexpected Usage %u.\n", desc.Usage);
    ok(desc.BindFlags == D3D10_BIND_RENDER_TARGET, "Got unexpected BindFlags %u.\n", desc.BindFlags);
    ok(desc.CPUAccessFlags == 0, "Got unexpected CPUAccessFlags %u.\n", desc.CPUAccessFlags);
    ok(desc.MiscFlags == 0, "Got unexpected MiscFlags %u.\n", desc.MiscFlags);

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
    ok(FAILED(hr), "Texture should not implement IDXGISurface\n");
    if (SUCCEEDED(hr)) IDXGISurface_Release(surface);
    ID3D10Texture2D_Release(texture);

    desc.MipLevels = 1;
    desc.ArraySize = 2;
    hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x\n", hr);

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
    ok(FAILED(hr), "Texture should not implement IDXGISurface\n");
    if (SUCCEEDED(hr)) IDXGISurface_Release(surface);
    ID3D10Texture2D_Release(texture);

    ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 2, &quality_level_count);
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 2;
    hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &texture);
    if (quality_level_count)
    {
        ok(SUCCEEDED(hr), "Got unexpected hr %#x.\n", hr);
        ID3D10Texture2D_Release(texture);
        desc.SampleDesc.Quality = quality_level_count;
        hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &texture);
    }
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* We assume 15 samples multisampling is never supported in practice. */
    desc.SampleDesc.Count = 15;
    desc.SampleDesc.Quality = 0;
    hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &texture);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_texture2d_interfaces(void)
{
    ID3D11Texture2D *d3d11_texture;
    D3D10_TEXTURE2D_DESC desc;
    ID3D10Texture2D *texture;
    IDXGISurface *surface;
    ID3D10Device *device;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    static const struct test
    {
        UINT bind_flags;
        UINT misc_flags;
        UINT expected_bind_flags;
        UINT expected_misc_flags;
    }
    desc_conversion_tests[] =
    {
        {
            D3D10_BIND_RENDER_TARGET, 0,
            D3D11_BIND_RENDER_TARGET, 0
        },
        {
            0, D3D10_RESOURCE_MISC_SHARED,
            0, D3D11_RESOURCE_MISC_SHARED
        },
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    desc.Width = 512;
    desc.Height = 512;
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x.\n", hr);

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
    ok(hr == E_NOINTERFACE, "Texture should not implement IDXGISurface.\n");

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_ID3D11Texture2D, (void **)&d3d11_texture);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Texture should implement ID3D11Texture2D.\n");
    if (SUCCEEDED(hr)) ID3D11Texture2D_Release(d3d11_texture);
    ID3D10Texture2D_Release(texture);

    if (FAILED(hr))
    {
        win_skip("D3D11 is not available, skipping tests.\n");
        ID3D10Device_Release(device);
        return;
    }

    for (i = 0; i < sizeof(desc_conversion_tests) / sizeof(*desc_conversion_tests); ++i)
    {
        const struct test *current = &desc_conversion_tests[i];
        D3D11_TEXTURE2D_DESC d3d11_desc;
        ID3D11Device *d3d11_device;

        desc.Width = 512;
        desc.Height = 512;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D10_USAGE_DEFAULT;
        desc.BindFlags = current->bind_flags;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = current->misc_flags;

        hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &texture);
        /* Shared resources are not supported by REF and WARP devices. */
        ok(SUCCEEDED(hr) || broken(hr == E_OUTOFMEMORY),
                "Test %u: Failed to create a 2d texture, hr %#x.\n", i, hr);
        if (FAILED(hr))
        {
            win_skip("Failed to create ID3D10Texture2D, skipping test %u.\n", i);
            continue;
        }

        hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
        ok(SUCCEEDED(hr), "Test %u: Texture should implement IDXGISurface.\n", i);
        IDXGISurface_Release(surface);

        hr = ID3D10Texture2D_QueryInterface(texture, &IID_ID3D11Texture2D, (void **)&d3d11_texture);
        ok(SUCCEEDED(hr), "Test %u: Texture should implement ID3D11Texture2D.\n", i);
        ID3D10Texture2D_Release(texture);

        ID3D11Texture2D_GetDesc(d3d11_texture, &d3d11_desc);

        ok(d3d11_desc.Width == desc.Width,
                "Test %u: Got unexpected Width %u.\n", i, d3d11_desc.Width);
        ok(d3d11_desc.Height == desc.Height,
                "Test %u: Got unexpected Height %u.\n", i, d3d11_desc.Height);
        ok(d3d11_desc.MipLevels == desc.MipLevels,
                "Test %u: Got unexpected MipLevels %u.\n", i, d3d11_desc.MipLevels);
        ok(d3d11_desc.ArraySize == desc.ArraySize,
                "Test %u: Got unexpected ArraySize %u.\n", i, d3d11_desc.ArraySize);
        ok(d3d11_desc.Format == desc.Format,
                "Test %u: Got unexpected Format %u.\n", i, d3d11_desc.Format);
        ok(d3d11_desc.SampleDesc.Count == desc.SampleDesc.Count,
                "Test %u: Got unexpected SampleDesc.Count %u.\n", i, d3d11_desc.SampleDesc.Count);
        ok(d3d11_desc.SampleDesc.Quality == desc.SampleDesc.Quality,
                "Test %u: Got unexpected SampleDesc.Quality %u.\n", i, d3d11_desc.SampleDesc.Quality);
        ok(d3d11_desc.Usage == (D3D11_USAGE)desc.Usage,
                "Test %u: Got unexpected Usage %u.\n", i, d3d11_desc.Usage);
        ok(d3d11_desc.BindFlags == current->expected_bind_flags,
                "Test %u: Got unexpected BindFlags %#x.\n", i, d3d11_desc.BindFlags);
        ok(d3d11_desc.CPUAccessFlags == desc.CPUAccessFlags,
                "Test %u: Got unexpected CPUAccessFlags %#x.\n", i, d3d11_desc.CPUAccessFlags);
        ok(d3d11_desc.MiscFlags == current->expected_misc_flags,
                "Test %u: Got unexpected MiscFlags %#x.\n", i, d3d11_desc.MiscFlags);

        d3d11_device = NULL;
        ID3D11Texture2D_GetDevice(d3d11_texture, &d3d11_device);
        ok(!!d3d11_device, "Test %u: Got NULL, expected device pointer.\n", i);
        ID3D11Device_Release(d3d11_device);

        ID3D11Texture2D_Release(d3d11_texture);
    }

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_texture3d(void)
{
    ULONG refcount, expected_refcount;
    D3D10_SUBRESOURCE_DATA data = {0};
    ID3D10Device *device, *tmp;
    D3D10_TEXTURE3D_DESC desc;
    ID3D10Texture3D *texture;
    IDXGISurface *surface;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    desc.Width = 64;
    desc.Height = 64;
    desc.Depth = 64;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture3D(device, &desc, &data, &texture);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateTexture3D(device, &desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 3d texture, hr %#x.\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10Texture3D_GetDevice(texture, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    hr = ID3D10Texture3D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
    ok(FAILED(hr), "Texture should not implement IDXGISurface.\n");
    if (SUCCEEDED(hr)) IDXGISurface_Release(surface);
    ID3D10Texture3D_Release(texture);

    desc.MipLevels = 0;
    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateTexture3D(device, &desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 3d texture, hr %#x.\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10Texture3D_GetDevice(texture, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    ID3D10Texture3D_GetDesc(texture, &desc);
    ok(desc.Width == 64, "Got unexpected Width %u.\n", desc.Width);
    ok(desc.Height == 64, "Got unexpected Height %u.\n", desc.Height);
    ok(desc.Depth == 64, "Got unexpected Depth %u.\n", desc.Depth);
    ok(desc.MipLevels == 7, "Got unexpected MipLevels %u.\n", desc.MipLevels);
    ok(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM, "Got unexpected Format %#x.\n", desc.Format);
    ok(desc.Usage == D3D10_USAGE_DEFAULT, "Got unexpected Usage %u.\n", desc.Usage);
    ok(desc.BindFlags == D3D10_BIND_RENDER_TARGET, "Got unexpected BindFlags %u.\n", desc.BindFlags);
    ok(desc.CPUAccessFlags == 0, "Got unexpected CPUAccessFlags %u.\n", desc.CPUAccessFlags);
    ok(desc.MiscFlags == 0, "Got unexpected MiscFlags %u.\n", desc.MiscFlags);

    hr = ID3D10Texture3D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
    ok(FAILED(hr), "Texture should not implement IDXGISurface.\n");
    if (SUCCEEDED(hr)) IDXGISurface_Release(surface);
    ID3D10Texture3D_Release(texture);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_buffer_interfaces(void)
{
    ID3D11Buffer *d3d11_buffer;
    D3D10_BUFFER_DESC desc;
    ID3D10Buffer *buffer;
    ID3D10Device *device;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    static const struct test
    {
        UINT bind_flags;
        UINT misc_flags;
        UINT expected_bind_flags;
        UINT expected_misc_flags;
    }
    desc_conversion_tests[] =
    {
        {
            D3D10_BIND_VERTEX_BUFFER, 0,
            D3D11_BIND_VERTEX_BUFFER, 0
        },
        {
            D3D10_BIND_INDEX_BUFFER, 0,
            D3D11_BIND_INDEX_BUFFER, 0
        },
        {
            D3D10_BIND_CONSTANT_BUFFER, 0,
            D3D11_BIND_CONSTANT_BUFFER, 0
        },
        {
            D3D10_BIND_SHADER_RESOURCE, 0,
            D3D11_BIND_SHADER_RESOURCE, 0
        },
        {
            D3D10_BIND_STREAM_OUTPUT, 0,
            D3D11_BIND_STREAM_OUTPUT, 0
        },
        {
            D3D10_BIND_RENDER_TARGET, 0,
            D3D11_BIND_RENDER_TARGET, 0
        },
        {
            0, D3D10_RESOURCE_MISC_SHARED,
            0, D3D11_RESOURCE_MISC_SHARED
        },
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    desc.ByteWidth = 1024;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    hr = ID3D10Device_CreateBuffer(device, &desc, NULL, &buffer);
    ok(SUCCEEDED(hr), "Failed to create a buffer, hr %#x.\n", hr);

    hr = ID3D10Buffer_QueryInterface(buffer, &IID_ID3D11Buffer, (void **)&d3d11_buffer);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Buffer should implement ID3D11Buffer.\n");
    if (SUCCEEDED(hr)) ID3D11Buffer_Release(d3d11_buffer);
    ID3D10Buffer_Release(buffer);

    if (FAILED(hr))
    {
        win_skip("D3D11 is not available.\n");
        ID3D10Device_Release(device);
        return;
    }

    for (i = 0; i < sizeof(desc_conversion_tests) / sizeof(*desc_conversion_tests); ++i)
    {
        const struct test *current = &desc_conversion_tests[i];
        D3D11_BUFFER_DESC d3d11_desc;
        ID3D11Device *d3d11_device;

        desc.ByteWidth = 1024;
        desc.Usage = D3D10_USAGE_DEFAULT;
        desc.BindFlags = current->bind_flags;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = current->misc_flags;

        hr = ID3D10Device_CreateBuffer(device, &desc, NULL, &buffer);
        /* Shared resources are not supported by REF and WARP devices. */
        ok(SUCCEEDED(hr) || broken(hr == E_OUTOFMEMORY), "Test %u: Failed to create a buffer, hr %#x.\n", i, hr);
        if (FAILED(hr))
        {
            win_skip("Failed to create a buffer, skipping test %u.\n", i);
            continue;
        }

        hr = ID3D10Buffer_QueryInterface(buffer, &IID_ID3D11Buffer, (void **)&d3d11_buffer);
        ok(SUCCEEDED(hr), "Test %u: Buffer should implement ID3D11Buffer.\n", i);
        ID3D10Buffer_Release(buffer);

        ID3D11Buffer_GetDesc(d3d11_buffer, &d3d11_desc);

        ok(d3d11_desc.ByteWidth == desc.ByteWidth,
                "Test %u: Got unexpected ByteWidth %u.\n", i, d3d11_desc.ByteWidth);
        ok(d3d11_desc.Usage == (D3D11_USAGE)desc.Usage,
                "Test %u: Got unexpected Usage %u.\n", i, d3d11_desc.Usage);
        ok(d3d11_desc.BindFlags == current->expected_bind_flags,
                "Test %u: Got unexpected BindFlags %#x.\n", i, d3d11_desc.BindFlags);
        ok(d3d11_desc.CPUAccessFlags == desc.CPUAccessFlags,
                "Test %u: Got unexpected CPUAccessFlags %#x.\n", i, d3d11_desc.CPUAccessFlags);
        ok(d3d11_desc.MiscFlags == current->expected_misc_flags,
                "Test %u: Got unexpected MiscFlags %#x.\n", i, d3d11_desc.MiscFlags);
        ok(d3d11_desc.StructureByteStride == 0,
                "Test %u: Got unexpected StructureByteStride %u.\n", i, d3d11_desc.StructureByteStride);

        d3d11_device = NULL;
        ID3D11Buffer_GetDevice(d3d11_buffer, &d3d11_device);
        ok(!!d3d11_device, "Test %u: Got NULL, expected device pointer.\n", i);
        ID3D11Device_Release(d3d11_device);

        ID3D11Buffer_Release(d3d11_buffer);
    }

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_depthstencil_view(void)
{
    D3D10_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D10_TEXTURE2D_DESC texture_desc;
    ULONG refcount, expected_refcount;
    ID3D10DepthStencilView *dsview;
    ID3D10Device *device, *tmp;
    ID3D10Texture2D *texture;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    texture_desc.Width = 512;
    texture_desc.Height = 512;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_DEPTH_STENCIL;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x\n", hr);

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateDepthStencilView(device, (ID3D10Resource *)texture, NULL, &dsview);
    ok(SUCCEEDED(hr), "Failed to create a depthstencil view, hr %#x\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10DepthStencilView_GetDevice(dsview, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    ID3D10DepthStencilView_GetDesc(dsview, &dsv_desc);
    ok(dsv_desc.Format == texture_desc.Format, "Got unexpected format %#x.\n", dsv_desc.Format);
    ok(dsv_desc.ViewDimension == D3D10_DSV_DIMENSION_TEXTURE2D,
            "Got unexpected view dimension %#x.\n", dsv_desc.ViewDimension);
    ok(U(dsv_desc).Texture2D.MipSlice == 0, "Got Unexpected mip slice %u.\n", U(dsv_desc).Texture2D.MipSlice);

    ID3D10DepthStencilView_Release(dsview);
    ID3D10Texture2D_Release(texture);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_depthstencil_view_interfaces(void)
{
    D3D11_DEPTH_STENCIL_VIEW_DESC d3d11_dsv_desc;
    D3D10_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    ID3D11DepthStencilView *d3d11_dsview;
    D3D10_TEXTURE2D_DESC texture_desc;
    ID3D10DepthStencilView *dsview;
    ID3D10Texture2D *texture;
    ID3D10Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    texture_desc.Width = 512;
    texture_desc.Height = 512;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_DEPTH_STENCIL;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x.\n", hr);

    dsv_desc.Format = texture_desc.Format;
    dsv_desc.ViewDimension = D3D10_DSV_DIMENSION_TEXTURE2D;
    U(dsv_desc).Texture2D.MipSlice = 0;

    hr = ID3D10Device_CreateDepthStencilView(device, (ID3D10Resource *)texture, &dsv_desc, &dsview);
    ok(SUCCEEDED(hr), "Failed to create a depthstencil view, hr %#x.\n", hr);

    hr = ID3D10DepthStencilView_QueryInterface(dsview, &IID_ID3D11DepthStencilView, (void **)&d3d11_dsview);
    ID3D10DepthStencilView_Release(dsview);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Depth stencil view should implement ID3D11DepthStencilView.\n");

    if (SUCCEEDED(hr))
    {
        ID3D11DepthStencilView_GetDesc(d3d11_dsview, &d3d11_dsv_desc);
        ok(d3d11_dsv_desc.Format == dsv_desc.Format, "Got unexpected format %#x.\n", d3d11_dsv_desc.Format);
        ok(d3d11_dsv_desc.ViewDimension == (D3D11_DSV_DIMENSION)dsv_desc.ViewDimension,
                "Got unexpected view dimension %u.\n", d3d11_dsv_desc.ViewDimension);
        ok(!d3d11_dsv_desc.Flags, "Got unexpected flags %#x.\n", d3d11_dsv_desc.Flags);
        ok(U(d3d11_dsv_desc).Texture2D.MipSlice == U(dsv_desc).Texture2D.MipSlice,
                "Got unexpected mip slice %u.\n", U(d3d11_dsv_desc).Texture2D.MipSlice);

        ID3D11DepthStencilView_Release(d3d11_dsview);
    }
    else
    {
        win_skip("D3D11 is not available.\n");
    }

    ID3D10Texture2D_Release(texture);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_rendertarget_view(void)
{
    D3D10_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D10_SUBRESOURCE_DATA data = {0};
    D3D10_TEXTURE2D_DESC texture_desc;
    ULONG refcount, expected_refcount;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10RenderTargetView *rtview;
    ID3D10Device *device, *tmp;
    ID3D10Texture2D *texture;
    ID3D10Buffer *buffer;
    IUnknown *iface;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    buffer_desc.ByteWidth = 1024;
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &data, &buffer);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, NULL, &buffer);
    ok(SUCCEEDED(hr), "Failed to create a buffer, hr %#x\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10Buffer_GetDevice(buffer, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    rtv_desc.ViewDimension = D3D10_RTV_DIMENSION_BUFFER;
    U(rtv_desc).Buffer.ElementOffset = 0;
    U(rtv_desc).Buffer.ElementWidth = 64;

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)buffer, &rtv_desc, &rtview);
    ok(SUCCEEDED(hr), "Failed to create a rendertarget view, hr %#x\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10RenderTargetView_GetDevice(rtview, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    hr = ID3D10RenderTargetView_QueryInterface(rtview, &IID_ID3D11RenderTargetView, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Render target view should implement ID3D11RenderTargetView.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    ID3D10RenderTargetView_Release(rtview);
    ID3D10Buffer_Release(buffer);

    texture_desc.Width = 512;
    texture_desc.Height = 512;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x\n", hr);

    /* For texture resources it's allowed to specify NULL as desc */
    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)texture, NULL, &rtview);
    ok(SUCCEEDED(hr), "Failed to create a rendertarget view, hr %#x\n", hr);

    ID3D10RenderTargetView_GetDesc(rtview, &rtv_desc);
    ok(rtv_desc.Format == texture_desc.Format, "Expected format %#x, got %#x\n", texture_desc.Format, rtv_desc.Format);
    ok(rtv_desc.ViewDimension == D3D10_RTV_DIMENSION_TEXTURE2D,
            "Expected view dimension D3D10_RTV_DIMENSION_TEXTURE2D, got %#x\n", rtv_desc.ViewDimension);
    ok(U(rtv_desc).Texture2D.MipSlice == 0, "Expected mip slice 0, got %#x\n", U(rtv_desc).Texture2D.MipSlice);

    hr = ID3D10RenderTargetView_QueryInterface(rtview, &IID_ID3D11RenderTargetView, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Render target view should implement ID3D11RenderTargetView.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    ID3D10RenderTargetView_Release(rtview);
    ID3D10Texture2D_Release(texture);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_shader_resource_view(void)
{
    D3D10_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D10_TEXTURE2D_DESC texture_desc;
    ULONG refcount, expected_refcount;
    ID3D10ShaderResourceView *srview;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10Device *device, *tmp;
    ID3D10Texture2D *texture;
    ID3D10Buffer *buffer;
    IUnknown *iface;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    buffer_desc.ByteWidth = 1024;
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, NULL, &buffer);
    ok(SUCCEEDED(hr), "Failed to create a buffer, hr %#x\n", hr);

    hr = ID3D10Device_CreateShaderResourceView(device, (ID3D10Resource *)buffer, NULL, &srview);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D10_SRV_DIMENSION_BUFFER;
    U(srv_desc).Buffer.ElementOffset = 0;
    U(srv_desc).Buffer.ElementWidth = 64;

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateShaderResourceView(device, (ID3D10Resource *)buffer, &srv_desc, &srview);
    ok(SUCCEEDED(hr), "Failed to create a shader resource view, hr %#x\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10ShaderResourceView_GetDevice(srview, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    hr = ID3D10ShaderResourceView_QueryInterface(srview, &IID_ID3D10ShaderResourceView1, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Shader resource view should implement ID3D10ShaderResourceView1.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);
    hr = ID3D10ShaderResourceView_QueryInterface(srview, &IID_ID3D11ShaderResourceView, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Shader resource view should implement ID3D11ShaderResourceView.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    ID3D10ShaderResourceView_Release(srview);
    ID3D10Buffer_Release(buffer);

    texture_desc.Width = 512;
    texture_desc.Height = 512;
    texture_desc.MipLevels = 0;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x\n", hr);

    hr = ID3D10Device_CreateShaderResourceView(device, (ID3D10Resource *)texture, NULL, &srview);
    ok(SUCCEEDED(hr), "Failed to create a shader resource view, hr %#x\n", hr);

    ID3D10ShaderResourceView_GetDesc(srview, &srv_desc);
    ok(srv_desc.Format == texture_desc.Format, "Got unexpected format %#x.\n", srv_desc.Format);
    ok(srv_desc.ViewDimension == D3D10_SRV_DIMENSION_TEXTURE2D,
            "Got unexpected view dimension %#x.\n", srv_desc.ViewDimension);
    ok(U(srv_desc).Texture2D.MostDetailedMip == 0, "Got unexpected MostDetailedMip %u.\n",
            U(srv_desc).Texture2D.MostDetailedMip);
    ok(U(srv_desc).Texture2D.MipLevels == 10, "Got unexpected MipLevels %u.\n", U(srv_desc).Texture2D.MipLevels);

    hr = ID3D10ShaderResourceView_QueryInterface(srview, &IID_ID3D10ShaderResourceView1, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Shader resource view should implement ID3D10ShaderResourceView1.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);
    hr = ID3D10ShaderResourceView_QueryInterface(srview, &IID_ID3D11ShaderResourceView, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Shader resource view should implement ID3D11ShaderResourceView.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    ID3D10ShaderResourceView_Release(srview);
    ID3D10Texture2D_Release(texture);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_shader(void)
{
#if 0
float4 light;
float4x4 mat;

struct input
{
    float4 position : POSITION;
    float3 normal : NORMAL;
};

struct output
{
    float4 position : POSITION;
    float4 diffuse : COLOR;
};

output main(const input v)
{
    output o;

    o.position = mul(v.position, mat);
    o.diffuse = dot((float3)light, v.normal);

    return o;
}
#endif
    static const DWORD vs_4_0[] =
    {
        0x43425844, 0x3ae813ca, 0x0f034b91, 0x790f3226, 0x6b4a718a, 0x00000001, 0x000001c0,
        0x00000003, 0x0000002c, 0x0000007c, 0x000000cc, 0x4e475349, 0x00000048, 0x00000002,
        0x00000008, 0x00000038, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f,
        0x00000041, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x00000707, 0x49534f50,
        0x4e4f4954, 0x524f4e00, 0x004c414d, 0x4e47534f, 0x00000048, 0x00000002, 0x00000008,
        0x00000038, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f, 0x00000041,
        0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x49534f50, 0x4e4f4954,
        0x4c4f4300, 0xab00524f, 0x52444853, 0x000000ec, 0x00010040, 0x0000003b, 0x04000059,
        0x00208e46, 0x00000000, 0x00000005, 0x0300005f, 0x001010f2, 0x00000000, 0x0300005f,
        0x00101072, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2,
        0x00000001, 0x08000011, 0x00102012, 0x00000000, 0x00101e46, 0x00000000, 0x00208e46,
        0x00000000, 0x00000001, 0x08000011, 0x00102022, 0x00000000, 0x00101e46, 0x00000000,
        0x00208e46, 0x00000000, 0x00000002, 0x08000011, 0x00102042, 0x00000000, 0x00101e46,
        0x00000000, 0x00208e46, 0x00000000, 0x00000003, 0x08000011, 0x00102082, 0x00000000,
        0x00101e46, 0x00000000, 0x00208e46, 0x00000000, 0x00000004, 0x08000010, 0x001020f2,
        0x00000001, 0x00208246, 0x00000000, 0x00000000, 0x00101246, 0x00000001, 0x0100003e,
    };

    static const DWORD vs_2_0[] =
    {
        0xfffe0200, 0x002bfffe, 0x42415443, 0x0000001c, 0x00000077, 0xfffe0200, 0x00000002,
        0x0000001c, 0x00000100, 0x00000070, 0x00000044, 0x00040002, 0x00000001, 0x0000004c,
        0x00000000, 0x0000005c, 0x00000002, 0x00000004, 0x00000060, 0x00000000, 0x6867696c,
        0xabab0074, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x0074616d, 0x00030003,
        0x00040004, 0x00000001, 0x00000000, 0x325f7376, 0x4d00305f, 0x6f726369, 0x74666f73,
        0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072,
        0x392e3932, 0x332e3235, 0x00313131, 0x0200001f, 0x80000000, 0x900f0000, 0x0200001f,
        0x80000003, 0x900f0001, 0x03000009, 0xc0010000, 0x90e40000, 0xa0e40000, 0x03000009,
        0xc0020000, 0x90e40000, 0xa0e40001, 0x03000009, 0xc0040000, 0x90e40000, 0xa0e40002,
        0x03000009, 0xc0080000, 0x90e40000, 0xa0e40003, 0x03000008, 0xd00f0000, 0xa0e40004,
        0x90e40001, 0x0000ffff,
    };

    static const DWORD vs_3_0[] =
    {
        0xfffe0300, 0x002bfffe, 0x42415443, 0x0000001c, 0x00000077, 0xfffe0300, 0x00000002,
        0x0000001c, 0x00000100, 0x00000070, 0x00000044, 0x00040002, 0x00000001, 0x0000004c,
        0x00000000, 0x0000005c, 0x00000002, 0x00000004, 0x00000060, 0x00000000, 0x6867696c,
        0xabab0074, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x0074616d, 0x00030003,
        0x00040004, 0x00000001, 0x00000000, 0x335f7376, 0x4d00305f, 0x6f726369, 0x74666f73,
        0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072,
        0x392e3932, 0x332e3235, 0x00313131, 0x0200001f, 0x80000000, 0x900f0000, 0x0200001f,
        0x80000003, 0x900f0001, 0x0200001f, 0x80000000, 0xe00f0000, 0x0200001f, 0x8000000a,
        0xe00f0001, 0x03000009, 0xe0010000, 0x90e40000, 0xa0e40000, 0x03000009, 0xe0020000,
        0x90e40000, 0xa0e40001, 0x03000009, 0xe0040000, 0x90e40000, 0xa0e40002, 0x03000009,
        0xe0080000, 0x90e40000, 0xa0e40003, 0x03000008, 0xe00f0001, 0xa0e40004, 0x90e40001,
        0x0000ffff,
    };

#if 0
float4 main(const float4 color : COLOR) : SV_TARGET
{
    float4 o;

    o = color;

    return o;
}
#endif
    static const DWORD ps_4_0[] =
    {
        0x43425844, 0x4da9446f, 0xfbe1f259, 0x3fdb3009, 0x517521fa, 0x00000001, 0x000001ac,
        0x00000005, 0x00000034, 0x0000008c, 0x000000bc, 0x000000f0, 0x00000130, 0x46454452,
        0x00000050, 0x00000000, 0x00000000, 0x00000000, 0x0000001c, 0xffff0400, 0x00000100,
        0x0000001c, 0x7263694d, 0x666f736f, 0x52282074, 0x4c482029, 0x53204c53, 0x65646168,
        0x6f432072, 0x6c69706d, 0x39207265, 0x2e39322e, 0x2e323539, 0x31313133, 0xababab00,
        0x4e475349, 0x00000028, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000,
        0x00000003, 0x00000000, 0x00000f0f, 0x4f4c4f43, 0xabab0052, 0x4e47534f, 0x0000002c,
        0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x00000038, 0x00000040,
        0x0000000e, 0x03001062, 0x001010f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000000,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e, 0x54415453,
        0x00000074, 0x00000002, 0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000,
        0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000,
    };

#if 0
struct gs_out
{
    float4 pos : SV_POSITION;
};

[maxvertexcount(4)]
void main(point float4 vin[1] : POSITION, inout TriangleStream<gs_out> vout)
{
    float offset = 0.1 * vin[0].w;
    gs_out v;

    v.pos = float4(vin[0].x - offset, vin[0].y - offset, vin[0].z, vin[0].w);
    vout.Append(v);
    v.pos = float4(vin[0].x - offset, vin[0].y + offset, vin[0].z, vin[0].w);
    vout.Append(v);
    v.pos = float4(vin[0].x + offset, vin[0].y - offset, vin[0].z, vin[0].w);
    vout.Append(v);
    v.pos = float4(vin[0].x + offset, vin[0].y + offset, vin[0].z, vin[0].w);
    vout.Append(v);
}
#endif
    static const DWORD gs_4_0[] =
    {
        0x43425844, 0x000ee786, 0xc624c269, 0x885a5cbe, 0x444b3b1f, 0x00000001, 0x0000023c, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x000001a0, 0x00020040,
        0x00000068, 0x0400005f, 0x002010f2, 0x00000001, 0x00000000, 0x02000068, 0x00000001, 0x0100085d,
        0x0100285c, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x0200005e, 0x00000004, 0x0f000032,
        0x00100032, 0x00000000, 0x80201ff6, 0x00000041, 0x00000000, 0x00000000, 0x00004002, 0x3dcccccd,
        0x3dcccccd, 0x00000000, 0x00000000, 0x00201046, 0x00000000, 0x00000000, 0x05000036, 0x00102032,
        0x00000000, 0x00100046, 0x00000000, 0x06000036, 0x001020c2, 0x00000000, 0x00201ea6, 0x00000000,
        0x00000000, 0x01000013, 0x05000036, 0x00102012, 0x00000000, 0x0010000a, 0x00000000, 0x0e000032,
        0x00100052, 0x00000000, 0x00201ff6, 0x00000000, 0x00000000, 0x00004002, 0x3dcccccd, 0x00000000,
        0x3dcccccd, 0x00000000, 0x00201106, 0x00000000, 0x00000000, 0x05000036, 0x00102022, 0x00000000,
        0x0010002a, 0x00000000, 0x06000036, 0x001020c2, 0x00000000, 0x00201ea6, 0x00000000, 0x00000000,
        0x01000013, 0x05000036, 0x00102012, 0x00000000, 0x0010000a, 0x00000000, 0x05000036, 0x00102022,
        0x00000000, 0x0010001a, 0x00000000, 0x06000036, 0x001020c2, 0x00000000, 0x00201ea6, 0x00000000,
        0x00000000, 0x01000013, 0x05000036, 0x00102032, 0x00000000, 0x00100086, 0x00000000, 0x06000036,
        0x001020c2, 0x00000000, 0x00201ea6, 0x00000000, 0x00000000, 0x01000013, 0x0100003e,
    };

    ULONG refcount, expected_refcount;
    ID3D10Device *device, *tmp;
    ID3D10GeometryShader *gs;
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    IUnknown *iface;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    /* vertex shader */
    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateVertexShader(device, vs_4_0, sizeof(vs_4_0), &vs);
    ok(SUCCEEDED(hr), "Failed to create SM4 vertex shader, hr %#x\n", hr);

    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10VertexShader_GetDevice(vs, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    hr = ID3D10VertexShader_QueryInterface(vs, &IID_ID3D11VertexShader, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Vertex shader should implement ID3D11VertexShader.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    refcount = ID3D10VertexShader_Release(vs);
    ok(!refcount, "Vertex shader has %u references left.\n", refcount);

    hr = ID3D10Device_CreateVertexShader(device, vs_2_0, sizeof(vs_2_0), &vs);
    ok(hr == E_INVALIDARG, "Created a SM2 vertex shader, hr %#x\n", hr);

    hr = ID3D10Device_CreateVertexShader(device, vs_3_0, sizeof(vs_3_0), &vs);
    ok(hr == E_INVALIDARG, "Created a SM3 vertex shader, hr %#x\n", hr);

    hr = ID3D10Device_CreateVertexShader(device, ps_4_0, sizeof(ps_4_0), &vs);
    ok(hr == E_INVALIDARG, "Created a SM4 vertex shader from a pixel shader source, hr %#x\n", hr);

    /* pixel shader */
    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreatePixelShader(device, ps_4_0, sizeof(ps_4_0), &ps);
    ok(SUCCEEDED(hr), "Failed to create SM4 vertex shader, hr %#x\n", hr);

    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10PixelShader_GetDevice(ps, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    hr = ID3D10PixelShader_QueryInterface(ps, &IID_ID3D11PixelShader, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Pixel shader should implement ID3D11PixelShader.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    refcount = ID3D10PixelShader_Release(ps);
    ok(!refcount, "Pixel shader has %u references left.\n", refcount);

    /* geometry shader */
    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateGeometryShader(device, gs_4_0, sizeof(gs_4_0), &gs);
    ok(SUCCEEDED(hr), "Failed to create SM4 geometry shader, hr %#x.\n", hr);

    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10GeometryShader_GetDevice(gs, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    hr = ID3D10GeometryShader_QueryInterface(gs, &IID_ID3D11GeometryShader, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Geometry shader should implement ID3D11GeometryShader.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    refcount = ID3D10GeometryShader_Release(gs);
    ok(!refcount, "Geometry shader has %u references left.\n", refcount);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_sampler_state(void)
{
    static const struct test
    {
        D3D10_FILTER filter;
        D3D11_FILTER expected_filter;
    }
    desc_conversion_tests[] =
    {
        {D3D10_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_MIP_POINT},
        {D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR},
        {D3D10_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT, D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT},
        {D3D10_FILTER_MIN_POINT_MAG_MIP_LINEAR, D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR},
        {D3D10_FILTER_MIN_LINEAR_MAG_MIP_POINT, D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT},
        {D3D10_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR, D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR},
        {D3D10_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT},
        {D3D10_FILTER_MIN_MAG_MIP_LINEAR, D3D11_FILTER_MIN_MAG_MIP_LINEAR},
        {D3D10_FILTER_ANISOTROPIC, D3D11_FILTER_ANISOTROPIC},
        {D3D10_FILTER_COMPARISON_MIN_MAG_MIP_POINT, D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT},
        {D3D10_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR, D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR},
        {
            D3D10_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT,
            D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT
        },
        {D3D10_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR, D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR},
        {D3D10_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT, D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT},
        {
            D3D10_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
            D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR
        },
        {D3D10_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT},
        {D3D10_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR},
        {D3D10_FILTER_COMPARISON_ANISOTROPIC, D3D11_FILTER_COMPARISON_ANISOTROPIC},
    };

    ID3D10SamplerState *sampler_state1, *sampler_state2;
    ID3D11SamplerState *d3d11_sampler_state;
    ULONG refcount, expected_refcount;
    ID3D10Device *device, *tmp;
    ID3D11Device *d3d11_device;
    D3D10_SAMPLER_DESC desc;
    unsigned int i;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    hr = ID3D10Device_CreateSamplerState(device, NULL, &sampler_state1);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    desc.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D10_TEXTURE_ADDRESS_WRAP;
    desc.AddressV = D3D10_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D10_TEXTURE_ADDRESS_WRAP;
    desc.MipLODBias = 0.0f;
    desc.MaxAnisotropy = 16;
    desc.ComparisonFunc = D3D10_COMPARISON_ALWAYS;
    desc.BorderColor[0] = 0.0f;
    desc.BorderColor[1] = 1.0f;
    desc.BorderColor[2] = 0.0f;
    desc.BorderColor[3] = 1.0f;
    desc.MinLOD = 0.0f;
    desc.MaxLOD = 16.0f;

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateSamplerState(device, &desc, &sampler_state1);
    ok(SUCCEEDED(hr), "Failed to create sampler state, hr %#x.\n", hr);
    hr = ID3D10Device_CreateSamplerState(device, &desc, &sampler_state2);
    ok(SUCCEEDED(hr), "Failed to create sampler state, hr %#x.\n", hr);
    ok(sampler_state1 == sampler_state2, "Got different sampler state objects.\n");
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10SamplerState_GetDevice(sampler_state1, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    ID3D10SamplerState_GetDesc(sampler_state1, &desc);
    ok(desc.Filter == D3D10_FILTER_MIN_MAG_MIP_LINEAR, "Got unexpected filter %#x.\n", desc.Filter);
    ok(desc.AddressU == D3D10_TEXTURE_ADDRESS_WRAP, "Got unexpected address u %u.\n", desc.AddressU);
    ok(desc.AddressV == D3D10_TEXTURE_ADDRESS_WRAP, "Got unexpected address v %u.\n", desc.AddressV);
    ok(desc.AddressW == D3D10_TEXTURE_ADDRESS_WRAP, "Got unexpected address w %u.\n", desc.AddressW);
    ok(!desc.MipLODBias, "Got unexpected mip LOD bias %f.\n", desc.MipLODBias);
    ok(!desc.MaxAnisotropy || broken(desc.MaxAnisotropy == 16) /* Not set to 0 on all Windows versions. */,
            "Got unexpected max anisotropy %u.\n", desc.MaxAnisotropy);
    ok(desc.ComparisonFunc == D3D10_COMPARISON_NEVER, "Got unexpected comparison func %u.\n", desc.ComparisonFunc);
    ok(!desc.BorderColor[0] && !desc.BorderColor[1] && !desc.BorderColor[2] && !desc.BorderColor[3],
            "Got unexpected border color {%.8e, %.8e, %.8e, %.8e}.\n",
            desc.BorderColor[0], desc.BorderColor[1], desc.BorderColor[2], desc.BorderColor[3]);
    ok(!desc.MinLOD, "Got unexpected min LOD %f.\n", desc.MinLOD);
    ok(desc.MaxLOD == 16.0f, "Got unexpected max LOD %f.\n", desc.MaxLOD);

    refcount = ID3D10SamplerState_Release(sampler_state2);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);
    refcount = ID3D10SamplerState_Release(sampler_state1);
    ok(!refcount, "Got unexpected refcount %u.\n", refcount);

    hr = ID3D10Device_QueryInterface(device, &IID_ID3D11Device, (void **)&d3d11_device);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Device should implement ID3D11Device.\n");
    if (FAILED(hr))
    {
        win_skip("D3D11 is not available.\n");
        goto done;
    }

    for (i = 0; i < sizeof(desc_conversion_tests) / sizeof(*desc_conversion_tests); ++i)
    {
        const struct test *current = &desc_conversion_tests[i];
        D3D11_SAMPLER_DESC d3d11_desc, expected_desc;

        desc.Filter = current->filter;
        desc.AddressU = D3D10_TEXTURE_ADDRESS_WRAP;
        desc.AddressV = D3D10_TEXTURE_ADDRESS_WRAP;
        desc.AddressW = D3D10_TEXTURE_ADDRESS_BORDER;
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = 16;
        desc.ComparisonFunc = D3D10_COMPARISON_ALWAYS;
        desc.BorderColor[0] = 0.0f;
        desc.BorderColor[1] = 1.0f;
        desc.BorderColor[2] = 0.0f;
        desc.BorderColor[3] = 1.0f;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = 16.0f;

        hr = ID3D10Device_CreateSamplerState(device, &desc, &sampler_state1);
        ok(SUCCEEDED(hr), "Test %u: Failed to create sampler state, hr %#x.\n", i, hr);

        hr = ID3D10SamplerState_QueryInterface(sampler_state1, &IID_ID3D11SamplerState,
                (void **)&d3d11_sampler_state);
        ok(SUCCEEDED(hr), "Test %u: Sampler state should implement ID3D11SamplerState.\n", i);

        memcpy(&expected_desc, &desc, sizeof(expected_desc));
        expected_desc.Filter = current->expected_filter;
        if (!D3D11_DECODE_IS_ANISOTROPIC_FILTER(current->filter))
            expected_desc.MaxAnisotropy = 0;
        if (!D3D11_DECODE_IS_COMPARISON_FILTER(current->filter))
            expected_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;

        ID3D11SamplerState_GetDesc(d3d11_sampler_state, &d3d11_desc);
        ok(d3d11_desc.Filter == expected_desc.Filter,
                "Test %u: Got unexpected filter %#x.\n", i, d3d11_desc.Filter);
        ok(d3d11_desc.AddressU == expected_desc.AddressU,
                "Test %u: Got unexpected address u %u.\n", i, d3d11_desc.AddressU);
        ok(d3d11_desc.AddressV == expected_desc.AddressV,
                "Test %u: Got unexpected address v %u.\n", i, d3d11_desc.AddressV);
        ok(d3d11_desc.AddressW == expected_desc.AddressW,
                "Test %u: Got unexpected address w %u.\n", i, d3d11_desc.AddressW);
        ok(d3d11_desc.MipLODBias == expected_desc.MipLODBias,
                "Test %u: Got unexpected mip LOD bias %f.\n", i, d3d11_desc.MipLODBias);
        ok(d3d11_desc.MaxAnisotropy == expected_desc.MaxAnisotropy,
                "Test %u: Got unexpected max anisotropy %u.\n", i, d3d11_desc.MaxAnisotropy);
        ok(d3d11_desc.ComparisonFunc == expected_desc.ComparisonFunc,
                "Test %u: Got unexpected comparison func %u.\n", i, d3d11_desc.ComparisonFunc);
        ok(d3d11_desc.BorderColor[0] == expected_desc.BorderColor[0]
                && d3d11_desc.BorderColor[1] == expected_desc.BorderColor[1]
                && d3d11_desc.BorderColor[2] == expected_desc.BorderColor[2]
                && d3d11_desc.BorderColor[3] == expected_desc.BorderColor[3],
                "Test %u: Got unexpected border color {%.8e, %.8e, %.8e, %.8e}.\n", i,
                d3d11_desc.BorderColor[0], d3d11_desc.BorderColor[1],
                d3d11_desc.BorderColor[2], d3d11_desc.BorderColor[3]);
        ok(d3d11_desc.MinLOD == expected_desc.MinLOD,
                "Test %u: Got unexpected min LOD %f.\n", i, d3d11_desc.MinLOD);
        ok(d3d11_desc.MaxLOD == expected_desc.MaxLOD,
                "Test %u: Got unexpected max LOD %f.\n", i, d3d11_desc.MaxLOD);

        refcount = ID3D11SamplerState_Release(d3d11_sampler_state);
        ok(refcount == 1, "Test %u: Got unexpected refcount %u.\n", i, refcount);

        hr = ID3D11Device_CreateSamplerState(d3d11_device, &d3d11_desc, &d3d11_sampler_state);
        ok(SUCCEEDED(hr), "Test %u: Failed to create sampler state, hr %#x.\n", i, hr);
        hr = ID3D11SamplerState_QueryInterface(d3d11_sampler_state, &IID_ID3D10SamplerState,
                (void **)&sampler_state2);
        ok(SUCCEEDED(hr), "Test %u: Sampler state should implement ID3D10SamplerState.\n", i);
        ok(sampler_state1 == sampler_state2, "Test %u: Got different sampler state objects.\n", i);

        refcount = ID3D11SamplerState_Release(d3d11_sampler_state);
        ok(refcount == 2, "Test %u: Got unexpected refcount %u.\n", i, refcount);
        refcount = ID3D10SamplerState_Release(sampler_state2);
        ok(refcount == 1, "Test %u: Got unexpected refcount %u.\n", i, refcount);
        refcount = ID3D10SamplerState_Release(sampler_state1);
        ok(!refcount, "Test %u: Got unexpected refcount %u.\n", i, refcount);
    }

    ID3D11Device_Release(d3d11_device);

done:
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_blend_state(void)
{
    ID3D10BlendState *blend_state1, *blend_state2;
    ID3D11BlendState *d3d11_blend_state;
    ULONG refcount, expected_refcount;
    D3D11_BLEND_DESC d3d11_blend_desc;
    D3D10_BLEND_DESC blend_desc;
    ID3D11Device *d3d11_device;
    ID3D10Device *device, *tmp;
    IUnknown *iface;
    unsigned int i;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D10Device_CreateBlendState(device, NULL, &blend_state1);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.SrcBlend = D3D10_BLEND_ONE;
    blend_desc.DestBlend = D3D10_BLEND_ZERO;
    blend_desc.BlendOp = D3D10_BLEND_OP_ADD;
    blend_desc.SrcBlendAlpha = D3D10_BLEND_ONE;
    blend_desc.DestBlendAlpha = D3D10_BLEND_ZERO;
    blend_desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        blend_desc.BlendEnable[i] = FALSE;
        blend_desc.RenderTargetWriteMask[i] = D3D10_COLOR_WRITE_ENABLE_ALL;
    }

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateBlendState(device, &blend_desc, &blend_state1);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);
    hr = ID3D10Device_CreateBlendState(device, &blend_desc, &blend_state2);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);
    ok(blend_state1 == blend_state2, "Got different blend state objects.\n");
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10BlendState_GetDevice(blend_state1, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    hr = ID3D10BlendState_QueryInterface(blend_state1, &IID_ID3D10BlendState1, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Blend state should implement ID3D10BlendState1.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    hr = ID3D10Device_QueryInterface(device, &IID_ID3D11Device, (void **)&d3d11_device);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Device should implement ID3D11Device.\n");
    if (FAILED(hr))
    {
        win_skip("D3D11 is not available.\n");
        goto done;
    }

    hr = ID3D10BlendState_QueryInterface(blend_state1, &IID_ID3D11BlendState, (void **)&d3d11_blend_state);
    ok(SUCCEEDED(hr), "Blend state should implement ID3D11BlendState.\n");

    ID3D11BlendState_GetDesc(d3d11_blend_state, &d3d11_blend_desc);
    ok(d3d11_blend_desc.AlphaToCoverageEnable == blend_desc.AlphaToCoverageEnable,
            "Got unexpected alpha to coverage enable %#x.\n", d3d11_blend_desc.AlphaToCoverageEnable);
    ok(d3d11_blend_desc.IndependentBlendEnable == FALSE,
            "Got unexpected independent blend enable %#x.\n", d3d11_blend_desc.IndependentBlendEnable);
    for (i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        ok(d3d11_blend_desc.RenderTarget[i].BlendEnable == blend_desc.BlendEnable[i],
                "Got unexpected blend enable %#x for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].BlendEnable, i);
        ok(d3d11_blend_desc.RenderTarget[i].SrcBlend == (D3D11_BLEND)blend_desc.SrcBlend,
                "Got unexpected src blend %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].SrcBlend, i);
        ok(d3d11_blend_desc.RenderTarget[i].DestBlend == (D3D11_BLEND)blend_desc.DestBlend,
                "Got unexpected dest blend %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].DestBlend, i);
        ok(d3d11_blend_desc.RenderTarget[i].BlendOp == (D3D11_BLEND_OP)blend_desc.BlendOp,
                "Got unexpected blend op %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].BlendOp, i);
        ok(d3d11_blend_desc.RenderTarget[i].SrcBlendAlpha == (D3D11_BLEND)blend_desc.SrcBlendAlpha,
                "Got unexpected src blend alpha %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].SrcBlendAlpha, i);
        ok(d3d11_blend_desc.RenderTarget[i].DestBlendAlpha == (D3D11_BLEND)blend_desc.DestBlendAlpha,
                "Got unexpected dest blend alpha %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].DestBlendAlpha, i);
        ok(d3d11_blend_desc.RenderTarget[i].BlendOpAlpha == (D3D11_BLEND_OP)blend_desc.BlendOpAlpha,
                "Got unexpected blend op alpha %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].BlendOpAlpha, i);
        ok(d3d11_blend_desc.RenderTarget[i].RenderTargetWriteMask == blend_desc.RenderTargetWriteMask[i],
                "Got unexpected render target write mask %#x for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].RenderTargetWriteMask, i);
    }

    refcount = ID3D11BlendState_Release(d3d11_blend_state);
    ok(refcount == 2, "Got unexpected refcount %u.\n", refcount);
    refcount = ID3D10BlendState_Release(blend_state2);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);

    hr = ID3D11Device_CreateBlendState(d3d11_device, &d3d11_blend_desc, &d3d11_blend_state);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);

    hr = ID3D11BlendState_QueryInterface(d3d11_blend_state, &IID_ID3D10BlendState, (void **)&blend_state2);
    ok(SUCCEEDED(hr), "Blend state should implement ID3D10BlendState.\n");
    ok(blend_state1 == blend_state2, "Got different blend state objects.\n");

    refcount = ID3D11BlendState_Release(d3d11_blend_state);
    ok(refcount == 2, "Got unexpected refcount %u.\n", refcount);
    refcount = ID3D10BlendState_Release(blend_state2);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);
    refcount = ID3D10BlendState_Release(blend_state1);
    ok(!refcount, "Got unexpected refcount %u.\n", refcount);

    blend_desc.BlendEnable[0] = TRUE;
    blend_desc.RenderTargetWriteMask[1] = D3D10_COLOR_WRITE_ENABLE_RED;
    blend_desc.RenderTargetWriteMask[2] = D3D10_COLOR_WRITE_ENABLE_GREEN;
    blend_desc.RenderTargetWriteMask[3] = D3D10_COLOR_WRITE_ENABLE_BLUE;

    hr = ID3D10Device_CreateBlendState(device, &blend_desc, &blend_state1);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);

    hr = ID3D10BlendState_QueryInterface(blend_state1, &IID_ID3D11BlendState, (void **)&d3d11_blend_state);
    ok(SUCCEEDED(hr), "Blend state should implement ID3D11BlendState.\n");

    ID3D11BlendState_GetDesc(d3d11_blend_state, &d3d11_blend_desc);
    ok(d3d11_blend_desc.AlphaToCoverageEnable == blend_desc.AlphaToCoverageEnable,
            "Got unexpected alpha to coverage enable %#x.\n", d3d11_blend_desc.AlphaToCoverageEnable);
    ok(d3d11_blend_desc.IndependentBlendEnable == TRUE,
            "Got unexpected independent blend enable %#x.\n", d3d11_blend_desc.IndependentBlendEnable);
    for (i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        ok(d3d11_blend_desc.RenderTarget[i].BlendEnable == blend_desc.BlendEnable[i],
                "Got unexpected blend enable %#x for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].BlendEnable, i);
        ok(d3d11_blend_desc.RenderTarget[i].SrcBlend == (D3D11_BLEND)blend_desc.SrcBlend,
                "Got unexpected src blend %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].SrcBlend, i);
        ok(d3d11_blend_desc.RenderTarget[i].DestBlend == (D3D11_BLEND)blend_desc.DestBlend,
                "Got unexpected dest blend %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].DestBlend, i);
        ok(d3d11_blend_desc.RenderTarget[i].BlendOp == (D3D11_BLEND_OP)blend_desc.BlendOp,
                "Got unexpected blend op %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].BlendOp, i);
        ok(d3d11_blend_desc.RenderTarget[i].SrcBlendAlpha == (D3D11_BLEND)blend_desc.SrcBlendAlpha,
                "Got unexpected src blend alpha %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].SrcBlendAlpha, i);
        ok(d3d11_blend_desc.RenderTarget[i].DestBlendAlpha == (D3D11_BLEND)blend_desc.DestBlendAlpha,
                "Got unexpected dest blend alpha %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].DestBlendAlpha, i);
        ok(d3d11_blend_desc.RenderTarget[i].BlendOpAlpha == (D3D11_BLEND_OP)blend_desc.BlendOpAlpha,
                "Got unexpected blend op alpha %u for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].BlendOpAlpha, i);
        ok(d3d11_blend_desc.RenderTarget[i].RenderTargetWriteMask == blend_desc.RenderTargetWriteMask[i],
                "Got unexpected render target write mask %#x for render target %u.\n",
                d3d11_blend_desc.RenderTarget[i].RenderTargetWriteMask, i);
    }

    refcount = ID3D11BlendState_Release(d3d11_blend_state);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);

    hr = ID3D11Device_CreateBlendState(d3d11_device, &d3d11_blend_desc, &d3d11_blend_state);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);

    hr = ID3D11BlendState_QueryInterface(d3d11_blend_state, &IID_ID3D10BlendState, (void **)&blend_state2);
    ok(SUCCEEDED(hr), "Blend state should implement ID3D10BlendState.\n");
    ok(blend_state1 == blend_state2, "Got different blend state objects.\n");

    refcount = ID3D11BlendState_Release(d3d11_blend_state);
    ok(refcount == 2, "Got unexpected refcount %u.\n", refcount);

    ID3D11Device_Release(d3d11_device);

done:
    refcount = ID3D10BlendState_Release(blend_state2);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);
    refcount = ID3D10BlendState_Release(blend_state1);
    ok(!refcount, "Got unexpected refcount %u.\n", refcount);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_depthstencil_state(void)
{
    ID3D10DepthStencilState *ds_state1, *ds_state2;
    ULONG refcount, expected_refcount;
    D3D10_DEPTH_STENCIL_DESC ds_desc;
    ID3D10Device *device, *tmp;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    hr = ID3D10Device_CreateDepthStencilState(device, NULL, &ds_state1);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ds_desc.DepthEnable = TRUE;
    ds_desc.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
    ds_desc.DepthFunc = D3D10_COMPARISON_LESS;
    ds_desc.StencilEnable = FALSE;
    ds_desc.StencilReadMask = D3D10_DEFAULT_STENCIL_READ_MASK;
    ds_desc.StencilWriteMask = D3D10_DEFAULT_STENCIL_WRITE_MASK;
    ds_desc.FrontFace.StencilFailOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.FrontFace.StencilDepthFailOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.FrontFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.FrontFace.StencilFunc = D3D10_COMPARISON_ALWAYS;
    ds_desc.BackFace.StencilFailOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.BackFace.StencilDepthFailOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.BackFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.BackFace.StencilFunc = D3D10_COMPARISON_ALWAYS;

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateDepthStencilState(device, &ds_desc, &ds_state1);
    ok(SUCCEEDED(hr), "Failed to create depthstencil state, hr %#x.\n", hr);
    hr = ID3D10Device_CreateDepthStencilState(device, &ds_desc, &ds_state2);
    ok(SUCCEEDED(hr), "Failed to create depthstencil state, hr %#x.\n", hr);
    ok(ds_state1 == ds_state2, "Got different depthstencil state objects.\n");
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10DepthStencilState_GetDevice(ds_state1, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    refcount = ID3D10DepthStencilState_Release(ds_state2);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);
    refcount = ID3D10DepthStencilState_Release(ds_state1);
    ok(!refcount, "Got unexpected refcount %u.\n", refcount);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_rasterizer_state(void)
{
    ID3D10RasterizerState *rast_state1, *rast_state2;
    ULONG refcount, expected_refcount;
    D3D10_RASTERIZER_DESC rast_desc;
    ID3D10Device *device, *tmp;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    hr = ID3D10Device_CreateRasterizerState(device, NULL, &rast_state1);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    rast_desc.FillMode = D3D10_FILL_SOLID;
    rast_desc.CullMode = D3D10_CULL_BACK;
    rast_desc.FrontCounterClockwise = FALSE;
    rast_desc.DepthBias = 0;
    rast_desc.DepthBiasClamp = 0.0f;
    rast_desc.SlopeScaledDepthBias = 0.0f;
    rast_desc.DepthClipEnable = TRUE;
    rast_desc.ScissorEnable = FALSE;
    rast_desc.MultisampleEnable = FALSE;
    rast_desc.AntialiasedLineEnable = FALSE;

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreateRasterizerState(device, &rast_desc, &rast_state1);
    ok(SUCCEEDED(hr), "Failed to create rasterizer state, hr %#x.\n", hr);
    hr = ID3D10Device_CreateRasterizerState(device, &rast_desc, &rast_state2);
    ok(SUCCEEDED(hr), "Failed to create rasterizer state, hr %#x.\n", hr);
    ok(rast_state1 == rast_state2, "Got different rasterizer state objects.\n");
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10RasterizerState_GetDevice(rast_state1, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    refcount = ID3D10RasterizerState_Release(rast_state2);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);
    refcount = ID3D10RasterizerState_Release(rast_state1);
    ok(!refcount, "Got unexpected refcount %u.\n", refcount);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_predicate(void)
{
    static const D3D10_QUERY other_queries[] =
    {
        D3D10_QUERY_EVENT,
        D3D10_QUERY_OCCLUSION,
        D3D10_QUERY_TIMESTAMP,
        D3D10_QUERY_TIMESTAMP_DISJOINT,
        D3D10_QUERY_PIPELINE_STATISTICS,
        D3D10_QUERY_SO_STATISTICS,
    };

    ULONG refcount, expected_refcount;
    D3D10_QUERY_DESC query_desc;
    ID3D10Predicate *predicate;
    ID3D10Device *device, *tmp;
    IUnknown *iface;
    unsigned int i;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D10Device_CreatePredicate(device, NULL, &predicate);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    query_desc.MiscFlags = 0;

    for (i = 0; i < sizeof(other_queries) / sizeof(*other_queries); ++i)
    {
        query_desc.Query = other_queries[i];
        hr = ID3D10Device_CreatePredicate(device, &query_desc, &predicate);
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x for query type %u.\n", hr, other_queries[i]);
    }

    query_desc.Query = D3D10_QUERY_OCCLUSION_PREDICATE;
    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device_CreatePredicate(device, &query_desc, &predicate);
    ok(SUCCEEDED(hr), "Failed to create predicate, hr %#x.\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10Predicate_GetDevice(predicate, &tmp);
    ok(tmp == device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);
    hr = ID3D10Predicate_QueryInterface(predicate, &IID_ID3D11Predicate, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Predicate should implement ID3D11Predicate.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);
    ID3D10Predicate_Release(predicate);

    query_desc.Query = D3D10_QUERY_SO_OVERFLOW_PREDICATE;
    hr = ID3D10Device_CreatePredicate(device, &query_desc, &predicate);
    todo_wine ok(SUCCEEDED(hr), "Failed to create predicate, hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D10Predicate_Release(predicate);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_device_removed_reason(void)
{
    ID3D10Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    hr = ID3D10Device_GetDeviceRemovedReason(device);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ID3D10Device_GetDeviceRemovedReason(device);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_scissor(void)
{
    D3D10_SUBRESOURCE_DATA buffer_data;
    ID3D10InputLayout *input_layout;
    D3D10_RASTERIZER_DESC rs_desc;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10RenderTargetView *rtv;
    ID3D10Texture2D *backbuffer;
    unsigned int stride, offset;
    ID3D10RasterizerState *rs;
    IDXGISwapChain *swapchain;
    D3D10_RECT scissor_rect;
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    ID3D10Device *device;
    D3D10_VIEWPORT vp;
    ID3D10Buffer *vb;
    ULONG refcount;
    DWORD color;
    HWND window;
    HRESULT hr;

    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};
    static const D3D10_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
        /* float4 main(float4 position : POSITION) : SV_POSITION
         * {
         *     return position;
         * } */
        0x43425844, 0x1fa8c27f, 0x52d2f21d, 0xc196fdb7, 0x376f283a, 0x00000001, 0x000001b4, 0x00000005,
        0x00000034, 0x0000008c, 0x000000c0, 0x000000f4, 0x00000138, 0x46454452, 0x00000050, 0x00000000,
        0x00000000, 0x00000000, 0x0000001c, 0xfffe0400, 0x00000100, 0x0000001c, 0x7263694d, 0x666f736f,
        0x52282074, 0x4c482029, 0x53204c53, 0x65646168, 0x6f432072, 0x6c69706d, 0x39207265, 0x2e30332e,
        0x30303239, 0x3336312e, 0xab003438, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e, 0x54415453, 0x00000074,
        0x00000002, 0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    };
    static const DWORD ps_code[] =
    {
        /* float4 main(float4 position : SV_POSITION) : SV_Target
         * {
         *     return float4(0.0, 1.0, 0.0, 1.0);
         * } */
        0x43425844, 0xe70802a0, 0xee334047, 0x7bfd0c79, 0xaeff7804, 0x00000001, 0x000001b0, 0x00000005,
        0x00000034, 0x0000008c, 0x000000c0, 0x000000f4, 0x00000134, 0x46454452, 0x00000050, 0x00000000,
        0x00000000, 0x00000000, 0x0000001c, 0xffff0400, 0x00000100, 0x0000001c, 0x7263694d, 0x666f736f,
        0x52282074, 0x4c482029, 0x53204c53, 0x65646168, 0x6f432072, 0x6c69706d, 0x39207265, 0x2e30332e,
        0x30303239, 0x3336312e, 0xab003438, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000038, 0x00000040,
        0x0000000e, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
        0x00000000, 0x3f800000, 0x00000000, 0x3f800000, 0x0100003e, 0x54415453, 0x00000074, 0x00000002,
        0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    };
    static const struct
    {
        float x, y;
    }
    quad[] =
    {
        {-1.0f, -1.0f},
        {-1.0f,  1.0f},
        { 1.0f, -1.0f},
        { 1.0f,  1.0f},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d3d10core_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D10Texture2D, (void **)&backbuffer);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateInputLayout(device, layout_desc, sizeof(layout_desc) / sizeof(*layout_desc),
            vs_code, sizeof(vs_code), &input_layout);
    ok(SUCCEEDED(hr), "Failed to create input layout, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.pSysMem = quad;
    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &buffer_data, &vb);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);
    hr = ID3D10Device_CreateVertexShader(device, vs_code, sizeof(vs_code), &vs);
    ok(SUCCEEDED(hr), "Failed to create vertex shader, hr %#x.\n", hr);
    hr = ID3D10Device_CreatePixelShader(device, ps_code, sizeof(ps_code), &ps);
    ok(SUCCEEDED(hr), "Failed to create pixel shader, hr %#x.\n", hr);

    rs_desc.FillMode = D3D10_FILL_SOLID;
    rs_desc.CullMode = D3D10_CULL_BACK;
    rs_desc.FrontCounterClockwise = FALSE;
    rs_desc.DepthBias = 0;
    rs_desc.DepthBiasClamp = 0.0f;
    rs_desc.SlopeScaledDepthBias = 0.0f;
    rs_desc.DepthClipEnable = TRUE;
    rs_desc.ScissorEnable = TRUE;
    rs_desc.MultisampleEnable = FALSE;
    rs_desc.AntialiasedLineEnable = FALSE;
    hr = ID3D10Device_CreateRasterizerState(device, &rs_desc, &rs);
    ok(SUCCEEDED(hr), "Failed to create rasterizer state, hr %#x.\n", hr);

    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)backbuffer, NULL, &rtv);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);

    ID3D10Device_IASetInputLayout(device, input_layout);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    stride = sizeof(*quad);
    offset = 0;
    ID3D10Device_IASetVertexBuffers(device, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(device, vs);
    ID3D10Device_PSSetShader(device, ps);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 640;
    vp.Height = 480;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(device, 1, &vp);

    scissor_rect.left = 160;
    scissor_rect.top = 120;
    scissor_rect.right = 480;
    scissor_rect.bottom = 360;
    ID3D10Device_RSSetScissorRects(device, 1, &scissor_rect);

    ID3D10Device_OMSetRenderTargets(device, 1, &rtv, NULL);

    ID3D10Device_ClearRenderTargetView(device, rtv, red);
    color = get_texture_color(backbuffer, 320, 240);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);

    ID3D10Device_Draw(device, 4, 0);
    color = get_texture_color(backbuffer, 320, 60);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 80, 240);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 320, 240);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 560, 240);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 320, 420);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);

    ID3D10Device_ClearRenderTargetView(device, rtv, red);
    ID3D10Device_RSSetState(device, rs);
    ID3D10Device_Draw(device, 4, 0);
    color = get_texture_color(backbuffer, 320, 60);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 80, 240);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 320, 240);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 560, 240);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 320, 420);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);

    ID3D10RenderTargetView_Release(rtv);
    ID3D10RasterizerState_Release(rs);
    ID3D10PixelShader_Release(ps);
    ID3D10VertexShader_Release(vs);
    ID3D10Buffer_Release(vb);
    ID3D10InputLayout_Release(input_layout);
    ID3D10Texture2D_Release(backbuffer);
    IDXGISwapChain_Release(swapchain);
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
    DestroyWindow(window);
}

static void test_clear_state(void)
{
    static const D3D10_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
#if 0
float4 main(float4 pos : POSITION) : POSITION
{
    return pos;
}
#endif
    static const DWORD simple_vs[] =
    {
        0x43425844, 0x66689e7c, 0x643f0971, 0xb7f67ff4, 0xabc48688, 0x00000001, 0x000000d4, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x49534f50, 0x4e4f4954, 0xababab00, 0x52444853, 0x00000038, 0x00010040,
        0x0000000e, 0x0300005f, 0x001010f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000000, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };

#if 0
struct gs_out
{
    float4 pos : SV_POSITION;
};

[maxvertexcount(4)]
void main(point float4 vin[1] : POSITION, inout TriangleStream<gs_out> vout)
{
    float offset = 0.1 * vin[0].w;
    gs_out v;

    v.pos = float4(vin[0].x - offset, vin[0].y - offset, vin[0].z, vin[0].w);
    vout.Append(v);
    v.pos = float4(vin[0].x - offset, vin[0].y + offset, vin[0].z, vin[0].w);
    vout.Append(v);
    v.pos = float4(vin[0].x + offset, vin[0].y - offset, vin[0].z, vin[0].w);
    vout.Append(v);
    v.pos = float4(vin[0].x + offset, vin[0].y + offset, vin[0].z, vin[0].w);
    vout.Append(v);
}
#endif
    static const DWORD simple_gs[] =
    {
        0x43425844, 0x000ee786, 0xc624c269, 0x885a5cbe, 0x444b3b1f, 0x00000001, 0x0000023c, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x000001a0, 0x00020040,
        0x00000068, 0x0400005f, 0x002010f2, 0x00000001, 0x00000000, 0x02000068, 0x00000001, 0x0100085d,
        0x0100285c, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x0200005e, 0x00000004, 0x0f000032,
        0x00100032, 0x00000000, 0x80201ff6, 0x00000041, 0x00000000, 0x00000000, 0x00004002, 0x3dcccccd,
        0x3dcccccd, 0x00000000, 0x00000000, 0x00201046, 0x00000000, 0x00000000, 0x05000036, 0x00102032,
        0x00000000, 0x00100046, 0x00000000, 0x06000036, 0x001020c2, 0x00000000, 0x00201ea6, 0x00000000,
        0x00000000, 0x01000013, 0x05000036, 0x00102012, 0x00000000, 0x0010000a, 0x00000000, 0x0e000032,
        0x00100052, 0x00000000, 0x00201ff6, 0x00000000, 0x00000000, 0x00004002, 0x3dcccccd, 0x00000000,
        0x3dcccccd, 0x00000000, 0x00201106, 0x00000000, 0x00000000, 0x05000036, 0x00102022, 0x00000000,
        0x0010002a, 0x00000000, 0x06000036, 0x001020c2, 0x00000000, 0x00201ea6, 0x00000000, 0x00000000,
        0x01000013, 0x05000036, 0x00102012, 0x00000000, 0x0010000a, 0x00000000, 0x05000036, 0x00102022,
        0x00000000, 0x0010001a, 0x00000000, 0x06000036, 0x001020c2, 0x00000000, 0x00201ea6, 0x00000000,
        0x00000000, 0x01000013, 0x05000036, 0x00102032, 0x00000000, 0x00100086, 0x00000000, 0x06000036,
        0x001020c2, 0x00000000, 0x00201ea6, 0x00000000, 0x00000000, 0x01000013, 0x0100003e,
    };

#if 0
float4 main(float4 color : COLOR) : SV_TARGET
{
    return color;
}
#endif
    static const DWORD simple_ps[] =
    {
        0x43425844, 0x08c2b568, 0x17d33120, 0xb7d82948, 0x13a570fb, 0x00000001, 0x000000d0, 0x00000003,
        0x0000002c, 0x0000005c, 0x00000090, 0x4e475349, 0x00000028, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x4f4c4f43, 0xabab0052, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x00000038, 0x00000040, 0x0000000e,
        0x03001062, 0x001010f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000000, 0x05000036, 0x001020f2,
        0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };

    D3D10_VIEWPORT tmp_viewport[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D10ShaderResourceView *tmp_srv[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D10ShaderResourceView *srv[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D10RenderTargetView *tmp_rtv[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
    RECT tmp_rect[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D10SamplerState *tmp_sampler[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
    ID3D10RenderTargetView *rtv[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D10Texture2D *rt_texture[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D10Buffer *cb[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    ID3D10Buffer *tmp_buffer[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    ID3D10SamplerState *sampler[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
    ID3D10Buffer *buffer[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT offset[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT stride[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    ID3D10Buffer *so_buffer[D3D10_SO_BUFFER_SLOT_COUNT];
    ID3D10InputLayout *tmp_input_layout, *input_layout;
    ID3D10DepthStencilState *tmp_ds_state, *ds_state;
    ID3D10BlendState *tmp_blend_state, *blend_state;
    ID3D10RasterizerState *tmp_rs_state, *rs_state;
    ID3D10Predicate *tmp_predicate, *predicate;
    D3D10_SHADER_RESOURCE_VIEW_DESC srv_desc;
    ID3D10DepthStencilView *tmp_dsv, *dsv;
    D3D10_PRIMITIVE_TOPOLOGY topology;
    D3D10_TEXTURE2D_DESC texture_desc;
    ID3D10GeometryShader *tmp_gs, *gs;
    D3D10_DEPTH_STENCIL_DESC ds_desc;
    ID3D10VertexShader *tmp_vs, *vs;
    D3D10_SAMPLER_DESC sampler_desc;
    D3D10_QUERY_DESC predicate_desc;
    ID3D10PixelShader *tmp_ps, *ps;
    D3D10_RASTERIZER_DESC rs_desc;
    D3D10_BUFFER_DESC buffer_desc;
    D3D10_BLEND_DESC blend_desc;
    ID3D10Texture2D *ds_texture;
    float tmp_blend_factor[4];
    float blend_factor[4];
    ID3D10Device *device;
    BOOL predicate_value;
    DXGI_FORMAT format;
    UINT sample_mask;
    UINT stencil_ref;
    ULONG refcount;
    UINT count, i;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    /* Verify the initial state after device creation. */

    ID3D10Device_VSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected constant buffer %p in slot %u.\n", tmp_buffer[i], i);
    }
    ID3D10Device_VSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(!tmp_srv[i], "Got unexpected shader resource view %p in slot %u.\n", tmp_srv[i], i);
    }
    ID3D10Device_VSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(!tmp_sampler[i], "Got unexpected sampler %p in slot %u.\n", tmp_sampler[i], i);
    }
    ID3D10Device_VSGetShader(device, &tmp_vs);
    ok(!tmp_vs, "Got unexpected vertex shader %p.\n", tmp_vs);

    ID3D10Device_GSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected constant buffer %p in slot %u.\n", tmp_buffer[i], i);
    }
    ID3D10Device_GSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(!tmp_srv[i], "Got unexpected shader resource view %p in slot %u.\n", tmp_srv[i], i);
    }
    ID3D10Device_GSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(!tmp_sampler[i], "Got unexpected sampler %p in slot %u.\n", tmp_sampler[i], i);
    }
    ID3D10Device_GSGetShader(device, &tmp_gs);
    ok(!tmp_gs, "Got unexpected geometry shader %p.\n", tmp_gs);

    ID3D10Device_PSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected constant buffer %p in slot %u.\n", tmp_buffer[i], i);
    }
    ID3D10Device_PSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(!tmp_srv[i], "Got unexpected shader resource view %p in slot %u.\n", tmp_srv[i], i);
    }
    ID3D10Device_PSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(!tmp_sampler[i], "Got unexpected sampler %p in slot %u.\n", tmp_sampler[i], i);
    }
    ID3D10Device_PSGetShader(device, &tmp_ps);
    ok(!tmp_ps, "Got unexpected pixel shader %p.\n", tmp_ps);

    ID3D10Device_IAGetVertexBuffers(device, 0, D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, tmp_buffer, stride, offset);
    for (i = 0; i < D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected vertex buffer %p in slot %u.\n", tmp_buffer[i], i);
        ok(!stride[i], "Got unexpected stride %u in slot %u.\n", stride[i], i);
        ok(!offset[i], "Got unexpected offset %u in slot %u.\n", offset[i], i);
    }
    ID3D10Device_IAGetIndexBuffer(device, tmp_buffer, &format, offset);
    ok(!tmp_buffer[0], "Got unexpected index buffer %p.\n", tmp_buffer[0]);
    ok(format == DXGI_FORMAT_UNKNOWN, "Got unexpected index buffer format %#x.\n", format);
    ok(!offset[0], "Got unexpected index buffer offset %u.\n", offset[0]);
    ID3D10Device_IAGetInputLayout(device, &tmp_input_layout);
    ok(!tmp_input_layout, "Got unexpected input layout %p.\n", tmp_input_layout);
    ID3D10Device_IAGetPrimitiveTopology(device, &topology);
    ok(topology == D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED, "Got unexpected primitive topology %#x.\n", topology);

    ID3D10Device_OMGetBlendState(device, &tmp_blend_state, blend_factor, &sample_mask);
    ok(!tmp_blend_state, "Got unexpected blend state %p.\n", tmp_blend_state);
    ok(blend_factor[0] == 1.0f && blend_factor[1] == 1.0f
            && blend_factor[2] == 1.0f && blend_factor[3] == 1.0f,
            "Got unexpected blend factor {%.8e, %.8e, %.8e, %.8e}.\n",
            blend_factor[0], blend_factor[1], blend_factor[2], blend_factor[3]);
    ok(sample_mask == D3D10_DEFAULT_SAMPLE_MASK, "Got unexpected sample mask %#x.\n", sample_mask);
    ID3D10Device_OMGetDepthStencilState(device, &tmp_ds_state, &stencil_ref);
    ok(!tmp_ds_state, "Got unexpected depth stencil state %p.\n", tmp_ds_state);
    ok(!stencil_ref, "Got unexpected stencil ref %u.\n", stencil_ref);
    ID3D10Device_OMGetRenderTargets(device, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT, tmp_rtv, &tmp_dsv);
    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        ok(!tmp_rtv[i], "Got unexpected render target view %p in slot %u.\n", tmp_rtv[i], i);
    }
    ok(!tmp_dsv, "Got unexpected depth stencil view %p.\n", tmp_dsv);

    ID3D10Device_RSGetScissorRects(device, &count, NULL);
    todo_wine ok(!count, "Got unexpected scissor rect count %u.\n", count);
    memset(tmp_rect, 0x55, sizeof(tmp_rect));
    count = D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D10Device_RSGetScissorRects(device, &count, tmp_rect);
    for (i = 0; i < D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; ++i)
    {
        ok(!tmp_rect[i].left && !tmp_rect[i].top && !tmp_rect[i].right && !tmp_rect[i].bottom,
                "Got unexpected scissor rect {%d, %d, %d, %d} in slot %u.\n",
                tmp_rect[i].left, tmp_rect[i].top, tmp_rect[i].right, tmp_rect[i].bottom, i);
    }
    ID3D10Device_RSGetViewports(device, &count, NULL);
    todo_wine ok(!count, "Got unexpected viewport count %u.\n", count);
    memset(tmp_viewport, 0x55, sizeof(tmp_viewport));
    count = D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D10Device_RSGetViewports(device, &count, tmp_viewport);
    for (i = 0; i < D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; ++i)
    {
        ok(!tmp_viewport[i].TopLeftX && !tmp_viewport[i].TopLeftY && !tmp_viewport[i].Width
                && !tmp_viewport[i].Height && !tmp_viewport[i].MinDepth && !tmp_viewport[i].MaxDepth,
                "Got unexpected viewport {%d, %d, %u, %u, %.8e, %.8e} in slot %u.\n",
                tmp_viewport[i].TopLeftX, tmp_viewport[i].TopLeftY, tmp_viewport[i].Width,
                tmp_viewport[i].Height, tmp_viewport[i].MinDepth, tmp_viewport[i].MaxDepth, i);
    }
    ID3D10Device_RSGetState(device, &tmp_rs_state);
    ok(!tmp_rs_state, "Got unexpected rasterizer state %p.\n", tmp_rs_state);

    ID3D10Device_SOGetTargets(device, D3D10_SO_BUFFER_SLOT_COUNT, tmp_buffer, offset);
    for (i = 0; i < D3D10_SO_BUFFER_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected stream output %p in slot %u.\n", tmp_buffer[i], i);
        ok(!offset[i], "Got unexpected stream output offset %u in slot %u.\n", offset[i], i);
    }

    ID3D10Device_GetPredication(device, &tmp_predicate, &predicate_value);
    ok(!tmp_predicate, "Got unexpected predicate %p.\n", tmp_predicate);
    ok(!predicate_value, "Got unexpected predicate value %#x.\n", predicate_value);

    /* Create resources. */

    buffer_desc.ByteWidth = 1024;
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        hr = ID3D10Device_CreateBuffer(device, &buffer_desc, NULL, &cb[i]);
        ok(SUCCEEDED(hr), "Failed to create buffer, hr %#x.\n", hr);
    }

    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER | D3D10_BIND_INDEX_BUFFER | D3D10_BIND_SHADER_RESOURCE;

    for (i = 0; i < D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        hr = ID3D10Device_CreateBuffer(device, &buffer_desc, NULL, &buffer[i]);
        ok(SUCCEEDED(hr), "Failed to create buffer, hr %#x.\n", hr);

        stride[i] = (i + 1) * 4;
        offset[i] = (i + 1) * 16;
    }

    buffer_desc.BindFlags = D3D10_BIND_STREAM_OUTPUT;

    for (i = 0; i < D3D10_SO_BUFFER_SLOT_COUNT; ++i)
    {
        hr = ID3D10Device_CreateBuffer(device, &buffer_desc, NULL, &so_buffer[i]);
        ok(SUCCEEDED(hr), "Failed to create buffer, hr %#x.\n", hr);
    }

    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D10_SRV_DIMENSION_BUFFER;
    U(srv_desc).Buffer.ElementOffset = 0;
    U(srv_desc).Buffer.ElementWidth = 64;

    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        hr = ID3D10Device_CreateShaderResourceView(device,
                (ID3D10Resource *)buffer[i % D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT], &srv_desc, &srv[i]);
        ok(SUCCEEDED(hr), "Failed to create shader resource view, hr %#x.\n", hr);
    }

    sampler_desc.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MipLODBias = 0.0f;
    sampler_desc.MaxAnisotropy = 16;
    sampler_desc.ComparisonFunc = D3D10_COMPARISON_NEVER;
    sampler_desc.BorderColor[0] = 0.0f;
    sampler_desc.BorderColor[1] = 0.0f;
    sampler_desc.BorderColor[2] = 0.0f;
    sampler_desc.BorderColor[3] = 0.0f;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = 16.0f;

    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        sampler_desc.MinLOD = (float)i;

        hr = ID3D10Device_CreateSamplerState(device, &sampler_desc, &sampler[i]);
        ok(SUCCEEDED(hr), "Failed to create sampler state, hr %#x.\n", hr);
    }

    hr = ID3D10Device_CreateVertexShader(device, simple_vs, sizeof(simple_vs), &vs);
    ok(SUCCEEDED(hr), "Failed to create vertex shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreateGeometryShader(device, simple_gs, sizeof(simple_gs), &gs);
    ok(SUCCEEDED(hr), "Failed to create geometry shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreatePixelShader(device, simple_ps, sizeof(simple_ps), &ps);
    ok(SUCCEEDED(hr), "Failed to create pixel shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreateInputLayout(device, layout_desc, sizeof(layout_desc) / sizeof(*layout_desc),
            simple_vs, sizeof(simple_vs), &input_layout);
    ok(SUCCEEDED(hr), "Failed to create input layout, hr %#x.\n", hr);

    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.BlendEnable[0] = FALSE;
    blend_desc.BlendEnable[1] = FALSE;
    blend_desc.BlendEnable[2] = FALSE;
    blend_desc.BlendEnable[3] = FALSE;
    blend_desc.BlendEnable[4] = FALSE;
    blend_desc.BlendEnable[5] = FALSE;
    blend_desc.BlendEnable[6] = FALSE;
    blend_desc.BlendEnable[7] = FALSE;
    blend_desc.SrcBlend = D3D10_BLEND_ONE;
    blend_desc.DestBlend = D3D10_BLEND_ZERO;
    blend_desc.BlendOp = D3D10_BLEND_OP_ADD;
    blend_desc.SrcBlendAlpha = D3D10_BLEND_ONE;
    blend_desc.DestBlendAlpha = D3D10_BLEND_ZERO;
    blend_desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    blend_desc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTargetWriteMask[1] = D3D10_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTargetWriteMask[2] = D3D10_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTargetWriteMask[3] = D3D10_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTargetWriteMask[4] = D3D10_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTargetWriteMask[5] = D3D10_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTargetWriteMask[6] = D3D10_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTargetWriteMask[7] = D3D10_COLOR_WRITE_ENABLE_ALL;

    hr = ID3D10Device_CreateBlendState(device, &blend_desc, &blend_state);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);

    ds_desc.DepthEnable = TRUE;
    ds_desc.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
    ds_desc.DepthFunc = D3D10_COMPARISON_LESS;
    ds_desc.StencilEnable = FALSE;
    ds_desc.StencilReadMask = D3D10_DEFAULT_STENCIL_READ_MASK;
    ds_desc.StencilWriteMask = D3D10_DEFAULT_STENCIL_WRITE_MASK;
    ds_desc.FrontFace.StencilFailOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.FrontFace.StencilDepthFailOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.FrontFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.FrontFace.StencilFunc = D3D10_COMPARISON_ALWAYS;
    ds_desc.BackFace.StencilFailOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.BackFace.StencilDepthFailOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.BackFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
    ds_desc.BackFace.StencilFunc = D3D10_COMPARISON_ALWAYS;

    hr = ID3D10Device_CreateDepthStencilState(device, &ds_desc, &ds_state);
    ok(SUCCEEDED(hr), "Failed to create depthstencil state, hr %#x.\n", hr);

    texture_desc.Width = 512;
    texture_desc.Height = 512;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &rt_texture[i]);
        ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);
    }

    texture_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    texture_desc.BindFlags = D3D10_BIND_DEPTH_STENCIL;

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &ds_texture);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)rt_texture[i], NULL, &rtv[i]);
        ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);
    }

    hr = ID3D10Device_CreateDepthStencilView(device, (ID3D10Resource *)ds_texture, NULL, &dsv);
    ok(SUCCEEDED(hr), "Failed to create depthstencil view, hr %#x.\n", hr);

    for (i = 0; i < D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; ++i)
    {
        tmp_rect[i].left = i;
        tmp_rect[i].top = i * 2;
        tmp_rect[i].right = i + 1;
        tmp_rect[i].bottom = (i + 1) * 2;

        tmp_viewport[i].TopLeftX = i * 3;
        tmp_viewport[i].TopLeftY = i * 4;
        tmp_viewport[i].Width = 3;
        tmp_viewport[i].Height = 4;
        tmp_viewport[i].MinDepth = i * 0.01f;
        tmp_viewport[i].MaxDepth = (i + 1) * 0.01f;
    }

    rs_desc.FillMode = D3D10_FILL_SOLID;
    rs_desc.CullMode = D3D10_CULL_BACK;
    rs_desc.FrontCounterClockwise = FALSE;
    rs_desc.DepthBias = 0;
    rs_desc.DepthBiasClamp = 0.0f;
    rs_desc.SlopeScaledDepthBias = 0.0f;
    rs_desc.DepthClipEnable = TRUE;
    rs_desc.ScissorEnable = FALSE;
    rs_desc.MultisampleEnable = FALSE;
    rs_desc.AntialiasedLineEnable = FALSE;

    hr = ID3D10Device_CreateRasterizerState(device, &rs_desc, &rs_state);
    ok(SUCCEEDED(hr), "Failed to create rasterizer state, hr %#x.\n", hr);

    predicate_desc.Query = D3D10_QUERY_OCCLUSION_PREDICATE;
    predicate_desc.MiscFlags = 0;

    hr = ID3D10Device_CreatePredicate(device, &predicate_desc, &predicate);
    ok(SUCCEEDED(hr), "Failed to create predicate, hr %#x.\n", hr);

    /* Verify the behavior of set state methods. */

    blend_factor[0] = 0.1f;
    blend_factor[1] = 0.2f;
    blend_factor[2] = 0.3f;
    blend_factor[3] = 0.4f;
    ID3D10Device_OMSetBlendState(device, blend_state, blend_factor, D3D10_DEFAULT_SAMPLE_MASK);
    ID3D10Device_OMGetBlendState(device, &tmp_blend_state, tmp_blend_factor, &sample_mask);
    ok(tmp_blend_factor[0] == 0.1f && tmp_blend_factor[1] == 0.2f
            && tmp_blend_factor[2] == 0.3f && tmp_blend_factor[3] == 0.4f,
            "Got unexpected blend factor {%.8e, %.8e, %.8e, %.8e}.\n",
            tmp_blend_factor[0], tmp_blend_factor[1], tmp_blend_factor[2], tmp_blend_factor[3]);
    ID3D10BlendState_Release(tmp_blend_state);

    ID3D10Device_OMSetBlendState(device, blend_state, NULL, D3D10_DEFAULT_SAMPLE_MASK);
    ID3D10Device_OMGetBlendState(device, &tmp_blend_state, tmp_blend_factor, &sample_mask);
    ok(tmp_blend_factor[0] == 1.0f && tmp_blend_factor[1] == 1.0f
            && tmp_blend_factor[2] == 1.0f && tmp_blend_factor[3] == 1.0f,
            "Got unexpected blend factor {%.8e, %.8e, %.8e, %.8e}.\n",
            tmp_blend_factor[0], tmp_blend_factor[1], tmp_blend_factor[2], tmp_blend_factor[3]);
    ID3D10BlendState_Release(tmp_blend_state);

    /* Setup state. */

    ID3D10Device_VSSetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, cb);
    ID3D10Device_VSSetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, srv);
    ID3D10Device_VSSetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, sampler);
    ID3D10Device_VSSetShader(device, vs);

    ID3D10Device_GSSetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, cb);
    ID3D10Device_GSSetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, srv);
    ID3D10Device_GSSetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, sampler);
    ID3D10Device_GSSetShader(device, gs);

    ID3D10Device_PSSetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, cb);
    ID3D10Device_PSSetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, srv);
    ID3D10Device_PSSetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, sampler);
    ID3D10Device_PSSetShader(device, ps);

    ID3D10Device_IASetVertexBuffers(device, 0, D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, buffer, stride, offset);
    ID3D10Device_IASetIndexBuffer(device, buffer[0], DXGI_FORMAT_R32_UINT, offset[0]);
    ID3D10Device_IASetInputLayout(device, input_layout);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    blend_factor[0] = 0.1f;
    blend_factor[1] = 0.2f;
    blend_factor[2] = 0.3f;
    blend_factor[3] = 0.4f;
    ID3D10Device_OMSetBlendState(device, blend_state, blend_factor, 0xff00ff00);
    ID3D10Device_OMSetDepthStencilState(device, ds_state, 3);
    ID3D10Device_OMSetRenderTargets(device, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);

    ID3D10Device_RSSetScissorRects(device, D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, tmp_rect);
    ID3D10Device_RSSetViewports(device, D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, tmp_viewport);
    ID3D10Device_RSSetState(device, rs_state);

    ID3D10Device_SOSetTargets(device, D3D10_SO_BUFFER_SLOT_COUNT, so_buffer, offset);

    ID3D10Device_SetPredication(device, predicate, TRUE);

    /* Verify the set state. */

    ID3D10Device_VSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(tmp_buffer[i] == cb[i], "Got unexpected constant buffer %p in slot %u, expected %p.\n",
                tmp_buffer[i], i, cb[i]);
        ID3D10Buffer_Release(tmp_buffer[i]);
    }
    ID3D10Device_VSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(tmp_srv[i] == srv[i], "Got unexpected shader resource view %p in slot %u, expected %p.\n",
                tmp_srv[i], i, srv[i]);
        ID3D10ShaderResourceView_Release(tmp_srv[i]);
    }
    ID3D10Device_VSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(tmp_sampler[i] == sampler[i], "Got unexpected sampler %p in slot %u, expected %p.\n",
                tmp_sampler[i], i, sampler[i]);
        ID3D10SamplerState_Release(tmp_sampler[i]);
    }
    ID3D10Device_VSGetShader(device, &tmp_vs);
    ok(tmp_vs == vs, "Got unexpected vertex shader %p, expected %p.\n", tmp_vs, vs);
    ID3D10VertexShader_Release(tmp_vs);

    ID3D10Device_GSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(tmp_buffer[i] == cb[i], "Got unexpected constant buffer %p in slot %u, expected %p.\n",
                tmp_buffer[i], i, cb[i]);
        ID3D10Buffer_Release(tmp_buffer[i]);
    }
    ID3D10Device_GSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(tmp_srv[i] == srv[i], "Got unexpected shader resource view %p in slot %u, expected %p.\n",
                tmp_srv[i], i, srv[i]);
        ID3D10ShaderResourceView_Release(tmp_srv[i]);
    }
    ID3D10Device_GSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(tmp_sampler[i] == sampler[i], "Got unexpected sampler %p in slot %u, expected %p.\n",
                tmp_sampler[i], i, sampler[i]);
        ID3D10SamplerState_Release(tmp_sampler[i]);
    }
    ID3D10Device_GSGetShader(device, &tmp_gs);
    ok(tmp_gs == gs, "Got unexpected geometry shader %p, expected %p.\n", tmp_gs, gs);
    ID3D10GeometryShader_Release(tmp_gs);

    ID3D10Device_PSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(tmp_buffer[i] == cb[i], "Got unexpected constant buffer %p in slot %u, expected %p.\n",
                tmp_buffer[i], i, cb[i]);
        ID3D10Buffer_Release(tmp_buffer[i]);
    }
    ID3D10Device_PSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(tmp_srv[i] == srv[i], "Got unexpected shader resource view %p in slot %u, expected %p.\n",
                tmp_srv[i], i, srv[i]);
        ID3D10ShaderResourceView_Release(tmp_srv[i]);
    }
    ID3D10Device_PSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(tmp_sampler[i] == sampler[i], "Got unexpected sampler %p in slot %u, expected %p.\n",
                tmp_sampler[i], i, sampler[i]);
        ID3D10SamplerState_Release(tmp_sampler[i]);
    }
    ID3D10Device_PSGetShader(device, &tmp_ps);
    ok(tmp_ps == ps, "Got unexpected pixel shader %p, expected %p.\n", tmp_ps, ps);
    ID3D10PixelShader_Release(tmp_ps);

    ID3D10Device_IAGetVertexBuffers(device, 0, D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, tmp_buffer, stride, offset);
    for (i = 0; i < D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(tmp_buffer[i] == buffer[i], "Got unexpected vertex buffer %p in slot %u, expected %p.\n",
                tmp_buffer[i], i, buffer[i]);
        ok(stride[i] == (i + 1) * 4, "Got unexpected stride %u in slot %u.\n", stride[i], i);
        ok(offset[i] == (i + 1) * 16, "Got unexpected offset %u in slot %u.\n", offset[i], i);
        ID3D10Buffer_Release(tmp_buffer[i]);
    }
    ID3D10Device_IAGetIndexBuffer(device, tmp_buffer, &format, offset);
    ok(tmp_buffer[0] == buffer[0], "Got unexpected index buffer %p, expected %p.\n", tmp_buffer[0], buffer[0]);
    ID3D10Buffer_Release(tmp_buffer[0]);
    ok(format == DXGI_FORMAT_R32_UINT, "Got unexpected index buffer format %#x.\n", format);
    todo_wine ok(offset[0] == 16, "Got unexpected index buffer offset %u.\n", offset[0]);
    ID3D10Device_IAGetInputLayout(device, &tmp_input_layout);
    ok(tmp_input_layout == input_layout, "Got unexpected input layout %p, expected %p.\n",
            tmp_input_layout, input_layout);
    ID3D10InputLayout_Release(tmp_input_layout);
    ID3D10Device_IAGetPrimitiveTopology(device, &topology);
    ok(topology == D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "Got unexpected primitive topology %#x.\n", topology);

    ID3D10Device_OMGetBlendState(device, &tmp_blend_state, blend_factor, &sample_mask);
    ok(tmp_blend_state == blend_state, "Got unexpected blend state %p, expected %p.\n", tmp_blend_state, blend_state);
    ID3D10BlendState_Release(tmp_blend_state);
    ok(blend_factor[0] == 0.1f && blend_factor[1] == 0.2f
            && blend_factor[2] == 0.3f && blend_factor[3] == 0.4f,
            "Got unexpected blend factor {%.8e, %.8e, %.8e, %.8e}.\n",
            blend_factor[0], blend_factor[1], blend_factor[2], blend_factor[3]);
    ok(sample_mask == 0xff00ff00, "Got unexpected sample mask %#x.\n", sample_mask);
    ID3D10Device_OMGetDepthStencilState(device, &tmp_ds_state, &stencil_ref);
    ok(tmp_ds_state == ds_state, "Got unexpected depth stencil state %p, expected %p.\n", tmp_ds_state, ds_state);
    ID3D10DepthStencilState_Release(tmp_ds_state);
    ok(stencil_ref == 3, "Got unexpected stencil ref %u.\n", stencil_ref);
    ID3D10Device_OMGetRenderTargets(device, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT, tmp_rtv, &tmp_dsv);
    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        ok(tmp_rtv[i] == rtv[i], "Got unexpected render target view %p in slot %u, expected %p.\n",
                tmp_rtv[i], i, rtv[i]);
        ID3D10RenderTargetView_Release(tmp_rtv[i]);
    }
    ok(tmp_dsv == dsv, "Got unexpected depth stencil view %p, expected %p.\n", tmp_dsv, dsv);
    ID3D10DepthStencilView_Release(tmp_dsv);

    ID3D10Device_RSGetScissorRects(device, &count, NULL);
    todo_wine ok(count == D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE,
            "Got unexpected scissor rect count %u.\n", count);
    memset(tmp_rect, 0x55, sizeof(tmp_rect));
    ID3D10Device_RSGetScissorRects(device, &count, tmp_rect);
    for (i = 0; i < count; ++i)
    {
        ok(tmp_rect[i].left == i
                && tmp_rect[i].top == i * 2
                && tmp_rect[i].right == i + 1
                && tmp_rect[i].bottom == (i + 1) * 2,
                "Got unexpected scissor rect {%d, %d, %d, %d} in slot %u.\n",
                tmp_rect[i].left, tmp_rect[i].top, tmp_rect[i].right, tmp_rect[i].bottom, i);
    }
    ID3D10Device_RSGetViewports(device, &count, NULL);
    todo_wine ok(count == D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE,
            "Got unexpected viewport count %u.\n", count);
    memset(tmp_viewport, 0x55, sizeof(tmp_viewport));
    ID3D10Device_RSGetViewports(device, &count, tmp_viewport);
    for (i = 0; i < count; ++i)
    {
        ok(tmp_viewport[i].TopLeftX == i * 3
                && tmp_viewport[i].TopLeftY == i * 4
                && tmp_viewport[i].Width == 3
                && tmp_viewport[i].Height == 4
                && compare_float(tmp_viewport[i].MinDepth, i * 0.01f, 16)
                && compare_float(tmp_viewport[i].MaxDepth, (i + 1) * 0.01f, 16),
                "Got unexpected viewport {%d, %d, %u, %u, %.8e, %.8e} in slot %u.\n",
                tmp_viewport[i].TopLeftX, tmp_viewport[i].TopLeftY, tmp_viewport[i].Width,
                tmp_viewport[i].Height, tmp_viewport[i].MinDepth, tmp_viewport[i].MaxDepth, i);
    }
    ID3D10Device_RSGetState(device, &tmp_rs_state);
    ok(tmp_rs_state == rs_state, "Got unexpected rasterizer state %p, expected %p.\n", tmp_rs_state, rs_state);
    ID3D10RasterizerState_Release(tmp_rs_state);

    ID3D10Device_SOGetTargets(device, D3D10_SO_BUFFER_SLOT_COUNT, tmp_buffer, offset);
    for (i = 0; i < D3D10_SO_BUFFER_SLOT_COUNT; ++i)
    {
        ok(tmp_buffer[i] == so_buffer[i], "Got unexpected stream output %p in slot %u, expected %p.\n",
                tmp_buffer[i], i, so_buffer[i]);
        ID3D10Buffer_Release(tmp_buffer[i]);
        todo_wine ok(offset[i] == ~0u, "Got unexpected stream output offset %u in slot %u.\n", offset[i], i);
    }

    ID3D10Device_GetPredication(device, &tmp_predicate, &predicate_value);
    ok(tmp_predicate == predicate, "Got unexpected predicate %p, expected %p.\n", tmp_predicate, predicate);
    ID3D10Predicate_Release(tmp_predicate);
    ok(predicate_value, "Got unexpected predicate value %#x.\n", predicate_value);

    /* Verify ClearState(). */

    ID3D10Device_ClearState(device);

    ID3D10Device_VSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected constant buffer %p in slot %u.\n", tmp_buffer[i], i);
    }
    ID3D10Device_VSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(!tmp_srv[i], "Got unexpected shader resource view %p in slot %u.\n", tmp_srv[i], i);
    }
    ID3D10Device_VSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(!tmp_sampler[i], "Got unexpected sampler %p in slot %u.\n", tmp_sampler[i], i);
    }
    ID3D10Device_VSGetShader(device, &tmp_vs);
    ok(!tmp_vs, "Got unexpected vertex shader %p.\n", tmp_vs);

    ID3D10Device_GSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected constant buffer %p in slot %u.\n", tmp_buffer[i], i);
    }
    ID3D10Device_GSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(!tmp_srv[i], "Got unexpected shader resource view %p in slot %u.\n", tmp_srv[i], i);
    }
    ID3D10Device_GSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(!tmp_sampler[i], "Got unexpected sampler %p in slot %u.\n", tmp_sampler[i], i);
    }
    ID3D10Device_GSGetShader(device, &tmp_gs);
    ok(!tmp_gs, "Got unexpected geometry shader %p.\n", tmp_gs);

    ID3D10Device_PSGetConstantBuffers(device, 0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, tmp_buffer);
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected constant buffer %p in slot %u.\n", tmp_buffer[i], i);
    }
    ID3D10Device_PSGetShaderResources(device, 0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, tmp_srv);
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(!tmp_srv[i], "Got unexpected shader resource view %p in slot %u.\n", tmp_srv[i], i);
    }
    ID3D10Device_PSGetSamplers(device, 0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT, tmp_sampler);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ok(!tmp_sampler[i], "Got unexpected sampler %p in slot %u.\n", tmp_sampler[i], i);
    }
    ID3D10Device_PSGetShader(device, &tmp_ps);
    ok(!tmp_ps, "Got unexpected pixel shader %p.\n", tmp_ps);

    ID3D10Device_IAGetVertexBuffers(device, 0, D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, tmp_buffer, stride, offset);
    for (i = 0; i < D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected vertex buffer %p in slot %u.\n", tmp_buffer[i], i);
        todo_wine ok(!stride[i], "Got unexpected stride %u in slot %u.\n", stride[i], i);
        todo_wine ok(!offset[i], "Got unexpected offset %u in slot %u.\n", offset[i], i);
    }
    ID3D10Device_IAGetIndexBuffer(device, tmp_buffer, &format, offset);
    ok(!tmp_buffer[0], "Got unexpected index buffer %p.\n", tmp_buffer[0]);
    ok(format == DXGI_FORMAT_UNKNOWN, "Got unexpected index buffer format %#x.\n", format);
    ok(!offset[0], "Got unexpected index buffer offset %u.\n", offset[0]);
    ID3D10Device_IAGetInputLayout(device, &tmp_input_layout);
    ok(!tmp_input_layout, "Got unexpected input layout %p.\n", tmp_input_layout);
    ID3D10Device_IAGetPrimitiveTopology(device, &topology);
    ok(topology == D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED, "Got unexpected primitive topology %#x.\n", topology);

    ID3D10Device_OMGetBlendState(device, &tmp_blend_state, blend_factor, &sample_mask);
    ok(!tmp_blend_state, "Got unexpected blend state %p.\n", tmp_blend_state);
    ok(blend_factor[0] == 1.0f && blend_factor[1] == 1.0f
            && blend_factor[2] == 1.0f && blend_factor[3] == 1.0f,
            "Got unexpected blend factor {%.8e, %.8e, %.8e, %.8e}.\n",
            blend_factor[0], blend_factor[1], blend_factor[2], blend_factor[3]);
    ok(sample_mask == D3D10_DEFAULT_SAMPLE_MASK, "Got unexpected sample mask %#x.\n", sample_mask);
    ID3D10Device_OMGetDepthStencilState(device, &tmp_ds_state, &stencil_ref);
    ok(!tmp_ds_state, "Got unexpected depth stencil state %p.\n", tmp_ds_state);
    ok(!stencil_ref, "Got unexpected stencil ref %u.\n", stencil_ref);
    ID3D10Device_OMGetRenderTargets(device, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT, tmp_rtv, &tmp_dsv);
    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        ok(!tmp_rtv[i], "Got unexpected render target view %p in slot %u.\n", tmp_rtv[i], i);
    }
    ok(!tmp_dsv, "Got unexpected depth stencil view %p.\n", tmp_dsv);

    ID3D10Device_RSGetScissorRects(device, &count, NULL);
    todo_wine ok(!count, "Got unexpected scissor rect count %u.\n", count);
    memset(tmp_rect, 0x55, sizeof(tmp_rect));
    count = D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D10Device_RSGetScissorRects(device, &count, tmp_rect);
    for (i = 0; i < D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; ++i)
    {
        if (!i)
            todo_wine ok(!tmp_rect[i].left && !tmp_rect[i].top && !tmp_rect[i].right && !tmp_rect[i].bottom,
                    "Got unexpected scissor rect {%d, %d, %d, %d} in slot %u.\n",
                    tmp_rect[i].left, tmp_rect[i].top, tmp_rect[i].right, tmp_rect[i].bottom, i);
        else
            ok(!tmp_rect[i].left && !tmp_rect[i].top && !tmp_rect[i].right && !tmp_rect[i].bottom,
                    "Got unexpected scissor rect {%d, %d, %d, %d} in slot %u.\n",
                    tmp_rect[i].left, tmp_rect[i].top, tmp_rect[i].right, tmp_rect[i].bottom, i);
    }
    ID3D10Device_RSGetViewports(device, &count, NULL);
    todo_wine ok(!count, "Got unexpected viewport count %u.\n", count);
    memset(tmp_viewport, 0x55, sizeof(tmp_viewport));
    count = D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D10Device_RSGetViewports(device, &count, tmp_viewport);
    for (i = 0; i < D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; ++i)
    {
        if (!i)
            todo_wine ok(!tmp_viewport[i].TopLeftX && !tmp_viewport[i].TopLeftY && !tmp_viewport[i].Width
                    && !tmp_viewport[i].Height && !tmp_viewport[i].MinDepth && !tmp_viewport[i].MaxDepth,
                    "Got unexpected viewport {%d, %d, %u, %u, %.8e, %.8e} in slot %u.\n",
                    tmp_viewport[i].TopLeftX, tmp_viewport[i].TopLeftY, tmp_viewport[i].Width,
                    tmp_viewport[i].Height, tmp_viewport[i].MinDepth, tmp_viewport[i].MaxDepth, i);
        else
            ok(!tmp_viewport[i].TopLeftX && !tmp_viewport[i].TopLeftY && !tmp_viewport[i].Width
                    && !tmp_viewport[i].Height && !tmp_viewport[i].MinDepth && !tmp_viewport[i].MaxDepth,
                    "Got unexpected viewport {%d, %d, %u, %u, %.8e, %.8e} in slot %u.\n",
                    tmp_viewport[i].TopLeftX, tmp_viewport[i].TopLeftY, tmp_viewport[i].Width,
                    tmp_viewport[i].Height, tmp_viewport[i].MinDepth, tmp_viewport[i].MaxDepth, i);
    }
    ID3D10Device_RSGetState(device, &tmp_rs_state);
    ok(!tmp_rs_state, "Got unexpected rasterizer state %p.\n", tmp_rs_state);

    ID3D10Device_SOGetTargets(device, D3D10_SO_BUFFER_SLOT_COUNT, tmp_buffer, offset);
    for (i = 0; i < D3D10_SO_BUFFER_SLOT_COUNT; ++i)
    {
        ok(!tmp_buffer[i], "Got unexpected stream output %p in slot %u.\n", tmp_buffer[i], i);
        ok(!offset[i], "Got unexpected stream output offset %u in slot %u.\n", offset[i], i);
    }

    ID3D10Device_GetPredication(device, &tmp_predicate, &predicate_value);
    ok(!tmp_predicate, "Got unexpected predicate %p.\n", tmp_predicate);
    ok(!predicate_value, "Got unexpected predicate value %#x.\n", predicate_value);

    /* Cleanup. */

    ID3D10Predicate_Release(predicate);
    ID3D10RasterizerState_Release(rs_state);
    ID3D10DepthStencilView_Release(dsv);
    ID3D10Texture2D_Release(ds_texture);

    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        ID3D10RenderTargetView_Release(rtv[i]);
        ID3D10Texture2D_Release(rt_texture[i]);
    }

    ID3D10DepthStencilState_Release(ds_state);
    ID3D10BlendState_Release(blend_state);
    ID3D10InputLayout_Release(input_layout);
    ID3D10VertexShader_Release(vs);
    ID3D10GeometryShader_Release(gs);
    ID3D10PixelShader_Release(ps);

    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        ID3D10SamplerState_Release(sampler[i]);
    }

    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ID3D10ShaderResourceView_Release(srv[i]);
    }

    for (i = 0; i < D3D10_SO_BUFFER_SLOT_COUNT; ++i)
    {
        ID3D10Buffer_Release(so_buffer[i]);
    }

    for (i = 0; i < D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        ID3D10Buffer_Release(buffer[i]);
    }

    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        ID3D10Buffer_Release(cb[i]);
    }

    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_blend(void)
{
    ID3D10RenderTargetView *backbuffer_rtv, *offscreen_rtv;
    ID3D10BlendState *src_blend, *dst_blend;
    ID3D10Texture2D *backbuffer, *offscreen;
    D3D10_SUBRESOURCE_DATA buffer_data;
    D3D10_TEXTURE2D_DESC texture_desc;
    ID3D10InputLayout *input_layout;
    D3D10_BUFFER_DESC buffer_desc;
    D3D10_BLEND_DESC blend_desc;
    unsigned int stride, offset;
    IDXGISwapChain *swapchain;
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    ID3D10Device *device;
    D3D10_VIEWPORT vp;
    ID3D10Buffer *vb;
    ULONG refcount;
    DWORD color;
    HWND window;
    HRESULT hr;

    static const DWORD vs_code[] =
    {
#if 0
        struct vs_out
        {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };

        struct vs_out main(float4 position : POSITION, float4 color : COLOR)
        {
            struct vs_out o;

            o.position = position;
            o.color = color;

            return o;
        }
#endif
        0x43425844, 0x5c73b061, 0x5c71125f, 0x3f8b345f, 0xce04b9ab, 0x00000001, 0x00000140, 0x00000003,
        0x0000002c, 0x0000007c, 0x000000d0, 0x4e475349, 0x00000048, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000041, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0x4c4f4300, 0xab00524f, 0x4e47534f,
        0x0000004c, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001, 0x00000003, 0x00000000,
        0x0000000f, 0x00000044, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x505f5653,
        0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052, 0x52444853, 0x00000068, 0x00010040, 0x0000001a,
        0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x001010f2, 0x00000001, 0x04000067, 0x001020f2,
        0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000001, 0x05000036, 0x001020f2, 0x00000000,
        0x00101e46, 0x00000000, 0x05000036, 0x001020f2, 0x00000001, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        struct vs_out
        {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };

        float4 main(struct vs_out i) : SV_TARGET
        {
            return i.color;
        }
#endif
        0x43425844, 0xe2087fa6, 0xa35fbd95, 0x8e585b3f, 0x67890f54, 0x00000001, 0x000000f4, 0x00000003,
        0x0000002c, 0x00000080, 0x000000b4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x00000038, 0x00000040,
        0x0000000e, 0x03001062, 0x001010f2, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const struct
    {
        struct vec3 position;
        DWORD diffuse;
    }
    quads[] =
    {
        /* quad1 */
        {{-1.0f, -1.0f, 0.1f}, 0x4000ff00},
        {{-1.0f,  0.0f, 0.1f}, 0x4000ff00},
        {{ 1.0f, -1.0f, 0.1f}, 0x4000ff00},
        {{ 1.0f,  0.0f, 0.1f}, 0x4000ff00},
        /* quad2 */
        {{-1.0f,  0.0f, 0.1f}, 0xc0ff0000},
        {{-1.0f,  1.0f, 0.1f}, 0xc0ff0000},
        {{ 1.0f,  0.0f, 0.1f}, 0xc0ff0000},
        {{ 1.0f,  1.0f, 0.1f}, 0xc0ff0000},
    };
    static const D3D10_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const float blend_factor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float red[] = {1.0f, 0.0f, 0.0f, 0.5f};

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d3d10core_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D10Texture2D, (void **)&backbuffer);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateInputLayout(device, layout_desc, sizeof(layout_desc) / sizeof(*layout_desc),
            vs_code, sizeof(vs_code), &input_layout);
    ok(SUCCEEDED(hr), "Failed to create input layout, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(quads);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.pSysMem = quads;
    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &buffer_data, &vb);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);
    hr = ID3D10Device_CreateVertexShader(device, vs_code, sizeof(vs_code), &vs);
    ok(SUCCEEDED(hr), "Failed to create vertex shader, hr %#x.\n", hr);
    hr = ID3D10Device_CreatePixelShader(device, ps_code, sizeof(ps_code), &ps);
    ok(SUCCEEDED(hr), "Failed to create pixel shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)backbuffer, NULL, &backbuffer_rtv);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);

    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.BlendEnable[0] = TRUE;
    blend_desc.SrcBlend = D3D10_BLEND_SRC_ALPHA;
    blend_desc.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    blend_desc.BlendOp = D3D10_BLEND_OP_ADD;
    blend_desc.SrcBlendAlpha = D3D10_BLEND_SRC_ALPHA;
    blend_desc.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
    blend_desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    blend_desc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;

    hr = ID3D10Device_CreateBlendState(device, &blend_desc, &src_blend);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);

    blend_desc.SrcBlend = D3D10_BLEND_DEST_ALPHA;
    blend_desc.DestBlend = D3D10_BLEND_INV_DEST_ALPHA;
    blend_desc.SrcBlendAlpha = D3D10_BLEND_DEST_ALPHA;
    blend_desc.DestBlendAlpha = D3D10_BLEND_INV_DEST_ALPHA;

    hr = ID3D10Device_CreateBlendState(device, &blend_desc, &dst_blend);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);

    ID3D10Device_OMSetRenderTargets(device, 1, &backbuffer_rtv, NULL);
    ID3D10Device_IASetInputLayout(device, input_layout);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    stride = sizeof(*quads);
    offset = 0;
    ID3D10Device_IASetVertexBuffers(device, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(device, vs);
    ID3D10Device_PSSetShader(device, ps);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 640;
    vp.Height = 480;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(device, 1, &vp);

    ID3D10Device_ClearRenderTargetView(device, backbuffer_rtv, red);

    ID3D10Device_OMSetBlendState(device, src_blend, blend_factor, D3D10_DEFAULT_SAMPLE_MASK);
    ID3D10Device_Draw(device, 4, 0);
    ID3D10Device_OMSetBlendState(device, dst_blend, blend_factor, D3D10_DEFAULT_SAMPLE_MASK);
    ID3D10Device_Draw(device, 4, 4);

    color = get_texture_color(backbuffer, 320, 360);
    ok(compare_color(color, 0x700040bf, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 320, 120);
    ok(compare_color(color, 0xa080007f, 1), "Got unexpected color 0x%08x.\n", color);

    texture_desc.Width = 128;
    texture_desc.Height = 128;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_RENDER_TARGET;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    /* DXGI_FORMAT_B8G8R8X8_UNORM is not supported on all implementations. */
    if (FAILED(ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &offscreen)))
    {
        skip("DXGI_FORMAT_B8G8R8X8_UNORM not supported, skipping tests.\n");
        goto done;
    }

    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)offscreen, NULL, &offscreen_rtv);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);

    ID3D10Device_OMSetRenderTargets(device, 1, &offscreen_rtv, NULL);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 128;
    vp.Height = 128;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(device, 1, &vp);

    ID3D10Device_ClearRenderTargetView(device, offscreen_rtv, red);

    ID3D10Device_OMSetBlendState(device, src_blend, blend_factor, D3D10_DEFAULT_SAMPLE_MASK);
    ID3D10Device_Draw(device, 4, 0);
    ID3D10Device_OMSetBlendState(device, dst_blend, blend_factor, D3D10_DEFAULT_SAMPLE_MASK);
    ID3D10Device_Draw(device, 4, 4);

    color = get_texture_color(offscreen, 64, 96) & 0x00ffffff;
    ok(compare_color(color, 0x00bf4000, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(offscreen, 64, 32) & 0x00ffffff;
    ok(compare_color(color, 0x000000ff, 1), "Got unexpected color 0x%08x.\n", color);

    ID3D10RenderTargetView_Release(offscreen_rtv);
    ID3D10Texture2D_Release(offscreen);
done:
    ID3D10BlendState_Release(dst_blend);
    ID3D10BlendState_Release(src_blend);
    ID3D10PixelShader_Release(ps);
    ID3D10VertexShader_Release(vs);
    ID3D10Buffer_Release(vb);
    ID3D10InputLayout_Release(input_layout);
    ID3D10RenderTargetView_Release(backbuffer_rtv);
    ID3D10Texture2D_Release(backbuffer);
    IDXGISwapChain_Release(swapchain);
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
    DestroyWindow(window);
}

static void test_texture(void)
{
    struct shader
    {
        const DWORD *code;
        size_t size;
    };
    struct texture
    {
        UINT width;
        UINT height;
        UINT miplevel_count;
        DXGI_FORMAT format;
        D3D10_SUBRESOURCE_DATA data[3];
    };

    D3D10_SUBRESOURCE_DATA resource_data;
    const struct texture *current_texture;
    D3D10_TEXTURE2D_DESC texture_desc;
    D3D10_SAMPLER_DESC sampler_desc;
    ID3D10InputLayout *input_layout;
    const struct shader *current_ps;
    ID3D10ShaderResourceView *srv;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10Texture2D *backbuffer;
    ID3D10RenderTargetView *rtv;
    ID3D10SamplerState *sampler;
    unsigned int stride, offset;
    struct texture_readback rb;
    IDXGISwapChain *swapchain;
    ID3D10Texture2D *texture;
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    ID3D10Buffer *vb, *cb;
    ID3D10Device *device;
    unsigned int i, x, y;
    struct vec4 miplevel;
    D3D10_VIEWPORT vp;
    ULONG refcount;
    HWND window;
    DWORD color;
    HRESULT hr;

    static const D3D10_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
#if 0
        float4 main(float4 position : POSITION) : SV_POSITION
        {
            return position;
        }
#endif
        0x43425844, 0xa7a2f22d, 0x83ff2560, 0xe61638bd, 0x87e3ce90, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_ld_code[] =
    {
#if 0
        Texture2D t;

        float miplevel;

        float4 main(float4 position : SV_POSITION) : SV_TARGET
        {
            float3 p;
            t.GetDimensions(miplevel, p.x, p.y, p.z);
            p.z = miplevel;
            p *= float3(position.x / 640.0f, position.y / 480.0f, 1.0f);
            return t.Load(int3(p));
        }
#endif
        0x43425844, 0xbdda6bdf, 0xc6ffcdf1, 0xa58596b3, 0x822383f0, 0x00000001, 0x000001ac, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x00000110, 0x00000040,
        0x00000044, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x04001858, 0x00107000, 0x00000000,
        0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
        0x02000068, 0x00000001, 0x0600001c, 0x00100012, 0x00000000, 0x0020800a, 0x00000000, 0x00000000,
        0x0700003d, 0x001000f2, 0x00000000, 0x0010000a, 0x00000000, 0x00107e46, 0x00000000, 0x07000038,
        0x00100032, 0x00000000, 0x00100046, 0x00000000, 0x00101046, 0x00000000, 0x06000036, 0x001000c2,
        0x00000000, 0x00208006, 0x00000000, 0x00000000, 0x0a000038, 0x001000f2, 0x00000000, 0x00100e46,
        0x00000000, 0x00004002, 0x3acccccd, 0x3b088889, 0x3f800000, 0x3f800000, 0x0500001b, 0x001000f2,
        0x00000000, 0x00100e46, 0x00000000, 0x0700002d, 0x001020f2, 0x00000000, 0x00100e46, 0x00000000,
        0x00107e46, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_ld_sint8_code[] =
    {
#if 0
        Texture2D<int4> t;

        float4 main(float4 position : SV_POSITION) : SV_TARGET
        {
            float3 p, s;
            int4 c;

            p = float3(position.x / 640.0f, position.y / 480.0f, 0.0f);
            t.GetDimensions(0, s.x, s.y, s.z);
            p *= s;

            c = t.Load(int3(p));
            return (max(c / (float4)127, (float4)-1) + (float4)1) / 2.0f;
        }
#endif
        0x43425844, 0xb3d0b0fc, 0x0e486f4a, 0xf67eec12, 0xfb9dd52f, 0x00000001, 0x00000240, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x000001a4, 0x00000040,
        0x00000069, 0x04001858, 0x00107000, 0x00000000, 0x00003333, 0x04002064, 0x00101032, 0x00000000,
        0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000002, 0x0700003d, 0x001000f2,
        0x00000000, 0x00004001, 0x00000000, 0x00107e46, 0x00000000, 0x0a000038, 0x00100032, 0x00000001,
        0x00101046, 0x00000000, 0x00004002, 0x3acccccd, 0x3b088889, 0x00000000, 0x00000000, 0x08000036,
        0x001000c2, 0x00000001, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x07000038,
        0x001000f2, 0x00000000, 0x00100f46, 0x00000000, 0x00100e46, 0x00000001, 0x0500001b, 0x001000f2,
        0x00000000, 0x00100e46, 0x00000000, 0x0700002d, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000,
        0x00107e46, 0x00000000, 0x0500002b, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000, 0x0a000038,
        0x001000f2, 0x00000000, 0x00100e46, 0x00000000, 0x00004002, 0x3c010204, 0x3c010204, 0x3c010204,
        0x3c010204, 0x0a000034, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000, 0x00004002, 0xbf800000,
        0xbf800000, 0xbf800000, 0xbf800000, 0x0a000000, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000,
        0x00004002, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x0a000038, 0x001020f2, 0x00000000,
        0x00100e46, 0x00000000, 0x00004002, 0x3f000000, 0x3f000000, 0x3f000000, 0x3f000000, 0x0100003e,
    };
    static const DWORD ps_ld_uint8_code[] =
    {
#if 0
        Texture2D<uint4> t;

        float4 main(float4 position : SV_POSITION) : SV_TARGET
        {
            float3 p, s;

            p = float3(position.x / 640.0f, position.y / 480.0f, 0.0f);
            t.GetDimensions(0, s.x, s.y, s.z);
            p *= s;

            return t.Load(int3(p)) / (float4)255;
        }
#endif
        0x43425844, 0xd09917eb, 0x4508a07e, 0xb0b7250a, 0x228c1f0e, 0x00000001, 0x000001c8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x0000012c, 0x00000040,
        0x0000004b, 0x04001858, 0x00107000, 0x00000000, 0x00004444, 0x04002064, 0x00101032, 0x00000000,
        0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000002, 0x0700003d, 0x001000f2,
        0x00000000, 0x00004001, 0x00000000, 0x00107e46, 0x00000000, 0x0a000038, 0x00100032, 0x00000001,
        0x00101046, 0x00000000, 0x00004002, 0x3acccccd, 0x3b088889, 0x00000000, 0x00000000, 0x08000036,
        0x001000c2, 0x00000001, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x07000038,
        0x001000f2, 0x00000000, 0x00100f46, 0x00000000, 0x00100e46, 0x00000001, 0x0500001b, 0x001000f2,
        0x00000000, 0x00100e46, 0x00000000, 0x0700002d, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000,
        0x00107e46, 0x00000000, 0x05000056, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000, 0x0a000038,
        0x001020f2, 0x00000000, 0x00100e46, 0x00000000, 0x00004002, 0x3b808081, 0x3b808081, 0x3b808081,
        0x3b808081, 0x0100003e,
    };
    static const DWORD ps_sample_code[] =
    {
#if 0
        Texture2D t;
        SamplerState s;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 p;

            p.x = position.x / 640.0f;
            p.y = position.y / 480.0f;
            return t.Sample(s, p);
        }
#endif
        0x43425844, 0x1ce9b612, 0xc8176faa, 0xd37844af, 0xdb515605, 0x00000001, 0x00000134, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000098, 0x00000040,
        0x00000026, 0x0300005a, 0x00106000, 0x00000000, 0x04001858, 0x00107000, 0x00000000, 0x00005555,
        0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068,
        0x00000001, 0x0a000038, 0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x00004002, 0x3acccccd,
        0x3b088889, 0x00000000, 0x00000000, 0x09000045, 0x001020f2, 0x00000000, 0x00100046, 0x00000000,
        0x00107e46, 0x00000000, 0x00106000, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_sample_b_code[] =
    {
#if 0
        Texture2D t;
        SamplerState s;

        float bias;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 p;

            p.x = position.x / 640.0f;
            p.y = position.y / 480.0f;
            return t.SampleBias(s, p, bias);
        }
#endif
        0x43425844, 0xc39b0686, 0x8244a7fc, 0x14c0b97a, 0x2900b3b7, 0x00000001, 0x00000150, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x000000b4, 0x00000040,
        0x0000002d, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300005a, 0x00106000, 0x00000000,
        0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001,
        0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0a000038, 0x00100032, 0x00000000,
        0x00101046, 0x00000000, 0x00004002, 0x3acccccd, 0x3b088889, 0x00000000, 0x00000000, 0x0c00004a,
        0x001020f2, 0x00000000, 0x00100046, 0x00000000, 0x00107e46, 0x00000000, 0x00106000, 0x00000000,
        0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_sample_l_code[] =
    {
#if 0
        Texture2D t;
        SamplerState s;

        float level;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 p;

            p.x = position.x / 640.0f;
            p.y = position.y / 480.0f;
            return t.SampleLevel(s, p, level);
        }
#endif
        0x43425844, 0x61e05d85, 0x2a7300fb, 0x0a83706b, 0x889d1683, 0x00000001, 0x00000150, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x000000b4, 0x00000040,
        0x0000002d, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300005a, 0x00106000, 0x00000000,
        0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001,
        0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0a000038, 0x00100032, 0x00000000,
        0x00101046, 0x00000000, 0x00004002, 0x3acccccd, 0x3b088889, 0x00000000, 0x00000000, 0x0c000048,
        0x001020f2, 0x00000000, 0x00100046, 0x00000000, 0x00107e46, 0x00000000, 0x00106000, 0x00000000,
        0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const struct shader ps_ld = {ps_ld_code, sizeof(ps_ld_code)};
    static const struct shader ps_ld_sint8 = {ps_ld_sint8_code, sizeof(ps_ld_sint8_code)};
    static const struct shader ps_ld_uint8 = {ps_ld_uint8_code, sizeof(ps_ld_uint8_code)};
    static const struct shader ps_sample = {ps_sample_code, sizeof(ps_sample_code)};
    static const struct shader ps_sample_b = {ps_sample_b_code, sizeof(ps_sample_b_code)};
    static const struct shader ps_sample_l = {ps_sample_l_code, sizeof(ps_sample_l_code)};
    static const struct
    {
        struct vec2 position;
    }
    quad[] =
    {
        {{-1.0f, -1.0f}},
        {{-1.0f,  1.0f}},
        {{ 1.0f, -1.0f}},
        {{ 1.0f,  1.0f}},
    };
    static const DWORD rgba_level_0[] =
    {
        0xff0000ff, 0xff00ffff, 0xff00ff00, 0xffffff00,
        0xffff0000, 0xffff00ff, 0xff000000, 0xff7f7f7f,
        0xffffffff, 0xffffffff, 0xffffffff, 0xff000000,
        0xffffffff, 0xff000000, 0xff000000, 0xff000000,
    };
    static const DWORD rgba_level_1[] =
    {
        0xffffffff, 0xff0000ff,
        0xff000000, 0xff00ff00,
    };
    static const DWORD rgba_level_2[] =
    {
        0xffff0000,
    };
    static const DWORD srgb_data[] =
    {
        0x00000000, 0xffffffff, 0xff000000, 0x7f7f7f7f,
        0xff010203, 0xff102030, 0xff0a0b0c, 0xff8090a0,
        0xffb1c4de, 0xfff0f1f2, 0xfffafdfe, 0xff5a560f,
        0xffd5ff00, 0xffc8f99f, 0xffaa00aa, 0xffdd55bb,
    };
    static const BYTE bc1_data[4 * 8] =
    {
        0x00, 0xf8, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00,
        0xe0, 0x07, 0xe0, 0x07, 0x00, 0x00, 0x00, 0x00,
        0x1f, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    };
    static const BYTE bc2_data[4 * 16] =
    {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xf8, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x07, 0xe0, 0x07, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    };
    static const BYTE bc3_data[4 * 16] =
    {
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x07, 0xe0, 0x07, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    };
    static const struct texture rgba_texture =
    {
        4, 4, 3, DXGI_FORMAT_R8G8B8A8_UNORM,
        {
            {rgba_level_0, 4 * sizeof(*rgba_level_0), 0},
            {rgba_level_1, 2 * sizeof(*rgba_level_1), 0},
            {rgba_level_2,     sizeof(*rgba_level_2), 0},
        }
    };
    static const struct texture srgb_texture = {4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            {{srgb_data, 4 * sizeof(*srgb_data)}}};
    static const struct texture bc1_texture = {8, 8, 1, DXGI_FORMAT_BC1_UNORM, {{bc1_data, 2 * 8}}};
    static const struct texture bc2_texture = {8, 8, 1, DXGI_FORMAT_BC2_UNORM, {{bc2_data, 2 * 16}}};
    static const struct texture bc3_texture = {8, 8, 1, DXGI_FORMAT_BC3_UNORM, {{bc3_data, 2 * 16}}};
    static const struct texture bc1_texture_srgb = {8, 8, 1, DXGI_FORMAT_BC1_UNORM_SRGB, {{bc1_data, 2 * 8}}};
    static const struct texture bc2_texture_srgb = {8, 8, 1, DXGI_FORMAT_BC2_UNORM_SRGB, {{bc2_data, 2 * 16}}};
    static const struct texture bc3_texture_srgb = {8, 8, 1, DXGI_FORMAT_BC3_UNORM_SRGB, {{bc3_data, 2 * 16}}};
    static const struct texture sint8_texture = {4, 4, 1, DXGI_FORMAT_R8G8B8A8_SINT,
            {{rgba_level_0, 4 * sizeof(*rgba_level_0)}}};
    static const struct texture uint8_texture = {4, 4, 1, DXGI_FORMAT_R8G8B8A8_UINT,
            {{rgba_level_0, 4 * sizeof(*rgba_level_0)}}};
    static const DWORD level_1_colors[] =
    {
        0xffffffff, 0xffffffff, 0xff0000ff, 0xff0000ff,
        0xffffffff, 0xffffffff, 0xff0000ff, 0xff0000ff,
        0xff000000, 0xff000000, 0xff00ff00, 0xff00ff00,
        0xff000000, 0xff000000, 0xff00ff00, 0xff00ff00,
    };
    static const DWORD lerp_1_2_colors[] =
    {
        0xffff7f7f, 0xffff7f7f, 0xff7f007f, 0xff7f007f,
        0xffff7f7f, 0xffff7f7f, 0xff7f007f, 0xff7f007f,
        0xff7f0000, 0xff7f0000, 0xff7f7f00, 0xff7f7f00,
        0xff7f0000, 0xff7f0000, 0xff7f7f00, 0xff7f7f00,
    };
    static const DWORD level_2_colors[] =
    {
        0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000,
        0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000,
        0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000,
        0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000,
    };
    static const DWORD srgb_colors[] =
    {
        0x00000001, 0xffffffff, 0xff000000, 0x7f363636,
        0xff000000, 0xff010408, 0xff010101, 0xff37475a,
        0xff708cba, 0xffdee0e2, 0xfff3fbfd, 0xff1a1801,
        0xffa9ff00, 0xff93f159, 0xff670067, 0xffb8177f,
    };
    static const DWORD bc_colors[] =
    {
        0xff0000ff, 0xff0000ff, 0xff00ff00, 0xff00ff00,
        0xff0000ff, 0xff0000ff, 0xff00ff00, 0xff00ff00,
        0xffff0000, 0xffff0000, 0xffffffff, 0xffffffff,
        0xffff0000, 0xffff0000, 0xffffffff, 0xffffffff,
    };
    static const DWORD sint8_colors[] =
    {
        0x7e80807e, 0x7e807e7e, 0x7e807e80, 0x7e7e7e80,
        0x7e7e8080, 0x7e7e7f7f, 0x7e808080, 0x7effffff,
        0x7e7e7e7e, 0x7e7e7e7e, 0x7e7e7e7e, 0x7e808080,
        0x7e7e7e7e, 0x7e7f7f7f, 0x7e7f7f7f, 0x7e7f7f7f,
    };
    static const DWORD zero_colors[4 * 4] = {0};
    static const float red[] = {1.0f, 0.0f, 0.0f, 0.5f};

    static const struct test
    {
        const struct shader *ps;
        const struct texture *texture;
        D3D10_FILTER filter;
        float lod_bias;
        float min_lod;
        float max_lod;
        float miplevel;
        const DWORD *expected_colors;
    }
    tests[] =
    {
        {&ps_ld,       &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, rgba_level_0},
        {&ps_ld,       &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  1.0f, level_1_colors},
        {&ps_ld,       &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  2.0f, level_2_colors},
        {&ps_ld,       &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  3.0f, zero_colors},
        {&ps_ld,       &srgb_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, srgb_colors},
        {&ps_ld,       &bc1_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_ld,       &bc1_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  1.0f, zero_colors},
        {&ps_ld,       &bc2_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_ld,       &bc2_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  1.0f, zero_colors},
        {&ps_ld,       &bc3_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_ld,       &bc3_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  1.0f, zero_colors},
        {&ps_ld,       &bc1_texture_srgb, D3D10_FILTER_MIN_MAG_MIP_POINT,     0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_ld,       &bc2_texture_srgb, D3D10_FILTER_MIN_MAG_MIP_POINT,     0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_ld,       &bc3_texture_srgb, D3D10_FILTER_MIN_MAG_MIP_POINT,     0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_ld_sint8, &sint8_texture, D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, sint8_colors},
        {&ps_ld_uint8, &uint8_texture, D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, rgba_level_0},
        {&ps_sample,   &bc1_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_sample,   &bc2_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_sample,   &bc3_texture,   D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, bc_colors},
        {&ps_sample,   &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, rgba_level_0},
        {&ps_sample,   &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  0.0f, rgba_level_0},
        {&ps_sample,   &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        2.0f, 0.0f, D3D10_FLOAT32_MAX,  0.0f, rgba_level_0},
        {&ps_sample,   &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        8.0f, 0.0f, D3D10_FLOAT32_MAX,  0.0f, level_1_colors},
        {&ps_sample,   &srgb_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  0.0f, srgb_colors},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  0.0f, rgba_level_0},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        8.0f, 0.0f, D3D10_FLOAT32_MAX,  0.0f, level_1_colors},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  8.0f, level_1_colors},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  8.4f, level_1_colors},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  8.5f, level_2_colors},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  9.0f, level_2_colors},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              2.0f,  1.0f, rgba_level_0},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              2.0f,  9.0f, level_2_colors},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              1.0f,  9.0f, level_1_colors},
        {&ps_sample_b, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f,              0.0f,  9.0f, rgba_level_0},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX, -1.0f, rgba_level_0},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  0.0f, rgba_level_0},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  0.4f, rgba_level_0},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  0.5f, level_1_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  1.0f, level_1_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  1.4f, level_1_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  1.5f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  2.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  3.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        0.0f, 0.0f, D3D10_FLOAT32_MAX,  4.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 0.0f, 0.0f, D3D10_FLOAT32_MAX,  1.5f, lerp_1_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 0.0f, D3D10_FLOAT32_MAX, -2.0f, rgba_level_0},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 0.0f, D3D10_FLOAT32_MAX, -1.0f, level_1_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 0.0f, D3D10_FLOAT32_MAX,  0.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 0.0f, D3D10_FLOAT32_MAX,  1.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 0.0f, D3D10_FLOAT32_MAX,  1.5f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 2.0f,              2.0f, -9.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 2.0f,              2.0f, -1.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 2.0f,              2.0f,  0.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 2.0f,              2.0f,  1.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR, 2.0f, 2.0f,              2.0f,  9.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        2.0f, 2.0f,              2.0f, -9.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        2.0f, 2.0f,              2.0f, -1.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        2.0f, 2.0f,              2.0f,  0.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        2.0f, 2.0f,              2.0f,  1.0f, level_2_colors},
        {&ps_sample_l, &rgba_texture,  D3D10_FILTER_MIN_MAG_MIP_POINT,        2.0f, 2.0f,              2.0f,  9.0f, level_2_colors},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }
    window = CreateWindowA("static", "d3d10core_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D10Texture2D, (void **)&backbuffer);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateInputLayout(device, layout_desc, sizeof(layout_desc) / sizeof(*layout_desc),
            vs_code, sizeof(vs_code), &input_layout);
    ok(SUCCEEDED(hr), "Failed to create input layout, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    resource_data.pSysMem = quad;
    resource_data.SysMemPitch = 0;
    resource_data.SysMemSlicePitch = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &vb);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(miplevel);
    buffer_desc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, NULL, &cb);
    ok(SUCCEEDED(hr), "Failed to create constant buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateVertexShader(device, vs_code, sizeof(vs_code), &vs);
    ok(SUCCEEDED(hr), "Failed to create vertex shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)backbuffer, NULL, &rtv);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);

    ID3D10Device_OMSetRenderTargets(device, 1, &rtv, NULL);
    ID3D10Device_IASetInputLayout(device, input_layout);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    stride = sizeof(*quad);
    offset = 0;
    ID3D10Device_IASetVertexBuffers(device, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(device, vs);
    ID3D10Device_PSSetConstantBuffers(device, 0, 1, &cb);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 640;
    vp.Height = 480;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(device, 1, &vp);

    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.MipLevels = 3;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    sampler_desc.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MipLODBias = 0.0f;
    sampler_desc.MaxAnisotropy = 0;
    sampler_desc.ComparisonFunc = D3D10_COMPARISON_NEVER;
    sampler_desc.BorderColor[0] = 0.0f;
    sampler_desc.BorderColor[1] = 0.0f;
    sampler_desc.BorderColor[2] = 0.0f;
    sampler_desc.BorderColor[3] = 0.0f;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = D3D10_FLOAT32_MAX;

    ps = NULL;
    srv = NULL;
    sampler = NULL;
    texture = NULL;
    current_ps = NULL;
    current_texture = NULL;
    for (i = 0; i < sizeof(tests) / sizeof(*tests); ++i)
    {
        const struct test *test = &tests[i];

        if (current_ps != test->ps)
        {
            if (ps)
                ID3D10PixelShader_Release(ps);

            current_ps = test->ps;

            hr = ID3D10Device_CreatePixelShader(device, current_ps->code, current_ps->size, &ps);
            ok(SUCCEEDED(hr), "Test %u: Failed to create pixel shader, hr %#x.\n", i, hr);

            ID3D10Device_PSSetShader(device, ps);
        }

        if (current_texture != test->texture)
        {
            if (texture)
                ID3D10Texture2D_Release(texture);
            if (srv)
                ID3D10ShaderResourceView_Release(srv);

            current_texture = test->texture;

            texture_desc.Width = current_texture->width;
            texture_desc.Height = current_texture->height;
            texture_desc.MipLevels = current_texture->miplevel_count;
            texture_desc.Format = current_texture->format;

            hr = ID3D10Device_CreateTexture2D(device, &texture_desc, current_texture->data, &texture);
            ok(SUCCEEDED(hr), "Test %u: Failed to create 2d texture, hr %#x.\n", i, hr);

            hr = ID3D10Device_CreateShaderResourceView(device, (ID3D10Resource *)texture, NULL, &srv);
            ok(SUCCEEDED(hr), "Test %u: Failed to create shader resource view, hr %#x.\n", i, hr);

            ID3D10Device_PSSetShaderResources(device, 0, 1, &srv);
        }

        if (!sampler || (sampler_desc.Filter != test->filter
                || sampler_desc.MipLODBias != test->lod_bias
                || sampler_desc.MinLOD != test->min_lod
                || sampler_desc.MaxLOD != test->max_lod))
        {
            if (sampler)
                ID3D10SamplerState_Release(sampler);

            sampler_desc.Filter = test->filter;
            sampler_desc.MipLODBias = test->lod_bias;
            sampler_desc.MinLOD = test->min_lod;
            sampler_desc.MaxLOD = test->max_lod;

            hr = ID3D10Device_CreateSamplerState(device, &sampler_desc, &sampler);
            ok(SUCCEEDED(hr), "Test %u: Failed to create sampler state, hr %#x.\n", i, hr);

            ID3D10Device_PSSetSamplers(device, 0, 1, &sampler);
        }

        miplevel.x = test->miplevel;
        ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)cb, 0, NULL, &miplevel, 0, 0);

        ID3D10Device_ClearRenderTargetView(device, rtv, red);
        ID3D10Device_Draw(device, 4, 0);

        get_texture_readback(backbuffer, &rb);
        for (x = 0; x < 4; ++x)
        {
            for (y = 0; y < 4; ++y)
            {
                color = get_readback_color(&rb, 80 + x * 160, 60 + y * 120);
                ok(compare_color(color, test->expected_colors[y * 4 + x], 1),
                        "Test %u: Got unexpected color 0x%08x at (%u, %u).\n", i, color, x, y);
            }
        }
        release_texture_readback(&rb);
    }
    ID3D10ShaderResourceView_Release(srv);
    ID3D10SamplerState_Release(sampler);
    ID3D10Texture2D_Release(texture);
    ID3D10PixelShader_Release(ps);

    ID3D10Buffer_Release(cb);
    ID3D10VertexShader_Release(vs);
    ID3D10Buffer_Release(vb);
    ID3D10InputLayout_Release(input_layout);
    ID3D10RenderTargetView_Release(rtv);
    ID3D10Texture2D_Release(backbuffer);
    IDXGISwapChain_Release(swapchain);
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
    DestroyWindow(window);
}

static void test_private_data(void)
{
    D3D10_TEXTURE2D_DESC texture_desc;
    ULONG refcount, expected_refcount;
    ID3D11Texture2D *d3d11_texture;
    ID3D11Device *d3d11_device;
    ID3D10Device *test_object;
    ID3D10Texture2D *texture;
    IDXGIDevice *dxgi_device;
    IDXGISurface *surface;
    ID3D10Device *device;
    IUnknown *ptr;
    HRESULT hr;
    UINT size;

    static const GUID test_guid =
            {0xfdb37466, 0x428f, 0x4edf, {0xa3, 0x7f, 0x9b, 0x1d, 0xf4, 0x88, 0xc5, 0xfc}};
    static const GUID test_guid2 =
            {0x2e5afac2, 0x87b5, 0x4c10, {0x9b, 0x4b, 0x89, 0xd7, 0xd1, 0x12, 0xe7, 0x2b}};
    static const DWORD data[] = {1, 2, 3, 4};

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    test_object = create_device();

    texture_desc.Width = 512;
    texture_desc.Height = 512;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);
    hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get IDXGISurface, hr %#x.\n", hr);

    /* SetPrivateData() with a pointer of NULL has the purpose of
     * FreePrivateData() in previous D3D versions. A successful clear returns
     * S_OK. A redundant clear S_FALSE. Setting a NULL interface is not
     * considered a clear but as setting an interface pointer that happens to
     * be NULL. */
    hr = ID3D10Device_SetPrivateData(device, &test_guid, 0, NULL);
    ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);
    hr = ID3D10Device_SetPrivateDataInterface(device, &test_guid, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ID3D10Device_SetPrivateData(device, &test_guid, ~0u, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ID3D10Device_SetPrivateData(device, &test_guid, ~0u, NULL);
    ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);

    hr = ID3D10Device_SetPrivateDataInterface(device, &test_guid, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    size = sizeof(ptr) * 2;
    ptr = (IUnknown *)0xdeadbeef;
    hr = ID3D10Device_GetPrivateData(device, &test_guid, &size, &ptr);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(!ptr, "Got unexpected pointer %p.\n", ptr);
    ok(size == sizeof(IUnknown *), "Got unexpected size %u.\n", size);

    hr = ID3D10Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    ok(SUCCEEDED(hr), "Failed to get DXGI device, hr %#x.\n", hr);
    size = sizeof(ptr) * 2;
    ptr = (IUnknown *)0xdeadbeef;
    hr = IDXGIDevice_GetPrivateData(dxgi_device, &test_guid, &size, &ptr);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(!ptr, "Got unexpected pointer %p.\n", ptr);
    ok(size == sizeof(IUnknown *), "Got unexpected size %u.\n", size);
    IDXGIDevice_Release(dxgi_device);

    refcount = get_refcount((IUnknown *)test_object);
    hr = ID3D10Device_SetPrivateDataInterface(device, &test_guid,
            (IUnknown *)test_object);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    expected_refcount = refcount + 1;
    refcount = get_refcount((IUnknown *)test_object);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    hr = ID3D10Device_SetPrivateDataInterface(device, &test_guid,
            (IUnknown *)test_object);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    refcount = get_refcount((IUnknown *)test_object);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);

    hr = ID3D10Device_SetPrivateDataInterface(device, &test_guid, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    --expected_refcount;
    refcount = get_refcount((IUnknown *)test_object);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);

    hr = ID3D10Device_SetPrivateDataInterface(device, &test_guid,
            (IUnknown *)test_object);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    size = sizeof(data);
    hr = ID3D10Device_SetPrivateData(device, &test_guid, size, data);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    refcount = get_refcount((IUnknown *)test_object);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    hr = ID3D10Device_SetPrivateData(device, &test_guid, 42, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ID3D10Device_SetPrivateData(device, &test_guid, 42, NULL);
    ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);

    hr = ID3D10Device_SetPrivateDataInterface(device, &test_guid,
            (IUnknown *)test_object);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ++expected_refcount;
    size = 2 * sizeof(ptr);
    ptr = NULL;
    hr = ID3D10Device_GetPrivateData(device, &test_guid, &size, &ptr);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(size == sizeof(test_object), "Got unexpected size %u.\n", size);
    ++expected_refcount;
    refcount = get_refcount((IUnknown *)test_object);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    IUnknown_Release(ptr);
    --expected_refcount;

    hr = ID3D10Device_QueryInterface(device, &IID_ID3D11Device, (void **)&d3d11_device);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Device should implement ID3D11Device.\n");
    if (SUCCEEDED(hr))
    {
        ptr = NULL;
        size = sizeof(ptr);
        hr = ID3D11Device_GetPrivateData(d3d11_device, &test_guid, &size, &ptr);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(ptr == (IUnknown *)test_object, "Got unexpected ptr %p, expected %p.\n", ptr, test_object);
        IUnknown_Release(ptr);
        ID3D11Device_Release(d3d11_device);
        refcount = get_refcount((IUnknown *)test_object);
        ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n",
                refcount, expected_refcount);
    }

    ptr = (IUnknown *)0xdeadbeef;
    size = 1;
    hr = ID3D10Device_GetPrivateData(device, &test_guid, &size, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(size == sizeof(device), "Got unexpected size %u.\n", size);
    size = 2 * sizeof(ptr);
    hr = ID3D10Device_GetPrivateData(device, &test_guid, &size, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(size == sizeof(device), "Got unexpected size %u.\n", size);
    refcount = get_refcount((IUnknown *)test_object);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);

    size = 1;
    hr = ID3D10Device_GetPrivateData(device, &test_guid, &size, &ptr);
    ok(hr == DXGI_ERROR_MORE_DATA, "Got unexpected hr %#x.\n", hr);
    ok(size == sizeof(device), "Got unexpected size %u.\n", size);
    ok(ptr == (IUnknown *)0xdeadbeef, "Got unexpected pointer %p.\n", ptr);
    hr = ID3D10Device_GetPrivateData(device, &test_guid2, NULL, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    size = 0xdeadbabe;
    hr = ID3D10Device_GetPrivateData(device, &test_guid2, &size, &ptr);
    ok(hr == DXGI_ERROR_NOT_FOUND, "Got unexpected hr %#x.\n", hr);
    ok(size == 0, "Got unexpected size %u.\n", size);
    ok(ptr == (IUnknown *)0xdeadbeef, "Got unexpected pointer %p.\n", ptr);
    hr = ID3D10Device_GetPrivateData(device, &test_guid, NULL, &ptr);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(ptr == (IUnknown *)0xdeadbeef, "Got unexpected pointer %p.\n", ptr);

    hr = ID3D10Texture2D_SetPrivateDataInterface(texture, &test_guid, (IUnknown *)test_object);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ptr = NULL;
    size = sizeof(ptr);
    hr = IDXGISurface_GetPrivateData(surface, &test_guid, &size, &ptr);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(ptr == (IUnknown *)test_object, "Got unexpected ptr %p, expected %p.\n", ptr, test_object);
    IUnknown_Release(ptr);

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_ID3D11Texture2D, (void **)&d3d11_texture);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Texture should implement ID3D11Texture2D.\n");
    if (SUCCEEDED(hr))
    {
        ptr = NULL;
        size = sizeof(ptr);
        hr = ID3D11Texture2D_GetPrivateData(d3d11_texture, &test_guid, &size, &ptr);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(ptr == (IUnknown *)test_object, "Got unexpected ptr %p, expected %p.\n", ptr, test_object);
        IUnknown_Release(ptr);
        ID3D11Texture2D_Release(d3d11_texture);
    }

    IDXGISurface_Release(surface);
    ID3D10Texture2D_Release(texture);
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
    refcount = ID3D10Device_Release(test_object);
    ok(!refcount, "Test object has %u references left.\n", refcount);
}

static void test_il_append_aligned(void)
{
    ID3D10RenderTargetView *backbuffer_rtv;
    D3D10_SUBRESOURCE_DATA resource_data;
    ID3D10InputLayout *input_layout;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10Texture2D *backbuffer;
    unsigned int stride, offset;
    IDXGISwapChain *swapchain;
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    ID3D10Device *device;
    ID3D10Buffer *vb[3];
    D3D10_VIEWPORT vp;
    ULONG refcount;
    DWORD color;
    HWND window;
    HRESULT hr;

    static const D3D10_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"COLOR",    2, DXGI_FORMAT_R32G32_FLOAT,       1, D3D10_APPEND_ALIGNED_ELEMENT,
                D3D10_INPUT_PER_INSTANCE_DATA, 2},
        {"COLOR",    3, DXGI_FORMAT_R32G32_FLOAT,       2, D3D10_APPEND_ALIGNED_ELEMENT,
                D3D10_INPUT_PER_INSTANCE_DATA, 1},
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D10_APPEND_ALIGNED_ELEMENT,
                D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32_FLOAT,       2, D3D10_APPEND_ALIGNED_ELEMENT,
                D3D10_INPUT_PER_INSTANCE_DATA, 1},
        {"COLOR",    1, DXGI_FORMAT_R32G32_FLOAT,       1, D3D10_APPEND_ALIGNED_ELEMENT,
                D3D10_INPUT_PER_INSTANCE_DATA, 2},
    };
    static const DWORD vs_code[] =
    {
#if 0
        struct vs_in
        {
            float4 position : POSITION;
            float2 color_xy : COLOR0;
            float2 color_zw : COLOR1;
            unsigned int instance_id : SV_INSTANCEID;
        };

        struct vs_out
        {
            float4 position : SV_POSITION;
            float2 color_xy : COLOR0;
            float2 color_zw : COLOR1;
        };

        struct vs_out main(struct vs_in i)
        {
            struct vs_out o;

            o.position = i.position;
            o.position.x += i.instance_id * 0.5;
            o.color_xy = i.color_xy;
            o.color_zw = i.color_zw;

            return o;
        }
#endif
        0x43425844, 0x52e3bf46, 0x6300403d, 0x624cffe4, 0xa4fc0013, 0x00000001, 0x00000214, 0x00000003,
        0x0000002c, 0x000000bc, 0x00000128, 0x4e475349, 0x00000088, 0x00000004, 0x00000008, 0x00000068,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000071, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x00000071, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
        0x00000303, 0x00000077, 0x00000000, 0x00000008, 0x00000001, 0x00000003, 0x00000101, 0x49534f50,
        0x4e4f4954, 0x4c4f4300, 0x5300524f, 0x4e495f56, 0x4e415453, 0x44494543, 0xababab00, 0x4e47534f,
        0x00000064, 0x00000003, 0x00000008, 0x00000050, 0x00000000, 0x00000001, 0x00000003, 0x00000000,
        0x0000000f, 0x0000005c, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x00000c03, 0x0000005c,
        0x00000001, 0x00000000, 0x00000003, 0x00000001, 0x0000030c, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4f4c4f43, 0xabab0052, 0x52444853, 0x000000e4, 0x00010040, 0x00000039, 0x0300005f, 0x001010f2,
        0x00000000, 0x0300005f, 0x00101032, 0x00000001, 0x0300005f, 0x00101032, 0x00000002, 0x04000060,
        0x00101012, 0x00000003, 0x00000008, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x03000065,
        0x00102032, 0x00000001, 0x03000065, 0x001020c2, 0x00000001, 0x02000068, 0x00000001, 0x05000056,
        0x00100012, 0x00000000, 0x0010100a, 0x00000003, 0x09000032, 0x00102012, 0x00000000, 0x0010000a,
        0x00000000, 0x00004001, 0x3f000000, 0x0010100a, 0x00000000, 0x05000036, 0x001020e2, 0x00000000,
        0x00101e56, 0x00000000, 0x05000036, 0x00102032, 0x00000001, 0x00101046, 0x00000001, 0x05000036,
        0x001020c2, 0x00000001, 0x00101406, 0x00000002, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        struct vs_out
        {
            float4 position : SV_POSITION;
            float2 color_xy : COLOR0;
            float2 color_zw : COLOR1;
        };

        float4 main(struct vs_out i) : SV_TARGET
        {
            return float4(i.color_xy.xy, i.color_zw.xy);
        }
#endif
        0x43425844, 0x64e48a09, 0xaa484d46, 0xe40a6e78, 0x9885edf3, 0x00000001, 0x00000118, 0x00000003,
        0x0000002c, 0x00000098, 0x000000cc, 0x4e475349, 0x00000064, 0x00000003, 0x00000008, 0x00000050,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x0000005c, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x0000005c, 0x00000001, 0x00000000, 0x00000003, 0x00000001,
        0x00000c0c, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052, 0x4e47534f, 0x0000002c,
        0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f,
        0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x00000044, 0x00000040, 0x00000011, 0x03001062,
        0x00101032, 0x00000001, 0x03001062, 0x001010c2, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const struct
    {
        struct vec4 position;
    }
    stream0[] =
    {
        {{-1.0f, -1.0f, 0.0f, 1.0f}},
        {{-1.0f,  1.0f, 0.0f, 1.0f}},
        {{-0.5f, -1.0f, 0.0f, 1.0f}},
        {{-0.5f,  1.0f, 0.0f, 1.0f}},
    };
    static const struct
    {
        struct vec2 color2;
        struct vec2 color1;
    }
    stream1[] =
    {
        {{0.5f, 0.5f}, {0.0f, 1.0f}},
        {{0.5f, 0.5f}, {1.0f, 1.0f}},
    };
    static const struct
    {
        struct vec2 color3;
        struct vec2 color0;
    }
    stream2[] =
    {
        {{0.5f, 0.5f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f}},
        {{0.5f, 0.5f}, {1.0f, 0.0f}},
    };
    static const float red[] = {1.0f, 0.0f, 0.0f, 0.5f};

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d3d10core_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D10Texture2D, (void **)&backbuffer);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateInputLayout(device, layout_desc, sizeof(layout_desc) / sizeof(*layout_desc),
            vs_code, sizeof(vs_code), &input_layout);
    ok(SUCCEEDED(hr), "Failed to create input layout, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(stream0);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    resource_data.pSysMem = stream0;
    resource_data.SysMemPitch = 0;
    resource_data.SysMemSlicePitch = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &vb[0]);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(stream1);
    resource_data.pSysMem = stream1;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &vb[1]);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(stream2);
    resource_data.pSysMem = stream2;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &vb[2]);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateVertexShader(device, vs_code, sizeof(vs_code), &vs);
    ok(SUCCEEDED(hr), "Failed to create vertex shader, hr %#x.\n", hr);
    hr = ID3D10Device_CreatePixelShader(device, ps_code, sizeof(ps_code), &ps);
    ok(SUCCEEDED(hr), "Failed to create pixel shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)backbuffer, NULL, &backbuffer_rtv);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);

    ID3D10Device_OMSetRenderTargets(device, 1, &backbuffer_rtv, NULL);
    ID3D10Device_IASetInputLayout(device, input_layout);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    offset = 0;
    stride = sizeof(*stream0);
    ID3D10Device_IASetVertexBuffers(device, 0, 1, &vb[0], &stride, &offset);
    stride = sizeof(*stream1);
    ID3D10Device_IASetVertexBuffers(device, 1, 1, &vb[1], &stride, &offset);
    stride = sizeof(*stream2);
    ID3D10Device_IASetVertexBuffers(device, 2, 1, &vb[2], &stride, &offset);
    ID3D10Device_VSSetShader(device, vs);
    ID3D10Device_PSSetShader(device, ps);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 640;
    vp.Height = 480;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(device, 1, &vp);

    ID3D10Device_ClearRenderTargetView(device, backbuffer_rtv, red);

    ID3D10Device_DrawInstanced(device, 4, 4, 0, 0);

    color = get_texture_color(backbuffer,  80, 240);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 240, 240);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 400, 240);
    ok(compare_color(color, 0xffff0000, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 560, 240);
    ok(compare_color(color, 0xffff00ff, 1), "Got unexpected color 0x%08x.\n", color);

    ID3D10PixelShader_Release(ps);
    ID3D10VertexShader_Release(vs);
    ID3D10Buffer_Release(vb[2]);
    ID3D10Buffer_Release(vb[1]);
    ID3D10Buffer_Release(vb[0]);
    ID3D10InputLayout_Release(input_layout);
    ID3D10RenderTargetView_Release(backbuffer_rtv);
    ID3D10Texture2D_Release(backbuffer);
    IDXGISwapChain_Release(swapchain);
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
    DestroyWindow(window);
}

static void test_fragment_coords(void)
{
    ID3D10RenderTargetView *backbuffer_rtv;
    D3D10_SUBRESOURCE_DATA resource_data;
    ID3D10InputLayout *input_layout;
    ID3D10PixelShader *ps, *ps_frac;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10Texture2D *backbuffer;
    unsigned int stride, offset;
    IDXGISwapChain *swapchain;
    ID3D10Buffer *vb, *ps_cb;
    ID3D10VertexShader *vs;
    ID3D10Device *device;
    D3D10_VIEWPORT vp;
    ULONG refcount;
    DWORD color;
    HWND window;
    HRESULT hr;

    static const D3D10_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
#if 0
        float4 main(float4 position : POSITION) : SV_POSITION
        {
            return position;
        }
#endif
        0x43425844, 0xa7a2f22d, 0x83ff2560, 0xe61638bd, 0x87e3ce90, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        float2 cutoff;

        float4 main(float4 position : SV_POSITION) : SV_TARGET
        {
            float4 ret = float4(0.0, 0.0, 0.0, 1.0);

            if (position.x > cutoff.x)
                ret.y = 1.0;
            if (position.y > cutoff.y)
                ret.z = 1.0;

            return ret;
        }
#endif
        0x43425844, 0x49fc9e51, 0x8068867d, 0xf20cfa39, 0xb8099e6b, 0x00000001, 0x00000144, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x000000a8, 0x00000040,
        0x0000002a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x04002064, 0x00101032, 0x00000000,
        0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x08000031, 0x00100032,
        0x00000000, 0x00208046, 0x00000000, 0x00000000, 0x00101046, 0x00000000, 0x0a000001, 0x00102062,
        0x00000000, 0x00100106, 0x00000000, 0x00004002, 0x00000000, 0x3f800000, 0x3f800000, 0x00000000,
        0x08000036, 0x00102092, 0x00000000, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x3f800000,
        0x0100003e,
    };
    static const DWORD ps_frac_code[] =
    {
#if 0
        float4 main(float4 position : SV_POSITION) : SV_TARGET
        {
            return float4(frac(position.xy), 0.0, 1.0);
        }
#endif
        0x43425844, 0x86d9d78a, 0x190b72c2, 0x50841fd6, 0xdc24022e, 0x00000001, 0x000000f8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x0000005c, 0x00000040,
        0x00000017, 0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
        0x0500001a, 0x00102032, 0x00000000, 0x00101046, 0x00000000, 0x08000036, 0x001020c2, 0x00000000,
        0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e,
    };
    static const struct
    {
        struct vec2 position;
    }
    quad[] =
    {
        {{-1.0f, -1.0f}},
        {{-1.0f,  1.0f}},
        {{ 1.0f, -1.0f}},
        {{ 1.0f,  1.0f}},
    };
    static const float red[] = {1.0f, 0.0f, 0.0f, 0.5f};
    struct vec4 cutoff = {320.0f, 240.0f, 0.0f, 0.0f};

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d3d10core_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D10Texture2D, (void **)&backbuffer);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateInputLayout(device, layout_desc, sizeof(layout_desc) / sizeof(*layout_desc),
            vs_code, sizeof(vs_code), &input_layout);
    ok(SUCCEEDED(hr), "Failed to create input layout, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    resource_data.pSysMem = quad;
    resource_data.SysMemPitch = 0;
    resource_data.SysMemSlicePitch = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &vb);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(cutoff);
    buffer_desc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;

    resource_data.pSysMem = &cutoff;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &ps_cb);
    ok(SUCCEEDED(hr), "Failed to create constant buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateVertexShader(device, vs_code, sizeof(vs_code), &vs);
    ok(SUCCEEDED(hr), "Failed to create vertex shader, hr %#x.\n", hr);
    hr = ID3D10Device_CreatePixelShader(device, ps_code, sizeof(ps_code), &ps);
    ok(SUCCEEDED(hr), "Failed to create pixel shader, hr %#x.\n", hr);
    hr = ID3D10Device_CreatePixelShader(device, ps_frac_code, sizeof(ps_frac_code), &ps_frac);
    ok(SUCCEEDED(hr), "Failed to create pixel shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)backbuffer, NULL, &backbuffer_rtv);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);

    ID3D10Device_OMSetRenderTargets(device, 1, &backbuffer_rtv, NULL);
    ID3D10Device_IASetInputLayout(device, input_layout);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    stride = sizeof(*quad);
    offset = 0;
    ID3D10Device_IASetVertexBuffers(device, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(device, vs);
    ID3D10Device_PSSetConstantBuffers(device, 0, 1, &ps_cb);
    ID3D10Device_PSSetShader(device, ps);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 640;
    vp.Height = 480;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(device, 1, &vp);

    ID3D10Device_ClearRenderTargetView(device, backbuffer_rtv, red);

    ID3D10Device_Draw(device, 4, 0);

    color = get_texture_color(backbuffer, 319, 239);
    ok(compare_color(color, 0xff000000, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 320, 239);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 319, 240);
    ok(compare_color(color, 0xffff0000, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 320, 240);
    ok(compare_color(color, 0xffffff00, 1), "Got unexpected color 0x%08x.\n", color);

    ID3D10Buffer_Release(ps_cb);
    cutoff.x = 16.0f;
    cutoff.y = 16.0f;
    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &ps_cb);
    ok(SUCCEEDED(hr), "Failed to create constant buffer, hr %#x.\n", hr);
    ID3D10Device_PSSetConstantBuffers(device, 0, 1, &ps_cb);

    ID3D10Device_Draw(device, 4, 0);

    color = get_texture_color(backbuffer, 14, 14);
    ok(compare_color(color, 0xff000000, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 18, 14);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 14, 18);
    ok(compare_color(color, 0xffff0000, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_texture_color(backbuffer, 18, 18);
    ok(compare_color(color, 0xffffff00, 1), "Got unexpected color 0x%08x.\n", color);

    ID3D10Device_PSSetShader(device, ps_frac);
    ID3D10Device_ClearRenderTargetView(device, backbuffer_rtv, red);

    ID3D10Device_Draw(device, 4, 0);

    color = get_texture_color(backbuffer, 14, 14);
    ok(compare_color(color, 0xff008080, 1), "Got unexpected color 0x%08x.\n", color);

    ID3D10Buffer_Release(ps_cb);
    ID3D10PixelShader_Release(ps_frac);
    ID3D10PixelShader_Release(ps);
    ID3D10VertexShader_Release(vs);
    ID3D10Buffer_Release(vb);
    ID3D10InputLayout_Release(input_layout);
    ID3D10RenderTargetView_Release(backbuffer_rtv);
    ID3D10Texture2D_Release(backbuffer);
    IDXGISwapChain_Release(swapchain);
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
    DestroyWindow(window);
}

static void test_update_subresource(void)
{
    ID3D10RenderTargetView *backbuffer_rtv;
    D3D10_SUBRESOURCE_DATA resource_data;
    D3D10_TEXTURE2D_DESC texture_desc;
    ID3D10SamplerState *sampler_state;
    ID3D10ShaderResourceView *ps_srv;
    D3D10_SAMPLER_DESC sampler_desc;
    ID3D10InputLayout *input_layout;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10Texture2D *backbuffer;
    unsigned int stride, offset;
    struct texture_readback rb;
    IDXGISwapChain *swapchain;
    ID3D10Texture2D *texture;
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    ID3D10Device *device;
    D3D10_VIEWPORT vp;
    unsigned int i, j;
    ID3D10Buffer *vb;
    ULONG refcount;
    D3D10_BOX box;
    DWORD color;
    HWND window;
    HRESULT hr;

    static const D3D10_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
#if 0
        float4 main(float4 position : POSITION) : SV_POSITION
        {
            return position;
        }
#endif
        0x43425844, 0xa7a2f22d, 0x83ff2560, 0xe61638bd, 0x87e3ce90, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        Texture2D t;
        SamplerState s;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 p;

            p.x = position.x / 640.0f;
            p.y = position.y / 480.0f;
            return t.Sample(s, p);
        }
#endif
        0x43425844, 0x1ce9b612, 0xc8176faa, 0xd37844af, 0xdb515605, 0x00000001, 0x00000134, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000098, 0x00000040,
        0x00000026, 0x0300005a, 0x00106000, 0x00000000, 0x04001858, 0x00107000, 0x00000000, 0x00005555,
        0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068,
        0x00000001, 0x0a000038, 0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x00004002, 0x3acccccd,
        0x3b088889, 0x00000000, 0x00000000, 0x09000045, 0x001020f2, 0x00000000, 0x00100046, 0x00000000,
        0x00107e46, 0x00000000, 0x00106000, 0x00000000, 0x0100003e,
    };
    static const struct
    {
        float x, y;
    }
    quad[] =
    {
        {-1.0f, -1.0f},
        {-1.0f,  1.0f},
        { 1.0f, -1.0f},
        { 1.0f,  1.0f},
    };
    static const float red[] = {1.0f, 0.0f, 0.0f, 0.5f};
    static const DWORD bitmap_data[] =
    {
        0xff0000ff, 0xff00ffff, 0xff00ff00, 0xffffff00,
        0xffff0000, 0xffff00ff, 0xff000000, 0xff7f7f7f,
        0xffffffff, 0xffffffff, 0xffffffff, 0xff000000,
        0xffffffff, 0xff000000, 0xff000000, 0xff000000,
    };
    static const DWORD expected_colors[] =
    {
        0xffffffff, 0xff000000, 0xffffffff, 0xff000000,
        0xff00ff00, 0xff0000ff, 0xff00ffff, 0x00000000,
        0xffffff00, 0xffff0000, 0xffff00ff, 0x00000000,
        0xff000000, 0xff7f7f7f, 0xffffffff, 0x00000000,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d3d10core_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D10Texture2D, (void **)&backbuffer);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateInputLayout(device, layout_desc, sizeof(layout_desc) / sizeof(*layout_desc),
            vs_code, sizeof(vs_code), &input_layout);
    ok(SUCCEEDED(hr), "Failed to create input layout, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    resource_data.pSysMem = quad;
    resource_data.SysMemPitch = 0;
    resource_data.SysMemSlicePitch = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &vb);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);

    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x\n", hr);

    hr = ID3D10Device_CreateShaderResourceView(device, (ID3D10Resource *)texture, NULL, &ps_srv);
    ok(SUCCEEDED(hr), "Failed to create shader resource view, hr %#x\n", hr);

    sampler_desc.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MipLODBias = 0.0f;
    sampler_desc.MaxAnisotropy = 0;
    sampler_desc.ComparisonFunc = D3D10_COMPARISON_NEVER;
    sampler_desc.BorderColor[0] = 0.0f;
    sampler_desc.BorderColor[1] = 0.0f;
    sampler_desc.BorderColor[2] = 0.0f;
    sampler_desc.BorderColor[3] = 0.0f;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = 0.0f;

    hr = ID3D10Device_CreateSamplerState(device, &sampler_desc, &sampler_state);
    ok(SUCCEEDED(hr), "Failed to create sampler state, hr %#x.\n", hr);

    hr = ID3D10Device_CreateVertexShader(device, vs_code, sizeof(vs_code), &vs);
    ok(SUCCEEDED(hr), "Failed to create vertex shader, hr %#x.\n", hr);
    hr = ID3D10Device_CreatePixelShader(device, ps_code, sizeof(ps_code), &ps);
    ok(SUCCEEDED(hr), "Failed to create pixel shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)backbuffer, NULL, &backbuffer_rtv);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);

    ID3D10Device_OMSetRenderTargets(device, 1, &backbuffer_rtv, NULL);
    ID3D10Device_IASetInputLayout(device, input_layout);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    stride = sizeof(*quad);
    offset = 0;
    ID3D10Device_IASetVertexBuffers(device, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(device, vs);
    ID3D10Device_PSSetShaderResources(device, 0, 1, &ps_srv);
    ID3D10Device_PSSetSamplers(device, 0, 1, &sampler_state);
    ID3D10Device_PSSetShader(device, ps);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 640;
    vp.Height = 480;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(device, 1, &vp);

    ID3D10Device_ClearRenderTargetView(device, backbuffer_rtv, red);

    ID3D10Device_Draw(device, 4, 0);
    get_texture_readback(backbuffer, &rb);
    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            color = get_readback_color(&rb, 80 + j * 160, 60 + i * 120);
            ok(compare_color(color, 0x00000000, 0),
                    "Got unexpected color 0x%08x at (%u, %u).\n", color, j, i);
        }
    }
    release_texture_readback(&rb);

    set_box(&box, 1, 1, 0, 3, 3, 1);
    ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)texture, 0, &box,
            bitmap_data, 4 * sizeof(*bitmap_data), 0);
    set_box(&box, 0, 3, 0, 3, 4, 1);
    ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)texture, 0, &box,
            &bitmap_data[6], 4 * sizeof(*bitmap_data), 0);
    set_box(&box, 0, 0, 0, 4, 1, 1);
    ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)texture, 0, &box,
            &bitmap_data[10], 4 * sizeof(*bitmap_data), 0);
    set_box(&box, 0, 1, 0, 1, 3, 1);
    ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)texture, 0, &box,
            &bitmap_data[2], sizeof(*bitmap_data), 0);
    set_box(&box, 4, 4, 0, 3, 1, 1);
    ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)texture, 0, &box,
            bitmap_data, sizeof(*bitmap_data), 0);
    set_box(&box, 0, 0, 0, 4, 4, 0);
    ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)texture, 0, &box,
            bitmap_data, 4 * sizeof(*bitmap_data), 0);
    ID3D10Device_Draw(device, 4, 0);
    get_texture_readback(backbuffer, &rb);
    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            color = get_readback_color(&rb, 80 + j * 160, 60 + i * 120);
            ok(compare_color(color, expected_colors[j + i * 4], 1),
                    "Got unexpected color 0x%08x at (%u, %u), expected 0x%08x.\n",
                    color, j, i, expected_colors[j + i * 4]);
        }
    }
    release_texture_readback(&rb);

    ID3D10Device_UpdateSubresource(device, (ID3D10Resource *)texture, 0, NULL,
            bitmap_data, 4 * sizeof(*bitmap_data), 0);
    ID3D10Device_Draw(device, 4, 0);
    get_texture_readback(backbuffer, &rb);
    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            color = get_readback_color(&rb, 80 + j * 160, 60 + i * 120);
            ok(compare_color(color, bitmap_data[j + i * 4], 1),
                    "Got unexpected color 0x%08x at (%u, %u), expected 0x%08x.\n",
                    color, j, i, bitmap_data[j + i * 4]);
        }
    }
    release_texture_readback(&rb);

    ID3D10PixelShader_Release(ps);
    ID3D10VertexShader_Release(vs);
    ID3D10SamplerState_Release(sampler_state);
    ID3D10ShaderResourceView_Release(ps_srv);
    ID3D10Texture2D_Release(texture);
    ID3D10Buffer_Release(vb);
    ID3D10InputLayout_Release(input_layout);
    ID3D10RenderTargetView_Release(backbuffer_rtv);
    ID3D10Texture2D_Release(backbuffer);
    IDXGISwapChain_Release(swapchain);
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
    DestroyWindow(window);
}

static void test_copy_subresource_region(void)
{
    ID3D10Texture2D *dst_texture, *src_texture;
    ID3D10RenderTargetView *backbuffer_rtv;
    D3D10_SUBRESOURCE_DATA resource_data;
    D3D10_TEXTURE2D_DESC texture_desc;
    ID3D10SamplerState *sampler_state;
    ID3D10ShaderResourceView *ps_srv;
    D3D10_SAMPLER_DESC sampler_desc;
    ID3D10InputLayout *input_layout;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10Texture2D *backbuffer;
    unsigned int stride, offset;
    struct texture_readback rb;
    IDXGISwapChain *swapchain;
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    ID3D10Device *device;
    D3D10_VIEWPORT vp;
    unsigned int i, j;
    ID3D10Buffer *vb;
    ULONG refcount;
    D3D10_BOX box;
    DWORD color;
    HWND window;
    HRESULT hr;

    static const D3D10_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
#if 0
        float4 main(float4 position : POSITION) : SV_POSITION
        {
            return position;
        }
#endif
        0x43425844, 0xa7a2f22d, 0x83ff2560, 0xe61638bd, 0x87e3ce90, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        Texture2D t;
        SamplerState s;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 p;

            p.x = position.x / 640.0f;
            p.y = position.y / 480.0f;
            return t.Sample(s, p);
        }
#endif
        0x43425844, 0x1ce9b612, 0xc8176faa, 0xd37844af, 0xdb515605, 0x00000001, 0x00000134, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000098, 0x00000040,
        0x00000026, 0x0300005a, 0x00106000, 0x00000000, 0x04001858, 0x00107000, 0x00000000, 0x00005555,
        0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068,
        0x00000001, 0x0a000038, 0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x00004002, 0x3acccccd,
        0x3b088889, 0x00000000, 0x00000000, 0x09000045, 0x001020f2, 0x00000000, 0x00100046, 0x00000000,
        0x00107e46, 0x00000000, 0x00106000, 0x00000000, 0x0100003e,
    };
    static const struct
    {
        float x, y;
    }
    quad[] =
    {
        {-1.0f, -1.0f},
        {-1.0f,  1.0f},
        { 1.0f, -1.0f},
        { 1.0f,  1.0f},
    };
    static const float red[] = {1.0f, 0.0f, 0.0f, 0.5f};
    static const DWORD bitmap_data[] =
    {
        0xff0000ff, 0xff00ffff, 0xff00ff00, 0xffffff00,
        0xffff0000, 0xffff00ff, 0xff000000, 0xff7f7f7f,
        0xffffffff, 0xffffffff, 0xffffffff, 0xff000000,
        0xffffffff, 0xff000000, 0xff000000, 0xff000000,
    };
    static const DWORD expected_colors[] =
    {
        0xffffffff, 0xff000000, 0xff000000, 0xff000000,
        0xffffff00, 0xff0000ff, 0xff00ffff, 0x00000000,
        0xff7f7f7f, 0xffff0000, 0xffff00ff, 0xff7f7f7f,
        0xffffffff, 0xffffffff, 0xff000000, 0x00000000,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }
    window = CreateWindowA("static", "d3d10core_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D10Texture2D, (void **)&backbuffer);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    hr = ID3D10Device_CreateInputLayout(device, layout_desc, sizeof(layout_desc) / sizeof(*layout_desc),
            vs_code, sizeof(vs_code), &input_layout);
    ok(SUCCEEDED(hr), "Failed to create input layout, hr %#x.\n", hr);

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    resource_data.pSysMem = quad;
    resource_data.SysMemPitch = 0;
    resource_data.SysMemSlicePitch = 0;

    hr = ID3D10Device_CreateBuffer(device, &buffer_desc, &resource_data, &vb);
    ok(SUCCEEDED(hr), "Failed to create vertex buffer, hr %#x.\n", hr);

    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &dst_texture);
    ok(SUCCEEDED(hr), "Failed to create 2d texture, hr %#x.\n", hr);

    texture_desc.Usage = D3D10_USAGE_IMMUTABLE;

    resource_data.pSysMem = bitmap_data;
    resource_data.SysMemPitch = 4 * sizeof(*bitmap_data);

    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, &resource_data, &src_texture);
    ok(SUCCEEDED(hr), "Failed to create 2d texture, hr %#x.\n", hr);

    hr = ID3D10Device_CreateShaderResourceView(device, (ID3D10Resource *)dst_texture, NULL, &ps_srv);
    ok(SUCCEEDED(hr), "Failed to create shader resource view, hr %#x.\n", hr);

    sampler_desc.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MipLODBias = 0.0f;
    sampler_desc.MaxAnisotropy = 0;
    sampler_desc.ComparisonFunc = D3D10_COMPARISON_NEVER;
    sampler_desc.BorderColor[0] = 0.0f;
    sampler_desc.BorderColor[1] = 0.0f;
    sampler_desc.BorderColor[2] = 0.0f;
    sampler_desc.BorderColor[3] = 0.0f;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = 0.0f;

    hr = ID3D10Device_CreateSamplerState(device, &sampler_desc, &sampler_state);
    ok(SUCCEEDED(hr), "Failed to create sampler state, hr %#x.\n", hr);

    hr = ID3D10Device_CreateVertexShader(device, vs_code, sizeof(vs_code), &vs);
    ok(SUCCEEDED(hr), "Failed to create vertex shader, hr %#x.\n", hr);
    hr = ID3D10Device_CreatePixelShader(device, ps_code, sizeof(ps_code), &ps);
    ok(SUCCEEDED(hr), "Failed to create pixel shader, hr %#x.\n", hr);

    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)backbuffer, NULL, &backbuffer_rtv);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);

    ID3D10Device_OMSetRenderTargets(device, 1, &backbuffer_rtv, NULL);
    ID3D10Device_IASetInputLayout(device, input_layout);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    stride = sizeof(*quad);
    offset = 0;
    ID3D10Device_IASetVertexBuffers(device, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(device, vs);
    ID3D10Device_PSSetShaderResources(device, 0, 1, &ps_srv);
    ID3D10Device_PSSetSamplers(device, 0, 1, &sampler_state);
    ID3D10Device_PSSetShader(device, ps);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 640;
    vp.Height = 480;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ID3D10Device_RSSetViewports(device, 1, &vp);

    ID3D10Device_ClearRenderTargetView(device, backbuffer_rtv, red);

    set_box(&box, 0, 0, 0, 2, 2, 1);
    ID3D10Device_CopySubresourceRegion(device, (ID3D10Resource *)dst_texture, 0,
            1, 1, 0, (ID3D10Resource *)src_texture, 0, &box);
    set_box(&box, 1, 2, 0, 4, 3, 1);
    ID3D10Device_CopySubresourceRegion(device, (ID3D10Resource *)dst_texture, 0,
            0, 3, 0, (ID3D10Resource *)src_texture, 0, &box);
    set_box(&box, 0, 3, 0, 4, 4, 1);
    ID3D10Device_CopySubresourceRegion(device, (ID3D10Resource *)dst_texture, 0,
            0, 0, 0, (ID3D10Resource *)src_texture, 0, &box);
    set_box(&box, 3, 0, 0, 4, 2, 1);
    ID3D10Device_CopySubresourceRegion(device, (ID3D10Resource *)dst_texture, 0,
            0, 1, 0, (ID3D10Resource *)src_texture, 0, &box);
    set_box(&box, 3, 1, 0, 4, 2, 1);
    ID3D10Device_CopySubresourceRegion(device, (ID3D10Resource *)dst_texture, 0,
            3, 2, 0, (ID3D10Resource *)src_texture, 0, &box);
    set_box(&box, 0, 0, 0, 4, 4, 0);
    ID3D10Device_CopySubresourceRegion(device, (ID3D10Resource *)dst_texture, 0,
            0, 0, 0, (ID3D10Resource *)src_texture, 0, &box);
    ID3D10Device_Draw(device, 4, 0);
    get_texture_readback(backbuffer, &rb);
    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            color = get_readback_color(&rb, 80 + j * 160, 60 + i * 120);
            ok(compare_color(color, expected_colors[j + i * 4], 1),
                    "Got unexpected color 0x%08x at (%u, %u), expected 0x%08x.\n",
                    color, j, i, expected_colors[j + i * 4]);
        }
    }
    release_texture_readback(&rb);

    ID3D10Device_CopySubresourceRegion(device, (ID3D10Resource *)dst_texture, 0,
            0, 0, 0, (ID3D10Resource *)src_texture, 0, NULL);
    ID3D10Device_Draw(device, 4, 0);
    get_texture_readback(backbuffer, &rb);
    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            color = get_readback_color(&rb, 80 + j * 160, 60 + i * 120);
            ok(compare_color(color, bitmap_data[j + i * 4], 1),
                    "Got unexpected color 0x%08x at (%u, %u), expected 0x%08x.\n",
                    color, j, i, bitmap_data[j + i * 4]);
        }
    }
    release_texture_readback(&rb);

    ID3D10PixelShader_Release(ps);
    ID3D10VertexShader_Release(vs);
    ID3D10SamplerState_Release(sampler_state);
    ID3D10ShaderResourceView_Release(ps_srv);
    ID3D10Texture2D_Release(dst_texture);
    ID3D10Texture2D_Release(src_texture);
    ID3D10Buffer_Release(vb);
    ID3D10InputLayout_Release(input_layout);
    ID3D10RenderTargetView_Release(backbuffer_rtv);
    ID3D10Texture2D_Release(backbuffer);
    IDXGISwapChain_Release(swapchain);
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
    DestroyWindow(window);
}

static void test_multisample_init(void)
{
    D3D10_TEXTURE2D_DESC desc;
    ID3D10Texture2D *backbuffer, *multi;
    ID3D10Device *device;
    ULONG refcount;
    DWORD color;
    HRESULT hr;
    unsigned int x, y;
    struct texture_readback rb;
    BOOL all_zero = TRUE;
    UINT count = 0;
    HWND window;
    IDXGISwapChain *swapchain;
    ID3D10RenderTargetView *rtview;
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 2, &count);
    ok(SUCCEEDED(hr), "Failed to get quality levels, hr %#x.\n", hr);
    if (!count)
    {
        skip("Multisampling not supported for DXGI_FORMAT_R8G8B8A8_UNORM, skipping tests.\n");
        goto done;
    }

    window = CreateWindowA("static", "d3d10core_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D10Texture2D, (void **)&backbuffer);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource *)backbuffer, NULL, &rtview);
    ok(SUCCEEDED(hr), "Failed to create rendertarget view, hr %#x.\n", hr);
    ID3D10Device_ClearRenderTargetView(device, rtview, white);

    desc.Width = 640;
    desc.Height = 480;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 2;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    hr = ID3D10Device_CreateTexture2D(device, &desc, NULL, &multi);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    ID3D10Device_ResolveSubresource(device, (ID3D10Resource *)backbuffer, 0,
            (ID3D10Resource *)multi, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

    get_texture_readback(backbuffer, &rb);
    for (y = 0; y < 480; ++y)
    {
        for (x = 0; x < 640; ++x)
        {
            color = get_readback_color(&rb, x, y);
            if (!compare_color(color, 0x00000000, 0))
            {
                all_zero = FALSE;
                break;
            }
        }
        if (!all_zero)
            break;
    }
    release_texture_readback(&rb);
    todo_wine ok(all_zero, "Got unexpected color 0x%08x, position %ux%u.\n", color, x, y);

    ID3D10RenderTargetView_Release(rtview);
    ID3D10Texture2D_Release(backbuffer);
    IDXGISwapChain_Release(swapchain);
    ID3D10Texture2D_Release(multi);
    DestroyWindow(window);
done:
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_check_multisample_quality_levels(void)
{
    ID3D10Device *device;
    UINT quality_levels;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 2, &quality_levels);
    if (!quality_levels)
    {
        skip("Multisampling not supported for DXGI_FORMAT_R8G8B8A8_UNORM, skipping test.\n");
        goto done;
    }

    quality_levels = 0xdeadbeef;
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_UNKNOWN, 2, &quality_levels);
    todo_wine ok(SUCCEEDED(hr), "Got unexpected hr %#x.\n", hr);
    ok(!quality_levels, "Got unexpected quality_levels %u.\n", quality_levels);
    quality_levels = 0xdeadbeef;
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, 65536, 2, &quality_levels);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    todo_wine ok(quality_levels == 0xdeadbeef, "Got unexpected quality_levels %u.\n", quality_levels);

    quality_levels = 0xdeadbeef;
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 0, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &quality_levels);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!quality_levels, "Got unexpected quality_levels %u.\n", quality_levels);

    quality_levels = 0xdeadbeef;
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 1, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 1, &quality_levels);
    ok(SUCCEEDED(hr), "Got unexpected hr %#x.\n", hr);
    ok(quality_levels == 1, "Got unexpected quality_levels %u.\n", quality_levels);

    quality_levels = 0xdeadbeef;
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 2, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 2, &quality_levels);
    ok(SUCCEEDED(hr), "Got unexpected hr %#x.\n", hr);
    ok(quality_levels, "Got unexpected quality_levels %u.\n", quality_levels);

    /* We assume 15 samples multisampling is never supported in practice. */
    quality_levels = 0xdeadbeef;
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 15, &quality_levels);
    ok(SUCCEEDED(hr), "Got unexpected hr %#x.\n", hr);
    ok(!quality_levels, "Got unexpected quality_levels %u.\n", quality_levels);
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 32, &quality_levels);
    ok(SUCCEEDED(hr), "Got unexpected hr %#x.\n", hr);
    quality_levels = 0xdeadbeef;
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 33, &quality_levels);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!quality_levels, "Got unexpected quality_levels %u.\n", quality_levels);
    quality_levels = 0xdeadbeef;
    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_R8G8B8A8_UNORM, 64, &quality_levels);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!quality_levels, "Got unexpected quality_levels %u.\n", quality_levels);

    hr = ID3D10Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_BC3_UNORM, 2, &quality_levels);
    ok(SUCCEEDED(hr), "Got unexpected hr %#x.\n", hr);
    ok(!quality_levels, "Got unexpected quality_levels %u.\n", quality_levels);

done:
    refcount = ID3D10Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

START_TEST(device)
{
    test_feature_level();
    test_device_interfaces();
    test_create_texture2d();
    test_texture2d_interfaces();
    test_create_texture3d();
    test_buffer_interfaces();
    test_create_depthstencil_view();
    test_depthstencil_view_interfaces();
    test_create_rendertarget_view();
    test_create_shader_resource_view();
    test_create_shader();
    test_create_sampler_state();
    test_create_blend_state();
    test_create_depthstencil_state();
    test_create_rasterizer_state();
    test_create_predicate();
    test_device_removed_reason();
    test_scissor();
    test_clear_state();
    test_blend();
    test_texture();
    test_private_data();
    test_il_append_aligned();
    test_fragment_coords();
    test_update_subresource();
    test_copy_subresource_region();
    test_multisample_init();
    test_check_multisample_quality_levels();
}

/*
 * Copyright 2008-2012 Henri Verbeet for CodeWeavers
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
 *
 */

#include "config.h"
#include "wine/port.h"

#define NONAMELESSUNION
#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

static void STDMETHODCALLTYPE d3d_null_wined3d_object_destroyed(void *parent) {}

const struct wined3d_parent_ops d3d_null_wined3d_parent_ops =
{
    d3d_null_wined3d_object_destroyed,
};

/* ID3D11DeviceContext - immediate context methods */

static inline struct d3d11_immediate_context *impl_from_ID3D11DeviceContext(ID3D11DeviceContext *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_immediate_context, ID3D11DeviceContext_iface);
}

static inline struct d3d_device *device_from_immediate_ID3D11DeviceContext(ID3D11DeviceContext *iface)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    return CONTAINING_RECORD(context, struct d3d_device, immediate_context);
}

static HRESULT STDMETHODCALLTYPE d3d11_immediate_context_QueryInterface(ID3D11DeviceContext *iface,
        REFIID riid, void **out)
{
    TRACE("iface %p, riid %s, out %p.\n", iface, debugstr_guid(riid), out);

    if (IsEqualGUID(riid, &IID_ID3D11DeviceContext)
            || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D11DeviceContext_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_immediate_context_AddRef(ID3D11DeviceContext *iface)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    ULONG refcount = InterlockedIncrement(&context->refcount);

    TRACE("%p increasing refcount to %u.\n", context, refcount);

    if (refcount == 1)
    {
        ID3D11Device_AddRef(&device->ID3D11Device_iface);
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_immediate_context_Release(ID3D11DeviceContext *iface)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    ULONG refcount = InterlockedDecrement(&context->refcount);

    TRACE("%p decreasing refcount to %u.\n", context, refcount);

    if (!refcount)
    {
        ID3D11Device_Release(&device->ID3D11Device_iface);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GetDevice(ID3D11DeviceContext *iface, ID3D11Device **device)
{
    struct d3d_device *device_object = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = &device_object->ID3D11Device_iface;
    ID3D11Device_AddRef(*device);
}

static HRESULT STDMETHODCALLTYPE d3d11_immediate_context_GetPrivateData(ID3D11DeviceContext *iface, REFGUID guid,
        UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_immediate_context_SetPrivateData(ID3D11DeviceContext *iface, REFGUID guid,
        UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_immediate_context_SetPrivateDataInterface(ID3D11DeviceContext *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d11_immediate_context_VSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer *const *buffers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D11Buffer(buffers[i]);

        wined3d_device_set_vs_cb(device->wined3d_device, start_slot + i,
                buffer ? buffer->wined3d_buffer : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_PSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView *const *views)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n",
            iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct d3d_shader_resource_view *view = unsafe_impl_from_ID3D11ShaderResourceView(views[i]);

        wined3d_device_set_ps_resource_view(device->wined3d_device, start_slot + i,
                view ? view->wined3d_view : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_PSSetShader(ID3D11DeviceContext *iface,
        ID3D11PixelShader *shader, ID3D11ClassInstance *const *class_instances, UINT class_instance_count)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_pixel_shader *ps = unsafe_impl_from_ID3D11PixelShader(shader);

    TRACE("iface %p, shader %p, class_instances %p, class_instance_count %u.\n",
            iface, shader, class_instances, class_instance_count);

    if (class_instances)
        FIXME("Dynamic linking is not implemented yet.\n");

    wined3d_mutex_lock();
    wined3d_device_set_pixel_shader(device->wined3d_device, ps ? ps->wined3d_shader : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_PSSetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState *const *samplers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler = unsafe_impl_from_ID3D11SamplerState(samplers[i]);

        wined3d_device_set_ps_sampler(device->wined3d_device, start_slot + i,
                sampler ? sampler->wined3d_sampler : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_VSSetShader(ID3D11DeviceContext *iface,
        ID3D11VertexShader *shader, ID3D11ClassInstance *const *class_instances, UINT class_instance_count)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_vertex_shader *vs = unsafe_impl_from_ID3D11VertexShader(shader);

    TRACE("iface %p, shader %p, class_instances %p, class_instance_count %u.\n",
            iface, shader, class_instances, class_instance_count);

    if (class_instances)
        FIXME("Dynamic linking is not implemented yet.\n");

    wined3d_mutex_lock();
    wined3d_device_set_vertex_shader(device->wined3d_device, vs ? vs->wined3d_shader : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DrawIndexed(ID3D11DeviceContext *iface,
        UINT index_count, UINT start_index_location, INT base_vertex_location)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, index_count %u, start_index_location %u, base_vertex_location %d.\n",
            iface, index_count, start_index_location, base_vertex_location);

    wined3d_mutex_lock();
    wined3d_device_set_base_vertex_index(device->wined3d_device, base_vertex_location);
    wined3d_device_draw_indexed_primitive(device->wined3d_device, start_index_location, index_count);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_Draw(ID3D11DeviceContext *iface,
        UINT vertex_count, UINT start_vertex_location)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, vertex_count %u, start_vertex_location %u.\n",
            iface, vertex_count, start_vertex_location);

    wined3d_mutex_lock();
    wined3d_device_draw_primitive(device->wined3d_device, start_vertex_location, vertex_count);
    wined3d_mutex_unlock();
}

static HRESULT STDMETHODCALLTYPE d3d11_immediate_context_Map(ID3D11DeviceContext *iface, ID3D11Resource *resource,
        UINT subresource_idx, D3D11_MAP map_type, UINT map_flags, D3D11_MAPPED_SUBRESOURCE *mapped_subresource)
{
    struct wined3d_resource *wined3d_resource;
    struct wined3d_map_desc map_desc;
    HRESULT hr;

    TRACE("iface %p, resource %p, subresource_idx %u, map_type %u, map_flags %#x, mapped_subresource %p.\n",
            iface, resource, subresource_idx, map_type, map_flags, mapped_subresource);

    if (map_flags)
        FIXME("Ignoring map_flags %#x.\n", map_flags);

    wined3d_resource = wined3d_resource_from_d3d11_resource(resource);

    wined3d_mutex_lock();
    hr = wined3d_resource_sub_resource_map(wined3d_resource, subresource_idx,
            &map_desc, NULL, wined3d_map_flags_from_d3d11_map_type(map_type));
    wined3d_mutex_unlock();

    mapped_subresource->pData = map_desc.data;
    mapped_subresource->RowPitch = map_desc.row_pitch;
    mapped_subresource->DepthPitch = map_desc.slice_pitch;

    return hr;
}

static void STDMETHODCALLTYPE d3d11_immediate_context_Unmap(ID3D11DeviceContext *iface, ID3D11Resource *resource,
        UINT subresource_idx)
{
    struct wined3d_resource *wined3d_resource;

    TRACE("iface %p, resource %p, subresource_idx %u.\n", iface, resource, subresource_idx);

    wined3d_resource = wined3d_resource_from_d3d11_resource(resource);

    wined3d_mutex_lock();
    wined3d_resource_sub_resource_unmap(wined3d_resource, subresource_idx);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_PSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer *const *buffers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D11Buffer(buffers[i]);

        wined3d_device_set_ps_cb(device->wined3d_device, start_slot + i,
                buffer ? buffer->wined3d_buffer : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_IASetInputLayout(ID3D11DeviceContext *iface,
        ID3D11InputLayout *input_layout)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_input_layout *layout = unsafe_impl_from_ID3D11InputLayout(input_layout);

    TRACE("iface %p, input_layout %p.\n", iface, input_layout);

    wined3d_mutex_lock();
    wined3d_device_set_vertex_declaration(device->wined3d_device, layout ? layout->wined3d_decl : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_IASetVertexBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer *const *buffers, const UINT *strides, const UINT *offsets)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p, strides %p, offsets %p.\n",
            iface, start_slot, buffer_count, buffers, strides, offsets);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D11Buffer(buffers[i]);

        wined3d_device_set_stream_source(device->wined3d_device, start_slot + i,
                buffer ? buffer->wined3d_buffer : NULL, offsets[i], strides[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_IASetIndexBuffer(ID3D11DeviceContext *iface,
        ID3D11Buffer *buffer, DXGI_FORMAT format, UINT offset)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_buffer *buffer_impl = unsafe_impl_from_ID3D11Buffer(buffer);

    TRACE("iface %p, buffer %p, format %s, offset %u.\n",
            iface, buffer, debug_dxgi_format(format), offset);

    if (offset)
        FIXME("offset %u not supported.\n", offset);

    wined3d_mutex_lock();
    wined3d_device_set_index_buffer(device->wined3d_device,
            buffer_impl ? buffer_impl->wined3d_buffer : NULL,
            wined3dformat_from_dxgi_format(format));
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DrawIndexedInstanced(ID3D11DeviceContext *iface,
        UINT instance_index_count, UINT instance_count, UINT start_index_location, INT base_vertex_location,
        UINT start_instance_location)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, instance_index_count %u, instance_count %u, start_index_location %u, "
            "base_vertex_location %d, start_instance_location %u.\n",
            iface, instance_index_count, instance_count, start_index_location,
            base_vertex_location, start_instance_location);

    wined3d_mutex_lock();
    wined3d_device_set_base_vertex_index(device->wined3d_device, base_vertex_location);
    wined3d_device_draw_indexed_primitive_instanced(device->wined3d_device, start_index_location,
            instance_index_count, start_instance_location, instance_count);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DrawInstanced(ID3D11DeviceContext *iface,
        UINT instance_vertex_count, UINT instance_count, UINT start_vertex_location, UINT start_instance_location)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, instance_vertex_count %u, instance_count %u, start_vertex_location %u, "
            "start_instance_location %u.\n",
            iface, instance_vertex_count, instance_count, start_vertex_location,
            start_instance_location);

    wined3d_mutex_lock();
    wined3d_device_draw_primitive_instanced(device->wined3d_device, start_vertex_location,
            instance_vertex_count, start_instance_location, instance_count);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer *const *buffers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D11Buffer(buffers[i]);

        wined3d_device_set_gs_cb(device->wined3d_device, start_slot + i,
                buffer ? buffer->wined3d_buffer : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GSSetShader(ID3D11DeviceContext *iface,
        ID3D11GeometryShader *shader, ID3D11ClassInstance *const *class_instances, UINT class_instance_count)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_geometry_shader *gs = unsafe_impl_from_ID3D11GeometryShader(shader);

    TRACE("iface %p, shader %p, class_instances %p, class_instance_count %u.\n",
            iface, shader, class_instances, class_instance_count);

    if (class_instances)
        FIXME("Dynamic linking is not implemented yet.\n");

    wined3d_mutex_lock();
    wined3d_device_set_geometry_shader(device->wined3d_device, gs ? gs->wined3d_shader : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_IASetPrimitiveTopology(ID3D11DeviceContext *iface,
        D3D11_PRIMITIVE_TOPOLOGY topology)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, topology %u.\n", iface, topology);

    wined3d_mutex_lock();
    wined3d_device_set_primitive_type(device->wined3d_device, (enum wined3d_primitive_type)topology);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_VSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView *const *views)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct d3d_shader_resource_view *view = unsafe_impl_from_ID3D11ShaderResourceView(views[i]);

        wined3d_device_set_vs_resource_view(device->wined3d_device, start_slot + i,
                view ? view->wined3d_view : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_VSSetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState *const *samplers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler = unsafe_impl_from_ID3D11SamplerState(samplers[i]);

        wined3d_device_set_vs_sampler(device->wined3d_device, start_slot + i,
                sampler ? sampler->wined3d_sampler : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_Begin(ID3D11DeviceContext *iface,
        ID3D11Asynchronous *asynchronous)
{
    FIXME("iface %p, asynchronous %p stub!\n", iface, asynchronous);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_End(ID3D11DeviceContext *iface,
        ID3D11Asynchronous *asynchronous)
{
    FIXME("iface %p, asynchronous %p stub!\n", iface, asynchronous);
}

static HRESULT STDMETHODCALLTYPE d3d11_immediate_context_GetData(ID3D11DeviceContext *iface,
        ID3D11Asynchronous *asynchronous, void *data, UINT data_size, UINT data_flags)
{
    FIXME("iface %p, asynchronous %p, data %p, data_size %u, data_flags %#x stub!\n",
            iface, asynchronous, data, data_size, data_flags);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d11_immediate_context_SetPredication(ID3D11DeviceContext *iface,
        ID3D11Predicate *predicate, BOOL value)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_query *query;

    TRACE("iface %p, predicate %p, value %#x.\n", iface, predicate, value);

    query = unsafe_impl_from_ID3D11Query((ID3D11Query *)predicate);

    wined3d_mutex_lock();
    wined3d_device_set_predication(device->wined3d_device, query ? query->wined3d_query : NULL, value);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView *const *views)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct d3d_shader_resource_view *view = unsafe_impl_from_ID3D11ShaderResourceView(views[i]);

        wined3d_device_set_gs_resource_view(device->wined3d_device, start_slot + i,
                view ? view->wined3d_view : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GSSetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState *const *samplers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler = unsafe_impl_from_ID3D11SamplerState(samplers[i]);

        wined3d_device_set_gs_sampler(device->wined3d_device, start_slot + i,
                sampler ? sampler->wined3d_sampler : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_OMSetRenderTargets(ID3D11DeviceContext *iface,
        UINT render_target_view_count, ID3D11RenderTargetView *const *render_target_views,
        ID3D11DepthStencilView *depth_stencil_view)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_depthstencil_view *dsv;
    unsigned int i;

    TRACE("iface %p, render_target_view_count %u, render_target_views %p, depth_stencil_view %p.\n",
            iface, render_target_view_count, render_target_views, depth_stencil_view);

    wined3d_mutex_lock();
    for (i = 0; i < render_target_view_count; ++i)
    {
        struct d3d_rendertarget_view *rtv = unsafe_impl_from_ID3D11RenderTargetView(render_target_views[i]);
        wined3d_device_set_rendertarget_view(device->wined3d_device, i, rtv ? rtv->wined3d_view : NULL, FALSE);
    }
    for (; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        wined3d_device_set_rendertarget_view(device->wined3d_device, i, NULL, FALSE);
    }

    dsv = unsafe_impl_from_ID3D11DepthStencilView(depth_stencil_view);
    wined3d_device_set_depth_stencil_view(device->wined3d_device, dsv ? dsv->wined3d_view : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_OMSetRenderTargetsAndUnorderedAccessViews(
        ID3D11DeviceContext *iface, UINT render_target_view_count,
        ID3D11RenderTargetView *const *render_target_views, ID3D11DepthStencilView *depth_stencil_view,
        UINT unordered_access_view_start_slot, UINT unordered_access_view_count,
        ID3D11UnorderedAccessView *const *unordered_access_views, const UINT *initial_counts)
{
    FIXME("iface %p, render_target_view_count %u, render_target_views %p, depth_stencil_view %p, "
            "unordered_access_view_start_slot %u, unordered_access_view_count %u, unordered_access_views %p, "
            "initial_counts %p stub!\n",
            iface, render_target_view_count, render_target_views, depth_stencil_view,
            unordered_access_view_start_slot, unordered_access_view_count, unordered_access_views,
            initial_counts);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_OMSetBlendState(ID3D11DeviceContext *iface,
        ID3D11BlendState *blend_state, const FLOAT blend_factor[4], UINT sample_mask)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    static const float default_blend_factor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    const D3D11_BLEND_DESC *desc;

    TRACE("iface %p, blend_state %p, blend_factor %p, sample_mask 0x%08x.\n",
            iface, blend_state, blend_factor, sample_mask);

    if (!blend_factor)
        blend_factor = default_blend_factor;

    if (blend_factor[0] != 1.0f || blend_factor[1] != 1.0f || blend_factor[2] != 1.0f || blend_factor[3] != 1.0f)
        FIXME("Ignoring blend factor {%.8e %.8e %.8e %.8e}.\n",
                blend_factor[0], blend_factor[1], blend_factor[2], blend_factor[3]);

    wined3d_mutex_lock();
    memcpy(device->blend_factor, blend_factor, 4 * sizeof(*blend_factor));
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_MULTISAMPLEMASK, sample_mask);
    if (!(device->blend_state = unsafe_impl_from_ID3D11BlendState(blend_state)))
    {
        wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_ALPHABLENDENABLE, FALSE);
        wined3d_device_set_render_state(device->wined3d_device,
                WINED3D_RS_COLORWRITEENABLE, D3D11_COLOR_WRITE_ENABLE_ALL);
        wined3d_device_set_render_state(device->wined3d_device,
                WINED3D_RS_COLORWRITEENABLE1, D3D11_COLOR_WRITE_ENABLE_ALL);
        wined3d_device_set_render_state(device->wined3d_device,
                WINED3D_RS_COLORWRITEENABLE2, D3D11_COLOR_WRITE_ENABLE_ALL);
        wined3d_device_set_render_state(device->wined3d_device,
                WINED3D_RS_COLORWRITEENABLE3, D3D11_COLOR_WRITE_ENABLE_ALL);
        wined3d_mutex_unlock();
        return;
    }

    desc = &device->blend_state->desc;
    /* glSampleCoverage() */
    if (desc->AlphaToCoverageEnable)
        FIXME("Ignoring AlphaToCoverageEnable %#x.\n", desc->AlphaToCoverageEnable);
    /* glEnableIndexedEXT(GL_BLEND, ...) */
    FIXME("Per-rendertarget blend not implemented.\n");
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_ALPHABLENDENABLE,
            desc->RenderTarget[0].BlendEnable);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_SRCBLEND, desc->RenderTarget[0].SrcBlend);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_DESTBLEND, desc->RenderTarget[0].DestBlend);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_BLENDOP, desc->RenderTarget[0].BlendOp);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_SEPARATEALPHABLENDENABLE, TRUE);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_SRCBLENDALPHA,
            desc->RenderTarget[0].SrcBlendAlpha);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_DESTBLENDALPHA,
            desc->RenderTarget[0].DestBlendAlpha);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_BLENDOPALPHA,
            desc->RenderTarget[0].BlendOpAlpha);
    FIXME("Color mask > 3 not implemented.\n");
    wined3d_device_set_render_state(device->wined3d_device,
            WINED3D_RS_COLORWRITEENABLE, desc->RenderTarget[0].RenderTargetWriteMask);
    wined3d_device_set_render_state(device->wined3d_device,
            WINED3D_RS_COLORWRITEENABLE1, desc->RenderTarget[1].RenderTargetWriteMask);
    wined3d_device_set_render_state(device->wined3d_device,
            WINED3D_RS_COLORWRITEENABLE2, desc->RenderTarget[2].RenderTargetWriteMask);
    wined3d_device_set_render_state(device->wined3d_device,
            WINED3D_RS_COLORWRITEENABLE3, desc->RenderTarget[3].RenderTargetWriteMask);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_OMSetDepthStencilState(ID3D11DeviceContext *iface,
        ID3D11DepthStencilState *depth_stencil_state, UINT stencil_ref)
{
    FIXME("iface %p, depth_stencil_state %p, stencil_ref %u stub!\n", iface, depth_stencil_state, stencil_ref);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_SOSetTargets(ID3D11DeviceContext *iface, UINT buffer_count,
        ID3D11Buffer *const *buffers, const UINT *offsets)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int count, i;

    TRACE("iface %p, buffer_count %u, buffers %p, offsets %p.\n", iface, buffer_count, buffers, offsets);

    count = min(buffer_count, D3D11_SO_BUFFER_SLOT_COUNT);
    wined3d_mutex_lock();
    for (i = 0; i < count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D11Buffer(buffers[i]);

        wined3d_device_set_stream_output(device->wined3d_device, i,
                buffer ? buffer->wined3d_buffer : NULL, offsets[i]);
    }
    for (; i < D3D11_SO_BUFFER_SLOT_COUNT; ++i)
    {
        wined3d_device_set_stream_output(device->wined3d_device, i, NULL, 0);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DrawAuto(ID3D11DeviceContext *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DrawIndexedInstancedIndirect(ID3D11DeviceContext *iface,
        ID3D11Buffer *buffer, UINT offset)
{
    FIXME("iface %p, buffer %p, offset %u stub!\n", iface, buffer, offset);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DrawInstancedIndirect(ID3D11DeviceContext *iface,
        ID3D11Buffer *buffer, UINT offset)
{
    FIXME("iface %p, buffer %p, offset %u stub!\n", iface, buffer, offset);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_Dispatch(ID3D11DeviceContext *iface,
        UINT thread_group_count_x, UINT thread_group_count_y, UINT thread_group_count_z)
{
    FIXME("iface %p, thread_group_count_x %u, thread_group_count_y %u, thread_group_count_z %u stub!\n",
            iface, thread_group_count_x, thread_group_count_y, thread_group_count_z);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DispatchIndirect(ID3D11DeviceContext *iface,
        ID3D11Buffer *buffer, UINT offset)
{
    FIXME("iface %p, buffer %p, offset %u stub!\n", iface, buffer, offset);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_RSSetState(ID3D11DeviceContext *iface,
        ID3D11RasterizerState *rasterizer_state)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    const D3D11_RASTERIZER_DESC *desc;

    TRACE("iface %p, rasterizer_state %p.\n", iface, rasterizer_state);

    wined3d_mutex_lock();
    if (!(device->rasterizer_state = unsafe_impl_from_ID3D11RasterizerState(rasterizer_state)))
    {
        wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_FILLMODE, WINED3D_FILL_SOLID);
        wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_CULLMODE, WINED3D_CULL_CCW);
        wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_SCISSORTESTENABLE, FALSE);
        wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_MULTISAMPLEANTIALIAS, FALSE);
        wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_ANTIALIASEDLINEENABLE, FALSE);
        wined3d_mutex_unlock();
        return;
    }

    desc = &device->rasterizer_state->desc;
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_FILLMODE, desc->FillMode);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_CULLMODE, desc->CullMode);
    /* glFrontFace() */
    if (desc->FrontCounterClockwise)
        FIXME("Ignoring FrontCounterClockwise %#x.\n", desc->FrontCounterClockwise);
    /* OpenGL style depth bias. */
    if (desc->DepthBias || desc->SlopeScaledDepthBias)
        FIXME("Ignoring depth bias.\n");
    /* GL_DEPTH_CLAMP */
    if (!desc->DepthClipEnable)
        FIXME("Ignoring DepthClipEnable %#x.\n", desc->DepthClipEnable);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_SCISSORTESTENABLE, desc->ScissorEnable);
    wined3d_device_set_render_state(device->wined3d_device, WINED3D_RS_MULTISAMPLEANTIALIAS, desc->MultisampleEnable);
    wined3d_device_set_render_state(device->wined3d_device,
            WINED3D_RS_ANTIALIASEDLINEENABLE, desc->AntialiasedLineEnable);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_RSSetViewports(ID3D11DeviceContext *iface,
        UINT viewport_count, const D3D11_VIEWPORT *viewports)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_viewport wined3d_vp;

    TRACE("iface %p, viewport_count %u, viewports %p.\n", iface, viewport_count, viewports);

    if (viewport_count > 1)
        FIXME("Multiple viewports not implemented.\n");

    if (!viewport_count)
        return;

    if (viewports[0].TopLeftX != (UINT)viewports[0].TopLeftX
            || viewports[0].TopLeftY != (UINT)viewports[0].TopLeftY
            || viewports[0].Width != (UINT)viewports[0].Width
            || viewports[0].Height != (UINT)viewports[0].Height)
        FIXME("Floating-point viewports not implemented.\n");

    wined3d_vp.x = viewports[0].TopLeftX;
    wined3d_vp.y = viewports[0].TopLeftY;
    wined3d_vp.width = viewports[0].Width;
    wined3d_vp.height = viewports[0].Height;
    wined3d_vp.min_z = viewports[0].MinDepth;
    wined3d_vp.max_z = viewports[0].MaxDepth;

    wined3d_mutex_lock();
    wined3d_device_set_viewport(device->wined3d_device, &wined3d_vp);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_RSSetScissorRects(ID3D11DeviceContext *iface,
        UINT rect_count, const D3D11_RECT *rects)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, rect_count %u, rects %p.\n", iface, rect_count, rects);

    if (rect_count > 1)
        FIXME("Multiple scissor rects not implemented.\n");

    if (!rect_count)
        return;

    wined3d_mutex_lock();
    wined3d_device_set_scissor_rect(device->wined3d_device, rects);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CopySubresourceRegion(ID3D11DeviceContext *iface,
        ID3D11Resource *dst_resource, UINT dst_subresource_idx, UINT dst_x, UINT dst_y, UINT dst_z,
        ID3D11Resource *src_resource, UINT src_subresource_idx, const D3D11_BOX *src_box)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_resource *wined3d_dst_resource, *wined3d_src_resource;
    struct wined3d_box wined3d_src_box;

    TRACE("iface %p, dst_resource %p, dst_subresource_idx %u, dst_x %u, dst_y %u, dst_z %u, "
            "src_resource %p, src_subresource_idx %u, src_box %p.\n",
            iface, dst_resource, dst_subresource_idx, dst_x, dst_y, dst_z,
            src_resource, src_subresource_idx, src_box);

    if (src_box)
    {
        wined3d_src_box.left = src_box->left;
        wined3d_src_box.top = src_box->top;
        wined3d_src_box.front = src_box->front;
        wined3d_src_box.right = src_box->right;
        wined3d_src_box.bottom = src_box->bottom;
        wined3d_src_box.back = src_box->back;
    }

    wined3d_dst_resource = wined3d_resource_from_d3d11_resource(dst_resource);
    wined3d_src_resource = wined3d_resource_from_d3d11_resource(src_resource);
    wined3d_mutex_lock();
    wined3d_device_copy_sub_resource_region(device->wined3d_device, wined3d_dst_resource, dst_subresource_idx,
            dst_x, dst_y, dst_z, wined3d_src_resource, src_subresource_idx, src_box ? &wined3d_src_box : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CopyResource(ID3D11DeviceContext *iface,
        ID3D11Resource *dst_resource, ID3D11Resource *src_resource)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_resource *wined3d_dst_resource, *wined3d_src_resource;

    TRACE("iface %p, dst_resource %p, src_resource %p.\n", iface, dst_resource, src_resource);

    wined3d_dst_resource = wined3d_resource_from_d3d11_resource(dst_resource);
    wined3d_src_resource = wined3d_resource_from_d3d11_resource(src_resource);
    wined3d_mutex_lock();
    wined3d_device_copy_resource(device->wined3d_device, wined3d_dst_resource, wined3d_src_resource);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_UpdateSubresource(ID3D11DeviceContext *iface,
        ID3D11Resource *resource, UINT subresource_idx, const D3D11_BOX *box,
        const void *data, UINT row_pitch, UINT depth_pitch)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_resource *wined3d_resource;
    struct wined3d_box wined3d_box;

    TRACE("iface %p, resource %p, subresource_idx %u, box %p, data %p, row_pitch %u, depth_pitch %u.\n",
            iface, resource, subresource_idx, box, data, row_pitch, depth_pitch);

    if (box)
    {
        wined3d_box.left = box->left;
        wined3d_box.top = box->top;
        wined3d_box.front = box->front;
        wined3d_box.right = box->right;
        wined3d_box.bottom = box->bottom;
        wined3d_box.back = box->back;
    }

    wined3d_resource = wined3d_resource_from_d3d11_resource(resource);
    wined3d_mutex_lock();
    wined3d_device_update_sub_resource(device->wined3d_device, wined3d_resource,
            subresource_idx, box ? &wined3d_box : NULL, data, row_pitch, depth_pitch);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CopyStructureCount(ID3D11DeviceContext *iface,
        ID3D11Buffer *dst_buffer, UINT dst_offset, ID3D11UnorderedAccessView *src_view)
{
    FIXME("iface %p, dst_buffer %p, dst_offset %u, src_view %p stub!\n",
            iface, dst_buffer, dst_offset, src_view);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_ClearRenderTargetView(ID3D11DeviceContext *iface,
        ID3D11RenderTargetView *render_target_view, const FLOAT color_rgba[4])
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_rendertarget_view *view = unsafe_impl_from_ID3D11RenderTargetView(render_target_view);
    const struct wined3d_color color = {color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]};
    HRESULT hr;

    TRACE("iface %p, render_target_view %p, color_rgba {%.8e %.8e %.8e %.8e}.\n",
            iface, render_target_view, color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);

    wined3d_mutex_lock();
    if (FAILED(hr = wined3d_device_clear_rendertarget_view(device->wined3d_device, view->wined3d_view, NULL, &color)))
        ERR("Failed to clear view, hr %#x.\n", hr);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_ClearUnorderedAccessViewUint(ID3D11DeviceContext *iface,
        ID3D11UnorderedAccessView *unordered_access_view, const UINT values[4])
{
    FIXME("iface %p, unordered_access_view %p, values {%u %u %u %u} stub!\n",
            iface, unordered_access_view, values[0], values[1], values[2], values[3]);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_ClearUnorderedAccessViewFloat(ID3D11DeviceContext *iface,
        ID3D11UnorderedAccessView *unordered_access_view, const FLOAT values[4])
{
    FIXME("iface %p, unordered_access_view %p, values {%.8e %.8e %.8e %.8e} stub!\n",
            iface, unordered_access_view, values[0], values[1], values[2], values[3]);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_ClearDepthStencilView(ID3D11DeviceContext *iface,
        ID3D11DepthStencilView *depth_stencil_view, UINT flags, FLOAT depth, UINT8 stencil)
{
    FIXME("iface %p, depth_stencil_view %p, flags %#x, depth %f, stencil %u stub!\n",
            iface, depth_stencil_view, flags, depth, stencil);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GenerateMips(ID3D11DeviceContext *iface,
        ID3D11ShaderResourceView *view)
{
    FIXME("iface %p, view %p stub!\n", iface, view);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_SetResourceMinLOD(ID3D11DeviceContext *iface,
        ID3D11Resource *resource, FLOAT min_lod)
{
    FIXME("iface %p, resource %p, min_lod %f stub!\n", iface, resource, min_lod);
}

static FLOAT STDMETHODCALLTYPE d3d11_immediate_context_GetResourceMinLOD(ID3D11DeviceContext *iface,
        ID3D11Resource *resource)
{
    FIXME("iface %p, resource %p stub!\n", iface, resource);

    return 0.0f;
}

static void STDMETHODCALLTYPE d3d11_immediate_context_ResolveSubresource(ID3D11DeviceContext *iface,
        ID3D11Resource *dst_resource, UINT dst_subresource_idx,
        ID3D11Resource *src_resource, UINT src_subresource_idx,
        DXGI_FORMAT format)
{
    FIXME("iface %p, dst_resource %p, dst_subresource_idx %u, src_resource %p, src_subresource_idx %u, "
            "format %s stub!\n",
            iface, dst_resource, dst_subresource_idx, src_resource, src_subresource_idx,
            debug_dxgi_format(format));
}

static void STDMETHODCALLTYPE d3d11_immediate_context_ExecuteCommandList(ID3D11DeviceContext *iface,
        ID3D11CommandList *command_list, BOOL restore_state)
{
    FIXME("iface %p, command_list %p, restore_state %#x stub!\n", iface, command_list, restore_state);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_HSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView *const *views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_HSSetShader(ID3D11DeviceContext *iface,
        ID3D11HullShader *shader, ID3D11ClassInstance *const *class_instances, UINT class_instance_count)
{
    FIXME("iface %p, shader %p, class_instances %p, class_instance_count %u stub!\n",
            iface, shader, class_instances, class_instance_count);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_HSSetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState *const *samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_HSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer *const *buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView *const *views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DSSetShader(ID3D11DeviceContext *iface,
        ID3D11DomainShader *shader, ID3D11ClassInstance *const *class_instances, UINT class_instance_count)
{
    FIXME("iface %p, shader %p, class_instances %p, class_instance_count %u stub!\n",
            iface, shader, class_instances, class_instance_count);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DSSetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState *const *samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer *const *buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView *const *views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSSetUnorderedAccessViews(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11UnorderedAccessView *const *views, const UINT *initial_counts)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p, initial_counts %p stub!\n",
            iface, start_slot, view_count, views, initial_counts);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSSetShader(ID3D11DeviceContext *iface,
        ID3D11ComputeShader *shader, ID3D11ClassInstance *const *class_instances, UINT class_instance_count)
{
    FIXME("iface %p, shader %p, class_instances %p, class_instance_count %u stub!\n",
            iface, shader, class_instances, class_instance_count);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSSetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState *const *samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer *const *buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_VSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer **buffers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (!(wined3d_buffer = wined3d_device_get_vs_cb(device->wined3d_device, start_slot + i)))
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D11Buffer_iface;
        ID3D11Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_PSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView **views)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n",
            iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct wined3d_shader_resource_view *wined3d_view;
        struct d3d_shader_resource_view *view_impl;

        if (!(wined3d_view = wined3d_device_get_ps_resource_view(device->wined3d_device, start_slot + i)))
        {
            views[i] = NULL;
            continue;
        }

        view_impl = wined3d_shader_resource_view_get_parent(wined3d_view);
        views[i] = &view_impl->ID3D11ShaderResourceView_iface;
        ID3D11ShaderResourceView_AddRef(views[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_PSGetShader(ID3D11DeviceContext *iface,
        ID3D11PixelShader **shader, ID3D11ClassInstance **class_instances, UINT *class_instance_count)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_shader *wined3d_shader;
    struct d3d_pixel_shader *shader_impl;

    TRACE("iface %p, shader %p, class_instances %p, class_instance_count %p.\n",
            iface, shader, class_instances, class_instance_count);

    if (class_instances || class_instance_count)
        FIXME("Dynamic linking not implemented yet.\n");

    wined3d_mutex_lock();
    if (!(wined3d_shader = wined3d_device_get_pixel_shader(device->wined3d_device)))
    {
        wined3d_mutex_unlock();
        *shader = NULL;
        return;
    }

    shader_impl = wined3d_shader_get_parent(wined3d_shader);
    wined3d_mutex_unlock();
    *shader = &shader_impl->ID3D11PixelShader_iface;
    ID3D11PixelShader_AddRef(*shader);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_PSGetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState **samplers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct wined3d_sampler *wined3d_sampler;
        struct d3d_sampler_state *sampler_impl;

        if (!(wined3d_sampler = wined3d_device_get_ps_sampler(device->wined3d_device, start_slot + i)))
        {
            samplers[i] = NULL;
            continue;
        }

        sampler_impl = wined3d_sampler_get_parent(wined3d_sampler);
        samplers[i] = &sampler_impl->ID3D11SamplerState_iface;
        ID3D11SamplerState_AddRef(samplers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_VSGetShader(ID3D11DeviceContext *iface,
        ID3D11VertexShader **shader, ID3D11ClassInstance **class_instances, UINT *class_instance_count)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_vertex_shader *shader_impl;
    struct wined3d_shader *wined3d_shader;

    TRACE("iface %p, shader %p, class_instances %p, class_instance_count %p.\n",
            iface, shader, class_instances, class_instance_count);

    if (class_instances || class_instance_count)
        FIXME("Dynamic linking not implemented yet.\n");

    wined3d_mutex_lock();
    if (!(wined3d_shader = wined3d_device_get_vertex_shader(device->wined3d_device)))
    {
        wined3d_mutex_unlock();
        *shader = NULL;
        return;
    }

    shader_impl = wined3d_shader_get_parent(wined3d_shader);
    wined3d_mutex_unlock();
    *shader = &shader_impl->ID3D11VertexShader_iface;
    ID3D11VertexShader_AddRef(*shader);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_PSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer **buffers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (!(wined3d_buffer = wined3d_device_get_ps_cb(device->wined3d_device, start_slot + i)))
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D11Buffer_iface;
        ID3D11Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_IAGetInputLayout(ID3D11DeviceContext *iface,
        ID3D11InputLayout **input_layout)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_vertex_declaration *wined3d_declaration;
    struct d3d_input_layout *input_layout_impl;

    TRACE("iface %p, input_layout %p.\n", iface, input_layout);

    wined3d_mutex_lock();
    if (!(wined3d_declaration = wined3d_device_get_vertex_declaration(device->wined3d_device)))
    {
        wined3d_mutex_unlock();
        *input_layout = NULL;
        return;
    }

    input_layout_impl = wined3d_vertex_declaration_get_parent(wined3d_declaration);
    wined3d_mutex_unlock();
    *input_layout = &input_layout_impl->ID3D11InputLayout_iface;
    ID3D11InputLayout_AddRef(*input_layout);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_IAGetVertexBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer **buffers, UINT *strides, UINT *offsets)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p, strides %p, offsets %p stub!\n",
            iface, start_slot, buffer_count, buffers, strides, offsets);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_IAGetIndexBuffer(ID3D11DeviceContext *iface,
        ID3D11Buffer **buffer, DXGI_FORMAT *format, UINT *offset)
{
    FIXME("iface %p, buffer %p, format %p, offset %p stub!\n", iface, buffer, format, offset);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer **buffers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (!(wined3d_buffer = wined3d_device_get_gs_cb(device->wined3d_device, start_slot + i)))
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D11Buffer_iface;
        ID3D11Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GSGetShader(ID3D11DeviceContext *iface,
        ID3D11GeometryShader **shader, ID3D11ClassInstance **class_instances, UINT *class_instance_count)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct d3d_geometry_shader *shader_impl;
    struct wined3d_shader *wined3d_shader;

    TRACE("iface %p, shader %p, class_instances %p, class_instance_count %p.\n",
            iface, shader, class_instances, class_instance_count);

    if (class_instances || class_instance_count)
        FIXME("Dynamic linking not implemented yet.\n");

    wined3d_mutex_lock();
    if (!(wined3d_shader = wined3d_device_get_geometry_shader(device->wined3d_device)))
    {
        wined3d_mutex_unlock();
        *shader = NULL;
        return;
    }

    shader_impl = wined3d_shader_get_parent(wined3d_shader);
    wined3d_mutex_unlock();
    *shader = &shader_impl->ID3D11GeometryShader_iface;
    ID3D11GeometryShader_AddRef(*shader);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_IAGetPrimitiveTopology(ID3D11DeviceContext *iface,
        D3D11_PRIMITIVE_TOPOLOGY *topology)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, topology %p.\n", iface, topology);

    wined3d_mutex_lock();
    wined3d_device_get_primitive_type(device->wined3d_device, (enum wined3d_primitive_type *)topology);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_VSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView **views)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct wined3d_shader_resource_view *wined3d_view;
        struct d3d_shader_resource_view *view_impl;

        if (!(wined3d_view = wined3d_device_get_vs_resource_view(device->wined3d_device, start_slot + i)))
        {
            views[i] = NULL;
            continue;
        }

        view_impl = wined3d_shader_resource_view_get_parent(wined3d_view);
        views[i] = &view_impl->ID3D11ShaderResourceView_iface;
        ID3D11ShaderResourceView_AddRef(views[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_VSGetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState **samplers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct wined3d_sampler *wined3d_sampler;
        struct d3d_sampler_state *sampler_impl;

        if (!(wined3d_sampler = wined3d_device_get_vs_sampler(device->wined3d_device, start_slot + i)))
        {
            samplers[i] = NULL;
            continue;
        }

        sampler_impl = wined3d_sampler_get_parent(wined3d_sampler);
        samplers[i] = &sampler_impl->ID3D11SamplerState_iface;
        ID3D11SamplerState_AddRef(samplers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GetPredication(ID3D11DeviceContext *iface,
        ID3D11Predicate **predicate, BOOL *value)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_query *wined3d_predicate;
    struct d3d_query *predicate_impl;

    TRACE("iface %p, predicate %p, value %p.\n", iface, predicate, value);

    wined3d_mutex_lock();
    if (!(wined3d_predicate = wined3d_device_get_predication(device->wined3d_device, value)))
    {
        wined3d_mutex_unlock();
        *predicate = NULL;
        return;
    }

    predicate_impl = wined3d_query_get_parent(wined3d_predicate);
    wined3d_mutex_unlock();
    *predicate = (ID3D11Predicate *)&predicate_impl->ID3D11Query_iface;
    ID3D11Predicate_AddRef(*predicate);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView **views)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct wined3d_shader_resource_view *wined3d_view;
        struct d3d_shader_resource_view *view_impl;

        if (!(wined3d_view = wined3d_device_get_gs_resource_view(device->wined3d_device, start_slot + i)))
        {
            views[i] = NULL;
            continue;
        }

        view_impl = wined3d_shader_resource_view_get_parent(wined3d_view);
        views[i] = &view_impl->ID3D11ShaderResourceView_iface;
        ID3D11ShaderResourceView_AddRef(views[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_GSGetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState **samplers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler_impl;
        struct wined3d_sampler *wined3d_sampler;

        if (!(wined3d_sampler = wined3d_device_get_gs_sampler(device->wined3d_device, start_slot + i)))
        {
            samplers[i] = NULL;
            continue;
        }

        sampler_impl = wined3d_sampler_get_parent(wined3d_sampler);
        samplers[i] = &sampler_impl->ID3D11SamplerState_iface;
        ID3D11SamplerState_AddRef(samplers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_OMGetRenderTargets(ID3D11DeviceContext *iface,
        UINT render_target_view_count, ID3D11RenderTargetView **render_target_views,
        ID3D11DepthStencilView **depth_stencil_view)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_rendertarget_view *wined3d_view;

    TRACE("iface %p, render_target_view_count %u, render_target_views %p, depth_stencil_view %p.\n",
            iface, render_target_view_count, render_target_views, depth_stencil_view);

    wined3d_mutex_lock();
    if (render_target_views)
    {
        struct d3d_rendertarget_view *view_impl;
        unsigned int i;

        for (i = 0; i < render_target_view_count; ++i)
        {
            if (!(wined3d_view = wined3d_device_get_rendertarget_view(device->wined3d_device, i))
                    || !(view_impl = wined3d_rendertarget_view_get_parent(wined3d_view)))
            {
                render_target_views[i] = NULL;
                continue;
            }

            render_target_views[i] = &view_impl->ID3D11RenderTargetView_iface;
            ID3D11RenderTargetView_AddRef(render_target_views[i]);
        }
    }

    if (depth_stencil_view)
    {
        struct d3d_depthstencil_view *view_impl;

        if (!(wined3d_view = wined3d_device_get_depth_stencil_view(device->wined3d_device))
                || !(view_impl = wined3d_rendertarget_view_get_parent(wined3d_view)))
        {
            *depth_stencil_view = NULL;
        }
        else
        {
            *depth_stencil_view = &view_impl->ID3D11DepthStencilView_iface;
            ID3D11DepthStencilView_AddRef(*depth_stencil_view);
        }
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_OMGetRenderTargetsAndUnorderedAccessViews(
        ID3D11DeviceContext *iface,
        UINT render_target_view_count, ID3D11RenderTargetView **render_target_views,
        ID3D11DepthStencilView **depth_stencil_view,
        UINT unordered_access_view_start_slot, UINT unordered_access_view_count,
        ID3D11UnorderedAccessView **unordered_access_views)
{
    FIXME("iface %p, render_target_view_count %u, render_target_views %p, depth_stencil_view %p, "
            "unordered_access_view_start_slot %u, unordered_access_view_count %u, "
            "unordered_access_views %p stub!\n",
            iface, render_target_view_count, render_target_views, depth_stencil_view,
            unordered_access_view_start_slot, unordered_access_view_count, unordered_access_views);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_OMGetBlendState(ID3D11DeviceContext *iface,
        ID3D11BlendState **blend_state, FLOAT blend_factor[4], UINT *sample_mask)
{
    FIXME("iface %p, blend_state %p, blend_factor %p, sample_mask %p stub!\n",
            iface, blend_state, blend_factor, sample_mask);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_OMGetDepthStencilState(ID3D11DeviceContext *iface,
        ID3D11DepthStencilState **depth_stencil_state, UINT *stencil_ref)
{
    FIXME("iface %p, depth_stencil_state %p, stencil_ref %p stub!\n", iface, depth_stencil_state, stencil_ref);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_SOGetTargets(ID3D11DeviceContext *iface,
        UINT buffer_count, ID3D11Buffer **buffers)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    unsigned int i;

    TRACE("iface %p, buffer_count %u, buffers %p.\n", iface, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (!(wined3d_buffer = wined3d_device_get_stream_output(device->wined3d_device, i, NULL)))
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D11Buffer_iface;
        ID3D11Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d11_immediate_context_RSGetState(ID3D11DeviceContext *iface,
        ID3D11RasterizerState **rasterizer_state)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);

    TRACE("iface %p, rasterizer_state %p.\n", iface, rasterizer_state);

    if ((*rasterizer_state = device->rasterizer_state ? &device->rasterizer_state->ID3D11RasterizerState_iface : NULL))
        ID3D11RasterizerState_AddRef(*rasterizer_state);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_RSGetViewports(ID3D11DeviceContext *iface,
        UINT *viewport_count, D3D11_VIEWPORT *viewports)
{
    struct d3d_device *device = device_from_immediate_ID3D11DeviceContext(iface);
    struct wined3d_viewport wined3d_vp;

    TRACE("iface %p, viewport_count %p, viewports %p.\n", iface, viewport_count, viewports);

    if (!viewports)
    {
        *viewport_count = 1;
        return;
    }

    if (!*viewport_count)
        return;

    wined3d_mutex_lock();
    wined3d_device_get_viewport(device->wined3d_device, &wined3d_vp);
    wined3d_mutex_unlock();

    viewports[0].TopLeftX = wined3d_vp.x;
    viewports[0].TopLeftY = wined3d_vp.y;
    viewports[0].Width = wined3d_vp.width;
    viewports[0].Height = wined3d_vp.height;
    viewports[0].MinDepth = wined3d_vp.min_z;
    viewports[0].MaxDepth = wined3d_vp.max_z;

    if (*viewport_count > 1)
        memset(&viewports[1], 0, (*viewport_count - 1) * sizeof(*viewports));
}

static void STDMETHODCALLTYPE d3d11_immediate_context_RSGetScissorRects(ID3D11DeviceContext *iface,
        UINT *rect_count, D3D11_RECT *rects)
{
    FIXME("iface %p, rect_count %p, rects %p stub!\n", iface, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_HSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView **views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n", iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_HSGetShader(ID3D11DeviceContext *iface,
        ID3D11HullShader **shader, ID3D11ClassInstance **class_instances, UINT *class_instance_count)
{
    FIXME("iface %p, shader %p, class_instances %p, class_instance_count %p stub!\n",
            iface, shader, class_instances, class_instance_count);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_HSGetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState **samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_HSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer **buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView **views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n",
            iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DSGetShader(ID3D11DeviceContext *iface,
        ID3D11DomainShader **shader, ID3D11ClassInstance **class_instances, UINT *class_instance_count)
{
    FIXME("iface %p, shader %p, class_instances %p, class_instance_count %p stub!\n",
            iface, shader, class_instances, class_instance_count);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DSGetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState **samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_DSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer **buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffers %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11ShaderResourceView **views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n", iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSGetUnorderedAccessViews(ID3D11DeviceContext *iface,
        UINT start_slot, UINT view_count, ID3D11UnorderedAccessView **views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n", iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSGetShader(ID3D11DeviceContext *iface,
        ID3D11ComputeShader **shader, ID3D11ClassInstance **class_instances, UINT *class_instance_count)
{
    FIXME("iface %p, shader %p, class_instances %p, class_instance_count %p stub!\n",
            iface, shader, class_instances, class_instance_count);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSGetSamplers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT sampler_count, ID3D11SamplerState **samplers)
{
    FIXME("iface %p, start_slot %u, sampler_count %u, samplers %p stub!\n",
            iface, start_slot, sampler_count, samplers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_CSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start_slot, UINT buffer_count, ID3D11Buffer **buffers)
{
    FIXME("iface %p, start_slot %u, buffer_count %u, buffer %p stub!\n",
            iface, start_slot, buffer_count, buffers);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_ClearState(ID3D11DeviceContext *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static void STDMETHODCALLTYPE d3d11_immediate_context_Flush(ID3D11DeviceContext *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE d3d11_immediate_context_GetType(ID3D11DeviceContext *iface)
{
    TRACE("iface %p.\n", iface);

    return D3D11_DEVICE_CONTEXT_IMMEDIATE;
}

static UINT STDMETHODCALLTYPE d3d11_immediate_context_GetContextFlags(ID3D11DeviceContext *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d11_immediate_context_FinishCommandList(ID3D11DeviceContext *iface,
        BOOL restore, ID3D11CommandList **command_list)
{
    FIXME("iface %p, restore %#x, command_list %p stub!\n", iface, restore, command_list);

    return E_NOTIMPL;
}

static const struct ID3D11DeviceContextVtbl d3d11_immediate_context_vtbl =
{
    /* IUnknown methods */
    d3d11_immediate_context_QueryInterface,
    d3d11_immediate_context_AddRef,
    d3d11_immediate_context_Release,
    /* ID3D11DeviceChild methods */
    d3d11_immediate_context_GetDevice,
    d3d11_immediate_context_GetPrivateData,
    d3d11_immediate_context_SetPrivateData,
    d3d11_immediate_context_SetPrivateDataInterface,
    /* ID3D11DeviceContext methods */
    d3d11_immediate_context_VSSetConstantBuffers,
    d3d11_immediate_context_PSSetShaderResources,
    d3d11_immediate_context_PSSetShader,
    d3d11_immediate_context_PSSetSamplers,
    d3d11_immediate_context_VSSetShader,
    d3d11_immediate_context_DrawIndexed,
    d3d11_immediate_context_Draw,
    d3d11_immediate_context_Map,
    d3d11_immediate_context_Unmap,
    d3d11_immediate_context_PSSetConstantBuffers,
    d3d11_immediate_context_IASetInputLayout,
    d3d11_immediate_context_IASetVertexBuffers,
    d3d11_immediate_context_IASetIndexBuffer,
    d3d11_immediate_context_DrawIndexedInstanced,
    d3d11_immediate_context_DrawInstanced,
    d3d11_immediate_context_GSSetConstantBuffers,
    d3d11_immediate_context_GSSetShader,
    d3d11_immediate_context_IASetPrimitiveTopology,
    d3d11_immediate_context_VSSetShaderResources,
    d3d11_immediate_context_VSSetSamplers,
    d3d11_immediate_context_Begin,
    d3d11_immediate_context_End,
    d3d11_immediate_context_GetData,
    d3d11_immediate_context_SetPredication,
    d3d11_immediate_context_GSSetShaderResources,
    d3d11_immediate_context_GSSetSamplers,
    d3d11_immediate_context_OMSetRenderTargets,
    d3d11_immediate_context_OMSetRenderTargetsAndUnorderedAccessViews,
    d3d11_immediate_context_OMSetBlendState,
    d3d11_immediate_context_OMSetDepthStencilState,
    d3d11_immediate_context_SOSetTargets,
    d3d11_immediate_context_DrawAuto,
    d3d11_immediate_context_DrawIndexedInstancedIndirect,
    d3d11_immediate_context_DrawInstancedIndirect,
    d3d11_immediate_context_Dispatch,
    d3d11_immediate_context_DispatchIndirect,
    d3d11_immediate_context_RSSetState,
    d3d11_immediate_context_RSSetViewports,
    d3d11_immediate_context_RSSetScissorRects,
    d3d11_immediate_context_CopySubresourceRegion,
    d3d11_immediate_context_CopyResource,
    d3d11_immediate_context_UpdateSubresource,
    d3d11_immediate_context_CopyStructureCount,
    d3d11_immediate_context_ClearRenderTargetView,
    d3d11_immediate_context_ClearUnorderedAccessViewUint,
    d3d11_immediate_context_ClearUnorderedAccessViewFloat,
    d3d11_immediate_context_ClearDepthStencilView,
    d3d11_immediate_context_GenerateMips,
    d3d11_immediate_context_SetResourceMinLOD,
    d3d11_immediate_context_GetResourceMinLOD,
    d3d11_immediate_context_ResolveSubresource,
    d3d11_immediate_context_ExecuteCommandList,
    d3d11_immediate_context_HSSetShaderResources,
    d3d11_immediate_context_HSSetShader,
    d3d11_immediate_context_HSSetSamplers,
    d3d11_immediate_context_HSSetConstantBuffers,
    d3d11_immediate_context_DSSetShaderResources,
    d3d11_immediate_context_DSSetShader,
    d3d11_immediate_context_DSSetSamplers,
    d3d11_immediate_context_DSSetConstantBuffers,
    d3d11_immediate_context_CSSetShaderResources,
    d3d11_immediate_context_CSSetUnorderedAccessViews,
    d3d11_immediate_context_CSSetShader,
    d3d11_immediate_context_CSSetSamplers,
    d3d11_immediate_context_CSSetConstantBuffers,
    d3d11_immediate_context_VSGetConstantBuffers,
    d3d11_immediate_context_PSGetShaderResources,
    d3d11_immediate_context_PSGetShader,
    d3d11_immediate_context_PSGetSamplers,
    d3d11_immediate_context_VSGetShader,
    d3d11_immediate_context_PSGetConstantBuffers,
    d3d11_immediate_context_IAGetInputLayout,
    d3d11_immediate_context_IAGetVertexBuffers,
    d3d11_immediate_context_IAGetIndexBuffer,
    d3d11_immediate_context_GSGetConstantBuffers,
    d3d11_immediate_context_GSGetShader,
    d3d11_immediate_context_IAGetPrimitiveTopology,
    d3d11_immediate_context_VSGetShaderResources,
    d3d11_immediate_context_VSGetSamplers,
    d3d11_immediate_context_GetPredication,
    d3d11_immediate_context_GSGetShaderResources,
    d3d11_immediate_context_GSGetSamplers,
    d3d11_immediate_context_OMGetRenderTargets,
    d3d11_immediate_context_OMGetRenderTargetsAndUnorderedAccessViews,
    d3d11_immediate_context_OMGetBlendState,
    d3d11_immediate_context_OMGetDepthStencilState,
    d3d11_immediate_context_SOGetTargets,
    d3d11_immediate_context_RSGetState,
    d3d11_immediate_context_RSGetViewports,
    d3d11_immediate_context_RSGetScissorRects,
    d3d11_immediate_context_HSGetShaderResources,
    d3d11_immediate_context_HSGetShader,
    d3d11_immediate_context_HSGetSamplers,
    d3d11_immediate_context_HSGetConstantBuffers,
    d3d11_immediate_context_DSGetShaderResources,
    d3d11_immediate_context_DSGetShader,
    d3d11_immediate_context_DSGetSamplers,
    d3d11_immediate_context_DSGetConstantBuffers,
    d3d11_immediate_context_CSGetShaderResources,
    d3d11_immediate_context_CSGetUnorderedAccessViews,
    d3d11_immediate_context_CSGetShader,
    d3d11_immediate_context_CSGetSamplers,
    d3d11_immediate_context_CSGetConstantBuffers,
    d3d11_immediate_context_ClearState,
    d3d11_immediate_context_Flush,
    d3d11_immediate_context_GetType,
    d3d11_immediate_context_GetContextFlags,
    d3d11_immediate_context_FinishCommandList,
};

static HRESULT d3d11_immediate_context_init(struct d3d11_immediate_context *context, struct d3d_device *device)
{
    context->ID3D11DeviceContext_iface.lpVtbl = &d3d11_immediate_context_vtbl;
    context->refcount = 1;

    ID3D11Device_AddRef(&device->ID3D11Device_iface);

    return S_OK;
}

/* ID3D11Device methods */

static HRESULT STDMETHODCALLTYPE d3d11_device_QueryInterface(ID3D11Device *iface, REFIID riid, void **out)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    return IUnknown_QueryInterface(device->outer_unk, riid, out);
}

static ULONG STDMETHODCALLTYPE d3d11_device_AddRef(ID3D11Device *iface)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    return IUnknown_AddRef(device->outer_unk);
}

static ULONG STDMETHODCALLTYPE d3d11_device_Release(ID3D11Device *iface)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    return IUnknown_Release(device->outer_unk);
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateBuffer(ID3D11Device *iface, const D3D11_BUFFER_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *data, ID3D11Buffer **buffer)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_buffer *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, buffer %p.\n", iface, desc, data, buffer);

    if (FAILED(hr = d3d_buffer_create(device, desc, data, &object)))
        return hr;

    *buffer = &object->ID3D11Buffer_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateTexture1D(ID3D11Device *iface,
        const D3D11_TEXTURE1D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data, ID3D11Texture1D **texture)
{
    FIXME("iface %p, desc %p, data %p, texture %p stub!\n", iface, desc, data, texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateTexture2D(ID3D11Device *iface,
        const D3D11_TEXTURE2D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data, ID3D11Texture2D **texture)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_texture2d *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, texture %p.\n", iface, desc, data, texture);

    if (FAILED(hr = d3d_texture2d_create(device, desc, data, &object)))
        return hr;

    *texture = &object->ID3D11Texture2D_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateTexture3D(ID3D11Device *iface,
        const D3D11_TEXTURE3D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data, ID3D11Texture3D **texture)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_texture3d *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, texture %p.\n", iface, desc, data, texture);

    if (FAILED(hr = d3d_texture3d_create(device, desc, data, &object)))
        return hr;

    *texture = &object->ID3D11Texture3D_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateShaderResourceView(ID3D11Device *iface,
        ID3D11Resource *resource, const D3D11_SHADER_RESOURCE_VIEW_DESC *desc, ID3D11ShaderResourceView **view)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_shader_resource_view *object;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    if (FAILED(hr = d3d_shader_resource_view_create(device, resource, desc, &object)))
        return hr;

    *view = &object->ID3D11ShaderResourceView_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateUnorderedAccessView(ID3D11Device *iface,
        ID3D11Resource *resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc, ID3D11UnorderedAccessView **view)
{
    FIXME("iface %p, resource %p, desc %p, view %p stub!\n", iface, resource, desc, view);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateRenderTargetView(ID3D11Device *iface,
        ID3D11Resource *resource, const D3D11_RENDER_TARGET_VIEW_DESC *desc, ID3D11RenderTargetView **view)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_rendertarget_view *object;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    if (FAILED(hr = d3d_rendertarget_view_create(device, resource, desc, &object)))
        return hr;

    *view = &object->ID3D11RenderTargetView_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateDepthStencilView(ID3D11Device *iface,
        ID3D11Resource *resource, const D3D11_DEPTH_STENCIL_VIEW_DESC *desc, ID3D11DepthStencilView **view)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_depthstencil_view *object;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    if (FAILED(hr = d3d_depthstencil_view_create(device, resource, desc, &object)))
        return hr;

    *view = &object->ID3D11DepthStencilView_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateInputLayout(ID3D11Device *iface,
        const D3D11_INPUT_ELEMENT_DESC *element_descs, UINT element_count, const void *shader_byte_code,
        SIZE_T shader_byte_code_length, ID3D11InputLayout **input_layout)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_input_layout *object;
    HRESULT hr;

    TRACE("iface %p, element_descs %p, element_count %u, shader_byte_code %p, shader_byte_code_length %lu, "
            "input_layout %p.\n", iface, element_descs, element_count, shader_byte_code,
            shader_byte_code_length, input_layout);

    if (FAILED(hr = d3d_input_layout_create(device, element_descs, element_count,
            shader_byte_code, shader_byte_code_length, &object)))
        return hr;

    *input_layout = &object->ID3D11InputLayout_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateVertexShader(ID3D11Device *iface, const void *byte_code,
        SIZE_T byte_code_length, ID3D11ClassLinkage *class_linkage, ID3D11VertexShader **shader)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_vertex_shader *object;
    HRESULT hr;

    TRACE("iface %p, byte_code %p, byte_code_length %lu, class_linkage %p, shader %p.\n",
            iface, byte_code, byte_code_length, class_linkage, shader);

    if (class_linkage)
        FIXME("Class linkage is not implemented yet.\n");

    if (FAILED(hr = d3d_vertex_shader_create(device, byte_code, byte_code_length, &object)))
        return hr;

    *shader = &object->ID3D11VertexShader_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateGeometryShader(ID3D11Device *iface, const void *byte_code,
        SIZE_T byte_code_length, ID3D11ClassLinkage *class_linkage, ID3D11GeometryShader **shader)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_geometry_shader *object;
    HRESULT hr;

    TRACE("iface %p, byte_code %p, byte_code_length %lu, class_linkage %p, shader %p.\n",
            iface, byte_code, byte_code_length, class_linkage, shader);

    if (class_linkage)
        FIXME("Class linkage is not implemented yet.\n");

    if (FAILED(hr = d3d_geometry_shader_create(device, byte_code, byte_code_length, &object)))
        return hr;

    *shader = &object->ID3D11GeometryShader_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateGeometryShaderWithStreamOutput(ID3D11Device *iface,
        const void *byte_code, SIZE_T byte_code_length, const D3D11_SO_DECLARATION_ENTRY *so_entries,
        UINT entry_count, const UINT *buffer_strides, UINT strides_count, UINT rasterized_stream,
        ID3D11ClassLinkage *class_linkage, ID3D11GeometryShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, so_entries %p, entry_count %u, "
            "buffer_strides %p, strides_count %u, rasterized_stream %u, class_linkage %p, shader %p stub!\n",
            iface, byte_code, byte_code_length, so_entries, entry_count, buffer_strides, strides_count,
            rasterized_stream, class_linkage, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreatePixelShader(ID3D11Device *iface, const void *byte_code,
        SIZE_T byte_code_length, ID3D11ClassLinkage *class_linkage, ID3D11PixelShader **shader)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_pixel_shader *object;
    HRESULT hr;

    TRACE("iface %p, byte_code %p, byte_code_length %lu, class_linkage %p, shader %p.\n",
            iface, byte_code, byte_code_length, class_linkage, shader);

    if (class_linkage)
        FIXME("Class linkage is not implemented yet.\n");

    if (FAILED(hr = d3d_pixel_shader_create(device, byte_code, byte_code_length, &object)))
        return hr;

    *shader = &object->ID3D11PixelShader_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateHullShader(ID3D11Device *iface, const void *byte_code,
        SIZE_T byte_code_length, ID3D11ClassLinkage *class_linkage, ID3D11HullShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, class_linkage %p, shader %p stub!\n",
            iface, byte_code, byte_code_length, class_linkage, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateDomainShader(ID3D11Device *iface, const void *byte_code,
        SIZE_T byte_code_length, ID3D11ClassLinkage *class_linkage, ID3D11DomainShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, class_linkage %p, shader %p stub!\n",
            iface, byte_code, byte_code_length, class_linkage, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateComputeShader(ID3D11Device *iface, const void *byte_code,
        SIZE_T byte_code_length, ID3D11ClassLinkage *class_linkage, ID3D11ComputeShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, class_linkage %p, shader %p stub!\n",
            iface, byte_code, byte_code_length, class_linkage, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateClassLinkage(ID3D11Device *iface,
        ID3D11ClassLinkage **class_linkage)
{
    FIXME("iface %p, class_linkage %p stub!\n", iface, class_linkage);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateBlendState(ID3D11Device *iface,
        const D3D11_BLEND_DESC *desc, ID3D11BlendState **blend_state)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_blend_state *object;
    struct wine_rb_entry *entry;
    D3D11_BLEND_DESC tmp_desc;
    unsigned int i, j;
    HRESULT hr;

    TRACE("iface %p, desc %p, blend_state %p.\n", iface, desc, blend_state);

    if (!desc)
        return E_INVALIDARG;

    /* D3D11_RENDER_TARGET_BLEND_DESC has a hole, which is a problem because we use
     * D3D11_BLEND_DESC as a key in the rbtree. */
    memset(&tmp_desc, 0, sizeof(tmp_desc));
    tmp_desc.AlphaToCoverageEnable = desc->AlphaToCoverageEnable;
    tmp_desc.IndependentBlendEnable = desc->IndependentBlendEnable;
    for (i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        j = desc->IndependentBlendEnable ? i : 0;
        tmp_desc.RenderTarget[i].BlendEnable = desc->RenderTarget[j].BlendEnable;
        tmp_desc.RenderTarget[i].SrcBlend = desc->RenderTarget[j].SrcBlend;
        tmp_desc.RenderTarget[i].DestBlend = desc->RenderTarget[j].DestBlend;
        tmp_desc.RenderTarget[i].BlendOp = desc->RenderTarget[j].BlendOp;
        tmp_desc.RenderTarget[i].SrcBlendAlpha = desc->RenderTarget[j].SrcBlendAlpha;
        tmp_desc.RenderTarget[i].DestBlendAlpha = desc->RenderTarget[j].DestBlendAlpha;
        tmp_desc.RenderTarget[i].BlendOpAlpha = desc->RenderTarget[j].BlendOpAlpha;
        tmp_desc.RenderTarget[i].RenderTargetWriteMask = desc->RenderTarget[j].RenderTargetWriteMask;
    }

    wined3d_mutex_lock();
    if ((entry = wine_rb_get(&device->blend_states, &tmp_desc)))
    {
        object = WINE_RB_ENTRY_VALUE(entry, struct d3d_blend_state, entry);

        TRACE("Returning existing blend state %p.\n", object);
        *blend_state = &object->ID3D11BlendState_iface;
        ID3D11BlendState_AddRef(*blend_state);
        wined3d_mutex_unlock();

        return S_OK;
    }
    wined3d_mutex_unlock();

    if (!(object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d_blend_state_init(object, device, &tmp_desc)))
    {
        WARN("Failed to initialize blend state, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created blend state %p.\n", object);
    *blend_state = &object->ID3D11BlendState_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateDepthStencilState(ID3D11Device *iface,
        const D3D11_DEPTH_STENCIL_DESC *desc, ID3D11DepthStencilState **depth_stencil_state)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_depthstencil_state *object;
    D3D11_DEPTH_STENCIL_DESC tmp_desc;
    struct wine_rb_entry *entry;
    HRESULT hr;

    TRACE("iface %p, desc %p, depth_stencil_state %p.\n", iface, desc, depth_stencil_state);

    if (!desc)
        return E_INVALIDARG;

    /* D3D11_DEPTH_STENCIL_DESC has a hole, which is a problem because we use
     * it as a key in the rbtree. */
    memset(&tmp_desc, 0, sizeof(tmp_desc));
    tmp_desc.DepthEnable = desc->DepthEnable;
    tmp_desc.DepthWriteMask = desc->DepthWriteMask;
    tmp_desc.DepthFunc = desc->DepthFunc;
    tmp_desc.StencilEnable = desc->StencilEnable;
    tmp_desc.StencilReadMask = desc->StencilReadMask;
    tmp_desc.StencilWriteMask = desc->StencilWriteMask;
    tmp_desc.FrontFace = desc->FrontFace;
    tmp_desc.BackFace = desc->BackFace;

    wined3d_mutex_lock();
    if ((entry = wine_rb_get(&device->depthstencil_states, &tmp_desc)))
    {
        object = WINE_RB_ENTRY_VALUE(entry, struct d3d_depthstencil_state, entry);

        TRACE("Returning existing depthstencil state %p.\n", object);
        *depth_stencil_state = &object->ID3D11DepthStencilState_iface;
        ID3D11DepthStencilState_AddRef(*depth_stencil_state);
        wined3d_mutex_unlock();

        return S_OK;
    }
    wined3d_mutex_unlock();

    if (!(object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d_depthstencil_state_init(object, device, &tmp_desc)))
    {
        WARN("Failed to initialize depthstencil state, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created depthstencil state %p.\n", object);
    *depth_stencil_state = &object->ID3D11DepthStencilState_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateRasterizerState(ID3D11Device *iface,
        const D3D11_RASTERIZER_DESC *desc, ID3D11RasterizerState **rasterizer_state)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_rasterizer_state *object;
    struct wine_rb_entry *entry;
    HRESULT hr;

    TRACE("iface %p, desc %p, rasterizer_state %p.\n", iface, desc, rasterizer_state);

    if (!desc)
        return E_INVALIDARG;

    wined3d_mutex_lock();
    if ((entry = wine_rb_get(&device->rasterizer_states, desc)))
    {
        object = WINE_RB_ENTRY_VALUE(entry, struct d3d_rasterizer_state, entry);

        TRACE("Returning existing rasterizer state %p.\n", object);
        *rasterizer_state = &object->ID3D11RasterizerState_iface;
        ID3D11RasterizerState_AddRef(*rasterizer_state);
        wined3d_mutex_unlock();

        return S_OK;
    }
    wined3d_mutex_unlock();

    if (!(object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d_rasterizer_state_init(object, device, desc)))
    {
        WARN("Failed to initialize rasterizer state, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created rasterizer state %p.\n", object);
    *rasterizer_state = &object->ID3D11RasterizerState_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateSamplerState(ID3D11Device *iface,
        const D3D11_SAMPLER_DESC *desc, ID3D11SamplerState **sampler_state)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    D3D11_SAMPLER_DESC normalized_desc;
    struct d3d_sampler_state *object;
    struct wine_rb_entry *entry;
    HRESULT hr;

    TRACE("iface %p, desc %p, sampler_state %p.\n", iface, desc, sampler_state);

    if (!desc)
        return E_INVALIDARG;

    normalized_desc = *desc;
    if (!D3D11_DECODE_IS_ANISOTROPIC_FILTER(normalized_desc.Filter))
        normalized_desc.MaxAnisotropy = 0;
    if (!D3D11_DECODE_IS_COMPARISON_FILTER(normalized_desc.Filter))
        normalized_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (normalized_desc.AddressU != D3D11_TEXTURE_ADDRESS_BORDER
            && normalized_desc.AddressV != D3D11_TEXTURE_ADDRESS_BORDER
            && normalized_desc.AddressW != D3D11_TEXTURE_ADDRESS_BORDER)
        memset(&normalized_desc.BorderColor, 0, sizeof(normalized_desc.BorderColor));

    wined3d_mutex_lock();
    if ((entry = wine_rb_get(&device->sampler_states, &normalized_desc)))
    {
        object = WINE_RB_ENTRY_VALUE(entry, struct d3d_sampler_state, entry);

        TRACE("Returning existing sampler state %p.\n", object);
        *sampler_state = &object->ID3D11SamplerState_iface;
        ID3D11SamplerState_AddRef(*sampler_state);
        wined3d_mutex_unlock();

        return S_OK;
    }
    wined3d_mutex_unlock();

    if (!(object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d_sampler_state_init(object, device, &normalized_desc)))
    {
        WARN("Failed to initialize sampler state, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created sampler state %p.\n", object);
    *sampler_state = &object->ID3D11SamplerState_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateQuery(ID3D11Device *iface,
        const D3D11_QUERY_DESC *desc, ID3D11Query **query)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_query *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, query %p.\n", iface, desc, query);

    if (FAILED(hr = d3d_query_create(device, desc, FALSE, &object)))
        return hr;

    *query = &object->ID3D11Query_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreatePredicate(ID3D11Device *iface, const D3D11_QUERY_DESC *desc,
        ID3D11Predicate **predicate)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct d3d_query *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, predicate %p.\n", iface, desc, predicate);

    if (FAILED(hr = d3d_query_create(device, desc, TRUE, &object)))
        return hr;

    *predicate = (ID3D11Predicate *)&object->ID3D11Query_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateCounter(ID3D11Device *iface, const D3D11_COUNTER_DESC *desc,
        ID3D11Counter **counter)
{
    FIXME("iface %p, desc %p, counter %p stub!\n", iface, desc, counter);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateDeferredContext(ID3D11Device *iface, UINT flags,
        ID3D11DeviceContext **context)
{
    FIXME("iface %p, flags %#x, context %p stub!\n", iface, flags, context);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_OpenSharedResource(ID3D11Device *iface, HANDLE resource, REFIID riid,
        void **out)
{
    FIXME("iface %p, resource %p, riid %s, out %p stub!\n", iface, resource, debugstr_guid(riid), out);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CheckFormatSupport(ID3D11Device *iface, DXGI_FORMAT format,
        UINT *format_support)
{
    FIXME("iface %p, format %u, format_support %p stub!\n", iface, format, format_support);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CheckMultisampleQualityLevels(ID3D11Device *iface,
        DXGI_FORMAT format, UINT sample_count, UINT *quality_level_count)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);
    struct wined3d_device_creation_parameters params;
    struct wined3d *wined3d;
    HRESULT hr;

    TRACE("iface %p, format %s, sample_count %u, quality_level_count %p.\n",
            iface, debug_dxgi_format(format), sample_count, quality_level_count);

    if (!quality_level_count)
        return E_INVALIDARG;

    *quality_level_count = 0;

    if (!sample_count)
        return E_FAIL;
    if (sample_count == 1)
    {
        *quality_level_count = 1;
        return S_OK;
    }
    if (sample_count > D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT)
        return E_FAIL;

    wined3d_mutex_lock();
    wined3d = wined3d_device_get_wined3d(device->wined3d_device);
    wined3d_device_get_creation_parameters(device->wined3d_device, &params);
    hr = wined3d_check_device_multisample_type(wined3d, params.adapter_idx, params.device_type,
            wined3dformat_from_dxgi_format(format), TRUE, sample_count, quality_level_count);
    wined3d_mutex_unlock();

    if (hr == WINED3DERR_INVALIDCALL)
        return E_INVALIDARG;
    if (hr == WINED3DERR_NOTAVAILABLE)
        return S_OK;
    return hr;
}

static void STDMETHODCALLTYPE d3d11_device_CheckCounterInfo(ID3D11Device *iface, D3D11_COUNTER_INFO *info)
{
    FIXME("iface %p, info %p stub!\n", iface, info);
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CheckCounter(ID3D11Device *iface, const D3D11_COUNTER_DESC *desc,
        D3D11_COUNTER_TYPE *type, UINT *active_counter_count, char *name, UINT *name_length,
        char *units, UINT *units_length, char *description, UINT *description_length)
{
    FIXME("iface %p, desc %p, type %p, active_counter_count %p, name %p, name_length %p, "
            "units %p, units_length %p, description %p, description_length %p stub!\n",
            iface, desc, type, active_counter_count, name, name_length,
            units, units_length, description, description_length);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CheckFeatureSupport(ID3D11Device *iface, D3D11_FEATURE feature,
        void *feature_support_data, UINT feature_support_data_size)
{
    FIXME("iface %p, feature %u, feature_support_data %p, feature_support_data_size %u stub!\n",
            iface, feature, feature_support_data, feature_support_data_size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_GetPrivateData(ID3D11Device *iface, REFGUID guid,
        UINT *data_size, void *data)
{
    IDXGIDevice *dxgi_device;
    HRESULT hr;

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    if (FAILED(hr = ID3D11Device_QueryInterface(iface, &IID_IDXGIDevice, (void **)&dxgi_device)))
        return hr;
    hr = IDXGIDevice_GetPrivateData(dxgi_device, guid, data_size, data);
    IDXGIDevice_Release(dxgi_device);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_SetPrivateData(ID3D11Device *iface, REFGUID guid,
        UINT data_size, const void *data)
{
    IDXGIDevice *dxgi_device;
    HRESULT hr;

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    if (FAILED(hr = ID3D11Device_QueryInterface(iface, &IID_IDXGIDevice, (void **)&dxgi_device)))
        return hr;
    hr = IDXGIDevice_SetPrivateData(dxgi_device, guid, data_size, data);
    IDXGIDevice_Release(dxgi_device);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_SetPrivateDataInterface(ID3D11Device *iface, REFGUID guid,
        const IUnknown *data)
{
    IDXGIDevice *dxgi_device;
    HRESULT hr;

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    if (FAILED(hr = ID3D11Device_QueryInterface(iface, &IID_IDXGIDevice, (void **)&dxgi_device)))
        return hr;
    hr = IDXGIDevice_SetPrivateDataInterface(dxgi_device, guid, data);
    IDXGIDevice_Release(dxgi_device);

    return hr;
}

static D3D_FEATURE_LEVEL STDMETHODCALLTYPE d3d11_device_GetFeatureLevel(ID3D11Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return D3D_FEATURE_LEVEL_10_0;
}

static UINT STDMETHODCALLTYPE d3d11_device_GetCreationFlags(ID3D11Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_GetDeviceRemovedReason(ID3D11Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return S_OK;
}

static void STDMETHODCALLTYPE d3d11_device_GetImmediateContext(ID3D11Device *iface,
        ID3D11DeviceContext **immediate_context)
{
    struct d3d_device *device = impl_from_ID3D11Device(iface);

    TRACE("iface %p, immediate_context %p.\n", iface, immediate_context);

    *immediate_context = &device->immediate_context.ID3D11DeviceContext_iface;
    ID3D11DeviceContext_AddRef(*immediate_context);
}

static HRESULT STDMETHODCALLTYPE d3d11_device_SetExceptionMode(ID3D11Device *iface, UINT flags)
{
    FIXME("iface %p, flags %#x stub!\n", iface, flags);

    return E_NOTIMPL;
}

static UINT STDMETHODCALLTYPE d3d11_device_GetExceptionMode(ID3D11Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static const struct ID3D11DeviceVtbl d3d11_device_vtbl =
{
    /* IUnknown methods */
    d3d11_device_QueryInterface,
    d3d11_device_AddRef,
    d3d11_device_Release,
    /* ID3D11Device methods */
    d3d11_device_CreateBuffer,
    d3d11_device_CreateTexture1D,
    d3d11_device_CreateTexture2D,
    d3d11_device_CreateTexture3D,
    d3d11_device_CreateShaderResourceView,
    d3d11_device_CreateUnorderedAccessView,
    d3d11_device_CreateRenderTargetView,
    d3d11_device_CreateDepthStencilView,
    d3d11_device_CreateInputLayout,
    d3d11_device_CreateVertexShader,
    d3d11_device_CreateGeometryShader,
    d3d11_device_CreateGeometryShaderWithStreamOutput,
    d3d11_device_CreatePixelShader,
    d3d11_device_CreateHullShader,
    d3d11_device_CreateDomainShader,
    d3d11_device_CreateComputeShader,
    d3d11_device_CreateClassLinkage,
    d3d11_device_CreateBlendState,
    d3d11_device_CreateDepthStencilState,
    d3d11_device_CreateRasterizerState,
    d3d11_device_CreateSamplerState,
    d3d11_device_CreateQuery,
    d3d11_device_CreatePredicate,
    d3d11_device_CreateCounter,
    d3d11_device_CreateDeferredContext,
    d3d11_device_OpenSharedResource,
    d3d11_device_CheckFormatSupport,
    d3d11_device_CheckMultisampleQualityLevels,
    d3d11_device_CheckCounterInfo,
    d3d11_device_CheckCounter,
    d3d11_device_CheckFeatureSupport,
    d3d11_device_GetPrivateData,
    d3d11_device_SetPrivateData,
    d3d11_device_SetPrivateDataInterface,
    d3d11_device_GetFeatureLevel,
    d3d11_device_GetCreationFlags,
    d3d11_device_GetDeviceRemovedReason,
    d3d11_device_GetImmediateContext,
    d3d11_device_SetExceptionMode,
    d3d11_device_GetExceptionMode,
};

/* Inner IUnknown methods */

static inline struct d3d_device *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_device, IUnknown_inner);
}

static HRESULT STDMETHODCALLTYPE d3d_device_inner_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    struct d3d_device *device = impl_from_IUnknown(iface);

    TRACE("iface %p, riid %s, out %p.\n", iface, debugstr_guid(riid), out);

    if (IsEqualGUID(riid, &IID_ID3D11Device)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        *out = &device->ID3D11Device_iface;
    }
    else if (IsEqualGUID(riid, &IID_ID3D10Device1)
            || IsEqualGUID(riid, &IID_ID3D10Device))
    {
        *out = &device->ID3D10Device1_iface;
    }
    else if (IsEqualGUID(riid, &IID_ID3D10Multithread))
    {
        *out = &device->ID3D10Multithread_iface;
    }
    else if (IsEqualGUID(riid, &IID_IWineDXGIDeviceParent))
    {
        *out = &device->IWineDXGIDeviceParent_iface;
    }
    else
    {
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));
        *out = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE d3d_device_inner_AddRef(IUnknown *iface)
{
    struct d3d_device *device = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedIncrement(&device->refcount);

    TRACE("%p increasing refcount to %u.\n", device, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d_device_inner_Release(IUnknown *iface)
{
    struct d3d_device *device = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&device->refcount);

    TRACE("%p decreasing refcount to %u.\n", device, refcount);

    if (!refcount)
    {
        if (device->wined3d_device)
        {
            wined3d_mutex_lock();
            wined3d_device_decref(device->wined3d_device);
            wined3d_mutex_unlock();
        }
        wine_rb_destroy(&device->sampler_states, NULL, NULL);
        wine_rb_destroy(&device->rasterizer_states, NULL, NULL);
        wine_rb_destroy(&device->depthstencil_states, NULL, NULL);
        wine_rb_destroy(&device->blend_states, NULL, NULL);
    }

    return refcount;
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3d10_device_QueryInterface(ID3D10Device1 *iface, REFIID riid,
        void **ppv)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);
    return IUnknown_QueryInterface(This->outer_unk, riid, ppv);
}

static ULONG STDMETHODCALLTYPE d3d10_device_AddRef(ID3D10Device1 *iface)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);
    return IUnknown_AddRef(This->outer_unk);
}

static ULONG STDMETHODCALLTYPE d3d10_device_Release(ID3D10Device1 *iface)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);
    return IUnknown_Release(This->outer_unk);
}

/* ID3D10Device methods */

static void STDMETHODCALLTYPE d3d10_device_VSSetConstantBuffers(ID3D10Device1 *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer *const *buffers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D10Buffer(buffers[i]);

        wined3d_device_set_vs_cb(device->wined3d_device, start_slot + i,
                buffer ? buffer->wined3d_buffer : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_PSSetShaderResources(ID3D10Device1 *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView *const *views)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n",
            iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct d3d_shader_resource_view *view = unsafe_impl_from_ID3D10ShaderResourceView(views[i]);

        wined3d_device_set_ps_resource_view(device->wined3d_device, start_slot + i,
                view ? view->wined3d_view : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_PSSetShader(ID3D10Device1 *iface,
        ID3D10PixelShader *shader)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);
    struct d3d_pixel_shader *ps = unsafe_impl_from_ID3D10PixelShader(shader);

    TRACE("iface %p, shader %p\n", iface, shader);

    wined3d_mutex_lock();
    wined3d_device_set_pixel_shader(This->wined3d_device, ps ? ps->wined3d_shader : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_PSSetSamplers(ID3D10Device1 *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState *const *samplers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler = unsafe_impl_from_ID3D10SamplerState(samplers[i]);

        wined3d_device_set_ps_sampler(device->wined3d_device, start_slot + i,
                sampler ? sampler->wined3d_sampler : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_VSSetShader(ID3D10Device1 *iface,
        ID3D10VertexShader *shader)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);
    struct d3d_vertex_shader *vs = unsafe_impl_from_ID3D10VertexShader(shader);

    TRACE("iface %p, shader %p\n", iface, shader);

    wined3d_mutex_lock();
    wined3d_device_set_vertex_shader(This->wined3d_device, vs ? vs->wined3d_shader : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_DrawIndexed(ID3D10Device1 *iface, UINT index_count,
        UINT start_index_location, INT base_vertex_location)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);

    TRACE("iface %p, index_count %u, start_index_location %u, base_vertex_location %d.\n",
            iface, index_count, start_index_location, base_vertex_location);

    wined3d_mutex_lock();
    wined3d_device_set_base_vertex_index(This->wined3d_device, base_vertex_location);
    wined3d_device_draw_indexed_primitive(This->wined3d_device, start_index_location, index_count);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_Draw(ID3D10Device1 *iface, UINT vertex_count,
        UINT start_vertex_location)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);

    TRACE("iface %p, vertex_count %u, start_vertex_location %u\n",
            iface, vertex_count, start_vertex_location);

    wined3d_mutex_lock();
    wined3d_device_draw_primitive(This->wined3d_device, start_vertex_location, vertex_count);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_PSSetConstantBuffers(ID3D10Device1 *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer *const *buffers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D10Buffer(buffers[i]);

        wined3d_device_set_ps_cb(device->wined3d_device, start_slot + i,
                buffer ? buffer->wined3d_buffer : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_IASetInputLayout(ID3D10Device1 *iface,
        ID3D10InputLayout *input_layout)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);
    struct d3d_input_layout *layout = unsafe_impl_from_ID3D10InputLayout(input_layout);

    TRACE("iface %p, input_layout %p\n", iface, input_layout);

    wined3d_mutex_lock();
    wined3d_device_set_vertex_declaration(This->wined3d_device,
            layout ? layout->wined3d_decl : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_IASetVertexBuffers(ID3D10Device1 *iface, UINT start_slot,
        UINT buffer_count, ID3D10Buffer *const *buffers, const UINT *strides, const UINT *offsets)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p, strides %p, offsets %p\n",
            iface, start_slot, buffer_count, buffers, strides, offsets);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D10Buffer(buffers[i]);

        wined3d_device_set_stream_source(This->wined3d_device, start_slot + i,
                buffer ? buffer->wined3d_buffer : NULL, offsets[i], strides[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_IASetIndexBuffer(ID3D10Device1 *iface,
        ID3D10Buffer *buffer, DXGI_FORMAT format, UINT offset)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);
    struct d3d_buffer *buffer_impl = unsafe_impl_from_ID3D10Buffer(buffer);

    TRACE("iface %p, buffer %p, format %s, offset %u.\n",
            iface, buffer, debug_dxgi_format(format), offset);

    wined3d_mutex_lock();
    wined3d_device_set_index_buffer(This->wined3d_device,
            buffer_impl ? buffer_impl->wined3d_buffer : NULL,
            wined3dformat_from_dxgi_format(format));
    wined3d_mutex_unlock();
    if (offset) FIXME("offset %u not supported.\n", offset);
}

static void STDMETHODCALLTYPE d3d10_device_DrawIndexedInstanced(ID3D10Device1 *iface,
        UINT instance_index_count, UINT instance_count, UINT start_index_location,
        INT base_vertex_location, UINT start_instance_location)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, instance_index_count %u, instance_count %u, start_index_location %u, "
            "base_vertex_location %d, start_instance_location %u.\n",
            iface, instance_index_count, instance_count, start_index_location,
            base_vertex_location, start_instance_location);

    wined3d_mutex_lock();
    wined3d_device_set_base_vertex_index(device->wined3d_device, base_vertex_location);
    wined3d_device_draw_indexed_primitive_instanced(device->wined3d_device, start_index_location,
            instance_index_count, start_instance_location, instance_count);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_DrawInstanced(ID3D10Device1 *iface,
        UINT instance_vertex_count, UINT instance_count,
        UINT start_vertex_location, UINT start_instance_location)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, instance_vertex_count %u, instance_count %u, start_vertex_location %u, "
            "start_instance_location %u.\n", iface, instance_vertex_count, instance_count,
            start_vertex_location, start_instance_location);

    wined3d_mutex_lock();
    wined3d_device_draw_primitive_instanced(device->wined3d_device, start_vertex_location,
            instance_vertex_count, start_instance_location, instance_count);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_GSSetConstantBuffers(ID3D10Device1 *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer *const *buffers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D10Buffer(buffers[i]);

        wined3d_device_set_gs_cb(device->wined3d_device, start_slot + i,
                buffer ? buffer->wined3d_buffer : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_GSSetShader(ID3D10Device1 *iface, ID3D10GeometryShader *shader)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_geometry_shader *gs = unsafe_impl_from_ID3D10GeometryShader(shader);

    TRACE("iface %p, shader %p.\n", iface, shader);

    wined3d_mutex_lock();
    wined3d_device_set_geometry_shader(device->wined3d_device, gs ? gs->wined3d_shader : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_IASetPrimitiveTopology(ID3D10Device1 *iface,
        D3D10_PRIMITIVE_TOPOLOGY topology)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);

    TRACE("iface %p, topology %s\n", iface, debug_d3d10_primitive_topology(topology));

    wined3d_mutex_lock();
    wined3d_device_set_primitive_type(This->wined3d_device, (enum wined3d_primitive_type)topology);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_VSSetShaderResources(ID3D10Device1 *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView *const *views)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n",
            iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct d3d_shader_resource_view *view = unsafe_impl_from_ID3D10ShaderResourceView(views[i]);

        wined3d_device_set_vs_resource_view(device->wined3d_device, start_slot + i,
                view ? view->wined3d_view : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_VSSetSamplers(ID3D10Device1 *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState *const *samplers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler = unsafe_impl_from_ID3D10SamplerState(samplers[i]);

        wined3d_device_set_vs_sampler(device->wined3d_device, start_slot + i,
                sampler ? sampler->wined3d_sampler : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_SetPredication(ID3D10Device1 *iface, ID3D10Predicate *predicate, BOOL value)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_query *query;

    TRACE("iface %p, predicate %p, value %#x.\n", iface, predicate, value);

    query = unsafe_impl_from_ID3D10Query((ID3D10Query *)predicate);
    wined3d_mutex_lock();
    wined3d_device_set_predication(device->wined3d_device, query ? query->wined3d_query : NULL, value);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_GSSetShaderResources(ID3D10Device1 *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView *const *views)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n",
            iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct d3d_shader_resource_view *view = unsafe_impl_from_ID3D10ShaderResourceView(views[i]);

        wined3d_device_set_gs_resource_view(device->wined3d_device, start_slot + i,
                view ? view->wined3d_view : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_GSSetSamplers(ID3D10Device1 *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState *const *samplers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler = unsafe_impl_from_ID3D10SamplerState(samplers[i]);

        wined3d_device_set_gs_sampler(device->wined3d_device, start_slot + i,
                sampler ? sampler->wined3d_sampler : NULL);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_OMSetRenderTargets(ID3D10Device1 *iface,
        UINT render_target_view_count, ID3D10RenderTargetView *const *render_target_views,
        ID3D10DepthStencilView *depth_stencil_view)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_depthstencil_view *dsv;
    unsigned int i;

    TRACE("iface %p, render_target_view_count %u, render_target_views %p, depth_stencil_view %p.\n",
            iface, render_target_view_count, render_target_views, depth_stencil_view);

    wined3d_mutex_lock();
    for (i = 0; i < render_target_view_count; ++i)
    {
        struct d3d_rendertarget_view *rtv = unsafe_impl_from_ID3D10RenderTargetView(render_target_views[i]);

        wined3d_device_set_rendertarget_view(device->wined3d_device, i,
                rtv ? rtv->wined3d_view : NULL, FALSE);
    }
    for (; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        wined3d_device_set_rendertarget_view(device->wined3d_device, i, NULL, FALSE);
    }

    dsv = unsafe_impl_from_ID3D10DepthStencilView(depth_stencil_view);
    wined3d_device_set_depth_stencil_view(device->wined3d_device,
            dsv ? dsv->wined3d_view : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_OMSetBlendState(ID3D10Device1 *iface,
        ID3D10BlendState *blend_state, const FLOAT blend_factor[4], UINT sample_mask)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_blend_state *blend_state_object;

    TRACE("iface %p, blend_state %p, blend_factor %p, sample_mask 0x%08x.\n",
            iface, blend_state, blend_factor, sample_mask);

    blend_state_object = unsafe_impl_from_ID3D10BlendState(blend_state);
    d3d11_immediate_context_OMSetBlendState(&device->immediate_context.ID3D11DeviceContext_iface,
            blend_state_object ? &blend_state_object->ID3D11BlendState_iface : NULL, blend_factor, sample_mask);
}

static void STDMETHODCALLTYPE d3d10_device_OMSetDepthStencilState(ID3D10Device1 *iface,
        ID3D10DepthStencilState *depth_stencil_state, UINT stencil_ref)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, depth_stencil_state %p, stencil_ref %u.\n",
            iface, depth_stencil_state, stencil_ref);

    device->depth_stencil_state = unsafe_impl_from_ID3D10DepthStencilState(depth_stencil_state);
    device->stencil_ref = stencil_ref;
}

static void STDMETHODCALLTYPE d3d10_device_SOSetTargets(ID3D10Device1 *iface,
        UINT target_count, ID3D10Buffer *const *targets, const UINT *offsets)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int count, i;

    TRACE("iface %p, target_count %u, targets %p, offsets %p.\n", iface, target_count, targets, offsets);

    count = min(target_count, 4);
    wined3d_mutex_lock();
    for (i = 0; i < count; ++i)
    {
        struct d3d_buffer *buffer = unsafe_impl_from_ID3D10Buffer(targets[i]);

        wined3d_device_set_stream_output(device->wined3d_device, i,
                buffer ? buffer->wined3d_buffer : NULL, offsets[i]);
    }

    for (i = count; i < 4; ++i)
    {
        wined3d_device_set_stream_output(device->wined3d_device, i, NULL, 0);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_DrawAuto(ID3D10Device1 *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static void STDMETHODCALLTYPE d3d10_device_RSSetState(ID3D10Device1 *iface, ID3D10RasterizerState *rasterizer_state)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_rasterizer_state *rasterizer_state_object;

    TRACE("iface %p, rasterizer_state %p.\n", iface, rasterizer_state);

    rasterizer_state_object = unsafe_impl_from_ID3D10RasterizerState(rasterizer_state);
    d3d11_immediate_context_RSSetState(&device->immediate_context.ID3D11DeviceContext_iface,
            rasterizer_state_object ? &rasterizer_state_object->ID3D11RasterizerState_iface : NULL);
}

static void STDMETHODCALLTYPE d3d10_device_RSSetViewports(ID3D10Device1 *iface,
        UINT viewport_count, const D3D10_VIEWPORT *viewports)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct wined3d_viewport wined3d_vp;

    TRACE("iface %p, viewport_count %u, viewports %p.\n", iface, viewport_count, viewports);

    if (viewport_count > 1)
        FIXME("Multiple viewports not implemented.\n");

    if (!viewport_count)
        return;

    wined3d_vp.x = viewports[0].TopLeftX;
    wined3d_vp.y = viewports[0].TopLeftY;
    wined3d_vp.width = viewports[0].Width;
    wined3d_vp.height = viewports[0].Height;
    wined3d_vp.min_z = viewports[0].MinDepth;
    wined3d_vp.max_z = viewports[0].MaxDepth;

    wined3d_mutex_lock();
    wined3d_device_set_viewport(device->wined3d_device, &wined3d_vp);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_RSSetScissorRects(ID3D10Device1 *iface,
        UINT rect_count, const D3D10_RECT *rects)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, rect_count %u, rects %p.\n", iface, rect_count, rects);

    if (rect_count > 1)
        FIXME("Multiple scissor rects not implemented.\n");

    if (!rect_count)
        return;

    wined3d_mutex_lock();
    wined3d_device_set_scissor_rect(device->wined3d_device, rects);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_CopySubresourceRegion(ID3D10Device1 *iface,
        ID3D10Resource *dst_resource, UINT dst_subresource_idx, UINT dst_x, UINT dst_y, UINT dst_z,
        ID3D10Resource *src_resource, UINT src_subresource_idx, const D3D10_BOX *src_box)
{
    struct wined3d_resource *wined3d_dst_resource, *wined3d_src_resource;
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct wined3d_box wined3d_src_box;

    TRACE("iface %p, dst_resource %p, dst_subresource_idx %u, dst_x %u, dst_y %u, dst_z %u, "
            "src_resource %p, src_subresource_idx %u, src_box %p.\n",
            iface, dst_resource, dst_subresource_idx, dst_x, dst_y, dst_z,
            src_resource, src_subresource_idx, src_box);

    if (src_box)
    {
        wined3d_src_box.left = src_box->left;
        wined3d_src_box.top = src_box->top;
        wined3d_src_box.front = src_box->front;
        wined3d_src_box.right = src_box->right;
        wined3d_src_box.bottom = src_box->bottom;
        wined3d_src_box.back = src_box->back;
    }

    wined3d_dst_resource = wined3d_resource_from_d3d10_resource(dst_resource);
    wined3d_src_resource = wined3d_resource_from_d3d10_resource(src_resource);
    wined3d_mutex_lock();
    wined3d_device_copy_sub_resource_region(device->wined3d_device, wined3d_dst_resource, dst_subresource_idx,
            dst_x, dst_y, dst_z, wined3d_src_resource, src_subresource_idx, src_box ? &wined3d_src_box : NULL);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_CopyResource(ID3D10Device1 *iface,
        ID3D10Resource *dst_resource, ID3D10Resource *src_resource)
{
    struct wined3d_resource *wined3d_dst_resource, *wined3d_src_resource;
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, dst_resource %p, src_resource %p.\n", iface, dst_resource, src_resource);

    wined3d_dst_resource = wined3d_resource_from_d3d10_resource(dst_resource);
    wined3d_src_resource = wined3d_resource_from_d3d10_resource(src_resource);
    wined3d_mutex_lock();
    wined3d_device_copy_resource(device->wined3d_device, wined3d_dst_resource, wined3d_src_resource);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_UpdateSubresource(ID3D10Device1 *iface,
        ID3D10Resource *resource, UINT subresource_idx, const D3D10_BOX *box,
        const void *data, UINT row_pitch, UINT depth_pitch)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    ID3D11Resource *d3d11_resource;

    TRACE("iface %p, resource %p, subresource_idx %u, box %p, data %p, row_pitch %u, depth_pitch %u.\n",
            iface, resource, subresource_idx, box, data, row_pitch, depth_pitch);

    ID3D10Resource_QueryInterface(resource, &IID_ID3D11Resource, (void **)&d3d11_resource);
    d3d11_immediate_context_UpdateSubresource(&device->immediate_context.ID3D11DeviceContext_iface,
            d3d11_resource, subresource_idx, (const D3D11_BOX *)box, data, row_pitch, depth_pitch);
    ID3D11Resource_Release(d3d11_resource);
}

static void STDMETHODCALLTYPE d3d10_device_ClearRenderTargetView(ID3D10Device1 *iface,
        ID3D10RenderTargetView *render_target_view, const FLOAT color_rgba[4])
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_rendertarget_view *view = unsafe_impl_from_ID3D10RenderTargetView(render_target_view);
    const struct wined3d_color color = {color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]};
    HRESULT hr;

    TRACE("iface %p, render_target_view %p, color_rgba {%.8e, %.8e, %.8e, %.8e}.\n",
            iface, render_target_view, color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);

    wined3d_mutex_lock();
    if (FAILED(hr = wined3d_device_clear_rendertarget_view(device->wined3d_device, view->wined3d_view, NULL, &color)))
        ERR("Failed to clear view, hr %#x.\n", hr);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_ClearDepthStencilView(ID3D10Device1 *iface,
        ID3D10DepthStencilView *depth_stencil_view, UINT flags, FLOAT depth, UINT8 stencil)
{
    FIXME("iface %p, depth_stencil_view %p, flags %#x, depth %f, stencil %u stub!\n",
            iface, depth_stencil_view, flags, depth, stencil);
}

static void STDMETHODCALLTYPE d3d10_device_GenerateMips(ID3D10Device1 *iface,
        ID3D10ShaderResourceView *shader_resource_view)
{
    FIXME("iface %p, shader_resource_view %p stub!\n", iface, shader_resource_view);
}

static void STDMETHODCALLTYPE d3d10_device_ResolveSubresource(ID3D10Device1 *iface,
        ID3D10Resource *dst_resource, UINT dst_subresource_idx,
        ID3D10Resource *src_resource, UINT src_subresource_idx, DXGI_FORMAT format)
{
    FIXME("iface %p, dst_resource %p, dst_subresource_idx %u, "
            "src_resource %p, src_subresource_idx %u, format %s stub!\n",
            iface, dst_resource, dst_subresource_idx,
            src_resource, src_subresource_idx, debug_dxgi_format(format));
}

static void STDMETHODCALLTYPE d3d10_device_VSGetConstantBuffers(ID3D10Device1 *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer **buffers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (!(wined3d_buffer = wined3d_device_get_vs_cb(device->wined3d_device, start_slot + i)))
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D10Buffer_iface;
        ID3D10Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_PSGetShaderResources(ID3D10Device1 *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView **views)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n",
            iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct wined3d_shader_resource_view *wined3d_view;
        struct d3d_shader_resource_view *view_impl;

        if (!(wined3d_view = wined3d_device_get_ps_resource_view(device->wined3d_device, start_slot + i)))
        {
            views[i] = NULL;
            continue;
        }

        view_impl = wined3d_shader_resource_view_get_parent(wined3d_view);
        views[i] = (ID3D10ShaderResourceView *)&view_impl->ID3D10ShaderResourceView1_iface;
        ID3D10ShaderResourceView_AddRef(views[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_PSGetShader(ID3D10Device1 *iface, ID3D10PixelShader **shader)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_pixel_shader *shader_impl;
    struct wined3d_shader *wined3d_shader;

    TRACE("iface %p, shader %p.\n", iface, shader);

    wined3d_mutex_lock();
    if (!(wined3d_shader = wined3d_device_get_pixel_shader(device->wined3d_device)))
    {
        wined3d_mutex_unlock();
        *shader = NULL;
        return;
    }

    shader_impl = wined3d_shader_get_parent(wined3d_shader);
    wined3d_mutex_unlock();
    *shader = &shader_impl->ID3D10PixelShader_iface;
    ID3D10PixelShader_AddRef(*shader);
}

static void STDMETHODCALLTYPE d3d10_device_PSGetSamplers(ID3D10Device1 *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState **samplers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler_impl;
        struct wined3d_sampler *wined3d_sampler;

        if (!(wined3d_sampler = wined3d_device_get_ps_sampler(device->wined3d_device, start_slot + i)))
        {
            samplers[i] = NULL;
            continue;
        }

        sampler_impl = wined3d_sampler_get_parent(wined3d_sampler);
        samplers[i] = &sampler_impl->ID3D10SamplerState_iface;
        ID3D10SamplerState_AddRef(samplers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_VSGetShader(ID3D10Device1 *iface, ID3D10VertexShader **shader)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_vertex_shader *shader_impl;
    struct wined3d_shader *wined3d_shader;

    TRACE("iface %p, shader %p.\n", iface, shader);

    wined3d_mutex_lock();
    if (!(wined3d_shader = wined3d_device_get_vertex_shader(device->wined3d_device)))
    {
        wined3d_mutex_unlock();
        *shader = NULL;
        return;
    }

    shader_impl = wined3d_shader_get_parent(wined3d_shader);
    wined3d_mutex_unlock();
    *shader = &shader_impl->ID3D10VertexShader_iface;
    ID3D10VertexShader_AddRef(*shader);
}

static void STDMETHODCALLTYPE d3d10_device_PSGetConstantBuffers(ID3D10Device1 *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer **buffers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (!(wined3d_buffer = wined3d_device_get_ps_cb(device->wined3d_device, start_slot + i)))
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D10Buffer_iface;
        ID3D10Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_IAGetInputLayout(ID3D10Device1 *iface, ID3D10InputLayout **input_layout)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct wined3d_vertex_declaration *wined3d_declaration;
    struct d3d_input_layout *input_layout_impl;

    TRACE("iface %p, input_layout %p.\n", iface, input_layout);

    wined3d_mutex_lock();
    if (!(wined3d_declaration = wined3d_device_get_vertex_declaration(device->wined3d_device)))
    {
        wined3d_mutex_unlock();
        *input_layout = NULL;
        return;
    }

    input_layout_impl = wined3d_vertex_declaration_get_parent(wined3d_declaration);
    wined3d_mutex_unlock();
    *input_layout = &input_layout_impl->ID3D10InputLayout_iface;
    ID3D10InputLayout_AddRef(*input_layout);
}

static void STDMETHODCALLTYPE d3d10_device_IAGetVertexBuffers(ID3D10Device1 *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer **buffers, UINT *strides, UINT *offsets)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p, strides %p, offsets %p.\n",
            iface, start_slot, buffer_count, buffers, strides, offsets);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (FAILED(wined3d_device_get_stream_source(device->wined3d_device, start_slot + i,
                &wined3d_buffer, &offsets[i], &strides[i])))
            ERR("Failed to get vertex buffer.\n");

        if (!wined3d_buffer)
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D10Buffer_iface;
        ID3D10Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_IAGetIndexBuffer(ID3D10Device1 *iface,
        ID3D10Buffer **buffer, DXGI_FORMAT *format, UINT *offset)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    enum wined3d_format_id wined3d_format;
    struct wined3d_buffer *wined3d_buffer;
    struct d3d_buffer *buffer_impl;

    TRACE("iface %p, buffer %p, format %p, offset %p.\n", iface, buffer, format, offset);

    wined3d_mutex_lock();
    wined3d_buffer = wined3d_device_get_index_buffer(device->wined3d_device, &wined3d_format);
    *format = dxgi_format_from_wined3dformat(wined3d_format);
    *offset = 0; /* FIXME */
    if (!wined3d_buffer)
    {
        wined3d_mutex_unlock();
        *buffer = NULL;
        return;
    }

    buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
    wined3d_mutex_unlock();
    *buffer = &buffer_impl->ID3D10Buffer_iface;
    ID3D10Buffer_AddRef(*buffer);
}

static void STDMETHODCALLTYPE d3d10_device_GSGetConstantBuffers(ID3D10Device1 *iface,
        UINT start_slot, UINT buffer_count, ID3D10Buffer **buffers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, buffer_count %u, buffers %p.\n",
            iface, start_slot, buffer_count, buffers);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (!(wined3d_buffer = wined3d_device_get_gs_cb(device->wined3d_device, start_slot + i)))
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D10Buffer_iface;
        ID3D10Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_GSGetShader(ID3D10Device1 *iface, ID3D10GeometryShader **shader)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_geometry_shader *shader_impl;
    struct wined3d_shader *wined3d_shader;

    TRACE("iface %p, shader %p.\n", iface, shader);

    wined3d_mutex_lock();
    if (!(wined3d_shader = wined3d_device_get_geometry_shader(device->wined3d_device)))
    {
        wined3d_mutex_unlock();
        *shader = NULL;
        return;
    }

    shader_impl = wined3d_shader_get_parent(wined3d_shader);
    wined3d_mutex_unlock();
    *shader = &shader_impl->ID3D10GeometryShader_iface;
    ID3D10GeometryShader_AddRef(*shader);
}

static void STDMETHODCALLTYPE d3d10_device_IAGetPrimitiveTopology(ID3D10Device1 *iface,
        D3D10_PRIMITIVE_TOPOLOGY *topology)
{
    struct d3d_device *This = impl_from_ID3D10Device(iface);

    TRACE("iface %p, topology %p\n", iface, topology);

    wined3d_mutex_lock();
    wined3d_device_get_primitive_type(This->wined3d_device, (enum wined3d_primitive_type *)topology);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_VSGetShaderResources(ID3D10Device1 *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView **views)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n",
            iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct wined3d_shader_resource_view *wined3d_view;
        struct d3d_shader_resource_view *view_impl;

        if (!(wined3d_view = wined3d_device_get_vs_resource_view(device->wined3d_device, start_slot + i)))
        {
            views[i] = NULL;
            continue;
        }

        view_impl = wined3d_shader_resource_view_get_parent(wined3d_view);
        views[i] = (ID3D10ShaderResourceView *)&view_impl->ID3D10ShaderResourceView1_iface;
        ID3D10ShaderResourceView_AddRef(views[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_VSGetSamplers(ID3D10Device1 *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState **samplers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler_impl;
        struct wined3d_sampler *wined3d_sampler;

        if (!(wined3d_sampler = wined3d_device_get_vs_sampler(device->wined3d_device, start_slot + i)))
        {
            samplers[i] = NULL;
            continue;
        }

        sampler_impl = wined3d_sampler_get_parent(wined3d_sampler);
        samplers[i] = &sampler_impl->ID3D10SamplerState_iface;
        ID3D10SamplerState_AddRef(samplers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_GetPredication(ID3D10Device1 *iface,
        ID3D10Predicate **predicate, BOOL *value)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct wined3d_query *wined3d_predicate;
    struct d3d_query *predicate_impl;

    TRACE("iface %p, predicate %p, value %p.\n", iface, predicate, value);

    wined3d_mutex_lock();
    if (!(wined3d_predicate = wined3d_device_get_predication(device->wined3d_device, value)))
    {
        wined3d_mutex_unlock();
        *predicate = NULL;
        return;
    }

    predicate_impl = wined3d_query_get_parent(wined3d_predicate);
    wined3d_mutex_unlock();
    *predicate = (ID3D10Predicate *)&predicate_impl->ID3D10Query_iface;
    ID3D10Predicate_AddRef(*predicate);
}

static void STDMETHODCALLTYPE d3d10_device_GSGetShaderResources(ID3D10Device1 *iface,
        UINT start_slot, UINT view_count, ID3D10ShaderResourceView **views)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n",
            iface, start_slot, view_count, views);

    wined3d_mutex_lock();
    for (i = 0; i < view_count; ++i)
    {
        struct wined3d_shader_resource_view *wined3d_view;
        struct d3d_shader_resource_view *view_impl;

        if (!(wined3d_view = wined3d_device_get_gs_resource_view(device->wined3d_device, start_slot + i)))
        {
            views[i] = NULL;
            continue;
        }

        view_impl = wined3d_shader_resource_view_get_parent(wined3d_view);
        views[i] = (ID3D10ShaderResourceView *)&view_impl->ID3D10ShaderResourceView1_iface;
        ID3D10ShaderResourceView_AddRef(views[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_GSGetSamplers(ID3D10Device1 *iface,
        UINT start_slot, UINT sampler_count, ID3D10SamplerState **samplers)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, start_slot %u, sampler_count %u, samplers %p.\n",
            iface, start_slot, sampler_count, samplers);

    wined3d_mutex_lock();
    for (i = 0; i < sampler_count; ++i)
    {
        struct d3d_sampler_state *sampler_impl;
        struct wined3d_sampler *wined3d_sampler;

        if (!(wined3d_sampler = wined3d_device_get_gs_sampler(device->wined3d_device, start_slot + i)))
        {
            samplers[i] = NULL;
            continue;
        }

        sampler_impl = wined3d_sampler_get_parent(wined3d_sampler);
        samplers[i] = &sampler_impl->ID3D10SamplerState_iface;
        ID3D10SamplerState_AddRef(samplers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_OMGetRenderTargets(ID3D10Device1 *iface,
        UINT view_count, ID3D10RenderTargetView **render_target_views, ID3D10DepthStencilView **depth_stencil_view)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct wined3d_rendertarget_view *wined3d_view;

    TRACE("iface %p, view_count %u, render_target_views %p, depth_stencil_view %p.\n",
            iface, view_count, render_target_views, depth_stencil_view);

    wined3d_mutex_lock();
    if (render_target_views)
    {
        struct d3d_rendertarget_view *view_impl;
        unsigned int i;

        for (i = 0; i < view_count; ++i)
        {
            if (!(wined3d_view = wined3d_device_get_rendertarget_view(device->wined3d_device, i))
                    || !(view_impl = wined3d_rendertarget_view_get_parent(wined3d_view)))
            {
                render_target_views[i] = NULL;
                continue;
            }

            render_target_views[i] = &view_impl->ID3D10RenderTargetView_iface;
            ID3D10RenderTargetView_AddRef(render_target_views[i]);
        }
    }

    if (depth_stencil_view)
    {
        struct d3d_depthstencil_view *view_impl;

        if (!(wined3d_view = wined3d_device_get_depth_stencil_view(device->wined3d_device))
                || !(view_impl = wined3d_rendertarget_view_get_parent(wined3d_view)))
        {
            *depth_stencil_view = NULL;
        }
        else
        {
            *depth_stencil_view = &view_impl->ID3D10DepthStencilView_iface;
            ID3D10DepthStencilView_AddRef(*depth_stencil_view);
        }
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_OMGetBlendState(ID3D10Device1 *iface,
        ID3D10BlendState **blend_state, FLOAT blend_factor[4], UINT *sample_mask)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, blend_state %p, blend_factor %p, sample_mask %p.\n",
            iface, blend_state, blend_factor, sample_mask);

    if ((*blend_state = device->blend_state ? (ID3D10BlendState *)&device->blend_state->ID3D10BlendState1_iface : NULL))
        ID3D10BlendState_AddRef(*blend_state);
    wined3d_mutex_lock();
    memcpy(blend_factor, device->blend_factor, 4 * sizeof(*blend_factor));
    *sample_mask = wined3d_device_get_render_state(device->wined3d_device, WINED3D_RS_MULTISAMPLEMASK);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_OMGetDepthStencilState(ID3D10Device1 *iface,
        ID3D10DepthStencilState **depth_stencil_state, UINT *stencil_ref)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, depth_stencil_state %p, stencil_ref %p.\n",
            iface, depth_stencil_state, stencil_ref);

    if ((*depth_stencil_state = device->depth_stencil_state
            ? &device->depth_stencil_state->ID3D10DepthStencilState_iface : NULL))
        ID3D10DepthStencilState_AddRef(*depth_stencil_state);
    *stencil_ref = device->stencil_ref;
}

static void STDMETHODCALLTYPE d3d10_device_SOGetTargets(ID3D10Device1 *iface,
        UINT buffer_count, ID3D10Buffer **buffers, UINT *offsets)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p, buffer_count %u, buffers %p, offsets %p.\n",
            iface, buffer_count, buffers, offsets);

    wined3d_mutex_lock();
    for (i = 0; i < buffer_count; ++i)
    {
        struct wined3d_buffer *wined3d_buffer;
        struct d3d_buffer *buffer_impl;

        if (!(wined3d_buffer = wined3d_device_get_stream_output(device->wined3d_device, i, &offsets[i])))
        {
            buffers[i] = NULL;
            continue;
        }

        buffer_impl = wined3d_buffer_get_parent(wined3d_buffer);
        buffers[i] = &buffer_impl->ID3D10Buffer_iface;
        ID3D10Buffer_AddRef(buffers[i]);
    }
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_RSGetState(ID3D10Device1 *iface, ID3D10RasterizerState **rasterizer_state)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, rasterizer_state %p.\n", iface, rasterizer_state);

    if ((*rasterizer_state = device->rasterizer_state ? &device->rasterizer_state->ID3D10RasterizerState_iface : NULL))
        ID3D10RasterizerState_AddRef(*rasterizer_state);
}

static void STDMETHODCALLTYPE d3d10_device_RSGetViewports(ID3D10Device1 *iface,
        UINT *viewport_count, D3D10_VIEWPORT *viewports)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct wined3d_viewport wined3d_vp;

    TRACE("iface %p, viewport_count %p, viewports %p.\n", iface, viewport_count, viewports);

    if (!viewports)
    {
        *viewport_count = 1;
        return;
    }

    if (!*viewport_count)
        return;

    wined3d_mutex_lock();
    wined3d_device_get_viewport(device->wined3d_device, &wined3d_vp);
    wined3d_mutex_unlock();

    viewports[0].TopLeftX = wined3d_vp.x;
    viewports[0].TopLeftY = wined3d_vp.y;
    viewports[0].Width = wined3d_vp.width;
    viewports[0].Height = wined3d_vp.height;
    viewports[0].MinDepth = wined3d_vp.min_z;
    viewports[0].MaxDepth = wined3d_vp.max_z;

    if (*viewport_count > 1)
        memset(&viewports[1], 0, (*viewport_count - 1) * sizeof(*viewports));
}

static void STDMETHODCALLTYPE d3d10_device_RSGetScissorRects(ID3D10Device1 *iface, UINT *rect_count, D3D10_RECT *rects)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, rect_count %p, rects %p.\n", iface, rect_count, rects);

    if (!rects)
    {
        *rect_count = 1;
        return;
    }

    if (!*rect_count)
        return;

    wined3d_mutex_lock();
    wined3d_device_get_scissor_rect(device->wined3d_device, rects);
    wined3d_mutex_unlock();
    if (*rect_count > 1)
        memset(&rects[1], 0, (*rect_count - 1) * sizeof(*rects));
}

static HRESULT STDMETHODCALLTYPE d3d10_device_GetDeviceRemovedReason(ID3D10Device1 *iface)
{
    TRACE("iface %p.\n", iface);

    /* In the current implementation the device is never removed, so we can
     * just return S_OK here. */

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_SetExceptionMode(ID3D10Device1 *iface, UINT flags)
{
    FIXME("iface %p, flags %#x stub!\n", iface, flags);

    return E_NOTIMPL;
}

static UINT STDMETHODCALLTYPE d3d10_device_GetExceptionMode(ID3D10Device1 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_GetPrivateData(ID3D10Device1 *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d11_device_GetPrivateData(&device->ID3D11Device_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_SetPrivateData(ID3D10Device1 *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d11_device_SetPrivateData(&device->ID3D11Device_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_SetPrivateDataInterface(ID3D10Device1 *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return d3d11_device_SetPrivateDataInterface(&device->ID3D11Device_iface, guid, data);
}

static void STDMETHODCALLTYPE d3d10_device_ClearState(ID3D10Device1 *iface)
{
    static const float blend_factor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    unsigned int i;

    TRACE("iface %p.\n", iface);

    wined3d_mutex_lock();
    wined3d_device_set_vertex_shader(device->wined3d_device, NULL);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        wined3d_device_set_vs_sampler(device->wined3d_device, i, NULL);
    }
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        wined3d_device_set_vs_resource_view(device->wined3d_device, i, NULL);
    }
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        wined3d_device_set_vs_cb(device->wined3d_device, i, NULL);
    }
    wined3d_device_set_geometry_shader(device->wined3d_device, NULL);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        wined3d_device_set_gs_sampler(device->wined3d_device, i, NULL);
    }
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        wined3d_device_set_gs_resource_view(device->wined3d_device, i, NULL);
    }
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        wined3d_device_set_gs_cb(device->wined3d_device, i, NULL);
    }
    wined3d_device_set_pixel_shader(device->wined3d_device, NULL);
    for (i = 0; i < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        wined3d_device_set_ps_sampler(device->wined3d_device, i, NULL);
    }
    for (i = 0; i < D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        wined3d_device_set_ps_resource_view(device->wined3d_device, i, NULL);
    }
    for (i = 0; i < D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
    {
        wined3d_device_set_ps_cb(device->wined3d_device, i, NULL);
    }
    for (i = 0; i < D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        wined3d_device_set_stream_source(device->wined3d_device, i, NULL, 0, 0);
    }
    wined3d_device_set_index_buffer(device->wined3d_device, NULL, WINED3DFMT_UNKNOWN);
    wined3d_device_set_vertex_declaration(device->wined3d_device, NULL);
    wined3d_device_set_primitive_type(device->wined3d_device, WINED3D_PT_UNDEFINED);
    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        wined3d_device_set_rendertarget_view(device->wined3d_device, i, NULL, FALSE);
    }
    wined3d_device_set_depth_stencil_view(device->wined3d_device, NULL);
    ID3D10Device1_OMSetDepthStencilState(iface, NULL, 0);
    ID3D10Device1_OMSetBlendState(iface, NULL, blend_factor, D3D10_DEFAULT_SAMPLE_MASK);
    ID3D10Device1_RSSetViewports(iface, 0, NULL);
    ID3D10Device1_RSSetScissorRects(iface, 0, NULL);
    ID3D10Device1_RSSetState(iface, NULL);
    for (i = 0; i < D3D10_SO_BUFFER_SLOT_COUNT; ++i)
    {
        wined3d_device_set_stream_output(device->wined3d_device, i, NULL, 0);
    }
    wined3d_device_set_predication(device->wined3d_device, NULL, FALSE);
    wined3d_mutex_unlock();
}

static void STDMETHODCALLTYPE d3d10_device_Flush(ID3D10Device1 *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateBuffer(ID3D10Device1 *iface,
        const D3D10_BUFFER_DESC *desc, const D3D10_SUBRESOURCE_DATA *data, ID3D10Buffer **buffer)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    D3D11_BUFFER_DESC d3d11_desc;
    struct d3d_buffer *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, buffer %p.\n", iface, desc, data, buffer);

    d3d11_desc.ByteWidth = desc->ByteWidth;
    d3d11_desc.Usage = d3d11_usage_from_d3d10_usage(desc->Usage);
    d3d11_desc.BindFlags = d3d11_bind_flags_from_d3d10_bind_flags(desc->BindFlags);
    d3d11_desc.CPUAccessFlags = d3d11_cpu_access_flags_from_d3d10_cpu_access_flags(desc->CPUAccessFlags);
    d3d11_desc.MiscFlags = d3d11_resource_misc_flags_from_d3d10_resource_misc_flags(desc->MiscFlags);
    d3d11_desc.StructureByteStride = 0;

    if (FAILED(hr = d3d_buffer_create(device, &d3d11_desc, (const D3D11_SUBRESOURCE_DATA *)data, &object)))
        return hr;

    *buffer = &object->ID3D10Buffer_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateTexture1D(ID3D10Device1 *iface,
        const D3D10_TEXTURE1D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture1D **texture)
{
    FIXME("iface %p, desc %p, data %p, texture %p stub!\n", iface, desc, data, texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateTexture2D(ID3D10Device1 *iface,
        const D3D10_TEXTURE2D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data,
        ID3D10Texture2D **texture)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    D3D11_TEXTURE2D_DESC d3d11_desc;
    struct d3d_texture2d *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, texture %p.\n", iface, desc, data, texture);

    d3d11_desc.Width = desc->Width;
    d3d11_desc.Height = desc->Height;
    d3d11_desc.MipLevels = desc->MipLevels;
    d3d11_desc.ArraySize = desc->ArraySize;
    d3d11_desc.Format = desc->Format;
    d3d11_desc.SampleDesc = desc->SampleDesc;
    d3d11_desc.Usage = d3d11_usage_from_d3d10_usage(desc->Usage);
    d3d11_desc.BindFlags = d3d11_bind_flags_from_d3d10_bind_flags(desc->BindFlags);
    d3d11_desc.CPUAccessFlags = d3d11_cpu_access_flags_from_d3d10_cpu_access_flags(desc->CPUAccessFlags);
    d3d11_desc.MiscFlags = d3d11_resource_misc_flags_from_d3d10_resource_misc_flags(desc->MiscFlags);

    if (FAILED(hr = d3d_texture2d_create(device, &d3d11_desc, (const D3D11_SUBRESOURCE_DATA *)data, &object)))
        return hr;

    *texture = &object->ID3D10Texture2D_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateTexture3D(ID3D10Device1 *iface,
        const D3D10_TEXTURE3D_DESC *desc, const D3D10_SUBRESOURCE_DATA *data,
        ID3D10Texture3D **texture)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    D3D11_TEXTURE3D_DESC d3d11_desc;
    struct d3d_texture3d *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, texture %p.\n", iface, desc, data, texture);

    d3d11_desc.Width = desc->Width;
    d3d11_desc.Height = desc->Height;
    d3d11_desc.Depth = desc->Depth;
    d3d11_desc.MipLevels = desc->MipLevels;
    d3d11_desc.Format = desc->Format;
    d3d11_desc.Usage = d3d11_usage_from_d3d10_usage(desc->Usage);
    d3d11_desc.BindFlags = d3d11_bind_flags_from_d3d10_bind_flags(desc->BindFlags);
    d3d11_desc.CPUAccessFlags = d3d11_cpu_access_flags_from_d3d10_cpu_access_flags(desc->CPUAccessFlags);
    d3d11_desc.MiscFlags = d3d11_resource_misc_flags_from_d3d10_resource_misc_flags(desc->MiscFlags);

    if (FAILED(hr = d3d_texture3d_create(device, &d3d11_desc, (const D3D11_SUBRESOURCE_DATA *)data, &object)))
        return hr;

    *texture = &object->ID3D10Texture3D_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateShaderResourceView1(ID3D10Device1 *iface,
        ID3D10Resource *resource, const D3D10_SHADER_RESOURCE_VIEW_DESC1 *desc, ID3D10ShaderResourceView1 **view)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_shader_resource_view *object;
    ID3D11Resource *d3d11_resource;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    if (FAILED(hr = ID3D10Resource_QueryInterface(resource, &IID_ID3D11Resource, (void **)&d3d11_resource)))
    {
        ERR("Resource does not implement ID3D11Resource.\n");
        return E_FAIL;
    }

    hr = d3d_shader_resource_view_create(device, d3d11_resource, (const D3D11_SHADER_RESOURCE_VIEW_DESC *)desc,
            &object);
    ID3D11Resource_Release(d3d11_resource);
    if (FAILED(hr))
        return hr;

    *view = &object->ID3D10ShaderResourceView1_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateShaderResourceView(ID3D10Device1 *iface,
        ID3D10Resource *resource, const D3D10_SHADER_RESOURCE_VIEW_DESC *desc, ID3D10ShaderResourceView **view)
{
    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    return d3d10_device_CreateShaderResourceView1(iface, resource,
            (const D3D10_SHADER_RESOURCE_VIEW_DESC1 *)desc, (ID3D10ShaderResourceView1 **)view);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateRenderTargetView(ID3D10Device1 *iface,
        ID3D10Resource *resource, const D3D10_RENDER_TARGET_VIEW_DESC *desc, ID3D10RenderTargetView **view)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_rendertarget_view *object;
    ID3D11Resource *d3d11_resource;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    if (FAILED(hr = ID3D10Resource_QueryInterface(resource, &IID_ID3D11Resource, (void **)&d3d11_resource)))
    {
        ERR("Resource does not implement ID3D11Resource.\n");
        return E_FAIL;
    }

    hr = d3d_rendertarget_view_create(device, d3d11_resource, (const D3D11_RENDER_TARGET_VIEW_DESC *)desc, &object);
    ID3D11Resource_Release(d3d11_resource);
    if (FAILED(hr))
        return hr;

    *view = &object->ID3D10RenderTargetView_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateDepthStencilView(ID3D10Device1 *iface,
        ID3D10Resource *resource, const D3D10_DEPTH_STENCIL_VIEW_DESC *desc, ID3D10DepthStencilView **view)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    D3D11_DEPTH_STENCIL_VIEW_DESC d3d11_desc;
    struct d3d_depthstencil_view *object;
    ID3D11Resource *d3d11_resource;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    if (desc)
    {
        d3d11_desc.Format = desc->Format;
        d3d11_desc.ViewDimension = desc->ViewDimension;
        d3d11_desc.Flags = 0;
        memcpy(&d3d11_desc.u, &desc->u, sizeof(d3d11_desc.u));
    }

    if (FAILED(hr = ID3D10Resource_QueryInterface(resource, &IID_ID3D11Resource, (void **)&d3d11_resource)))
    {
        ERR("Resource does not implement ID3D11Resource.\n");
        return E_FAIL;
    }

    hr = d3d_depthstencil_view_create(device, d3d11_resource, desc ? &d3d11_desc : NULL, &object);
    ID3D11Resource_Release(d3d11_resource);
    if (FAILED(hr))
        return hr;

    *view = &object->ID3D10DepthStencilView_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateInputLayout(ID3D10Device1 *iface,
        const D3D10_INPUT_ELEMENT_DESC *element_descs, UINT element_count,
        const void *shader_byte_code, SIZE_T shader_byte_code_length,
        ID3D10InputLayout **input_layout)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_input_layout *object;
    HRESULT hr;

    TRACE("iface %p, element_descs %p, element_count %u, shader_byte_code %p, "
            "shader_byte_code_length %lu, input_layout %p\n",
            iface, element_descs, element_count, shader_byte_code,
            shader_byte_code_length, input_layout);

    if (FAILED(hr = d3d_input_layout_create(device, (const D3D11_INPUT_ELEMENT_DESC *)element_descs, element_count,
            shader_byte_code, shader_byte_code_length, &object)))
        return hr;

    *input_layout = &object->ID3D10InputLayout_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateVertexShader(ID3D10Device1 *iface,
        const void *byte_code, SIZE_T byte_code_length, ID3D10VertexShader **shader)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_vertex_shader *object;
    HRESULT hr;

    TRACE("iface %p, byte_code %p, byte_code_length %lu, shader %p.\n",
            iface, byte_code, byte_code_length, shader);

    if (FAILED(hr = d3d_vertex_shader_create(device, byte_code, byte_code_length, &object)))
        return hr;

    *shader = &object->ID3D10VertexShader_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateGeometryShader(ID3D10Device1 *iface,
        const void *byte_code, SIZE_T byte_code_length, ID3D10GeometryShader **shader)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_geometry_shader *object;
    HRESULT hr;

    TRACE("iface %p, byte_code %p, byte_code_length %lu, shader %p.\n",
            iface, byte_code, byte_code_length, shader);

    if (FAILED(hr = d3d_geometry_shader_create(device, byte_code, byte_code_length, &object)))
        return hr;

    *shader = &object->ID3D10GeometryShader_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateGeometryShaderWithStreamOutput(ID3D10Device1 *iface,
        const void *byte_code, SIZE_T byte_code_length, const D3D10_SO_DECLARATION_ENTRY *output_stream_decls,
        UINT output_stream_decl_count, UINT output_stream_stride, ID3D10GeometryShader **shader)
{
    FIXME("iface %p, byte_code %p, byte_code_length %lu, output_stream_decls %p, "
            "output_stream_decl_count %u, output_stream_stride %u, shader %p stub!\n",
            iface, byte_code, byte_code_length, output_stream_decls,
            output_stream_decl_count, output_stream_stride, shader);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreatePixelShader(ID3D10Device1 *iface,
        const void *byte_code, SIZE_T byte_code_length, ID3D10PixelShader **shader)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_pixel_shader *object;
    HRESULT hr;

    TRACE("iface %p, byte_code %p, byte_code_length %lu, shader %p.\n",
            iface, byte_code, byte_code_length, shader);

    if (FAILED(hr = d3d_pixel_shader_create(device, byte_code, byte_code_length, &object)))
        return hr;

    *shader = &object->ID3D10PixelShader_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateBlendState1(ID3D10Device1 *iface,
        const D3D10_BLEND_DESC1 *desc, ID3D10BlendState1 **blend_state)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    ID3D11BlendState *d3d11_blend_state;
    HRESULT hr;

    TRACE("iface %p, desc %p, blend_state %p.\n", iface, desc, blend_state);

    if (FAILED(hr = d3d11_device_CreateBlendState(&device->ID3D11Device_iface, (D3D11_BLEND_DESC *)desc,
            &d3d11_blend_state)))
        return hr;

    hr = ID3D11BlendState_QueryInterface(d3d11_blend_state, &IID_ID3D10BlendState1, (void **)blend_state);
    ID3D11BlendState_Release(d3d11_blend_state);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateBlendState(ID3D10Device1 *iface,
        const D3D10_BLEND_DESC *desc, ID3D10BlendState **blend_state)
{
    D3D10_BLEND_DESC1 d3d10_1_desc;
    unsigned int i;

    TRACE("iface %p, desc %p, blend_state %p.\n", iface, desc, blend_state);

    if (!desc)
        return E_INVALIDARG;

    d3d10_1_desc.AlphaToCoverageEnable = desc->AlphaToCoverageEnable;
    d3d10_1_desc.IndependentBlendEnable = FALSE;
    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT - 1; ++i)
    {
        if (desc->BlendEnable[i] != desc->BlendEnable[i + 1]
                || desc->RenderTargetWriteMask[i] != desc->RenderTargetWriteMask[i + 1])
            d3d10_1_desc.IndependentBlendEnable = TRUE;
    }

    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        d3d10_1_desc.RenderTarget[i].BlendEnable = desc->BlendEnable[i];
        d3d10_1_desc.RenderTarget[i].SrcBlend = desc->SrcBlend;
        d3d10_1_desc.RenderTarget[i].DestBlend = desc->DestBlend;
        d3d10_1_desc.RenderTarget[i].BlendOp = desc->BlendOp;
        d3d10_1_desc.RenderTarget[i].SrcBlendAlpha = desc->SrcBlendAlpha;
        d3d10_1_desc.RenderTarget[i].DestBlendAlpha = desc->DestBlendAlpha;
        d3d10_1_desc.RenderTarget[i].BlendOpAlpha = desc->BlendOpAlpha;
        d3d10_1_desc.RenderTarget[i].RenderTargetWriteMask = desc->RenderTargetWriteMask[i];
    }

    return d3d10_device_CreateBlendState1(iface, &d3d10_1_desc, (ID3D10BlendState1 **)blend_state);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateDepthStencilState(ID3D10Device1 *iface,
        const D3D10_DEPTH_STENCIL_DESC *desc, ID3D10DepthStencilState **depth_stencil_state)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    ID3D11DepthStencilState *d3d11_depth_stencil_state;
    HRESULT hr;

    TRACE("iface %p, desc %p, depth_stencil_state %p.\n", iface, desc, depth_stencil_state);

    if (FAILED(hr = d3d11_device_CreateDepthStencilState(&device->ID3D11Device_iface,
            (const D3D11_DEPTH_STENCIL_DESC *)desc, &d3d11_depth_stencil_state)))
        return hr;

    hr = ID3D11DepthStencilState_QueryInterface(d3d11_depth_stencil_state, &IID_ID3D10DepthStencilState,
            (void **)depth_stencil_state);
    ID3D11DepthStencilState_Release(d3d11_depth_stencil_state);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateRasterizerState(ID3D10Device1 *iface,
        const D3D10_RASTERIZER_DESC *desc, ID3D10RasterizerState **rasterizer_state)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    ID3D11RasterizerState *d3d11_rasterizer_state;
    HRESULT hr;

    TRACE("iface %p, desc %p, rasterizer_state %p.\n", iface, desc, rasterizer_state);

    if (FAILED(hr = d3d11_device_CreateRasterizerState(&device->ID3D11Device_iface,
            (const D3D11_RASTERIZER_DESC *)desc, &d3d11_rasterizer_state)))
        return hr;

    hr = ID3D11RasterizerState_QueryInterface(d3d11_rasterizer_state,
            &IID_ID3D10RasterizerState, (void **)rasterizer_state);
    ID3D11RasterizerState_Release(d3d11_rasterizer_state);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateSamplerState(ID3D10Device1 *iface,
        const D3D10_SAMPLER_DESC *desc, ID3D10SamplerState **sampler_state)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    ID3D11SamplerState *d3d11_sampler_state;
    HRESULT hr;

    TRACE("iface %p, desc %p, sampler_state %p.\n", iface, desc, sampler_state);

    if (FAILED(hr = d3d11_device_CreateSamplerState(&device->ID3D11Device_iface,
            (const D3D11_SAMPLER_DESC *)desc, &d3d11_sampler_state)))
        return hr;

    hr = ID3D11SamplerState_QueryInterface(d3d11_sampler_state, &IID_ID3D10SamplerState, (void **)sampler_state);
    ID3D11SamplerState_Release(d3d11_sampler_state);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateQuery(ID3D10Device1 *iface,
        const D3D10_QUERY_DESC *desc, ID3D10Query **query)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_query *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, query %p.\n", iface, desc, query);

    if (FAILED(hr = d3d_query_create(device, (const D3D11_QUERY_DESC *)desc, FALSE, &object)))
        return hr;

    *query = &object->ID3D10Query_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreatePredicate(ID3D10Device1 *iface,
        const D3D10_QUERY_DESC *desc, ID3D10Predicate **predicate)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);
    struct d3d_query *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, predicate %p.\n", iface, desc, predicate);

    if (FAILED(hr = d3d_query_create(device, (const D3D11_QUERY_DESC *)desc, TRUE, &object)))
        return hr;

    *predicate = (ID3D10Predicate *)&object->ID3D10Query_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CreateCounter(ID3D10Device1 *iface,
        const D3D10_COUNTER_DESC *desc, ID3D10Counter **counter)
{
    FIXME("iface %p, desc %p, counter %p stub!\n", iface, desc, counter);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CheckFormatSupport(ID3D10Device1 *iface,
        DXGI_FORMAT format, UINT *format_support)
{
    FIXME("iface %p, format %s, format_support %p stub!\n",
            iface, debug_dxgi_format(format), format_support);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CheckMultisampleQualityLevels(ID3D10Device1 *iface,
        DXGI_FORMAT format, UINT sample_count, UINT *quality_level_count)
{
    struct d3d_device *device = impl_from_ID3D10Device(iface);

    TRACE("iface %p, format %s, sample_count %u, quality_level_count %p.\n",
            iface, debug_dxgi_format(format), sample_count, quality_level_count);

    return d3d11_device_CheckMultisampleQualityLevels(&device->ID3D11Device_iface, format,
            sample_count, quality_level_count);
}

static void STDMETHODCALLTYPE d3d10_device_CheckCounterInfo(ID3D10Device1 *iface, D3D10_COUNTER_INFO *counter_info)
{
    FIXME("iface %p, counter_info %p stub!\n", iface, counter_info);
}

static HRESULT STDMETHODCALLTYPE d3d10_device_CheckCounter(ID3D10Device1 *iface,
        const D3D10_COUNTER_DESC *desc, D3D10_COUNTER_TYPE *type, UINT *active_counters, char *name,
        UINT *name_length, char *units, UINT *units_length, char *description, UINT *description_length)
{
    FIXME("iface %p, desc %p, type %p, active_counters %p, name %p, name_length %p, "
            "units %p, units_length %p, description %p, description_length %p stub!\n",
            iface, desc, type, active_counters, name, name_length,
            units, units_length, description, description_length);

    return E_NOTIMPL;
}

static UINT STDMETHODCALLTYPE d3d10_device_GetCreationFlags(ID3D10Device1 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d10_device_OpenSharedResource(ID3D10Device1 *iface,
        HANDLE resource_handle, REFIID guid, void **resource)
{
    FIXME("iface %p, resource_handle %p, guid %s, resource %p stub!\n",
            iface, resource_handle, debugstr_guid(guid), resource);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d10_device_SetTextFilterSize(ID3D10Device1 *iface, UINT width, UINT height)
{
    FIXME("iface %p, width %u, height %u stub!\n", iface, width, height);
}

static void STDMETHODCALLTYPE d3d10_device_GetTextFilterSize(ID3D10Device1 *iface, UINT *width, UINT *height)
{
    FIXME("iface %p, width %p, height %p stub!\n", iface, width, height);
}

static D3D10_FEATURE_LEVEL1 STDMETHODCALLTYPE d3d10_device_GetFeatureLevel(ID3D10Device1 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return D3D10_FEATURE_LEVEL_10_1;
}

static const struct ID3D10Device1Vtbl d3d10_device1_vtbl =
{
    /* IUnknown methods */
    d3d10_device_QueryInterface,
    d3d10_device_AddRef,
    d3d10_device_Release,
    /* ID3D10Device methods */
    d3d10_device_VSSetConstantBuffers,
    d3d10_device_PSSetShaderResources,
    d3d10_device_PSSetShader,
    d3d10_device_PSSetSamplers,
    d3d10_device_VSSetShader,
    d3d10_device_DrawIndexed,
    d3d10_device_Draw,
    d3d10_device_PSSetConstantBuffers,
    d3d10_device_IASetInputLayout,
    d3d10_device_IASetVertexBuffers,
    d3d10_device_IASetIndexBuffer,
    d3d10_device_DrawIndexedInstanced,
    d3d10_device_DrawInstanced,
    d3d10_device_GSSetConstantBuffers,
    d3d10_device_GSSetShader,
    d3d10_device_IASetPrimitiveTopology,
    d3d10_device_VSSetShaderResources,
    d3d10_device_VSSetSamplers,
    d3d10_device_SetPredication,
    d3d10_device_GSSetShaderResources,
    d3d10_device_GSSetSamplers,
    d3d10_device_OMSetRenderTargets,
    d3d10_device_OMSetBlendState,
    d3d10_device_OMSetDepthStencilState,
    d3d10_device_SOSetTargets,
    d3d10_device_DrawAuto,
    d3d10_device_RSSetState,
    d3d10_device_RSSetViewports,
    d3d10_device_RSSetScissorRects,
    d3d10_device_CopySubresourceRegion,
    d3d10_device_CopyResource,
    d3d10_device_UpdateSubresource,
    d3d10_device_ClearRenderTargetView,
    d3d10_device_ClearDepthStencilView,
    d3d10_device_GenerateMips,
    d3d10_device_ResolveSubresource,
    d3d10_device_VSGetConstantBuffers,
    d3d10_device_PSGetShaderResources,
    d3d10_device_PSGetShader,
    d3d10_device_PSGetSamplers,
    d3d10_device_VSGetShader,
    d3d10_device_PSGetConstantBuffers,
    d3d10_device_IAGetInputLayout,
    d3d10_device_IAGetVertexBuffers,
    d3d10_device_IAGetIndexBuffer,
    d3d10_device_GSGetConstantBuffers,
    d3d10_device_GSGetShader,
    d3d10_device_IAGetPrimitiveTopology,
    d3d10_device_VSGetShaderResources,
    d3d10_device_VSGetSamplers,
    d3d10_device_GetPredication,
    d3d10_device_GSGetShaderResources,
    d3d10_device_GSGetSamplers,
    d3d10_device_OMGetRenderTargets,
    d3d10_device_OMGetBlendState,
    d3d10_device_OMGetDepthStencilState,
    d3d10_device_SOGetTargets,
    d3d10_device_RSGetState,
    d3d10_device_RSGetViewports,
    d3d10_device_RSGetScissorRects,
    d3d10_device_GetDeviceRemovedReason,
    d3d10_device_SetExceptionMode,
    d3d10_device_GetExceptionMode,
    d3d10_device_GetPrivateData,
    d3d10_device_SetPrivateData,
    d3d10_device_SetPrivateDataInterface,
    d3d10_device_ClearState,
    d3d10_device_Flush,
    d3d10_device_CreateBuffer,
    d3d10_device_CreateTexture1D,
    d3d10_device_CreateTexture2D,
    d3d10_device_CreateTexture3D,
    d3d10_device_CreateShaderResourceView,
    d3d10_device_CreateRenderTargetView,
    d3d10_device_CreateDepthStencilView,
    d3d10_device_CreateInputLayout,
    d3d10_device_CreateVertexShader,
    d3d10_device_CreateGeometryShader,
    d3d10_device_CreateGeometryShaderWithStreamOutput,
    d3d10_device_CreatePixelShader,
    d3d10_device_CreateBlendState,
    d3d10_device_CreateDepthStencilState,
    d3d10_device_CreateRasterizerState,
    d3d10_device_CreateSamplerState,
    d3d10_device_CreateQuery,
    d3d10_device_CreatePredicate,
    d3d10_device_CreateCounter,
    d3d10_device_CheckFormatSupport,
    d3d10_device_CheckMultisampleQualityLevels,
    d3d10_device_CheckCounterInfo,
    d3d10_device_CheckCounter,
    d3d10_device_GetCreationFlags,
    d3d10_device_OpenSharedResource,
    d3d10_device_SetTextFilterSize,
    d3d10_device_GetTextFilterSize,
    d3d10_device_CreateShaderResourceView1,
    d3d10_device_CreateBlendState1,
    d3d10_device_GetFeatureLevel,
};

static const struct IUnknownVtbl d3d_device_inner_unknown_vtbl =
{
    /* IUnknown methods */
    d3d_device_inner_QueryInterface,
    d3d_device_inner_AddRef,
    d3d_device_inner_Release,
};

/* ID3D10Multithread methods */

static inline struct d3d_device *impl_from_ID3D10Multithread(ID3D10Multithread *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_device, ID3D10Multithread_iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_multithread_QueryInterface(ID3D10Multithread *iface, REFIID iid, void **out)
{
    struct d3d_device *device = impl_from_ID3D10Multithread(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return IUnknown_QueryInterface(device->outer_unk, iid, out);
}

static ULONG STDMETHODCALLTYPE d3d10_multithread_AddRef(ID3D10Multithread *iface)
{
    struct d3d_device *device = impl_from_ID3D10Multithread(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_AddRef(device->outer_unk);
}

static ULONG STDMETHODCALLTYPE d3d10_multithread_Release(ID3D10Multithread *iface)
{
    struct d3d_device *device = impl_from_ID3D10Multithread(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_Release(device->outer_unk);
}

static void STDMETHODCALLTYPE d3d10_multithread_Enter(ID3D10Multithread *iface)
{
    TRACE("iface %p.\n", iface);

    wined3d_mutex_lock();
}

static void STDMETHODCALLTYPE d3d10_multithread_Leave(ID3D10Multithread *iface)
{
    TRACE("iface %p.\n", iface);

    wined3d_mutex_unlock();
}

static BOOL STDMETHODCALLTYPE d3d10_multithread_SetMultithreadProtected(ID3D10Multithread *iface, BOOL protect)
{
    FIXME("iface %p, protect %#x stub!\n", iface, protect);

    return TRUE;
}

static BOOL STDMETHODCALLTYPE d3d10_multithread_GetMultithreadProtected(ID3D10Multithread *iface)
{
    FIXME("iface %p stub!\n", iface);

    return TRUE;
}

static const struct ID3D10MultithreadVtbl d3d10_multithread_vtbl =
{
    d3d10_multithread_QueryInterface,
    d3d10_multithread_AddRef,
    d3d10_multithread_Release,
    d3d10_multithread_Enter,
    d3d10_multithread_Leave,
    d3d10_multithread_SetMultithreadProtected,
    d3d10_multithread_GetMultithreadProtected,
};

/* IWineDXGIDeviceParent IUnknown methods */

static inline struct d3d_device *device_from_dxgi_device_parent(IWineDXGIDeviceParent *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_device, IWineDXGIDeviceParent_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_device_parent_QueryInterface(IWineDXGIDeviceParent *iface,
        REFIID riid, void **ppv)
{
    struct d3d_device *device = device_from_dxgi_device_parent(iface);
    return IUnknown_QueryInterface(device->outer_unk, riid, ppv);
}

static ULONG STDMETHODCALLTYPE dxgi_device_parent_AddRef(IWineDXGIDeviceParent *iface)
{
    struct d3d_device *device = device_from_dxgi_device_parent(iface);
    return IUnknown_AddRef(device->outer_unk);
}

static ULONG STDMETHODCALLTYPE dxgi_device_parent_Release(IWineDXGIDeviceParent *iface)
{
    struct d3d_device *device = device_from_dxgi_device_parent(iface);
    return IUnknown_Release(device->outer_unk);
}

static struct wined3d_device_parent * STDMETHODCALLTYPE dxgi_device_parent_get_wined3d_device_parent(
        IWineDXGIDeviceParent *iface)
{
    struct d3d_device *device = device_from_dxgi_device_parent(iface);
    return &device->device_parent;
}

static const struct IWineDXGIDeviceParentVtbl d3d_dxgi_device_parent_vtbl =
{
    /* IUnknown methods */
    dxgi_device_parent_QueryInterface,
    dxgi_device_parent_AddRef,
    dxgi_device_parent_Release,
    /* IWineDXGIDeviceParent methods */
    dxgi_device_parent_get_wined3d_device_parent,
};

static inline struct d3d_device *device_from_wined3d_device_parent(struct wined3d_device_parent *device_parent)
{
    return CONTAINING_RECORD(device_parent, struct d3d_device, device_parent);
}

static void CDECL device_parent_wined3d_device_created(struct wined3d_device_parent *device_parent,
        struct wined3d_device *wined3d_device)
{
    struct d3d_device *device = device_from_wined3d_device_parent(device_parent);

    TRACE("device_parent %p, wined3d_device %p.\n", device_parent, wined3d_device);

    wined3d_device_incref(wined3d_device);
    device->wined3d_device = wined3d_device;
}

static void CDECL device_parent_mode_changed(struct wined3d_device_parent *device_parent)
{
    TRACE("device_parent %p.\n", device_parent);
}

static void CDECL device_parent_activate(struct wined3d_device_parent *device_parent, BOOL activate)
{
    TRACE("device_parent %p, activate %#x.\n", device_parent, activate);
}

static HRESULT CDECL device_parent_sub_resource_created(struct wined3d_device_parent *device_parent,
        struct wined3d_texture *wined3d_texture, unsigned int sub_resource_idx, void **parent,
        const struct wined3d_parent_ops **parent_ops)
{
    TRACE("device_parent %p, wined3d_texture %p, sub_resource_idx %u, parent %p, parent_ops %p.\n",
            device_parent, wined3d_texture, sub_resource_idx, parent, parent_ops);

    *parent = NULL;
    *parent_ops = &d3d_null_wined3d_parent_ops;

    return S_OK;
}

static HRESULT CDECL device_parent_create_swapchain_texture(struct wined3d_device_parent *device_parent,
        void *container_parent, const struct wined3d_resource_desc *wined3d_desc,
        struct wined3d_texture **wined3d_texture)
{
    struct d3d_device *device = device_from_wined3d_device_parent(device_parent);
    struct d3d_texture2d *texture;
    ID3D10Texture2D *texture_iface;
    D3D10_TEXTURE2D_DESC desc;
    HRESULT hr;

    FIXME("device_parent %p, container_parent %p, wined3d_desc %p, wined3d_texture %p partial stub!\n",
            device_parent, container_parent, wined3d_desc, wined3d_texture);

    FIXME("Implement DXGI<->wined3d usage conversion\n");

    desc.Width = wined3d_desc->width;
    desc.Height = wined3d_desc->height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = dxgi_format_from_wined3dformat(wined3d_desc->format);
    desc.SampleDesc.Count = wined3d_desc->multisample_type ? wined3d_desc->multisample_type : 1;
    desc.SampleDesc.Quality = wined3d_desc->multisample_quality;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    if (FAILED(hr = d3d10_device_CreateTexture2D(&device->ID3D10Device1_iface,
            &desc, NULL, &texture_iface)))
    {
        ERR("CreateTexture2D failed, returning %#x\n", hr);
        return hr;
    }

    texture = impl_from_ID3D10Texture2D(texture_iface);

    *wined3d_texture = texture->wined3d_texture;
    wined3d_texture_incref(*wined3d_texture);
    ID3D10Texture2D_Release(&texture->ID3D10Texture2D_iface);

    return S_OK;
}

static HRESULT CDECL device_parent_create_swapchain(struct wined3d_device_parent *device_parent,
        struct wined3d_swapchain_desc *desc, struct wined3d_swapchain **swapchain)
{
    struct d3d_device *device = device_from_wined3d_device_parent(device_parent);
    IWineDXGIDevice *wine_device;
    HRESULT hr;

    TRACE("device_parent %p, desc %p, swapchain %p.\n", device_parent, desc, swapchain);

    if (FAILED(hr = d3d10_device_QueryInterface(&device->ID3D10Device1_iface,
            &IID_IWineDXGIDevice, (void **)&wine_device)))
    {
        ERR("Device should implement IWineDXGIDevice.\n");
        return E_FAIL;
    }

    hr = IWineDXGIDevice_create_swapchain(wine_device, desc, swapchain);
    IWineDXGIDevice_Release(wine_device);
    if (FAILED(hr))
    {
        ERR("Failed to create DXGI swapchain, returning %#x\n", hr);
        return hr;
    }

    return S_OK;
}

static const struct wined3d_device_parent_ops d3d_wined3d_device_parent_ops =
{
    device_parent_wined3d_device_created,
    device_parent_mode_changed,
    device_parent_activate,
    device_parent_sub_resource_created,
    device_parent_sub_resource_created,
    device_parent_create_swapchain_texture,
    device_parent_create_swapchain,
};

static void *d3d_rb_alloc(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void *d3d_rb_realloc(void *ptr, size_t size)
{
    return HeapReAlloc(GetProcessHeap(), 0, ptr, size);
}

static void d3d_rb_free(void *ptr)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}

static int d3d_sampler_state_compare(const void *key, const struct wine_rb_entry *entry)
{
    const D3D11_SAMPLER_DESC *ka = key;
    const D3D11_SAMPLER_DESC *kb = &WINE_RB_ENTRY_VALUE(entry, const struct d3d_sampler_state, entry)->desc;

    return memcmp(ka, kb, sizeof(*ka));
}

static const struct wine_rb_functions d3d_sampler_state_rb_ops =
{
    d3d_rb_alloc,
    d3d_rb_realloc,
    d3d_rb_free,
    d3d_sampler_state_compare,
};

static int d3d_blend_state_compare(const void *key, const struct wine_rb_entry *entry)
{
    const D3D11_BLEND_DESC *ka = key;
    const D3D11_BLEND_DESC *kb = &WINE_RB_ENTRY_VALUE(entry, const struct d3d_blend_state, entry)->desc;

    return memcmp(ka, kb, sizeof(*ka));
}

static const struct wine_rb_functions d3d_blend_state_rb_ops =
{
    d3d_rb_alloc,
    d3d_rb_realloc,
    d3d_rb_free,
    d3d_blend_state_compare,
};

static int d3d_depthstencil_state_compare(const void *key, const struct wine_rb_entry *entry)
{
    const D3D11_DEPTH_STENCIL_DESC *ka = key;
    const D3D11_DEPTH_STENCIL_DESC *kb = &WINE_RB_ENTRY_VALUE(entry,
            const struct d3d_depthstencil_state, entry)->desc;

    return memcmp(ka, kb, sizeof(*ka));
}

static const struct wine_rb_functions d3d_depthstencil_state_rb_ops =
{
    d3d_rb_alloc,
    d3d_rb_realloc,
    d3d_rb_free,
    d3d_depthstencil_state_compare,
};

static int d3d_rasterizer_state_compare(const void *key, const struct wine_rb_entry *entry)
{
    const D3D11_RASTERIZER_DESC *ka = key;
    const D3D11_RASTERIZER_DESC *kb = &WINE_RB_ENTRY_VALUE(entry, const struct d3d_rasterizer_state, entry)->desc;

    return memcmp(ka, kb, sizeof(*ka));
}

static const struct wine_rb_functions d3d_rasterizer_state_rb_ops =
{
    d3d_rb_alloc,
    d3d_rb_realloc,
    d3d_rb_free,
    d3d_rasterizer_state_compare,
};

HRESULT d3d_device_init(struct d3d_device *device, void *outer_unknown)
{
    device->IUnknown_inner.lpVtbl = &d3d_device_inner_unknown_vtbl;
    device->ID3D11Device_iface.lpVtbl = &d3d11_device_vtbl;
    device->ID3D10Device1_iface.lpVtbl = &d3d10_device1_vtbl;
    device->ID3D10Multithread_iface.lpVtbl = &d3d10_multithread_vtbl;
    device->IWineDXGIDeviceParent_iface.lpVtbl = &d3d_dxgi_device_parent_vtbl;
    device->device_parent.ops = &d3d_wined3d_device_parent_ops;
    device->refcount = 1;
    /* COM aggregation always takes place */
    device->outer_unk = outer_unknown;

    if (FAILED(d3d11_immediate_context_init(&device->immediate_context, device)))
    {
        WARN("Failed to initialize immediate device context.\n");
        return E_FAIL;
    }
    ID3D11DeviceContext_Release(&device->immediate_context.ID3D11DeviceContext_iface);

    if (wine_rb_init(&device->blend_states, &d3d_blend_state_rb_ops) == -1)
    {
        WARN("Failed to initialize blend state rbtree.\n");
        return E_FAIL;
    }
    device->blend_factor[0] = 1.0f;
    device->blend_factor[1] = 1.0f;
    device->blend_factor[2] = 1.0f;
    device->blend_factor[3] = 1.0f;

    if (wine_rb_init(&device->depthstencil_states, &d3d_depthstencil_state_rb_ops) == -1)
    {
        WARN("Failed to initialize depthstencil state rbtree.\n");
        wine_rb_destroy(&device->blend_states, NULL, NULL);
        return E_FAIL;
    }

    if (wine_rb_init(&device->rasterizer_states, &d3d_rasterizer_state_rb_ops) == -1)
    {
        WARN("Failed to initialize rasterizer state rbtree.\n");
        wine_rb_destroy(&device->depthstencil_states, NULL, NULL);
        wine_rb_destroy(&device->blend_states, NULL, NULL);
        return E_FAIL;
    }

    if (wine_rb_init(&device->sampler_states, &d3d_sampler_state_rb_ops) == -1)
    {
        WARN("Failed to initialize sampler state rbtree.\n");
        wine_rb_destroy(&device->rasterizer_states, NULL, NULL);
        wine_rb_destroy(&device->depthstencil_states, NULL, NULL);
        wine_rb_destroy(&device->blend_states, NULL, NULL);
        return E_FAIL;
    }

    return S_OK;
}

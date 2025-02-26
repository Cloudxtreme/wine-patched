/*
 * Xinerama support
 *
 * Copyright 2006 Alexandre Julliard
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

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#ifdef HAVE_X11_EXTENSIONS_XINERAMA_H
#include <X11/extensions/Xinerama.h>
#endif
#include "wine/library.h"
#include "x11drv.h"
#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(x11drv);

static RECT virtual_screen_rect;

static MONITORINFOEXW default_monitor =
{
    sizeof(default_monitor),    /* cbSize */
    { 0, 0, 0, 0 },             /* rcMonitor */
    { 0, 0, 0, 0 },             /* rcWork */
    MONITORINFOF_PRIMARY,       /* dwFlags */
    { '\\','\\','.','\\','D','I','S','P','L','A','Y','1',0 }   /* szDevice */
};
static const WCHAR monitor_deviceW[] = { '\\','\\','.','\\','D','I','S','P','L','A','Y','%','d',0 };

static MONITORINFOEXW *monitors;
static int nb_monitors;

static inline MONITORINFOEXW *get_primary(void)
{
    /* default to 0 if specified primary is invalid */
    int idx = primary_monitor;
    if (idx >= nb_monitors) idx = 0;
    return &monitors[idx];
}

static inline HMONITOR index_to_monitor( int index )
{
    return (HMONITOR)(UINT_PTR)(index + 1);
}

static inline int monitor_to_index( HMONITOR handle )
{
    UINT_PTR index = (UINT_PTR)handle;
    if (index < 1 || index > nb_monitors) return -1;
    return index - 1;
}

static void query_work_area( RECT *rc_work )
{
    Atom type;
    int format;
    unsigned long count, remaining;
    long *work_area;

    if (!XGetWindowProperty( gdi_display, DefaultRootWindow(gdi_display), x11drv_atom(_NET_WORKAREA), 0,
                             ~0, False, XA_CARDINAL, &type, &format, &count,
                             &remaining, (unsigned char **)&work_area ))
    {
        if (type == XA_CARDINAL && format == 32 && count >= 4)
        {
            SetRect( rc_work, work_area[0], work_area[1],
                     work_area[0] + work_area[2], work_area[1] + work_area[3] );
        }
        XFree( work_area );
    }
}

#ifdef SONAME_LIBXINERAMA

#define MAKE_FUNCPTR(f) static typeof(f) * p##f

MAKE_FUNCPTR(XineramaQueryExtension);
MAKE_FUNCPTR(XineramaQueryScreens);

static void load_xinerama(void)
{
    void *handle;

    if (!(handle = wine_dlopen(SONAME_LIBXINERAMA, RTLD_NOW, NULL, 0)))
    {
        WARN( "failed to open %s\n", SONAME_LIBXINERAMA );
        return;
    }
    pXineramaQueryExtension = wine_dlsym( handle, "XineramaQueryExtension", NULL, 0 );
    if (!pXineramaQueryExtension) WARN( "XineramaQueryScreens not found\n" );
    pXineramaQueryScreens = wine_dlsym( handle, "XineramaQueryScreens", NULL, 0 );
    if (!pXineramaQueryScreens) WARN( "XineramaQueryScreens not found\n" );
}

static int query_screens(void)
{
    int i, count, event_base, error_base;
    XineramaScreenInfo *screens;
    RECT rc_work = {0, 0, 0, 0};

    if (!monitors)  /* first time around */
        load_xinerama();

    query_work_area( &rc_work );

    if (!pXineramaQueryExtension || !pXineramaQueryScreens ||
        !pXineramaQueryExtension( gdi_display, &event_base, &error_base ) ||
        !(screens = pXineramaQueryScreens( gdi_display, &count ))) return 0;

    if (monitors != &default_monitor) HeapFree( GetProcessHeap(), 0, monitors );
    if ((monitors = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*monitors) )))
    {
        int device = 2; /* 1 is reserved for primary */

        nb_monitors = count;
        for (i = 0; i < nb_monitors; i++)
        {
            monitors[i].cbSize = sizeof( monitors[i] );
            monitors[i].rcMonitor.left   = screens[i].x_org;
            monitors[i].rcMonitor.top    = screens[i].y_org;
            monitors[i].rcMonitor.right  = screens[i].x_org + screens[i].width;
            monitors[i].rcMonitor.bottom = screens[i].y_org + screens[i].height;
            monitors[i].dwFlags          = 0;
            if (!IntersectRect( &monitors[i].rcWork, &rc_work, &monitors[i].rcMonitor ))
                monitors[i].rcWork = monitors[i].rcMonitor;
        }

        get_primary()->dwFlags |= MONITORINFOF_PRIMARY;

        for (i = 0; i < nb_monitors; i++)
        {
            snprintfW( monitors[i].szDevice, sizeof(monitors[i].szDevice) / sizeof(WCHAR),
                       monitor_deviceW, (monitors[i].dwFlags & MONITORINFOF_PRIMARY) ? 1 : device++ );
        }
    }
    else count = 0;

    XFree( screens );
    return count;
}

#else  /* SONAME_LIBXINERAMA */

static inline int query_screens(void)
{
    return 0;
}

#endif  /* SONAME_LIBXINERAMA */

POINT virtual_screen_to_root( INT x, INT y )
{
    POINT pt;
    pt.x = x - virtual_screen_rect.left;
    pt.y = y - virtual_screen_rect.top;
    return pt;
}

POINT root_to_virtual_screen( INT x, INT y )
{
    POINT pt;
    pt.x = x + virtual_screen_rect.left;
    pt.y = y + virtual_screen_rect.top;
    return pt;
}

RECT get_virtual_screen_rect(void)
{
    return virtual_screen_rect;
}

RECT get_primary_monitor_rect(void)
{
    return get_primary()->rcMonitor;
}

void xinerama_init( unsigned int width, unsigned int height )
{
    MONITORINFOEXW *primary;
    int i;
    RECT rect;

    SetRect( &rect, 0, 0, width, height );

    if (root_window != DefaultRootWindow( gdi_display ) || !query_screens())
    {
        default_monitor.rcWork = default_monitor.rcMonitor = rect;
        if (root_window == DefaultRootWindow( gdi_display ))
            query_work_area( &default_monitor.rcWork );
        nb_monitors = 1;
        monitors = &default_monitor;
    }

    primary = get_primary();
    SetRectEmpty( &virtual_screen_rect );

    /* coordinates (0,0) have to point to the primary monitor origin */
    OffsetRect( &rect, -primary->rcMonitor.left, -primary->rcMonitor.top );
    for (i = 0; i < nb_monitors; i++)
    {
        OffsetRect( &monitors[i].rcMonitor, rect.left, rect.top );
        OffsetRect( &monitors[i].rcWork, rect.left, rect.top );
        UnionRect( &virtual_screen_rect, &virtual_screen_rect, &monitors[i].rcMonitor );
        TRACE( "monitor %p: %s work %s%s\n",
               index_to_monitor(i), wine_dbgstr_rect(&monitors[i].rcMonitor),
               wine_dbgstr_rect(&monitors[i].rcWork),
               (monitors[i].dwFlags & MONITORINFOF_PRIMARY) ? " (primary)" : "" );
    }

    TRACE( "virtual size: %s primary: %s\n",
           wine_dbgstr_rect(&virtual_screen_rect), wine_dbgstr_rect(&primary->rcMonitor) );
}


/***********************************************************************
 *		X11DRV_GetMonitorInfo  (X11DRV.@)
 */
BOOL CDECL X11DRV_GetMonitorInfo( HMONITOR handle, LPMONITORINFO info )
{
    int i = monitor_to_index( handle );

    if (i == -1)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    info->rcMonitor = monitors[i].rcMonitor;
    info->rcWork = monitors[i].rcWork;
    info->dwFlags = monitors[i].dwFlags;
    if (info->cbSize >= sizeof(MONITORINFOEXW))
        lstrcpyW( ((MONITORINFOEXW *)info)->szDevice, monitors[i].szDevice );
    return TRUE;
}

#ifdef __i386__
/* MJ's Help Diagnostic expects that %ecx contains the address to rect,
 * so we need a small assembly wrapper to call the proc. */
extern BOOL enum_monitor_wrapper( void *callback, HMONITOR monitor, HDC hdc, RECT *rect, LPARAM data );
__ASM_GLOBAL_FUNC( enum_monitor_wrapper,
    "pushl %ebp\n\t"
    __ASM_CFI(".cfi_adjust_cfa_offset 4\n\t")
    __ASM_CFI(".cfi_rel_offset %ebp,0\n\t")
    "movl %esp,%ebp\n\t"
    __ASM_CFI(".cfi_def_cfa_register %ebp\n\t")
    "subl $8,%esp\n\t"
    "pushl 24(%ebp)\n\t"
    "pushl 20(%ebp)\n\t"
    "pushl 16(%ebp)\n\t"
    "pushl 12(%ebp)\n\t"
    "movl 20(%ebp),%ecx\n\t"
    "call *8(%ebp)\n\t"
    "leave\n\t"
    __ASM_CFI(".cfi_def_cfa %esp,4\n\t")
    __ASM_CFI(".cfi_same_value %ebp\n\t")
    "ret" )
#else
#define enum_monitor_wrapper( callback, monitor, hdc, rect, data ) (callback)( (monitor), (hdc), (rect), (data) )
#endif

/***********************************************************************
 *		X11DRV_EnumDisplayMonitors  (X11DRV.@)
 */
BOOL CDECL X11DRV_EnumDisplayMonitors( HDC hdc, LPRECT rect, MONITORENUMPROC proc, LPARAM lp )
{
    int i;

    if (hdc)
    {
        POINT origin;
        RECT limit;

        if (!GetDCOrgEx( hdc, &origin )) return FALSE;
        if (GetClipBox( hdc, &limit ) == ERROR) return FALSE;

        if (rect && !IntersectRect( &limit, &limit, rect )) return TRUE;

        for (i = 0; i < nb_monitors; i++)
        {
            RECT monrect = monitors[i].rcMonitor;
            OffsetRect( &monrect, -origin.x, -origin.y );
            if (IntersectRect( &monrect, &monrect, &limit ))
                if (!enum_monitor_wrapper( proc, index_to_monitor(i), hdc, &monrect, lp ))
                    return FALSE;
        }
    }
    else
    {
        for (i = 0; i < nb_monitors; i++)
        {
            RECT unused;
            if (!rect || IntersectRect( &unused, &monitors[i].rcMonitor, rect ))
                if (!enum_monitor_wrapper( proc, index_to_monitor(i), 0, &monitors[i].rcMonitor, lp ))
                    return FALSE;
        }
    }
    return TRUE;
}

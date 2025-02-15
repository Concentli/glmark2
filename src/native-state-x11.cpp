/*
 * Copyright © 2010-2011 Linaro Limited
 * Copyright © 2013 Canonical Ltd
 *
 * This file is part of the glmark2 OpenGL (ES) 2.0 benchmark.
 *
 * glmark2 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * glmark2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * glmark2.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *  Alexandros Frantzis
 */
#include "native-state-x11.h"
#include "log.h"
#include "options.h"
#include "util.h"

#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#include <X11/extensions/Xrandr.h>

/******************
 * Public methods *
 ******************/

namespace
{

std::string const x11_position_opt{"position"};

std::string get_x11_position_option()
{
    std::string position{""};

    for (auto const& opt : Options::winsys_options)
    {
        if (opt.name == x11_position_opt)
            position = opt.value;
    }

    return position;
}

std::pair<int,int> parse_pos(const std::string &str)
{
    std::pair<int,int> pos {0, 0};
    std::vector<std::string> d;
    Util::split(str, ',', d, Util::SplitModeNormal);

    if (d.size() > 1) {
        pos.first = Util::fromString<int>(d[0]);
        pos.second = Util::fromString<int>(d[1]);
    }

    return pos;
}

}

NativeStateX11::NativeStateX11() :
    xdpy_(0),
    xwin_(0),
    properties_()
{
    Options::winsys_options_help =
       "  position=x,y  position of the output window on screen\n";
}

NativeStateX11::~NativeStateX11()
{
    if (xdpy_)
    {
        if (xwin_)
            XDestroyWindow(xdpy_, xwin_);

        XCloseDisplay(xdpy_);
    }
}

bool
NativeStateX11::init_display()
{
    if (!xdpy_)
        xdpy_ = XOpenDisplay(NULL);

    return (xdpy_ != 0);
}

void*
NativeStateX11::display()
{
    return (void*)xdpy_;
}

static bool
get_main_screen_resolution(Display* display, int& width, int& height)
{
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    XRRScreenResources* screen_resources = XRRGetScreenResources(display, root);
    if (!screen_resources) {
        Log::error("Error: Unable to get screen resources.\n");
        return false;
    }

    RROutput primary_output = XRRGetOutputPrimary(display, root);
    if (primary_output == None) {
        Log::error("Error: Unable to get primary output.\n");
        XRRFreeScreenResources(screen_resources);
        return false;
    }

    XRROutputInfo* output_info = XRRGetOutputInfo(display, screen_resources, primary_output);
    if (!output_info) {
        Log::error("Error: Unable to get output info for primary output.\n");
        XRRFreeScreenResources(screen_resources);
        return false;
    }

    XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(display, screen_resources, output_info->crtc);
    if (!crtc_info) {
        Log::error("Error: Unable to get CRTC info for the primary output.\n");
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_resources);
        return false;
    }

    width = crtc_info->width;
    height = crtc_info->height;

    XRRFreeCrtcInfo(crtc_info);
    XRRFreeOutputInfo(output_info);
    XRRFreeScreenResources(screen_resources);

    return true;
}

bool
NativeStateX11::create_window(WindowProperties const& properties)
{
    static const char *win_name("glmark2 " GLMARK_VERSION);
    std::string x11_position = get_x11_position_option();
    bool has_x11_position = !x11_position.empty();
    int x = 0, y = 0;

    if (!xdpy_) {
        Log::error("Error: X11 Display has not been initialized!\n");
        return false;
    }

    /* Recreate an existing window only if it has actually been resized */
    if (xwin_) {
        if (properties_.fullscreen != properties.fullscreen ||
            (properties.fullscreen == false &&
             (properties_.width != properties.width ||
              properties_.height != properties.height)))
        {
            XDestroyWindow(xdpy_, xwin_);
            xwin_ = 0;
        }
        else
        {
            return true;
        }
    }

    /* Set desired attributes */
    properties_.fullscreen = properties.fullscreen;
    properties_.visual_id = properties.visual_id;

    if (properties_.fullscreen) {
        /* Get the screen (root window) size */
        int width, height;
        if (get_main_screen_resolution(xdpy_, width, height)) {
            properties_.width = width;
            properties_.height = height;
        }
    }
    else {
        properties_.width = properties.width;
        properties_.height = properties.height;
    }

    XVisualInfo vis_tmpl;
    XVisualInfo *vis_info = 0;
    int num_visuals;

    /* The X window visual must match the supplied visual id */
    vis_tmpl.visualid = properties_.visual_id;
    vis_info = XGetVisualInfo(xdpy_, VisualIDMask, &vis_tmpl,
                              &num_visuals);
    if (!vis_info) {
        Log::error("Error: Could not get a valid XVisualInfo!\n");
        return false;
    }

    if (has_x11_position) {
        std::pair<int, int> pos = parse_pos(x11_position);
        x = pos.first;
        y = pos.second;
        Log::debug("Creating XWindow X: %d Y: %d W: %d H: %d VisualID: 0x%x\n",
                   x, y, properties_.width, properties_.height, vis_info->visualid);
    }
    else {
        Log::debug("Creating XWindow W: %d H: %d VisualID: 0x%x\n",
                   properties_.width, properties_.height, vis_info->visualid);
    }


    /* window attributes */
    XSetWindowAttributes attr;
    unsigned long mask;
    Window root = RootWindow(xdpy_, DefaultScreen(xdpy_));

    attr.background_pixel = 0;
    attr.border_pixel = 0;
    attr.colormap = XCreateColormap(xdpy_, root, vis_info->visual, AllocNone);
    attr.event_mask = KeyPressMask;
    mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

    xwin_ = XCreateWindow(xdpy_, root, x, y, properties_.width, properties_.height,
                          0, vis_info->depth, InputOutput,
                          vis_info->visual, mask, &attr);

    XFree(vis_info);

    if (!xwin_) {
        Log::error("Error: XCreateWindow() failed!\n");
        return false;
    }

    /* set hints and properties */
    Atom fs_atom = None;
    if (properties_.fullscreen) {
        fs_atom = XInternAtom(xdpy_, "_NET_WM_STATE_FULLSCREEN", True);
        if (fs_atom == None)
            Log::debug("Warning: Could not set EWMH Fullscreen hint.\n");
    }
    if (fs_atom != None) {
        XChangeProperty(xdpy_, xwin_,
                        XInternAtom(xdpy_, "_NET_WM_STATE", True),
                        XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(&fs_atom),  1);
    }
    else {
        XSizeHints sizehints;
        sizehints.min_width  = properties_.width;
        sizehints.min_height = properties_.height;
        sizehints.max_width  = properties_.width;
        sizehints.max_height = properties_.height;
        sizehints.flags = PMaxSize | PMinSize;

        if (has_x11_position) {
            sizehints.x = x;
            sizehints.y = y;
            sizehints.flags |=  PPosition;
        }

        XSetWMProperties(xdpy_, xwin_, NULL, NULL,
                         NULL, 0, &sizehints, NULL, NULL);
    }

    /* Set the window name */
    XStoreName(xdpy_ , xwin_,  win_name);

    /* Gracefully handle Window Delete event from window manager */
    wm_delete_window_ = XInternAtom(xdpy_, "WM_DELETE_WINDOW", True);
    XSetWMProtocols(xdpy_, xwin_, &wm_delete_window_, 1);

    return true;
}

void*
NativeStateX11::window(WindowProperties& properties)
{
    properties = properties_;
    return (void*)xwin_;
}

void
NativeStateX11::visible(bool visible)
{
    if (visible)
        XMapWindow(xdpy_, xwin_);
}

bool
NativeStateX11::should_quit()
{
    XEvent event;

    if (!XPending(xdpy_))
        return false;

    XNextEvent(xdpy_, &event);

    if (event.type == KeyPress) {
        if (XLookupKeysym(&event.xkey, 0) == XK_Escape)
            return true;
    }
    else if (event.type == ClientMessage) {
        if ((Atom)event.xclient.data.l[0] == wm_delete_window_) {
            /* Window Delete event from window manager */
            return true;
        }
    }

    return false;
}

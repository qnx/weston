/*
 * Copyright © 2024 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <weston.h>
#include <libweston/libweston.h>
#include <libweston/weston-log.h>
#include <libweston/desktop.h>
#include <libweston/config-parser.h>
#include <libweston/shell-utils.h>
#include <libweston/zalloc.h>
#include <libweston/matrix.h>
#include <libweston/xwayland-api.h>

#include <libweston/backend-drm.h>
#include <libweston/backend-headless.h>
#include <libweston/backend-x11.h>
#include <libweston/backend-wayland.h>

#include <libweston/backend-rdp.h>
#include <libweston/backend-vnc.h>
#include <libweston/backend-pipewire.h>
#include <libweston/windowed-output-api.h>

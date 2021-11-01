/*
 * Copyright 2020-2025 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef WESTON_LUA_SHELL_H
#define WESTON_LUA_SHELL_H

#include <libweston/desktop.h>
#include <libweston/libweston.h>
#include <libweston/config-parser.h>
#include "libweston/shell-utils.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#define LUA_ENUM(x) do {\
	lua_pushnumber(shell->lua, x); \
	lua_setglobal(shell->lua, #x); \
} while (0)

enum lua_shell_cb_id {
	LUA_SHELL_CB_INIT = 0,
	LUA_SHELL_CB_KEYBOARD_FOCUS,
	LUA_SHELL_CB_OUTPUT_CREATE,
	LUA_SHELL_CB_OUTPUT_RESIZED,
	LUA_SHELL_CB_OUTPUT_MOVED,
	LUA_SHELL_CB_SEAT_CREATE,
	LUA_SHELL_CB_SET_XWAYLAND_POSITION,
	LUA_SHELL_CB_SURFACE_ADDED,
	LUA_SHELL_CB_SURFACE_COMMITTED,
	LUA_SHELL_CB_SURFACE_MOVE,
	LUA_SHELL_CB_SURFACE_REMOVED,
	LUA_SHELL_CB_SURFACE_RESIZE,
	LUA_SHELL_CB_SURFACE_FULLSCREEN,
	LUA_SHELL_CB_SURFACE_MAXIMIZE,
	LUA_SHELL_NUM_CB,
};

struct lua_shell_callback {
	const char *name;
	uint32_t regid;
};

struct lua_shell {
	struct weston_compositor *compositor;
	struct weston_desktop *desktop;

	struct lua_shell_callback callbacks[LUA_SHELL_NUM_CB];

	struct wl_listener destroy_listener;
	struct wl_listener output_created_listener;
	struct wl_listener output_resized_listener;
	struct wl_listener output_moved_listener;
	struct wl_listener seat_created_listener;
	struct wl_listener transform_listener;

	struct wl_list output_list;
	struct wl_list seat_list;
	struct wl_list layer_list;
	struct wl_list surface_list;
	struct wl_list view_list;
	struct wl_list timer_list;
	struct wl_list curtain_list;
	struct wl_list binding_list;

	const struct weston_xwayland_surface_api *xwayland_surface_api;
	struct weston_config *config;

	struct lua_State *lua;
};

struct lua_object {
	uint32_t lua_regid;
	uint32_t lua_private_regid;
};

struct lua_shell_output {
	uint32_t lua_regid;
	uint32_t lua_private_regid;

	struct lua_shell *shell;
	struct weston_output *output;
	struct wl_listener output_destroy_listener;

	struct wl_list link;	/** lua_shell::output_list */
};

struct lua_shell_curtain {
	uint32_t lua_regid;
	uint32_t lua_private_regid;

	struct lua_shell *shell;
	struct lua_shell_view *view;
	struct weston_curtain_params params;
	struct weston_curtain *curtain;
	char *name;

	struct wl_list link;	/** lua_shell:curtain_list */
};

struct lua_shell_surface {
	uint32_t lua_regid;
	uint32_t lua_private_regid;

	struct lua_shell *shell;
	struct weston_desktop_surface *desktop_surface;

	struct lua_shell_output *shoutput;
	struct wl_listener output_destroy_listener;

	struct wl_signal destroy_signal;
	struct wl_listener parent_destroy_listener;
	struct lua_shell_surface *parent;

	struct wl_list view_list;

	struct wl_list link;	/** lua_shell::surface_list */
};

struct lua_shell_view {
	uint32_t lua_regid;
	uint32_t lua_private_regid;

	struct lua_shell *shell;
	bool is_desktop_surface;
	struct lua_shell_surface *surface;
	struct weston_view *view;
	struct lua_shell_layer *layer;
	struct wl_listener view_destroy_listener;
	struct wl_list surface_link;	/** lua_shell_surface::view_list */

	struct wl_list link;	/** lua_shell::view_list */
};

struct lua_shell_layer {
	uint32_t lua_regid;
	uint32_t lua_private_regid;

	struct lua_shell *shell;
	struct weston_layer layer;

	struct wl_list link;	/** lua_shell::layer_list */
};

struct lua_shell_seat {
	uint32_t lua_regid;
	uint32_t lua_private_regid;

	struct lua_shell *shell;
	struct weston_seat *seat;
	struct wl_listener seat_destroy_listener;

	struct weston_surface *focused_surface;

	struct wl_listener caps_changed_listener;
	struct wl_listener keyboard_focus_listener;

	struct wl_list link;	/** lua_shell::seat_list */
};

struct lua_shell_timer {
	uint32_t lua_regid;
	uint32_t lua_private_regid;

	struct lua_shell *shell;
	struct wl_event_source *event_source;
	uint32_t cb_regid;

	struct wl_list link;	/** lua_shell::timer_list */
};

struct lua_shell_binding {
        struct weston_binding *binding;
        struct lua_shell *shell;
        uint32_t callback_regid;

        struct wl_list link;	/** lua_shell::binding_list */
};

static inline struct lua_shell *
get_shell_from_arg(struct lua_State *lua, int arg)
{
	struct lua_shell **shell = luaL_checkudata(lua, arg, "weston.global");
	luaL_argcheck(lua, shell && *shell, arg, "`weston' expected");
	return *shell;
}

static inline struct lua_shell_output *
get_output_from_arg(struct lua_State *lua, int arg)
{
	struct lua_shell_output **shoutput =
		luaL_checkudata(lua, arg, "weston.output");
	luaL_argcheck(lua, shoutput && *shoutput, arg,
	              "`weston.output' expected");
	return *shoutput;
}

static inline struct lua_shell_seat *
get_seat_from_arg(struct lua_State *lua, int arg)
{
	struct lua_shell_seat **shseat = luaL_checkudata(lua, arg, "weston.seat");
	luaL_argcheck(lua, shseat && *shseat, arg, "`weston.seat' expected");
	return *shseat;
}

static inline struct lua_shell_surface *
get_surface_from_arg(struct lua_State *lua, int arg)
{
	struct lua_shell_surface **shsurf =
		luaL_checkudata(lua, arg, "weston.surface");
	luaL_argcheck(lua, shsurf && *shsurf, arg, "`weston.surface' expected");
	return *shsurf;
}

static inline struct lua_shell_view *
get_view_from_arg(struct lua_State *lua, int arg)
{
	struct lua_shell_view **shview = luaL_checkudata(lua, arg, "weston.view");
	luaL_argcheck(lua, shview && *shview, arg, "`weston.view' expected");
	return *shview;
}

static inline struct lua_shell_layer *
get_layer_from_arg(struct lua_State *lua, int arg)
{
	struct lua_shell_layer **shlayer = luaL_checkudata(lua, arg, "weston.layer");
	luaL_argcheck(lua, shlayer && *shlayer, arg, "`weston.layer' expected");
	return *shlayer;
}

static inline struct lua_shell_curtain *
get_curtain_from_arg(struct lua_State *lua, int arg)
{
	struct lua_shell_curtain **shcurtain = luaL_checkudata(lua, arg, "weston.curtain");
	luaL_argcheck(lua, shcurtain && *shcurtain, arg, "`weston.curtain` expected");
	return *shcurtain;
}

#endif /* WESTON_LUA_SHELL_H */

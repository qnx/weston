/*
 * Copyright 2010-2012 Intel Corporation
 * Copyright 2013 Raspberry Pi Foundation
 * Copyright 2011-2012,2020,2025 Collabora, Ltd.
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

#include <config.h>
#include <assert.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lua-shell.h"
#include "frontend/weston.h"
#include "shared/helpers.h"
#include "shared/weston-assert.h"
#include "shared/xalloc.h"
#include "libweston/shell-utils.h"

#include <libweston/xwayland-api.h>

static void
lua_dump_stack(struct lua_State *L)
{
	int top=lua_gettop(L);

	weston_log("=== Lua Shell stack dump ===\n");
	for (int i=1; i <= top; i++) {
		weston_log_continue(STAMP_SPACE "%d\t%s\t", i, luaL_typename(L,i));
		switch (lua_type(L, i)) {
		case LUA_TNIL:
			weston_log_continue(STAMP_SPACE "%s\n", "nil");
			break;
		case LUA_TNUMBER:
			weston_log_continue(STAMP_SPACE "%g\n",lua_tonumber(L,i));
			break;
		case LUA_TBOOLEAN:
			weston_log_continue(STAMP_SPACE "%s\n", (lua_toboolean(L, i) ? "true" : "false"));
			break;
		case LUA_TSTRING:
			weston_log_continue(STAMP_SPACE "%s\n",lua_tostring(L,i));
			break;
		case LUA_TTABLE:
		case LUA_TFUNCTION:
		case LUA_TUSERDATA:
		case LUA_TLIGHTUSERDATA:
		case LUA_TTHREAD:
			default:
			weston_log_continue(STAMP_SPACE "%p\n",lua_topointer(L,i));
			break;
		}
	}
	weston_log_continue(STAMP_SPACE "============================\n");
}

static void *
lxzalloc(struct lua_State *lua, size_t size, const char *meta)
{
	struct lua_object *allocation;
	void **obj;
	uint32_t regid;

	allocation = xzalloc(size);
	obj = lua_newuserdata(lua, sizeof(*obj));
	*obj = allocation;
	luaL_getmetatable(lua, meta);
	lua_setmetatable(lua, -2);
	regid = luaL_ref(lua, LUA_REGISTRYINDEX);
	allocation->lua_regid = regid;

	return allocation;
}

static void
lfree(struct lua_State *lua, void *data)
{
	struct lua_object *obj = data;
	int lua_regid = obj->lua_regid;

	if (obj->lua_private_regid)
		luaL_unref(lua, LUA_REGISTRYINDEX,
			   obj->lua_private_regid);
	obj->lua_private_regid = 0;

	obj->lua_regid = 0;
	luaL_unref(lua, LUA_REGISTRYINDEX, lua_regid);

	/* DO NOT FREE! LUA objects are freed from the garbage
	 * collector later.
	 */
}

static int
lgc(struct lua_State *lua)
{
	struct lua_object **udata = lua_touserdata(lua, 1);
	struct lua_object *obj = *udata;

	assert(obj->lua_regid == 0);
	assert(obj->lua_private_regid == 0);

	free(obj);

	return 0;
}

static bool
lua_shell_push_function(struct lua_shell *shell, enum lua_shell_cb_id id)
{
	struct lua_State *lua = shell->lua;

	if (!shell->callbacks[id].regid)
		return false;

	lua_rawgeti(lua, LUA_REGISTRYINDEX, shell->callbacks[id].regid);

	assert(lua_isfunction(lua, -1));

	return true;
}

static bool
lua_shell_call_function(struct lua_shell *shell, const char *func,
			int args, int rets)
{
	weston_assert_true(shell->compositor,
			   lua_isfunction(shell->lua, -(args + 1)));

	if (lua_pcall(shell->lua, args, rets, 0) != 0) {
		weston_log("error from function '%s': %s\n",
			   func, lua_tostring(shell->lua, -1));
		lua_dump_stack(shell->lua);

		return false;
	}
	return true;
}

static struct lua_shell_surface *
get_lua_shell_surface(struct weston_surface *surface)
{
	struct weston_desktop_surface *desktop_surface =
		weston_surface_get_desktop_surface(surface);

	if (desktop_surface)
		return weston_desktop_surface_get_user_data(desktop_surface);

	return NULL;
}

static void
lua_shell_seat_handle_destroy(struct wl_listener *listener, void *data);

static struct lua_shell_seat *
get_lua_shell_seat(struct weston_seat *seat)
{
	struct wl_listener *listener;

	listener = wl_signal_get(&seat->destroy_signal,
				 lua_shell_seat_handle_destroy);
	assert(listener != NULL);

	return container_of(listener,
			    struct lua_shell_seat, seat_destroy_listener);
}

static void
lua_shell_view_handle_destroy(struct wl_listener *listener, void *data)
{
	/* We don't really care. We use this to help get lua_shell_view
	 * from a weston_view
	 */
}

static struct lua_shell_view *
get_lua_shell_view(struct weston_view *view)
{
	struct wl_listener *listener;

	listener = wl_signal_get(&view->destroy_signal,
				 lua_shell_view_handle_destroy);
	if (!listener)
		return NULL;

	return container_of(listener,
			    struct lua_shell_view, view_destroy_listener);
}

/*
 * lua_shell_surface
 */

static void
lua_shell_surface_set_output(struct lua_shell_surface *shsurf,
			     struct lua_shell_output *shoutput);
static void
lua_shell_surface_set_parent(struct lua_shell_surface *shsurf,
			     struct lua_shell_surface *parent);

static void
lua_shell_surface_notify_parent_destroy(struct wl_listener *listener, void *data)
{
	struct lua_shell_surface *shsurf =
		container_of(listener,
			     struct lua_shell_surface, parent_destroy_listener);

	lua_shell_surface_set_parent(shsurf, shsurf->parent->parent);
}

static void
lua_shell_surface_notify_output_destroy(struct wl_listener *listener, void *data)
{
	struct lua_shell_surface *shsurf =
		container_of(listener,
			     struct lua_shell_surface, output_destroy_listener);

	lua_shell_surface_set_output(shsurf, NULL);
}

static void
lua_shell_surface_set_output(struct lua_shell_surface *shsurf,
                             struct lua_shell_output *shoutput)
{
	shsurf->shoutput = shoutput;

	if (shsurf->output_destroy_listener.notify) {
		wl_list_remove(&shsurf->output_destroy_listener.link);
		shsurf->output_destroy_listener.notify = NULL;
	}

	if (!shsurf->shoutput)
		return;

	shsurf->output_destroy_listener.notify =
		lua_shell_surface_notify_output_destroy;
	wl_signal_add(&shsurf->shoutput->output->destroy_signal,
		      &shsurf->output_destroy_listener);
}

static void
lua_shell_surface_set_parent(struct lua_shell_surface *shsurf,
			     struct lua_shell_surface *parent)
{
	if (shsurf->parent_destroy_listener.notify) {
		wl_list_remove(&shsurf->parent_destroy_listener.link);
		shsurf->parent_destroy_listener.notify = NULL;
	}

	shsurf->parent = parent;

	if (!shsurf->parent)
		return;

	shsurf->parent_destroy_listener.notify =
		lua_shell_surface_notify_parent_destroy;
	wl_signal_add(&shsurf->parent->destroy_signal,
		      &shsurf->parent_destroy_listener);
}

static void
lua_shell_view_dispose(struct lua_shell_view *shview)
{
	wl_list_remove(&shview->view_destroy_listener.link);
	wl_list_remove(&shview->surface_link);
	wl_list_remove(&shview->link);

	if (shview->is_desktop_surface) {
		weston_desktop_surface_unlink_view(shview->view);
		weston_view_destroy(shview->view);
	}

	lfree(shview->shell->lua, shview);
}

static void
lua_shell_layer_dispose(struct lua_shell_layer *shlayer)
{
	wl_list_remove(&shlayer->link);

	weston_layer_fini(&shlayer->layer);

	lfree(shlayer->shell->lua, shlayer);
}

static void
lua_shell_curtain_dispose(struct lua_shell_curtain *shcurtain)
{
	wl_list_remove(&shcurtain->link);

	free(shcurtain->name);
	weston_shell_utils_curtain_destroy(shcurtain->curtain);

	lfree(shcurtain->shell->lua, shcurtain);
}

static void
lua_shell_surface_dispose(struct lua_shell_surface *shsurf)
{
	struct lua_shell_view *shview, *tmp;

	wl_list_remove(&shsurf->link);

	wl_signal_emit(&shsurf->destroy_signal, shsurf);

	weston_desktop_surface_set_user_data(shsurf->desktop_surface, NULL);
	shsurf->desktop_surface = NULL;

	if (shsurf->output_destroy_listener.notify) {
		wl_list_remove(&shsurf->output_destroy_listener.link);
		shsurf->output_destroy_listener.notify = NULL;
	}

	if (shsurf->parent_destroy_listener.notify) {
		wl_list_remove(&shsurf->parent_destroy_listener.link);
		shsurf->parent_destroy_listener.notify = NULL;
		shsurf->parent = NULL;
	}

	wl_list_for_each_safe(shview, tmp, &shsurf->view_list, surface_link)
		lua_shell_view_dispose(shview);

	lfree(shsurf->shell->lua, shsurf);
}

static struct lua_shell_surface *
lua_shell_surface_added(struct lua_shell *shell,
			 struct weston_desktop_surface *desktop_surface)
{
	struct lua_shell_surface *shsurf;

	shsurf = lxzalloc(shell->lua, sizeof *shsurf, "weston.surface");

	shsurf->shell = shell;
	shsurf->desktop_surface = desktop_surface;
	wl_list_init(&shsurf->view_list);

	weston_desktop_surface_set_user_data(desktop_surface, shsurf);

	wl_signal_init(&shsurf->destroy_signal);

	wl_list_insert(shell->surface_list.prev, &shsurf->link);

	if (!lua_shell_push_function(shell, LUA_SHELL_CB_SURFACE_ADDED))
		return shsurf;

	lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
	lua_shell_call_function(shell, "surface_added", 1, 0);

	return shsurf;
}

/*
 * lua_shell_seat
 */

static void
lua_shell_seat_handle_keyboard_focus(struct wl_listener *listener, void *data)
{
	struct weston_keyboard *keyboard = data;
	struct lua_shell_seat *shseat = get_lua_shell_seat(keyboard->seat);
	struct lua_shell *shell = shseat->shell;

	if (!lua_shell_push_function(shell, LUA_SHELL_CB_KEYBOARD_FOCUS))
		return;

	lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shseat->lua_regid);
	lua_shell_call_function(shell, "keyboard_focus", 1, 0);
}

static void
lua_shell_seat_destroy(struct lua_shell_seat *shseat)
{
	wl_list_remove(&shseat->keyboard_focus_listener.link);
	wl_list_remove(&shseat->caps_changed_listener.link);
	wl_list_remove(&shseat->seat_destroy_listener.link);

	wl_list_remove(&shseat->link);

	lfree(shseat->shell->lua, shseat);
}

static void
lua_shell_seat_handle_destroy(struct wl_listener *listener, void *data)
{
	struct lua_shell_seat *shseat =
		container_of(listener,
			     struct lua_shell_seat, seat_destroy_listener);

	lua_shell_seat_destroy(shseat);
}

static void
lua_shell_seat_handle_caps_changed(struct wl_listener *listener, void *data)
{
	struct weston_keyboard *keyboard;
	struct lua_shell_seat *shseat;

	shseat = container_of(listener, struct lua_shell_seat,
			      caps_changed_listener);
	keyboard = weston_seat_get_keyboard(shseat->seat);

	if (keyboard &&
	    wl_list_empty(&shseat->keyboard_focus_listener.link)) {
		wl_signal_add(&keyboard->focus_signal,
			      &shseat->keyboard_focus_listener);
	} else if (!keyboard) {
		wl_list_remove(&shseat->keyboard_focus_listener.link);
		wl_list_init(&shseat->keyboard_focus_listener.link);
	}
}

static struct lua_shell_seat *
lua_shell_seat_create(struct lua_shell *shell, struct weston_seat *seat)
{
	struct lua_shell_seat *shseat;

	shseat = lxzalloc(shell->lua, sizeof *shseat, "weston.seat");

	shseat->seat = seat;
	shseat->shell = shell;

	shseat->seat_destroy_listener.notify = lua_shell_seat_handle_destroy;
	wl_signal_add(&seat->destroy_signal, &shseat->seat_destroy_listener);

	shseat->keyboard_focus_listener.notify = lua_shell_seat_handle_keyboard_focus;
	wl_list_init(&shseat->keyboard_focus_listener.link);

	shseat->caps_changed_listener.notify = lua_shell_seat_handle_caps_changed;
	wl_signal_add(&seat->updated_caps_signal,
		      &shseat->caps_changed_listener);
	lua_shell_seat_handle_caps_changed(&shseat->caps_changed_listener, NULL);

	wl_list_insert(&shell->seat_list, &shseat->link);

	if (!lua_shell_push_function(shell, LUA_SHELL_CB_SEAT_CREATE))
		return shseat;

	lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shseat->lua_regid);
	lua_shell_call_function(shell, "seat_create", 1, 0);
	return shseat;
}

/*
 * lua_shell_output
 */

static void
lua_shell_output_destroy(struct lua_shell_output *shoutput)
{
	shoutput->output = NULL;
	shoutput->output_destroy_listener.notify = NULL;

	wl_list_remove(&shoutput->output_destroy_listener.link);
	wl_list_remove(&shoutput->link);

	lfree(shoutput->shell->lua, shoutput);
}

static void
lua_shell_output_notify_output_destroy(struct wl_listener *listener, void *data)
{
	struct lua_shell_output *shoutput =
		container_of(listener,
			     struct lua_shell_output, output_destroy_listener);

	lua_shell_output_destroy(shoutput);
}

static struct lua_shell_output *
lua_shell_output_create(struct lua_shell *shell, struct weston_output *output)
{
	struct lua_shell_output *shoutput;

	shoutput = lxzalloc(shell->lua, sizeof *shoutput, "weston.output");

	shoutput->output = output;
	shoutput->shell = shell;

	shoutput->output_destroy_listener.notify =
		lua_shell_output_notify_output_destroy;
	wl_signal_add(&shoutput->output->destroy_signal,
		      &shoutput->output_destroy_listener);

	wl_list_insert(shell->output_list.prev, &shoutput->link);

	weston_output_set_shell_private(output, shoutput);
	if (!lua_shell_push_function(shell, LUA_SHELL_CB_OUTPUT_CREATE))
		return shoutput;

	lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shoutput->lua_regid);
	lua_shell_call_function(shell, "output_create", 1, 0);

	return shoutput;
}

/*
 * libweston-desktop
 */

static void
desktop_surface_added(struct weston_desktop_surface *desktop_surface,
		      void *data)
{
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(desktop_surface);
	struct lua_shell *shell = data;
	struct lua_shell_surface *shsurf;

	shsurf = lua_shell_surface_added(shell, desktop_surface);
	if (!shsurf)
		return;

	weston_surface_set_label_func(surface,
				      weston_shell_utils_surface_get_label);
}

static void
desktop_surface_removed(struct weston_desktop_surface *desktop_surface,
			void *data)
{
	struct lua_shell *shell = data;
	struct lua_shell_surface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);

	if (!shsurf)
		return;

	if (!lua_shell_push_function(shell, LUA_SHELL_CB_SURFACE_REMOVED))
		return;

	lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
	lua_shell_call_function(shell, "surface_removed", 1, 0);

	lua_shell_surface_dispose(shsurf);
}

static void
desktop_surface_committed(struct weston_desktop_surface *desktop_surface,
			  struct weston_coord_surface buf_offset, void *data)
{
	struct lua_shell_surface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct lua_State *lua = shsurf->shell->lua;

	if (!lua_shell_push_function(shsurf->shell, LUA_SHELL_CB_SURFACE_COMMITTED))
		return;

	lua_rawgeti(lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
	lua_shell_call_function(shsurf->shell, "surface_committed", 1, 0);
}

static void
desktop_surface_move(struct weston_desktop_surface *desktop_surface,
		     struct weston_seat *seat, uint32_t serial, void *shell)
{
	struct lua_shell *ls = shell;
	struct lua_shell_surface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct lua_shell_seat *shseat =
		get_lua_shell_seat(seat);

	if (!lua_shell_push_function(ls, LUA_SHELL_CB_SURFACE_MOVE))
		return;

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shseat->lua_regid);
	lua_pushnumber(ls->lua, serial);
	lua_shell_call_function(ls, "surface_move", 3, 0);
}

static void
desktop_surface_resize(struct weston_desktop_surface *desktop_surface,
		       struct weston_seat *seat, uint32_t serial,
		       enum weston_desktop_surface_edge edges, void *shell)
{
	struct lua_shell *ls = shell;
	struct lua_shell_surface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct lua_shell_seat *shseat =
		get_lua_shell_seat(seat);

	if (!lua_shell_push_function(ls, LUA_SHELL_CB_SURFACE_RESIZE))
		return;

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shseat->lua_regid);
	lua_pushnumber(ls->lua, edges);
	lua_shell_call_function(ls, "surface_resize", 3, 0);
}

static void
desktop_surface_set_parent(struct weston_desktop_surface *desktop_surface,
			   struct weston_desktop_surface *parent,
			   void *shell)
{
	struct lua_shell_surface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct lua_shell_surface *shsurf_parent =
		parent ? weston_desktop_surface_get_user_data(parent) : NULL;

	lua_shell_surface_set_parent(shsurf, shsurf_parent);
}

static void
desktop_surface_fullscreen_requested(struct weston_desktop_surface *desktop_surface,
				     bool fullscreen,
				     struct weston_output *output, void *shell)
{
	struct lua_shell *ls = shell;
	struct lua_shell_surface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);

	if (!lua_shell_push_function(ls, LUA_SHELL_CB_SURFACE_FULLSCREEN))
		return;

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
	if (output) {
		struct lua_shell_output *shoutput =
			weston_output_get_shell_private(output);

		lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shoutput->lua_regid);
	} else
		lua_pushnil(ls->lua);
	lua_pushboolean(ls->lua, fullscreen);
	lua_shell_call_function(ls, "surface_fullscreen", 3, 0);
}

static void
desktop_surface_maximized_requested(struct weston_desktop_surface *desktop_surface,
				    bool maximized, void *shell)
{
	struct lua_shell *ls = shell;
	struct lua_shell_surface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);

	if (!lua_shell_push_function(ls, LUA_SHELL_CB_SURFACE_MAXIMIZE))
		return;

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
	lua_pushboolean(ls->lua, maximized);
	lua_shell_call_function(ls, "surface_maximize", 2, 0);
}

static void
desktop_surface_minimized_requested(struct weston_desktop_surface *desktop_surface,
				    void *shell)
{
}

static void
desktop_surface_ping_timeout(struct weston_desktop_client *desktop_client,
			     void *shell_)
{
}

static void
desktop_surface_pong(struct weston_desktop_client *desktop_client,
		     void *shell_)
{
}

static void
desktop_surface_set_xwayland_position(struct weston_desktop_surface *desktop_surface,
				      struct weston_coord_global pos, void *shell)
{
	struct lua_shell *ls = shell;
	struct lua_shell_surface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);

	if (!lua_shell_push_function(ls, LUA_SHELL_CB_SET_XWAYLAND_POSITION))
		return;

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
	lua_pushnumber(ls->lua, pos.c.x);
	lua_pushnumber(ls->lua, pos.c.y);
	lua_shell_call_function(ls, "set_xwayland_position", 3, 0);
}

static const struct weston_desktop_api lua_shell_desktop_api = {
	.struct_size = sizeof(struct weston_desktop_api),
	.surface_added = desktop_surface_added,
	.surface_removed = desktop_surface_removed,
	.committed = desktop_surface_committed,
	.move = desktop_surface_move,
	.resize = desktop_surface_resize,
	.set_parent = desktop_surface_set_parent,
	.fullscreen_requested = desktop_surface_fullscreen_requested,
	.maximized_requested = desktop_surface_maximized_requested,
	.minimized_requested = desktop_surface_minimized_requested,
	.ping_timeout = desktop_surface_ping_timeout,
	.pong = desktop_surface_pong,
	.set_xwayland_position = desktop_surface_set_xwayland_position,
};

/*
 * lua_shell
 */

static void
lua_shell_binding_destroy(struct lua_shell_binding *shbinding)
{
	luaL_unref(shbinding->shell->lua, LUA_REGISTRYINDEX,
		   shbinding->callback_regid);
	free(shbinding);
}

static void
button_binding_cb(struct weston_pointer *pointer,
		  const struct timespec *time,
		  uint32_t button, void *data)
{
	struct lua_shell_binding *shbinding = data;
	struct lua_shell *ls = shbinding->shell;
	struct lua_shell_view *shview = NULL;
	struct lua_shell_seat *shseat = get_lua_shell_seat(pointer->seat);

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shbinding->callback_regid);

	if (pointer->focus) {
		shview = get_lua_shell_view(pointer->focus);
		if (!shview)
			return;

		lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shview->lua_regid);
	} else {
		lua_pushnil(ls->lua);
	}
	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shseat->lua_regid);
	lua_pushnumber(ls->lua, button);
	lua_shell_call_function(ls, "[button callback]", 3, 0);
}

static int
lua_shell_env_add_button_binding(struct lua_State *lua)
{
	struct lua_shell_binding *shbinding;
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	int button = luaL_checkinteger(lua, 2);
	int mods = luaL_checkinteger(lua, 3);

	luaL_checktype(lua, 4, LUA_TFUNCTION);

	shbinding = xzalloc(sizeof *shbinding);
	shbinding->callback_regid = luaL_ref(lua, LUA_REGISTRYINDEX);
	shbinding->shell = shell;

	shbinding->binding =
		weston_compositor_add_button_binding(shell->compositor,
						     button, mods,
						     button_binding_cb,
						     shbinding);
	wl_list_insert(shell->binding_list.prev, &shbinding->link);
	return 0;
}

static void
touch_binding_cb(struct weston_touch *touch,
		 const struct timespec *time,
		 void *data)
{
	struct lua_shell_binding *shbinding = data;
	struct lua_shell *ls = shbinding->shell;
	struct lua_shell_view *shview = NULL;
	struct lua_shell_seat *shseat = get_lua_shell_seat(touch->seat);

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shbinding->callback_regid);

	if (touch->focus) {
		shview = get_lua_shell_view(touch->focus);
		lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shview->lua_regid);
	} else {
		lua_pushnil(ls->lua);
	}
	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shseat->lua_regid);
	lua_shell_call_function(ls, "[touch callback]", 2, 0);
}

static int
lua_shell_env_add_touch_binding(struct lua_State *lua)
{
	struct lua_shell_binding *shbinding;
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	int mods = luaL_checkinteger(lua, 2);

	luaL_checktype(lua, 3, LUA_TFUNCTION);

	shbinding = xzalloc(sizeof *shbinding);
	shbinding->callback_regid = luaL_ref(lua, LUA_REGISTRYINDEX);
	shbinding->shell = shell;

	shbinding->binding =
		weston_compositor_add_touch_binding(shell->compositor,
						    mods,
						    touch_binding_cb,
						    shbinding);
	wl_list_insert(shell->binding_list.prev, &shbinding->link);
	return 0;
}

static void
lua_shell_handle_output_created(struct wl_listener *listener, void *data)
{
	struct lua_shell *shell =
		container_of(listener, struct lua_shell, output_created_listener);
	struct weston_output *output = data;

	lua_shell_output_create(shell, output);
}

static void
lua_shell_handle_output_resized(struct wl_listener *listener, void *data)
{
	struct lua_shell *ls =
		container_of(listener, struct lua_shell, output_resized_listener);
	struct weston_output *output = data;
	struct lua_shell_output *shoutput =
		weston_output_get_shell_private(output);

	if (!lua_shell_push_function(ls, LUA_SHELL_CB_OUTPUT_RESIZED))
		return;

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shoutput->lua_regid);
	lua_shell_call_function(ls, "output_resized", 1, 0);
}

static void
lua_shell_handle_output_moved(struct wl_listener *listener, void *data)
{
	struct lua_shell *ls =
		container_of(listener, struct lua_shell, output_moved_listener);
	struct weston_output *output = data;
	struct lua_shell_output *shoutput = weston_output_get_shell_private(output);

	if (!lua_shell_push_function(ls, LUA_SHELL_CB_OUTPUT_MOVED))
		return;

	lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shoutput->lua_regid);
	lua_pushnumber(ls->lua, output->move.c.x);
	lua_pushnumber(ls->lua, output->move.c.y);
	lua_shell_call_function(ls, "output_moved", 3, 0);
}

static void
lua_shell_handle_seat_created(struct wl_listener *listener, void *data)
{
	struct weston_seat *seat = data;
	struct lua_shell *shell =
		container_of(listener, struct lua_shell, seat_created_listener);
	lua_shell_seat_create(shell, seat);
}

static void
transform_handler(struct wl_listener *listener, void *data)
{
}

static int
lua_shell_env_output_get_dimensions(struct lua_State *lua)
{
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 1);

	lua_pushinteger(lua, shoutput->output->width);
	lua_pushinteger(lua, shoutput->output->height);
	return 2;
}

static int
lua_shell_env_output_get_position(struct lua_State *lua)
{
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 1);

	lua_pushinteger(lua, shoutput->output->pos.c.x);
	lua_pushinteger(lua, shoutput->output->pos.c.y);
	return 2;
}

static int
lua_shell_env_output_get_name(struct lua_State *lua)
{
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 1);

	lua_pushstring(lua, shoutput->output->name);
	return 1;
}

static int
lua_shell_env_output_get_scale(struct lua_State *lua)
{
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 1);

	lua_pushinteger(lua, shoutput->output->current_scale);
	return 1;
}

static int
lua_shell_env_output_is_enabled(struct lua_State *lua)
{
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 1);

	lua_pushinteger(lua, shoutput->output->enabled);
	return 1;
}

static int
lua_shell_env_output_set_private(struct lua_State *lua)
{
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 1);

	assert(shoutput->lua_private_regid == 0);
	shoutput->lua_private_regid = luaL_ref(lua, LUA_REGISTRYINDEX);

	return 0;
}

static int
lua_shell_env_output_get_private(struct lua_State *lua)
{
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 1);

	lua_rawgeti(lua, LUA_REGISTRYINDEX, shoutput->lua_private_regid);

	return 1;
}

static void
lua_shell_env_init_output(struct lua_shell *shell)
{
	static const luaL_Reg lua_output_obj_funcs[] = {
		{ "__gc", lgc },
		{ "get_dimensions", lua_shell_env_output_get_dimensions },
		{ "get_position", lua_shell_env_output_get_position },
		{ "get_name", lua_shell_env_output_get_name },
		{ "get_scale", lua_shell_env_output_get_scale },
		{ "is_enabled", lua_shell_env_output_is_enabled },
		{ "set_private", lua_shell_env_output_set_private },
		{ "get_private", lua_shell_env_output_get_private },
		{ NULL, NULL }
	};

	luaL_newmetatable(shell->lua, "weston.output");
	lua_pushvalue(shell->lua, -1);
	lua_setfield(shell->lua, -2, "__index");
	luaL_setfuncs(shell->lua, lua_output_obj_funcs, 0);
}

static int
lua_shell_env_seat_get_capabilities(struct lua_State *lua)
{
	struct lua_shell_seat *shseat = get_seat_from_arg(lua, 1);
	struct weston_seat *seat = shseat->seat;

	lua_pushinteger(lua, seat->pointer_device_count);
	lua_pushinteger(lua, seat->keyboard_device_count);
	lua_pushinteger(lua, seat->touch_device_count);

	return 3;
}

static int
lua_shell_env_seat_get_name(struct lua_State *lua)
{
	struct lua_shell_seat *shseat = get_seat_from_arg(lua, 1);

	lua_pushstring(lua, shseat->seat->seat_name);
	return 1;
}

static void
lua_shell_env_init_seat(struct lua_shell *shell)
{
	static const luaL_Reg lua_seat_obj_funcs[] = {
		{ "__gc", lgc },
		{ "get_capabilities", lua_shell_env_seat_get_capabilities },
		{ "get_name", lua_shell_env_seat_get_name },
		{ NULL, NULL }
	};

	luaL_newmetatable(shell->lua, "weston.seat");
	lua_pushvalue(shell->lua, -1);
	lua_setfield(shell->lua, -2, "__index");
	luaL_setfuncs(shell->lua, lua_seat_obj_funcs, 0);
}

static int
lua_shell_env_surface_get_role(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct weston_surface *surface;

	surface = weston_desktop_surface_get_surface(shsurf->desktop_surface);
	lua_pushstring(lua, weston_surface_get_role(surface));
	return 1;
}

static int
lua_shell_env_surface_get_app_id(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	const char *app_id =
		weston_desktop_surface_get_app_id(shsurf->desktop_surface);

	lua_pushstring(lua, app_id);
	return 1;
}

static int
lua_shell_env_surface_get_title(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	const char *title =
		weston_desktop_surface_get_title(shsurf->desktop_surface);

	lua_pushstring(lua, title);
	return 1;
}

static int
lua_shell_env_surface_get_dimensions(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct weston_surface *surface;

	surface = weston_desktop_surface_get_surface(shsurf->desktop_surface);
	lua_pushinteger(lua, surface->width);
	lua_pushinteger(lua, surface->height);
	return 2;
}

static int
lua_shell_env_surface_get_geometry(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct weston_geometry geometry;

	geometry = weston_desktop_surface_get_geometry(shsurf->desktop_surface);
	lua_pushinteger(lua, geometry.x);
	lua_pushinteger(lua, geometry.y);
	return 2;
}

static int
lua_shell_env_surface_get_output(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);

	lua_rawgeti(lua, LUA_REGISTRYINDEX, shsurf->shoutput->lua_regid);
	return 1;
}

static int
lua_shell_env_surface_get_private(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);

	lua_rawgeti(lua, LUA_REGISTRYINDEX, shsurf->lua_private_regid);

	return 1;
}

static int
lua_shell_env_surface_set_output(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 2);

	lua_shell_surface_set_output(shsurf, shoutput);
	return 0;
}

static int
lua_shell_env_surface_set_private(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);

	assert(shsurf->lua_private_regid == 0);
	shsurf->lua_private_regid = luaL_ref(lua, LUA_REGISTRYINDEX);

	return 0;
}

static int
lua_shell_env_surface_set_state_fullscreen(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 2);

	lua_shell_surface_set_output(shsurf, shoutput);
	weston_desktop_surface_set_fullscreen(shsurf->desktop_surface, true);
	weston_desktop_surface_set_size(shsurf->desktop_surface,
					shsurf->shoutput->output->width,
					shsurf->shoutput->output->height);

	return 0;
}

static int
lua_shell_env_surface_get_state_fullscreen(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct weston_desktop_surface *dsurface = shsurf->desktop_surface;

	lua_pushinteger(lua, weston_desktop_surface_get_fullscreen(dsurface));

	return 1;
}

static int
lua_shell_env_surface_set_state_maximized(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 2);

	lua_shell_surface_set_output(shsurf, shoutput);
	weston_desktop_surface_set_maximized(shsurf->desktop_surface, true);
	weston_desktop_surface_set_size(shsurf->desktop_surface,
					shsurf->shoutput->output->width,
					shsurf->shoutput->output->height);

	return 0;
}

static int
lua_shell_env_surface_get_state_maximized(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct weston_desktop_surface *dsurface = shsurf->desktop_surface;

	lua_pushinteger(lua, weston_desktop_surface_get_maximized(dsurface));
	return 1;
}

static int
lua_shell_env_surface_set_state_normal(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	int width = luaL_checkinteger(lua, 2);
	int height = luaL_checkinteger(lua, 3);

	weston_desktop_surface_set_fullscreen(shsurf->desktop_surface, false);
	weston_desktop_surface_set_maximized(shsurf->desktop_surface, false);
	weston_desktop_surface_set_size(shsurf->desktop_surface, width, height);

	return 0;
}

static int
lua_shell_env_surface_get_parent(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct lua_shell_surface *parent = shsurf->parent;

	if (!parent)
		lua_pushnil(lua);
	else
		lua_rawgeti(lua, LUA_REGISTRYINDEX, parent->lua_regid);

	return 1;
}

static int
lua_shell_env_surface_get_views(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct lua_shell *ls = shsurf->shell;
	struct lua_shell_view *shview;

	lua_newtable(ls->lua);
	wl_list_for_each(shview, &shsurf->view_list, surface_link) {
		char buf[32];

		lua_rawgeti(ls->lua, LUA_REGISTRYINDEX, shview->lua_regid);
		snprintf(buf, sizeof(buf), "view-%"PRIu32, shview->lua_regid);
		lua_setfield(ls->lua, -2, buf);
	}

	return 1;
}

static int
lua_shell_env_surface_create_view(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct lua_shell_view *shview;
	struct weston_view *view;

	shview = lxzalloc(lua, sizeof(*shview), "weston.view");
	view = weston_desktop_surface_create_view(shsurf->desktop_surface);
	shview->view = view;
	shview->shell = shsurf->shell;
	shview->surface = shsurf;
	shview->is_desktop_surface = true;

	wl_list_insert(shview->shell->view_list.prev, &shview->link);
	wl_list_insert(shsurf->view_list.prev, &shview->surface_link);

	shview->view_destroy_listener.notify = lua_shell_view_handle_destroy;
	wl_signal_add(&view->destroy_signal, &shview->view_destroy_listener);

	lua_rawgeti(lua, LUA_REGISTRYINDEX, shview->lua_regid);

	return 1;
}

static int
lua_shell_env_surface_map(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct weston_surface *surface = weston_desktop_surface_get_surface(shsurf->desktop_surface);

	weston_surface_map(surface);
	return 0;
}

static int
lua_shell_env_surface_is_mapped(struct lua_State *lua)
{
	struct lua_shell_surface *shsurf = get_surface_from_arg(lua, 1);
	struct weston_surface *wsurf;
	bool mapped;

	wsurf = weston_desktop_surface_get_surface(shsurf->desktop_surface);
	mapped = weston_surface_is_mapped(wsurf);
	lua_pushboolean(lua, mapped);

	return 1;
}

static int
lua_shell_env_view_get_surface(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);

	if (!shview->surface)
		lua_pushnil(lua);
	else
		lua_rawgeti(lua, LUA_REGISTRYINDEX, shview->surface->lua_regid);

	return 1;
}

static int
lua_shell_env_view_get_private_surface(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);

	if (!shview->surface)
		lua_pushnil(lua);
	else
		lua_rawgeti(lua, LUA_REGISTRYINDEX, shview->surface->lua_private_regid);

	return 1;
}

static int
lua_shell_env_view_get_layer(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);

	if (!shview->layer)
		lua_pushnil(lua);
	else
		lua_rawgeti(lua, LUA_REGISTRYINDEX, shview->layer->lua_regid);

	return 1;
}

static int
lua_shell_env_view_set_layer(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	struct lua_shell_layer *shlayer = get_layer_from_arg(lua, 2);

	shview->layer = shlayer;
	weston_view_move_to_layer(shview->view, &shlayer->layer.view_list);
	return 0;
}

static int
lua_shell_env_view_unset_layer(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);

	shview->layer = NULL;
	weston_view_move_to_layer(shview->view, NULL);

	return 0;
}

static int
lua_shell_env_view_get_position(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	lua_pushinteger(lua, shview->view->geometry.pos_offset.x);
	lua_pushinteger(lua, shview->view->geometry.pos_offset.y);
	return 2;
}

static int
lua_shell_env_view_set_position(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	int x = luaL_checkinteger(lua, 2);
	int y = luaL_checkinteger(lua, 3);
	struct weston_coord_global pos;

	pos.c = weston_coord(x, y);
	weston_view_set_position(shview->view, pos);
	weston_view_update_transform(shview->view);

	return 0;
}

static int
lua_shell_env_view_get_dimensions(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	int width, height;

	if (shview->view->geometry.scissor_enabled) {
		pixman_box32_t *extents =
			pixman_region32_extents(&shview->view->geometry.scissor);
		width = extents->x2 - extents->x1;
		height = extents->y2 - extents->y1;
	} else {
		struct weston_output *output;
		struct weston_compositor *ec =
			shview->view->surface->compositor;

		width = 0;
		height = 0;
		wl_list_for_each(output, &ec->output_list, link) {
			width = MAX(width, output->pos.c.x + output->width);
			height = MAX(height, output->pos.c.y + output->height);
		}
	}

	lua_pushinteger(lua, width);
	lua_pushinteger(lua, height);
	return 2;
}

static int
lua_shell_env_view_set_dimensions(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	struct lua_shell_surface *shsurf = shview->surface;
	int width = luaL_checkinteger(lua, 2);
	int height = luaL_checkinteger(lua, 3);

	if (!shsurf)
		return 0;

	weston_desktop_surface_set_size(shsurf->desktop_surface, width, height);

	return 0;
}

static int
lua_shell_env_view_set_output(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	struct lua_shell_output *shoutput = get_output_from_arg(lua, 2);

	weston_view_set_output(shview->view, shoutput->output);
	return 0;
}

static int
lua_shell_env_view_get_output(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	struct weston_output *output = shview->view->output;
	uint32_t regid = LUA_REFNIL;

	if (output) {
		struct lua_shell_output *shoutput;

		shoutput = weston_output_get_shell_private(output);
		regid = shoutput->lua_regid;
	}
	lua_rawgeti(lua, LUA_REGISTRYINDEX, regid);
	return 1;
}

static int
lua_shell_env_view_get_alpha(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);

	lua_pushnumber(lua, shview->view->alpha);
	return 1;
}

static int
lua_shell_env_view_set_alpha(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	float alpha = luaL_checknumber(lua, 2);

	weston_view_set_alpha(shview->view, alpha);

	return 0;
}

static int
lua_shell_env_view_activate(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	struct lua_shell_seat *shseat = get_seat_from_arg(lua, 2);
	struct weston_surface *main_surface =
		weston_surface_get_main_surface(shview->view->surface);
	struct lua_shell_surface *shsurf = get_lua_shell_surface(main_surface);

	if (!shsurf)
		return 0;

	weston_view_activate_input(shview->view, shseat->seat, WESTON_ACTIVATE_FLAG_NONE);
	weston_desktop_surface_set_activated(shsurf->desktop_surface, true);

	return 0;
}

static int
lua_shell_env_view_deactivate(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	struct weston_surface *main_surface =
		weston_surface_get_main_surface(shview->view->surface);
	struct lua_shell_surface *shsurf = get_lua_shell_surface(main_surface);

	if (!shsurf)
		return 0;

	weston_desktop_surface_set_activated(shsurf->desktop_surface, false);
	return 0;
}

static int
lua_shell_env_view_move_behind_other_view(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	struct lua_shell_view *other_shview = get_view_from_arg(lua, 2);
	struct lua_shell *shell = shview->shell;
	struct weston_compositor *wc = shell->compositor;

	weston_assert_true(wc, &other_shview->view->layer_link.layer);

	weston_view_move_to_layer(shview->view,
				  &other_shview->view->layer_link);

	return 0;
}

static int
lua_shell_env_view_move_in_front_of_other_view(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);
	struct lua_shell_view *other_shview = get_view_from_arg(lua, 2);
	struct lua_shell *shell = shview->shell;
	struct weston_compositor *wc = shell->compositor;

	weston_assert_true(wc, &other_shview->view->layer_link.layer);

	weston_view_move_before_layer_entry(shview->view,
					    &other_shview->view->layer_link);

	return 0;
}

static int
lua_shell_env_view_dispose(struct lua_State *lua)
{
	struct lua_shell_view *shview = get_view_from_arg(lua, 1);

	lua_shell_view_dispose(shview);

	return 0;
}

static void
lua_shell_env_init_surface_view(struct lua_shell *shell)
{
	static const luaL_Reg lua_surface_obj_funcs[] = {
		{ "__gc", lgc },
		{ "get_role", lua_shell_env_surface_get_role },
		{ "get_app_id", lua_shell_env_surface_get_app_id },
		{ "get_title", lua_shell_env_surface_get_title },
		{ "get_geometry", lua_shell_env_surface_get_geometry},
		{ "get_dimensions", lua_shell_env_surface_get_dimensions },
		{ "get_output", lua_shell_env_surface_get_output },
		{ "get_private", lua_shell_env_surface_get_private },
		{ "set_output", lua_shell_env_surface_set_output },
		{ "set_private", lua_shell_env_surface_set_private },
		{ "set_state_fullscreen", lua_shell_env_surface_set_state_fullscreen },
		{ "get_state_fullscreen", lua_shell_env_surface_get_state_fullscreen },
		{ "set_state_maximized", lua_shell_env_surface_set_state_maximized },
		{ "get_state_maximized", lua_shell_env_surface_get_state_maximized },
		{ "set_state_normal", lua_shell_env_surface_set_state_normal },
		{ "get_parent", lua_shell_env_surface_get_parent },
		{ "get_views", lua_shell_env_surface_get_views },
		{ "create_view", lua_shell_env_surface_create_view },
		{ "map", lua_shell_env_surface_map },
		{ "is_mapped", lua_shell_env_surface_is_mapped },
		{ NULL, NULL }
	};
	static const luaL_Reg lua_view_obj_funcs[] = {
		{ "__gc", lgc },
		{ "get_surface", lua_shell_env_view_get_surface },
		{ "get_private_surface", lua_shell_env_view_get_private_surface },
		{ "get_layer", lua_shell_env_view_get_layer },
		{ "set_layer", lua_shell_env_view_set_layer },
		{ "unset_layer", lua_shell_env_view_unset_layer },
		{ "get_position", lua_shell_env_view_get_position },
		{ "set_position", lua_shell_env_view_set_position },
		{ "get_dimensions", lua_shell_env_view_get_dimensions },
		{ "set_dimensions", lua_shell_env_view_set_dimensions },
		{ "get_output", lua_shell_env_view_get_output },
		{ "set_output", lua_shell_env_view_set_output },
		{ "get_alpha", lua_shell_env_view_get_alpha },
		{ "set_alpha", lua_shell_env_view_set_alpha },
		{ "activate", lua_shell_env_view_activate },
		{ "deactivate", lua_shell_env_view_deactivate },
		{ "move_behind_other_view", lua_shell_env_view_move_behind_other_view },
		{ "move_in_front_of_other_view", lua_shell_env_view_move_in_front_of_other_view },
		{ "dispose", lua_shell_env_view_dispose },
		{ NULL, NULL }
	};

	luaL_newmetatable(shell->lua, "weston.surface");
	lua_pushvalue(shell->lua, -1);
	lua_setfield(shell->lua, -2, "__index");
	luaL_setfuncs(shell->lua, lua_surface_obj_funcs, 0);

	luaL_newmetatable(shell->lua, "weston.view");
	lua_pushvalue(shell->lua, -1);
	lua_setfield(shell->lua, -2, "__index");
	luaL_setfuncs(shell->lua, lua_view_obj_funcs, 0);
}

static int
lua_shell_env_layer_get_position(struct lua_State *lua)
{
	struct lua_shell_layer *shlayer = get_layer_from_arg(lua, 1);

	lua_pushinteger(lua, shlayer->layer.position);
	return 1;
}

static int
lua_shell_env_layer_set_position(struct lua_State *lua)
{
	struct lua_shell_layer *shlayer = get_layer_from_arg(lua, 1);
	int64_t position = luaL_checkinteger(lua, 2);

	weston_layer_set_position(&shlayer->layer, position);
	return 0;
}

static int
lua_shell_env_layer_get_views(struct lua_State *lua)
{
	struct lua_shell_layer *shlayer = get_layer_from_arg(lua, 1);
	struct lua_shell_view *shview;
	struct lua_shell *shell = shlayer->shell;
	struct weston_view *view;

	lua_newtable(shell->lua);

        wl_list_for_each(view, &shlayer->layer.view_list.link, layer_link.link) {
		char buf[32];

		shview = get_lua_shell_view(view);
		lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shview->lua_regid);

		snprintf(buf, sizeof(buf), "view-%"PRIu32, shview->lua_regid);
		lua_setfield(shell->lua, -2, buf);
	}
	return 1;
}

static void
lua_shell_env_init_enums(struct lua_shell *shell)
{
	/* input-event-codes */
	LUA_ENUM(BTN_LEFT);
	LUA_ENUM(BTN_RIGHT);

	/* enum weston_layer_position */
	LUA_ENUM(WESTON_LAYER_POSITION_NONE);
	LUA_ENUM(WESTON_LAYER_POSITION_HIDDEN);
	LUA_ENUM(WESTON_LAYER_POSITION_BACKGROUND);
	LUA_ENUM(WESTON_LAYER_POSITION_BOTTOM_UI);
	LUA_ENUM(WESTON_LAYER_POSITION_NORMAL);
	LUA_ENUM(WESTON_LAYER_POSITION_UI);
	LUA_ENUM(WESTON_LAYER_POSITION_FULLSCREEN);
	LUA_ENUM(WESTON_LAYER_POSITION_TOP_UI);
	LUA_ENUM(WESTON_LAYER_POSITION_LOCK);
	LUA_ENUM(WESTON_LAYER_POSITION_CURSOR);
	LUA_ENUM(WESTON_LAYER_POSITION_FADE);
}

static void
lua_shell_env_init_layer(struct lua_shell *shell)
{
	static const luaL_Reg lua_layer_obj_funcs[] = {
		{ "__gc", lgc },
		{ "get_position", lua_shell_env_layer_get_position },
		{ "set_position", lua_shell_env_layer_set_position },
		{ "get_views", lua_shell_env_layer_get_views },
		{ NULL, NULL }
	};

	luaL_newmetatable(shell->lua, "weston.layer");
	lua_pushvalue(shell->lua, -1);
	lua_setfield(shell->lua, -2, "__index");
	luaL_setfuncs(shell->lua, lua_layer_obj_funcs, 0);
}

static int
lua_shell_env_curtain_set_color(struct lua_State *lua)
{
	struct lua_shell_curtain *shcurtain = get_curtain_from_arg(lua, 1);
	unsigned int color = luaL_checkinteger(lua, 2);

	assert(!shcurtain->view);

	shcurtain->params.r = ((color >> 16) & 0xff) / 255.0;
        shcurtain->params.g = ((color >> 8) & 0xff) / 255.0;
        shcurtain->params.b = ((color >> 0) & 0xff) / 255.0;
        shcurtain->params.a = ((color >> 24) & 0xff) / 255.0;

	return 0;
}

static int
lua_shell_env_curtain_set_position(struct lua_State *lua)
{
	struct lua_shell_curtain *shcurtain = get_curtain_from_arg(lua, 1);
	unsigned int x = luaL_checkinteger(lua, 2);
	unsigned int y = luaL_checkinteger(lua, 3);

	assert(!shcurtain->view);

	shcurtain->params.pos.c = weston_coord(x, y);

	return 0;
}

static int
lua_shell_env_curtain_set_dimensions(struct lua_State *lua)
{
	struct lua_shell_curtain *shcurtain = get_curtain_from_arg(lua, 1);
	unsigned int w = luaL_checkinteger(lua, 2);
	unsigned int h = luaL_checkinteger(lua, 3);

	assert(!shcurtain->view);

	shcurtain->params.width = w;
	shcurtain->params.height = h;

	return 0;
}

static int
lua_shell_env_curtain_set_capture_input(struct lua_State *lua)
{
	struct lua_shell_curtain *shcurtain = get_curtain_from_arg(lua, 1);
	bool capture_input = lua_toboolean(lua, 2);

	assert(!shcurtain->view);

	shcurtain->params.capture_input = capture_input;
	return 0;
}

static int
lua_shell_curtain_get_label(struct weston_surface *surface,
			    char *buf, size_t len)
{
	struct lua_shell_curtain *shcurtain = surface->committed_private;
	const char *name = "unnamed";

	if (shcurtain->name)
		name = shcurtain->name;

	return snprintf(buf, len, "%s (curtain)", name);
}

static int
lua_shell_env_curtain_get_view(struct lua_State *lua)
{
	struct lua_shell_curtain *shcurtain = get_curtain_from_arg(lua, 1);
	struct lua_shell *shell = shcurtain->shell;
	struct lua_shell_view *shview = shcurtain->view;
	struct weston_view *view;

	if (shview)
		goto done;

	shcurtain->params.get_label = lua_shell_curtain_get_label;
	shcurtain->params.surface_private = shcurtain;
	shcurtain->curtain = weston_shell_utils_curtain_create(shell->compositor,
							       &shcurtain->params);

	shview = lxzalloc(lua, sizeof(*shview), "weston.view");
	shview->view = shcurtain->curtain->view;
	shcurtain->view = shview;
	shview->shell = shell;
	view = shview->view;

	shview->view_destroy_listener.notify = lua_shell_view_handle_destroy;
	wl_signal_add(&view->destroy_signal, &shview->view_destroy_listener);

	wl_list_insert(shview->shell->view_list.prev, &shview->link);
	wl_list_init(&shview->surface_link);

done:
	lua_rawgeti(lua, LUA_REGISTRYINDEX, shview->lua_regid);

	return 1;
}

static int
lua_shell_env_curtain_dispose(struct lua_State *lua)
{
	struct lua_shell_curtain *shcurtain = get_curtain_from_arg(lua, 1);

	lua_shell_curtain_dispose(shcurtain);

	return 0;
}

static void
lua_shell_env_init_curtain(struct lua_shell *shell)
{
	static const luaL_Reg lua_layer_obj_funcs[] = {
		{ "__gc", lgc },
		{ "set_color", lua_shell_env_curtain_set_color },
		{ "set_position", lua_shell_env_curtain_set_position },
		{ "set_dimensions", lua_shell_env_curtain_set_dimensions },
		{ "set_capture_input", lua_shell_env_curtain_set_capture_input },
		{ "get_view", lua_shell_env_curtain_get_view },
		{ "dispose", lua_shell_env_curtain_dispose },
		{ NULL, NULL }
	};

	luaL_newmetatable(shell->lua, "weston.curtain");
	lua_pushvalue(shell->lua, -1);
	lua_setfield(shell->lua, -2, "__index");
	luaL_setfuncs(shell->lua, lua_layer_obj_funcs, 0);
}

static void
lua_shell_env_destroy_callbacks(struct lua_shell *shell)
{
	struct lua_shell_callback *cb = shell->callbacks;
	unsigned int i;

	for (i = 0; i < LUA_SHELL_NUM_CB; i++) {
		if (!cb[i].regid)
			continue;

		luaL_unref(shell->lua, LUA_REGISTRYINDEX, cb[i].regid);
		cb[i].regid = 0;
	}
}

static bool
lua_shell_env_init_callbacks(struct lua_shell *shell)
{
	struct lua_State *lua = shell->lua;
	int objtype;
	unsigned int i;
	struct lua_shell_callback *cb = shell->callbacks;

	cb[LUA_SHELL_CB_INIT].name =			"init";
	cb[LUA_SHELL_CB_SURFACE_ADDED].name =		"surface_added";
	cb[LUA_SHELL_CB_KEYBOARD_FOCUS].name =		"keyboard_focus";
	cb[LUA_SHELL_CB_SEAT_CREATE].name =		"seat_create";
	cb[LUA_SHELL_CB_SURFACE_ADDED].name =		"surface_added";
	cb[LUA_SHELL_CB_SURFACE_COMMITTED].name = 	"surface_committed";
	cb[LUA_SHELL_CB_SURFACE_MOVE].name =		"surface_move";
	cb[LUA_SHELL_CB_SURFACE_REMOVED].name = 	"surface_removed";
	cb[LUA_SHELL_CB_SURFACE_RESIZE].name =		"surface_resize";
	cb[LUA_SHELL_CB_SURFACE_FULLSCREEN].name =	"surface_fullscreen";
	cb[LUA_SHELL_CB_SURFACE_MAXIMIZE].name =	"surface_maximize";
	cb[LUA_SHELL_CB_SET_XWAYLAND_POSITION].name =	"set_xwayland_position";
	cb[LUA_SHELL_CB_OUTPUT_CREATE].name = 		"output_create";
	cb[LUA_SHELL_CB_OUTPUT_RESIZED].name =		"output_resized";
	cb[LUA_SHELL_CB_OUTPUT_MOVED].name =		"output_moved";

	lua_getglobal(lua, "lua_shell_callbacks");
	if (!lua_istable(lua, -1)) {
		weston_log("lua_shell_callbacks table missing\n");
		return false;
	}

	for (i = 0; i < LUA_SHELL_NUM_CB; i++) {
		assert(shell->callbacks[i].name);

		objtype = lua_getfield(lua, -1, shell->callbacks[i].name);
		/* No callback provided for this one. */
		if (objtype == LUA_TNIL) {
			lua_pop(lua, 1);
			continue;
		}

		if (objtype != LUA_TFUNCTION) {
			weston_log("LUA callback for '%s' was not a function!\n",
				   shell->callbacks[i].name);
			return false;
		}

		shell->callbacks[i].regid = luaL_ref(lua, LUA_REGISTRYINDEX);
	}

	return true;
}

static int
lua_shell_env_get_outputs(struct lua_State *lua)
{
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	struct lua_shell_output *shoutput;

	lua_newtable(shell->lua); /* outputs[] = { */
	wl_list_for_each(shoutput, &shell->output_list, link) {
		lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shoutput->lua_regid);
		lua_setfield(shell->lua, -2, shoutput->output->name);
	}

	return 1;
}

static int
lua_shell_env_get_seats(struct lua_State *lua)
{
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	struct lua_shell_seat *shseat;

	lua_newtable(shell->lua); /* seats[] = { */
	wl_list_for_each(shseat, &shell->seat_list, link) {
		lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shseat->lua_regid);
		lua_setfield(shell->lua, -2, shseat->seat->seat_name);
	}

	return 1;
}

static int
lua_shell_env_get_surfaces(struct lua_State *lua)
{
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	struct lua_shell_surface *shsurf;

	lua_newtable(shell->lua); /* surfaces[] = { */
	wl_list_for_each(shsurf, &shell->surface_list, link) {
		char buf[32];
		lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shsurf->lua_regid);
		snprintf(buf, sizeof(buf), "surf-%" PRIu32, shsurf->lua_regid);
		lua_setfield(shell->lua, -2, buf);
	}

	return 1;
}

static int
lua_shell_env_get_views(struct lua_State *lua)
{
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	struct lua_shell_view *shview;

	lua_newtable(shell->lua); /* views[] = { */
	wl_list_for_each(shview, &shell->view_list, link) {
		char buf[32];
		lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shview->lua_regid);
		snprintf(buf, sizeof(buf), "view-%" PRIu32, shview->lua_regid);
		lua_setfield(shell->lua, -2, buf);
	}

	return 1;
}

static int
lua_shell_env_get_layers(struct lua_State *lua)
{
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	struct lua_shell_layer *shlayer;

	lua_newtable(shell->lua); /* layers[] = { */
	wl_list_for_each(shlayer, &shell->layer_list, link) {
		char buf[32];

		lua_rawgeti(shell->lua, LUA_REGISTRYINDEX, shlayer->lua_regid);
		snprintf(buf, sizeof(buf), "layer-0x%" PRIx64, shlayer->layer.position);
		lua_setfield(shell->lua, -2, buf);
	}
	return 1;
}

static int
lua_shell_env_create_layer(struct lua_State *lua)
{
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	struct lua_shell_layer *shlayer;

	shlayer = lxzalloc(shell->lua, sizeof *shlayer, "weston.layer");

	shlayer->shell = shell;
	weston_layer_init(&shlayer->layer, shell->compositor);
	wl_list_insert(&shell->layer_list, &shlayer->link);

	lua_rawgeti(lua, LUA_REGISTRYINDEX, shlayer->lua_regid);
	return 1;
}

static int
lua_shell_env_create_curtain(struct lua_State *lua)
{
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	struct lua_shell_curtain *shcurtain;
	const char *name = lua_tostring(lua, -1);

	shcurtain = lxzalloc(shell->lua, sizeof *shcurtain, "weston.curtain");
	shcurtain->shell = shell;
	wl_list_insert(&shell->curtain_list, &shcurtain->link);

	lua_rawgeti(lua, LUA_REGISTRYINDEX, shcurtain->lua_regid);
	if (name)
		shcurtain->name = strdup(name);

	return 1;
}

static int
timer_wrapper(void *data)
{
	struct lua_shell_timer *timer = data;
	struct lua_shell *shell = timer->shell;
	struct lua_State *lua = shell->lua;

	lua_rawgeti(lua, LUA_REGISTRYINDEX, timer->cb_regid);
	lua_rawgeti(lua, LUA_REGISTRYINDEX, timer->lua_private_regid);

	lua_shell_call_function(shell, "[timer callback]", 1, 0);

	luaL_unref(lua, LUA_REGISTRYINDEX, timer->cb_regid);
	luaL_unref(lua, LUA_REGISTRYINDEX, timer->lua_private_regid);
	wl_event_source_remove(timer->event_source);
	free(data);
	return 0;
}

static int
lua_shell_env_set_timer(struct lua_State *lua)
{
	struct wl_event_loop *loop;
	struct lua_shell *shell = get_shell_from_arg(lua, 1);
	struct lua_shell_timer *timer;
	int timeout = luaL_checkinteger(lua, 4);

	luaL_checktype(lua, 2, LUA_TFUNCTION);

	lua_pop(lua, 1);

	timer = xzalloc(sizeof *timer);
	timer->shell = shell;

	loop = wl_display_get_event_loop(shell->compositor->wl_display);
	timer->event_source = wl_event_loop_add_timer(loop, timer_wrapper, timer);
	timer->lua_private_regid = luaL_ref(lua, LUA_REGISTRYINDEX);
	timer->cb_regid = luaL_ref(lua, LUA_REGISTRYINDEX);
	wl_event_source_timer_update(timer->event_source, timeout);

	return 0;
}

static void
lua_shell_env_init_weston(struct lua_shell *shell)
{
	static const luaL_Reg lua_global_obj_funcs[] = {
		{ "get_outputs", lua_shell_env_get_outputs, },
		{ "get_seats", lua_shell_env_get_seats, },
		{ "get_surfaces", lua_shell_env_get_surfaces, },
		{ "get_layers", lua_shell_env_get_layers, },
		{ "get_views", lua_shell_env_get_views, },
		{ "create_layer", lua_shell_env_create_layer, },
		{ "create_curtain", lua_shell_env_create_curtain, },
		{ "set_timer", lua_shell_env_set_timer, },
		{ "add_touch_binding", lua_shell_env_add_touch_binding, },
		{ "add_button_binding", lua_shell_env_add_button_binding, },
		{ NULL, NULL }
	};

	/* instantiate the type */
	luaL_newmetatable(shell->lua, "weston.global");
	lua_pushvalue(shell->lua, -1);
	lua_setfield(shell->lua, -2, "__index");
	luaL_setfuncs(shell->lua, lua_global_obj_funcs, 0);

	/* create a new typed variable */
	struct lua_shell **out = lua_newuserdata(shell->lua, sizeof(*out));
	*out = shell;
	luaL_getmetatable(shell->lua, "weston.global");
	lua_setmetatable(shell->lua, -2);
	lua_setglobal(shell->lua, "weston");
}

static int
lua_shell_init_env(struct lua_shell *shell, const char *script)
{
	/* set up the core Lua interpreter */
	shell->lua = luaL_newstate();
	if (!shell->lua) {
		weston_log("Couldn't initialize Lua environment\n");
		return -1;
	}

	/* add Lua standard libraries */
	luaL_openlibs(shell->lua);

	/* initialise our types and global singleton */
	lua_shell_env_init_enums(shell);
	lua_shell_env_init_curtain(shell);
	lua_shell_env_init_output(shell);
	lua_shell_env_init_seat(shell);
	lua_shell_env_init_surface_view(shell);
	lua_shell_env_init_layer(shell);
	lua_shell_env_init_weston(shell);

	/* Read the initial lua setup script */
	if (luaL_dofile(shell->lua, script) != LUA_OK) {
		const char *error;

		error = lua_tostring(shell->lua, -1);
		weston_log("Lua script '%s' is not ok: %s\n", script, error);
		return -1;
	}

	if (!lua_shell_env_init_callbacks(shell))
		return -1;

	if (!lua_shell_push_function(shell, LUA_SHELL_CB_INIT)) {
		weston_log("Lua init-script missing init function\n");
		return -1;
	}

	if (!lua_shell_call_function(shell, "init", 0, 0))
		return -1;

	return 0;
}

static void
lua_shell_destroy(struct wl_listener *listener, void *data)
{
	struct lua_shell *shell =
		container_of(listener, struct lua_shell, destroy_listener);
	struct lua_shell_output *shoutput, *shoutput_next;
	struct lua_shell_seat *shseat, *shseat_next;
	struct lua_shell_view *shview, *shview_next;
	struct lua_shell_surface *shsurf, *shsurf_next;
	struct lua_shell_layer *shlayer, *shlayer_next;
	struct lua_shell_curtain *shcurtain, *shcurtain_next;
	struct lua_shell_binding *shbinding, *shbinding_next;

	wl_list_remove(&shell->destroy_listener.link);
	wl_list_remove(&shell->output_created_listener.link);
	wl_list_remove(&shell->output_resized_listener.link);
	wl_list_remove(&shell->output_moved_listener.link);
	wl_list_remove(&shell->seat_created_listener.link);
	wl_list_remove(&shell->transform_listener.link);

	wl_list_for_each_safe(shcurtain, shcurtain_next, &shell->curtain_list, link)
		lua_shell_curtain_dispose(shcurtain);

	wl_list_for_each_safe(shoutput, shoutput_next, &shell->output_list, link)
		lua_shell_output_destroy(shoutput);

	wl_list_for_each_safe(shseat, shseat_next, &shell->seat_list, link)
		lua_shell_seat_destroy(shseat);

	wl_list_for_each_safe(shsurf, shsurf_next, &shell->surface_list, link)
		lua_shell_surface_dispose(shsurf);

	wl_list_for_each_safe(shview, shview_next, &shell->view_list, link)
		lua_shell_view_dispose(shview);

	wl_list_for_each_safe(shlayer, shlayer_next, &shell->layer_list, link)
		lua_shell_layer_dispose(shlayer);

	wl_list_for_each_safe(shbinding, shbinding_next, &shell->binding_list, link)
		lua_shell_binding_destroy(shbinding);

	weston_desktop_destroy(shell->desktop);

	if (shell->lua) {
		lua_shell_env_destroy_callbacks(shell);
		lua_close(shell->lua);
	}

	if (shell->config)
		weston_config_destroy(shell->config);

	free(shell);
}

WL_EXPORT int
wet_shell_init(struct weston_compositor *ec,
	       int *argc, char *argv[])
{
	struct weston_config_section *shell_section = NULL;
	char *script = NULL;
	struct lua_shell *shell;
	struct weston_seat *seat;
	struct weston_output *output;
	const char *config_file;
        const struct weston_option options[] = {
                { WESTON_OPTION_STRING, "lua-script", 0, &script },
	};

	shell = zalloc(sizeof *shell);
	if (shell == NULL)
		return -1;

	shell->compositor = ec;

	wl_list_init(&shell->surface_list);
	wl_list_init(&shell->layer_list);
	wl_list_init(&shell->view_list);
	wl_list_init(&shell->timer_list);
	wl_list_init(&shell->seat_list);
	wl_list_init(&shell->output_list);
	wl_list_init(&shell->curtain_list);
	wl_list_init(&shell->binding_list);

	/* Init these because it makes cleanup nicer if
	 * lua_shell_init_env() fails */
	wl_list_init(&shell->seat_created_listener.link);
	wl_list_init(&shell->output_created_listener.link);
	wl_list_init(&shell->output_resized_listener.link);
	wl_list_init(&shell->output_moved_listener.link);

	if (!weston_compositor_add_destroy_listener_once(ec,
							 &shell->destroy_listener,
							 lua_shell_destroy)) {
		free(shell);
		return 0;
	}

	shell->transform_listener.notify = transform_handler;
	wl_signal_add(&ec->transform_signal, &shell->transform_listener);

	config_file = weston_config_get_name_from_env();
	shell->config = weston_config_parse(config_file);

	parse_options(options, ARRAY_LENGTH(options), argc, argv);

	shell->desktop = weston_desktop_create(ec, &lua_shell_desktop_api,
					       shell);
	if (!shell->desktop)
		return -1;

	if (shell->config)
		shell_section = weston_config_get_section(shell->config, "shell", NULL, NULL);
	if (!script && shell_section)
		weston_config_section_get_string(shell_section, "lua-script", &script, NULL);

	if (!script) {
		weston_log("No LUA script\n");
		return -1;
	}

	if (lua_shell_init_env(shell, script)) {
		free(script);
		return -1;
	}
	free(script);

	wl_list_for_each(seat, &ec->seat_list, link)
		lua_shell_seat_create(shell, seat);
	shell->seat_created_listener.notify = lua_shell_handle_seat_created;
	wl_signal_add(&ec->seat_created_signal, &shell->seat_created_listener);

	wl_list_for_each(output, &ec->output_list, link)
		lua_shell_output_create(shell, output);

	shell->output_created_listener.notify = lua_shell_handle_output_created;
	wl_signal_add(&ec->output_created_signal, &shell->output_created_listener);

	shell->output_resized_listener.notify = lua_shell_handle_output_resized;
	wl_signal_add(&ec->output_resized_signal, &shell->output_resized_listener);

	shell->output_moved_listener.notify = lua_shell_handle_output_moved;
	wl_signal_add(&ec->output_moved_signal, &shell->output_moved_listener);

	screenshooter_create(ec);

	return 0;
}

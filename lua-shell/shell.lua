background_layer = {}
normal_layer = {}
hidden_layer = {}
fullscreen_layer = {}
background_curtain = nil
current_width = 0
current_height = 0

function get_tile_row_col(count, width, height)
  if (count == 0) then
    return 1, 1
  end

  local cols = math.ceil(math.sqrt(count))
  local rows = math.ceil(count / cols)

  return cols, rows
end

function sort_views(views)
  local count = 0
  local sorted_views = {}

  for k, v in pairs(views) do
    count = count + 1
    sorted_views[count] = k
  end
  table.sort(sorted_views)

  return count, sorted_views
end

function relayout(output)
  local views = normal_layer:get_views()
  local output_width, output_height
  local count = 0
  local col = 0
  local x = 0
  local row = 0
  local y = 0
  local count, sorted_views = sort_views(views)

  if (output == nil) then
    output_width, output_height = primary_output:get_dimensions()
  else
    output_width, output_height = output:get_dimensions()
  end

  local cols, rows = get_tile_row_col(count)
  local width = math.floor(output_width / cols)
  local height = math.floor(output_height / rows)
  for k, v in ipairs(sorted_views) do
    col = col + 1
    if (col > cols) then
      row = row + 1
      col = 1
      x = 0
      y = y + height
    end
    local view = views[v]
    local surface = view:get_surface()
    local gx, gy = surface:get_geometry()
    view:set_position(x - gx, y - gy)
    surface:set_state_normal(width, height)
    x = x + width
  end
end

function recreate_background(output)
  local x, y = output:get_position()
  local w, h = output:get_dimensions()

  if (background_curtain ~= nil) then
    background_curtain:dispose()
  end

  background_curtain = weston:create_curtain("output curtain")
  background_curtain:set_color(0xFF000000)
  background_curtain:set_position(x, y)
  background_curtain:set_dimensions(w, h)
  background_curtain:set_capture_input(true)
  bv = background_curtain:get_view()
  bv:set_output(output)
  bv:set_layer(background_layer)
end

function my_output_create(output)
  local pd = { has_fullscreen_view = false }

  if (primary_output == nil) then
    primary_output = output
  end

  recreate_background(output)

  pd.background_view = bv
  output:set_private(pd)
end

function output_moved(output, move_x, move_y)
  local views = background_layer:get_views()
  for k, v in pairs(views) do
    if v:get_output() == output then
      x, y = v:get_position()
      v:set_position(x + move_x, y + move_y)
    end
  end

  views = normal_layer:get_views()
  for k, v in pairs(views) do
    if v:get_output() == output then
      x, y = v:get_position()
      v:set_position(x + move_x, y + move_y)
    end
  end
end

function output_resized(output)
  recreate_background(output)
  relayout(output)
end

function surface_added(surface)
  local pd = {last_width = 0, last_height = 0, maximized = false,
	      map_fullscreen = false, fullscreen_output = nil}

  pd.view = surface:create_view()
  pd.width = 0
  pd.height = 0

  local outputs = weston:get_outputs()
  for n, o in pairs(outputs) do
    surface:set_output(o)
    pd.view:set_output(o)
    if (current_width == 0 and current_height == 0) then
      output_width, output_height = o:get_dimensions()
      current_width = output_width
      current_height = output_height
    else
      current_width = math.floor(current_width / 2)
      current_height = math.floor(current_height / 2)
    end
    pd.view:set_dimensions(current_width, current_height)
  end

  surface:set_private(pd)
end

function surface_removed(surface)
  local pd = surface:get_private()

  if (active_view == pd.view) then
    pd.view:deactivate()
    active_view = nil
  end

  if (pd.fullscreen_output) then
    unset_fullscreen(surface)
  end

  pd.view:dispose()

  current_width = current_width * 2
  current_height = current_height * 2
  relayout(nil)
end

function surface_maximize(surface, maximized)
  if (maximized) then
    local pd = surface:get_private()
    pd.view:set_position(0, 0)

    local output = pd.view:get_output()
    if (output == nil) then
      output = primary_output
    end
    surface:set_state_maximized(output)

    pd.maximized = true
  else
    pd.maximized = false
    relayout(nil)
  end
end

function set_fullscreen(surface, output)
  local surf_pd = surface:get_private()
  local output_pd = output:get_private()

  -- We only allow one fullscreen client
  if (surf_pd.fullscreen_output ~= nil or output_pd.has_fullscreen_view) then
    return
  end

  surface:set_state_fullscreen(output)
  output_pd.has_fullscreen_view = true
  output_pd.background_view:set_layer(fullscreen_layer)
  surf_pd.view:move_in_front_of_other_view(output_pd.background_view)
  surf_pd.fullscreen_output = output
  surf_pd.view:set_position(0, 0)
end

function unset_fullscreen(surface)
  local surf_pd = surface:get_private()
  local output = surface:get_output()
  local output_pd = output:get_private()

  if (surf_pd.fullscreen_output == nil) then
    return
  end

  output_pd.background_view:set_layer(background_layer)
  surf_pd.view:set_layer(normal_layer)
  output_pd.has_fullscreen_view = false
  surf_pd.fullscreen_output = nil
  relayout(nil)
end

function surface_fullscreen(surface, output, fullscreen)
  if (fullscreen) then
    local pd = surface:get_private()
    if (output == nil) then
      output = pd.view:get_output()
    end

    if (output == nil) then
      output = primary_output
    end

    if (surface:is_mapped()) then
      set_fullscreen(surface, output)
    else
      pd.map_fullscreen = true
    end
  else
    unset_fullscreen(surface)
    relayout(nil)
  end
end

function lower_fullscreen_layer(surface, output)
  local views_in_fs_layer = fullscreen_layer:get_views()
  local count, sorted_views = sort_views(views_in_fs_layer)

  for k, v in ipairs(sorted_views) do
    local view = views_in_fs_layer[v]
    local pd = view:get_private_surface()

    -- no continue in lua, we have go the other way around
    if (pd ~= nil) then
      if (output ~= nil and pd.fullscreen_output == output) then
        local output_pd = output:get_private()

        if (output_pd.background_view) then
          output_pd.background_view:set_layer(background_layer)
        end

        view:set_layer(normal_layer)
        pd.map_fullscreen = false
        pd.fullscreen_output = nil
        output_pd.has_fullscreen_view = false
        relayout(nil)
      end
    end
  end
end

function surface_committed(surface)
  local pd = surface:get_private()
  local w, h = surface:get_dimensions()
  local good_seat = nil
  local output = nil

  if (w == 0) then
    return
  end

  local is_resized = w ~= pd.width or h ~= pd.height

  pd.width = w
  pd.height = h

  if (is_resized) then
    relayout(nil)
  end

  if surface:is_mapped() then
    return
  end

  surface:map()

  local seats = weston:get_seats()
  for n, o in pairs(seats) do
    good_seat = o
  end

  output = pd.view:get_output()
  lower_fullscreen_layer(surface, output)

  if (active_view ~= nil) then
    active_view:deactivate()
  end

  pd.view:activate(good_seat)
  active_view = pd.view
  pd.view:set_layer(normal_layer)

  if (pd.maximized) then
    surface:set_state_maximized(output)
    return
  elseif (pd.map_fullscreen) then
    set_fullscreen(surface, output)
    return
  end

  relayout(nil)
end

function click_to_activate(focus_view, seat, button)
  if (active_view == focus_view) then
    return
  end

  if (active_view ~= nil) then
    active_view:deactivate()
  end

  focus_view:activate(seat)
  active_view = focus_view
end

function my_init()
  background_layer = weston:create_layer()
  background_layer:set_position(WESTON_LAYER_POSITION_BACKGROUND)

  normal_layer = weston:create_layer()
  normal_layer:set_position(WESTON_LAYER_POSITION_NORMAL)

  hidden_layer = weston:create_layer()
  hidden_layer:set_position(WESTON_LAYER_POSITION_HIDDEN)

  fullscreen_layer = weston:create_layer()
  fullscreen_layer:set_position(WESTON_LAYER_POSITION_FULLSCREEN)

  weston:add_button_binding(BTN_LEFT, 0, click_to_activate)
  weston:add_button_binding(BTN_RIGHT, 0, click_to_activate)
end

lua_shell_callbacks = {
  init = my_init,
  surface_added = surface_added,
  surface_committed = surface_committed,
  surface_fullscreen = surface_fullscreen,
  surface_maximize = surface_maximize,
  surface_removed = surface_removed,
  output_create = my_output_create,
  output_moved = output_moved,
  output_resized = output_resized,
}

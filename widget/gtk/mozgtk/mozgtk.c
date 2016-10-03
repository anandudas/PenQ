#include "mozilla/Types.h"
#include "mozilla/Assertions.h"

#define STUB(symbol) MOZ_EXPORT void symbol (void) { MOZ_CRASH(); }

#ifdef COMMON_SYMBOLS
STUB(gdk_atom_intern)
STUB(gdk_atom_name)
STUB(gdk_beep)
STUB(gdk_color_free)
STUB(gdk_cursor_new_for_display)
STUB(gdk_cursor_new_from_name)
STUB(gdk_cursor_new_from_pixbuf)
STUB(gdk_display_close)
STUB(gdk_display_get_default)
STUB(gdk_display_get_default_screen)
STUB(gdk_display_get_pointer)
STUB(gdk_display_get_window_at_pointer)
STUB(gdk_display_manager_get)
STUB(gdk_display_manager_set_default_display)
STUB(gdk_display_open)
STUB(gdk_display_sync)
STUB(gdk_display_warp_pointer)
STUB(gdk_drag_context_get_actions)
STUB(gdk_drag_context_get_dest_window)
STUB(gdk_drag_context_list_targets)
STUB(gdk_drag_status)
STUB(gdk_error_trap_pop)
STUB(gdk_error_trap_push)
STUB(gdk_event_copy)
STUB(gdk_event_free)
STUB(gdk_event_get_axis)
STUB(gdk_event_get_time)
STUB(gdk_event_handler_set)
STUB(gdk_event_peek)
STUB(gdk_event_put)
STUB(gdk_flush)
STUB(gdk_get_default_root_window)
STUB(gdk_get_display)
STUB(gdk_get_display_arg_name)
STUB(gdk_get_program_class)
STUB(gdk_keymap_get_default)
STUB(gdk_keymap_get_direction)
STUB(gdk_keymap_get_entries_for_keyval)
STUB(gdk_keymap_get_for_display)
STUB(gdk_keymap_have_bidi_layouts)
STUB(gdk_keymap_translate_keyboard_state)
STUB(gdk_keyval_name)
STUB(gdk_keyval_to_unicode)
STUB(gdk_pango_context_get)
STUB(gdk_pointer_grab)
STUB(gdk_pointer_ungrab)
STUB(gdk_property_get)
STUB(gdk_screen_get_default)
STUB(gdk_screen_get_display)
STUB(gdk_screen_get_font_options)
STUB(gdk_screen_get_height)
STUB(gdk_screen_get_height_mm)
STUB(gdk_screen_get_monitor_at_window)
STUB(gdk_screen_get_monitor_geometry)
STUB(gdk_screen_get_number)
STUB(gdk_screen_get_resolution)
STUB(gdk_screen_get_rgba_visual)
STUB(gdk_screen_get_root_window)
STUB(gdk_screen_get_system_visual)
STUB(gdk_screen_get_width)
STUB(gdk_screen_height)
STUB(gdk_screen_is_composited)
STUB(gdk_screen_width)
STUB(gdk_unicode_to_keyval)
STUB(gdk_visual_get_depth)
STUB(gdk_visual_get_system)
STUB(gdk_window_add_filter)
STUB(gdk_window_begin_move_drag)
STUB(gdk_window_begin_resize_drag)
STUB(gdk_window_destroy)
STUB(gdk_window_focus)
STUB(gdk_window_get_children)
STUB(gdk_window_get_display)
STUB(gdk_window_get_events)
STUB(gdk_window_get_geometry)
STUB(gdk_window_get_height)
STUB(gdk_window_get_origin)
STUB(gdk_window_get_parent)
STUB(gdk_window_get_position)
STUB(gdk_window_get_root_origin)
STUB(gdk_window_get_screen)
STUB(gdk_window_get_state)
STUB(gdk_window_get_toplevel)
STUB(gdk_window_get_update_area)
STUB(gdk_window_get_user_data)
STUB(gdk_window_get_visual)
STUB(gdk_window_get_width)
STUB(gdk_window_hide)
STUB(gdk_window_input_shape_combine_region)
STUB(gdk_window_invalidate_rect)
STUB(gdk_window_invalidate_region)
STUB(gdk_window_is_destroyed)
STUB(gdk_window_is_visible)
STUB(gdk_window_lower)
STUB(gdk_window_move)
STUB(gdk_window_move_resize)
STUB(gdk_window_new)
STUB(gdk_window_peek_children)
STUB(gdk_window_process_updates)
STUB(gdk_window_raise)
STUB(gdk_window_remove_filter)
STUB(gdk_window_reparent)
STUB(gdk_window_resize)
STUB(gdk_window_set_cursor)
STUB(gdk_window_set_debug_updates)
STUB(gdk_window_set_decorations)
STUB(gdk_window_set_events)
STUB(gdk_window_set_role)
STUB(gdk_window_set_urgency_hint)
STUB(gdk_window_set_user_data)
STUB(gdk_window_shape_combine_region)
STUB(gdk_window_show)
STUB(gdk_window_show_unraised)
STUB(gdk_x11_atom_to_xatom)
STUB(gdk_x11_display_get_user_time)
STUB(gdk_x11_display_get_xdisplay)
STUB(gdk_x11_get_default_root_xwindow)
STUB(gdk_x11_get_default_xdisplay)
STUB(gdk_x11_get_server_time)
STUB(gdk_x11_get_xatom_by_name)
STUB(gdk_x11_get_xatom_by_name_for_display)
STUB(gdk_x11_lookup_xdisplay)
STUB(gdk_x11_screen_get_xscreen)
STUB(gdk_x11_screen_supports_net_wm_hint)
STUB(gdk_x11_visual_get_xvisual)
STUB(gdk_x11_window_foreign_new_for_display)
STUB(gdk_x11_window_lookup_for_display)
STUB(gdk_x11_window_set_user_time)
STUB(gdk_x11_xatom_to_atom)
STUB(gtk_accel_label_new)
STUB(gtk_alignment_get_type)
STUB(gtk_alignment_new)
STUB(gtk_alignment_set_padding)
STUB(gtk_arrow_get_type)
STUB(gtk_arrow_new)
STUB(gtk_bindings_activate)
STUB(gtk_bin_get_child)
STUB(gtk_bin_get_type)
STUB(gtk_border_free)
STUB(gtk_box_get_type)
STUB(gtk_box_pack_start)
STUB(gtk_button_new)
STUB(gtk_button_new_with_label)
STUB(gtk_check_button_new_with_label)
STUB(gtk_check_button_new_with_mnemonic)
STUB(gtk_check_menu_item_new)
STUB(gtk_check_version)
STUB(gtk_clipboard_clear)
STUB(gtk_clipboard_get)
STUB(gtk_clipboard_request_contents)
STUB(gtk_clipboard_request_text)
STUB(gtk_clipboard_set_can_store)
STUB(gtk_clipboard_set_with_data)
STUB(gtk_clipboard_store)
STUB(gtk_color_selection_dialog_get_color_selection)
STUB(gtk_color_selection_dialog_get_type)
STUB(gtk_color_selection_dialog_new)
STUB(gtk_color_selection_get_current_color)
STUB(gtk_color_selection_get_type)
STUB(gtk_color_selection_set_current_color)
STUB(gtk_combo_box_get_active)
STUB(gtk_combo_box_get_type)
STUB(gtk_combo_box_new)
STUB(gtk_combo_box_new_with_entry)
STUB(gtk_combo_box_set_active)
STUB(gtk_combo_box_text_get_type)
STUB(gtk_combo_box_text_new)
STUB(gtk_container_add)
STUB(gtk_container_forall)
STUB(gtk_container_get_border_width)
STUB(gtk_container_get_type)
STUB(gtk_container_set_border_width)
STUB(gtk_container_set_resize_mode)
STUB(gtk_dialog_get_content_area)
STUB(gtk_dialog_get_type)
STUB(gtk_dialog_new_with_buttons)
STUB(gtk_dialog_run)
STUB(gtk_dialog_set_alternative_button_order)
STUB(gtk_dialog_set_default_response)
STUB(gtk_drag_begin)
STUB(gtk_drag_dest_set)
STUB(gtk_drag_finish)
STUB(gtk_drag_get_data)
STUB(gtk_drag_get_source_widget)
STUB(gtk_drag_set_icon_pixbuf)
STUB(gtk_drag_set_icon_widget)
STUB(gtk_editable_get_type)
STUB(gtk_editable_select_region)
STUB(gtk_entry_get_text)
STUB(gtk_entry_get_type)
STUB(gtk_entry_new)
STUB(gtk_entry_set_activates_default)
STUB(gtk_entry_set_text)
STUB(gtk_enumerate_printers)
STUB(gtk_expander_new)
STUB(gtk_file_chooser_add_filter)
STUB(gtk_file_chooser_dialog_new)
STUB(gtk_file_chooser_get_filenames)
STUB(gtk_file_chooser_get_filter)
STUB(gtk_file_chooser_get_preview_filename)
STUB(gtk_file_chooser_get_type)
STUB(gtk_file_chooser_get_uri)
STUB(gtk_file_chooser_list_filters)
STUB(gtk_file_chooser_set_current_folder)
STUB(gtk_file_chooser_set_current_name)
STUB(gtk_file_chooser_set_do_overwrite_confirmation)
STUB(gtk_file_chooser_set_filename)
STUB(gtk_file_chooser_set_filter)
STUB(gtk_file_chooser_set_local_only)
STUB(gtk_file_chooser_set_preview_widget)
STUB(gtk_file_chooser_set_preview_widget_active)
STUB(gtk_file_chooser_set_select_multiple)
STUB(gtk_file_chooser_widget_get_type)
STUB(gtk_file_filter_add_pattern)
STUB(gtk_file_filter_new)
STUB(gtk_file_filter_set_name)
STUB(gtk_fixed_new)
STUB(gtk_frame_new)
STUB(gtk_grab_add)
STUB(gtk_grab_remove)
STUB(gtk_handle_box_new)
STUB(gtk_hbox_new)
STUB(gtk_icon_info_free)
STUB(gtk_icon_info_load_icon)
STUB(gtk_icon_set_add_source)
STUB(gtk_icon_set_new)
STUB(gtk_icon_set_render_icon)
STUB(gtk_icon_set_unref)
STUB(gtk_icon_size_lookup)
STUB(gtk_icon_source_free)
STUB(gtk_icon_source_new)
STUB(gtk_icon_source_set_icon_name)
STUB(gtk_icon_theme_add_builtin_icon)
STUB(gtk_icon_theme_get_default)
STUB(gtk_icon_theme_get_icon_sizes)
STUB(gtk_icon_theme_lookup_by_gicon)
STUB(gtk_icon_theme_lookup_icon)
STUB(gtk_image_get_type)
STUB(gtk_image_menu_item_new)
STUB(gtk_image_new)
STUB(gtk_image_new_from_stock)
STUB(gtk_image_set_from_pixbuf)
STUB(gtk_im_context_filter_keypress)
STUB(gtk_im_context_focus_in)
STUB(gtk_im_context_focus_out)
STUB(gtk_im_context_get_preedit_string)
STUB(gtk_im_context_reset)
STUB(gtk_im_context_set_client_window)
STUB(gtk_im_context_set_cursor_location)
STUB(gtk_im_context_set_surrounding)
STUB(gtk_im_context_simple_new)
STUB(gtk_im_multicontext_get_type)
STUB(gtk_im_multicontext_new)
STUB(gtk_info_bar_get_type)
STUB(gtk_info_bar_get_content_area)
STUB(gtk_info_bar_new)
STUB(gtk_init)
STUB(gtk_invisible_new)
STUB(gtk_key_snooper_install)
STUB(gtk_key_snooper_remove)
STUB(gtk_label_get_type)
STUB(gtk_label_new)
STUB(gtk_label_set_markup)
STUB(gtk_link_button_new)
STUB(gtk_main_do_event)
STUB(gtk_main_iteration)
STUB(gtk_menu_attach_to_widget)
STUB(gtk_menu_bar_new)
STUB(gtk_menu_get_type)
STUB(gtk_menu_item_get_type)
STUB(gtk_menu_item_new)
STUB(gtk_menu_item_set_submenu)
STUB(gtk_menu_new)
STUB(gtk_menu_shell_append)
STUB(gtk_menu_shell_get_type)
STUB(gtk_misc_get_alignment)
STUB(gtk_misc_get_padding)
STUB(gtk_misc_get_type)
STUB(gtk_misc_set_alignment)
STUB(gtk_misc_set_padding)
STUB(gtk_notebook_new)
STUB(gtk_page_setup_copy)
STUB(gtk_page_setup_get_bottom_margin)
STUB(gtk_page_setup_get_left_margin)
STUB(gtk_page_setup_get_orientation)
STUB(gtk_page_setup_get_paper_size)
STUB(gtk_page_setup_get_right_margin)
STUB(gtk_page_setup_get_top_margin)
STUB(gtk_page_setup_new)
STUB(gtk_page_setup_set_bottom_margin)
STUB(gtk_page_setup_set_left_margin)
STUB(gtk_page_setup_set_orientation)
STUB(gtk_page_setup_set_paper_size)
STUB(gtk_page_setup_set_paper_size_and_default_margins)
STUB(gtk_page_setup_set_right_margin)
STUB(gtk_page_setup_set_top_margin)
STUB(gtk_paper_size_free)
STUB(gtk_paper_size_get_display_name)
STUB(gtk_paper_size_get_height)
STUB(gtk_paper_size_get_name)
STUB(gtk_paper_size_get_width)
STUB(gtk_paper_size_is_custom)
STUB(gtk_paper_size_is_equal)
STUB(gtk_paper_size_new)
STUB(gtk_paper_size_new_custom)
STUB(gtk_paper_size_set_size)
STUB(gtk_parse_args)
STUB(gtk_plug_get_socket_window)
STUB(gtk_plug_get_type)
STUB(gtk_printer_accepts_pdf)
STUB(gtk_printer_get_name)
STUB(gtk_printer_get_type)
STUB(gtk_printer_is_default)
STUB(gtk_print_job_new)
STUB(gtk_print_job_send)
STUB(gtk_print_job_set_source_file)
STUB(gtk_print_run_page_setup_dialog)
STUB(gtk_print_settings_copy)
STUB(gtk_print_settings_foreach)
STUB(gtk_print_settings_get)
STUB(gtk_print_settings_get_duplex)
STUB(gtk_print_settings_get_n_copies)
STUB(gtk_print_settings_get_page_ranges)
STUB(gtk_print_settings_get_paper_size)
STUB(gtk_print_settings_get_printer)
STUB(gtk_print_settings_get_print_pages)
STUB(gtk_print_settings_get_resolution)
STUB(gtk_print_settings_get_reverse)
STUB(gtk_print_settings_get_scale)
STUB(gtk_print_settings_get_use_color)
STUB(gtk_print_settings_has_key)
STUB(gtk_print_settings_new)
STUB(gtk_print_settings_set)
STUB(gtk_print_settings_set_duplex)
STUB(gtk_print_settings_set_n_copies)
STUB(gtk_print_settings_set_orientation)
STUB(gtk_print_settings_set_page_ranges)
STUB(gtk_print_settings_set_paper_size)
STUB(gtk_print_settings_set_printer)
STUB(gtk_print_settings_set_print_pages)
STUB(gtk_print_settings_set_resolution)
STUB(gtk_print_settings_set_reverse)
STUB(gtk_print_settings_set_scale)
STUB(gtk_print_settings_set_use_color)
STUB(gtk_print_unix_dialog_add_custom_tab)
STUB(gtk_print_unix_dialog_get_page_setup)
STUB(gtk_print_unix_dialog_get_selected_printer)
STUB(gtk_print_unix_dialog_get_settings)
STUB(gtk_print_unix_dialog_get_type)
STUB(gtk_print_unix_dialog_new)
STUB(gtk_print_unix_dialog_set_manual_capabilities)
STUB(gtk_print_unix_dialog_set_page_setup)
STUB(gtk_print_unix_dialog_set_settings)
STUB(gtk_progress_bar_new)
STUB(gtk_propagate_event)
STUB(gtk_radio_button_get_type)
STUB(gtk_radio_button_new_with_label)
STUB(gtk_radio_button_new_with_mnemonic)
STUB(gtk_radio_button_new_with_mnemonic_from_widget)
STUB(gtk_range_get_min_slider_size)
STUB(gtk_range_get_type)
STUB(gtk_recent_manager_add_item)
STUB(gtk_recent_manager_get_default)
STUB(gtk_scrollbar_get_type)
STUB(gtk_scrolled_window_new)
STUB(gtk_selection_data_copy)
STUB(gtk_selection_data_free)
STUB(gtk_selection_data_get_data)
STUB(gtk_selection_data_get_length)
STUB(gtk_selection_data_get_selection)
STUB(gtk_selection_data_get_target)
STUB(gtk_selection_data_get_targets)
STUB(gtk_selection_data_set)
STUB(gtk_selection_data_set_pixbuf)
STUB(gtk_selection_data_set_text)
STUB(gtk_selection_data_targets_include_text)
STUB(gtk_separator_get_type)
STUB(gtk_separator_menu_item_new)
STUB(gtk_separator_tool_item_new)
STUB(gtk_settings_get_default)
STUB(gtk_settings_get_for_screen)
STUB(gtk_socket_add_id)
STUB(gtk_socket_get_id)
STUB(gtk_socket_get_type)
STUB(gtk_socket_get_plug_window)
STUB(gtk_socket_new)
STUB(gtk_spin_button_new)
STUB(gtk_statusbar_new)
STUB(gtk_style_lookup_icon_set)
STUB(gtk_table_attach)
STUB(gtk_table_get_type)
STUB(gtk_table_new)
STUB(gtk_target_list_add)
STUB(gtk_target_list_add_image_targets)
STUB(gtk_target_list_new)
STUB(gtk_target_list_unref)
STUB(gtk_targets_include_image)
STUB(gtk_target_table_free)
STUB(gtk_target_table_new_from_list)
STUB(gtk_text_view_new)
STUB(gtk_toggle_button_get_active)
STUB(gtk_toggle_button_get_type)
STUB(gtk_toggle_button_new)
STUB(gtk_toggle_button_set_active)
STUB(gtk_toggle_button_set_inconsistent)
STUB(gtk_toolbar_new)
STUB(gtk_tooltip_get_type)
STUB(gtk_tree_view_append_column)
STUB(gtk_tree_view_column_new)
STUB(gtk_tree_view_column_set_title)
STUB(gtk_tree_view_get_type)
STUB(gtk_tree_view_new)
STUB(gtk_vbox_new)
STUB(gtk_widget_add_events)
STUB(gtk_widget_class_find_style_property)
STUB(gtk_widget_destroy)
STUB(gtk_widget_destroyed)
STUB(gtk_widget_ensure_style)
STUB(gtk_widget_event)
STUB(gtk_widget_get_accessible)
STUB(gtk_widget_get_allocation)
STUB(gtk_widget_get_default_direction)
STUB(gtk_widget_get_display)
STUB(gtk_widget_get_events)
STUB(gtk_widget_get_has_window)
STUB(gtk_widget_get_mapped)
STUB(gtk_widget_get_parent)
STUB(gtk_widget_get_parent_window)
STUB(gtk_widget_get_realized)
STUB(gtk_widget_get_screen)
STUB(gtk_widget_get_settings)
STUB(gtk_widget_get_style)
STUB(gtk_widget_get_toplevel)
STUB(gtk_widget_get_type)
STUB(gtk_widget_get_visible)
STUB(gtk_widget_get_visual)
STUB(gtk_widget_get_window)
STUB(gtk_widget_grab_focus)
STUB(gtk_widget_has_focus)
STUB(gtk_widget_has_grab)
STUB(gtk_widget_hide)
STUB(gtk_widget_is_focus)
STUB(gtk_widget_is_toplevel)
STUB(gtk_widget_map)
STUB(gtk_widget_modify_bg)
STUB(gtk_widget_realize)
STUB(gtk_widget_reparent)
STUB(gtk_widget_set_allocation)
STUB(gtk_widget_set_app_paintable)
STUB(gtk_window_set_auto_startup_notification)
STUB(gtk_window_set_opacity)
STUB(gtk_window_set_screen)
STUB(gtk_widget_set_can_focus)
STUB(gtk_widget_set_direction)
STUB(gtk_widget_set_double_buffered)
STUB(gtk_widget_set_has_window)
STUB(gtk_widget_set_mapped)
STUB(gtk_widget_set_name)
STUB(gtk_widget_set_parent)
STUB(gtk_widget_set_parent_window)
STUB(gtk_widget_set_realized)
STUB(gtk_widget_set_redraw_on_allocate)
STUB(gtk_widget_set_sensitive)
STUB(gtk_widget_set_window)
STUB(gtk_widget_show)
STUB(gtk_widget_show_all)
STUB(gtk_widget_size_allocate)
STUB(gtk_widget_style_get)
STUB(gtk_widget_unparent)
STUB(gtk_window_deiconify)
STUB(gtk_window_fullscreen)
STUB(gtk_window_get_group)
STUB(gtk_window_get_transient_for)
STUB(gtk_window_get_type)
STUB(gtk_window_get_type_hint)
STUB(gtk_window_get_window_type)
STUB(gtk_window_group_add_window)
STUB(gtk_window_group_get_current_grab)
STUB(gtk_window_group_new)
STUB(gtk_window_iconify)
STUB(gtk_window_is_active)
STUB(gtk_window_maximize)
STUB(gtk_window_move)
STUB(gtk_window_new)
STUB(gtk_window_present_with_time)
STUB(gtk_window_resize)
STUB(gtk_window_set_accept_focus)
STUB(gtk_window_set_decorated)
STUB(gtk_window_set_deletable)
STUB(gtk_window_set_destroy_with_parent)
STUB(gtk_window_set_geometry_hints)
STUB(gtk_window_set_icon_name)
STUB(gtk_window_set_modal)
STUB(gtk_window_set_skip_taskbar_hint)
STUB(gtk_window_set_title)
STUB(gtk_window_set_transient_for)
STUB(gtk_window_set_type_hint)
STUB(gtk_window_set_wmclass)
STUB(gtk_window_unfullscreen)
STUB(gtk_window_unmaximize)
#endif

#ifdef GTK3_SYMBOLS
STUB(gdk_device_get_source)
STUB(gdk_device_manager_get_client_pointer)
STUB(gdk_disable_multidevice)
STUB(gdk_device_manager_list_devices)
STUB(gdk_display_get_device_manager)
STUB(gdk_error_trap_pop_ignored)
STUB(gdk_event_get_source_device)
STUB(gdk_window_get_type)
STUB(gdk_x11_window_get_xid)
STUB(gdk_x11_display_get_type)
STUB(gtk_box_new)
STUB(gtk_cairo_should_draw_window)
STUB(gtk_cairo_transform_to_window)
STUB(gtk_combo_box_text_append)
STUB(gtk_drag_set_icon_surface)
STUB(gtk_get_major_version)
STUB(gtk_get_micro_version)
STUB(gtk_get_minor_version)
STUB(gtk_menu_button_new)
STUB(gtk_offscreen_window_new)
STUB(gtk_paned_new)
STUB(gtk_render_activity)
STUB(gtk_render_arrow)
STUB(gtk_render_background)
STUB(gtk_render_check)
STUB(gtk_render_expander)
STUB(gtk_render_extension)
STUB(gtk_render_focus)
STUB(gtk_render_frame)
STUB(gtk_render_frame_gap)
STUB(gtk_render_handle)
STUB(gtk_render_line)
STUB(gtk_render_option)
STUB(gtk_render_slider)
STUB(gtk_scale_new)
STUB(gtk_scrollbar_new)
STUB(gtk_style_context_add_class)
STUB(gtk_style_context_add_region)
STUB(gtk_style_context_get)
STUB(gtk_style_context_get_background_color)
STUB(gtk_style_context_get_border)
STUB(gtk_style_context_get_border_color)
STUB(gtk_style_context_get_color)
STUB(gtk_style_context_get_direction)
STUB(gtk_style_context_get_margin)
STUB(gtk_style_context_get_padding)
STUB(gtk_style_context_get_path)
STUB(gtk_style_context_get_property)
STUB(gtk_style_context_get_state)
STUB(gtk_style_context_get_style)
STUB(gtk_style_context_has_class)
STUB(gtk_style_context_invalidate)
STUB(gtk_style_context_new)
STUB(gtk_style_context_remove_class)
STUB(gtk_style_context_remove_region)
STUB(gtk_style_context_restore)
STUB(gtk_style_context_save)
STUB(gtk_style_context_set_direction)
STUB(gtk_style_context_set_path)
STUB(gtk_style_context_set_parent)
STUB(gtk_style_context_set_state)
STUB(gtk_style_properties_lookup_property)
STUB(gtk_tree_view_column_get_button)
STUB(gtk_widget_get_preferred_size)
STUB(gtk_widget_get_state_flags)
STUB(gtk_widget_get_style_context)
STUB(gtk_widget_path_append_for_widget)
STUB(gtk_widget_path_append_type)
STUB(gtk_widget_path_copy)
STUB(gtk_widget_path_free)
STUB(gtk_widget_path_new)
STUB(gtk_widget_path_unref)
STUB(gtk_widget_set_visual)
STUB(gtk_app_chooser_dialog_new_for_content_type)
STUB(gtk_app_chooser_get_type)
STUB(gtk_app_chooser_get_app_info)
STUB(gtk_app_chooser_dialog_get_type)
STUB(gtk_app_chooser_dialog_set_heading)
STUB(gtk_color_chooser_dialog_new)
STUB(gtk_color_chooser_dialog_get_type)
STUB(gtk_color_chooser_get_type)
STUB(gtk_color_chooser_set_rgba)
STUB(gtk_color_chooser_get_rgba)
STUB(gtk_color_chooser_set_use_alpha)
#endif

#ifdef GTK2_SYMBOLS
STUB(gdk_drawable_get_screen)
STUB(gdk_rgb_get_colormap)
STUB(gdk_rgb_get_visual)
STUB(gdk_window_lookup)
STUB(gdk_window_set_back_pixmap)
STUB(gdk_x11_colormap_foreign_new)
STUB(gdk_x11_colormap_get_xcolormap)
STUB(gdk_x11_drawable_get_xdisplay)
STUB(gdk_x11_drawable_get_xid)
STUB(gdk_x11_window_get_drawable_impl)
STUB(gdkx_visual_get)
STUB(gtk_object_get_type)
#endif

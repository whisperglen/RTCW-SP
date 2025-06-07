
#include "win_dx9int.h"
extern "C" {
#include "tr_local.h"
}
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

enum exc_wprocnmouse_e
{
	WNDPROC_SET_IMGUI,
	WNDPROC_RESTORE_WOLF
};

static BOOL g_initialised = FALSE;
static BOOL g_visible = FALSE;
static BOOL g_in_mouse_val = FALSE;
static BOOL g_use_shortcut_keys = TRUE;
static BOOL g_debug_controls = FALSE;
static ImVec2 g_mouse_click_pos;
static BOOL g_mouse_click_new = FALSE;

static void *g_hwnd = NULL;
static WNDPROC g_game_wndproc = NULL;
static int window_center_x = 0;
static int window_center_y = 0;

static ImGuiIO *io;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

extern "C" cvar_t  *in_mouse;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern "C" void IN_DeactivateMouse( void );
extern "C" void IN_StartupMouse(void);
extern "C" void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );
extern "C" LONG WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

static LRESULT WINAPI wnd_proc_hk( HWND hWnd, UINT message_type, WPARAM wparam, LPARAM lparam );
static void exchange_wndproc_and_mouse( enum exc_wprocnmouse_e action );

static void do_draw()
{
	static float f = 0.0f;
	static int counter = 0;

	ImGui::Begin( "Wolf config" );                          // Create a window and append into it.

	if ( ImGui::Button( "Play!" ) )
	{
		exchange_wndproc_and_mouse(WNDPROC_RESTORE_WOLF);
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Close" ) )
	{
		ri.Cvar_Set( "r_showimgui", "0" );
	}

	if ( ImGui::CollapsingHeader( "Flashlight Config" ) )
	{
		const float* pos = qdx_4imgui_flashlight_position_3f();
		const float* dir = qdx_4imgui_flashlight_direction_3f();
		ImGui::Text( "Position  %.3f %.3f %.3f", pos[0], pos[1], pos[2] );
		ImGui::Text( "Direction %.3f %.3f %.3f", dir[0], dir[1], dir[2] );
		ImGui::SeparatorText("Placement");
		float* pos_off = qdx_4imgui_flashlight_position_off_3f();
		float* dir_off = qdx_4imgui_flashlight_direction_off_3f();
		ImGui::DragFloat3( "Position  Offset", pos_off, 0.01, -100, 100 );
		ImGui::DragFloat2( "Direction Offset", dir_off, 0.001, -1, 1 );
		ImGui::SeparatorText("Spot 1");
		ImGui::ColorEdit3( "Color##1", qdx_4imgui_flashlight_colors_3f(0), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##1", qdx_4imgui_flashlight_radiance_1f(0), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##1", qdx_4imgui_flashlight_coneangles_1f(0), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##1", qdx_4imgui_flashlight_conesoft_1f(0), 0.001, 0, 1 );
		ImGui::DragFloat( "Volumetric##1", qdx_4imgui_flashlight_volumetric_1f(0), 0.1, 0, 10 );
		ImGui::SeparatorText("Spot 2");
		ImGui::ColorEdit3( "Color##2", qdx_4imgui_flashlight_colors_3f(1), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##2", qdx_4imgui_flashlight_radiance_1f(1), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##2", qdx_4imgui_flashlight_coneangles_1f(1), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##2", qdx_4imgui_flashlight_conesoft_1f(1), 0.001, 0, 1 );
		ImGui::DragFloat( "Volumetric##2", qdx_4imgui_flashlight_volumetric_1f(1), 0.1, 0, 10 );
		ImGui::SeparatorText("Spot 3");
		ImGui::ColorEdit3( "Color##3", qdx_4imgui_flashlight_colors_3f(2), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##3", qdx_4imgui_flashlight_radiance_1f(2), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##3", qdx_4imgui_flashlight_coneangles_1f(2), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##3", qdx_4imgui_flashlight_conesoft_1f(2), 0.001, 0, 1 );
		ImGui::DragFloat( "Volumetric##3", qdx_4imgui_flashlight_volumetric_1f(2), 0.1, 0, 10 );

		ImGui::NewLine();
		if ( ImGui::Button( "Save Configuration" ) )
		{
			qdx_flashlight_save();
		}
	}

	if ( ImGui::CollapsingHeader( "Lights Global Config" ) )
	{

		ImGui::SeparatorText( "DynamicLight Radiance:" );
		ImGui::Text( "Radiance = color * (BASE + intensity * SCALE)" );
		ImGui::DragFloat( "BASE##1", qdx_4imgui_radiance_dynamic_1f(), 10, 0, 50000 );
		ImGui::DragFloat( "SCALE##1", qdx_4imgui_radiance_dynamic_scale_1f(), 0.01, 0, 10 );
		ImGui::Text( "Radius = RADIUS + intensity * RADIUS_SCALE" );
		ImGui::DragFloat( "RADIUS##1", qdx_4imgui_radius_dynamic_1f(), 0.1, 0, 10 );
		ImGui::DragFloat( "RADIUS_SCALE##1", qdx_4imgui_radius_dynamic_scale_1f(), 0.0005, 0, 1 );
		
		ImGui::SeparatorText( "Corona Radiance:" );
		if(ImGui::DragFloat( "BASE##2", qdx_4imgui_radiance_coronas_1f(), 10, 0, 50000 ))
		{
			qdx_lights_clear( LIGHT_CORONA );
		}
		if(ImGui::DragFloat( "RADIUS##2", qdx_4imgui_radius_coronas_1f(), 0.1, 0, 10 ))
		{
			qdx_lights_clear( LIGHT_CORONA );
		}

		ImGui::NewLine();
		if ( ImGui::Button( "Save to Global" ) )
		{
			qdx_radiance_save( true );
		}
		ImGui::SameLine();
		if ( ImGui::Button( " Save to Map " ) )
		{
			qdx_radiance_save( false );
		}
	}
	
	if ( ImGui::CollapsingHeader( "Light Override" ) )
	{
		static int light_type = 0;
		const light_type_e map_light_type[] = { LIGHT_DYNAMIC, LIGHT_CORONA };
		static int32_t selected_index = 0;
		static bool flash_it = false;
		uint64_comp_t idval;
		static light_override_t* ovr = NULL;
		static float original_color[3] = { 0 };

#define FLASH_IT() if(ovr) { flash_it = 1; original_color[0] = ovr->color[0]; original_color[1] = ovr->color[1]; original_color[2] = ovr->color[2]; }
#define UNFLASH_IT() if(ovr) { flash_it = 0; ovr->color[0] = original_color[0]; ovr->color[1] = original_color[1]; ovr->color[2] = original_color[2]; }

		if ( 0 == qdx_4imgui_light_picking_count() )
		{
			ovr = NULL;
		}

		ImGui::Text( "Hint: Select type, then Scan for closest lights" );
		if ( ImGui::Combo( "##11", &light_type, "Scan DynamicLights\0Scan Coronas\0\0" ) )
		{
			//require a Scan after this changes
			UNFLASH_IT();
			qdx_4imgui_light_picking_clear();
			ovr = NULL;
		}
		if ( ImGui::Button( " Scan " ) )
		{
			selected_index = 0;
			UNFLASH_IT();
			qdx_light_scan_closest_lights(map_light_type[light_type]);
			ovr = qdx_4imgui_light_get_override( qdx_4imgui_light_picking_id( selected_index ), map_light_type[light_type] );
		}
		ImGui::SameLine();
		if ( ImGui::Checkbox( "Flash-it", &flash_it ) )
		{   //value changed
			if ( ovr == NULL )
			{   //don't allow it
				flash_it = 0;
			}
			else
			{
				if ( flash_it ) { FLASH_IT(); }
				else { UNFLASH_IT(); }
			}
		}
		/* Do the flashing here */
		if ( ovr && flash_it )
		{
			float xval = sinf( ri.Milliseconds()/100.0f );
			xval += 1.0f;
			xval *= 0.5f;
			ovr->color[0] = xval;
			ovr->color[1] = xval;
			ovr->color[2] = xval;
			ovr->updated = 1;
		}
		idval.ll = qdx_4imgui_light_picking_id( selected_index );
		ImGui::Text( "Active ID: 0x%x%x", idval.u32[1], idval.u32[0] );

		/* Controls are active if light was found and we are not flashing-it */
		if ( qdx_4imgui_light_picking_count() && flash_it == 0 )
		{
			if ( ImGui::SliderInt( "Closest", &selected_index, 0, qdx_4imgui_light_picking_count() - 1 ) )
			{
				ovr = qdx_4imgui_light_get_override( qdx_4imgui_light_picking_id( selected_index ), map_light_type[light_type] );
			}
			if ( ovr )
			{
				if ( ImGui::DragFloat3( "Position  Offset##10", ovr->position_offset, 0.1, -200, 200 ) ) ovr->updated = 1;
				if ( ImGui::ColorEdit3( "Color##10", ovr->color, ImGuiColorEditFlags_Float ) ) ovr->updated = 1;
				if ( ImGui::DragFloat( "RADIANCE##10", &ovr->radiance_base, 10, 0, 50000 ) ) ovr->updated = 1;
				if ( light_type == 0 )
				{
					if ( ImGui::DragFloat( "RADIANCE_SCALE##10", &ovr->radiance_scale, 0.1, 0, 10 ) ) ovr->updated = 1;
				}
				else
				{
					ImGui::NewLine();
				}
				if ( ImGui::DragFloat( "RADIUS##10", &ovr->radius_base, 0.1, 0, 10 ) ) ovr->updated = 1;
				if ( light_type == 0 )
				{
					if ( ImGui::DragFloat( "RADIUS_SCALE##10", &ovr->radius_scale, 0.0005, 0, 1 ) ) ovr->updated = 1;
				}
				else
				{
					ImGui::NewLine();
				}

				if ( ImGui::DragFloat( "VOLUMETRIC_SCALE", &ovr->volumetric_scale, 0.1, 0, 10 ) ) ovr->updated = 1;
				if ( ImGui::Button( " Save " ) )
				{
					qdx_light_override_save( ovr );
				}
				ImGui::SameLine();
				if ( ImGui::Button( " Reset " ) )
				{
					qdx_4imgui_light_clear_override( idval.ll );
					ovr = qdx_4imgui_light_get_override( qdx_4imgui_light_picking_id( selected_index ), map_light_type[light_type] );
				}
			}
		}
	}

	/*==========================
	 * Togglers
	 *==========================*/
	ImGui::NewLine(); ImVec2 toggle_sz( 150, 0 );
	if ( ImGui::Button( "Toggle Flashlight", toggle_sz ) )
	{
		ri.Cvar_Set( "r_rmx_flashlight", (r_rmx_flashlight->integer ? "0" : "1") );
	}
	ImGui::SameLine();
	ImGui::Text( "Value: %s", r_rmx_flashlight->integer ? "on" : "off");
	if ( ImGui::Button( "Toggle DynamicLight", toggle_sz ) )
	{
		ri.Cvar_Set( "r_rmx_dynamiclight", (r_rmx_dynamiclight->integer ? "0" : "1") );
	}
	ImGui::SameLine();
	ImGui::Text( "Value: %s", r_rmx_dynamiclight->integer ? "on" : "off" );
	//ImGui::SameLine();
	if ( ImGui::Button( "Toggle Coronas", toggle_sz ) )
	{
		ri.Cvar_Set( "r_rmx_coronas", (r_rmx_coronas->integer ? "0" : "1") );
	}
	ImGui::SameLine();
	ImGui::Text( "Value: %s", r_rmx_coronas->integer ? "on" : "off" );
	
	/*==========================
	* Quick Actions
	*==========================*/
	ImGui::NewLine();
	if ( ImGui::Button( "QUIT" ) )
	{
		g_visible = FALSE;
		exchange_wndproc_and_mouse(WNDPROC_RESTORE_WOLF);
		ri.Cmd_ExecuteText(EXEC_APPEND, "quit");
	}
	ImGui::SameLine();
	if ( ImGui::Button( "NOCLIP" ) )
	{
		ri.Cmd_ExecuteText(EXEC_APPEND, "noclip");
	}
	ImGui::SameLine();
	if ( ImGui::Button( "NOTARGET" ) )
	{
		ri.Cmd_ExecuteText(EXEC_APPEND, "notarget");
	}
	ImGui::SameLine();
	if ( ImGui::Button( "GIVE ALL" ) )
	{
		ri.Cmd_ExecuteText(EXEC_APPEND, "give all");
	}
	if ( g_in_mouse_val == 0 )
	{
		ImGui::SameLine();
		if ( ImGui::Button( "FIX DA MOUSE!" ) )
		{
			g_in_mouse_val = 1;
			ri.Cvar_Set( "in_mouse", "1" );
			IN_StartupMouse();
			ri.Cvar_Set( "in_mouse", "0" );
		}
	}

	/*==========================
	* Debug Controls
	*==========================*/
	if ( g_debug_controls )
	{
		ImGui::NewLine();
		if ( ImGui::Button( " MeshAnim " ) )
		{
			ri.Cvar_Set( "r_nomeshanim", (r_nomeshanim->integer ? "0" : "1") );
		}
		ImGui::SameLine();
		ImGui::Text( "Value: %s", r_nomeshanim->integer == 0 ? "on" : "off" );
		ImGui::SliderInt( "Model type", helper_value( 0 ), -1, 4 );
		ImGui::SliderInt( "Bone Num", helper_value( 1 ), -1, 4 );
		ImGui::SliderInt( "Max Bones", helper_value( 2 ), 0, 50 );
		ImGui::SliderInt( "RotX", helper_value( 3 ), -180, 180 );
		ImGui::SliderInt( "RotY", helper_value( 4 ), -180, 180 );
		ImGui::SliderInt( "RotZ", helper_value( 5 ), -180, 180 );
		ImGui::Text( "Num entities: %d", tr.refdef.num_entities );
	}

	/*==========================
	* Stats
	*==========================*/
	ImGui::NewLine();
	ImGui::Text( "App average %.3f ms/frame (%.1f FPS)", 1000.0f / io->Framerate, io->Framerate );
	ImGui::End();
}

void qdx_imgui_init(void *hwnd, void *device)
{
	if ( !g_initialised )
	{
		g_initialised = TRUE;
		g_hwnd = hwnd;
		g_use_shortcut_keys = qdx_readsetting( "imgui_allow_shortcut_keys", g_use_shortcut_keys );
		g_debug_controls = qdx_readsetting( "imgui_debug_controls", g_debug_controls );

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		io = &ImGui::GetIO();
		io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

																  // Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsLight();

		// Setup Platform/Renderer backends
		ImGui_ImplWin32_Init( hwnd );
		ImGui_ImplDX9_Init( (IDirect3DDevice9*)device );
	}
}

void qdx_imgui_deinit()
{
	if ( g_initialised )
	{
		g_initialised = FALSE;
		g_hwnd = NULL;

		if ( g_visible )
		{
			g_visible = FALSE;
			exchange_wndproc_and_mouse(WNDPROC_RESTORE_WOLF);
		}

		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
}

void qdx_imgui_draw()
{
	if ( g_use_shortcut_keys )
	{
		inputs_t keys = get_keypressed();
		if ( keys.imgui || (keys.alt && keys.c) )
		{   //toggle cvar
			ri.Cvar_Set( "r_showimgui", (r_showimgui->integer ? "0" : "1") );
		}
	}

	if ( r_showimgui->modified )
	{
		r_showimgui->modified = qfalse;

		if ( r_showimgui->integer )
		{
			void *game_wndproc = (WNDPROC)(GetWindowLongPtr(HWND(g_hwnd), GWLP_WNDPROC));
			if ( MainWndProc != game_wndproc )
			{
				if ( 0 )
				{
					ri.Printf( PRINT_ALL, "Cannot open Wolf config right now\n" );
					ri.Cvar_Set( "r_showimgui", "0" );
				}
				else
				{ //activate imgui anyway
					ri.Printf( PRINT_ALL, "Unexpected MainWndProc\n" );
					g_visible = TRUE;
#if 1
					exchange_wndproc_and_mouse( WNDPROC_SET_IMGUI );
#else
					//Remix 0.6.0 immediately replaces our proc, and stores it for future use
					//we would have to restore wolf main proc for it to work
					//Trouble is, in fullscreen dx9 has an own proc assigned that calls wolf proc
					game_wndproc = NULL;
					g_in_mouse_val = in_mouse->integer;
					if ( g_in_mouse_val )
						ri.Cvar_Set( "in_mouse", "0" );
					IN_DeactivateMouse();
#endif
				}
			}
			else
			{
				g_visible = TRUE;
				exchange_wndproc_and_mouse(WNDPROC_SET_IMGUI);
			}
		}
		else
		{
			g_visible = FALSE;
			exchange_wndproc_and_mouse(WNDPROC_RESTORE_WOLF);
		}
	}

	if ( g_visible )
	{
		if ( g_mouse_click_new )
		{
			g_mouse_click_new = FALSE;
			//if ( remixOnline && remixInterface.pick_RequestObjectPicking )
			//{
			//	const int32_t RECT_SPAN = 3;
			//	remixapi_Rect2D rect;
			//	rect.left = g_mouse_click_pos.x - RECT_SPAN;
			//	rect.right = g_mouse_click_pos.x + RECT_SPAN;
			//	rect.top = g_mouse_click_pos.y - RECT_SPAN;
			//	rect.bottom = g_mouse_click_pos.y + RECT_SPAN;
			//	remixInterface.pick_RequestObjectPicking( &rect, qdx_light_pick_callback, NULL );
			//}
		}

		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		DWORD zen_val = FALSE, alpha_val = FALSE, scis_val = FALSE;
		qdx.device->GetRenderState( D3DRS_ZENABLE, &zen_val );
		qdx.device->GetRenderState( D3DRS_ALPHABLENDENABLE, &alpha_val );
		qdx.device->GetRenderState( D3DRS_SCISSORTESTENABLE, &scis_val );

		if ( zen_val != FALSE )
			qdx.device->SetRenderState( D3DRS_ZENABLE, FALSE );
		if ( alpha_val != FALSE )
			qdx.device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
		if ( scis_val != FALSE )
			qdx.device->SetRenderState( D3DRS_SCISSORTESTENABLE, FALSE );

		do_draw();

		if ( DX9_BEGIN_SCENE() == D3D_OK )
		{
			ImGui::Render();
			ImGui_ImplDX9_RenderDrawData( ImGui::GetDrawData() );
			DX9_END_SCENE();
		}

		if ( zen_val != FALSE )
			qdx.device->SetRenderState( D3DRS_ZENABLE, zen_val );
		if ( alpha_val != FALSE )
			qdx.device->SetRenderState( D3DRS_ALPHABLENDENABLE, alpha_val );
		if ( scis_val != FALSE )
			qdx.device->SetRenderState( D3DRS_SCISSORTESTENABLE, scis_val );
	}
}

static LRESULT CALLBACK wnd_proc_hk(HWND hWnd, UINT message_type, WPARAM wParam, LPARAM lParam)
{
	bool pass_msg_to_game = false;

	switch (message_type)
	{
	case WM_KEYUP: case WM_SYSKEYUP: // always pass button up events to prevent "stuck" game keys

				   // allows user to move the game window via titlebar :>
	case WM_NCLBUTTONDOWN: case WM_NCLBUTTONUP: case WM_NCMOUSEMOVE: case WM_NCMOUSELEAVE:
	case WM_WINDOWPOSCHANGED: //case WM_WINDOWPOSCHANGING:

							  //case WM_INPUT:
							  //case WM_NCHITTEST: // sets cursor to center
							  //case WM_SETCURSOR: case WM_CAPTURECHANGED:

	case WM_NCACTIVATE:
	case WM_SETFOCUS: case WM_KILLFOCUS:
	case WM_SYSCOMMAND:

	case WM_GETMINMAXINFO: case WM_ENTERSIZEMOVE: case WM_EXITSIZEMOVE:
	case WM_SIZING: case WM_MOVING: case WM_MOVE:

	case WM_CLOSE: case WM_ACTIVATE:
		pass_msg_to_game = true;
		break;

	case WM_MOUSEACTIVATE: 
		//if (!io->WantCaptureMouse)
		//{
		//	pass_msg_to_game = true;
		//}
		break;

	default: break; 
	}

	BOOL imgui_consumed = ImGui_ImplWin32_WndProcHandler( hWnd, message_type, wParam, lParam );

	static bool mouse0down = false;
	if ( !imgui_consumed )
	{
		if ( io->KeyCtrl && io->MouseDown[0] && mouse0down == false )
		{
			mouse0down = true;
			g_mouse_click_pos = io->MousePos;
			g_mouse_click_new = TRUE;
		}
	}

	if ( io->MouseDown[0] == false )
	{
		mouse0down = false;
	}

	if ( g_game_wndproc )
	{

		//if ( FALSE == g_game_input_blocked )
		//{
		//	//send mouse too
		//	if ( io->MouseDelta.x || io->MouseDelta.y )
		//	{
		//		Sys_QueEvent( 0, SE_MOUSE, io->MouseDelta.x, io->MouseDelta.y, 0, NULL );
		//	}
		//}

		if ( pass_msg_to_game || ( !imgui_consumed && !io->WantCaptureMouse) )
		{
			return g_game_wndproc( hWnd, message_type, wParam, lParam );
		}
	}

	//return TRUE;
	return DefWindowProc(hWnd, message_type, wParam, lParam);
}

static void exchange_wndproc_and_mouse( enum exc_wprocnmouse_e action )
{
	switch ( action )
	{
	case WNDPROC_SET_IMGUI:
		g_game_wndproc = (WNDPROC)(SetWindowLongPtr(HWND(g_hwnd), GWLP_WNDPROC, LONG_PTR(wnd_proc_hk)));
		g_in_mouse_val = in_mouse->integer;
		if ( g_in_mouse_val )
			ri.Cvar_Set( "in_mouse", "0" );
		IN_DeactivateMouse();
		break;
	case WNDPROC_RESTORE_WOLF:
		if( g_game_wndproc )
			SetWindowLongPtr( HWND( g_hwnd ), GWLP_WNDPROC, LONG_PTR( g_game_wndproc ) );
		g_game_wndproc = NULL;
		if( g_in_mouse_val )
			ri.Cvar_Set( "in_mouse", "1" );
		g_in_mouse_val = 0;
		break;
	}
}
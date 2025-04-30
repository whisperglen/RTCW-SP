
#include "win_dx9int.h"
extern "C" {
#include "tr_local.h"
}
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

static BOOL g_initialised = FALSE;
static BOOL g_visible = FALSE;
static bool g_game_input_blocked = TRUE;
static BOOL g_in_mouse_val = FALSE;

static void *g_hwnd = NULL;
static WNDPROC g_game_wndproc = NULL;
static int window_center_x = 0;
static int window_center_y = 0;

static ImGuiIO *io;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

extern "C" cvar_t  *in_mouse;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern "C" void IN_DeactivateMouse( void );
extern "C" void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );

static LRESULT WINAPI wnd_proc_hk( HWND hWnd, UINT message_type, WPARAM wparam, LPARAM lparam );
static void game_input_activated(bool active);

static void do_draw()
{
	static float f = 0.0f;
	static int counter = 0;

	ImGui::Begin( "Wolf config" );                          // Create a window and append into it.
	if ( ImGui::Checkbox( "Block Game Input", &g_game_input_blocked ) )
	{
		game_input_activated(!g_game_input_blocked);
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Close Config" ) )
	{
		ri.Cvar_Set( "r_showimgui", "0" );
	}

	if ( ImGui::CollapsingHeader( "Flashlight Config" ) )
	{
		const float* pos = qdx_4imgui_flashlight_position_3f();
		const float* dir = qdx_4imgui_flashlight_direction_3f();
		ImGui::Text( "Position  %.3f %.3f %.3f", pos[0], pos[1], pos[2] );
		ImGui::Text( "Direction %.3f %.3f %.3f", dir[0], dir[1], dir[3] );
		ImGui::SeparatorText("Placement:");
		float* pos_off = qdx_4imgui_flashlight_position_off_3f();
		float* dir_off = qdx_4imgui_flashlight_direction_off_3f();
		ImGui::DragFloat3( "Position  Offset", pos_off, 0.01, -100, 100 );
		ImGui::DragFloat3( "Direction Offset", dir_off, 0.001, -1, 1 );
		ImGui::SeparatorText("Spot 1");
		ImGui::ColorEdit3( "Color##1", qdx_4imgui_flashlight_colors_3f(0), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##1", qdx_4imgui_flashlight_radiance_1f(0), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##1", qdx_4imgui_flashlight_coneangles_1f(0), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##1", qdx_4imgui_flashlight_conesoft_1f(0), 0.001, 0, 1 );
		ImGui::SeparatorText("Spot 2");
		ImGui::ColorEdit3( "Color##2", qdx_4imgui_flashlight_colors_3f(1), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##2", qdx_4imgui_flashlight_radiance_1f(1), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##2", qdx_4imgui_flashlight_coneangles_1f(1), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##2", qdx_4imgui_flashlight_conesoft_1f(1), 0.001, 0, 1 );
		ImGui::SeparatorText("Spot 3");
		ImGui::ColorEdit3( "Color##3", qdx_4imgui_flashlight_colors_3f(2), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##3", qdx_4imgui_flashlight_radiance_1f(2), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##3", qdx_4imgui_flashlight_coneangles_1f(2), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##3", qdx_4imgui_flashlight_conesoft_1f(2), 0.001, 0, 1 );

		if ( ImGui::Button( "Save" ) )
		{
		}
	}
	if ( ImGui::CollapsingHeader( "Static Lights" ) )
	{
		ImGui::DragFloat( "Radiance Dynamic Lights", qdx_4imgui_radiance_dynamic_1f(), 100, 0, 50000 );
		ImGui::DragFloat( "Radiance Corona Lights", qdx_4imgui_radiance_coronas_1f(), 100, 0, 50000 );
		if ( ImGui::Button( "Save to Global" ) )
		{
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Save to Map" ) )
		{
		}
	}

	ImGui::Text( "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io->Framerate, io->Framerate );
	ImGui::End();
}

void qdx_imgui_init(void *hwnd, void *device)
{
	if ( !g_initialised )
	{
		g_initialised = TRUE;
		g_hwnd = hwnd;

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

		if ( g_visible && g_in_mouse_val )
		{
			ri.Cvar_Set( "in_mouse", "1" );
		}
		g_visible = FALSE;
		g_game_input_blocked = TRUE;

		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
}

void qdx_imgui_draw()
{
	inputs_t keys = get_keypressed();
	if ( keys.imgui || (keys.alt && keys.c) )
	{
		ri.Cvar_Set( "r_showimgui", (r_showimgui->integer ? "0" : "1") );
	}

	if ( r_showimgui->modified )
	{
		r_showimgui->modified = qfalse;

		if ( r_showimgui->integer )
		{
			g_visible = TRUE;
			g_game_wndproc = (WNDPROC)(SetWindowLongPtr(HWND(g_hwnd), GWLP_WNDPROC, LONG_PTR(wnd_proc_hk)));
			g_in_mouse_val = in_mouse->integer;
			if( g_in_mouse_val )
				ri.Cvar_Set( "in_mouse", "0" );
			IN_DeactivateMouse();
		}
		else
		{
			g_visible = FALSE;
			if( g_game_wndproc )
				SetWindowLongPtr( HWND( g_hwnd ), GWLP_WNDPROC, LONG_PTR( g_game_wndproc ) );
			g_game_wndproc = NULL;
			if( g_in_mouse_val )
				ri.Cvar_Set( "in_mouse", "1" );
		}
	}

	if ( g_visible )
	{
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
	case WM_KEYUP: // always pass button up events to prevent "stuck" game keys

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

	if ( g_game_wndproc )
	{

		if ( FALSE == g_game_input_blocked )
		{
			//send mouse too
			int mx, my;
			POINT current_pos;

			// find mouse movement
			GetCursorPos( &current_pos );

			mx = current_pos.x - window_center_x;
			my = current_pos.y - window_center_y;

#define MAX_MOUSEMOVE 1000
			if ( mx > MAX_MOUSEMOVE ) mx = MAX_MOUSEMOVE;
			if ( mx < -MAX_MOUSEMOVE ) mx = -MAX_MOUSEMOVE;
			if ( my > MAX_MOUSEMOVE ) my = MAX_MOUSEMOVE;
			if ( my < -MAX_MOUSEMOVE ) my = -MAX_MOUSEMOVE;

			if ( mx || my ) {
				Sys_QueEvent( 0, SE_MOUSE, mx, my, 0, NULL );

				window_center_x = current_pos.x;
				window_center_y = current_pos.y;
			}
		}

		if ( pass_msg_to_game || (FALSE == g_game_input_blocked && !imgui_consumed && !io->WantCaptureMouse) )
		{
			return g_game_wndproc( hWnd, message_type, wParam, lParam );
		}
	}

	//return TRUE;
	return DefWindowProc(hWnd, message_type, wParam, lParam);
}

static void game_input_activated(bool active)
{
	if ( active )
	{
		int width, height;
		RECT window_rect;

		width = GetSystemMetrics( SM_CXSCREEN );
		height = GetSystemMetrics( SM_CYSCREEN );

		GetWindowRect( HWND( g_hwnd ), &window_rect );
		if ( window_rect.left < 0 ) {
			window_rect.left = 0;
		}
		if ( window_rect.top < 0 ) {
			window_rect.top = 0;
		}
		if ( window_rect.right >= width ) {
			window_rect.right = width - 1;
		}
		if ( window_rect.bottom >= height - 1 ) {
			window_rect.bottom = height - 1;
		}
		window_center_x = (window_rect.right + window_rect.left) / 2;
		window_center_y = (window_rect.top + window_rect.bottom) / 2;

		SetCapture( HWND(g_hwnd) );
		ClipCursor( &window_rect );
	}
	else
	{
		ReleaseCapture();
		ClipCursor( NULL );
	}
}
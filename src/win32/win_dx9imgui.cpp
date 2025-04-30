
#include "win_dx9int.h"
extern "C" {
#include "tr_local.h"
}
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

static BOOL g_initialised = FALSE;
static BOOL g_visible = FALSE;
static bool g_game_input_allowed = FALSE;
static BOOL g_in_mouse_val = FALSE;
static void *g_hwnd = NULL;

static WNDPROC g_game_wndproc = NULL;
static ImGuiIO *io;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

extern "C" cvar_t  *in_mouse;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI wnd_proc_hk( HWND hWnd, UINT message_type, WPARAM wparam, LPARAM lparam );


static void do_draw()
{
	static float f = 0.0f;
	static int counter = 0;

	ImGui::Begin( "Wolf config" );                          // Create a window and append into it.

	ImGui::Text( "This is some useful text." );               // Display some text (you can use a format strings too)
	//ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
	ImGui::Checkbox("Game Input", &g_game_input_allowed);

	ImGui::SliderFloat( "float", &f, 0.0f, 1.0f );            // Edit 1 float using a slider from 0.0f to 1.0f
	ImGui::ColorEdit3( "clear color", (float*)&clear_color ); // Edit 3 floats representing a color

	if ( ImGui::Button( "Button" ) )                            // Buttons return true when clicked (most widgets return true when edited/activated)
		counter++;
	ImGui::SameLine();
	ImGui::Text( "counter = %d", counter );

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
		g_game_input_allowed = FALSE;

		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
}

extern "C" void IN_DeactivateMouse( void );
void qdx_imgui_draw()
{
	inputs_t keys = get_keypressed();
	if ( keys.f10 || (keys.alt && keys.c) )
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
		//if (io->WantCaptureMouse || !g_visible)
		//{
		//	pass_msg_to_game = true;
		//}
		break;

	default: break; 
	}

	BOOL imgui_consumed = ImGui_ImplWin32_WndProcHandler( hWnd, message_type, wParam, lParam );

	if ( g_game_wndproc && ( !imgui_consumed || pass_msg_to_game || g_game_input_allowed )) {
		return g_game_wndproc( hWnd, message_type, wParam, lParam );
	}

	return TRUE;
	return DefWindowProc(hWnd, message_type, wParam, lParam);
}
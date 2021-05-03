#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <xcb/xcb.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_keysyms.h>

#include <EGL/egl.h>

#define TAG "game"

#include <rgu/log.h>
#include <rgu/utils.h>
#include <rgu/time.h>
#include <rgu/gl.h>

#include <game/game.h>

#define MOUSE_BTN_LEFT 1
#define MOUSE_BTN_MID 2
#define MOUSE_BTN_RIGHT 3
#define MOUSE_BTN_FWD 4
#define MOUSE_BTN_BACK 5

static xcb_connection_t *dpy_;
static xcb_screen_t *scr_;
static xcb_drawable_t win_;
static xcb_key_symbols_t *syms_;

static uint8_t done_;
static uint8_t pause_;
static time_t release_time_;
static xcb_keysym_t release_key_;
static time_t press_time_;
static xcb_keysym_t press_key_;

static EGLSurface surf_;
static EGLContext ctx_;
static EGLDisplay egl_;

static void handle_resize(xcb_resize_request_event_t *e)
{
}

static void handle_button_press(xcb_button_press_event_t *e)
{
	switch (e->detail) {
	case MOUSE_BTN_LEFT:
		game_touch(e->event_x, e->event_y);
		break;
	case MOUSE_BTN_MID: /* fall through */
	case MOUSE_BTN_RIGHT:
		break;
	case MOUSE_BTN_FWD:
		break;
	case MOUSE_BTN_BACK:
		break;
	default:
		break;
	}
}

static void handle_key_release(xcb_key_press_event_t *e)
{
	xcb_keysym_t sym;

	if (!syms_) {
		ee("key symbols not available\n");
		return;
	}

	ii("\033[1;31m::: KEY RELEASE :::\033[0m\n");

	sym = xcb_key_press_lookup_keysym(syms_, e, 0);
	release_key_ = sym;
	release_time_ = time(NULL);

	if (release_key_ == press_key_ && release_time_ - press_time_ < 1)
		return;

	if (sym == XK_Up) {
		game_keyrelease('e');
	} else if (sym == XK_Down) {
		game_keyrelease('d');
	} else if (sym == XK_Left) {
		game_keyrelease('s');
	} else if (sym == XK_Right) {
		game_keyrelease('f');
	}
}

static void handle_key_press(xcb_key_press_event_t *e)
{
	xcb_keysym_t sym;

	if (!syms_) {
		ee("key symbols not available\n");
		return;
	}

	ii("\033[0;32m::: KEY PRESS :::\033[0m\n");

	sym = xcb_key_press_lookup_keysym(syms_, e, 0);
	press_key_ = sym;
	press_time_ = time(NULL);

	if (sym == XK_Escape) {
		done_ = 1;
	} else if (sym == XK_A || sym == XK_a) {
		game_keypress('a');
	} else if (sym == XK_space) {
		game_keypress('x');
	} else if (sym == XK_S || sym == XK_s || sym == XK_A || sym == XK_a) {
		game_keypress('r');
	} else if (sym == XK_D || sym == XK_d) {
		game_keypress('g');
	} else if (sym == XK_F || sym == XK_f) {
		game_keypress('b');
	} else if (sym == XK_L || sym == XK_l || sym == 246) {
		game_keypress('p');
	} else if (sym == XK_K || sym == XK_k) {
		game_keypress('y');
	} else if (sym == XK_J || sym == XK_j) {
		game_keypress('c');
	} else if (sym == XK_X || sym == XK_x) {
		game_keypress('x');
	} else if (sym == XK_0) {
		game_keypress('0');
	} else if (sym == XK_1) {
		game_keypress('1');
	} else if (sym == XK_2) {
		game_keypress('2');
	} else if (sym == XK_3) {
		game_keypress('3');
	} else if (sym == XK_4) {
		game_keypress('4');
	} else if (sym == XK_5) {
		game_keypress('5');
	} else if (sym == XK_6) {
		game_keypress('6');
	} else if (sym == XK_7) {
		game_keypress('7');
	} else if (sym == XK_8) {
		game_keypress('8');
	} else if (sym == XK_Up) {
		game_keypress('e');
	} else if (sym == XK_Down) {
		game_keypress('d');
	} else if (sym == XK_Left) {
		game_keypress('s');
	} else if (sym == XK_Right) {
		game_keypress('f');
	} else if (sym == XK_Next) {
	} else if (sym == XK_Prior) {
	} else if (sym == XK_Return) {
		if (pause_) {
			dd("resume\n");
			pause_ = 0;
			game_resume();
		} else {
			dd("pause\n");
			pause_ = 1;
			game_pause();
		}
	} else if (sym == XK_BackSpace) {
	} else {
		return;
	}
}

static uint8_t panel_type_;

static uint8_t events(uint8_t poll)
{
	uint8_t rc;
	uint8_t refresh = 0;
	uint8_t type;
	xcb_generic_event_t *e;

	if (poll)
		e = xcb_poll_for_event(dpy_);
	else
		e = xcb_wait_for_event(dpy_);

	if (!e)
		goto out;

	rc = 0;
	type = XCB_EVENT_RESPONSE_TYPE(e);

	switch (type) {
	case XCB_VISIBILITY_NOTIFY:
		switch (((xcb_visibility_notify_event_t *) e)->state) {
		case XCB_VISIBILITY_PARTIALLY_OBSCURED: /* fall through */
		case XCB_VISIBILITY_UNOBSCURED:
			refresh = 1;
			break;
		}
		break;
	case XCB_EXPOSE:
		refresh = 1;
		break;
	case XCB_KEY_PRESS:
		handle_key_press((xcb_key_press_event_t *) e);
		rc = 1;
		break;
	case XCB_KEY_RELEASE:
		handle_key_release((xcb_key_press_event_t *) e);
		rc = 1;
		break;
	case XCB_BUTTON_PRESS:
		handle_button_press((xcb_button_press_event_t *) e);
		rc = 1;
		break;
	case XCB_BUTTON_RELEASE:
		game_touch(-1, -1);
		rc = 1;
		break;
	case XCB_RESIZE_REQUEST:
		handle_resize((xcb_resize_request_event_t *) e);
		break;
	default:
		dd("got message type %d", type);
	}

out:
#if 0
#ifdef HAVE_ADS
	if (panel_type_ == 0 && time_now - ad_time_ > 10) {
		panel_type_ = 2;
		ad_time_ = time_now;
	} else if (panel_type_ == 2 && time_now - ad_time_ > 10) {
		panel_type_ = 0;
		ad_time_ = time_now;
	}
#endif
#endif

	if (!pause_ && (refresh || poll)) {
		game_frame(panel_type_);
		eglSwapBuffers(egl_, surf_);
	}

	xcb_flush(dpy_);
	free(e);

	return rc;
}

static uint8_t create_window(uint16_t w, uint16_t h)
{
	uint32_t mask;
	uint32_t val[2];
	int num;
	EGLConfig cfg;
	xcb_window_t old_win = win_;

	static const EGLint attrs[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_DEPTH_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	static const EGLint ctx_attrs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	num = DefaultScreen(dpy_);

	if (!eglChooseConfig(egl_, attrs, &cfg, 1, &num)) {
		ee("eglChooseConfig() failed\n");
		return 0;
	}

	win_ = xcb_generate_id(dpy_);

	mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	val[0] = 0;
	val[1] = XCB_EVENT_MASK_EXPOSURE;
	val[1] |= XCB_EVENT_MASK_VISIBILITY_CHANGE;
	val[1] |= XCB_EVENT_MASK_KEY_PRESS;
	val[1] |= XCB_EVENT_MASK_KEY_RELEASE;
	val[1] |= XCB_EVENT_MASK_BUTTON_PRESS;
	val[1] |= XCB_EVENT_MASK_BUTTON_RELEASE;
	val[1] |= XCB_EVENT_MASK_POINTER_MOTION;
	val[1] |= XCB_EVENT_MASK_RESIZE_REDIRECT;

	xcb_create_window(dpy_, XCB_COPY_FROM_PARENT, win_, scr_->root, 0, 0,
	  w, h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, scr_->root_visual,
	  mask, val);

	xcb_change_property(dpy_, XCB_PROP_MODE_REPLACE, win_, XCB_ATOM_WM_NAME,
	  XCB_ATOM_STRING, 8, sizeof("opengl") - 1, "opengl");

	xcb_change_property(dpy_, XCB_PROP_MODE_REPLACE, win_, XCB_ATOM_WM_CLASS,
	  XCB_ATOM_STRING, 8, sizeof("opengl") - 1, "opengl");

	xcb_map_window(dpy_, win_);
	xcb_flush(dpy_);

	eglBindAPI(EGL_OPENGL_ES_API);

	if (!ctx_ && !(ctx_ = eglCreateContext(egl_, cfg, EGL_NO_CONTEXT, ctx_attrs))) {
		ee("Error: eglCreateContext failed\n");
		return 0;
	}

	if (surf_)
		eglDestroySurface(egl_, surf_);

	if (!(surf_ = eglCreateWindowSurface(egl_, cfg, win_, NULL))) {
		ee("Error: eglCreateWindowSurface failed\n");
		return 0;
	}

	if (!eglMakeCurrent(egl_, surf_, surf_, ctx_)) {
		ee("eglMakeCurrent() failed\n");
		return 0;
	}

	if (old_win != XCB_WINDOW_NONE) {
		xcb_destroy_window(dpy_, old_win);
		xcb_flush(dpy_);
	}

	return 1;
}

static uint8_t init_display(void)
{
	if (!(dpy_ = xcb_connect(NULL, NULL))) {
		ee("xcb_connect() failed\n");
		return 0;
	}

	if (!(syms_ = xcb_key_symbols_alloc(dpy_)))
		ee("xcb_key_symbols_alloc() failed\n");

	scr_ = xcb_setup_roots_iterator(xcb_get_setup(dpy_)).data;
	return 1;
}

#if 1
#ifndef WIDTH
#define WIDTH 540
#endif
#ifndef HEIGHT
#define HEIGHT 960
#endif
#else
#define WIDTH 240
#define HEIGHT 320
#endif

//#define HEIGHT 1005

int main(int argc, const char *argv[])
{
	EGLint major;
	EGLint minor;

	if (!(egl_ = eglGetDisplay(EGL_DEFAULT_DISPLAY))) {
		ee("eglGetDisplay() failed\n");
		return 1;
	}

	if (!eglInitialize(egl_, &major, &minor)) {
		ee("eglInitialize() failed\n");
		return 1;
	}

	ii("EGL_VERSION = %s\n", eglQueryString(egl_, EGL_VERSION));
	ii("EGL_VENDOR = %s\n", eglQueryString(egl_, EGL_VENDOR));
	ii("EGL_EXTENSIONS = %s\n", eglQueryString(egl_, EGL_EXTENSIONS));
	ii("EGL_CLIENT_APIS = %s\n", eglQueryString(egl_, EGL_CLIENT_APIS));

	if (!init_display())
		return 1;

	if (!create_window(WIDTH, HEIGHT))
		return 1;

	ii("created win %#x size (%d %d)\n", win_, (int) WIDTH, (int) HEIGHT);

	ii("GL_RENDERER = %s\n", (char *) glGetString(GL_RENDERER));
	ii("GL_VERSION = %s\n", (char *) glGetString(GL_VERSION));
	ii("GL_VENDOR = %s\n", (char *) glGetString(GL_VENDOR));
	ii("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));

	uint8_t flags = 0;
	panel_type_ = 0;

	mkdir(GAME_HOME, S_IRWXU);
	game_init(flags, GAME_HOME);
	game_open(NULL, GAME_HOME);

	while (!done_) {
		events(1);
		sleep_ms(16);
	}

	game_close();
	eglDestroyContext(egl_, ctx_);
	eglDestroySurface(egl_, surf_);
	eglTerminate(egl_);
	xcb_destroy_window(dpy_, win_);

	return 0;
}

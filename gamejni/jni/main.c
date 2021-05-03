#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <jni.h>

#include <EGL/egl.h>

#include <android/log.h>
#include <android_native_app_glue.h>

#include <rgu/gl.h>
#include <rgu/utils.h>
#include <game.h>

#undef TAG
#define TAG "rgfw"

#include <rgu/log.h>

struct engine_state {
	uint8_t val;
};

enum uint8_t {
	ENGINE_STOPPED,
	ENGINE_STARTED,
	ENGINE_PAUSED,
};

#define return_string_if(n, s) if (n == s) return #s

static inline const char *state2str(uint8_t state)
{
	return_string_if(state, ENGINE_STOPPED);
	return_string_if(state, ENGINE_STARTED);
	return_string_if(state, ENGINE_PAUSED);
	return "<nil>";
}

struct engine {
	struct android_app* app;
	int animating;
	EGLDisplay display;
	EGLSurface surface;
	EGLContext context;
	uint8_t state;
};

static struct engine engine_;

#define state_val() ((struct engine_state *) engine_.app->savedState)->val

static inline uint8_t saved_state(void)
{
	return state_val();
}

static void save_state(void)
{
	if (engine_.app->savedState) {
		state_val() = engine_.state;
	} else {
		struct engine_state *state = malloc(sizeof(*state));
		state->val = engine_.state;
		engine_.app->savedState = state;
		engine_.app->savedStateSize = sizeof(*state);
	}

	ii("saved state \033[1;33m%s\033[0m", state2str(saved_state()));
}

static void try_set_state(uint8_t state)
{
	if (engine_.app->savedState)
		engine_.state = saved_state();
	else
		engine_.state = state;

	ii("current state \033[1;33m%s\033[0m", state2str(engine_.state));
}

static void engine_init_display(void)
{
	engine_.display = EGL_NO_DISPLAY;

	EGLint format;
	EGLint configs_num;
	EGLConfig config;
	EGLSurface surface;
	EGLContext context;

	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(display, 0, 0);

	const EGLint attrs_norm[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16,
		EGL_STENCIL_SIZE, 0,
		EGL_NONE
	};

	const EGLint attrs_msaa[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16,
		EGL_STENCIL_SIZE, 0,
		EGL_SAMPLE_BUFFERS, 1,
		EGL_SAMPLES, 2,
		EGL_NONE
	};

	EGLConfig configs[1];

	/* try MSAA first */
	eglChooseConfig(display, attrs_msaa, configs, 1, &configs_num);

	if (configs_num) {
		ii("use MSAA");
		config = configs[0];
	} else {
		eglChooseConfig(display, attrs_norm, configs, 1, &configs_num);
		if (configs_num) {
			ii("do not use MSAA");
			config = configs[0];
		} else {
			eglChooseConfig(display, attrs_norm, configs, 1, &configs_num);

			if (!configs_num) {
				ee("no egl configs available");
				return;
			}

			ii("use first available EGL config");
			config = configs[0];
		}
	}

	eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

	const EGLint attrs_gles2[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	context = eglCreateContext(display, config, EGL_NO_CONTEXT, attrs_gles2);
	surface = eglCreateWindowSurface(display, config, engine_.app->window, NULL);

	if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
		ee("unable to init surface");
		return;
	}

	engine_.display = display;
	engine_.context = context;
	engine_.surface = surface;
	engine_.animating = 1;

	const int glinfo[] = {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS};

	for (uint8_t i = 0; i < ARRAY_SIZE(glinfo); ++i) {
		const GLubyte *str = glGetString(glinfo[i]);
		ii("GLES info: %s", str);
	}

	glEnable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	EGLint w;
	EGLint h;
	eglQuerySurface(display, surface, EGL_WIDTH, &w);
	eglQuerySurface(display, surface, EGL_HEIGHT, &h);
	ii("viewport wh (%d %d) is ready", w, h);
}

static void engine_draw_frame(void)
{
	game_frame(0);
	eglSwapBuffers(engine_.display, engine_.surface);
}

static void engine_init_window(void)
{
	if (!engine_.app->window)
		return;

	engine_init_display();
	ANativeActivity *activity = engine_.app->activity;

	if (engine_.state == ENGINE_PAUSED)
		game_resume();
	else
		game_open(activity->assetManager, activity->internalDataPath);

	engine_.state = ENGINE_STARTED;
	engine_draw_frame();
}

static void engine_term_display(void)
{
	if (engine_.display != EGL_NO_DISPLAY) {
		eglMakeCurrent(engine_.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

		if (engine_.context != EGL_NO_CONTEXT)
			eglDestroyContext(engine_.display, engine_.context);

		if (engine_.surface != EGL_NO_SURFACE)
			eglDestroySurface(engine_.display, engine_.surface);

		eglTerminate(engine_.display);
	}

	engine_.animating = 0;
	engine_.display = EGL_NO_DISPLAY;
	engine_.context = EGL_NO_CONTEXT;
	engine_.surface = EGL_NO_SURFACE;
}

static int32_t handle_motion_event(AInputEvent *event)
{
	if (game_input() == GAME_INPUT_CONTROLS) {
		dd("\033[1;33mGAME_INPUT_CONTROLS\033[0m\n");
		int32_t x = AMotionEvent_getX(event, 0);
		int32_t y = AMotionEvent_getY(event, 0);
		game_touch(x, y);
		engine_.state = ENGINE_STARTED;
		return 1;
	} else {
		dd("\033[1;33mGAME_INPUT_MENU\033[0m\n");
		uint32_t type = AMotionEvent_getAction(event);
		type &= AMOTION_EVENT_ACTION_MASK;

		if (type == AMOTION_EVENT_ACTION_DOWN ||
		  type == AMOTION_EVENT_ACTION_POINTER_DOWN) {
			int32_t x = AMotionEvent_getX(event, 0);
			int32_t y = AMotionEvent_getY(event, 0);
			game_touch(x, y);
			engine_.state = ENGINE_STARTED;
			return 1;
		}

		return 0;
	}
}

static int32_t engine_handle_input(struct android_app* app, AInputEvent* event)
{
	int type = AInputEvent_getType(event);

	if (type == AINPUT_EVENT_TYPE_KEY) {
		int code = AKeyEvent_getKeyCode(event);
		if (code == AKEYCODE_BACK)
			return 1;
	} else if (type == AINPUT_EVENT_TYPE_MOTION) {
		engine_.animating = 1;
		return handle_motion_event(event);
	}

	return 0;
}

static void engine_handle_cmd(struct android_app* app, int32_t cmd)
{
	if (cmd != APP_CMD_INIT_WINDOW && engine_.display == EGL_NO_DISPLAY)
		return;

	switch (cmd) {
	case APP_CMD_START:
		ii("APP_CMD_START");
		break;
	case APP_CMD_RESUME:
		ii("APP_CMD_RESUME | current state \033[0;32m%s\033[0m",
		  state2str(engine_.state));
		game_resume();
		break;
	case APP_CMD_PAUSE:
		ii("APP_CMD_PAUSE");
		engine_.state = ENGINE_PAUSED;
		save_state();
		break;
	case APP_CMD_STOP:
		ii("APP_CMD_STOP");
		try_set_state(ENGINE_PAUSED);
		break;
	case APP_CMD_DESTROY:
		ii("APP_CMD_DESTROY");
		break;
	case APP_CMD_SAVE_STATE:
		ii("APP_CMD_SAVE_STATE | current state \033[0;32m%s\033[0m",
		  state2str(engine_.state));
		save_state();
		break;
	case APP_CMD_INIT_WINDOW:
		ii("APP_CMD_INIT_WINDOW | current state \033[0;32m%s\033[0m",
		  state2str(engine_.state));
		engine_init_window();
		break;
	case APP_CMD_TERM_WINDOW:
		ii("APP_CMD_TERM_WINDOW | current state \033[0;32m%s\033[0m",
		  state2str(engine_.state));
		game_clean();
		engine_term_display();
		break;
	case APP_CMD_GAINED_FOCUS:
		ii("APP_CMD_GAINED_FOCUS | current state \033[0;32m%s\033[0m",
		  state2str(engine_.state));
		engine_.animating = 1;
		engine_.state = ENGINE_STARTED;
		break;
	case APP_CMD_LOST_FOCUS:
		ii("APP_CMD_LOST_FOCUS | current state \033[0;32m%s\033[0m",
		  state2str(engine_.state));
		engine_.animating = 0;
		engine_.state = ENGINE_PAUSED;
		break;
	}
}

static uint8_t handle_events(struct android_app *app)
{
	int events;
	struct android_poll_source *source;

	while ((ALooper_pollAll(engine_.animating ? 0 : -1, NULL, &events,
	  (void **) &source)) >= 0) {
		if (source)
			source->process(app, source);

		if (app->destroyRequested) {
			ii("app stopped");
			game_close();
			engine_.state = ENGINE_STOPPED;
			engine_term_display();
			return 0;
		} else if (engine_.state == ENGINE_PAUSED) {
			game_pause();
			engine_draw_frame();
		}
	}

	return 1;
}

static uint8_t java_failed(JNIEnv *env)
{
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env); /* writes to logcat */
		(*env)->ExceptionClear(env);
		return 1;
	}

	return 0;
}

static void setup_screen(void)
{
	JNIEnv *env = NULL;
	JavaVM *vm = engine_.app->activity->vm;

	(*vm)->AttachCurrentThread(vm, &env, NULL);

	jobject app_obj = engine_.app->activity->clazz;
	jclass app_class = (*env)->FindClass(env,
	  "android/app/NativeActivity");
	jmethodID app_method = (*env)->GetMethodID(env, app_class,
	  "getWindow", "()Landroid/view/Window;");

	jobject win_obj = (*env)->CallObjectMethod(env, app_obj, app_method);
	jclass win_class = (*env)->FindClass(env, "android/view/Window");
	jmethodID win_method = (*env)->GetMethodID(env, win_class,
	  "getDecorView", "()Landroid/view/View;");

	jobject view_obj = (*env)->CallObjectMethod(env, win_obj, win_method);
	jclass view_class = (*env)->FindClass(env, "android/view/View");
	jmethodID view_method = (*env)->GetMethodID(env, view_class,
	  "setSystemUiVisibility", "(I)V");

	jfieldID layout_id = (*env)->GetStaticFieldID(env, view_class,
	  "SYSTEM_UI_FLAG_LAYOUT_STABLE", "I");
	jfieldID layoutnav_id = (*env)->GetStaticFieldID(env, view_class,
	  "SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION", "I");
	jfieldID layoutfs_id = (*env)->GetStaticFieldID(env, view_class,
	  "SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN", "I");
	jfieldID navbar_id = (*env)->GetStaticFieldID(env, view_class,
	  "SYSTEM_UI_FLAG_HIDE_NAVIGATION", "I");
	jfieldID fullscreen_id = (*env)->GetStaticFieldID(env, view_class,
	  "SYSTEM_UI_FLAG_FULLSCREEN", "I");
	jfieldID immersive_id = (*env)->GetStaticFieldID(env, view_class,
	  "SYSTEM_UI_FLAG_IMMERSIVE_STICKY", "I");

	const int layout_flag = (*env)->GetStaticIntField(env, view_class,
	  layout_id);
	const int layoutnav_flag = (*env)->GetStaticIntField(env, view_class,
	  layoutnav_id);
	const int layoutfs_flag = (*env)->GetStaticIntField(env, view_class,
	  layoutfs_id);
	const int navbar_flag = (*env)->GetStaticIntField(env, view_class,
	  navbar_id);
	const int fullscreen_flag = (*env)->GetStaticIntField(env, view_class,
	  fullscreen_id);
	const int immersive_flag = (*env)->GetStaticIntField(env, view_class,
	  immersive_id);
	const int flags = layout_flag | layoutnav_flag | layoutfs_flag |
		navbar_flag | fullscreen_flag | immersive_flag;

	(*env)->CallVoidMethod(env, view_obj, view_method, flags);

	app_method = (*env)->GetMethodID(env, app_class,
	  "setRequestedOrientation", "(I)V");

/* https://developer.android.com/reference/android/content/pm/ActivityInfo#SCREEN_ORIENTATION_PORTRAIT */
#define SCREEN_ORIENTATION_PORTRAIT 1

	(*env)->CallVoidMethod(env, app_obj, app_method,
	  SCREEN_ORIENTATION_PORTRAIT);

	(*vm)->DetachCurrentThread(vm);
}

static void handle_uri(const char *uri, const char *action)
{
	JNIEnv *env = NULL;
	JavaVM *vm = engine_.app->activity->vm;

	(*vm)->AttachCurrentThread(vm, &env, NULL);

	jstring uri_string = (*env)->NewStringUTF(env, uri);
	jclass uri_class = (*env)->FindClass(env, "android/net/Uri");
	jmethodID uri_parse = (*env)->GetStaticMethodID(env, uri_class,
	  "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
	jobject uri_object = (*env)->CallStaticObjectMethod(env, uri_class,
	  uri_parse, uri_string);
	jclass intent_class = (*env)->FindClass(env, "android/content/Intent");
	jfieldID action_view_id = (*env)->GetStaticFieldID(env, intent_class,
	  action, "Ljava/lang/String;");
	jobject action_view = (*env)->GetStaticObjectField(env, intent_class,
	  action_view_id);
	jmethodID new_intent = (*env)->GetMethodID(env, intent_class, "<init>",
	  "(Ljava/lang/String;Landroid/net/Uri;)V");
	jobject intent = (*env)->AllocObject(env, intent_class);
	(*env)->CallVoidMethod(env, intent, new_intent, action_view,
	  uri_object);
	jclass activity_class = (*env)->FindClass(env, "android/app/Activity");
	jmethodID start_activity = (*env)->GetMethodID(env, activity_class,
	  "startActivity", "(Landroid/content/Intent;)V");
	(*env)->CallVoidMethod(env, engine_.app->activity->clazz,
	  start_activity, intent);
	(*vm)->DetachCurrentThread(vm);
}

char *engine_shared_dir(const char *appname)
{
	char *ret;
	JNIEnv *env = NULL;
	JavaVM *vm = engine_.app->activity->vm;

	(*vm)->AttachCurrentThread(vm, &env, NULL);

	jobject app_obj = engine_.app->activity->clazz;
	jclass app_class = (*env)->FindClass(env,
	  "android/app/NativeActivity");
	jmethodID app_method = (*env)->GetMethodID(env, app_class,
	  "getPackageManager", "()Landroid/content/pm/PackageManager;");

	jobject pm_obj = (*env)->CallObjectMethod(env, app_obj, app_method);
	jclass pm_class = (*env)->GetObjectClass(env, pm_obj);
	jmethodID pm_method = (*env)->GetMethodID(env, pm_class,
	  "getApplicationInfo",
	  "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;");

	jint flags = 0x00000040;
	jobject ai_obj = (*env)->CallObjectMethod(env, pm_obj, pm_method,
	  (*env)->NewStringUTF(env, appname), flags);

	if (java_failed(env) || !ai_obj) {
		ret = NULL;
		goto out;
	}

	jclass ai_class = (*env)->GetObjectClass(env, ai_obj);

	jfieldID dd_field = (*env)->GetFieldID(env, ai_class, "dataDir",
	  "Ljava/lang/String;");
	jobject dd_obj = (*env)->GetObjectField(env, ai_obj, dd_field);
	jclass dd_class = (*env)->GetObjectClass(env, dd_obj);
	jmethodID dd_method = (*env)->GetMethodID(env, dd_class, "toString",
	  "()Ljava/lang/String;");

	jstring dir_str = (*env)->CallObjectMethod(env, dd_obj, dd_method);
	const char *ptr = (*env)->GetStringUTFChars(env, dir_str, NULL);

	if (!ptr) {
		ret = NULL;
		goto out;
	}

	ret = calloc(1, strlen(ptr) + 1);
	sprintf(ret, "%s/files/", ptr);
	(*env)->ReleaseStringUTFChars(env, dir_str, ptr);

out:
	(*vm)->DetachCurrentThread(vm);
	return ret;
}

void engine_open_uri(const char *uri)
{
	handle_uri(uri, "ACTION_VIEW");
}

void engine_delete_app(const char *uri)
{
	handle_uri(uri, "ACTION_DELETE");
}

void android_main(struct android_app *app)
{
	engine_.display = EGL_NO_DISPLAY;
	app->userData = &engine_;
	app->onAppCmd = engine_handle_cmd;
	app->onInputEvent = engine_handle_input;
	engine_.app = app;
	game_init(0, NULL);
	try_set_state(ENGINE_STOPPED);
	setup_screen();

	ii("app started %s", state2str(engine_.state));

	while (handle_events(app)) {
		if (engine_.animating && engine_.display != EGL_NO_DISPLAY)
			engine_draw_frame();
	}

	ii("app exited");
}

#include "video.h"

#include "game.h"
#include "settings.h"

#include "RmlUi_Include_GL3.h"

#include <stdio.h>

static GLuint video_program, video_vao, video_texture;

static const char *video_vs =
	"#version 330 core\n"
	"out vec2 uv;\n"
	"void main()\n"
	"{\n"
	"	vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
	"	uv = vec2(p.x, 1.0 - p.y);\n"
	"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const char *video_fs =
	"#version 330 core\n"
	"in vec2 uv;\n"
	"out vec4 color;\n"
	"uniform sampler2D frame;\n"
	"void main()\n"
	"{\n"
	"	color = vec4(texture(frame, uv).rgb, 1.0);\n"
	"}\n";

static GLuint video_shader(GLenum type, const char *source, const char *what)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	glShaderSource(s, 1, &source, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if(!ok)
	{
		char log[1024];
		glGetShaderInfoLog(s, sizeof log, NULL, log);
		fprintf(stderr, "video: %s shader: %s\n", what, log);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

int sdl_video_init(void)
{
	int v = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
	GLuint vs, fs;
	struct SdlView view;
	GLint ok = 0;
	if(GLAD_VERSION_MAJOR(v) * 10 + GLAD_VERSION_MINOR(v) < 33)
	{
		fprintf(stderr, "video: OpenGL 3.3 needed, the context has %d.%d\n", GLAD_VERSION_MAJOR(v), GLAD_VERSION_MINOR(v));
		return -1;
	}
	if(sdl_video_view(&view))
		return -1;
	vs = video_shader(GL_VERTEX_SHADER, video_vs, "vertex");
	fs = video_shader(GL_FRAGMENT_SHADER, video_fs, "fragment");
	if(!vs || !fs)
		return -1;
	video_program = glCreateProgram();
	glAttachShader(video_program, vs);
	glAttachShader(video_program, fs);
	glLinkProgram(video_program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glGetProgramiv(video_program, GL_LINK_STATUS, &ok);
	if(!ok)
	{
		char log[1024];
		glGetProgramInfoLog(video_program, sizeof log, NULL, log);
		fprintf(stderr, "video: link: %s\n", log);
		return -1;
	}
	glGenVertexArrays(1, &video_vao);
	glGenTextures(1, &video_texture);
	glBindTexture(GL_TEXTURE_2D, video_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, view.w, view.h, 0, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if(glGetError() != GL_NO_ERROR)
	{
		fprintf(stderr, "video: the frame texture\n");
		return -1;
	}
	return 0;
}

int sdl_video_view(struct SdlView *v)
{
	const struct GameView *g = &game_def.view;
	if(!g->w)
	{
		v->x = 0;
		v->y = 0;
		v->w = AMIGA_FRAME_W;
		v->h = AMIGA_FRAME_H;
		return 0;
	}
	v->x = g->x - AMIGA_FRAME_DIW_X0;
	v->y = g->y - AMIGA_FRAME_LINE0;
	v->w = g->w;
	v->h = g->h;
	return v->x < 0 || v->y < 0 || v->w <= 0 || v->h <= 0 || v->x + v->w > AMIGA_FRAME_STRIDE || v->y + v->h > AMIGA_FRAME_H ? -1 : 0;
}

int sdl_video_rect(SDL_Window *window, struct SdlVideoRect *r)
{
	int pw = 0, ph = 0, w, h, forced = sdl_scale_forced();
	if(sdl_video_view(&r->view))
		return -1;
	if(!SDL_GetWindowSizeInPixels(window, &pw, &ph) || pw <= 0 || ph <= 0)
		return -1;
	if(forced || sdl_setting(SdlSettingScale) == SdlScaleInteger)
	{
		int n = pw / r->view.w < ph / r->view.h ? pw / r->view.w : ph / r->view.h;
		if(n < 1)
			n = 1;
		if(forced && forced < n)
			n = forced;
		w = r->view.w * n;
		h = r->view.h * n;
	}
	else if((long long)pw * r->view.h <= (long long)ph * r->view.w)
	{
		w = pw;
		h = pw * r->view.h / r->view.w;
	}
	else
	{
		h = ph;
		w = ph * r->view.w / r->view.h;
	}
	r->x = (pw - w) / 2;
	r->y = (ph - h) / 2;
	r->w = w;
	r->h = h;
	r->window_w = pw;
	r->window_h = ph;
	return 0;
}

void sdl_video_draw(SDL_Window *window, const uint16_t frame[AMIGA_FRAME_H][AMIGA_FRAME_STRIDE])
{
	struct SdlVideoRect r;
	if(sdl_video_rect(window, &r))
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glViewport(0, 0, r.window_w, r.window_h);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glViewport(r.x, r.window_h - r.y - r.h, r.w, r.h);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, AMIGA_FRAME_STRIDE);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, video_texture);
	/* $0RGB is BGRA 4444 reversed: B in bits 0-3 up to an alpha nibble the shader ignores. */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r.view.w, r.view.h, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV, &frame[r.view.y][r.view.x]);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glUseProgram(video_program);
	glBindVertexArray(video_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void sdl_video_quit(void)
{
	glDeleteTextures(1, &video_texture);
	glDeleteVertexArrays(1, &video_vao);
	glDeleteProgram(video_program);
}

#include "video.h"

#include "game.h"
#include "menu.h"
#include "settings.h"

#include "RmlUi_Include_GL3.h"

#include <stdio.h>

static GLuint video_program, video_crt_program, video_vao, video_vbo, video_texture, video_crt_texture;
/* crt-lottes's source: one texel per lores pixel and line of the view. */
static uint32_t video_crt_pixels[AMIGA_FRAME_ROWS * AMIGA_FRAME_STRIDE];

static const char *video_vs =
	"#version 330 core\n"
	"layout(location = 0) in vec2 position;\n"
	"layout(location = 1) in vec2 coord;\n"
	"out vec2 uv;\n"
	"void main()\n"
	"{\n"
	"	uv = coord;\n"
	"	gl_Position = vec4(position, 0.0, 1.0);\n"
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

/* `lengths` as glShaderSource takes it: NULL when every source ends in a NUL. */
static GLuint video_shader(GLenum type, GLsizei count, const char *const *sources, const GLint *lengths, const char *what)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	glShaderSource(s, count, sources, lengths);
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

/* Takes both shaders, 0 for one that failed. */
static GLuint video_link(GLuint vs, GLuint fs, const char *what)
{
	GLuint p;
	GLint ok = 0;
	if(!vs || !fs)
	{
		glDeleteShader(vs);
		glDeleteShader(fs);
		return 0;
	}
	p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glLinkProgram(p);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if(!ok)
	{
		char log[1024];
		glGetProgramInfoLog(p, sizeof log, NULL, log);
		fprintf(stderr, "video: %s link: %s\n", what, log);
		glDeleteProgram(p);
		return 0;
	}
	return p;
}

/* crt-lottes (frontend/shaders/crt-lottes.glsl), its fragment stage after video_vs. 0 when it
 * does not build. */
static GLuint video_crt_build(const struct SdlView *view)
{
	const struct SdlAsset *a = sdl_video_shaders;
	const char *sources[2] = {"#version 330 core\n", (const char *)a->data};
	const GLint lengths[2] = {-1, (GLint)a->size};
	GLuint p = video_link(video_shader(GL_VERTEX_SHADER, 1, &video_vs, NULL, "crt-lottes vertex"),
		video_shader(GL_FRAGMENT_SHADER, 2, sources, lengths, "crt-lottes fragment"), "crt-lottes");
	if(!p)
		return 0;
	glUseProgram(p);
	glUniform2f(glGetUniformLocation(p, "SourceSize"), (float)view->w, (float)view->h);
	glUseProgram(0);
	return p;
}

int sdl_video_init(void)
{
	/* Triangle strip: clip-space corner, then texture coordinate; the frame's first row is v = 0. */
	static const float quad[16] = {
		-1, -1, 0, 1,
		1, -1, 1, 1,
		-1, 1, 0, 0,
		1, 1, 1, 0};
	int v = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
	struct SdlView view;
	if(GLAD_VERSION_MAJOR(v) * 10 + GLAD_VERSION_MINOR(v) < 33)
	{
		fprintf(stderr, "video: OpenGL 3.3 needed, the context has %d.%d\n", GLAD_VERSION_MAJOR(v), GLAD_VERSION_MINOR(v));
		return -1;
	}
	if(sdl_video_view(&view))
		return -1;
	video_program = video_link(video_shader(GL_VERTEX_SHADER, 1, &video_vs, NULL, "vertex"),
		video_shader(GL_FRAGMENT_SHADER, 1, &video_fs, NULL, "fragment"), "frame");
	if(!video_program)
		return -1;
	video_crt_program = video_crt_build(&view);
	if(!video_crt_program)
		fprintf(stderr, "video: the setting crt draws the plain picture\n");
	glGenVertexArrays(1, &video_vao);
	glBindVertexArray(video_vao);
	glGenBuffers(1, &video_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, video_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);
	glGenTextures(1, &video_texture);
	glBindTexture(GL_TEXTURE_2D, video_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, view.w * AMIGA_FRAME_XS, view.h * AMIGA_FRAME_YS, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glGenTextures(1, &video_crt_texture);
	glBindTexture(GL_TEXTURE_2D, video_crt_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, view.w, view.h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	/* Black past the edges: crt-lottes's curved corners sample there. */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	if(glGetError() != GL_NO_ERROR)
	{
		fprintf(stderr, "video: the frame texture\n");
		return -1;
	}
	return 0;
}

/* Each lores pixel and line of the view as the mean of its AMIGA_FRAME_XS × AMIGA_FRAME_YS
 * frame-buffer pixels, rounded to nearest: a lores, non-interlaced pixel keeps its colour. */
static void video_crt_source(const struct Amiga *m, const struct SdlView *v)
{
	int x, y, i, j;
	for(y = 0; y < v->h; y++)
		for(x = 0; x < v->w; x++)
		{
			uint32_t r = 0, g = 0, b = 0;
			for(j = 0; j < AMIGA_FRAME_YS; j++)
				for(i = 0; i < AMIGA_FRAME_XS; i++)
				{
					uint32_t c = m->frame[(v->y + y) * AMIGA_FRAME_YS + j][(v->x + x) * AMIGA_FRAME_XS + i];
					r += (c >> 16) & 0xFF;
					g += (c >> 8) & 0xFF;
					b += c & 0xFF;
				}
			r = (r + AMIGA_FRAME_XS * AMIGA_FRAME_YS / 2) / (AMIGA_FRAME_XS * AMIGA_FRAME_YS);
			g = (g + AMIGA_FRAME_XS * AMIGA_FRAME_YS / 2) / (AMIGA_FRAME_XS * AMIGA_FRAME_YS);
			b = (b + AMIGA_FRAME_XS * AMIGA_FRAME_YS / 2) / (AMIGA_FRAME_XS * AMIGA_FRAME_YS);
			video_crt_pixels[y * v->w + x] = r << 16 | g << 8 | b;
		}
}

int sdl_video_view(struct SdlView *v)
{
	const struct GameView *g = &game_def.view;
	if(!g->w)
	{
		v->x = 0;
		v->y = AMIGA_FRAME_CAPTURE_ROW;
		v->w = AMIGA_FRAME_W;
		v->h = AMIGA_FRAME_H;
		return 0;
	}
	v->x = g->x - AMIGA_FRAME_DIW_X0;
	v->y = g->y - AMIGA_DISPLAY_LINE0;
	v->w = g->w;
	v->h = g->h;
	return v->x < 0 || v->y < 0 || v->w <= 0 || v->h <= 0 || v->x + v->w > AMIGA_FRAME_STRIDE || v->y + v->h > AMIGA_FRAME_ROWS ? -1 : 0;
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

void sdl_video_draw(SDL_Window *window, const struct Amiga *m)
{
	struct SdlVideoRect r;
	int crt;
	if(sdl_video_rect(window, &r))
		return;
	crt = video_crt_program && sdl_setting(SdlSettingCrt);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glViewport(0, 0, r.window_w, r.window_h);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glViewport(r.x, r.window_h - r.y - r.h, r.w, r.h);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glActiveTexture(GL_TEXTURE0);
	/* 0x00RRGGBB is BGRA 8888 reversed: B in bits 0-7 up to an alpha byte the shaders ignore. */
	if(crt)
	{
		video_crt_source(m, &r.view);
		glBindTexture(GL_TEXTURE_2D, video_crt_texture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r.view.w, r.view.h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, video_crt_pixels);
	}
	else
	{
		glPixelStorei(GL_UNPACK_ROW_LENGTH, AMIGA_FRAME_STRIDE * AMIGA_FRAME_XS);
		glBindTexture(GL_TEXTURE_2D, video_texture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r.view.w * AMIGA_FRAME_XS, r.view.h * AMIGA_FRAME_YS, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
			&m->frame[r.view.y * AMIGA_FRAME_YS][r.view.x * AMIGA_FRAME_XS]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
	glUseProgram(crt ? video_crt_program : video_program);
	glBindVertexArray(video_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void sdl_video_quit(void)
{
	glDeleteTextures(1, &video_crt_texture);
	glDeleteTextures(1, &video_texture);
	glDeleteBuffers(1, &video_vbo);
	glDeleteVertexArrays(1, &video_vao);
	glDeleteProgram(video_crt_program);
	glDeleteProgram(video_program);
}

#include "video.h"

#include "game.h"
#include "menu.h"
#include "settings.h"

#include "RmlUi_Include_GL3.h"

#include <stdio.h>
#include <string.h>

static GLuint video_program, video_crt_program, video_vao, video_vbo, video_texture;

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

/* crt-lottes keeps the defaults of its #pragma parameter lines, except these. */
static const struct { const char *name; float value; } video_crt_values[] = {
	{"shadowMask", 4.0f},
	{"brightBoost", 1.3f},
};

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

/* Takes both shaders, 0 for one that failed. Binds libretro's attribute names to the quad's
 * locations. */
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
	glBindAttribLocation(p, 0, "VertexCoord");
	glBindAttribLocation(p, 1, "TexCoord");
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

/* Sets each `#pragma parameter <name> "<label>" <default> <min> <max> <step>` uniform of the
 * bound program to its default. */
static void video_crt_defaults(GLuint p, const struct SdlAsset *a)
{
	static const char pragma[] = "#pragma parameter ";
	const char *s = (const char *)a->data, *end = s + a->size;
	while(s < end)
	{
		const char *eol = memchr(s, '\n', (size_t)(end - s));
		size_t n = (size_t)((eol ? eol : end) - s);
		char line[256], name[64];
		float v;
		if(n < sizeof line && n > sizeof pragma - 1 && !memcmp(s, pragma, sizeof pragma - 1))
		{
			memcpy(line, s, n);
			line[n] = 0;
			if(sscanf(line, "#pragma parameter %63s \"%*[^\"]\" %f", name, &v) == 2)
				glUniform1f(glGetUniformLocation(p, name), v);
		}
		s = eol ? eol + 1 : end;
	}
}

/* crt-lottes, as a libretro GLSL shader: one source for both stages, chosen by VERTEX or
 * FRAGMENT, its parameters as uniforms. 0 when it does not build. */
static GLuint video_crt_build(const struct SdlView *view)
{
	static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const struct SdlAsset *a = sdl_video_shaders;
	const char *sources[2] = {"#version 330 core\n#define VERTEX\n#define PARAMETER_UNIFORM\n", (const char *)a->data};
	const GLint lengths[2] = {-1, (GLint)a->size};
	GLuint vs, fs, p;
	size_t i;
	vs = video_shader(GL_VERTEX_SHADER, 2, sources, lengths, "crt-lottes vertex");
	sources[0] = "#version 330 core\n#define FRAGMENT\n#define PARAMETER_UNIFORM\n";
	fs = video_shader(GL_FRAGMENT_SHADER, 2, sources, lengths, "crt-lottes fragment");
	p = video_link(vs, fs, "crt-lottes");
	if(!p)
		return 0;
	glUseProgram(p);
	video_crt_defaults(p, a);
	for(i = 0; i < sizeof video_crt_values / sizeof video_crt_values[0]; i++)
		glUniform1f(glGetUniformLocation(p, video_crt_values[i].name), video_crt_values[i].value);
	glUniformMatrix4fv(glGetUniformLocation(p, "MVPMatrix"), 1, GL_FALSE, identity);
	glUniform2f(glGetUniformLocation(p, "TextureSize"), (float)view->w, (float)view->h);
	glUniform2f(glGetUniformLocation(p, "InputSize"), (float)view->w, (float)view->h);
	glUniform1i(glGetUniformLocation(p, "Texture"), 0);
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

void sdl_video_draw(SDL_Window *window, const uint32_t frame[AMIGA_FRAME_ROWS][AMIGA_FRAME_STRIDE])
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
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, AMIGA_FRAME_STRIDE);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, video_texture);
	/* 0x00RRGGBB is BGRA 8888 reversed: B in bits 0-7 up to an alpha byte the shader ignores. */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r.view.w, r.view.h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &frame[r.view.y][r.view.x]);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glUseProgram(video_crt_program && sdl_setting(SdlSettingCrt) ? video_crt_program : video_program);
	glBindVertexArray(video_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void sdl_video_quit(void)
{
	glDeleteTextures(1, &video_texture);
	glDeleteBuffers(1, &video_vbo);
	glDeleteVertexArrays(1, &video_vao);
	glDeleteProgram(video_crt_program);
	glDeleteProgram(video_program);
}

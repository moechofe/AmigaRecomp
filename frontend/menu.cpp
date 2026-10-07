#include "menu.h"

#include "disks.h"
#include "game.h"

#include <RmlUi/Core.h>

#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_GL3.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

uint32_t sdl_menu_path_event;

namespace {

constexpr int menu_messages_max = 6;
constexpr uint64_t menu_banner_ms = 10000;

struct MenuFile
{
	const uint8_t *data;
	size_t size, pos;
};

class SdlMenuFiles : public Rml::FileInterface
{
public:
	Rml::FileHandle Open(const Rml::String &path) override
	{
		size_t slash = path.find_last_of("/\\");
		Rml::String name = slash == Rml::String::npos ? path : path.substr(slash + 1);
		const SdlAsset *a = find(sdl_menu_game, name);
		if(!a)
			a = find(sdl_menu_shared, name);
		if(!a)
			return 0;
		return (Rml::FileHandle) new MenuFile{a->data, a->size, 0};
	}
	void Close(Rml::FileHandle file) override { delete (MenuFile *)file; }
	size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
	{
		MenuFile *f = (MenuFile *)file;
		size_t n = f->size - f->pos < size ? f->size - f->pos : size;
		if(n)
			std::memcpy(buffer, f->data + f->pos, n);
		f->pos += n;
		return n;
	}
	bool Seek(Rml::FileHandle file, long offset, int origin) override
	{
		MenuFile *f = (MenuFile *)file;
		long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)f->pos : origin == SEEK_END ? (long)f->size : -1;
		long to;
		if(base < 0)
			return false;
		to = base + offset;
		if(to < 0 || to > (long)f->size)
			return false;
		f->pos = (size_t)to;
		return true;
	}
	size_t Tell(Rml::FileHandle file) override { return ((MenuFile *)file)->pos; }

private:
	static const SdlAsset *find(const SdlAsset *table, const Rml::String &name)
	{
		for(; table->name; table++)
			if(name == table->name)
				return table;
		return 0;
	}
};

struct DiskRow
{
	Rml::String label, state, source;
};

struct MenuModel
{
	Rml::String title;
	bool open = false;
	bool booted = false;
	std::vector<DiskRow> disks;
	std::vector<Rml::String> messages;
	Rml::String banner;
	bool banner_on = false;
};

struct Menu
{
	SdlMenuFiles files;
	SystemInterface_SDL *system = nullptr;
	RenderInterface_GL3 *render = nullptr;
	Rml::Context *context = nullptr;
	Rml::DataModelHandle handle;
	MenuModel model;
	SDL_Window *window = nullptr;
	uint64_t banner_start = 0;
	bool rml_up = false;
};

Menu *menu;

void menu_dialog_done(void *, const char *const *list, int)
{
	if(!list)
	{
		std::fprintf(stderr, "file dialog: %s\n", SDL_GetError());
		return;
	}
	for(; *list; list++)
	{
		SDL_Event e;
		SDL_zero(e);
		e.type = sdl_menu_path_event;
		e.user.data1 = SDL_strdup(*list);
		if(!e.user.data1 || !SDL_PushEvent(&e))
			SDL_free(e.user.data1);
	}
}

void menu_open_file()
{
	static const SDL_DialogFileFilter filter = {"Amiga disk images", "adf;adz;gz;zip"};
	SDL_ShowOpenFileDialog(menu_dialog_done, nullptr, menu->window, &filter, 1, nullptr, true);
}

int menu_fail(const char *what)
{
	std::fprintf(stderr, "menu: cannot load %s\n", what);
	return -1;
}

} // namespace

int sdl_menu_init(SDL_Window *window)
{
	int w = 0, h = 0;
	Rml::ElementDocument *doc;
	Rml::DataModelConstructor ctor;

	menu = new Menu;
	menu->window = window;
	sdl_menu_path_event = SDL_RegisterEvents(1);
	menu->system = new SystemInterface_SDL(window);
	menu->render = new RenderInterface_GL3;
	if(!*menu->render)
		return menu_fail("the renderer");
	Rml::SetFileInterface(&menu->files);
	Rml::SetSystemInterface(menu->system);
	Rml::SetRenderInterface(menu->render);
	if(!Rml::Initialise())
		return menu_fail("RmlUi");
	menu->rml_up = true;
	if(!Rml::LoadFontFace("LatoLatin-Regular.ttf"))
		return menu_fail("LatoLatin-Regular.ttf");
	if(!Rml::LoadFontFace("LatoLatin-Bold.ttf"))
		return menu_fail("LatoLatin-Bold.ttf");
	SDL_GetWindowSizeInPixels(window, &w, &h);
	menu->context = Rml::CreateContext("menu", Rml::Vector2i(w, h));
	if(!menu->context)
		return menu_fail("the context");
	menu->context->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(window));

	ctor = menu->context->CreateDataModel("menu");
	if(!ctor)
		return menu_fail("the data model");
	if(auto row = ctor.RegisterStruct<DiskRow>())
	{
		row.RegisterMember("label", &DiskRow::label);
		row.RegisterMember("state", &DiskRow::state);
		row.RegisterMember("source", &DiskRow::source);
	}
	ctor.RegisterArray<std::vector<DiskRow>>();
	ctor.RegisterArray<std::vector<Rml::String>>();
	ctor.Bind("title", &menu->model.title);
	ctor.Bind("open", &menu->model.open);
	ctor.Bind("booted", &menu->model.booted);
	ctor.Bind("disks", &menu->model.disks);
	ctor.Bind("messages", &menu->model.messages);
	ctor.Bind("banner", &menu->model.banner);
	ctor.Bind("banner_on", &menu->model.banner_on);
	ctor.BindEventCallback("open_file", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { menu_open_file(); });
	ctor.BindEventCallback("close", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { sdl_menu_show(0); });
	menu->handle = ctor.GetModelHandle();
	menu->model.title = game_def.title;

	doc = menu->context->LoadDocument("menu.rml");
	if(!doc)
		return menu_fail("menu.rml");
	doc->Show();
	return 0;
}

void sdl_menu_quit(void)
{
	if(!menu)
		return;
	if(menu->rml_up)
		Rml::Shutdown();
	delete menu->render;
	delete menu->system;
	delete menu;
	menu = nullptr;
}

int sdl_menu_is_open(void)
{
	return menu && menu->model.open;
}

void sdl_menu_show(int open)
{
	menu->model.open = open != 0;
	menu->handle.DirtyVariable("open");
}

void sdl_menu_event(SDL_Event *e)
{
	RmlSDL::InputEventHandler(menu->context, menu->window, *e);
}

void sdl_menu_message(const char *text)
{
	std::vector<Rml::String> &m = menu->model.messages;
	m.push_back(text);
	if((int)m.size() > menu_messages_max)
		m.erase(m.begin());
	menu->handle.DirtyVariable("messages");
}

void sdl_menu_banner(const char *text)
{
	menu->model.banner = text;
	menu->model.banner_on = true;
	menu->banner_start = SDL_GetTicks();
	menu->handle.DirtyVariable("banner");
	menu->handle.DirtyVariable("banner_on");
}

void sdl_menu_refresh(int booted)
{
	std::vector<DiskRow> &rows = menu->model.disks;
	uint32_t r;
	int n;
	rows.clear();
	for(r = 0; r < game_def.disk_count; r++)
	{
		const GameDisk *d = &game_def.disks[r];
		const SdlRole *role = &sdl_roles[r];
		DiskRow row;
		row.label = d->label;
		if(d->writable)
			row.state = Rml::String("kept in ") + role->source;
		else if(!role->adf)
		{
			bool needed = false;
			for(n = 0; n < 4; n++)
				needed = needed || game_def.drives[n] == (int)r;
			row.state = needed ? "missing, needed to start" : "missing, needed later";
		}
		else
		{
			const char *word = role->rank == SdlRankVerified ? "verified dump: " : role->rank == SdlRankKnown ? "known dump: " : "unknown dump: ";
			row.state = Rml::String(word) + role->name;
			row.source = role->source;
		}
		rows.push_back(row);
	}
	menu->model.booted = booted != 0;
	menu->handle.DirtyVariable("disks");
	menu->handle.DirtyVariable("booted");
}

void sdl_menu_render(SDL_Window *window)
{
	int w = 0, h = 0;
	SDL_GetWindowSizeInPixels(window, &w, &h);
	if(menu->model.banner_on && SDL_GetTicks() - menu->banner_start >= menu_banner_ms)
	{
		menu->model.banner_on = false;
		menu->handle.DirtyVariable("banner_on");
	}
	menu->context->SetDimensions(Rml::Vector2i(w, h));
	menu->render->SetViewport(w, h);
	menu->context->Update();
	menu->render->BeginFrame();
	menu->context->Render();
	menu->render->EndFrame();
}

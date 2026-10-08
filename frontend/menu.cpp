#include "menu.h"

#include "amiga.h"
#include "disks.h"
#include "game.h"
#include "settings.h"

#include <RmlUi/Core.h>

#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_GL3.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

uint32_t sdl_menu_action_event;

namespace {

constexpr int menu_messages_max = 6;
constexpr uint64_t menu_banner_ms = 10000;

const SdlAsset *menu_asset(const Rml::String &name)
{
	for(const SdlAsset *table : {sdl_menu_game, sdl_menu_shared})
		for(const SdlAsset *a = table; a->name; a++)
			if(name == a->name)
				return a;
	return nullptr;
}

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
		const SdlAsset *a = menu_asset(name);
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
};

struct DiskRow
{
	Rml::String label, state, source;
	bool writable = false;
};

struct DriveRow
{
	Rml::String name, disk; /* "DF0"; the disk's label or "empty" (shown before boot) */
	int role = -1;
};

struct DriveChoice
{
	Rml::String label;
	int role = -1;
};

struct OptionRow
{
	Rml::String id, label, hint, type;
	int value = 0;
	std::vector<Rml::String> choices;
	Rml::String status;
};

struct MenuModel
{
	Rml::String title;
	bool open = false;
	bool booted = false;
	Rml::String page = "about";
	Rml::String about;
	std::vector<DiskRow> disks;
	std::vector<Rml::String> messages;
	Rml::String banner;
	bool banner_on = false;
	std::vector<OptionRow> settings, hacks;
	std::vector<DriveRow> drives;
	std::vector<DriveChoice> drive_choices;
};

struct Menu
{
	SdlMenuFiles files;
	SystemInterface_SDL *system = nullptr;
	RenderInterface_GL3 *render = nullptr;
	Rml::Context *context = nullptr;
	Rml::ElementDocument *doc = nullptr;
	Rml::Element *panel = nullptr;
	Rml::DataModelHandle handle;
	MenuModel model;
	SDL_Window *window = nullptr;
	uint64_t banner_start = 0;
	struct Amiga *m = nullptr;
	bool rml_up = false;
};

Menu *menu;

struct MenuDialog
{
	SdlMenuAction action;
	int role;
};

void menu_push(SdlMenuAction action, const char *path, int role)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = sdl_menu_action_event;
	e.user.code = action;
	e.user.data1 = path ? SDL_strdup(path) : nullptr;
	e.user.data2 = (void *)(intptr_t)role;
	if((path && !e.user.data1) || !SDL_PushEvent(&e))
		SDL_free(e.user.data1);
}

void menu_dialog_done(void *userdata, const char *const *list, int)
{
	MenuDialog *d = (MenuDialog *)userdata;
	if(!list)
		std::fprintf(stderr, "file dialog: %s\n", SDL_GetError());
	else
		for(; *list; list++)
			menu_push(d->action, *list, d->role);
	delete d;
}

void menu_dialog(SdlMenuAction action, int role)
{
	static const SDL_DialogFileFilter disks_filter = {"Amiga disk images", "adf;adz;gz;zip"};
	static const SDL_DialogFileFilter adf_filter = {"Amiga disk images", "adf"};
	static const SDL_DialogFileFilter state_filter = {"AmigaRecomp states", "sav"};
	MenuDialog *d = new MenuDialog{action, role};
	switch(action)
	{
	case SdlMenuOpenDisk:
		SDL_ShowOpenFileDialog(menu_dialog_done, d, menu->window, &disks_filter, 1, nullptr, true);
		break;
	case SdlMenuImportDisk:
		SDL_ShowOpenFileDialog(menu_dialog_done, d, menu->window, &adf_filter, 1, nullptr, false);
		break;
	case SdlMenuStateLoad:
		SDL_ShowOpenFileDialog(menu_dialog_done, d, menu->window, &state_filter, 1, nullptr, false);
		break;
	case SdlMenuStateSave:
		SDL_ShowSaveFileDialog(menu_dialog_done, d, menu->window, &state_filter, 1, nullptr);
		break;
	}
}

int menu_fail(const char *what)
{
	std::fprintf(stderr, "menu: cannot load %s\n", what);
	return -1;
}

OptionRow option_row(const struct GameSetting &s, int value, const char *status)
{
	OptionRow r;
	r.id = s.id;
	r.label = s.label;
	r.hint = s.hint;
	r.type = s.type == GameValueBool ? "bool" : "choice";
	r.value = value;
	for(int i = 0; s.type == GameValueChoice && s.choices && s.choices[i]; i++)
		r.choices.push_back(s.choices[i]);
	r.status = status;
	return r;
}

void menu_options_refresh()
{
	static const char *const status[] = {"idle", "active", "mismatch"};
	menu->model.settings.clear();
	menu->model.hacks.clear();
	for(int i = 0; i < SdlSettingCount; i++)
	{
		const bool unchosen = i == SdlSettingScale && sdl_scale_forced();
		menu->model.settings.push_back(option_row(sdl_settings[i], unchosen ? -1 : sdl_setting((SdlSetting)i), ""));
	}
	for(uint32_t i = 0; i < game_def.hack_count; i++)
		menu->model.hacks.push_back(option_row(game_def.hacks[i]->setting, sdl_hack_value(menu->m, i), status[menu->m->hacks.status[i]]));
	menu->handle.DirtyVariable("settings");
	menu->handle.DirtyVariable("hacks");
}

void menu_disks_refresh()
{
	std::vector<DiskRow> &rows = menu->model.disks;
	const bool booted = menu->model.booted;
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
			row.state = role->rank == SdlRankVerified ? "verified dump" : role->rank == SdlRankKnown ? "known dump" : "unknown dump";
			row.source = role->source;
		}
		row.writable = d->writable != 0;
		rows.push_back(row);
	}
	menu->model.drives.clear();
	for(n = 0; n < game_def.drive_count; n++)
	{
		DriveRow row;
		row.name = "DF" + std::to_string(n);
		row.role = booted ? menu->m->drive_role[n] : game_def.drives[n];
		row.disk = row.role < 0 ? "empty" : game_def.disks[row.role].label;
		menu->model.drives.push_back(row);
	}
	menu->model.drive_choices.clear();
	for(r = 0; r < game_def.disk_count; r++)
	{
		DriveChoice choice;
		if(!menu->m->disks[r].image)
			continue;
		choice.label = game_def.disks[r].label;
		choice.role = (int)r;
		menu->model.drive_choices.push_back(choice);
	}
	{
		DriveChoice none;
		none.label = "Empty";
		menu->model.drive_choices.push_back(none);
	}
	menu->handle.DirtyVariable("disks");
	menu->handle.DirtyVariable("drives");
	menu->handle.DirtyVariable("drive_choices");
}

} // namespace

int sdl_menu_init(SDL_Window *window, struct Amiga *m)
{
	int w = 0, h = 0;
	Rml::DataModelConstructor ctor;

	menu = new Menu;
	menu->window = window;
	menu->m = m;
	sdl_menu_action_event = SDL_RegisterEvents(1);
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

	ctor = menu->context->CreateDataModel("menu");
	if(!ctor)
		return menu_fail("the data model");
	if(auto row = ctor.RegisterStruct<DiskRow>())
	{
		row.RegisterMember("label", &DiskRow::label);
		row.RegisterMember("state", &DiskRow::state);
		row.RegisterMember("source", &DiskRow::source);
		row.RegisterMember("writable", &DiskRow::writable);
	}
	ctor.RegisterArray<std::vector<DiskRow>>();
	ctor.RegisterArray<std::vector<Rml::String>>();
	if(auto row = ctor.RegisterStruct<OptionRow>())
	{
		row.RegisterMember("id", &OptionRow::id);
		row.RegisterMember("label", &OptionRow::label);
		row.RegisterMember("hint", &OptionRow::hint);
		row.RegisterMember("type", &OptionRow::type);
		row.RegisterMember("value", &OptionRow::value);
		row.RegisterMember("choices", &OptionRow::choices);
		row.RegisterMember("status", &OptionRow::status);
	}
	ctor.RegisterArray<std::vector<OptionRow>>();
	if(auto row = ctor.RegisterStruct<DriveRow>())
	{
		row.RegisterMember("name", &DriveRow::name);
		row.RegisterMember("disk", &DriveRow::disk);
		row.RegisterMember("role", &DriveRow::role);
	}
	ctor.RegisterArray<std::vector<DriveRow>>();
	if(auto row = ctor.RegisterStruct<DriveChoice>())
	{
		row.RegisterMember("label", &DriveChoice::label);
		row.RegisterMember("role", &DriveChoice::role);
	}
	ctor.RegisterArray<std::vector<DriveChoice>>();
	ctor.Bind("title", &menu->model.title);
	ctor.Bind("open", &menu->model.open);
	ctor.Bind("booted", &menu->model.booted);
	ctor.Bind("page", &menu->model.page);
	ctor.Bind("about", &menu->model.about);
	ctor.Bind("disks", &menu->model.disks);
	ctor.Bind("messages", &menu->model.messages);
	ctor.Bind("banner", &menu->model.banner);
	ctor.Bind("banner_on", &menu->model.banner_on);
	ctor.Bind("settings", &menu->model.settings);
	ctor.Bind("hacks", &menu->model.hacks);
	ctor.Bind("drives", &menu->model.drives);
	ctor.Bind("drive_choices", &menu->model.drive_choices);
	ctor.BindEventCallback("open_disk", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { menu_dialog(SdlMenuOpenDisk, -1); });
	ctor.BindEventCallback("import_disk", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
		if(args.size() == 1)
			menu_dialog(SdlMenuImportDisk, args[0].Get<int>());
	});
	ctor.BindEventCallback("state_save", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { menu_push(SdlMenuStateSave, nullptr, -1); });
	ctor.BindEventCallback("state_load", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { menu_push(SdlMenuStateLoad, nullptr, -1); });
	ctor.BindEventCallback("state_save_as", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { menu_dialog(SdlMenuStateSave, -1); });
	ctor.BindEventCallback("state_load_from", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { menu_dialog(SdlMenuStateLoad, -1); });
	ctor.BindEventCallback("quit", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
		SDL_Event e;
		SDL_zero(e);
		e.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&e);
	});
	ctor.BindEventCallback("show_page", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
		Rml::Element *page;
		if(args.size() != 1)
			return;
		menu->model.page = args[0].Get<Rml::String>();
		menu->handle.DirtyVariable("page");
		page = menu->doc->GetElementById("page");
		if(page)
			page->SetScrollTop(0);
	});
	ctor.BindEventCallback("close", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { sdl_menu_show(0); });
	ctor.BindEventCallback("choose", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
		if(args.size() != 3)
			return;
		sdl_settings_choose(menu->m, args[0].Get<Rml::String>() == "hack", args[1].Get<uint32_t>(), args[2].Get<int>());
		menu_options_refresh();
	});
	ctor.BindEventCallback("insert", [](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
		if(args.size() != 2)
			return;
		sdl_disks_insert(menu->m, args[0].Get<int>(), args[1].Get<int>());
		menu_disks_refresh();
	});
	menu->handle = ctor.GetModelHandle();
	menu->model.title = game_def.title;
	if(const SdlAsset *a = menu_asset("about.rml"))
		menu->model.about.assign((const char *)a->data, a->size);

	menu->doc = menu->context->LoadDocument("menu.rml");
	if(!menu->doc)
		return menu_fail("menu.rml");
	menu->panel = menu->doc->GetElementById("panel");
	if(!menu->panel)
		return menu_fail("#panel in menu.rml");
	menu->doc->Show();
	menu_options_refresh();
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
	if(open)
	{
		menu_options_refresh();
		menu_disks_refresh();
	}
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
	menu->model.booted = booted != 0;
	menu->handle.DirtyVariable("booted");
	menu_disks_refresh();
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
	/* At most the display scale; below it, #panel's width fills the window. */
	float dp = SDL_GetWindowDisplayScale(window);
	const Rml::Property *pw = menu->panel->GetProperty("width");
	if(w > 0 && pw && pw->unit == Rml::Unit::DP && pw->Get<float>() > 0.0f && (float)w / pw->Get<float>() < dp)
		dp = (float)w / pw->Get<float>();
	menu->context->SetDensityIndependentPixelRatio(dp);
	menu->context->SetDimensions(Rml::Vector2i(w, h));
	menu->render->SetViewport(w, h);
	menu->context->Update();
	menu->render->BeginFrame();
	menu->context->Render();
	menu->render->EndFrame();
}

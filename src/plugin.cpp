#include <filesystem>
#include <string>
#include <fstream>
#include <set>
#include <random>
#include <loader.hpp>
#include <ida.hpp>
#include <idp.hpp>
#include <kernwin.hpp>
#include <diskio.hpp>
#include <expr.hpp>
#include "utils.hpp"


std::string get_history_path() {
	static std::string path = std::string(get_user_idadir()) + "/script_history.txt";
	return path;
}


enum ICON : int {
	ICON_SCRIPT = 64
};


enum COLOR : bgcolor_t {
	CLR_LIGHTBLUE = 0xFFFFF0,
	CLR_DARKBLUE = 0x606050,
};


struct script_t {
	qstring name;
	qstring directory;
	qstring modified;
	qstring created;
	std::filesystem::path path;
	bool linked = false;
};


struct script_chooser_t : chooser_t {
	inline static std::string last_path;
	inline static std::vector<std::unique_ptr<script_t> > scripts;
	inline static int linked_color;


	script_chooser_t() {
		title = "ScriptWatch";
		static const char *header_[] = {"Name", "Folder", "Modified", "Created", "Path"};
		static constexpr int widths_[] = {
			CHCOL_PLAIN | 25,
			CHCOL_PLAIN | 15,
			CHCOL_PLAIN | 15,
			CHCOL_PLAIN | 15,
			CHCOL_PATH | 25,
		};
		header = header_;
		columns = std::size(header_);
		widths = widths_;
		flags = CH_MODAL | CH_KEEP | CH_CAN_INS | CH_CAN_DEL | CH_CAN_REFRESH;
	}


	// ---------------------------- INHERITED ----------------------------


	cbret_t idaapi enter(const size_t n) override {
		last_path = scripts[n]->path;
		close_chooser(title);
		run_script(scripts[n]);
		save_history();
		return {static_cast<ssize_t>(n), NOTHING_CHANGED};
	}


	cbret_t idaapi ins(ssize_t n) override {
		const std::set<std::string> selected = askPaths();
		if (selected.empty()) {
			return NOTHING_CHANGED;
		}
		add_scripts(selected);
		save_history();
		return {n, ALL_CHANGED};
	}


	cbret_t idaapi del(const size_t n) override {
		if (const auto el = scripts.at(n).get(); !el->linked) {
			scripts.erase(scripts.begin() + static_cast<ssize_t>(n));
		} else {
			auto dirpath = scripts.at(n)->directory;
			const auto it = std::remove_if(
				scripts.begin(), scripts.end(), [&dirpath](const std::unique_ptr<script_t> &e) {
					return e->directory == dirpath;
				}
			);
			scripts.erase(it, scripts.end());
		}
		save_history();
		const auto size = static_cast<ssize_t>(scripts.size());
		const auto nn = n >= size ? size - 1 : n;
		return {static_cast<ssize_t>(nn), ALL_CHANGED};
	}


	cbret_t idaapi refresh(ssize_t n) override {
		if (scripts.empty() || n == -1) {
			last_path = "";
			n = 0;
		} else {
			const auto path = scripts[n]->path;
			const auto size = scripts.size();
			load_history();
			if (size != scripts.size()) {
				last_path = path;
				const auto idx = find_script_with_path(last_path);
				n = idx.has_value() ? static_cast<ssize_t>(idx.value()) : n;
			}
		}
		return {n, ALL_CHANGED};
	}


	[[nodiscard]] size_t idaapi get_count() const override {
		return scripts.size();
	}


	void idaapi get_row(qstrvec_t *cols, int *icon_id, chooser_item_attrs_t *attr, const size_t n) const override {
		const auto e = scripts[n].get();
		cols->at(0) = e->name;
		cols->at(1) = e->linked ? e->directory : "";
		cols->at(2) = e->modified;
		cols->at(3) = e->created;
		cols->at(4) = qstring(e->path.c_str());
		*icon_id = ICON_SCRIPT;
		if (e->linked)
			attr->color = linked_color;
	}


	// ---------------------------- CUSTOM ----------------------------


	static void add_scripts(const std::set<std::string> &paths) {
		auto [files, dirs] = sort_paths(paths);

		for (const auto &p: files) {
			check_and_add_script(p, false);
		}

		for (const auto &p: dirs) {
			auto expanded = expand_dir(p);
			for (const auto &f: expanded) {
				check_and_add_script(f, true);
			}
		}
	}


	static void check_and_add_script(const std::string &p, const bool linkdir) {
		const auto path = std::filesystem::path(p);

		if (!exists(path) || path.extension() != ".py")
			return;

		if (const auto idx = find_script_with_path(p); idx.has_value()) {
			scripts[idx.value()] = create_script(p, linkdir);
		} else {
			scripts.push_back(create_script(p, linkdir));
		}
	}


	static void run_script(const std::unique_ptr<script_t> &script) {
		const extlang_t *python = find_extlang_by_name("Python");

		if (python == nullptr) {
			msg("IDAPython not available\n");
			return;
		}

		qstring err;

		python->compile_file(script->path.c_str(), nullptr, &err);

		if (!err.empty()) {
			warning("%s: %s", script->path.c_str(), err.c_str());
		}
	}


	static std::unique_ptr<script_t> create_script(const std::string &path, const bool link_dir) {
		const std::filesystem::path file(path);
		auto script = std::make_unique<script_t>(script_t());

		script->name = file.stem().string().c_str();
		script->path = file;
		script->directory = file.parent_path().filename().string().c_str();
		script->linked = link_dir;

		char buff[64];
		struct stat st{};
		stat(file.c_str(), &st);

		std::strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M", std::localtime(&st.st_birthtime));
		script->created = buff;

		std::strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M", std::localtime(&st.st_mtime));
		script->modified = buff;
		return script;
	}


	static std::optional<size_t> find_script_with_path(const std::string &p) {
		const auto it = std::find_if(scripts.begin(), scripts.end(), [&p](const std::unique_ptr<script_t> &other) {
			                             return other->path == std::filesystem::path(p);
		                             }
		);
		return it == scripts.end() ? std::nullopt : std::optional(std::distance(scripts.begin(), it));
	}


	static std::set<std::string> expand_dir(const std::string &dirpath) {
		std::set<std::string> expanded;
		assert(std::filesystem::is_directory(dirpath));

		for (const auto &entry: std::filesystem::directory_iterator(dirpath))
			if (entry.is_regular_file())
				expanded.insert(entry.path().string());

		return expanded;
	}


	static std::pair<std::set<std::string>, std::set<std::string> > sort_paths(const std::set<std::string> &paths) {
		std::set<std::string> files;
		std::set<std::string> dirs;

		for (const auto &p: paths) {
			if (std::filesystem::is_directory(p))
				dirs.insert(p);
			else if (std::filesystem::is_regular_file(p))
				files.insert(p);
		}

		return {files, dirs};
	}


	static void load_history() {
		std::ifstream f(get_history_path(), std::ios::in);
		if (!f) {
			return;
		}

		std::string line;
		std::set<std::string> paths;

		while (std::getline(f, line)) {
			paths.insert(line);
		}

		add_scripts(paths);
		remove_deleted();
		sort_by_name();
	}


	static void save_history() {
		remove_deleted();
		sort_by_name();
		std::ofstream f(get_history_path(), std::ios::out);
		std::set<std::string> paths;

		for (const auto &p: scripts) {
			paths.insert(p->linked ? p->path.parent_path().c_str() : p->path.c_str());
		}

		for (const auto &p: paths) {
			f << p << "\n";
		}
	}


	static void remove_deleted() {
		const auto it = std::remove_if(scripts.begin(), scripts.end(), [](const std::unique_ptr<script_t> &s) {
			                               return !std::filesystem::exists(s->path.c_str());
		                               }
		);
		scripts.erase(it, scripts.end());
	}


	static void sort_by_name() {
		std::sort(scripts.begin(), scripts.end(),
		          [](const std::unique_ptr<script_t> &a, const std::unique_ptr<script_t> &b) {
			          return a->name < b->name;
		          }
		);
	}
};


struct Watcher {
	inline static std::unique_ptr<script_chooser_t> chooser;


	static plugmod_t * idaapi init() {
		if (!chooser) {
			chooser = std::make_unique<script_chooser_t>(script_chooser_t());
		}
		return PLUGIN_KEEP;
	}


	static void idaapi term() {
	}


	static bool idaapi run(size_t arg) {
		script_chooser_t::load_history();
		ssize_t idx = 0;
		if (!script_chooser_t::last_path.empty()) {
			const auto script = script_chooser_t::find_script_with_path(script_chooser_t::last_path);
			idx = script.has_value() ? static_cast<long>(script.value()) : 0;
		}
		script_chooser_t::linked_color = isDarkMode() ? CLR_DARKBLUE : CLR_LIGHTBLUE;
		chooser->choose(idx);
		return true;
	}
};


plugin_t PLUGIN = {
	IDP_INTERFACE_VERSION,
	0,
	Watcher::init,
	Watcher::term,
	Watcher::run,
	"",
	"",
	"ScriptWatch",
	""
};

#include "stdafx.h"

#include "foo_discogs.h"

#include "utils.h"
#include "tags.h"
#include "multiformat.h"
#include "track_matching_dialog.h"
#include "preview_dialog.h"
#include "update_art_dialog.h"
#include "update_tags_dialog.h"
#include "configuration_dialog.h"
#include "contextmenu_item_discogs.h"
#include "tag_mappings_dialog.h"


using namespace std;

foo_discogs *g_discogs = nullptr;
DiscogsInterface *discogs_interface = nullptr;


DECLARE_COMPONENT_VERSION(
// component name
"Discogs Tagger",
// component version
FOO_DISCOGS_VERSION,
"A tagger using the Discogs (https://www.discogs.com) database.\n"
"\n"
"Author:  zoomorph\n"
"Version: "FOO_DISCOGS_VERSION"\n"
"Compiled: "__DATE__ "\n"
"Website: https://bitbucket.org/zoomorph/foo_discogs\n"
"\n"
"Thanks to Michael Pujos (aka bubbleguuum) for starting this project (up to version 1.32).\n"
"\n"
"This plugin uses the following open source libraries (thanks to their respective authors):\n"
"jansson - JSON Parser: http://www.digip.org/jansson/\n"
"liboauthcpp - OAuth library: https://github.com/sirikata/liboauthcpp\n"
"\n"
"Discogs' staff are assholes. Avoid paying them fees when possible!")

#define MIN(a,b) ((a)<(b)?(a):(b))

static const char * ERROR_IMAGE_NUMBER_REQUIRED = "Cannot save multiple images with the same name. Use %IMAGE_NUMBER% when forming image names.";


class initquit_discogs : public initquit
{
	virtual void on_init() override {
		console::print("Loading");
		discogs_interface = new DiscogsInterface(); 
		g_discogs = new foo_discogs();
		load_dlls();
	}
	virtual void on_quit() override {
		console::print("Quitting");
		delete g_discogs;
		unload_dlls();
	}
};

static initquit_factory_t<initquit_discogs> foo_initquit;

foo_discogs::foo_discogs() {
	init_conf();
	init_tag_mappings();
	discogs_interface->fetcher->set_oauth(CONF.oauth_token, CONF.oauth_token_secret);

	static_api_ptr_t<titleformat_compiler>()->compile_force(release_name_script, "[%album artist%] - [%album%]");
	gave_oauth_warning = false;
}

foo_discogs::~foo_discogs() {
	if (find_release_dialog)  {
		find_release_dialog->destroy();
	}
	if (track_matching_dialog) {
		track_matching_dialog->destroy();
	}
	if (tag_mappings_dialog) {
		tag_mappings_dialog->destroy();
	}
	if (configuration_dialog) {
		configuration_dialog->destroy();
	}
	if (update_art_dialog) {
		update_art_dialog->destroy();
	}
	if (update_tags_dialog) {
		update_tags_dialog->destroy();
	}
	if (preview_tags_dialog) {
		preview_tags_dialog->destroy();
	}
}

void foo_discogs::item_display_web_page(const metadb_handle_ptr item, discog_web_page web_page) {
	file_info_impl finfo;

	char *url_prefix;
	char *url_postfix = "";
	const char *tag;
	char *backup_tag = nullptr;
	switch (web_page) {
		case ARTIST_PAGE:
			tag = TAG_ARTIST_ID;
			backup_tag = "DISCOGS_ARTIST_LINK";
			url_prefix = "https://www.discogs.com/artist/";
			break;
		case ARTIST_ART_PAGE:
			tag = TAG_ARTIST_ID;
			backup_tag = "DISCOGS_ARTIST_LINK";
			url_prefix = "https://www.discogs.com/artist/";
			url_postfix = "/images";
			break;
		case RELEASE_PAGE:
			tag = TAG_RELEASE_ID;
			url_prefix = "https://www.discogs.com/x/release/";
			break;
		case LABEL_PAGE:
			tag = TAG_LABEL_ID;
			backup_tag = "DISCOGS_LABEL_LINK";
			url_prefix = "https://www.discogs.com/label/";
			break;
		case ALBUM_ART_PAGE:
			tag = TAG_RELEASE_ID;
			url_prefix = "https://www.discogs.com/release/";
			url_postfix = "/images";
			break;
		case MASTER_RELEASE_PAGE:
			tag = TAG_MASTER_RELEASE_ID;
			url_prefix = "https://www.discogs.com/master/";
			break;
		default:
			return;
	}

	try {
		pfc::string8 tag_value;
		pfc::string8 url;
		item->get_info(finfo);
		if (file_info_get_tag(item, finfo, tag, tag_value, backup_tag)) {
			url << url_prefix << tag_value << url_postfix;
			display_url(url);
		}
		else if (web_page == MASTER_RELEASE_PAGE) {
			tag = TAG_RELEASE_ID;
			url_prefix = "https://www.discogs.com/release/";
			if (file_info_get_tag(item, finfo, tag, tag_value, backup_tag)) {
				url << url_prefix << tag_value << url_postfix;
				display_url(url);
			}
		}
	}
	catch (foo_discogs_exception &e) {
		add_error(e);
		display_errors();
		clear_errors();
	}
}

/*pfc::string8 foo_discogs::make_titleformat(const metadb_handle_ptr item, const char *dir_titleformat, const char *file_titleformat) {
	pfc::string8 dst;
	pfc::string8 dir, file;
	titleformat_object_wrapper dir_script(dir_titleformat);
	titleformat_object_wrapper file_script(file_titleformat);

	item->format_title(nullptr, dir, dir_script, nullptr);
	item->format_title(nullptr, file, file_script, nullptr);

	dst = dir.get_ptr();
	if (dst.get_length() && dst[dst.get_length() - 1] != '\\') {
		dst.add_char('\\');
	}
	dst += file;
	return dst;
}

pfc::string8 foo_discogs::print_filename(const metadb_handle_ptr item) {
	// This supports m-TAGS too
	const char *name_titleformat = "$if($strcmp($ext(%path%),tags), $info(@), %path%)";
	return g_discogs->make_titleformat(item, "", name_titleformat);
}*/

bool foo_discogs::file_info_get_tag(const metadb_handle_ptr item, file_info &finfo, const char* tag, pfc::string8 &value, const char *backup_tag) {
	const char *field = finfo.meta_get(tag, 0);
	if (field != nullptr) {
		value = field;
		return true;
	}
	else if (backup_tag != nullptr) {
		field = finfo.meta_get(backup_tag, 0);
		if (field != nullptr) {
			value = field;
			return true;
		}
	}
	return false;
}

bool foo_discogs::item_has_tag(const metadb_handle_ptr item, const char *tag, const char *backup_tag) {
	bool has_tag;
	file_info_impl file_info;
	pfc::string8 tmp;

	item->get_info(file_info);
	has_tag = file_info_get_tag(item, file_info, tag, tmp, backup_tag);

	return has_tag;
}

bool foo_discogs::some_item_has_tag(const metadb_handle_list items, const char*tag) {
	for (size_t i = 0; i < items.get_count(); i++) {
		if (item_has_tag(items.get_item(i), tag)) {
			return true;
		}
	}
	return false;
}


const ReleaseDisc_ptr& foo_discogs::file_info_get_discogs_disc(file_info &finfo, const metadb_handle_ptr item, const Release_ptr &release, size_t &track_num) {
	/*pfc::string8 dt_index;
	if (g_discogs->file_info_get_tag(item, finfo, TAG_DISCOGS_TRACKLIST_INDEX, dt_index)) {
		int pos = dt_index.find_first('/');
		if (pos != pfc::infinite_size) {
			int dt_index_i = std::stoi(substr(dt_index, pos + 1).get_ptr());
			int tr_number = std::stoi(substr(dt_index, 0, pos).get_ptr());
			if (dt_index_i == release->discogs_tracklist_count) {
				for (size_t i = 0; i < release->discs.get_size(); i++) {
					for (size_t j = 0; j < release->discs[i]->tracks.get_size(); j++) {
						if (tr_number == release->discs[i]->tracks[j]->discogs_tracklist_index) {
							track_num = j;
							return release->discs[i];
						}
					}
				}
			}
		}
	}*/
	throw foo_discogs_exception("Unable to map file to Discogs tracklist.");
}

const ReleaseDisc_ptr& foo_discogs::get_discogs_disc(const Release_ptr &release, size_t index, size_t &disc_track_index) {
	for (size_t i = 0; i < release->discs.get_size(); i++) {
		if (index < release->discs[i]->tracks.get_size()) {
			disc_track_index = index;
			return release->discs[i];
		}
		index -= release->discs[i]->tracks.get_size();
	}
	throw foo_discogs_exception("Unable to map index to Discogs tracklist.");
}

// TODO: ....
// expand all ability for searching of sub releases?

void foo_discogs::write_album_art(Release_ptr &release,
		metadb_handle_ptr item,
		const char *item_text,
		threaded_process_status &p_status,
		abort_callback &p_abort) {

	if (!release->images.get_size()) {
		return;
	}

	pfc::string8 formatted_release_name;
	char progress_text[512];
	// TODO: this was only done on fetch all... wtf is in item_text??
	sprintf_s(progress_text, "%s (fetching album art file list)", item_text);
	p_status.set_item(item_text);
	
	file_info_impl info;
	titleformat_hook_impl_multiformat hook(p_status, &release);

	pfc::string8 directory;
	CONF.album_art_directory_string->run_hook(item->get_location(), &info, &hook, directory, nullptr);
	
	// hardcode "nullptr" as don't write anything.
	if (STR_EQUAL(directory, "nullptr")) {
		log_msg("skipping album art because of nullptr directory");
		return;
	}

	if (directory[directory.get_length() - 1] != '\\') {
		directory.add_char('\\');
	}
	ensure_directory_exists(directory);

	abort_callback_impl _abort;
	pfc::string8 last_path = "";

	const size_t count = CONF.album_art_fetch_all ? release->images.get_size() : 1;

	for (size_t i = 0; i < count; i++) {
		hook.set_custom("IMAGE_NUMBER", i + 1);
		hook.set_image(&release->images[i]);

		pfc::string8 file;
		CONF.album_art_filename_string->run_hook(item->get_location(), &info, &hook, file, nullptr);

		if (STR_EQUAL(file, "nullptr")) {
			log_msg("skipping album image because of nullptr directory");
			continue;
		}

		pfc::string8 path = directory;
		path += makeFsCompliant(file);
		path += ".jpg";

		if (STR_EQUAL(path, last_path)) {
			foo_discogs_exception ex;
			ex << ERROR_IMAGE_NUMBER_REQUIRED;
			throw ex;
		}

		if (CONF.artist_art_overwrite || !filesystem::g_exists(path, _abort)) {
			sprintf_s(progress_text, "%s (fetching album art file %d/%d)", formatted_release_name.get_ptr(), i + 1, count);
			p_status.set_item(progress_text);
			write_image(release->images[i], path, p_abort);
		}
		last_path = path;
	}
}

void foo_discogs::write_artist_art(Release_ptr &release,
		metadb_handle_ptr item,
		const char *item_text,
		threaded_process_status &p_status,
		abort_callback &p_abort) {

	// ensure release is loaded
	release->load(p_status, p_abort);
	
	// get artist IDs from titleformat string
	MasterRelease_ptr master = discogs_interface->get_master_release(release->master_id);
	file_info_impl info;
	titleformat_hook_impl_multiformat hook(p_status, &master, &release);
	pfc::string8 str;
	pfc::array_t<int> ids;
	try {
		CONF.artist_art_id_format_string->run_hook(item->get_location(), &info, &hook, str, nullptr);
		string_encoded_array result(str);
		if (result.has_array()) {
			for (size_t i = 0; i < result.get_width(); i++) {
				const pfc::string8 &n = result.get_citem(i).get_pure_cvalue();
				int num = std::stoi(n.get_ptr());
				for (size_t j = 0; j < ids.get_size(); j++) {
					if (ids[j] == num) {
						num = 0;
						break;
					}
				}
				if (num != 0) {
					ids.append_single(num);
				}
			}
		}
		else {
			ids.append_single(std::stoi(result.get_pure_cvalue().get_ptr()));
		}

		// write art for each id
		for (size_t i = 0; i < ids.get_size(); i++) {
			pfc::string8 artist_id;
			artist_id << ids[i];
			Artist_ptr artist = discogs_interface->get_artist(artist_id);
			write_artist_art(artist, item, item_text, p_status, p_abort);
		}
	}
	catch (foo_discogs_exception &e) {
		foo_discogs_exception ex;
		ex << "Error writing artist art " << " [" << e.what() << "]";
		throw ex;
	}
}

void foo_discogs::write_artist_art(ReleaseArtist_ptr &release_artist,
		metadb_handle_ptr item,
		const char *item_text,
		threaded_process_status &p_status,
		abort_callback &p_abort) {

	write_artist_art(release_artist->full_artist, item, item_text, p_status, p_abort);
}

void foo_discogs::write_artist_art(Artist_ptr &artist,
		metadb_handle_ptr item,
		const char *item_text,
		threaded_process_status &p_status,
		abort_callback &p_abort) {
	
	if (!artist->images.get_size()) {
		return;
	}
	
	// ensure artist is loaded
	artist->load(p_status, p_abort);

	char progress_text[512];
	sprintf_s(progress_text, "%s (%s - fetching artist art file list)", item_text, artist->name.get_ptr());
	p_status.set_item(progress_text);

	file_info_impl info;
	titleformat_hook_impl_multiformat hook(p_status, &artist);

	pfc::string8 directory;
	CONF.artist_art_directory_string->run_hook(item->get_location(), &info, &hook, directory, nullptr);

	// hardcode "nullptr" as don't write anything.
	if (STR_EQUAL(directory, "nullptr")) {
		log_msg("skipping artist art because of nullptr directory");
		return;
	}

	if (directory[directory.get_length() - 1] != '\\') {
		directory.add_char('\\');
	}
	ensure_directory_exists(directory);

	abort_callback_impl _abort;
	pfc::string8 last_path = "";

	const size_t count = CONF.artist_art_fetch_all ? artist->images.get_size() : 1;

	for (size_t i = 0; i < count; i++) {
		hook.set_custom("IMAGE_NUMBER", i+1);
		hook.set_image(&artist->images[i]);

		pfc::string8 file;
		CONF.artist_art_filename_string->run_hook(item->get_location(), &info, &hook, file, nullptr);

		if (STR_EQUAL(file, "nullptr")) {
			log_msg("skipping artist image because of nullptr directory");
			continue;
		}

		pfc::string8 path = directory;
		path += makeFsCompliant(file);
		path += ".jpg";

		if (STR_EQUAL(path, last_path)) {
			foo_discogs_exception ex;
			ex << ERROR_IMAGE_NUMBER_REQUIRED;
			throw ex;
		}

		if (CONF.artist_art_overwrite || !filesystem::g_exists(path, _abort)) {
			sprintf_s(progress_text, "%s (%s - fetching artist art file %d/%d)", item_text, artist->name.get_ptr(), i + 1, count);
			p_status.set_item(progress_text);
			write_image(artist->images[i], path, p_abort);
		}
		last_path = path;
	}
}

void foo_discogs::write_image(Image_ptr &image, const pfc::string8 &full_path, abort_callback &p_abort) {
	MemoryBlock buffer;

	if (!image->url.get_length()) {
		foo_discogs_exception ex;
		ex << "Image URLs unavailable - Is OAuth working?";
		throw ex;
	}

	// create file
	service_ptr_t<file> write_file;
	try {
		filesystem::g_open_write_new(write_file, full_path.get_ptr(), p_abort);
	}
	catch (const foobar2000_io::exception_io &e) {
		foo_discogs_exception ex;
		ex << "Error creating file \"" << full_path << "\" [" << e.what() << "]";
		throw ex;
	}

	try
	{	
		// download image
		bool use_api;

		pfc::string8 url = image->url;
		use_api = false;

		discogs_interface->fetcher->fetch_url(url, "", buffer, p_abort, use_api);

		// write image
		try {
			write_file->write(buffer.get_ptr(), buffer.get_size(), p_abort);
		}
		catch (foobar2000_io::exception_io e) {
			foo_discogs_exception ex;
			ex << "Error writing to file " << full_path << " [" << e.what() << "]";
			throw ex;
		}
	}
	catch (foo_discogs_exception) {
		try {
			filesystem::g_remove(full_path.get_ptr(), p_abort);
		}
		catch (foobar2000_io::exception_io) {
		}
		throw;
	}
}

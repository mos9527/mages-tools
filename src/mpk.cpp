namespace mpk {
	constexpr uint32_t MPK_MAGIC = fourCC('M', 'P', 'K', '\0');

	struct mpk_header {
		uint32_t magic{};
		uint32_t version{};
		uint64_t entries{};
		char padding[0x30]{};
	};

	struct mpk_entry {
		uint32_t compression{}; // XXX: ignored for now
		uint32_t entry_id{};
		uint64_t offset{};
		uint64_t size{};
		uint64_t size_decompressed{};
		char filename[0xE0]{};

		// i.e. "0x1e_phone_rine.dds"
		static const mpk_entry from_unpacked_filename(std::stringstream& ss) {
			mpk_entry entry{};
			CHECK(ss >> std::hex >> entry.entry_id);
			CHECK(ss.ignore() >> entry.filename);
			return entry;
		}

		const std::string to_unpacked_filename() const {
			std::stringstream ss;
			ss << "0x" << std::hex << entry_id << "_" << filename;
			return ss.str();
		}
	};
}
int main(int argc, char* argv[])
{
	argh::parser cmdl(argv, argh::parser::Mode::PREFER_PARAM_FOR_UNREG_OPTION);

	struct {
		std::string infile;
		std::string outdir;
		std::string repack;
	} args;

	auto c_outdir = cmdl({ "o", "outdir" });
	auto c_infile = cmdl({ "i", "infile" });
	auto c_repack = cmdl({ "r", "repack" });
	if (!c_outdir || !(c_infile || c_repack)) {
		std::cerr << "MAGES. PacK - MPK Unpacker/Repacker\n";
		std::cerr << "Tested against STEINS;GATE Steam & STEINS;GATE 0 Steam MPK files\n";
		std::cerr << "Note:\n";
		std::cerr << "  - The unpacked files are named by their IDs in hex, the followed by their file name (i.e. 0x1e_phone_rine.dds)\n";		
		std::cerr << "Usage: " << argv[0] << " -o <outdir> -i [infile] -r [repack]\n";
		std::cerr << "	- unpacking: " << argv[0] << " -o <outdir> -i <.mpk input file>\n";
		std::cerr << "	- repacking: " << argv[0] << " -o <outdir> -r <.mpk repacked output>\n";
		return EXIT_FAILURE;
	}
	if (c_outdir) std::getline(c_outdir, args.outdir);
	if (c_infile) std::getline(c_infile, args.infile);
	if (c_repack) std::getline(c_repack, args.repack);

	{
		using namespace std::filesystem;
		if (args.repack.size()) { /* packing */
			std::vector<std::pair<mpk::mpk_entry, path>> entries;
			size_t buffer_size = 0;
			CHECK(exists(args.outdir) && is_directory(args.outdir), "Invalid input directory");
			for (auto& path : directory_iterator(args.outdir)) {
				std::stringstream ss(path.path().filename().string());
				entries.push_back({ mpk::mpk_entry::from_unpacked_filename(ss),path });
				buffer_size = std::max(buffer_size, file_size(path));
			}
			std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {return a.first.entry_id < b.first.entry_id; });
			// Sanity check : entry IDs must be unique and monotonically increasing
			for (size_t i = 0; i < entries.size(); i++)
				CHECK(entries[i].first.entry_id == i, "Invalid unpack source folder. Note that file IDs should be contagious and no extra files is present.");
			u8vec buffer(buffer_size);
			path output = path(args.repack);
			create_directories(output.parent_path());
			FILE* fp = fopen(output.string().c_str(), "wb");
			CHECK(fp, "Failed to open output file.");
			
			mpk::mpk_header hdr{};
			hdr.magic = mpk::MPK_MAGIC;
			hdr.version = 0x020000;
			hdr.entries = entries.size();
			fwrite(&hdr, sizeof(hdr), 1, fp);
			fseek(fp, hdr.entries * sizeof(mpk::mpk_entry), SEEK_CUR);
			fseek(fp, alignUp(ftell(fp), 2048), SEEK_SET);
			for (auto& [entry, path] : entries) {
				FILE* fp_in = fopen(path.string().c_str(), "rb");
				entry.offset = ftell(fp);
				entry.size_decompressed = entry.size = file_size(path);
				fread(buffer.data(), 1, entry.size, fp_in);
				fwrite(buffer.data(), 1, entry.size, fp);
				fseek(fp, alignUp(ftell(fp), 2048), SEEK_SET);
				fclose(fp_in);
			}
			fseek(fp, sizeof(hdr), SEEK_SET);
			for (auto& [entry, path] : entries) fwrite(&entry, sizeof(mpk::mpk_entry), 1, fp);
			fclose(fp);
		}
		else { /* unpacking */
			mpk::mpk_header hdr;

			FILE* fp = fopen(args.infile.c_str(), "rb");
			fread(&hdr, sizeof(hdr), 1, fp);    
			CHECK(hdr.magic == mpk::MPK_MAGIC);

			std::vector<mpk::mpk_entry> entries(hdr.entries);
			fread(entries.data(), sizeof(mpk::mpk_entry), hdr.entries, fp);

			u8vec buffer;
			for (const auto& entry : entries) {
				path output = path(args.outdir) / path(entry.to_unpacked_filename());
				create_directories(output.parent_path());
				FILE* fp_out = fopen(output.string().c_str(), "wb");
				fseek(fp, entry.offset, SEEK_SET);
				buffer.resize(entry.size);
				fread(buffer.data(), 1, entry.size, fp);
				fwrite(buffer.data(), 1, entry.size, fp_out);
				fclose(fp_out);
			}
			fclose(fp);
		}
	}
	return EXIT_SUCCESS;
}

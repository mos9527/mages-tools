// https://github.com/blueskythlikesclouds/MikuMikuLibrary/blob/master/MikuMikuLibrary/Archives/CriMw/CpkArchive.cs
constexpr uint32_t CPK_MAGIC = fourCCBig('C', 'P', 'K', ' ');
constexpr uint32_t UTF_MAGIC = fourCCBig('@', 'U', 'T', 'F');
namespace cpk {
	static void apply_utf_table_data_mask(u8vec& data) {
		for (int i = 0, j = 25951; i < data.size(); i++, j *= 16661) data[i] ^= (j & 0xFF);
	}
	struct utf_table_header {
		uint32_t magic;
		uint32_t _pad;
		uint32_t length;
		uint32_t _pad2;
	};	
	typedef std::variant<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double, std::string, u8vec> utf_table_field_type;	
	struct utf_table_field {
		char type;
		std::string name;
		bool hasDefaultValue;
		utf_table_field_type defaultValue;
		bool isValid;
	};		
	static void utf_table_data_from_vec(u8vec& data) {
		apply_utf_table_data_mask(data);
		u8stream stream(data, true);
		uint32_t magic; stream.read(magic);
		uint32_t length; stream.read(length);
		uint16_t encoding; stream.read(encoding);
		uint16_t rowOffset; stream.read(rowOffset);
		uint32_t stringPoolOffset; stream.read(stringPoolOffset);
		uint32_t dataPoolOffset; stream.read(dataPoolOffset);
		uint32_t nameOffset; stream.read(nameOffset);
		uint16_t fieldCount; stream.read(fieldCount);
		uint16_t rowStride; stream.read(rowStride);
		uint32_t rowCount; stream.read(rowCount);
		CHECK(magic == UTF_MAGIC);
		for (int i = 0; i < fieldCount; i++) {
			char flags = stream.read<char>();
			utf_table_field field;
			field.type = flags & 0xF;
			field.name = (flags & 0x10) ? stream.read<std::string>() : "";
			field.hasDefaultValue = (flags & 0x20) != 0;
			if (field.hasDefaultValue) {
				switch (field.type) {
				case 0: field.defaultValue = stream.read<uint8_t>(); break;
				case 1: field.defaultValue = stream.read<int8_t>(); break;
				case 2: field.defaultValue = stream.read<uint16_t>(); break;
				case 3: field.defaultValue = stream.read<int16_t>(); break;
				case 4: field.defaultValue = stream.read<uint32_t>(); break;
				case 5: field.defaultValue = stream.read<int32_t>(); break;
				case 6: field.defaultValue = stream.read<uint64_t>(); break;
				case 7: field.defaultValue = stream.read<int64_t>(); break;
				case 8: field.defaultValue = stream.read<float>(); break;
				case 9: field.defaultValue = stream.read<double>(); break;
				case 0xA: field.defaultValue = stream.read<std::string>(); break;
				case 0xB:
				{
					u8vec buffer;
					uint32_t offset = stream.read<uint32_t>();
					uint32_t length = stream.read<uint32_t>();
					size_t pos = stream.tell();
					stream.seek(offset);
					buffer.resize(length);
					stream.read(buffer.data(), length);
					stream.seek(pos);
					field.defaultValue = buffer;
				}
				}
				field.isValid = (flags & 0x40) != 0;
			}
		}
		return;
	}

	struct utf_table {
		utf_table_header hdr{};		
		utf_table(FILE* fp) {
			fread(&hdr, sizeof(hdr), 1, fp);
			u8vec buffer(hdr.length);
			fread(buffer.data(), 1, hdr.length, fp);
			utf_table_data_from_vec(buffer);
		}
	};
}
int main(int argc, char* argv[]) {
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
		std::cerr << "CriPacK Unpacker/Repacker\n";
		std::cerr << "Works with CHAOS;HEAD NOAH CPK files\n";
		std::cerr << "Usage: " << argv[0] << " -o <outdir> -i [infile] -r [repack]\n";
		std::cerr << "	- unpacking: " << argv[0] << " -o <outdir> -i .cpk input file>\n";
		std::cerr << "	- repacking: " << argv[0] << " -o <outdir> -r .cpk repacked output>\n";
		return EXIT_FAILURE;
	}
	if (c_outdir) std::getline(c_outdir, args.outdir);
	if (c_infile) std::getline(c_infile, args.infile);
	if (c_repack) std::getline(c_repack, args.repack);

	{
		using namespace std::filesystem;
		if (args.repack.size()) { /* packing */
		}
		else {
			FILE* fp = fopen(args.infile.c_str(), "rb");
			cpk::utf_table table(fp);
		}
	}
	return 0;
}
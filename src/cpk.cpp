// https://github.com/blueskythlikesclouds/MikuMikuLibrary/blob/master/MikuMikuLibrary/Archives/CriMw/CpkArchive.cs
constexpr uint32_t CPK_MAGIC = fourCC('C', 'P', 'K', ' ');
constexpr uint32_t UTF_MAGIC_BIG = fourCC('F', 'T', 'U', '@');
constexpr uint32_t ITOC_MAGIC = fourCC('I', 'T', 'O', 'C');
constexpr uint32_t CPK_OFFSET = 8;
namespace cpk {
	typedef std::variant<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double, std::string, u8vec> utf_table_field_type;	
	static void apply_utf_table_data_mask(u8vec& data) {
		for (int i = 0, j = 25951; i < data.size(); i++, j *= 16661) data[i] ^= (j & 0xFF);
	}
	struct utf_table_header {
		uint32_t magic;
		uint32_t _pad;
		uint32_t length;
		uint32_t _pad2;
	};
	struct utf_table_sub_header {
		uint32_t magic;
		uint32_t length;
		uint16_t encoding;
		uint16_t rowOffset;
		uint32_t stringPoolOffset;
		uint32_t dataPoolOffset;
		uint32_t nameOffset;
		uint16_t fieldCount;
		uint16_t rowStride;
		uint32_t rowCount;
		uint32_t to_file_offset(uint32_t hdr_offset) { return hdr_offset + 8; }
	};
	struct utf_table_field {
		uint8_t type;
		std::string name;
		bool hasDefaultValue;
		bool isValid;
		std::vector<utf_table_field_type> values;
	};
	struct utf_table_stream : public u8stream {
	private:		
		std::span<uint8_t> get_null_string_data(uint32_t offset) {
			uint32_t pos = header.to_file_offset(header.stringPoolOffset) + offset; offset = pos;
			while (src[pos]) pos++;
			return { src.begin() + offset, src.begin() + pos };
		}
		std::span<uint8_t> get_data_array_data(uint32_t offset, uint32_t length) {
			uint32_t pos = header.to_file_offset(header.dataPoolOffset) + offset; offset = pos;
			pos += length;
			return { src.begin() + offset, src.begin() + pos };
		}
	public:
		utf_table_sub_header header;
		utf_table_stream(u8vec& data) : u8stream(data, true) {
			*this >> header.magic >> header.length;			
			CHECK(header.magic == UTF_MAGIC_BIG);
			*this >> header.encoding >> header.rowOffset >> header.stringPoolOffset >> header.dataPoolOffset >> header.nameOffset >> header.fieldCount >> header.rowStride >> header.rowCount;						
		}
		std::string read_null_string() {
			uint32_t offset; *this >> offset;
			auto sp = get_null_string_data(offset);
			return { sp.begin(), sp.end() };
		}
		u8vec read_data_array() {
			uint32_t offset, length; *this >> offset >> length;
			auto sp = get_data_array_data(offset, length);
			return { sp.begin(), sp.end() };
		}
		utf_table_field_type read_variant(uint32_t type) {
			switch (type) {
			case 0: return read<uint8_t>(); break;
			case 1: return read<int8_t>(); break;
			case 2: return read<uint16_t>(); break;
			case 3: return read<int16_t>(); break;
			case 4: return read<uint32_t>(); break;
			case 5: return read<int32_t>(); break;
			case 6: return read<uint64_t>(); break;
			case 7: return read<int64_t>(); break;
			case 8: return read<float>(); break;
			case 9: return read<double>(); break;
			case 0xA: return read_null_string(); break;
			case 0xB: return read_data_array(); break;
			default:
				return 0;
			};
		}
	};		

	struct utf_table {
	private:
		utf_table_header hdr;
		std::unique_ptr<utf_table_stream> stream;
		std::vector<std::string> fields_ord;
	public:
		std::map<std::string, utf_table_field> fields;
		utf_table(FILE* fp, uint32_t magic) {
			fread(&hdr, sizeof(hdr), 1, fp);
			assert(hdr.magic == magic);
			u8vec buffer(hdr.length); fread(buffer.data(), 1, hdr.length, fp);
			apply_utf_table_data_mask(buffer);	
			stream.reset(new utf_table_stream(buffer));
			// populate the fields
			for (int i = 0; i < stream->header.fieldCount; i++) {
				uint8_t flags = stream->read<uint8_t>();
				utf_table_field field;
				field.type = flags & 0xF;
				field.name = (flags & 0x10) ? stream->read_null_string() : "";
				field.hasDefaultValue = (flags & 0x20) != 0;
				field.isValid = ((flags & 0x40) != 0);
				if (field.hasDefaultValue)
					field.values.push_back(stream->read_variant(field.type));
				fields_ord.push_back(field.name);
				fields[field.name] = field;
			}
			for (int i = 0, j = 0; i < stream->header.rowCount; i++, j += stream->header.rowStride) {
				uint32_t offset = stream->header.to_file_offset(stream->header.rowOffset) + j;
				stream->seek(offset);
				for (auto const& name : fields_ord) {
					auto& field = fields[name];
					if (!field.hasDefaultValue && field.isValid) {
						field.values.push_back(stream->read_variant(field.type));						
					}
				}
			}
			return;
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
			cpk::utf_table cpk_table(fp, CPK_MAGIC);
			uint64_t ItocOffset = std::get<uint64_t>(cpk_table.fields["ItocOffset"].values[0]);			
			fseek(fp, ItocOffset, 0);
			cpk::utf_table itoc_table(fp, ITOC_MAGIC);
			return 0;
		}
	}
	return 0;
}
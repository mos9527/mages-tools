// https://github.com/blueskythlikesclouds/MikuMikuLibrary/blob/master/MikuMikuLibrary/Archives/CriMw/CpkArchive.cs
// https://github.com/wmltogether/CriPakTools/blob/ab58c3d23035c54fd9321e28e556c39652a83136/LibCPK/CPK.cs#L314
// https://github.com/kamikat/cpktools/blob/master/cpk/crilayla.py
constexpr uint32_t CPK_MAGIC = fourCC('C', 'P', 'K', ' ');
constexpr uint32_t MAGIC_BIG = fourCC('F', 'T', 'U', '@');
constexpr uint64_t CRILAYLA_MAGIC = 4705233847682945603; // CRILAYLA
constexpr uint32_t ITOC_MAGIC = fourCC('I', 'T', 'O', 'C');
constexpr uint32_t CPK_OFFSET = 8;
namespace cpk {
	struct empty_init {};
	namespace crilayla {
		static void deflate(u8stream& stream, u8vec& header, u8vec& buffer) {
			assert(!stream.is_big_endian());
			assert(stream.read<uint64_t>() == CRILAYLA_MAGIC);

			uint32_t uncompressed_size, compressed_size;
			stream >> uncompressed_size >> compressed_size;

			header.resize(0x100);
			stream.read_at(header.data(), 0x100, compressed_size + 0x10, false);

			uint32_t data_size = uncompressed_size;
			uint32_t data_written = 0;
			buffer.resize(data_size);

			std::span<uint8_t> compressed(stream.begin(), stream.begin() + compressed_size);
			std::reverse(compressed.begin(), compressed.end());

			u8vec bits; bits.reserve(compressed_size * 8);
			for (auto const& byte : compressed)
				for (int i = 0; i < 8; i++)
					bits.push_back((byte >> (8 - i - 1) & 1)); // LE
			u8stream bitstream(std::move(bits), false);

			auto read_n = [&bitstream](char nbits) -> uint16_t {
				uint16_t num = 0;
				for (int i = 0; i < nbits && bitstream.remain(); i++) {
					uint8_t bit; bitstream >> bit;
					num |= bit << (nbits - i - 1); // LE
				}
				return num;
			};
			auto all_n_bits = [](auto value, char n) -> bool{
				return value == (1 << n) - 1;
			};

			while (data_written < data_size)
			{
				uint8_t ctl; bitstream >> ctl;
				if (ctl) {
					auto offset = read_n(13) + 3; // backwards from the *back* of the output stream
					size_t ref_count = 3; // previous bytes referenced. 3 minimum
					const u8vec vle_n_bits { 2, 3, 5, 8 };
					for (int i = 0, n_bits = vle_n_bits[0];;i++, i = std::min(i,3), n_bits = vle_n_bits[i]) {
						uint16_t vle_length = read_n(n_bits);
						ref_count += vle_length;
						if (!all_n_bits(vle_length, n_bits)) 
							break;
					}
					// fill in the referenced bytes from the *back* of the output buffer
					offset = data_size - 1 - data_written + offset;
					while (ref_count--) {
						buffer[data_size - 1 - data_written] = buffer[offset--];
						data_written++;
					}
				}
				else {
					uint8_t byte = read_n(8); // verbatim byte. into the back.
					buffer[data_size - 1 - data_written] = byte;
					data_written++;
				}

			}
		}
	};
	
	namespace utf {
		typedef std::variant<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double, std::string, u8vec> field_type;
		struct table_header {
			uint32_t magic;
			uint32_t _pad;
			uint32_t length;
			uint32_t _pad2;
		};
		struct table_sub_header {
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
			constexpr uint32_t to_file_offset(uint32_t hdr_offset) { return hdr_offset + 8; }
			constexpr uint32_t from_file_offset(uint32_t file_offse) { return file_offse - 8; }
		};
		struct table_field {
			uint8_t type{};
			std::string name;
			bool hasDefaultValue{};
			bool isValid{};
			std::vector<field_type> values;
		};
		struct table_stream : public u8stream {
		private:
			std::span<uint8_t> get_null_string_data(uint32_t offset) {
				uint32_t pos = header.to_file_offset(header.stringPoolOffset) + offset; offset = pos;
				while (buffer[pos]) pos++;
				return { buffer.begin() + offset, buffer.begin() + pos };
			}
			std::span<uint8_t> get_data_array_data(uint32_t offset, uint32_t length) {
				uint32_t pos = header.to_file_offset(header.dataPoolOffset) + offset; offset = pos;
				pos += length;
				return { buffer.begin() + offset, buffer.begin() + pos };
			}
			void populate_header() {
				*this >> header.magic >> header.length;
				CHECK(header.magic == MAGIC_BIG);
				*this >> header.encoding >> header.rowOffset >> header.stringPoolOffset >> header.dataPoolOffset >> header.nameOffset >> header.fieldCount >> header.rowStride >> header.rowCount;
			}
		public:
			table_sub_header header{};
			table_stream(u8vec&& buffer) : u8stream(std::move(buffer), true) {
				populate_header();
			}
			table_stream(u8vec const& buffer) : u8stream(buffer, true) {
				populate_header();
			}
			std::string read_null_string() {
				uint32_t offset; *this >> offset;
				auto sp = get_null_string_data(offset);
				return { sp.begin(), sp.end() };
			}
			void write_null_string(std::string const& str, uint32_t offset) {
				write_at((void*)str.data(), str.size(), offset, false);
			}
			u8vec read_data_array() {
				uint32_t offset, length; *this >> offset >> length;
				auto sp = get_data_array_data(offset, length);
				return { sp.begin(), sp.end() };
			}
			void write_data_array(u8vec const& buffer, uint32_t offset) {				
				write_at((void*)buffer.data(), buffer.size(), offset, false);
			}
			field_type read_variant(uint32_t type) {
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
		struct table {
		private:
			table_header hdr;
			table_stream stream;
			std::vector<std::string> fields_ord;			
			void populate_fields() {
				for (int i = 0; i < stream.header.fieldCount; i++) {
					uint8_t flags = stream.read<uint8_t>();
					table_field field;
					field.type = flags & 0xF;
					field.name = (flags & 0x10) ? stream.read_null_string() : "";
					field.hasDefaultValue = (flags & 0x20) != 0;
					field.isValid = ((flags & 0x40) != 0);
					if (field.hasDefaultValue)
						field.values.push_back(stream.read_variant(field.type));
					fields_ord.push_back(field.name);
					fields[field.name] = field;
				}
				for (int i = 0, j = 0; i < stream.header.rowCount; i++, j += stream.header.rowStride) {
					uint32_t offset = stream.header.to_file_offset(stream.header.rowOffset) + j;
					stream.seek(offset);
					for (auto const& name : fields_ord) {
						auto& field = fields[name];
						if (!field.hasDefaultValue && field.isValid) {
							field.values.push_back(stream.read_variant(field.type));
						}
					}
				}
			}
		public:
			std::map<std::string, table_field> fields;
			static u8vec read_table_data(FILE* fp, uint32_t magic, bool masked) {
				table_header hdr;
				fread(&hdr, sizeof(hdr), 1, fp);
				assert(hdr.magic == magic);
				u8vec buffer(hdr.length); fread(buffer.data(), 1, hdr.length, fp);
				if (masked) {
					for (int i = 0, j = 25951; i < buffer.size(); i++, j *= 16661)
						buffer[i] ^= (j & 0xFF);
				}
				return std::move(buffer);
			}
			table(u8vec&& buffer) : stream(std::move(buffer)) {
				populate_fields();
			}
			table(u8vec const& buffer) : stream(buffer) {
				populate_fields();
			}
			uint32_t get_row_count() const { return stream.header.rowCount; }
		};
	}
	
	struct cpk {
	private:
		utf::table header;
	public:
		struct file_entry {
			uint32_t id;
			uint32_t offset;
			uint32_t size;
			uint32_t size_decompressed;
		};
		std::vector<file_entry> files;
		cpk(FILE* fp) : header(utf::table::read_table_data(fp, CPK_MAGIC, true)) {
			uint64_t ItocOffset = std::get<uint64_t>(header.fields["ItocOffset"].values[0]);
			uint64_t ContentOffset = std::get<uint64_t>(header.fields["ContentOffset"].values[0]);
			uint16_t Align = std::get<uint16_t>(header.fields["Align"].values[0]);
			fseek(fp, ItocOffset, 0);			
			utf::table itoc_table(utf::table::read_table_data(fp, ITOC_MAGIC, true));
			utf::table dataL(std::get<u8vec>(itoc_table.fields["DataL"].values[0]));
			utf::table dataH(std::get<u8vec>(itoc_table.fields["DataH"].values[0]));
			files.reserve(dataH.get_row_count() + dataL.get_row_count());
			auto field_cast = [](utf::table_field& field, size_t index) -> uint32_t {
				return field.type == 0x4 ? std::get<uint32_t>(field.values[index]) : std::get<uint16_t>(field.values[index]);
			};
			auto populate_file_ids = [&](utf::table& buffer) {
				for (uint32_t i = 0; i < buffer.get_row_count(); i++) {
					files.push_back({
						std::get<uint16_t>(buffer.fields["ID"].values[i]),
						0,
						field_cast(buffer.fields["FileSize"], i),
						field_cast(buffer.fields["ExtractSize"], i)
					});											
				}
			};
			populate_file_ids(dataH); populate_file_ids(dataL);
			std::sort(files.begin(), files.end(), PRED(lhs.id < rhs.id));
			uint32_t offset = ContentOffset;
			for (auto& file : files) {
				file.offset = offset;
				offset += file.size; offset = alignUp(offset, Align);
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
			throw std::exception("not implemented");
		}
		else { /* unpacking */
			FILE* fp = fopen(args.infile.c_str(), "rb");
			CHECK(fp);
			cpk::cpk pack(fp);
			uint32_t id = 0;
			for (auto& file : pack.files) {				
				u8stream buffer_stream(file.size, false);
				fseek(fp, file.offset, 0);
				fread(buffer_stream.data(), 1, file.size, fp);

				path output = path(args.outdir) / std::to_string(id++);
				FILE* fout = fopen(output.string().c_str(), "wb");

				if (file.size != file.size_decompressed) {
					static u8vec deflate_header, deflate_data;
					cpk::crilayla::deflate(buffer_stream, deflate_header, deflate_data);
					fwrite(deflate_header.data(), 1, deflate_header.size(), fout);
					fwrite(deflate_data.data(), 1, deflate_data.size(), fout);
					fclose(fout);
				}
				else {
					fwrite(buffer_stream.data(), 1, buffer_stream.size(), fout);
					fclose(fout);
				}
			}
			return 0;
		}
	}
	return 0;
}
// https://github.com/blueskythlikesclouds/MikuMikuLibrary/blob/master/MikuMikuLibrary/Archives/CriMw/CpkArchive.cs
// https://github.com/wmltogether/CriPakTools/blob/ab58c3d23035c54fd9321e28e556c39652a83136/LibCPK/CPK.cs#L314
// https://github.com/kamikat/cpktools/blob/master/cpk/crilayla.py
constexpr uint32_t CPK_MAGIC = fourCC('C', 'P', 'K', ' ');
constexpr uint32_t CPK_MAGIC_BIG = fourCC(' ', 'K', 'P', 'C');
constexpr uint32_t UTF_MAGIC = fourCC('@', 'U', 'T', 'F');
constexpr uint32_t UTF_MAGIC_BIG = fourCC('F', 'T', 'U', '@');
constexpr uint32_t ITOC_MAGIC = fourCC('I', 'T', 'O', 'C');
constexpr uint32_t ITOC_MAGIC_BIG = fourCC('C', 'O', 'T', 'I');
constexpr uint64_t CRILAYLA_MAGIC = 4705233847682945603; // CRILAYLA

namespace cpk {
	namespace crilayla {
		static void deflate(u8stream& stream, u8vec& header, u8vec& buffer) {
			CHECK(!stream.is_big_endian());
			CHECK(stream.read<uint64_t>() == CRILAYLA_MAGIC);

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
		const uint8_t INVALID_TYPE = 255;
		typedef std::variant<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double, std::string, u8vec> field_type;
		enum class field_type_enum {
			UINT8 = 0,
			INT8 = 1,
			UINT16 = 2,
			INT16 = 3,
			UINT32 = 4,
			INT32 = 5,
			UINT64 = 6,
			INT64 = 7,
			FLOAT = 8,
			DOUBLE = 9,
			STRING = 0xA,
			DATA_ARRAY = 0xB,
			INVALID = -1
		};
		template<typename Cast> static Cast field_cast(utf::field_type const& field) {
			return std::visit([&](auto&& arg) -> Cast {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_convertible_v<T, Cast>) return arg;
				CHECK(false, "Invalid field cast");
				return Cast{};
			}, field);
		}
		struct table;
		struct table_header {
			uint32_t magic;
			uint32_t _pad;
			uint64_t length;
		};
		struct table_sub_header {
			uint32_t magic;
			uint32_t length;		
			uint32_t rowOffset;
			uint32_t stringPoolOffset;
			uint32_t dataPoolOffset;
			uint32_t nameOffset;
			uint16_t fieldCount;
			uint16_t rowStride;
			uint32_t rowCount;
			constexpr uint32_t to_block_offset(uint32_t hdr_offset) { return hdr_offset + 8; }
			constexpr uint32_t from_block_offset(uint32_t blk_offset) { return blk_offset - 8; }
		};
		struct table_field {
			friend table;		
			std::string name;
			bool hasDefaultValue{ false };
			bool isValid{ false };
			field_type_enum type{ field_type_enum::INVALID };
			std::vector<field_type> values;
			table_field() = default;
			table_field(std::string const& name) : name(name) {}
			table_field(std::string const& name, field_type_enum type, bool valid) : name(name), type(type), isValid(valid) {}
			table_field(std::string const& name, std::vector<field_type> const& values) : name(name), values(values) {
				isValid = true;
				type = (field_type_enum)values.front().index();
			}
			void set_flags(field_type_enum ntype, bool valid = false) { type = ntype, isValid = valid; }
			void push_back(field_type const& value) {	
				if (type == field_type_enum::INVALID) type = (field_type_enum)value.index();
				isValid = true;
				CHECK((field_type_enum)value.index() == type, "Invalid field type");
				values.push_back(value);
			}
		};
		struct table_stream : public u8stream {
			friend table;
		private:
			std::span<uint8_t> get_null_string_data(uint32_t offset) {
				uint32_t pos = header.to_block_offset(header.stringPoolOffset) + offset; offset = pos;
				while (buffer[pos]) pos++;
				return { buffer.begin() + offset, buffer.begin() + pos };
			}
			std::span<uint8_t> get_data_array_data(uint32_t offset, uint32_t length) {
				uint32_t pos = header.to_block_offset(header.dataPoolOffset) + offset; offset = pos;
				pos += length;
				return { buffer.begin() + offset, buffer.begin() + pos };
			}
			void read_header() {
				*this >> header.magic >> header.length;
				CHECK(header.magic == UTF_MAGIC_BIG);
				*this >> header.rowOffset >> header.stringPoolOffset >> header.dataPoolOffset >> header.nameOffset >> header.fieldCount >> header.rowStride >> header.rowCount;
			}
			void write_header() {
				*this << header.magic << header.length;
				*this << header.rowOffset << header.stringPoolOffset << header.dataPoolOffset << header.nameOffset << header.fieldCount << header.rowStride << header.rowCount;
			}
		public:
			table_sub_header header{};
			table_stream(uint32_t magic) : u8stream(0, true) { header.magic = magic; }
			table_stream(u8vec&& buffer) : u8stream(std::move(buffer), true) { read_header(); }
			table_stream(u8vec const& buffer) : u8stream(buffer, true) { read_header(); }
			void seek_after_header() { seek(sizeof(table_sub_header)); }
			std::string read_null_string() {
				uint32_t offset; *this >> offset;
				auto sp = get_null_string_data(offset);
				return { sp.begin(), sp.end() };
			}
			size_t write_null_string(std::string const& str, u8stream& stringPool) {
				*this << (uint32_t)stringPool.tell();
				size_t size = stringPool.write((void*)str.c_str(), str.size() + 1, false);
				return size;
			}
			u8vec read_data_array() {
				uint32_t offset, length; *this >> offset >> length;
				auto sp = get_data_array_data(offset, length);
				return { sp.begin(), sp.end() };
			}
			size_t write_data_array(u8vec const& buffer, u8stream& dataPool) {
				*this << (uint32_t)dataPool.tell() << (uint32_t)buffer.size();
				size_t size = dataPool.write((void*)buffer.data(), buffer.size(), false);	
				return size;
			}
			constexpr static size_t get_type_size(field_type_enum type) {
				using enum field_type_enum;
				switch (type) {
					case UINT8  : return sizeof(uint8_t); break;
					case INT8   : return sizeof(int8_t); break;
					case UINT16 : return sizeof(uint16_t); break;
					case INT16  : return sizeof(int16_t); break;
					case UINT32 : return sizeof(uint32_t); break;
					case INT32  : return sizeof(int32_t); break;
					case UINT64 : return sizeof(uint64_t); break;
					case INT64  : return sizeof(int64_t); break;
					case FLOAT  : return sizeof(float); break;
					case DOUBLE : return sizeof(double); break;
					case STRING:
					case DATA_ARRAY: 
						return sizeof(uint32_t); break;
					default:
						return 0;
				}
			};
			field_type read_variant(field_type_enum type) {
				using enum field_type_enum;
				switch (type) {
					case UINT8 : return read<uint8_t>(); break;
					case INT8  : return read<int8_t>(); break;
					case UINT16: return read<uint16_t>(); break;
					case INT16 : return read<int16_t>(); break;
					case UINT32: return read<uint32_t>(); break;
					case INT32 : return read<int32_t>(); break;
					case UINT64: return read<uint64_t>(); break;
					case INT64 : return read<int64_t>(); break;
					case FLOAT : return read<float>(); break;
					case DOUBLE: return read<double>(); break;
					case STRING: return read_null_string(); break;
					case DATA_ARRAY: return read_data_array(); break;
					default:
						return 0;
				};
			}
			void write_variant(field_type const& value, u8stream& stringPool, u8stream& dataPool) {
				std::visit([&](auto&& arg) {
					using T = std::decay_t<decltype(arg)>;
					if constexpr (std::is_same_v<T, std::string>) {
						write_null_string(arg, stringPool);						
					}
					else if constexpr (std::is_same_v<T, u8vec>) {
						write_data_array(arg, dataPool);
					}
					else {
						T larg = arg;
						write(larg);
					}
				}, value);
			}
		};
		struct table {
		private:
			table_header hdr{};
			table_stream stream;
			void read_fields() {
				stream.seek(0);
				stream.read_header();
				fields.reset();
				for (int i = 0; i < stream.header.fieldCount; i++) {
					uint8_t flags = stream.read<uint8_t>();
					table_field field;
					field.type = (field_type_enum)(flags & 0xF);
					field.name = (flags & 0x10) ? stream.read_null_string() : "";
					field.hasDefaultValue = (flags & 0x20) != 0;
					field.isValid = ((flags & 0x40) != 0);
					if (field.hasDefaultValue)
						field.push_back(stream.read_variant((field_type_enum)field.type));
					fields.insert(field.name, field);
				}				
				for (int i = 0, j = 0; i < stream.header.rowCount; i++, j += stream.header.rowStride) {
					uint32_t offset = stream.header.to_block_offset(stream.header.rowOffset) + j;
					stream.seek(offset);
					for (auto const& name : fields.ord) {
						auto& field = fields.data[name];
						if (!field.hasDefaultValue && field.isValid) {
							field.push_back(stream.read_variant((field_type_enum)field.type));
						}
					}
				}
			}
			void write_fields() {
				stream.seek_after_header();
				u8stream stringPool(0, false), dataPool(0, false);
				// CPK string pool always has two strings before anything. And the look up process skips the first two char** as well.
				// See: __int64 __fastcall criUtfRtv_LookUp(struct_a1 *a1, char *flag, char **strings)
				stringPool << u8vec{'<','N','U','L','L','>','\0','E','l',' ','P','s','y',' ', 'K','o','n','g','r','o','o','\0'};
				for (auto const& name : fields.ord) {
					auto& field = fields.data[name];
					uint8_t flags = (int)field.type;
					if (field.name.size()) flags |= 0x10;
					if (field.hasDefaultValue) flags |= 0x20;
					if (field.isValid) flags |= 0x40;
					stream.write(flags);
					if (field.name.size()) stream.write_null_string(field.name, stringPool);
					if (field.hasDefaultValue) stream.write_variant(field.values[0], stringPool, dataPool);
				}
				uint32_t rowCount = fields.ord.size() ? fields.data[fields.ord[0]].values.size() : 0, rowStride = 0;
				stream.header.rowOffset = stream.header.from_block_offset(stream.tell());
				for (int i = 0; i < rowCount; i++) {
					for (auto const& name : fields.ord) {
						auto& field = fields.data[name];
						if (!field.hasDefaultValue && field.isValid) {
							stream.write_variant(field.values[i], stringPool, dataPool);
							if (i == 0) rowStride += stream.get_type_size((field_type_enum)field.type);
						}
					}
				}
				stream.header.fieldCount = fields.ord.size();			
				stream.header.rowCount = rowCount, stream.header.rowStride = rowStride;
				stream.header.stringPoolOffset = stream.header.from_block_offset(stream.tell());
				stream << stringPool.buffer;
				stream.header.dataPoolOffset = stream.header.from_block_offset(stream.tell());
				stream << dataPool.buffer;
				stream.header.length = stream.size() - 8;  //  E06100311:UTF header size error. (%d)+(8)>(%d). This DOES NOT the magic & padding
				stream.seek(0);
				stream.write_header();
			}
		public:
			struct field_storage {
				friend table;
			private:
				std::vector<std::string> ord;
				std::map<std::string, table_field> data;
			public:
				bool contains(std::string const& name) { return data.contains(name); }
				table_field& insert(std::string const& name, table_field const& field) {
					ord.push_back(name);
					return data.insert({ name, field }).first->second;
				}
				table_field& insert(std::string const& name) {					
					return insert(name, table_field(name));
				}
				table_field& operator[](std::string const& name) {
					if (!data.contains(name)) insert(name);
					return data.at(name); 
				}
				void reset() { ord.clear(); data.clear(); }
			} fields;
			static void mask_table_data(u8vec& buffer) {
				for (int i = 0, j = 25951; i < buffer.size(); i++, j *= 16661)
					buffer[i] ^= (j & 0xFF);
			}
			static u8vec read_table_data(FILE* fp, uint32_t magic) {
				table_header hdr;
				fread(&hdr, sizeof(hdr), 1, fp);
				CHECK(hdr.magic == magic);
				u8vec buffer(hdr.length); fread(buffer.data(), 1, hdr.length, fp);				
				if (memcmp(buffer.data(), &UTF_MAGIC, sizeof(uint32_t)) != 0) {
					// Some CPK files has a simple XOR cipher
					mask_table_data(buffer);
				}
				return buffer;
			}
			
			table(uint32_t magic) : stream(magic) {}
			table(u8vec&& buffer) : stream(std::move(buffer)) {
				read_fields();
			}
			table(u8vec const& buffer) : stream(buffer) {
				read_fields();
			}
			uint32_t get_row_count() const { return stream.header.rowCount; }
			table_stream& commit_to_stream() {
				write_fields(); 
				return stream;
			}
			void reload_from_stream() {
				read_fields();
			}
		};
	}

	struct cpk {		
		struct file_entry {
			uint16_t id;
			std::string path;
			uint64_t size;
		};
		typedef std::vector<file_entry> file_entries;		
		struct packed_file_entry {			
			uint16_t id;
			uint64_t offset;
			uint64_t size;
			uint64_t size_decompressed;
		};
		typedef std::vector<packed_file_entry> packed_file_entries;

		static void pack(FILE* fp, file_entries& files) {
			using enum utf::field_type_enum;
			const uint16_t Align = 2048;
			const uint64_t ItocOffset = 0x800;
			// ITOC
			std::sort(files.begin(), files.end(), PRED(lhs.id < rhs.id));
			utf::table Itoc(UTF_MAGIC_BIG), DataL(UTF_MAGIC_BIG), DataH(UTF_MAGIC_BIG);
			DataL.fields["ID"].set_flags(UINT16);
			DataL.fields["FileSize"].set_flags(UINT16);
			DataL.fields["ExtractSize"].set_flags(UINT16);
			for (auto& file : files) {
				DataH.fields["ID"].push_back((uint16_t)file.id);
				DataH.fields["FileSize"].push_back((uint32_t)file.size);
				DataH.fields["ExtractSize"].push_back((uint32_t)file.size);
			}
			Itoc.fields["DataL"].push_back(DataL.commit_to_stream().buffer);
			Itoc.fields["DataH"].push_back(DataH.commit_to_stream().buffer);
			auto& ItocBuffer = Itoc.commit_to_stream().buffer;
			// This length offset only applies to non-CPK header packets			
			utf::table_header itocHdr{
				.magic = ITOC_MAGIC,
				.length = (uint32_t)ItocBuffer.size() + 0x10 
			};
			fseek(fp, ItocOffset, SEEK_SET);			
			fwrite(&itocHdr, sizeof(itocHdr), 1, fp);
			utf::table::mask_table_data(ItocBuffer);
			fwrite(ItocBuffer.data(), 1, ItocBuffer.size(), fp);
			// Content
			uint64_t ContentOffset = alignUp(ftell(fp), Align);
			fseek(fp, ContentOffset, SEEK_SET);
			u8vec buffer;
			for (auto& file : files) {
				FILE* fin = fopen(file.path.c_str(), "rb");
				if (fin) {
					buffer.resize(file.size);
					fread(buffer.data(), 1, file.size, fin);
					fwrite(buffer.data(), 1, file.size, fp);
					fclose(fin);
					fseek(fp, alignUp(ftell(fp), Align), SEEK_SET);
				}
			}
			utf::table CPK(UTF_MAGIC_BIG);
			CPK.fields["ContentOffset"].push_back((uint64_t)ContentOffset);
			CPK.fields["ContentSize"].push_back((uint64_t)(ftell(fp) - ContentOffset));
			/* ITOC */
			CPK.fields["ItocOffset"].push_back((uint64_t)ItocOffset);
			CPK.fields["ItocSize"].push_back((uint64_t)itocHdr.length);
			/* CPK Flags*/
			CPK.fields["Align"].push_back((uint16_t)Align);
			CPK.fields["CpkMode"].push_back((uint32_t)0x00);
			auto& CPKBuffer = CPK.commit_to_stream().buffer;
			utf::table::mask_table_data(CPKBuffer);
			fseek(fp, 0, SEEK_SET);
			utf::table_header cpkHdr{
				.magic = CPK_MAGIC,
				.length = (uint32_t)CPKBuffer.size()
			};
			fwrite(&cpkHdr, sizeof(cpkHdr), 1, fp);
			fwrite(CPKBuffer.data(), 1, CPKBuffer.size(), fp);
			fclose(fp);
		}
		static packed_file_entries unpack(FILE* fp) {
			packed_file_entries files;
			utf::table CPK(utf::table::read_table_data(fp, CPK_MAGIC));
			uint64_t ItocOffset = utf::field_cast<uint64_t>(CPK.fields["ItocOffset"].values[0]);
			uint64_t ContentOffset = utf::field_cast<uint64_t>(CPK.fields["ContentOffset"].values[0]);
			uint16_t Align = std::get<uint16_t>(CPK.fields["Align"].values[0]);
			fseek(fp, ItocOffset, SEEK_SET);
			utf::table Itoc(utf::table::read_table_data(fp, ITOC_MAGIC));
			auto populate_file_ids = [&](utf::table& table) {
				for (uint32_t i = 0; i < table.get_row_count(); i++) {
					files.push_back({
						std::get<uint16_t>(table.fields["ID"].values[i]),
						0,
						utf::field_cast<uint64_t>(table.fields["FileSize"].values[i]),
						utf::field_cast<uint64_t>(table.fields["ExtractSize"].values[i])
					});											
				}
			};
			if (Itoc.fields.contains("DataL")) { utf::table DataL(std::get<u8vec>(Itoc.fields["DataL"].values[0])); populate_file_ids(DataL); }
			if (Itoc.fields.contains("DataH")) { utf::table DataH(std::get<u8vec>(Itoc.fields["DataH"].values[0])); populate_file_ids(DataH); }
			std::sort(files.begin(), files.end(), PRED(lhs.id < rhs.id));
			uint64_t offset = ContentOffset;
			for (auto& file : files) {
				file.offset = offset;
				offset += file.size; offset = alignUp(offset, Align);
			}
			return files;
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
		std::cerr << "Works w/ CHAOS;HEAD NOAH Steam CPK files\n";
		std::cerr << "Note:\n";
		std::cerr << "  - The unpacked files are named by their IDs (i.e 0,1,2, ...). Which should also be the case for the files that's to be repacked.\n";
		std::cerr << "  - There's a maximum file size limit of 2GB. This is an inherent limitation coming from CriWare itself.\n";
		std::cerr << "Usage: " << argv[0] << " -o <outdir> -i [infile] -r [repack]\n";
		std::cerr << "	- unpacking: " << argv[0] << " -o <outdir> -i <.cpk input file>\n";
		std::cerr << "	- repacking: " << argv[0] << " -o <outdir> -r <.cpk repacked output>\n";
		return EXIT_FAILURE;
	}
	if (c_outdir) std::getline(c_outdir, args.outdir);
	if (c_infile) std::getline(c_infile, args.infile);
	if (c_repack) std::getline(c_repack, args.repack);

	{
		using namespace std::filesystem;
		if (args.repack.size()) { /* packing */
			path output = path(args.repack);
			create_directories(output.parent_path());
			FILE* fp = fopen(output.string().c_str(), "wb");
			CHECK(fp, "Failed to open output file");
			cpk::cpk::file_entries files;
			uint32_t id = 0;
			CHECK(exists(args.outdir) && is_directory(args.outdir), "Invalid input directory");
			for (auto& path : directory_iterator(args.outdir)) {
				std::stringstream ss(path.path().filename().string());
				uint16_t id; ss >> id;
				files.push_back(cpk::cpk::file_entry{
					.id = id,
					.path = path.path().string(),
					.size = static_cast<uint32_t>(file_size(path))
				});
			}
			cpk::cpk::pack(fp, files);
		}
		else { /* unpacking */
			FILE* fp = fopen(args.infile.c_str(), "rb");
			CHECK(fp, "Failed to open input file");
			cpk::cpk::packed_file_entries files = cpk::cpk::unpack(fp);
			uint32_t id = 0;
			for (auto& file : files) {
				u8stream buffer_stream(file.size, false);
				fseek(fp, file.offset, SEEK_SET);
				fread(buffer_stream.data(), 1, file.size, fp);

				path output = path(args.outdir) / std::to_string(id++);
				create_directories(output.parent_path());
				FILE* fout = fopen(output.string().c_str(), "wb");
				CHECK(fout, "Failed to open output file");
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
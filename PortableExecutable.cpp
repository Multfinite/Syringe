#include <algorithm>
#include <cassert>
#include <share.h>

#include "PortableExecutable.h"
#include "Log.h"

PortableExecutable::PortableExecutable()
{
	fHandle = nullptr;
}

PortableExecutable::~PortableExecutable()
{
	CloseHandle();

	for(size_t i = 0; i < vecImports.size(); i++)
	{
		if(vecImports[i].lpName) {
			free(vecImports[i].lpName);
		}

		for(size_t k = 0; k < vecImports[i].vecThunkData.size(); k++)
		{
			if(vecImports[i].vecThunkData[k].lpName) {
				free(vecImports[i].vecThunkData[k].lpName);
			}
		}

		vecImports[i].vecThunkData.clear();
	}
	vecImports.clear();
}

DWORD PortableExecutable::VirtualToRaw(DWORD dwAddress) const //address without the image base!
{
	IMAGE_SECTION_HEADER uSection;
	for(size_t i = 0; i < vecPESections.size(); i++)
	{
		uSection = vecPESections[i];
		if(dwAddress >= uSection.VirtualAddress &&
			dwAddress < uSection.VirtualAddress + uSection.SizeOfRawData)
		{
			DWORD dwDifference = dwAddress - uSection.VirtualAddress;
			return uSection.PointerToRawData + dwDifference;
		}
	}

	return 0;
}

bool PortableExecutable::ReadFile(std::string filename)
{
	if(Filename.empty() && !filename.empty())
	{
		Filename = std::move(filename);

		if(FILE* F = _fsopen(Filename.c_str(), "rb", _SH_DENYWR))
		{
			//DOS Header
			fread(&uDOSHeader, sizeof(IMAGE_DOS_HEADER), 1, F);
			if(uDOSHeader.e_magic == IMAGE_DOS_SIGNATURE)
			{
				//PE Header
				fseek(F, uDOSHeader.e_lfanew, SEEK_SET);
				fread(&uPEHeader, sizeof(IMAGE_NT_HEADERS), 1, F);
				if(uPEHeader.Signature == IMAGE_NT_SIGNATURE)
				{
					//Sections
					IMAGE_SECTION_HEADER current_section;
					if(uPEHeader.FileHeader.NumberOfSections > 0)
					{
						for(int i = 0; i < uPEHeader.FileHeader.NumberOfSections; i++)
						{
							fread(&current_section, sizeof(IMAGE_SECTION_HEADER), 1, F);
							vecPESections.push_back(current_section);
						}
					}

					//Imports
					int import_desc_count = uPEHeader.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size / sizeof(IMAGE_IMPORT_DESCRIPTOR) - 1; //minus one for end of array
					if(import_desc_count > 0)
					{
						std::vector<IMAGE_IMPORT_DESCRIPTOR> import_desc(import_desc_count);

						fseek(
							F,
							(long)VirtualToRaw(uPEHeader.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress),
							SEEK_SET);

						for(int i = 0; i < import_desc_count; i++) {
							fread(&import_desc[i], sizeof(IMAGE_IMPORT_DESCRIPTOR), 1, F);
						}

						PEImport current_import;
						char name_buf[0x100] = "\0";

						for(int i = 0; i < import_desc_count; i++)
						{
							current_import.vecThunkData.clear();
							current_import.uDesc = import_desc[i];
							if(!current_import.uDesc.Name) {
								break;
							}

							fseek(F, (long)VirtualToRaw(current_import.uDesc.Name), SEEK_SET);
							fgets(name_buf, 0x100, F);

							current_import.lpName = _strdup(name_buf);

							//Thunks
							PEThunkData current_thunk;
							current_thunk.lpName = nullptr;

							fseek(F, (long)VirtualToRaw(current_import.uDesc.FirstThunk), SEEK_SET);

							for(fread(&current_thunk.uThunkData.u1, sizeof(IMAGE_THUNK_DATA), 1, F);
								current_thunk.uThunkData.u1.AddressOfData;
								fread(&current_thunk.uThunkData.u1, sizeof(IMAGE_THUNK_DATA), 1, F))
							{
								current_import.vecThunkData.push_back(current_thunk);
							}

							for(size_t k = 0; k < current_import.vecThunkData.size(); k++)
							{
								current_import.vecThunkData[k].Address = current_import.uDesc.FirstThunk + k * sizeof(IMAGE_THUNK_DATA);

								if(current_import.vecThunkData[k].uThunkData.u1.AddressOfData & 0x80000000)
								{
									current_import.vecThunkData[k].bIsOrdinal = true;
									current_import.vecThunkData[k].Ordinal = current_import.vecThunkData[k].uThunkData.u1.AddressOfData & 0x7FFFFFFF;
								}
								else
								{
									current_import.vecThunkData[k].bIsOrdinal = false;

									fseek(F, (long)VirtualToRaw(current_import.vecThunkData[k].uThunkData.u1.AddressOfData & 0x7FFFFFFF), SEEK_SET);
									fread(&current_import.vecThunkData[k].wWord, 2, 1, F);
									fgets(name_buf, 0x100, F);
									current_import.vecThunkData[k].lpName = _strdup(name_buf);
								}
							}

							vecImports.push_back(current_import);
						}
					}

					return true;
				}
			}
			fclose(F);
		}
	}

	return false;
}

DWORD PortableExecutable::GetImageBase() const {
	return this->GetPEHeader()->OptionalHeader.ImageBase;
}

bool PortableExecutable::ReadBytes(DWORD dwRawAddress, size_t Size, void *Dest) const {
	if(!Filename.empty()) {
		assert(fHandle);
		auto success = false;
		if(!fseek(fHandle, long(dwRawAddress), SEEK_SET)) {
			success = (fread(Dest, Size, 1, fHandle) == 1);
		}
		return success;
	}
	return false;
}

bool PortableExecutable::ReadCString(DWORD dwRawAddress, std::string &Result) const {
	if(!Filename.empty()) {
		assert(fHandle);
		if(!fseek(fHandle, long(dwRawAddress), SEEK_SET)) {
			const size_t sz = 0x100;
			char tmpBuf[sz];

			tmpBuf[0] = 0;
			if(fread(tmpBuf, 1, sz, fHandle) == sz) {
				tmpBuf[sz - 1] = 0;
				Result.assign(tmpBuf);
				return true;
			}
		}
	}
	return false;
}

const IMAGE_SECTION_HEADER * PortableExecutable::FindSection(const char *findName) const {
	const size_t slen = strlen(findName);
	auto found = std::find_if(vecPESections.begin(), vecPESections.end(), [slen, findName](const decltype(*(vecPESections.begin())) & section) -> bool {
		return !memcmp(findName, section.Name, slen);
	});

	if(found == vecPESections.end()) {
		return nullptr;
	} else {
		return &(*found);
	}
}

void PortableExecutable::OpenHandle() {
	if(!Filename.empty()) {
		if(fHandle) {
			fclose(fHandle);
		}
		fHandle = _fsopen(Filename.c_str(), "rb", _SH_DENYNO);
	} else {
		fHandle = nullptr;
	}
}

void PortableExecutable::CloseHandle() {
	if(fHandle) {
		fclose(fHandle);
		fHandle = nullptr;
	}
}

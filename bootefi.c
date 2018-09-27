#include <efi.h>
#include <efiapi.h>
#include <efilib.h>
#include <elf.h>
#include <stddef.h>

EFI_SYSTEM_TABLE *ST;

const CHAR16 *memory_types[] = 
{
    L"EfiReservedMemoryType",
    L"EfiLoaderCode",
    L"EfiLoaderData",
    L"EfiBootServicesCode",
    L"EfiBootServicesData",
    L"EfiRuntimeServicesCode",
    L"EfiRuntimeServicesData",
    L"EfiConventionalMemory",
    L"EfiUnusableMemory",
    L"EfiACPIReclaimMemory",
    L"EfiACPIMemoryNVS",
    L"EfiMemoryMappedIO",
    L"EfiMemoryMappedIOPortSpace",
    L"EfiPalCode",
};

int memcmp(const void *aptr, const void *bptr, size_t n) {
	const unsigned char *a = aptr, *b = bptr;
	for (size_t i = 0; i < n; i++) {
		if (a[i] < b[i]) return -1;
		else if (a[i] > b[i]) return 1;
	}
	return 0;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *st) {
    EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;
    EFI_GUID gEfiAcpiTableGuid = ACPI_TABLE_GUID;

    ST = st;

    ST->ConOut->OutputString(ST->ConOut, L"Disabling watchdog timer.\n\n");
    ST->BootServices->SetWatchdogTimer(0, 0, 0, NULL);
    
    // open the kernel file from the device this app was loaded from
	EFI_FILE *Kernel;
	{
		EFI_HANDLE_PROTOCOL HandleProtocol = ST->BootServices->HandleProtocol;

		EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
		HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (void**)&LoadedImage);

		EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
		HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&FileSystem);

		EFI_FILE *Root;
		FileSystem->OpenVolume(FileSystem, &Root);

		EFI_STATUS s = Root->Open(Root, &Kernel, L"kernel", EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
		if (s != EFI_SUCCESS) {
			ST->ConOut->OutputString(ST->ConOut, L"kernel is missing\r\n");
			return s;
		}
	}

	EFI_ALLOCATE_POOL AllocatePool = ST->BootServices->AllocatePool;

	// load the elf header from the kernel
	Elf32_Ehdr header;
	{
		UINTN FileInfoSize;
		EFI_FILE_INFO *FileInfo;
		Kernel->GetInfo(Kernel, &gEfiFileInfoGuid, &FileInfoSize, NULL);
		AllocatePool(EfiLoaderData, FileInfoSize, (void**)&FileInfo);
		Kernel->GetInfo(Kernel, &gEfiFileInfoGuid, &FileInfoSize, (void**)&FileInfo);

		UINTN size = sizeof(header);
		Kernel->Read(Kernel, &size, &header);
	}

	// verify the kernel binary
	if (
		memcmp(&header.e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0 /*||
		header.e_ident[EI_CLASS] != ELFCLASS32 ||
		header.e_ident[EI_DATA] != ELFDATA2LSB ||
		header.e_type != ET_EXEC ||
		header.e_machine != (0x03 | 0x3E) ||
		header.e_version != EV_CURRENT
*/	) {
		ST->ConOut->OutputString(ST->ConOut, L"kernel format is bad\r\n");
		return -1;
	}

	// load the kernel segment headers
	Elf32_Phdr *phdrs;
	{
		Kernel->SetPosition(Kernel, header.e_phoff);
		UINTN size = header.e_phnum * header.e_phentsize;
		AllocatePool(EfiLoaderData, size, (void**)&phdrs);
		Kernel->Read(Kernel, &size, phdrs);
	}

	EFI_ALLOCATE_PAGES AllocatePages = ST->BootServices->AllocatePages;

	// load the actual kernel binary based on its segment headers
	for (
		Elf32_Phdr *phdr = phdrs;
		(char*)phdr < (char*)phdrs + header.e_phnum * header.e_phentsize;
		phdr = (Elf32_Phdr*)((char*)phdr + header.e_phentsize)
	) {
		switch (phdr->p_type) {
		case PT_LOAD: {
			int pages = (phdr->p_memsz + 0x1000 - 1) / 0x1000; // round up
			Elf32_Addr segment = phdr->p_paddr;
			AllocatePages(AllocateAddress, EfiLoaderData, pages, &segment);

			Kernel->SetPosition(Kernel, phdr->p_offset);
			UINTN size = phdr->p_filesz;
			Kernel->Read(Kernel, &size, (void*)segment);
			break;
		}
		}
	}

	// get the memory map from the firmware
	EFI_MEMORY_DESCRIPTOR *Map = NULL;
	UINTN MapSize, MapKey;
	UINTN DescriptorSize;
	UINT32 DescriptorVersion;
	EFI_STATUS result = -1;
	void *rsdp = NULL;
    {
		EFI_GET_MEMORY_MAP GetMemoryMap = ST->BootServices->GetMemoryMap;

		while ((result = GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion)) != EFI_SUCCESS)
        {
            if (result == EFI_BUFFER_TOO_SMALL)
            {
                MapSize += 2 * DescriptorSize;
		        AllocatePool(EfiLoaderData, MapSize, (void**)&Map);
            }
            else
            {
                Print(L"error getting memory map: %d\n", result);
            }
        }
	}

    Print(L"Got memory map at %x size %d\n", Map, MapSize);
	// get the acpi tables from the firmware
	for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
		EFI_CONFIGURATION_TABLE *Config = &ST->ConfigurationTable[i];
		if (memcmp(&Config->VendorGuid, &gEfiAcpiTableGuid, sizeof(Config->VendorGuid)) == 0) {
			rsdp = Config->VendorTable;
			break;
		}
	}
    uint8_t *startOfMemoryMap = (uint8_t *)Map;
    uint8_t *endOfMemoryMap = startOfMemoryMap + MapSize;

    uint8_t *offset = startOfMemoryMap;

    uint32_t counter = 0;

    while (offset < endOfMemoryMap)
    {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)offset;

        Print(L"Map %d:\n", counter);
        Print(L"  Type: %X, %s\n", desc->Type, memory_types[desc->Type]); 
        Print(L"  PhysicalStart: %X\n", desc->PhysicalStart);
        Print(L"  VirtualStart: %X\n", desc->VirtualStart);
        Print(L"  NumberOfPages: %X   (4k)\n", desc->NumberOfPages);
        Print(L"  Attribute: %X\n", desc->Attribute);

        offset += DescriptorSize;

        counter++;
    }
	// finish with firmware and jump to the kernel
	ST->BootServices->ExitBootServices(ImageHandle, MapKey);
	((__attribute__((sysv_abi)) void (*)(void*, size_t, size_t, void*))header.e_entry)(
		Map, MapSize, DescriptorSize,
		rsdp
	);
	return EFI_SUCCESS;
}

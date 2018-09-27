#ifndef ACPI_H
#define ACPI_H

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

#pragma pack(push, 1)

typedef struct _RSDP
{
   char Signature[8];
   uint8 Checksum;
   char OemId[6];
   uint8 Revision;
   uint32 RsdtAddress;
} RSDP_t;

typedef struct _RSDT
{
   char Signature[4];
   uint32 Length;
   uint8 Revision;
   uint8 Checksum;
   char OemId[6];
   char OemTableId[8];
   uint32 OemRevision;
   char CreatorId[4];
   uint32 CreatorRevision;
} RSDT_t;

typedef struct _MADT
{
   char Signature[4];
   uint32 Length;
   uint8 Revision;
   uint8 Checksum;
   char OemId[6];
   char OemTableId[8];
   uint32 OemRevision;
   char CreatorId[4];
   uint32 CreatorRevision;
   uint32 LapicAddress;
   uint32 Flags;
} MADT_t;

typedef struct _MADTSubtable
{
   uint8 Type;
   uint8 Length;
} MADTSubtable_t;

typedef struct _AcpiLapic
{
   uint8 Type;
   uint8 Length;
   uint8 AcpiId;
   uint8 LapicId;
   uint32 Flags;
} AcpiLapic_t;

typedef struct _AcpiIoApic
{
   uint8 Type;
   uint8 Length;
   uint8 IoApicId;
   uint8 Reserved;
   uint32 Address;
   uint8 IRQBase;
} AcpiIoApic_t;

#pragma pack(pop)

#define MADT_LAPIC 0
#define MADT_IOAPIC 1

int AcpiChecksum(void* ptr, int size)
{
   uint8 accum = 0;
   for(int i = 0; i < size; i++)
   {
      accum += ((uint8*)ptr)[i];
   }
   return (accum == 0);
}

#endif

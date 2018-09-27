// Multiprocessor support
// Search memory for MP description structures.
// http://developer.intel.com/design/pentium/datashts/24201606.pdf

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mp.h"
#include "x86.h"
#include "mmu.h"
#include "proc.h"
#include "acpi.h"

struct cpu cpus[NCPU];
int ismp;
int ncpu;
uchar ioapicid;

static uchar
sum(uchar *addr, int len)
{
  int i, sum;

  sum = 0;
  for(i=0; i<len; i++)
    sum += addr[i];
  return sum;
}

// Look for an MP structure in the len bytes at addr.
static struct mp*
mpsearch1(uint a, int len)
{
  uchar *e, *p, *addr;

  addr = P2V(a);
  e = addr+len;
  for(p = addr; p < e; p += sizeof(struct mp))
    if(memcmp(p, "_MP_", 4) == 0 && sum(p, sizeof(struct mp)) == 0)
      return (struct mp*)p;
  return 0;
}

// Search for the MP Floating Pointer Structure, which according to the
// spec is in one of the following three locations:
// 1) in the first KB of the EBDA;
// 2) in the last KB of system base memory;
// 3) in the BIOS ROM between 0xE0000 and 0xFFFFF.
static struct mp*
mpsearch(void)
{
  uchar *bda;
  uint p;
  struct mp *mp;

  bda = (uchar *) P2V(0x400);
  if((p = ((bda[0x0F]<<8)| bda[0x0E]) << 4)){
    if((mp = mpsearch1(p, 1024)))
      return mp;
  } else {
    p = ((bda[0x14]<<8)|bda[0x13])*1024;
    if((mp = mpsearch1(p-1024, 1024)))
      return mp;
  }
  return mpsearch1(0xF0000, 0x10000);
}

// Search for an MP configuration table.  For now,
// don't accept the default configurations (physaddr == 0).
// Check for correct signature, calculate the checksum and,
// if correct, check the version.
// To do: check extended table checksum.
static struct mpconf*
mpconfig(struct mp **pmp)
{
  struct mpconf *conf;
  struct mp *mp;

  if((mp = mpsearch()) == 0 || mp->physaddr == 0)
    return 0;
  conf = (struct mpconf*) P2V((uint) mp->physaddr);
  if(memcmp(conf, "PCMP", 4) != 0)
    return 0;
  if(conf->version != 1 && conf->version != 4)
    return 0;
  if(sum((uchar*)conf, conf->length) != 0)
    return 0;
  *pmp = mp;
  return conf;
}

void
mpinit_acpi(void * r)
{
  RSDP_t * rsdp = (RSDP_t*)r;
  RSDT_t * rsdt = P2V(rsdp->RsdtAddress);

    if(!AcpiChecksum(rsdt, rsdt->Length))
      return;
   
   // Iterate through the tables to find the MADT
   MADT_t* madt;
   int tableCount = (rsdt->Length-sizeof(RSDT_t))/4;
   int i;
   for(i = 0; i < tableCount; i++)
   {
      madt = (MADT_t*)P2V(*(((uint32*)(rsdt+1))+i));
      if(memcmp(madt, "APIC", 4) == 0)
         break;
   }
   if(i == tableCount) // If we didn't find the MADT, something screwed up
      return;
   if(!AcpiChecksum(madt, madt->Length)) // Checksum the MADT
      return;
   
   // Get the number of processors and IO APICs
   ncpu = 0;
   lapic = (uint*)madt->LapicAddress;
   for(MADTSubtable_t* sub = (MADTSubtable_t*)(madt+1); (void*)sub < ((void*)madt)+madt->Length; sub = (MADTSubtable_t*)(((void*)sub)+sub->Length))
   {
      if(sub->Type == MADT_LAPIC)
      {
        AcpiLapic_t* lapic_sub = ((AcpiLapic_t*)sub);
        if (!(lapic_sub->Flags & 0x1)) break;
        if(ncpu < NCPU) {
            cpus[ncpu].apicid = lapic_sub->LapicId;  // apicid may differ from ncpu
            ncpu++;
        }
      }
      else if (sub->Type == MADT_IOAPIC)
      {
          ioapicid = ((AcpiIoApic_t*)sub)->IoApicId;
      }
   }
   if(!ncpu){
    // Didn't like what we found; fall back to no MP.
    ncpu = 1;
    lapic = 0;
    ioapicid = 0;
    ismp = 0;
    return;
  } else {
    ismp = 1;
  }

 }

void
mpinit(void)
{
  uchar *p, *e;
  struct mp *mp;
  struct mpconf *conf;
  struct mpproc *proc;
  struct mpioapic *ioapic;

  if((conf = mpconfig(&mp)) == 0)
    return;
  ismp = 1;
  lapic = (uint*)conf->lapicaddr;
  for(p=(uchar*)(conf+1), e=(uchar*)conf+conf->length; p<e; ){
    switch(*p){
    case MPPROC:
      proc = (struct mpproc*)p;
      if(ncpu < NCPU) {
        cpus[ncpu].apicid = proc->apicid;  // apicid may differ from ncpu
        ncpu++;
      }
      p += sizeof(struct mpproc);
      continue;
    case MPIOAPIC:
      ioapic = (struct mpioapic*)p;
      ioapicid = ioapic->apicno;
      p += sizeof(struct mpioapic);
      continue;
    case MPBUS:
    case MPIOINTR:
    case MPLINTR:
      p += 8;
      continue;
    default:
      ismp = 0;
      break;
    }
  }
  if(!ismp){
    // Didn't like what we found; fall back to no MP.
    ncpu = 1;
    lapic = 0;
    ioapicid = 0;
    return;
  }

  if(mp->imcrp){
    // Bochs doesn't support IMCR, so this doesn't run on Bochs.
    // But it would on real hardware.
    outb(0x22, 0x70);   // Select IMCR
    outb(0x23, inb(0x23) | 1);  // Mask external interrupts.
  }
}

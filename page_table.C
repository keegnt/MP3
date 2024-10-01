#include "assert.H"
#include "exceptions.H"
#include "console.H"
#include "paging_low.H"
#include "page_table.H"

PageTable * PageTable::current_page_table = nullptr;
unsigned int PageTable::paging_enabled = 0;
ContFramePool * PageTable::kernel_mem_pool = nullptr;
ContFramePool * PageTable::process_mem_pool = nullptr;
unsigned long PageTable::shared_size = 0;



void PageTable::init_paging(ContFramePool * _kernel_mem_pool,
                            ContFramePool * _process_mem_pool,
                            const unsigned long _shared_size)
{
   kernel_mem_pool = _kernel_mem_pool;
   process_mem_pool = _process_mem_pool;
   shared_size = _shared_size;

   
   
   Console::puts("Initialized Paging System\n");
}

PageTable::PageTable()
{
   // Allocate a frame for the page directory from the kernel memory pool
   unsigned long page_directory_frame = kernel_mem_pool->get_frames(1); // Get physical frame number

   // The physical address of the page directory (no casting, just an integer)
   unsigned long page_directory_address = page_directory_frame * PAGE_SIZE;

   // Get the virtual address where this physical memory is mapped
   page_directory = reinterpret_cast<unsigned long*>(page_directory_address); // Now, we are casting it into a pointer

   // Clear all entries in the page directory
   for (int i = 0; i < ENTRIES_PER_PAGE; ++i) {
      page_directory[i] = 0;
   }

   // Allocate a frame for the first page table (for the first 4MB of memory)
   unsigned long first_page_table_frame = kernel_mem_pool->get_frames(1);
   if (first_page_table_frame == 0) {
      Console::puts("Failed to allocate first page table\n");
      return;
   }

   // The physical address of the first page table
   unsigned long first_page_table_address = first_page_table_frame * PAGE_SIZE;

   // Get the virtual address where this physical memory is mapped
   unsigned long *first_page_table = reinterpret_cast<unsigned long*>(first_page_table_address); // Only here we cast to access memory

   // Map the first 4MB of memory in the page table
   unsigned long address = 0;  // Holds the physical address for each page
   for (int i = 0; i < 1024; ++i) {
      first_page_table[i] = address | 0x3; // Set as present, supervisor, read/write (011 in binary)
      address += PAGE_SIZE; // Move to the next 4KB page
   }

   // Set the first entry in the page directory to point to the first page table (physical address)
   page_directory[0] = first_page_table_address | 0x3; // Present, supervisor, read/write

   // Mark all remaining page directory entries as not-present
   for (int i = 1; i < 1024; ++i) {
      page_directory[i] = 0x2; // Supervisor level, read/write, not present (010 in binary)
   }

   // Optional: Set up recursive mapping (the last entry points to the page directory itself)
   page_directory[ENTRIES_PER_PAGE - 1] = page_directory_address | 0x3; 

   Console::puts("Page directory with 4MB direct-mapped allocated\n");
}





void PageTable::load()
{
   if (page_directory == nullptr) {
      Console::puts("Error: Page directory not set\n");
      return;
   }
   write_cr3(reinterpret_cast<unsigned long>(page_directory));
   current_page_table = this;   
   Console::puts("Loaded page table\n");
}

void PageTable::enable_paging()
{
   
   write_cr0(read_cr0() | 0x80000000);
   paging_enabled = 1; 
   Console::puts("Enabled paging\n");
   
}


void PageTable::handle_fault(REGS * _r)
{
   // Ensure we have a valid current page table
   if (current_page_table == nullptr) {
      Console::puts("Error: No current page table loaded\n");
      return;
   }

   // Get the address that caused the fault
   unsigned long faulting_address = read_cr2();

   

   unsigned long pd_index = (faulting_address >> 22) & 0x3FF;  // Top 10 bits for PDE index
   unsigned long pt_index = (faulting_address >> 12) & 0x3FF;  // Next 10 bits for PTE index

   Console::puts("Page fault at address: ");
   Console::putui(faulting_address);
   Console::puts("PDE: ");
   Console::putui(pd_index);
   Console::puts("PTE: ");
   Console::putui(pt_index);
   Console::puts("\n");

   // Get the page directory entry (physical address)
   unsigned long page_directory_entry = current_page_table->page_directory[pd_index];

   Console::puts("Page directory entry for fault: ");
   Console::putui(page_directory_entry);
   Console::puts("\n");

   // Check if the page table is present, if not allocate a new page table
   if (!(page_directory_entry & 0x1)) {  // Check if the present bit is set
      Console::puts("Allocating new page table\n");

      // Allocate a new page table frame from the process memory pool
      unsigned long new_page_table_frame = kernel_mem_pool->get_frames(1);

      if (new_page_table_frame == 0) {
         Console::puts("Failed to allocate new page table\n");
         return;
      }

      // Convert the frame number to a physical address
      unsigned long new_page_table_address = new_page_table_frame * PAGE_SIZE;

      // Set the page directory entry to the physical address with present/read-write flags
      current_page_table->page_directory[pd_index] = new_page_table_address | 0x3;  // Present + read/write

      Console::puts("New page table allocated at address: ");
      Console::putui(new_page_table_address);
      Console::puts("\n");

      // Clear all entries in the newly allocated page table (using physical memory directly)
      unsigned long *new_page_table = reinterpret_cast<unsigned long*>(0xFFC00000 | (pd_index << 12));
      for (int i = 0; i < ENTRIES_PER_PAGE; ++i) {
         new_page_table[i] = 0;
      }
      
      unsigned long entry_frame = process_mem_pool->get_frames(1);
      unsigned long frame_address = PAGE_SIZE * entry_frame;

      Console::puts("Page table entry before update: ");
      Console::putui(new_page_table[pt_index]);
      Console::puts("\n");
      new_page_table[pt_index] = frame_address | 0x3;

      Console::puts("Page table entry after update: ");
      Console::putui(new_page_table[pt_index]);
      Console::puts("\n");

      
      Console::puts("Page fault handled\n ");

      return;  // Return after updating page directory, let the CPU retry
   }

   // The page table is present, now access it
   //unsigned long page_table_address = current_page_table->page_directory[pd_index] & ~0xFFF;  
   unsigned long *page_table = (unsigned long*)(current_page_table->page_directory[pd_index] & ~0xFFF);

   unsigned long page_table_entry = page_table[pt_index];

   Console::puts("Page table entry for fault: ");
   Console::putui(page_table_entry);
   Console::puts("\n");

   // Check if the page is present, if not allocate a new frame for the page
   if (!(page_table_entry & 0x1)) {  // Check if the page is present
      Console::puts("Allocating new frame for the page\n");

      // Allocate a new frame for the page from the process memory pool
      unsigned long new_frame = process_mem_pool->get_frames(1);

      if (new_frame == 0) {
         Console::puts("Failed to allocate new frame\n");
         return;
      }

      // Convert the frame number to a physical address
      unsigned long new_frame_address = new_frame * PAGE_SIZE;

      // Update the page table entry with the physical address and present/read-write flags
      page_table[pt_index] = new_frame_address | 0x3;  // Present + read/write

      Console::puts("New frame allocated at address: ");
      Console::putui(new_frame_address);
      Console::puts("\n");
   }

   Console::puts("Page fault handled successfully\n");
}





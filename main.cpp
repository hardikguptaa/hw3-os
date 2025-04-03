// main.cpp 
// Corrected Demand Paging Example
//  - Random replacement policy
//  - FIFO replacement policy
//  - Custom policy placeholder
// The <program> can be: "scan", "focus", or "sort".

 #include "page_table.h"
 #include "disk.h"
 #include "program.h"
 
 #include <vector>
 #include <queue>
 #include <cassert>
 #include <iostream>
 #include <string.h>
 #include <cstdlib>
 #include <ctime>
 
 using namespace std;
 
// Global variables

 typedef void (*program_f)(char *data, int length);
 static int nframes = 0;
 static vector<int> frame_table;
 static queue<int> fifo_queue;          
 static struct disk *global_disk = nullptr;
 static int page_fault_count = 0; // # times we load a *not-resident* page
 static int disk_read_count   = 0; // # times we disk_read
 static int disk_write_count  = 0; // # times we disk_write
 
//  Helper: evict_page_if_needed()
//     Evict the page currently occupying `victim_frame`.
//     - If that page is dirty, write it to disk.
//     - Clear the old page's bits in the page table (bits=0).
//     - Mark that frame as free in `frame_table`.

 static void evict_page_if_needed(struct page_table *pt, int victim_frame)
 {
     int old_page = frame_table[victim_frame];
     if (old_page < 0) {
         // Frame is already free, no eviction needed
         return;
     }
 
     // Find old page's bits
     int old_frame, old_bits;
     page_table_get_entry(pt, old_page, &old_frame, &old_bits);
 
     // If the old page is dirty, write it back to disk
     if (old_bits & PROT_WRITE) {
         disk_write_count++;
         char *physmem = page_table_get_physmem(pt);
         disk_write(global_disk, old_page, &physmem[victim_frame * PAGE_SIZE]);
     }
 
     // Now mark that old page as NOT resident
     page_table_set_entry(pt, old_page, old_frame, 0);
 
     // Mark the frame as free
     frame_table[victim_frame] = -1;
 }
 
//  RANDOM Page Fault Handler
 static void page_fault_handler_random(struct page_table *pt, int page)
 {
     // 1) Check current bits
     int frame, bits;
     page_table_get_entry(pt, page, &frame, &bits);
 
     // For debugging:
     cout << "[RANDOM] Page fault on page #" << page
          << " (current bits = " << bits << ")\n";
 
     // CASE A: Not resident -> actual page fault
     if ((bits & PROT_READ) == 0) {
         page_fault_count++;  // we are bringing in a new page
 
         // -- Pick a frame (either a free one or a random victim)
         int victim_frame = -1;
         for (int i=0; i < nframes; i++) {
             if (frame_table[i] < 0) {
                 victim_frame = i;
                 break;
             }
         }
         if (victim_frame < 0) {
             // all frames are full -> pick random
             victim_frame = rand() % nframes;
             // evict the old occupant
             evict_page_if_needed(pt, victim_frame);
 
             cout << "[RANDOM] Evicting frame " << victim_frame << endl;
         } else {
             cout << "[RANDOM] Using free frame " << victim_frame << endl;
         }
 
         // -- Read the page in from disk
         disk_read_count++;
         char *physmem = page_table_get_physmem(pt);
         disk_read(global_disk, page, &physmem[victim_frame * PAGE_SIZE]);
 
         // -- Set page entry to READ-only first
         page_table_set_entry(pt, page, victim_frame, PROT_READ);
 
         // -- Update frame_table
         frame_table[victim_frame] = page;
     }
     // CASE B: The page is resident but read-only -> must upgrade to read|write
     else if ((bits & PROT_WRITE) == 0) {
         // do not increment page_fault_count: this is just a write-fault to set the dirty bit
         page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
     }
     // CASE C: Already has read|write -> shouldn't fault
     else {
         cerr << "[RANDOM] Unexpected fault: page " << page
              << " already had R+W bits!\n";
         exit(1);
     }
 }
 
//  FIFO Page Fault Handler

 static void page_fault_handler_fifo(struct page_table *pt, int page)
 {
     int frame, bits;
     page_table_get_entry(pt, page, &frame, &bits);
 
     cout << "[FIFO] Page fault on page #" << page
          << " (current bits = " << bits << ")\n";
 
     // CASE A: Not resident
     if ((bits & PROT_READ) == 0) {
         page_fault_count++;
 
         // Check for a free frame
         int victim_frame = -1;
         for (int i=0; i < nframes; i++) {
             if (frame_table[i] < 0) {
                 victim_frame = i;
                 break;
             }
         }
 
         if (victim_frame < 0) {
             // No free frame, so evict from the front of the queue
             victim_frame = fifo_queue.front();
             fifo_queue.pop();
             evict_page_if_needed(pt, victim_frame);
 
             cout << "[FIFO] Evicting frame " << victim_frame << endl;
         } else {
             cout << "[FIFO] Using free frame " << victim_frame << endl;
         }
 
         // Read the new page from disk
         disk_read_count++;
         char *physmem = page_table_get_physmem(pt);
         disk_read(global_disk, page, &physmem[victim_frame * PAGE_SIZE]);
 
         // Map new page as read-only
         page_table_set_entry(pt, page, victim_frame, PROT_READ);
 
         // Update frame table + push to queue
         frame_table[victim_frame] = page;
         fifo_queue.push(victim_frame);
     }
     // CASE B: Already in memory but read-only => upgrade
     else if ((bits & PROT_WRITE) == 0) {
         page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
     }
     // CASE C: If already has R|W, unexpected
     else {
         cerr << "[FIFO] Unexpected fault: page " << page
              << " had R+W bits already!\n";
         exit(1);
     }
 }
 

//  CUSTOM Page Fault Handler (placeholder)
//    For your final solution, you'd implement a better policy.

 static void page_fault_handler_custom(struct page_table *pt, int page)
 {
     cerr << "[CUSTOM] Page fault on page #" << page
          << " -- not implemented!\n";
     exit(1);
 }
 
// main()
 int main(int argc, char *argv[])
 {
     if (argc != 5) {
         cerr << "Usage: " << argv[0]
              << " <npages> <nframes> <rand|fifo|custom> <sort|scan|focus>"
              << endl;
         exit(1);
     }
 
     // Parse command-line
     int npages = atoi(argv[1]);
     nframes    = atoi(argv[2]);
     const char *algorithm    = argv[3];
     const char *program_name = argv[4];
 
     // Validate program choice
     program_f program = nullptr;
     if (!strcmp(program_name, "sort")) {
         if (nframes < 2) {
             cerr << "ERROR: nframes >= 2 for sort program\n";
             exit(1);
         }
         program = sort_program;
     }
     else if (!strcmp(program_name, "scan")) {
         program = scan_program;
     }
     else if (!strcmp(program_name, "focus")) {
         program = focus_program;
     }
     else {
         cerr << "ERROR: Unknown program: " << program_name << endl;
         exit(1);
     }
 
     // Pick our page-fault handler
     page_fault_handler_t handler = nullptr;
     if (!strcmp(algorithm, "rand")) {
         handler = page_fault_handler_random;
     } else if (!strcmp(algorithm, "fifo")) {
         handler = page_fault_handler_fifo;
     } else if (!strcmp(algorithm, "custom")) {
         handler = page_fault_handler_custom;
     } else {
         cerr << "ERROR: Unknown algorithm: " << algorithm << endl;
         exit(1);
     }
 
     // Seed RNG for random replacement
     srand(time(NULL));
 
     global_disk = disk_open("myvirtualdisk", npages);
     if (!global_disk) {
         cerr << "ERROR: Couldn't create virtual disk: "
              << strerror(errno) << endl;
         exit(1);
     }
 
     frame_table.resize(nframes, -1);
 
     // Create the page table
     struct page_table *pt = page_table_create(npages, nframes, handler);
     if (!pt) {
         cerr << "ERROR: Couldn't create page table: "
              << strerror(errno) << endl;
         exit(1);
     }
 
     char *virtmem = page_table_get_virtmem(pt);
     program(virtmem, npages * PAGE_SIZE);
 
     cout << "=========================================\n";
     cout << "Program finished!\n";
     cout << "Page faults : " << page_fault_count << "\n";
     cout << "Disk reads  : " << disk_read_count << "\n";
     cout << "Disk writes : " << disk_write_count << "\n";
     cout << "=========================================\n";
 
     page_table_delete(pt);
     disk_close(global_disk);
 
     return 0;
 }
 
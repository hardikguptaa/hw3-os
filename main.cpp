// main.cpp
//
// Demand Paging Simulation
//
// Simulates virtual memory with three page replacement policies:
// - Random: evicts a random page
// - FIFO: evicts the oldest page
// - Custom (Clock): second-chance policy using reference bits
//
// Usage:
//   ./virtmem <npages> <nframes> <rand|fifo|custom> <program>
//   where <program> = scan | focus | sort

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

typedef void (*program_f)(char *data, int length);

// Globals
static int nframes = 0;
static vector<int> frame_table;
static queue<int> fifo_queue;
static vector<int> reference_bits;
static int clock_hand = 0;
static struct disk *global_disk = nullptr;

// Counters
static int page_fault_count = 0;
static int disk_read_count = 0;
static int disk_write_count = 0;

// Evict the page in victim_frame if it is occupied
static void evict_page_if_needed(struct page_table *pt, int victim_frame) {
    
    int old_page = frame_table[victim_frame];
    if (old_page < 0) return;  // frame is already free

    int old_frame, old_bits;
    page_table_get_entry(pt, old_page, &old_frame, &old_bits);

    // If the page is dirty, write it to disk
    if (old_bits & PROT_WRITE) {
        disk_write_count++;
        char *physmem = page_table_get_physmem(pt);
        disk_write(global_disk, old_page, &physmem[victim_frame * PAGE_SIZE]);
    }

    // Unmap the old page and mark frame as free
    page_table_set_entry(pt, old_page, old_frame, 0);
    frame_table[victim_frame] = -1;
}

/*
 * RANDOM Replacement Policy:
 * This algorithm picks a free frame if available; if all frames are occupied,
 * it randomly selects one to evict. It doesn't consider usage patterns,
 * which can lead to poor performance in some access patterns.
 */
static void page_fault_handler_random(struct page_table *pt, int page) {
    
    int frame, bits;
    page_table_get_entry(pt, page, &frame, &bits);

    if ((bits & PROT_READ) == 0) {
        page_fault_count++;

        // Try to find a free frame
        int victim_frame = -1;
        for (int i = 0; i < nframes; i++) {
            if (frame_table[i] < 0) {
                victim_frame = i;
                break;
            }
        }

        // If none, pick a random frame to evict
        if (victim_frame < 0) {
            victim_frame = rand() % nframes;
            evict_page_if_needed(pt, victim_frame);
        }

        // Load the new page
        disk_read_count++;
        char *physmem = page_table_get_physmem(pt);
        disk_read(global_disk, page, &physmem[victim_frame * PAGE_SIZE]);

        page_table_set_entry(pt, page, victim_frame, PROT_READ);
        frame_table[victim_frame] = page;
    }
    else if ((bits & PROT_WRITE) == 0) {
        // Upgrade to writable (write fault)
        page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
    }
    else {
        cerr << "[RANDOM] Unexpected fault on page " << page << "\n";
        exit(1);
    }
}

/*
 * FIFO Replacement Policy:
 * The First-In-First-Out strategy evicts the page that has been in memory
 * the longest, regardless of how frequently or recently it was accessed.
 * It uses a queue to track the order in which pages were loaded.
 */
static void page_fault_handler_fifo(struct page_table *pt, int page) {
    int frame, bits;
    page_table_get_entry(pt, page, &frame, &bits);

    if ((bits & PROT_READ) == 0) {
        page_fault_count++;

        // Find free frame if any
        int victim_frame = -1;
        for (int i = 0; i < nframes; i++) {
            if (frame_table[i] < 0) {
                victim_frame = i;
                break;
            }
        }

        // If no free frame, evict from front of FIFO queue
        if (victim_frame < 0) {
            victim_frame = fifo_queue.front();
            fifo_queue.pop();
            evict_page_if_needed(pt, victim_frame);
        }

        // Load the new page
        disk_read_count++;
        char *physmem = page_table_get_physmem(pt);
        disk_read(global_disk, page, &physmem[victim_frame * PAGE_SIZE]);

        page_table_set_entry(pt, page, victim_frame, PROT_READ);
        frame_table[victim_frame] = page;
        fifo_queue.push(victim_frame);
    }
    else if ((bits & PROT_WRITE) == 0) {
        page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
    }
    else {
        cerr << "[FIFO] Unexpected fault on page " << page << "\n";
        exit(1);
    }
}

/*
 * CLOCK (Second-Chance) Replacement Policy:
 * An enhancement to FIFO that gives pages a "second chance" before eviction.
 * Pages are tracked in a circular list with a reference bit.
 * If a page's bit is 1, it's cleared and skipped. If 0, it's evicted.
 */
static void page_fault_handler_custom(struct page_table *pt, int page) {
    int frame, bits;
    page_table_get_entry(pt, page, &frame, &bits);

    if ((bits & PROT_READ) == 0) {
        page_fault_count++;

        int victim_frame = -1;
        for (int i = 0; i < nframes; i++) {
            if (frame_table[i] < 0) {
                victim_frame = i;
                break;
            }
        }

        // Use Clock algorithm to find a victim
        if (victim_frame < 0) {
            while (true) {
                if (reference_bits[clock_hand] == 0) {
                    victim_frame = clock_hand;
                    clock_hand = (clock_hand + 1) % nframes;
                    break;
                }
                reference_bits[clock_hand] = 0;
                clock_hand = (clock_hand + 1) % nframes;
            }

            evict_page_if_needed(pt, victim_frame);
        }

        // Load the new page
        disk_read_count++;
        char *physmem = page_table_get_physmem(pt);
        disk_read(global_disk, page, &physmem[victim_frame * PAGE_SIZE]);

        page_table_set_entry(pt, page, victim_frame, PROT_READ);
        frame_table[victim_frame] = page;
        reference_bits[victim_frame] = 1;
    }
    else if ((bits & PROT_WRITE) == 0) {
        page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
        reference_bits[frame] = 1;
    }
    else {
        cerr << "[CUSTOM] Unexpected fault on page " << page << "\n";
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        cerr << "Usage: " << argv[0]
             << " <npages> <nframes> <rand|fifo|custom> <sort|scan|focus>\n";
        exit(1);
    }

    int npages = atoi(argv[1]);
    nframes = atoi(argv[2]);
    const char *algorithm = argv[3];
    const char *program_name = argv[4];

    // Select benchmark program
    program_f program = nullptr;
    if (!strcmp(program_name, "sort")) {
        if (nframes < 2) {
            cerr << "ERROR: nframes >= 2 for sort program\n";
            exit(1);
        }
        program = sort_program;
    } else if (!strcmp(program_name, "scan")) {
        program = scan_program;
    } else if (!strcmp(program_name, "focus")) {
        program = focus_program;
    } else {
        cerr << "ERROR: Unknown program: " << program_name << endl;
        exit(1);
    }

    // Select replacement algorithm
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

    srand(time(NULL));  // for random eviction

    // Create simulated disk
    global_disk = disk_open("myvirtualdisk", npages);
    if (!global_disk) {
        cerr << "ERROR: Couldn't create virtual disk: "
             << strerror(errno) << endl;
        exit(1);
    }

    frame_table.resize(nframes, -1);      // -1 = free
    reference_bits.resize(nframes, 0);    // used in Clock algorithm

    // Create page table with selected handler
    struct page_table *pt = page_table_create(npages, nframes, handler);
    if (!pt) {
        cerr << "ERROR: Couldn't create page table: "
             << strerror(errno) << endl;
        exit(1);
    }

    // Run selected benchmark program
    char *virtmem = page_table_get_virtmem(pt);
    program(virtmem, npages * PAGE_SIZE);

    // Report statistics
    // cout << "Program finished!\n";
    cout << "Page faults : " << page_fault_count << "\n";
    cout << "Disk reads  : " << disk_read_count << "\n";
    cout << "Disk writes : " << disk_write_count << "\n";

    page_table_delete(pt);
    disk_close(global_disk);
    return 0;
}
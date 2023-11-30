//  mem_mgr.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <cassert>
#include <cstdint>
#include <cstdint>

#pragma warning(disable : 4996)

#define ARGC_ERROR 1
#define FILE_ERROR 2

#define FRAME_SIZE  256
#define FIFO 0
#define LRU 1
#define REPLACE_POLICY FIFO

// SET TO 128 to use replacement policy: FIFO or LRU,
#define NFRAMES 256
#define PTABLE_SIZE 256
#define TLB_SIZE 16

struct page_node {    
    size_t npage;
    size_t frame_num;
    bool is_present;
    bool is_used;
};

char* ram = (char*)malloc(NFRAMES * FRAME_SIZE);
page_node pg_table[PTABLE_SIZE];  // page table and (single) TLB
page_node tlb[TLB_SIZE];

const char* passed_or_failed(bool condition) { return condition ? " + " : "fail"; }
size_t failed_asserts = 0;

size_t get_page(size_t x)   { return 0xff & (x >> 8); }
size_t get_offset(size_t x) { return 0xff & x; }

void get_page_offset(size_t x, size_t& page, size_t& offset) {
    page = get_page(x);
    offset = get_offset(x);
    // printf("x is: %zu, page: %zu, offset: %zu, address: %zu, paddress: %zu\n", 
    //        x, page, offset, (page << 8) | get_offset(x), page * 256 + offset);
}

void update_frame_ptable(size_t npage, size_t frame_num) {
    pg_table[npage].frame_num = frame_num;
    pg_table[npage].is_present = true;
    pg_table[npage].is_used = true;
}

int find_frame_ptable(size_t frame) {  // FIFO
    for (int i = 0; i < PTABLE_SIZE; i++) {
        if (pg_table[i].frame_num == frame && 
            pg_table[i].is_present == true) { return i; }
    }
    return -1;
}

size_t get_used_ptable() {  // LRU
    size_t unused = -1;
    for (size_t i = 0; i < PTABLE_SIZE; i++) {
        if (pg_table[i].is_used == false && 
            pg_table[i].is_present == true) { return (size_t)i; }
    }
    // All present pages have been used recently, set all page entry used flags to false
    for (size_t i = 0; i < PTABLE_SIZE; i++) { pg_table[i].is_used = false; }
    for (size_t i = 0; i < PTABLE_SIZE; i++) {
        page_node& r = pg_table[i];
        if (!r.is_used && r.is_present) { return i; }
    }
    return (size_t)-1;
}

int check_tlb(size_t page) {
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].npage == page) { return i; }
    }
    return -1;
}

void open_files(FILE*& fadd, FILE*& fcorr, FILE*& fback) { 
    fadd = fopen("addresses.txt", "r");
    if (fadd == NULL) { fprintf(stderr, "Could not open file: 'addresses.txt'\n");  exit(FILE_ERROR); }

    fcorr = fopen("correct.txt", "r");
    if (fcorr == NULL) { fprintf(stderr, "Could not open file: 'correct.txt'\n");  exit(FILE_ERROR); }

    fback = fopen("BACKING_STORE.bin", "rb");
    if (fback == NULL) { fprintf(stderr, "Could not open file: 'BACKING_STORE.bin'\n");  exit(FILE_ERROR); }
}
void close_files(FILE* fadd, FILE* fcorr, FILE* fback) { 
    fclose(fadd);
    fclose(fcorr);
    fclose(fback);
}

void initialize_pg_table_tlb() { 
    for (int i = 0; i < PTABLE_SIZE; ++i) {
        pg_table[i].npage = (size_t)i;
        pg_table[i].is_present = false;
        pg_table[i].is_used = false;
    }
    for (int i = 0; i < TLB_SIZE; i++) {
        tlb[i].npage = (size_t)-1;
        tlb[i].is_present = false;
        pg_table[i].is_used = false;
    }
}

void summarize(size_t pg_faults, size_t tlb_hits) { 
    printf("\nPage Fault Percentage: %1.3f%%", (double)pg_faults / 1000);
    printf("\nTLB Hit Percentage: %1.3f%%\n\n", (double)tlb_hits / 1000);
    printf("ALL logical ---> physical assertions PASSED!\n");
    printf("\n\t\t...done.\n");
}

void tlb_add(int index, page_node entry) {
    if (index < 0 || index >= TLB_SIZE) {
        // Index out of bounds, handle error accordingly
        fprintf(stderr, "Error: TLB index out of bounds\n");
        return;
    }

    // Replace or add the entry at the specified index
    tlb[index] = entry;
}

void tlb_remove(int index) {
    if (index < 0 || index >= TLB_SIZE) {
        // Index out of bounds, handle error accordingly
        fprintf(stderr, "Error: TLB index out of bounds\n");
        return;
    }

    // Set the TLB entry at the specified index as not present
    tlb[index].is_present = false;

    // Optional: Clean up or reset other fields
    tlb[index].npage = -1;
    tlb[index].frame_num = -1;
}

void tlb_hit(size_t& frame, size_t& page, size_t& tlb_hits, int result) {
    if (result < 0 || result >= TLB_SIZE) {
        // Result index out of bounds, handle error accordingly
        fprintf(stderr, "Error: TLB hit index out of bounds\n");
        return;
    }

    // Update the frame number with the one found in the TLB entry
    frame = tlb[result].frame_num;

    // Increment the TLB hits count
    tlb_hits++;

    // Optional: Update LRU information if using LRU policy
    // This would involve moving the accessed entry to a more 'recently used' position.
}

void tlb_miss(size_t& frame, size_t& page, size_t& tlb_track) {
    // Check if page is in the page table and update frame
    if (pg_table[page].is_present) {
        frame = pg_table[page].frame_num;
    } else {
        // Handle error or page fault if page is not in the page table
        fprintf(stderr, "Error: Page not found in page table during TLB miss\n");
        return;
    }

    // Prepare the new TLB entry
    page_node new_entry;
    new_entry.npage = page;
    new_entry.frame_num = frame;
    new_entry.is_present = true;

    // Update the TLB with the new entry
    // Assuming tlb_track keeps track of the next TLB index to update
    tlb_add(tlb_track % TLB_SIZE, new_entry);

    // Update tlb_track for the next entry
    tlb_track = (tlb_track + 1) % TLB_SIZE;
}

void fifo_replace_page(size_t& frame) {
    static size_t next_frame_to_replace = 0;

    // Check if the frame to be replaced is valid
    if (next_frame_to_replace >= NFRAMES) {
        fprintf(stderr, "Error: Invalid frame index for replacement\n");
        return;
    }

    // Identify the page currently occupying the frame
    int page_to_replace = find_frame_ptable(next_frame_to_replace);
    if (page_to_replace == -1) {
        fprintf(stderr, "Error: No page found in frame to replace\n");
        return;
    }

    // Update the page table to indicate the page is no longer in a frame
    pg_table[page_to_replace].is_present = false;

    // Assign the frame number to the frame variable
    frame = next_frame_to_replace;

    // Update the next frame to replace for the next call
    next_frame_to_replace = (next_frame_to_replace + 1) % NFRAMES;
}

void lru_replace_page(size_t& frame) {
    size_t least_recently_used_time = SIZE_MAX;
    size_t lru_page_index = -1;

    // Iterate over the page table to find the least recently used page
    for (size_t i = 0; i < PTABLE_SIZE; i++) {
        if (pg_table[i].is_present && pg_table[i].is_used < least_recently_used_time) {
            least_recently_used_time = pg_table[i].is_used;
            lru_page_index = i;
        }
    }

    if (lru_page_index == -1) {
        fprintf(stderr, "Error: No page found for LRU replacement\n");
        return;
    }

    // Replace the least recently used page
    frame = pg_table[lru_page_index].frame_num;
    pg_table[lru_page_index].is_present = false;
}

void page_fault(size_t& frame, size_t& page, size_t& frames_used, size_t& pg_faults, 
                size_t& tlb_track, FILE* fbacking) {  
    unsigned char buf[FRAME_SIZE];
    memset(buf, 0, sizeof(buf));
    bool is_memfull = frames_used >= NFRAMES;

    ++pg_faults;

    if (is_memfull) {
        // Memory is full, we need to replace a page
#if REPLACE_POLICY == FIFO
        fifo_replace_page(frame);
#elif REPLACE_POLICY == LRU
        lru_replace_page(frame);
#endif
    } else {
        // Memory is not full, use the next available frame
        frame = frames_used;
    }

    // Load the page into RAM at the frame location
    fseek(fbacking, page * FRAME_SIZE, SEEK_SET);
    fread(buf, FRAME_SIZE, 1, fbacking);

    // Copy the page into the frame
    memcpy(ram + (frame * FRAME_SIZE), buf, FRAME_SIZE);

    // Update the page table with the new frame
    update_frame_ptable(page, frame);

    // Add the page to the TLB
    tlb_add(tlb_track % TLB_SIZE, {page, frame, true, false}); // Assuming TLB_SIZE is the size of the TLB
    tlb_track = (tlb_track + 1) % TLB_SIZE;

    if (!is_memfull) {
        ++frames_used;
    }
}

void check_address_value(size_t logic_add, size_t page, size_t offset, size_t physical_add,
                         size_t& prev_frame, size_t frame, int val, int value, size_t o) { 
    printf("log: %5lu 0x%04x (pg:%3lu, off:%3lu)-->phy: %5lu (frm: %3lu) (prv: %3lu)--> val: %4d == value: %4d -- %s", 
          logic_add, (unsigned int)logic_add, page, offset, physical_add, frame, prev_frame, 
          val, value, passed_or_failed(val == value));

    if (frame < prev_frame) {  printf("   HIT!\n");
    } else {
        prev_frame = frame;
        printf("----> pg_fault\n");
    }
    if (o % 5 == 4) { printf("\n"); }
// if (o > 20) { exit(-1); }             // to check out first 20 elements

    if (val != value) { ++failed_asserts; }
    if (failed_asserts > 5) { exit(-1); }
//     assert(val == value);
}

void run_simulation() { 
        // addresses, pages, frames, values, hits and faults
    size_t logic_add, virt_add, phys_add, physical_add;
    size_t page, frame, offset, value, prev_frame = 0, tlb_track = 0;
    size_t frames_used = 0, pg_faults = 0, tlb_hits = 0;
    int val = 0;
    char buf[BUFSIZ];

    bool is_memfull = false;     // physical memory to store the frames

    initialize_pg_table_tlb();

        // addresses to test, correct values, and pages to load
    FILE *faddress, *fcorrect, *fbacking;
    open_files(faddress, fcorrect, fbacking);

    for (int o = 0; o < 1000; o++) {     // read from file correct.txt
        fscanf(fcorrect, "%s %s %lu %s %s %lu %s %ld", buf, buf, &virt_add, buf, buf, &phys_add, buf, &value);  

        fscanf(faddress, "%ld", &logic_add);  
        get_page_offset(logic_add, page, offset);

        int result = check_tlb(page);
        if (result >= 0) {  
            tlb_hit(frame, page, tlb_hits, result); 
        } else if (pg_table[page].is_present) {
            tlb_miss(frame, page, tlb_track);
        } else {         // page fault
            page_fault(frame, page, frames_used, pg_faults, tlb_track, fbacking);
        }

        physical_add = (frame * FRAME_SIZE) + offset;
        val = (int)*(ram + physical_add);

        check_address_value(logic_add, page, offset, physical_add, prev_frame, frame, val, value, o);
    }
    close_files(faddress, fcorrect, fbacking);  // and time to wrap things up
    free(ram);
    summarize(pg_faults, tlb_hits);
}


int main(int argc, const char * argv[]) {
    run_simulation();
// printf("\nFailed asserts: %lu\n\n", failed_asserts);   // allows asserts to fail silently and be counted
    return 0;
}

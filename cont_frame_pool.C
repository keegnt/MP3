/*
 File: ContFramePool.C
 
 Author:
 Date  : 
 
 */

/*--------------------------------------------------------------------------*/
/* 
 POSSIBLE IMPLEMENTATION
 -----------------------

 The class SimpleFramePool in file "simple_frame_pool.H/C" describes an
 incomplete vanilla implementation of a frame pool that allocates 
 *single* frames at a time. Because it does allocate one frame at a time, 
 it does not guarantee that a sequence of frames is allocated contiguously.
 This can cause problems.
 
 The class ContFramePool has the ability to allocate either single frames,
 or sequences of contiguous frames. This affects how we manage the
 free frames. In SimpleFramePool it is sufficient to maintain the free 
 frames.
 In ContFramePool we need to maintain free *sequences* of frames.
 
 This can be done in many ways, ranging from extensions to bitmaps to 
 free-lists of frames etc.
 
 IMPLEMENTATION:
 
 One simple way to manage sequences of free frames is to add a minor
 extension to the bitmap idea of SimpleFramePool: Instead of maintaining
 whether a frame is FREE or ALLOCATED, which requires one bit per frame, 
 we maintain whether the frame is FREE, or ALLOCATED, or HEAD-OF-SEQUENCE.
 The meaning of FREE is the same as in SimpleFramePool. 
 If a frame is marked as HEAD-OF-SEQUENCE, this means that it is allocated
 and that it is the first such frame in a sequence of frames. Allocated
 frames that are not first in a sequence are marked as ALLOCATED.
 
 NOTE: If we use this scheme to allocate only single frames, then all 
 frames are marked as either FREE or HEAD-OF-SEQUENCE.
 
 NOTE: In SimpleFramePool we needed only one bit to store the state of 
 each frame. Now we need two bits. In a first implementation you can choose
 to use one char per frame. This will allow you to check for a given status
 without having to do bit manipulations. Once you get this to work, 
 revisit the implementation and change it to using two bits. You will get 
 an efficiency penalty if you use one char (i.e., 8 bits) per frame when
 two bits do the trick.
 
 DETAILED IMPLEMENTATION:
 
 How can we use the HEAD-OF-SEQUENCE state to implement a contiguous
 allocator? Let's look a the individual functions:
 
 Constructor: Initialize all frames to FREE, except for any frames that you 
 need for the management of the frame pool, if any.
 
 get_frames(_n_frames): Traverse the "bitmap" of states and look for a 
 sequence of at least _n_frames entries that are FREE. If you find one, 
 mark the first one as HEAD-OF-SEQUENCE and the remaining _n_frames-1 as
 ALLOCATED.

 release_frames(_first_frame_no): Check whether the first frame is marked as
 HEAD-OF-SEQUENCE. If not, something went wrong. If it is, mark it as FREE.
 Traverse the subsequent frames until you reach one that is FREE or 
 HEAD-OF-SEQUENCE. Until then, mark the frames that you traverse as FREE.
 
 mark_inaccessible(_base_frame_no, _n_frames): This is no different than
 get_frames, without having to search for the free sequence. You tell the
 allocator exactly which frame to mark as HEAD-OF-SEQUENCE and how many
 frames after that to mark as ALLOCATED.
 
 needed_info_frames(_n_frames): This depends on how many bits you need 
 to store the state of each frame. If you use a char to represent the state
 of a frame, then you need one info frame for each FRAME_SIZE frames.
 
 A WORD ABOUT RELEASE_FRAMES():
 
 When we releae a frame, we only know its frame number. At the time
 of a frame's release, we don't know necessarily which pool it came
 from. Therefore, the function "release_frame" is static, i.e., 
 not associated with a particular frame pool.
 
 This problem is related to the lack of a so-called "placement delete" in
 C++. For a discussion of this see Stroustrup's FAQ:
 http://www.stroustrup.com/bs_faq2.html#placement-delete
 
 */
/*--------------------------------------------------------------------------*/


/*--------------------------------------------------------------------------*/
/* DEFINES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* INCLUDES */
/*--------------------------------------------------------------------------*/

#include "cont_frame_pool.H"
#include "console.H"
#include "utils.H"
#include "assert.H"

/*--------------------------------------------------------------------------*/
/* DATA STRUCTURES */
/*--------------------------------------------------------------------------*/

FramePoolNode* head = nullptr;
FramePoolNode* tail = nullptr;

/*--------------------------------------------------------------------------*/
/* CONSTANTS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* FORWARDS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* METHODS FOR CLASS   C o n t F r a m e P o o l */
/*--------------------------------------------------------------------------*/

ContFramePool::ContFramePool(unsigned long _base_frame_no,
                             unsigned long _n_frames,
                             unsigned long _info_frame_no)
: base_frame_no(_base_frame_no), nframes(_n_frames), info_frame_no(_info_frame_no){
    // Ensure the number of frames fits within physical memory limits
    assert(_n_frames <= FRAME_SIZE * 8);

    // Calculate the number of info frames needed
    unsigned long info_frames_needed = needed_info_frames(_n_frames);

    // Ensure the _info_frame_no is set, or use base_frame_no to handle info frames
    if (_info_frame_no == 0) {
        // If info_frame_no is not provided, allocate info frames at the base frame location
        _info_frame_no = _base_frame_no;
        base_frame_no += info_frames_needed; 
    }

    // Set the bitmap pointer to the correct location within the frame pool for info frames
    bitmap = reinterpret_cast<unsigned char*>(_info_frame_no * FRAME_SIZE);
    

    // Initialize all frames to free
    for (unsigned long i = 0; i < _n_frames; ++i) {
        set_state(i,FrameState::Free);
    }
    
    add_frame_pool(this);

    Console::puts("Constructor: Contiguous Frame Pool initialized\n");

    // Mark the management frame (info_frame_no) as used if needed
    if (info_frame_no != 0) {
        set_state(info_frame_no - base_frame_no, FrameState::Used);
    }

    // TODO: IMPLEMENTATION NEEEDED!
    // Console::puts("ContframePool::constructor not implemented!\n");
    // assert(false);
    // return 0;

}



// Destructor implementation
ContFramePool::~ContFramePool() {
    remove_frame_pool(this);
}

// Set the state of a specific frame
void ContFramePool::set_state(unsigned long _frame_no, FrameState _state) {
    // Calculate which byte in the bitmap array contains the two bits for this frame
    unsigned int bitmap_index = (_frame_no * 2) / 8;
    
    // Calculate the bit offset within that byte for the two bits representing the frame
    unsigned int bit_offset = (_frame_no * 2) % 8;

    // Create a mask for the two bits we want to modify
    unsigned char mask = 0x3 << bit_offset; 

    // Clear the two bits (set them to 00, free state) before setting new state
    bitmap[bitmap_index] &= ~mask;

    // Set the new state by ORing the appropriate value with the mask
    switch(_state) {
        case FrameState::Free:
            // No need to do anything since we cleared the bits to 00 (free)
            break;
        case FrameState::Used:
            bitmap[bitmap_index] |= (0x1 << bit_offset); 
            break;
        case FrameState::HoS:
            bitmap[bitmap_index] |= (0x2 << bit_offset); 
            break;
    }

    // Log the action
    // Console::puts("set_state: Marked frame ");
    // Console::putui(_frame_no);
    // Console::puts(" as ");
    
    // // Print the state being set
    // switch (get_state(_frame_no)) {
    //     case FrameState::Free: Console::puts("Free\n"); break;
    //     case FrameState::Used: Console::puts("Used\n"); break;
    //     case FrameState::HoS: Console::puts("HoS\n"); break;
    // }

    // Log the current state of the byte after modification
    // Console::puts("set_state: Byte ");
    // Console::putui(byte_index);
    // Console::puts(" after setting: ");
    // Console::putui(bitmap[byte_index]);
    // Console::puts("\n");

    // TODO: IMPLEMENTATION NEEEDED!
    // Console::puts("ContframePool::set_state not implemented!\n");
    // assert(false);
    // return 0;
}

// Get the state of a specific frame
ContFramePool::FrameState ContFramePool::get_state(unsigned long _frame_no) {
    

    unsigned long byte_index = (_frame_no * 2) / 8;  // 4 frames per byte
    unsigned int bit_offset = (_frame_no * 2) % 8; // 2 bits per frame

    // Extract the 2 bits that represent the frame's state
    unsigned char bits = (bitmap[byte_index] >> bit_offset) & 0b11;

    return static_cast<FrameState>(bits);
    // TODO: IMPLEMENTATION NEEEDED!
    // Console::puts("ContframePool::get_state not implemented!\n");
    // assert(false);
    // return 0;
}

void add_frame_pool(ContFramePool* pool) {
    if (next_free_node >= MAX_FRAME_POOLS) {
        Console::puts("Error: No more frame pool nodes available.\n");
        return;
    }

    FramePoolNode* new_node = &framePoolNodes[next_free_node];
    next_free_node++;
    new_node->pool = pool;
    new_node->prev = nullptr;
    new_node->next = head;

    if (head != nullptr) {
        head->prev = new_node;
    } else {
        tail = new_node;
    }
    
    head = new_node;
}

void remove_frame_pool(ContFramePool* pool) {
    FramePoolNode* current = head;

    while (current != nullptr) {
        if (current->pool == pool) {
            if (current->prev != nullptr) {
                current->prev->next = current->next;
            } else {
                // If it's the head node
                head = current->next;
            }

            if (current->next != nullptr) {
                current->next->prev = current->prev;
            } else {
                // If it's the tail node
                head = current->prev;
            }

            next_free_node--;
            *current = framePoolNodes[next_free_node];

            break;
        }

        current = current->next;
    }
}



unsigned long ContFramePool::get_frames(unsigned int _n_frames)
{
    // Ensure the request is valid
    if (_n_frames == 0 || _n_frames > nframes) {
        Console::puts("get_frames: invalid request\n");
        return 0; 
    }

    // Console::puts("get_frames: Starting to search for contiguous free frames.\n");
    // Console::putui(_n_frames);
    // Console::puts(" frames requested.\n");

    // Iterate over all frames to find a block of contiguous free frames
    for (unsigned long i = 0; i <= nframes - _n_frames; i++) {
        // Console::puts("get_frames: Checking starting frame ");
        // Console::putui(i);
        // Console::puts("\n");

        bool found_block = true;

        // Check if the next _n_frames frames are all free
        for (unsigned long j = 0; j < _n_frames; j++) {
            if (get_state(i + j) != FrameState::Free) {
                // Console::puts("get_frames: Frame ");
                // Console::putui(i + j);
                // Console::puts(" is not free. Skipping.\n");

                found_block = false;
                //i = i + j;
                break;
            }
            else {
                // Console::puts("get_frames: Frame ");
                // Console::putui(i + j);
                // Console::puts(" is free.\n");
            }
        }
        // If a block of contiguous free frames is found
        if (found_block) {

            // Console::puts("get_frames: Found a block of ");
            // Console::putui(_n_frames);
            // Console::puts(" free frames starting at ");
            // Console::putui(i);
            // Console::puts("\n");

            // Mark the first frame as the hos
            
            set_state(i, FrameState::HoS);
            // Console::puts("get_frames: Marked frame ");
            // Console::putui(i);
            // Console::puts(" as HoS: ");
            
            // // Print the state being set
            // switch (get_state(i)) {
            //     case FrameState::Free: Console::puts("Free\n"); break;
            //     case FrameState::Used: Console::puts("Used\n"); break;
            //     case FrameState::HoS: Console::puts("HoS\n"); break;
            // }   

            // Mark the remaining frames as used
            for (unsigned long j = 1; j < _n_frames; j++) {
                set_state(i + j, FrameState::Used);
                // Console::puts("get_frames: Marked frame ");
                // Console::putui(i+j);
                // Console::puts(" as Used: ");
                
                // Print the state being set
                // switch (get_state(i+j)) {
                //     case FrameState::Free: Console::puts("Free\n"); break;
                //     case FrameState::Used: Console::puts("Used\n"); break;
                //     case FrameState::HoS: Console::puts("HoS\n"); break;
                // }   
            }



            // Console::puts("get_frames: Printing updated bitmap section...\n");

            // unsigned long start_byte = i / 4; // 4 frames per byte, 2 bits per frame
            // unsigned long end_byte = (i + _n_frames - 1) / 4;

            // for (unsigned long byte_idx = start_byte; byte_idx <= end_byte; byte_idx++) {
            //     Console::puts("Byte ");
            //     Console::putui(byte_idx);
            //     Console::puts(": ");
                
            //     // Print each bit in the byte in binary format (from bit 7 to 0)
            //     unsigned char byte = bitmap[byte_idx];
            //     for (int bit = 7; bit >= 0; --bit) {
            //         Console::putch((byte & (1 << bit)) ? '1' : '0');
            //     }

            //     Console::puts("  Frames: ");
                
            //     // Print frame states in the byte
            //     for (int frame_offset = 0; frame_offset < 4; frame_offset++) {
            //         unsigned long frame_number = byte_idx * 4 + frame_offset;
            //         if (frame_number >= i && frame_number < i + _n_frames) {
            //             Console::putui(frame_number);
            //             Console::puts(": ");
            //             switch (get_state(frame_number)) {
            //                 case FrameState::Free:
            //                     Console::puts("Free ");
            //                     break;
            //                 case FrameState::Used:
            //                     Console::puts("Used ");
            //                     break;
            //                 case FrameState::HoS:
            //                     Console::puts("HoS ");
            //                     break;
            //                 default:
            //                     Console::puts("Unknown ");
            //                     break;
            //             }
            //         }
            //     }
            //     Console::puts("\n");
            // }

            // Return the first frame of the allocated block
            //Console::puts("get_frames: frames successfully allocated \n");

            // Console::puts("Bitmap after allocation:\n");
            // for (unsigned long i = 80; i <= 100; ++i) {
            //     Console::puts("Frame ");
            //     Console::putui(i);
            //     Console::puts(": ");
            //     switch (get_state(i)) {
            //         case FrameState::Free: Console::puts("Free\n"); break;
            //         case FrameState::Used: Console::puts("Used\n"); break;
            //         case FrameState::HoS: Console::puts("HoS\n"); break;
            //     }
            // }
            return base_frame_no + i;
        }
    }

    //If no contiguous block found
    return 0;
    
    // TODO: IMPLEMENTATION NEEEDED!
    // Console::puts("ContframePool::get_frames not implemented!\n");
    // assert(false);
    // return 0;
}

void ContFramePool::mark_inaccessible(unsigned long _base_frame_no,
                                      unsigned long _n_frames)
{
    // Validate inputs
    if ((_base_frame_no < base_frame_no) || (_base_frame_no + _n_frames > base_frame_no + nframes)) {
        // Out of bounds
        Console::puts("Error: The range to mark as inaccessible is out of bounds.\n");
        return;
    }

    unsigned long start_index = _base_frame_no;

    //set the head of sequence and increment index
    set_state(start_index, FrameState::HoS);
    start_index++;

    // Iterate through the range and mark frames as used
    for (unsigned long i = start_index; i < start_index + _n_frames; i++) {
        // Set the frame as used 
        set_state(i, FrameState::Used);
    
    }

    // TODO: IMPLEMENTATION NEEEDED!
    // Console::puts("ContframePool::mark_inaccessible not implemented!\n");
    // assert(false);
}

void ContFramePool::release_frames(unsigned long _first_frame_no)
{
    FramePoolNode* current = head;
    // Console::puts("Releasing frames starting at ");
    // Console::putui(_first_frame_no);
    // Console::puts("\n");

    // Traverse the list to find the correct frame pool
    while (current != nullptr) {
        ContFramePool* current_pool = current->pool;

        // Console::puts("Checking pool with frames from ");
        // Console::putui(current_pool->base_frame_no);
        // Console::puts(" to ");
        // Console::putui(current_pool->base_frame_no + current_pool->nframes - 1);
        // Console::puts("\n");

        // Check if the frame belongs to this pool
        if (_first_frame_no >= current_pool->base_frame_no &&
            _first_frame_no < current_pool->base_frame_no + current_pool->nframes) {

            // Console::puts("Found correct frame pool\n");
            
            unsigned long index = _first_frame_no-current_pool->base_frame_no;

            // Console::puts("Checking if state of frame ");
            // Console::putui(index);
            // Console::puts(" is Hos: ");
            // FrameState state = current_pool->get_state(index);
            // switch (state) {
            //     case FrameState::Free: Console::puts("Free\n"); break;
            //     case FrameState::Used: Console::puts("Used\n"); break;
            //     case FrameState::HoS: Console::puts("HoS\n"); break;
            // }

            // Make sure the frame is the head of a sequence 
            if (current_pool->get_state(index) != ContFramePool::FrameState::HoS) {
                Console::puts("Error: Frame ");
                Console::putui(_first_frame_no);
                Console::puts(" is not HoS. It is: ");
                FrameState state = current_pool->get_state(index);
                switch (state) {
                    case FrameState::Free: Console::puts("Free\n"); break;
                    case FrameState::Used: Console::puts("Used\n"); break;
                    case FrameState::HoS: Console::puts("HoS\n"); break;
                }

                return;
            }


            //set head frame to free
            current_pool->set_state(index,ContFramePool::FrameState::Free);
            index++;

            // Console::puts("Releasing frame ");
            // Console::putui(index);
            // Console::puts("\n");

            //loop until new head of sequence is found or end of frame pool is reached
            while (index < current_pool->nframes && 
                current_pool->get_state(index) != ContFramePool::FrameState::HoS && 
                current_pool->get_state(index) != ContFramePool::FrameState::Free) {      

                //set current frame to free
                current_pool->set_state(index, ContFramePool::FrameState::Free);
                index++;
                // Console::puts("Releasing frame ");
                // Console::putui(index);
                // Console::puts("\n");
            }
            // Exit after successful release
            Console::puts("release_frames: Frames released successfully \n");
            return;  
        }

        current = current->next; // Move to the next pool in the list
    }
    // frame not found in any pool
    Console::puts("release_frames Error: Frame not found in any pool.");

    // TODO: IMPLEMENTATION NEEEDED!
    // Console::puts("ContframePool::release_frames not implemented!\n");
    // assert(false);
    
}

unsigned long ContFramePool::needed_info_frames(unsigned long _n_frames)
{
    // TODO: IMPLEMENTATION NEEEDED!
    // Console::puts("ContframePool::need_info_frames not implemented!\n");
    // assert(false);
    // return 0;

     // Each frame requires 2 bits, so total bits needed
    unsigned long total_bits_needed = _n_frames * 2;

    // Each frame in the info pool can manage 32k bits (4096 bytes * 8 bits/byte)
    const unsigned long bits_per_info_frame = 4096 * 8;

    // Calculate the number of frames needed to store all the 2-bit states
    unsigned long info_frames_needed = total_bits_needed / bits_per_info_frame;

    // Add 1 if there's any remainder (i.e., if some bits are left over)
    if (total_bits_needed % bits_per_info_frame > 0) {
        info_frames_needed++;
    }

    return info_frames_needed;
}



/* Copyright (c) 2012 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef VM_H
#define VM_H

//------------------------------------------------------------------------------
struct region_info_t
{
    void*       base;
    size_t      size;
    unsigned    protect;
};

//------------------------------------------------------------------------------
extern void* g_current_proc;

//------------------------------------------------------------------------------
void*   get_alloc_base(void* addr);
void    get_region_info(void* addr, struct region_info_t* region_info);
void    set_region_write_state(struct region_info_t* region_info, int state);
int     write_vm(void* proc_handle, void* dest, const void* src, size_t size);
int     read_vm(void* proc_handle, void* dest, const void* src, size_t size);

#endif // VM_H

// vim: expandtab

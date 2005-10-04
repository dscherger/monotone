/*************************************************
* Pooling Allocator Source File                  *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/mem_pool.h>
#include <botan/conf.h>
#include <botan/util.h>

namespace Botan {

/*************************************************
* Pooling_Allocator Constructor                  *
*************************************************/
Pooling_Allocator::Pooling_Allocator(u32bit size) :
   PREF_SIZE(size ? size : Config::get_u32bit("base/memory_chunk")),
   ALIGN_TO(16)
   {
   if(PREF_SIZE == 0)
      throw Internal_Error("The base/memory_chunk option is unset");
   lock = get_mutex();
   initialized = destroyed = false;
   defrag_counter = 0;
   }

/*************************************************
* Pooling_Allocator Destructor                   *
*************************************************/
Pooling_Allocator::~Pooling_Allocator()
   {
   delete lock;
   if(!initialized)
      throw Invalid_State("Pooling_Allocator: Was never initialized");
   if(!destroyed)
      throw Invalid_State("Pooling_Allocator: Never released memory");
   }

/*************************************************
* Allocate some initial buffers                  *
*************************************************/
void Pooling_Allocator::init()
   {
   if(PREF_SIZE >= 64 && prealloc_bytes())
      {
      u32bit allocated = 0;
      while(allocated < prealloc_bytes())
         {
         void* ptr = 0;
         try {
            ptr = alloc_block(PREF_SIZE);
            allocated += PREF_SIZE;
         }
         catch(Exception) {}

         if(!ptr)
            break;

         real_mem.push_back(Buffer(ptr, PREF_SIZE, false));
         }
      }

   initialized = true;
   }

/*************************************************
* Free all remaining memory                      *
*************************************************/
void Pooling_Allocator::destroy()
   {
   if(!initialized)
      throw Invalid_State("Pooling_Allocator::destroy(): Never initialized");
   if(destroyed)
      throw Invalid_State("Pooling_Allocator::destroy(): Already destroyed");

   destroyed = true;
   for(u32bit j = 0; j != real_mem.size(); j++)
      dealloc_block(real_mem[j].buf, real_mem[j].length);
   }

/*************************************************
* Buffer Comparison                              *
*************************************************/
bool Pooling_Allocator::is_empty_buffer(const Buffer& block)
   {
   return (block.length == 0);
   }

/*************************************************
* Return true if these buffers are contiguous    *
*************************************************/
bool Pooling_Allocator::are_contiguous(const Buffer& a, const Buffer& b)
   {
   if((const byte*)a.buf + a.length == (const byte*)b.buf)
      return true;
   return false;
   }

/*************************************************
* See if two free blocks are from the same block *
*************************************************/
bool Pooling_Allocator::same_buffer(Buffer& a, Buffer& b) const
   {
   return (find_block(a.buf) == find_block(b.buf));
   }

/*************************************************
* Find the block containing this memory          *
*************************************************/
u32bit Pooling_Allocator::find_block(void* addr) const
   {
   for(u32bit j = 0; j != real_mem.size(); j++)
      {
      const byte* buf_addr = (const byte*)real_mem[j].buf;
      if(buf_addr <= (byte*)addr &&
         (byte*)addr < buf_addr + real_mem[j].length)
         return j;
      }
   throw Internal_Error("Pooling_Allocator::find_block: no buffer found");
   }

/*************************************************
* Remove empty buffers from list                 *
*************************************************/
void Pooling_Allocator::remove_empty_buffers(std::vector<Buffer>& list) const
   {
   std::vector<Buffer>::iterator empty;

   empty = std::find_if(list.begin(), list.end(), is_empty_buffer);
   while(empty != list.end())
      {
      list.erase(empty);
      empty = std::find_if(list.begin(), list.end(), is_empty_buffer);
      }
   }

/*************************************************
* Allocation                                     *
*************************************************/
void* Pooling_Allocator::allocate(u32bit n) const
   {
   struct Memory_Exhaustion : public Exception
      {
      Memory_Exhaustion() :
         Exception("Pooling_Allocator: Ran out of memory") {}
      };

   if(n == 0) return 0;
   n = round_up(n, ALIGN_TO);

   Mutex_Holder holder(lock);

   void* new_buf = find_free_block(n);
   if(new_buf)
      return alloc_hook(new_buf, n);

   Buffer block;
   block.length = ((n > PREF_SIZE) ? n : PREF_SIZE);
   block.buf = get_block(block.length);
   if(!block.buf)
      throw Memory_Exhaustion();
   free_list.push_back(block);

   new_buf = find_free_block(n);
   if(new_buf)
      return alloc_hook(new_buf, n);

   throw Memory_Exhaustion();
   }

/*************************************************
* Deallocation                                   *
*************************************************/
void Pooling_Allocator::deallocate(void* ptr, u32bit n) const
   {
   const u32bit RUNS_TO_DEFRAGS = 16;

   if(ptr == 0 || n == 0) return;

   n = round_up(n, ALIGN_TO);
   std::memset(ptr, 0, n);

   Mutex_Holder holder(lock);

   dealloc_hook(ptr, n);

   free_list.push_back(Buffer(ptr, n));
   if(free_list.size() >= 2)
      std::inplace_merge(free_list.begin(), free_list.end() - 1,
                         free_list.end());

   defrag_counter = (defrag_counter + 1) % RUNS_TO_DEFRAGS;
   if(defrag_counter == 0)
      {
      for(u32bit j = 0; j != free_list.size(); j++)
         {
         bool erase = false;
         if(free_list[j].buf == 0) continue;

         for(u32bit k = 0; k != real_mem.size(); k++)
            if(free_list[j].buf == real_mem[k].buf &&
               free_list[j].length == real_mem[k].length)
               erase = true;

         if(erase)
            {
            const u32bit buf = find_block(free_list[j].buf);
            free_block(real_mem[buf].buf, real_mem[buf].length);
            free_list[j].buf = 0;
            free_list[j].length = 0;
            }
         }

      defrag_free_list();
      }
   }

/*************************************************
* Handle Allocating New Memory                   *
*************************************************/
void* Pooling_Allocator::get_block(u32bit n) const
   {
   for(u32bit j = 0; j != real_mem.size(); j++)
      if(!real_mem[j].in_use && real_mem[j].length == n)
         {
         real_mem[j].in_use = true;
         return real_mem[j].buf;
         }

   void* ptr = 0;
   try {
      ptr = alloc_block(n);
   }
   catch(Exception) {}

   if(ptr)
      real_mem.push_back(Buffer(ptr, n, true));
   return ptr;
   }

/*************************************************
* Handle Deallocating Memory                     *
*************************************************/
void Pooling_Allocator::free_block(void* ptr, u32bit n) const
   {
   if(!ptr) return;

   u32bit free_space = 0;
   for(u32bit j = 0; j != real_mem.size(); j++)
      if(!real_mem[j].in_use)
         free_space += real_mem[j].length;

   bool free_this_block = false;
   if(free_space > keep_free())
      free_this_block = true;

   for(u32bit j = 0; j != real_mem.size(); j++)
      if(real_mem[j].buf == ptr)
         {
         if(!real_mem[j].in_use || real_mem[j].length != n)
            throw Internal_Error("Pooling_Allocator: Size mismatch in free");

         if(free_this_block)
            {
            dealloc_block(real_mem[j].buf, real_mem[j].length);
            real_mem[j].buf = 0;
            real_mem[j].length = 0;
            }
         else
            real_mem[j].in_use = false;

         return;
         }

   remove_empty_buffers(real_mem);

   throw Internal_Error("Pooling_Allocator: Unknown pointer was freed");
   }

/*************************************************
* Defragment the free list                       *
*************************************************/
void Pooling_Allocator::defrag_free_list() const
   {
   if(free_list.size() < 2) return;

   for(u32bit j = 0; j != free_list.size(); j++)
      {
      if(free_list[j].length == 0)
         continue;

      if(j > 0 &&
         are_contiguous(free_list[j-1], free_list[j]) &&
         same_buffer(free_list[j-1], free_list[j]))
         {
         free_list[j].buf = free_list[j-1].buf;
         free_list[j].length += free_list[j-1].length;
         free_list[j-1].length = 0;
         }

      if(j < free_list.size() - 1 &&
         are_contiguous(free_list[j], free_list[j+1]) &&
         same_buffer(free_list[j], free_list[j+1]))
         {
         free_list[j+1].buf = free_list[j].buf;
         free_list[j+1].length += free_list[j].length;
         free_list[j].length = 0;
         }
      }
   remove_empty_buffers(free_list);
   }

/*************************************************
* Find a block on the free list                  *
*************************************************/
void* Pooling_Allocator::find_free_block(u32bit n) const
   {
   void* retval = 0;

   for(u32bit j = 0; j != free_list.size(); j++)
      if(free_list[j].length >= n)
         {
         retval = free_list[j].buf;

         if(free_list[j].length == n)
            free_list.erase(free_list.begin() + j);
         else if(free_list[j].length > n)
            {
            free_list[j].length -= n;
            free_list[j].buf = ((byte*)free_list[j].buf) + n;
            }
         break;
         }

   return retval;
   }

/*************************************************
* Allocation hook for debugging                  *
*************************************************/
void* Pooling_Allocator::alloc_hook(void* ptr, u32bit) const
   {
   return ptr;
   }

/*************************************************
* Deallocation hook for debugging                *
*************************************************/
void Pooling_Allocator::dealloc_hook(void*, u32bit) const
   {
   }

/*************************************************
* Run internal consistency checks                *
*************************************************/
void Pooling_Allocator::consistency_check() const
   {
   for(u32bit j = 0; j != free_list.size(); j++)
      {
      const byte* byte_buf = (const byte*)free_list[j].buf;
      const u32bit length = free_list[j].length;

      for(u32bit k = 0; k != length; k++)
         if(byte_buf[k])
            throw Internal_Error("Pooling_Allocator: free list corrupted");
      }
   }

}

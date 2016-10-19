/*

Copyright (c) 2006-2016, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/config.hpp"

#include "libtorrent/assert.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"

#include <limits>

namespace libtorrent
{
#if !TORRENT_64BIT
	namespace {
		std::int64_t const max_32bit_mapping_size = 16 * 1024 * 1024;

		struct iovec_gap
		{
			std::int64_t offset;
			void* base;
			std::size_t len;
		};
	}
#endif

#if TORRENT_64BIT
	pool_file::pool_file(file_pool& pool)
		: m_file(std::make_shared<file>())
	{
		TORRENT_UNUSED(pool);
	}
#else
	pool_file::pool_file(file_pool& pool)
		: m_file(std::make_shared<file>())
		, m_pool(pool)
	{}
#endif

#if !TORRENT_64BIT
	pool_file::lru_mapping::lru_mapping(file_mapping_handle handle)
		: mapping_ptr(handle), last_use(aux::time_now())
	{}
#endif

	std::int64_t pool_file::writev(std::int64_t file_offset, span<file::iovec_t const> bufs
		, error_code& ec, int flags)
	{
#if TORRENT_64BIT
		auto mapping = m_mapping_ptr;
		std::size_t write_end = bufs_size(bufs) + file_offset;
		if (!mapping || write_end > mapping->size())
		{
			std::lock_guard<std::mutex> l(m_mutex);
			std::size_t file_size = get_size(ec);
			TORRENT_ASSERT(!ec);
			if (ec) return m_file->writev(file_offset, bufs, ec, flags);
			if (file_size < write_end) set_size_impl(write_end, ec);
			TORRENT_ASSERT(!ec);
			if (ec) return m_file->writev(file_offset, bufs, ec, flags);
			mapping = m_mapping_ptr;
			if (!mapping) mapping = map_op();
			TORRENT_ASSERT(mapping);
			if (!mapping)
				return m_file->writev(file_offset, bufs, ec, flags);
			TORRENT_ASSERT(mapping->size() >= write_end);
		}
		std::size_t written = 0;
		char* data = reinterpret_cast<char*>(mapping->data()) + file_offset;
		std::size_t map_size = mapping->size();
		for (auto buf : bufs)
		{
			TORRENT_ASSERT(file_offset + written + buf.iov_len <= map_size);
			//std::size_t cpy_len = (std::min)(buf.iov_len, map_size - file_offset - written);
			std::memcpy(data, buf.iov_base, buf.iov_len);
			written += buf.iov_len;
			data += buf.iov_len;
		}
		return written;
#else
		std::lock_guard<std::mutex> l(m_mutex);

		auto buf = bufs.begin();
		auto map = m_mappings.begin();

		for (;map != m_mappings.end(); ++map)
		{
			auto const& mapping = map->mapping_ptr;
			std::int64_t map_end = map->mapping_ptr->offset() + std::int64_t(map->mapping_ptr->size());
			if (map_end >= file_offset)
				break;
		}

		// regions that we don't have an existing mapping for
		std::vector<iovec_gap> gaps;
		std::int64_t buf_offset = file_offset;

		for (;map != m_mappings.end(); ++map)
		{
			auto const& mapping = map->mapping_ptr;
			std::int64_t const map_begin = map->mapping_ptr->offset();
			std::int64_t const map_end = map->mapping_ptr->offset() + std::int64_t(map->mapping_ptr->size());

			while (buf != bufs.end() && map_end > buf_offset)
			{
				if (buf_offset < map_begin)

			}
		}
#endif
	}

	std::int64_t pool_file::readv(std::int64_t file_offset, span<file::iovec_t const> bufs
		, error_code& ec, int flags)
	{
#if TORRENT_64BIT
		auto mapping = m_mapping_ptr;
		if (!mapping)
		{
			std::lock_guard<std::mutex> l(m_mutex);
			mapping = m_mapping_ptr;
			if (!mapping) mapping = map_op();
			TORRENT_ASSERT(mapping);
			// fall back to a regular write if the mmap failed
			if (!mapping)
				return m_file->readv(file_offset, bufs, ec, flags);
		}

		std::size_t read = 0;
		char* data = reinterpret_cast<char*>(mapping->data()) + file_offset;
		std::size_t map_size = mapping->size();
		for (auto buf : bufs)
		{
			if (file_offset + read >= map_size) break;
			std::size_t cpy_len = (std::min)(buf.iov_len, map_size - file_offset - read);
			std::memcpy(buf.iov_base, data, cpy_len);
			read += cpy_len;
			data += cpy_len;
		}
		return read;
#else
#endif
	}

#if TORRENT_64BIT
	file_mapping_handle pool_file::map_op()
	{
		error_code ec;
		std::int64_t size = get_size(ec);
		TORRENT_ASSERT(!ec);
		if (ec || size == 0)
		{
			m_mapping_ptr.reset();
			return file_mapping_handle();
		}
		auto mapping = m_file->map_region(handle(), 0, size, ec);
		m_mapping_ptr = mapping;
		TORRENT_ASSERT(!ec);
		return mapping;
	}
#else
	file_mapping_handle pool_file::map_op(std::int64_t offset, std::size_t len
		, error_code& ec)
	{
		if (sizeof(std::size_t) >= 8)
		{
			// on 64-bit systems things are very simple since we have plenty of address space
			// just map the whole file
			TORRENT_ASSERT(m_mappings.empty());
			m_mappings.push_back(m_file_ptr->map_region(0, get_size(ec), ec));
			if (ec) return file_mapping_handle();
			return m_mappings.back().mapping_ptr;
		}

		error_code ec;
		std::size_t mapping_size = (std::min)(get_size(ec), max_32bit_mapping_size);
		if (ec) return file_mapping_handle();

		m_pool.free_map_space(mapping_size);
	}
#endif

	file_pool::file_pool(int size)
		: m_size(size)
		, m_low_prio_io(true)
	{
	}

	file_pool::~file_pool() = default;

#ifdef TORRENT_WINDOWS
	void set_low_priority(file_handle const& f)
	{
		// file prio is only supported on vista and up
		// so load the functions dynamically
		typedef enum {
			FileBasicInfo,
			FileStandardInfo,
			FileNameInfo,
			FileRenameInfo,
			FileDispositionInfo,
			FileAllocationInfo,
			FileEndOfFileInfo,
			FileStreamInfo,
			FileCompressionInfo,
			FileAttributeTagInfo,
			FileIdBothDirectoryInfo,
			FileIdBothDirectoryRestartInfo,
			FileIoPriorityHintInfo,
			FileRemoteProtocolInfo,
			MaximumFileInfoByHandleClass
		} FILE_INFO_BY_HANDLE_CLASS_LOCAL;

		typedef enum {
			IoPriorityHintVeryLow = 0,
			IoPriorityHintLow,
			IoPriorityHintNormal,
			MaximumIoPriorityHintType
		} PRIORITY_HINT_LOCAL;

		typedef struct {
			PRIORITY_HINT_LOCAL PriorityHint;
		} FILE_IO_PRIORITY_HINT_INFO_LOCAL;

		typedef BOOL (WINAPI *SetFileInformationByHandle_t)(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS_LOCAL FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize);
		static SetFileInformationByHandle_t SetFileInformationByHandle = nullptr;

		static bool failed_kernel_load = false;

		if (failed_kernel_load) return;

		if (SetFileInformationByHandle == nullptr)
		{
			HMODULE kernel32 = LoadLibraryA("kernel32.dll");
			if (kernel32 == nullptr)
			{
				failed_kernel_load = true;
				return;
			}

			SetFileInformationByHandle = (SetFileInformationByHandle_t)GetProcAddress(kernel32, "SetFileInformationByHandle");
			if (SetFileInformationByHandle == nullptr)
			{
				failed_kernel_load = true;
				return;
			}
		}

		TORRENT_ASSERT(SetFileInformationByHandle);

		FILE_IO_PRIORITY_HINT_INFO_LOCAL io_hint;
		io_hint.PriorityHint = IoPriorityHintLow;
		SetFileInformationByHandle(f->native_handle(),
			FileIoPriorityHintInfo, &io_hint, sizeof(io_hint));
	}
#endif // TORRENT_WINDOWS

	pool_file_handle file_pool::open_file(void* st, std::string const& p
		, int file_index, file_storage const& fs, int m, error_code& ec)
	{
		// potentially used to hold a reference to a file object that's
		// about to be destructed. If we have such object we assign it to
		// this member to be destructed after we release the std::mutex. On some
		// operating systems (such as OSX) closing a file may take a long
		// time. We don't want to hold the std::mutex for that.
		pool_file_handle defer_destruction;

		std::unique_lock<std::mutex> l(m_mutex);

#if TORRENT_USE_ASSERTS
		// we're not allowed to open a file
		// from a deleted storage!
		TORRENT_ASSERT(std::find(m_deleted_storages.begin(), m_deleted_storages.end()
			, std::make_pair(fs.name(), static_cast<void const*>(&fs)))
			== m_deleted_storages.end());
#endif

		TORRENT_ASSERT(st != nullptr);
		TORRENT_ASSERT(is_complete(p));
		TORRENT_ASSERT((m & file::rw_mask) == file::read_only
			|| (m & file::rw_mask) == file::read_write);
		auto const i = m_files.find(std::make_pair(st, file_index));
		if (i != m_files.end())
		{
			lru_file_entry& e = i->second;
			e.last_use = aux::time_now();

			if (e.key != st && ((e.mode & file::rw_mask) != file::read_only
				|| (m & file::rw_mask) != file::read_only))
			{
				// this means that another instance of the storage
				// is using the exact same file.
				ec = errors::file_collision;
				return pool_file_handle();
			}

			e.key = st;
			// if we asked for a file in write mode,
			// and the cached file is is not opened in
			// write mode, re-open it
			if ((((e.mode & file::rw_mask) != file::read_write)
				&& ((m & file::rw_mask) == file::read_write))
				|| (e.mode & file::random_access) != (m & file::random_access))
			{
				// close the file before we open it with
				// the new read/write privileges, since windows may
				// file opening a file twice. However, since there may
				// be outstanding operations on it, we can't close the
				// file, we can only delete our reference to it.
				// if this is the only reference to the file, it will be closed
				defer_destruction = e.file_ptr;
				e.file_ptr = std::make_shared<pool_file>(*this);

				std::string full_path = fs.file_path(file_index, p);
				if (!e.file_ptr->open(full_path, m, ec))
				{
					m_files.erase(i);
					return pool_file_handle();
				}
#ifdef TORRENT_WINDOWS
				if (m_low_prio_io)
					set_low_priority(e.file_ptr->m_file);
#endif

				TORRENT_ASSERT(e.file_ptr->is_open());
				e.mode = m;
			}
			return e.file_ptr;
		}

		lru_file_entry e;
		e.file_ptr = std::make_shared<pool_file>(std::ref(*this));
		if (!e.file_ptr)
		{
			ec = error_code(boost::system::errc::not_enough_memory, generic_category());
			return pool_file_handle();
		}
		std::string full_path = fs.file_path(file_index, p);
		if (!e.file_ptr->open(full_path, m, ec))
			return pool_file_handle();
#ifdef TORRENT_WINDOWS
		if (m_low_prio_io)
			set_low_priority(e.file_ptr->m_file);
#endif
		e.mode = m;
		e.key = st;
		m_files.insert(std::make_pair(std::make_pair(st, file_index), e));
		TORRENT_ASSERT(e.file_ptr->is_open());

		pool_file_handle file_ptr = e.file_ptr;

		// the file is not in our cache
		if (int(m_files.size()) >= m_size)
		{
			// the file cache is at its maximum size, close
			// the least recently used (lru) file from it
			remove_oldest(l);
		}
		return file_ptr;
	}

	std::vector<pool_file_status> file_pool::get_status(void* st) const
	{
		std::vector<pool_file_status> ret;
		{
			std::unique_lock<std::mutex> l(m_mutex);

			auto const start = m_files.lower_bound(std::make_pair(st, 0));
			auto const end = m_files.upper_bound(std::make_pair(st
				, std::numeric_limits<int>::max()));

			for (auto i = start; i != end; ++i)
				ret.push_back({i->first.second, i->second.mode, i->second.last_use});
		}
		return ret;
	}

	void file_pool::remove_oldest(std::unique_lock<std::mutex>& l)
	{
		using value_type = std::map<std::pair<void*, int>, lru_file_entry>::value_type;
		auto const i = std::min_element(m_files.begin(), m_files.end()
			, [] (value_type const& lhs, value_type const& rhs)
				{ return lhs.second.last_use < rhs.second.last_use; });
		if (i == m_files.end()) return;

		pool_file_handle file_ptr = i->second.file_ptr;
		m_files.erase(i);

		// closing a file may be long running operation (mac os x)
		l.unlock();
		file_ptr.reset();
		l.lock();
	}

#if !TORRENT_64BIT
	namespace {
		struct candidate_mapping
		{
			candidate_mapping(pool_file::mappings_t& v
				, pool_file::mappings_t::iterator m)
				: vec(v)
				, mapping(m)
			{}

			pool_file::mappings_t& vec;
			pool_file::mappings_t::iterator mapping;

			bool operator<(candidate_mapping const& o) const
			{ return mapping->last_use < o.mapping->last_use; }
		};
	}

	void file_pool::free_map_space(std::size_t needed)
	{
		std::lock_guard<std::mutex> l(m_mutex);

		TORRENT_ASSERT(m_mapped_size_max >= m_mapped_size);
		std::size_t const free_space = m_mapped_size_max - m_mapped_size;
		if (free_space > needed) return;
		std::size_t const to_free = needed - free_space;
		std::vector<candidate_mapping> lru;
		std::size_t candidate_bytes = 0;

		// first build a list of the minimum number of least recently used mappings
		// needed to free up at least the required space
		for (auto& f : m_files)
		{
			auto& mappings = f.second.file_ptr.m_mappings;
			for (auto m = mappings.begin(); m != mappings.end(); ++m)
			{
				candidate_mapping candidate(mappings, m);
				if (lru.empty() || candidate < lru.front())
				{
					lru.push_back(candidate);
					std::push_heap(lru.begin(), lru.end());
					candidate_bytes += m->mapping_ptr->size();
					while (candidate_bytes - lru.front().mapping->mapping_ptr->size() > to_free)
					{
						candidate_bytes -= lru.front().mapping->mapping_ptr->size();
						std::pop_heap(lru.begin(), lru.end());
					}
				}
			}
		}

		// sort by containing vector so we can remove all mappings in a vector
		// at once
		std::sort(lru.begin(), lru.end()
			, [](candidate_mapping const& l, candidate_mapping const& r)
				{ return &l.vec < &r.vec; });

		auto vec = &lru.front().vec;
		for (auto const& m : lru)
		{
			// mark mappings for deletion by clearing their shared_ptr then remove them when
			// we get to the next file or the end of the lru list
			m.mapping->mapping_ptr.reset();
			if (&m.vec != vec || &m.vec == &lru.back().vec)
			{
				auto new_end = std::remove_if(vec->begin(), vec->end()
					, [](pool_file::lru_mapping const& mapping)
						{ return !mapping.mapping_ptr; });
				vec->erase(new_end, vec->end());
				vec = &m.vec;
			}
		}
	}
#endif

	void file_pool::release(void* st, int file_index)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		auto const i = m_files.find(std::make_pair(st, file_index));
		if (i == m_files.end()) return;

		pool_file_handle file_ptr = i->second.file_ptr;
		m_files.erase(i);

		// closing a file may be long running operation (mac os x)
		l.unlock();
		file_ptr.reset();
	}

	// closes files belonging to the specified
	// storage. If 0 is passed, all files are closed
	void file_pool::release(void* st)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		if (st == nullptr)
		{
			std::map<std::pair<void*, int>, lru_file_entry> tmp;
			tmp.swap(m_files);
			l.unlock();
			return;
		}

		std::vector<pool_file_handle> to_close;
		for (auto i = m_files.begin(); i != m_files.end();)
		{
			if (i->second.key == st)
			{
				to_close.push_back(i->second.file_ptr);
				m_files.erase(i++);
			}
			else
				++i;
		}
		l.unlock();
		// the files are closed here
	}

#if TORRENT_USE_ASSERTS
	void file_pool::mark_deleted(file_storage const& fs)
	{
		std::unique_lock<std::mutex> l(m_mutex);
		m_deleted_storages.push_back(std::make_pair(fs.name()
			, static_cast<void const*>(&fs)));
		if(m_deleted_storages.size() > 100)
			m_deleted_storages.erase(m_deleted_storages.begin());
	}

	bool file_pool::assert_idle_files(void* st) const
	{
		std::unique_lock<std::mutex> l(m_mutex);

		for (auto const& i : m_files)
		{
			if (i.second.key == st && !i.second.file_ptr.unique())
				return false;
		}
		return true;
	}
#endif

	void file_pool::resize(int size)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(size > 0);

		if (size == m_size) return;
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		while (int(m_files.size()) > m_size)
			remove_oldest(l);
	}

}

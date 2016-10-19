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

#ifndef TORRENT_FILE_POOL_HPP
#define TORRENT_FILE_POOL_HPP

#include <map>
#include <mutex>
#include <vector>

#include <cstdint>
#define TORRENT_64BIT (SIZE_MAX >= UINT64_MAX)

#include "libtorrent/file.hpp"
#include "libtorrent/aux_/time.hpp"

namespace libtorrent
{
	class file_storage;

	struct pool_file_status
	{
		// the index of the file this entry refers to into the ``file_storage``
		// file list of this torrent. This starts indexing at 0.
		int file_index;

		// ``open_mode`` is a bitmask of the file flags this file is currently opened with. These
		// are the flags used in the ``file::open()`` function. This enum is defined as a member
		// of the ``file`` class.
		//
		// ::
		//
		// 	enum
		// 	{
		// 		read_only = 0,
		// 		write_only = 1,
		// 		read_write = 2,
		// 		rw_mask = 3,
		// 		no_buffer = 4,
		// 		sparse = 8,
		// 		no_atime = 16,
		// 		random_access = 32,
		// 		lock_file = 64,
		// 	};
		//
		// Note that the read/write mode is not a bitmask. The two least significant bits are used
		// to represent the read/write mode. Those bits can be masked out using the ``rw_mask`` constant.
		int open_mode;

		// a (high precision) timestamp of when the file was last used.
		time_point last_use;
	};

	struct file_pool;

	struct pool_file : boost::noncopyable
	{
		friend struct file_pool;

#if !TORRENT_64BIT
		struct lru_mapping
		{
			lru_mapping(file_mapping_handle handle);
			file_mapping_handle mapping_ptr;
			time_point last_use;
		};

		using mappings_t = std::vector<lru_mapping>;
#endif

		pool_file(file_pool& pool);

		bool set_size(std::int64_t size, error_code& ec)
		{
#if TORRENT_64BIT
			std::lock_guard<std::mutex> l(m_mutex);
			return set_size_impl(size, ec);
#else
			std::lock_guard<std::mutex> l(m_mutex);
			m_mappings.clear();
			return m_file->set_size(size, ec);
#endif
		}

#if TORRENT_64BIT
		bool set_size_impl(std::int64_t size, error_code& ec)
		{
			m_mapping_ptr.reset();
			auto result = m_file->set_size(size, ec);
			map_op();
			return result;
		}
#endif

		bool open(std::string const& p, int m, error_code& ec)
		{ return m_file->open(p, m, ec); }

		std::int64_t get_size(error_code& ec) const { return m_file->get_size(ec); }

		bool is_open() const { return m_file->is_open(); }
		int open_mode() const { return m_file->open_mode(); }

		std::int64_t writev(std::int64_t file_offset, span<file::iovec_t const> bufs
			, error_code& ec, int flags = 0);
		std::int64_t readv(std::int64_t file_offset, span<file::iovec_t const> bufs
			, error_code& ec, int flags = 0);

		handle_type native_handle() const { return m_file->native_handle(); }
		file_handle handle() { return m_file; }

	private:
		// this needs to be a handle (i.e. shared_ptr) to file rather than a direct
		// member to avoid creating a reference cycle between the mappings
		// and the file
		file_handle m_file;

		// 64-bit: ensures that resizing and remapping the file is atomic
		// so that an incomplete mapping isn't assigned to m_mapping_ptr
		// 32-bit: protects m_mappings from concurrent access
		std::mutex m_mutex;

#if TORRENT_64BIT
		file_mapping_handle map_op();

		file_mapping_handle m_mapping_ptr;
#else
		file_mapping_handle map_op(std::int64_t offset, std::size_t len, error_code& ec);

		file_pool& m_pool;
		mappings_t m_mappings;
#endif
	};

	using pool_file_handle = std::shared_ptr<pool_file>;

	// this is an internal cache of open file handles. It's primarily used by
	// storage_interface implementations. It provides semi weak guarantees of
	// not opening more file handles than specified. Given multiple threads,
	// each with the ability to lock a file handle (via smart pointer), there
	// may be windows where more file handles are open.
	struct TORRENT_EXPORT file_pool : boost::noncopyable
	{
		friend struct pool_file;

		// ``size`` specifies the number of allowed files handles
		// to hold open at any given time.
		explicit file_pool(int size = 40);
		~file_pool();

		// return an open file handle to file at ``file_index`` in the
		// file_storage ``fs`` opened at save path ``p``. ``m`` is the
		// file open mode (see file::open_mode_t).
		pool_file_handle open_file(void* st, std::string const& p
			, int file_index, file_storage const& fs, int m, error_code& ec);
		// release all files belonging to the specified storage_interface (``st``)
		// the overload that takes ``file_index`` releases only the file with
		// that index in storage ``st``.
		void release(void* st = nullptr);
		void release(void* st, int file_index);

		// update the allowed number of open file handles to ``size``.
		void resize(int size);

		// returns the current limit of number of allowed open file handles held
		// by the file_pool.
		int size_limit() const { return m_size; }

		// internal
		void set_low_prio_io(bool b) { m_low_prio_io = b; }
		std::vector<pool_file_status> get_status(void* st) const;

#if TORRENT_USE_ASSERTS
		bool assert_idle_files(void* st) const;

		// remember that this storage has had
		// its files deleted. We may not open any
		// files from it again
		void mark_deleted(file_storage const& fs);
#endif

	private:

		void remove_oldest(std::unique_lock<std::mutex>& l);
#if !TORRENT_64BIT
		void free_map_space(std::size_t needed);
#endif

		std::size_t m_mapped_size_max;
		std::size_t m_mapped_size;
		int m_size;
		bool m_low_prio_io;

		struct lru_file_entry
		{
			lru_file_entry() : key(0), last_use(aux::time_now()), mode(0) {}

			mutable pool_file_handle file_ptr;
			void* key;
			time_point last_use;
			int mode;
		};

		// maps storage pointer, file index pairs to the
		// lru entry for the file
		std::map<std::pair<void*, int>, lru_file_entry> m_files;
#if TORRENT_USE_ASSERTS
		std::vector<std::pair<std::string, void const*>> m_deleted_storages;
#endif
		mutable std::mutex m_mutex;
	};
}

#endif

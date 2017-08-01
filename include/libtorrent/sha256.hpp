// SHA-256. Adapted from LibTomCrypt. This code is Public Domain

#ifndef TORRENT_SHA256_HPP_INCLUDED
#define TORRENT_SHA256_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include <cstdint>

namespace libtorrent {

	struct sha256_ctx
	{
		std::uint64_t length;
		std::uint32_t state[8];
		std::uint32_t curlen;
		std::uint8_t buf[64];
	};

	TORRENT_EXTRA_EXPORT void SHA256_init(sha256_ctx& md);
	TORRENT_EXTRA_EXPORT void SHA256_update(sha256_ctx& md
		, std::uint8_t const* in, size_t len);
	TORRENT_EXTRA_EXPORT void SHA256_final(std::uint8_t* digest, sha256_ctx& md);
}

#endif

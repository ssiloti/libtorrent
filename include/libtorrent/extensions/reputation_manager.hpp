/*

Copyright (c) 2014, Steven Siloti
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

#ifndef REPUTATION_MANAGER_HPP_INCLUDED
#define REPUTATION_MANAGER_HPP_INCLUDED

#if !defined(TORRENT_DISABLE_EXTENSIONS) && !defined(TORRENT_DISABLE_REPUTATION_TRACKING)

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"

namespace libtorrent
{
	struct plugin;
	struct lt_identify_plugin;

	struct TORRENT_EXPORT bad_reputation_password : std::exception
	{
		virtual char const* what() const throw() { return "invalid reputation password"; }
	};

	struct TORRENT_EXPORT reputation_handle
	{
		reputation_handle(boost::shared_ptr<plugin> repman)
			: reputation_plugin(repman) {}

		// returns the client's average ratio at all peers it knows about
		// weighted by the client's own value for earch peer as an intermediary
		double indirect_ratio();
		boost::shared_ptr<plugin> reputation_plugin;
	};

	TORRENT_EXPORT reputation_handle create_reputation_plugin(lt_identify_plugin& identity
		, std::string const& storage_path
		, std::string const& sk_password);
}

#endif // TORRENT_DISABLE_EXTENSIONS

#endif // REPUTATION_MANAGER_HPP_INCLUDED

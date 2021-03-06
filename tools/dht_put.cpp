/*

Copyright (c) 2014, Arvid Norberg
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


#include "libtorrent/session.hpp"
#include "libtorrent/hex.hpp" // for from_hex
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp" // for bencode()
#include "libtorrent/kademlia/item.hpp" // for sign_mutable_item
#include "libtorrent/ed25519.hpp"

#include <functional>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <cstdlib>

using namespace libtorrent;
using namespace std::placeholders;
namespace lt = libtorrent;

#ifdef TORRENT_DISABLE_DHT

int main(int argc, char* argv[])
{
	std::fprintf(stderr, "not built with DHT support\n");
	return 1;
}

#else

void usage()
{
	std::fprintf(stderr,
		"USAGE:\ndht <command> <arg>\n\nCOMMANDS:\n"
		"get <hash>                - retrieves and prints out the immutable\n"
		"                            item stored under hash.\n"
		"put <string>              - puts the specified string as an immutable\n"
		"                            item onto the DHT. The resulting target hash\n"
		"gen-key <key-file>        - generate ed25519 keypair and save it in\n"
		"                            the specified file\n"
		"dump-key <key-file>       - dump ed25519 keypair from the specified key\n"
		"                            file.\n"
		"mput <key-file> <string>  - puts the specified string as a mutable\n"
		"                            object under the public key in key-file\n"
		"mget <public-key>         - get a mutable object under the specified\n"
		"                            public key\n"
		);
	exit(1);
}

alert* wait_for_alert(lt::session& s, int alert_type)
{
	alert* ret = NULL;
	bool found = false;
	while (!found)
	{
		s.wait_for_alert(seconds(5));

		std::vector<alert*> alerts;
		s.pop_alerts(&alerts);
		for (std::vector<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			if ((*i)->type() != alert_type)
			{
				static int spinner = 0;
				static const char anim[] = {'-', '\\', '|', '/'};
				std::printf("\r%c", anim[spinner]);
				std::fflush(stdout);
				spinner = (spinner + 1) & 3;
				//print some alerts?
				continue;
			}
			ret = *i;
			found = true;
		}
	}
	std::printf("\n");
	return ret;
}

void put_string(entry& e, std::array<char, 64>& sig, boost::uint64_t& seq
	, std::string const& salt, char const* public_key, char const* private_key
	, char const* str)
{
	using libtorrent::dht::sign_mutable_item;

	e = std::string(str);
	std::vector<char> buf;
	bencode(std::back_inserter(buf), e);
	++seq;
	sign_mutable_item(std::pair<char const*, int>(&buf[0], int(buf.size()))
		, std::pair<char const*, int>(&salt[0], int(salt.size()))
		, seq
		, public_key
		, private_key
		, sig.data());
}

void bootstrap(lt::session& s)
{
	std::printf("bootstrapping\n");
	wait_for_alert(s, dht_bootstrap_alert::alert_type);
	std::printf("bootstrap done.\n");
}

int dump_key(char *filename)
{
	FILE* f = std::fopen(filename, "rb+");
	if (f == NULL)
	{
		std::fprintf(stderr, "failed to open file \"%s\": (%d) %s\n"
			, filename, errno, strerror(errno));
		return 1;
	}

	unsigned char seed[32];
	int size = int(fread(seed, 1, 32, f));
	if (size != 32)
	{
		std::fprintf(stderr, "invalid key file.\n");
		return 1;
	}
	std::fclose(f);

	std::array<char, 32> public_key;
	std::array<char, 64> private_key;
	ed25519_create_keypair((unsigned char*)public_key.data()
		, (unsigned char*)private_key.data(), seed);

	std::printf("public key: %s\nprivate key: %s\n",
		to_hex(std::string(public_key.data(), public_key.size())).c_str(),
		to_hex(std::string(private_key.data(), private_key.size())).c_str());

	return 0;
}

int generate_key(char* filename)
{
	unsigned char seed[32];
	ed25519_create_seed(seed);

	FILE* f = std::fopen(filename, "wb+");
	if (f == NULL)
	{
		std::fprintf(stderr, "failed to open file for writing \"%s\": (%d) %s\n"
			, filename, errno, strerror(errno));
		return 1;
	}

	int size = int(std::fwrite(seed, 1, 32, f));
	if (size != 32)
	{
		std::fprintf(stderr, "failed to write key file.\n");
		return 1;
	}
	std::fclose(f);

	return 0;
}

void load_dht_state(lt::session& s)
{
	FILE* f = std::fopen(".dht", "rb");
	if (f == NULL) return;

	fseek(f, 0, SEEK_END);
	int size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size > 0)
	{
		std::vector<char> state;
		state.resize(size);
		fread(&state[0], 1, state.size(), f);

		bdecode_node e;
		error_code ec;
		bdecode(&state[0], &state[0] + state.size(), e, ec);
		if (ec)
			std::fprintf(stderr, "failed to parse .dht file: (%d) %s\n"
				, ec.value(), ec.message().c_str());
		else
		{
			std::printf("load dht state from .dht\n");
			s.load_state(e);
		}
	}
	std::fclose(f);
}


int save_dht_state(lt::session& s)
{
	entry e;
	s.save_state(e, session::save_dht_state);
	std::vector<char> state;
	bencode(std::back_inserter(state), e);
	FILE* f = std::fopen(".dht", "wb+");
	if (f == NULL)
	{
		std::fprintf(stderr, "failed to open file .dht for writing");
		return 1;
	}
	std::fwrite(&state[0], 1, state.size(), f);
	std::fclose(f);

	return 0;
}

int main(int argc, char* argv[])
{
	// skip pointer to self
	++argv;
	--argc;

	if (argc < 1) usage();

	if (strcmp(argv[0], "dump-key") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		return dump_key(argv[0]);
	}

	if (strcmp(argv[0], "gen-key") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		return generate_key(argv[0]);
	}

	settings_pack sett;
	sett.set_bool(settings_pack::enable_dht, false);
	sett.set_int(settings_pack::alert_mask, 0xffffffff);
	lt::session s(sett);

	s.add_dht_router(std::pair<std::string, int>("router.utorrent.com", 6881));
	sett.set_bool(settings_pack::enable_dht, true);
	s.apply_settings(sett);

	load_dht_state(s);

	if (strcmp(argv[0], "get") == 0)
	{
		++argv;
		--argc;
	
		if (argc < 1) usage();

		if (strlen(argv[0]) != 40)
		{
			std::fprintf(stderr, "the hash is expected to be 40 hex characters\n");
			usage();
		}
		sha1_hash target;
		bool ret = from_hex(argv[0], 40, (char*)&target[0]);
		if (!ret)
		{
			std::fprintf(stderr, "invalid hex encoding of target hash\n");
			return 1;
		}

		bootstrap(s);
		s.dht_get_item(target);

		std::printf("GET %s\n", to_hex(target.to_string()).c_str());

		alert* a = wait_for_alert(s, dht_immutable_item_alert::alert_type);

		dht_immutable_item_alert* item = alert_cast<dht_immutable_item_alert>(a);
		entry data;
		if (item)
			data.swap(item->item);

		std::printf("%s", data.to_string().c_str());
	}
	else if (strcmp(argv[0], "put") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		entry data;
		data = std::string(argv[0]);

		bootstrap(s);
		sha1_hash target = s.dht_put_item(data);

		std::printf("PUT %s\n", to_hex(target.to_string()).c_str());

		alert* a = wait_for_alert(s, dht_put_alert::alert_type);
		dht_put_alert* pa = alert_cast<dht_put_alert>(a);
		std::printf("%s\n", pa->message().c_str());
	}
	else if (strcmp(argv[0], "mput") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		FILE* f = std::fopen(argv[0], "rb+");
		if (f == NULL)
		{
			std::fprintf(stderr, "failed to open file \"%s\": (%d) %s\n"
				, argv[0], errno, strerror(errno));
			return 1;
		}

		unsigned char seed[32];
		fread(seed, 1, 32, f);
		std::fclose(f);

		++argv;
		--argc;
		if (argc < 1) usage();

		std::array<char, 32> public_key;
		std::array<char, 64> private_key;
		ed25519_create_keypair((unsigned char*)public_key.data()
			, (unsigned char*)private_key.data(), seed);

		bootstrap(s);
		s.dht_put_item(public_key, std::bind(&put_string, _1, _2, _3, _4
			, public_key.data(), private_key.data(), argv[0]));

		std::printf("MPUT publick key: %s\n", to_hex(std::string(public_key.data()
			, public_key.size())).c_str());

		alert* a = wait_for_alert(s, dht_put_alert::alert_type);
		dht_put_alert* pa = alert_cast<dht_put_alert>(a);
		std::printf("%s\n", pa->message().c_str());
	}
	else if (strcmp(argv[0], "mget") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		int len = int(strlen(argv[0]));
		if (len != 64)
		{
			std::fprintf(stderr, "public key is expected to be 64 hex digits\n");
			return 1;
		}
		std::array<char, 32> public_key;
		bool ret = from_hex(argv[0], len, &public_key[0]);
		if (!ret)
		{
			std::fprintf(stderr, "invalid hex encoding of public key\n");
			return 1;
		}

		bootstrap(s);
		s.dht_get_item(public_key);
		std::printf("MGET %s\n", argv[0]);

		bool authoritative = false;

		while (!authoritative)
		{
			alert* a = wait_for_alert(s, dht_mutable_item_alert::alert_type);

			dht_mutable_item_alert* item = alert_cast<dht_mutable_item_alert>(a);
			entry data;
			if (item)
				data.swap(item->item);

			authoritative = item->authoritative;
			std::printf("%s: %s", authoritative ? "auth" : "non-auth", data.to_string().c_str());
		}
	}
	else
	{
		usage();
	}

	return save_dht_state(s);
}

#endif

